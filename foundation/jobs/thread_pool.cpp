#include "foundation/jobs/thread_pool.hpp"

#include <algorithm>
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

} // namespace mosaic::foundation::jobs
