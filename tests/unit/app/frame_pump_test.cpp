#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "app/cli/options.hpp"
#include "app/run/frame_pump.hpp"
#include "app/run/status_reporter.hpp"
#include "engine/runtime/runtime_loop.hpp"
#include "engine/world/world.hpp"

using mosaic::app::FramePump;
using mosaic::app::Options;
using mosaic::app::PumpDeps;
using mosaic::app::RunSummary;
using mosaic::app::StatusReporter;
using mosaic::engine::World;
using mosaic::engine::runtime::RuntimeLoop;

namespace
{
// Records reporter calls for assertions; no logging, no stdout.
class CapturingReporter : public StatusReporter
{
public:
    std::vector<std::uint64_t> progress_ticks;
    void progress(std::uint64_t tick, double) override { progress_ticks.push_back(tick); }
    void summary(const RunSummary&) override {}
    void error(const std::string&) override {}
};

// A monotonic clock that advances a fixed step per call and never stalls (so a
// long run never sees real_dt == 0). Each call returns the current time, then
// advances. step >= fixed_dt guarantees advance() banks at least one tick.
struct StepClock
{
    double t = 0.0;
    double step;
    double operator()()
    {
        double now = t;
        t += step;
        return now;
    }
};
} // namespace

TEST_CASE("ticks option bounds the run to exactly N ticks", "[app]")
{
    World world;
    // max_steps_per_advance = 1: each advance runs at most one fixed tick, so a
    // large real_dt cannot overshoot the ticks bound. The count is exact.
    RuntimeLoop loop(1.0F / 60.0F, 1);

    Options options;
    options.ticks = 5;
    options.fixed_hz = 60.0;

    StepClock clock{0.0, 1.0}; // 1s per call: always >= fixed_dt, so 1 tick/iter
    PumpDeps deps{std::ref(clock), [](double s) { REQUIRE(s > 0.0); }, [] { return false; }};

    CapturingReporter reporter;
    FramePump pump(loop, deps, reporter, options);
    RunSummary summary = pump.run(world);

    REQUIRE(loop.tick_count() == 5);
    REQUIRE(summary.ticks == 5);
    REQUIRE(summary.stopped_by == "ticks");
}

TEST_CASE("duration option stops once elapsed crosses the bound", "[app]")
{
    World world;
    RuntimeLoop loop(1.0F / 60.0F);

    Options options;
    options.duration_s = 2.0;

    StepClock clock{0.0, 1.0}; // elapsed crosses 2.0 within a few iterations
    PumpDeps deps{std::ref(clock), [](double) {}, [] { return false; }};

    CapturingReporter reporter;
    FramePump pump(loop, deps, reporter, options);
    RunSummary summary = pump.run(world);

    REQUIRE(summary.stopped_by == "duration");
    REQUIRE(summary.elapsed_s >= 2.0);
}

TEST_CASE("a stop request ends the run with stopped_by == signal", "[app]")
{
    World world;
    RuntimeLoop loop(1.0F / 60.0F);
    Options options; // no bounds

    StepClock clock{0.0, 0.1};
    bool stop = true; // already requested: the top-of-loop check fires first
    PumpDeps deps{std::ref(clock), [](double) {}, [&] { return stop; }};

    CapturingReporter reporter;
    FramePump pump(loop, deps, reporter, options);
    RunSummary summary = pump.run(world);

    REQUIRE(summary.stopped_by == "signal");
    REQUIRE(loop.tick_count() == 0); // stop checked before the first advance
}

TEST_CASE("progress fires on the sim-second cadence", "[app]")
{
    World world;
    // One tick per advance so the tick_count-driven cadence is exact: progress
    // fires at ticks 60, 120, 180 (1s, 2s, 3s at 60 Hz).
    RuntimeLoop loop(1.0F / 60.0F, 1);

    Options options;
    options.ticks = 180;
    options.fixed_hz = 60.0;

    StepClock clock{0.0, 1.0};
    PumpDeps deps{std::ref(clock), [](double) {}, [] { return false; }};

    CapturingReporter reporter;
    FramePump pump(loop, deps, reporter, options);
    pump.run(world);

    REQUIRE(reporter.progress_ticks.size() == 3);
    REQUIRE(reporter.progress_ticks.front() == 60);
    REQUIRE(reporter.progress_ticks.back() == 180);
}
