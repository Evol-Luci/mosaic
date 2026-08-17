#include <catch2/catch_test_macros.hpp>

#include "foundation/math/vec3.hpp"

using mosaic::foundation::math::Vec3;

TEST_CASE("Vec3 default is zero", "[math]")
{
    Vec3 v;
    REQUIRE(v == Vec3{0.0f, 0.0f, 0.0f});
}

TEST_CASE("Vec3 arithmetic", "[math]")
{
    const Vec3 a{1.0f, 2.0f, 3.0f};
    const Vec3 b{4.0f, 5.0f, 6.0f};

    REQUIRE((a + b) == Vec3{5.0f, 7.0f, 9.0f});
    REQUIRE((b - a) == Vec3{3.0f, 3.0f, 3.0f});
    REQUIRE((a * 2.0f) == Vec3{2.0f, 4.0f, 6.0f});
    REQUIRE((2.0f * a) == Vec3{2.0f, 4.0f, 6.0f});
    REQUIRE(a != b);
}
