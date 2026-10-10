#include <catch2/catch_test_macros.hpp>

#include "app/cli/options.hpp"
#include "app/run/editor_play_policy.hpp"

using mosaic::app::Options;
using mosaic::app::should_start_playing;

TEST_CASE("the editor starts stopped for an ordinary interactive launch", "[app][editor]")
{
    // The whole point of the change: a human opening the editor gets authoring mode.
    Options options;
    REQUIRE_FALSE(should_start_playing(options));
}

TEST_CASE("the editor starts playing when a tick bound is requested", "[app][editor]")
{
    // Without this the run HANGS rather than failing. RuntimeLoop::advance returns 0 while
    // stopped, so tick_count_ never increments, so FramePump's `tick_count() >= *ticks`
    // bound can never fire. A bounded run is non-interactive by definition, so the ticks
    // flag asking for simulation to happen is the only reading that makes sense -- and it
    // keeps the flag meaning exactly the same thing under `run` and under `editor`.
    Options options;
    options.ticks = 90;
    REQUIRE(should_start_playing(options));
}

TEST_CASE("a duration bound alone still starts stopped", "[app][editor]")
{
    // Deliberately NOT "any bound implies play". A duration bound is keyed on real elapsed
    // time, so it terminates correctly with the loop stopped -- which makes it the way to
    // bound an editor run that must actually observe the stopped default. Widening the
    // policy to every bound would remove the only such escape hatch.
    Options options;
    options.duration_s = 2.0;
    REQUIRE_FALSE(should_start_playing(options));
}

TEST_CASE("a tick bound wins when both bounds are given", "[app][editor]")
{
    // Both set: the tick bound is the one that cannot fire while stopped, so it decides.
    Options options;
    options.ticks = 30;
    options.duration_s = 2.0;
    REQUIRE(should_start_playing(options));
}

TEST_CASE("a zero tick bound still starts playing", "[app][editor]")
{
    // has_value, not a truthiness test on the number. `--ticks 0` means "stop after zero
    // ticks", which FramePump satisfies on its first bound check; treating 0 as "no bound"
    // would start stopped and then never terminate, which is the exact hang this policy
    // exists to prevent.
    Options options;
    options.ticks = 0;
    REQUIRE(should_start_playing(options));
}
