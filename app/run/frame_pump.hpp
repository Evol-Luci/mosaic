#pragma once

#include <cstdint>
#include <functional>

#include "app/cli/options.hpp"
#include "app/run/status_reporter.hpp"
#include "engine/runtime/runtime_loop.hpp"
#include "engine/world/world.hpp"

namespace mosaic::app
{

// Injected time/stop dependencies. Seconds are plain doubles so tests need no
// chrono. `now` is a monotonic clock; `sleep` sleeps that many seconds (a no-op
// in tests); `stop_requested` reports an external stop (SIGINT) — false headless.
struct PumpDeps
{
    std::function<double()>     now;
    std::function<void(double)> sleep;
    std::function<bool()>       stop_requested;
};

// Drives a World in real time over a RuntimeLoop until a stop source fires.
// Owns pacing and status emission; the per-tick order stays in World::tick.
class FramePump
{
public:
    FramePump(engine::runtime::RuntimeLoop& loop, PumpDeps deps,
              StatusReporter& reporter, Options options)
        : loop_(loop), deps_(std::move(deps)), reporter_(reporter),
          options_(std::move(options))
    {
    }

    RunSummary run(engine::World& world)
    {
        const double target_frame_period = options_.fps > 0.0 ? 1.0 / options_.fps : 0.0;
        const double progress_interval = 1.0; // sim seconds between progress lines

        loop_.start();
        const double t0 = deps_.now();
        double       t_prev = t0;
        double       elapsed = 0.0;
        std::uint64_t next_progress_tick =
            static_cast<std::uint64_t>(progress_interval * options_.fixed_hz);
        std::string stopped_by;

        while (true)
        {
            // An external stop (SIGINT) ends the run before doing more work, so a
            // stop requested before the first advance yields zero ticks.
            if (deps_.stop_requested())
            {
                stopped_by = "signal";
                break;
            }

            const double t = deps_.now();
            elapsed = t - t0;
            double real_dt = t - t_prev;
            if (real_dt < 0.0)
            {
                real_dt = 0.0;
            }
            t_prev = t;

            loop_.advance(world, static_cast<float>(real_dt));

            // Periodic progress, keyed on tick count (deterministic).
            while (loop_.tick_count() >= next_progress_tick)
            {
                reporter_.progress(loop_.tick_count(), elapsed);
                next_progress_tick +=
                    static_cast<std::uint64_t>(progress_interval * options_.fixed_hz);
            }

            // Tick / duration bounds are evaluated after advancing.
            if (options_.ticks && loop_.tick_count() >= *options_.ticks)
            {
                stopped_by = "ticks";
                break;
            }
            if (options_.duration_s && elapsed >= *options_.duration_s)
            {
                stopped_by = "duration";
                break;
            }

            // Pace to the target frame rate: sleep the remainder of the budget.
            const double spent = deps_.now() - t;
            const double remaining = target_frame_period - spent;
            if (remaining > 0.0)
            {
                deps_.sleep(remaining);
            }
        }

        loop_.stop();
        return RunSummary{loop_.tick_count(), elapsed,
                          static_cast<double>(loop_.fixed_dt()),
                          options_.scene.value_or(std::string{}), stopped_by};
    }

private:
    engine::runtime::RuntimeLoop& loop_;
    PumpDeps                      deps_;
    StatusReporter&               reporter_;
    Options                       options_;
};

} // namespace mosaic::app
