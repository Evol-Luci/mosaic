#include <atomic>
#include <future>
#include <stdexcept>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "foundation/jobs/thread_pool.hpp"

using mosaic::foundation::jobs::ThreadPool;

TEST_CASE("submit runs all tasks; side effects visible after wait", "[jobs]")
{
    ThreadPool pool(4);
    std::atomic<int> counter{0};
    std::vector<std::future<void>> futures;
    for (int i = 0; i < 100; ++i)
    {
        futures.push_back(pool.submit([&counter] { counter.fetch_add(1); }));
    }
    for (auto& f : futures)
    {
        f.wait();
    }
    REQUIRE(counter.load() == 100);
}

TEST_CASE("a submitted task's side effect is observable after its future", "[jobs]")
{
    ThreadPool pool(2);
    int value = 0;
    auto f = pool.submit([&value] { value = 42; });
    f.wait();
    REQUIRE(value == 42);
}

TEST_CASE("a submitted task's exception surfaces via future.get", "[jobs]")
{
    ThreadPool pool(2);
    auto f = pool.submit([] { throw std::runtime_error("boom"); });
    REQUIRE_THROWS_AS(f.get(), std::runtime_error);
}

TEST_CASE("destruction drains queued tasks (clean shutdown, no hang)", "[jobs]")
{
    std::atomic<int> counter{0};
    {
        ThreadPool pool(4);
        for (int i = 0; i < 1000; ++i)
        {
            pool.submit([&counter] { counter.fetch_add(1); });
        }
        // pool destructs here: must drain all queued tasks, then join.
    }
    REQUIRE(counter.load() == 1000);
}

TEST_CASE("a single-thread pool still completes all tasks", "[jobs]")
{
    ThreadPool pool(1);
    REQUIRE(pool.thread_count() == 1);
    std::atomic<int> counter{0};
    std::vector<std::future<void>> futures;
    for (int i = 0; i < 50; ++i)
    {
        futures.push_back(pool.submit([&counter] { counter.fetch_add(1); }));
    }
    for (auto& f : futures)
    {
        f.wait();
    }
    REQUIRE(counter.load() == 50);
}

TEST_CASE("thread_count clamps zero to one", "[jobs]")
{
    ThreadPool pool(0);
    REQUIRE(pool.thread_count() == 1);
}
