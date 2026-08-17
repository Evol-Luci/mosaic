#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "foundation/math/mat4.hpp"
#include "foundation/math/quat.hpp"
#include "foundation/math/vec3.hpp"

using Catch::Approx;
using mosaic::foundation::math::from_axis_angle;
using mosaic::foundation::math::Mat4;
using mosaic::foundation::math::Quat;
using mosaic::foundation::math::transform_point;
using mosaic::foundation::math::Vec3;

TEST_CASE("Mat4 identity is neutral under multiply", "[math]")
{
    const Mat4 t = Mat4::translation(Vec3{1.0f, 2.0f, 3.0f});
    const Mat4 r = Mat4::identity() * t;
    for (int i = 0; i < 16; ++i)
    {
        REQUIRE(r.m[i] == Approx(t.m[i]));
    }
}

TEST_CASE("translation column holds the offset", "[math]")
{
    const Mat4 t = Mat4::translation(Vec3{4.0f, 5.0f, 6.0f});
    REQUIRE(t.m[12] == Approx(4.0f));
    REQUIRE(t.m[13] == Approx(5.0f));
    REQUIRE(t.m[14] == Approx(6.0f));
}

TEST_CASE("transform_point of the origin equals the translation", "[math]")
{
    const Mat4 t = Mat4::translation(Vec3{7.0f, 8.0f, 9.0f});
    const Vec3 p = transform_point(t, Vec3{0.0f, 0.0f, 0.0f});
    REQUIRE(p.x == Approx(7.0f));
    REQUIRE(p.y == Approx(8.0f));
    REQUIRE(p.z == Approx(9.0f));
}

TEST_CASE("trs applies scale, then rotation, then translation", "[math]")
{
    const float pi = 3.14159265358979323846f;
    const Quat rot = from_axis_angle(Vec3{0.0f, 0.0f, 1.0f}, pi / 2.0f); // 90 deg about Z
    const Mat4 m = Mat4::trs(Vec3{10.0f, 0.0f, 0.0f}, rot, Vec3{1.0f, 1.0f, 1.0f});
    const Vec3 p = transform_point(m, Vec3{1.0f, 0.0f, 0.0f}); // (1,0,0) -> (0,1,0), + trans
    REQUIRE(p.x == Approx(10.0f).margin(1e-5));
    REQUIRE(p.y == Approx(1.0f).margin(1e-5));
    REQUIRE(p.z == Approx(0.0f).margin(1e-5));
}
