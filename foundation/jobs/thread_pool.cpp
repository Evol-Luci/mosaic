#include "foundation/jobs/thread_pool.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <memory>
#include <utility>

namespace mosaic::foundation::jobs
{

ThreadPool::ThreadPool(unsigned thread_count)
{
    const unsigned n = std::max(1u, thread_count);
    workers_.reserve(n);
    for (unsigned i = 0; i < n; ++i)
    {
        workers_.emplace_back([this] { worker_loop(); });
    }
}

ThreadPool::~ThreadPool()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    cv_.notify_all();
    for (auto& worker : workers_)
    {
        worker.join();
    }
}

std::future<void> ThreadPool::submit(std::function<void()> task)
{
    // packaged_task is move-only; wrap in shared_ptr so the queued std::function
    // (which requires a copyable target) can hold it.
    auto packaged = std::make_shared<std::packaged_task<void()>>(std::move(task));
    std::future<void> future = packaged->get_future();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push([packaged] { (*packaged)(); });
    }
    cv_.notify_one();
    return future;
}

void ThreadPool::worker_loop()
{
    for (;;)
    {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
            if (tasks_.empty())
            {
                // Woken only because stopping_ and the queue is drained -> exit.
                return;
            }
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        task();
    }
}

void ThreadPool::parallel_for(std::size_t begin, std::size_t end,
                              const std::function<void(std::size_t)>& body,
                              std::size_t grain)
{
    if (begin >= end)
    {
        return;
    }
    if (grain == 0)
    {
        grain = 1;
    }

    const std::size_t total = end - begin;
    const std::size_t max_chunks = (total + grain - 1) / grain;        // ceil(total/grain)
    const std::size_t chunk_count = std::min<std::size_t>(workers_.size(), max_chunks);
    const std::size_t per_chunk = (total + chunk_count - 1) / chunk_count; // ceil

    // The latch lives on this frame, which outlives the chunk tasks because we
    // block below until every chunk has decremented `remaining`.
    struct Latch
    {
        std::mutex              m;
        std::condition_variable cv;
        std::size_t             remaining;
        std::exception_ptr      error;
    };
    Latch latch;
    latch.remaining = chunk_count;

    for (std::size_t c = 0; c < chunk_count; ++c)
    {
        const std::size_t chunk_begin = begin + c * per_chunk;
        const std::size_t chunk_end = std::min(chunk_begin + per_chunk, end);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.push([&latch, &body, chunk_begin, chunk_end] {
                std::exception_ptr err;
                try
                {
                    for (std::size_t i = chunk_begin; i < chunk_end; ++i)
                    {
                        body(i);
                    }
                }
                catch (...)
                {
                    err = std::current_exception();
                }
                bool last = false;
                {
                    std::lock_guard<std::mutex> llock(latch.m);
                    if (err && !latch.error)
                    {
                        latch.error = err;
                    }
                    last = (--latch.remaining == 0);
                }
                if (last)
                {
                    latch.cv.notify_one();
                }
            });
        }
        cv_.notify_one();
    }

    std::unique_lock<std::mutex> lock(latch.m);
    latch.cv.wait(lock, [&latch] { return latch.remaining == 0; });
    if (latch.error)
    {
        std::rethrow_exception(latch.error);
    }
}

} // namespace mosaic::foundation::jobs
