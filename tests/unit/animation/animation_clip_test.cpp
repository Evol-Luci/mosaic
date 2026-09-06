#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/animation/animation_clip.hpp"

using Catch::Approx;
using mosaic::engine::animation::sample;
using mosaic::engine::animation::Track;

TEST_CASE("sample interpolates linearly and clamps outside the range", "[animation]")
{
    Track t;
    t.target = "Transform.position.x";
    t.keys = {{0.0f, 0.0}, {1.0f, 10.0}}; // 0 -> 10 over [0, 1]

    REQUIRE(sample(t, -1.0f) == Approx(0.0));  // before first -> clamp
    REQUIRE(sample(t, 0.0f) == Approx(0.0));   // exact first
    REQUIRE(sample(t, 0.5f) == Approx(5.0));   // linear midpoint
    REQUIRE(sample(t, 1.0f) == Approx(10.0));  // exact last
    REQUIRE(sample(t, 2.0f) == Approx(10.0));  // after last -> clamp
}

TEST_CASE("sample with step holds the left key's value", "[animation]")
{
    Track t;
    t.step = true;
    t.keys = {{0.0f, 1.0}, {1.0f, 2.0}, {2.0f, 3.0}};
    REQUIRE(sample(t, 0.5f) == Approx(1.0)); // holds key 0
    REQUIRE(sample(t, 1.5f) == Approx(2.0)); // holds key 1
    REQUIRE(sample(t, 2.0f) == Approx(3.0)); // exact last
}

TEST_CASE("sample of an empty track is zero", "[animation]")
{
    Track t;
    REQUIRE(sample(t, 0.5f) == Approx(0.0));
}
