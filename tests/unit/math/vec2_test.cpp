#include <catch2/catch_test_macros.hpp>
#include "foundation/math/vec2.hpp"

using mosaic::foundation::math::Vec2;

TEST_CASE("Vec2 defaults to zero and compares", "[math]")
{
    Vec2 a;
    REQUIRE(a.x == 0.0f);
    REQUIRE(a.y == 0.0f);
    REQUIRE(Vec2{1.0f, 2.0f} == Vec2{1.0f, 2.0f});
    REQUIRE_FALSE(Vec2{1.0f, 2.0f} == Vec2{1.0f, 3.0f});
}
