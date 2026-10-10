#include <catch2/catch_test_macros.hpp>

#include "studio/play_state.hpp"

using mosaic::studio::PlayState;

TEST_CASE("PlayState defaults to STOPPED so the editor opens in authoring mode",
          "[studio][play]")
{
    // Inverted on 2026-10-10. It defaulted to playing, so the editor began simulating the
    // instant it opened -- and once viewport clicks were routed to the game while playing,
    // the default click drove the game and selecting an object required pressing Stop
    // first. Mosaic is editor-facing, so the default must be authoring.
    //
    // This is the ONLY test that pins the default, because `mosaic editor --ticks N` now
    // starts playing by policy (see editor_play_policy.hpp) and so cannot observe it.
    PlayState play;
    REQUIRE_FALSE(play.is_playing());
}

TEST_CASE("PlayState play/stop are idempotent and toggle flips", "[studio][play]")
{
    PlayState play;
    play.stop();
    REQUIRE_FALSE(play.is_playing());
    play.stop(); // idempotent
    REQUIRE_FALSE(play.is_playing());
    play.play();
    REQUIRE(play.is_playing());
    play.play(); // idempotent
    REQUIRE(play.is_playing());
    play.toggle();
    REQUIRE_FALSE(play.is_playing());
    play.toggle();
    REQUIRE(play.is_playing());
}

TEST_CASE("PlayState step request is consumed once", "[studio][play]")
{
    PlayState play;
    REQUIRE_FALSE(play.consume_step_request()); // nothing queued
    play.request_step();
    REQUIRE(play.consume_step_request());       // returns true once
    REQUIRE_FALSE(play.consume_step_request());  // and clears
}
