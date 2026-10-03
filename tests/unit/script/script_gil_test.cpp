// Embedded CPython has one GIL, and these pin who holds it.
//
// py::initialize_interpreter() leaves the GIL held by whichever thread ran it. For a long
// time nothing in this module handed it off, so a script engine created on a worker thread
// called PyDict_New() with no GIL and segfaulted -- but ONLY when some earlier engine had
// already been created on the main thread. Run the threaded case alone and the worker won the
// std::call_once race, held the GIL, and everything worked. That is why
// `mosaic_unit_tests "[script]"` crashed 3/3 while ctest stayed green: ctest gives every
// TEST_CASE its own process, so the worker was always first.
//
// Each case below therefore does BOTH halves in one process, which is what makes it a
// regression guard ctest can actually run.

#include <atomic>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "modules/script_python/python_script_engine.hpp"

using mosaic::script::python::create_python_script_engine;

TEST_CASE("a script engine works on a worker thread after one was made on the main thread",
          "[script]")
{
    // The main thread goes first, which is what used to leave the GIL stranded here.
    auto main_engine = create_python_script_engine();
    REQUIRE(main_engine->run_string("x = 1\n").ok);

    std::atomic<bool> worker_ok{false};
    std::thread worker(
        [&]
        {
            auto worker_engine = create_python_script_engine();
            worker_ok.store(worker_engine->run_string("y = 2\n").ok);
        });
    worker.join();

    REQUIRE(worker_ok.load());
}

TEST_CASE("two script engines on two threads can both run a script", "[script]")
{
    // Pins the invariant rather than the symptom: with no thread owning the GIL implicitly,
    // any number of threads can take it in turn. A fix that merely handed ownership to the
    // worker would pass the case above and fail this one.
    auto main_engine = create_python_script_engine();
    REQUIRE(main_engine->run_string("x = 1\n").ok);

    constexpr int    kThreads = 4;
    std::atomic<int> succeeded{0};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i)
    {
        threads.emplace_back(
            [&]
            {
                auto engine = create_python_script_engine();
                if (engine->run_string("z = 3\nassert z == 3\n").ok) { succeeded.fetch_add(1); }
            });
    }
    for (std::thread& t : threads) { t.join(); }

    REQUIRE(succeeded.load() == kThreads);
}
