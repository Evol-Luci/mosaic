#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace mosaic::foundation::jobs
{

// A minimal fixed-size thread pool. Workers pull tasks FIFO from a shared queue.
// submit() returns a future fulfilled when the task finishes; parallel_for()
// (added alongside) blocks until a whole range is processed. Destruction drains
// every queued task, then joins — no detached threads, no unfulfilled future.
class ThreadPool
{
public:
    // Spins up `thread_count` workers, clamped to >= 1.
    explicit ThreadPool(unsigned thread_count = std::thread::hardware_concurrency());
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Enqueue a task; the returned future completes when it runs (or carries the
    // task's exception, rethrown on get()).
    std::future<void> submit(std::function<void()> task);

    // Split [begin, end) into up to thread_count chunks (respecting `grain`), run
    // `body(i)` over each index across the pool, and block until all complete.
    // Empty/inverted range is a no-op. The first exception thrown by any body is
    // rethrown from the calling thread after all chunks finish.
    void parallel_for(std::size_t begin, std::size_t end,
                      const std::function<void(std::size_t)>& body, std::size_t grain = 1);

    unsigned thread_count() const noexcept { return static_cast<unsigned>(workers_.size()); }

private:
    void worker_loop();

    std::vector<std::thread>          workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex                        mutex_;
    std::condition_variable           cv_;
    bool                              stopping_ = false;
};

} // namespace mosaic::foundation::jobs
