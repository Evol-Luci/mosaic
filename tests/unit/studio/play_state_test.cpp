#include <catch2/catch_test_macros.hpp>

#include "studio/play_state.hpp"

using mosaic::studio::PlayState;

TEST_CASE("PlayState defaults to playing", "[studio][play]")
{
    PlayState play;
    REQUIRE(play.is_playing());
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
