#pragma once

#include <cstdint>

#include "engine/world/world.hpp"

namespace mosaic::engine::runtime
{

// Fixed-timestep accumulator over World::tick. Owns pacing only: the caller feeds
// elapsed real time via advance(); the loop dispatches whole fixed ticks. No
// wall-clock inside, so a given (fixed_dt, real_dt schedule) is deterministic.
class RuntimeLoop
{
public:
    explicit RuntimeLoop(float fixed_dt, int max_steps_per_advance = 8)
        : fixed_dt_(fixed_dt), max_steps_(max_steps_per_advance)
    {
    }

    void start() { running_ = true; }
    void stop() { running_ = false; }
    bool is_running() const { return running_; }

    // Bank real_dt and run as many fixed ticks as fit (capped). No-op returning 0
    // while stopped. Remainder carries to the next call. Negative real_dt clamps to 0.
    int advance(World& world, float real_dt)
    {
        if (!running_)
        {
            return 0;
        }
        accumulator_ += (real_dt > 0.0f ? real_dt : 0.0f);
        int steps = 0;
        while (accumulator_ >= fixed_dt_ && steps < max_steps_)
        {
            world.tick(fixed_dt_);
            accumulator_ -= fixed_dt_;
            ++tick_count_;
            ++steps;
        }
        // Spiral-of-death guard: hit the cap with time still pending -> drop backlog.
        if (steps == max_steps_ && accumulator_ >= fixed_dt_)
        {
            accumulator_ = 0.0f;
        }
        return steps;
    }

    // One fixed tick regardless of running state / accumulator (editor/debug step).
    int step(World& world)
    {
        world.tick(fixed_dt_);
        ++tick_count_;
        return 1;
    }

    float fixed_dt() const { return fixed_dt_; }
    float accumulator() const { return accumulator_; }
    std::uint64_t tick_count() const { return tick_count_; }

private:
    float fixed_dt_;
    int max_steps_;
    float accumulator_ = 0.0f;
    bool running_ = false;
    std::uint64_t tick_count_ = 0;
};

} // namespace mosaic::engine::runtime
