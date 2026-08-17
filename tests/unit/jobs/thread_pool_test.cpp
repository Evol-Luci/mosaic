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

TEST_CASE("parallel_for visits every index exactly once", "[jobs]")
{
    ThreadPool pool(4);
    const std::size_t n = 10000;
    std::vector<int> hits(n, 0);
    pool.parallel_for(0, n, [&hits](std::size_t i) { hits[i] += 1; });
    for (std::size_t i = 0; i < n; ++i)
    {
        REQUIRE(hits[i] == 1);
    }
}

TEST_CASE("parallel_for on an empty or inverted range does nothing", "[jobs]")
{
    ThreadPool pool(4);
    std::atomic<int> calls{0};
    pool.parallel_for(5, 5, [&calls](std::size_t) { calls.fetch_add(1); });
    pool.parallel_for(10, 3, [&calls](std::size_t) { calls.fetch_add(1); });
    REQUIRE(calls.load() == 0);
}

TEST_CASE("parallel_for rethrows a body exception on the calling thread", "[jobs]")
{
    ThreadPool pool(4);
    REQUIRE_THROWS_AS(
        pool.parallel_for(0, 100,
                          [](std::size_t i) {
                              if (i == 50)
                              {
                                  throw std::runtime_error("body boom");
                              }
                          }),
        std::runtime_error);
}

TEST_CASE("parallel_for completes on a single-thread pool", "[jobs]")
{
    ThreadPool pool(1);
    const std::size_t n = 1000;
    std::vector<int> hits(n, 0);
    pool.parallel_for(0, n, [&hits](std::size_t i) { hits[i] = static_cast<int>(i); });
    for (std::size_t i = 0; i < n; ++i)
    {
        REQUIRE(hits[i] == static_cast<int>(i));
    }
}

TEST_CASE("parallel_for covers the whole range under a custom grain", "[jobs]")
{
    ThreadPool pool(4);
    const std::size_t n = 33; // not a multiple of thread_count or grain
    std::atomic<int> sum{0};
    pool.parallel_for(0, n, [&sum](std::size_t) { sum.fetch_add(1); }, 8);
    REQUIRE(sum.load() == 33);
}
