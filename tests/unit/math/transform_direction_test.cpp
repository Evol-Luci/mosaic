#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "foundation/math/mat4.hpp"
#include "foundation/math/vec3.hpp"

using mosaic::foundation::math::Mat4;
using mosaic::foundation::math::Vec3;
using mosaic::foundation::math::transform_direction;
using Catch::Approx;

TEST_CASE("transform_direction ignores translation", "[math]")
{
    Vec3 r = transform_direction(Mat4::translation(Vec3{3, 4, 5}), Vec3{1, 0, 0});
    REQUIRE(r.x == Approx(1.0f));
    REQUIRE(r.y == Approx(0.0f));
    REQUIRE(r.z == Approx(0.0f));
}

TEST_CASE("transform_direction applies scale (linear part)", "[math]")
{
    Vec3 r = transform_direction(Mat4::scale(Vec3{2, 3, 4}), Vec3{1, 1, 1});
    REQUIRE(r.x == Approx(2.0f));
    REQUIRE(r.y == Approx(3.0f));
    REQUIRE(r.z == Approx(4.0f));
}

TEST_CASE("transform_direction matches transform_point minus translation", "[math]")
{
    // For an affine matrix, direction transform == point transform of (v) minus point
    // transform of the origin.
    const Mat4 m = Mat4::trs(Vec3{7, -2, 1},
                             mosaic::foundation::math::Quat{0, 0.70710678f, 0, 0.70710678f},
                             Vec3{1, 1, 1});
    const Vec3 v{0, 0, -1};
    const Vec3 dir = transform_direction(m, v);
    const Vec3 expected = mosaic::foundation::math::transform_point(m, v)
                          - mosaic::foundation::math::transform_point(m, Vec3{0, 0, 0});
    REQUIRE(dir.x == Approx(expected.x).margin(1e-5));
    REQUIRE(dir.y == Approx(expected.y).margin(1e-5));
    REQUIRE(dir.z == Approx(expected.z).margin(1e-5));
}
