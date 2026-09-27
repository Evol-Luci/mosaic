#include <cmath>

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

TEST_CASE("identity Mat4 forward/up match the engine's -Z-forward, +Y-up convention", "[math]")
{
    const Mat4 m = Mat4::identity();
    const Vec3 f = m.forward();
    const Vec3 u = m.up();
    REQUIRE(f.x == Approx(0.0f));
    REQUIRE(f.y == Approx(0.0f));
    REQUIRE(f.z == Approx(-1.0f));
    REQUIRE(u.x == Approx(0.0f));
    REQUIRE(u.y == Approx(1.0f));
    REQUIRE(u.z == Approx(0.0f));
}

TEST_CASE("Mat4 forward/up rotate with a 90 degree yaw about world up", "[math]")
{
    // Yaw +90 degrees about +Y: -Z (forward) rotates to -X.
    const Quat yaw90 = from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, 1.57079632679f);
    const Mat4 m = Mat4::rotation(yaw90);
    const Vec3 f = m.forward();
    REQUIRE(f.x == Approx(-1.0f).margin(0.001));
    REQUIRE(f.y == Approx(0.0f).margin(0.001));
    REQUIRE(f.z == Approx(0.0f).margin(0.001));
    // Up is unaffected by a pure yaw about the up axis itself.
    const Vec3 u = m.up();
    REQUIRE(u.x == Approx(0.0f).margin(0.001));
    REQUIRE(u.y == Approx(1.0f).margin(0.001));
    REQUIRE(u.z == Approx(0.0f).margin(0.001));
}

TEST_CASE("Mat4 forward is normalized even under a non-unit scale", "[math]")
{
    const Mat4 m = Mat4::scale(Vec3{2.0f, 2.0f, 2.0f});
    const Vec3 f = m.forward();
    const float len = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
    REQUIRE(len == Approx(1.0f).margin(0.001));
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

TEST_CASE("transpose swaps rows and columns", "[math]")
{
    Mat4 m;
    for (int i = 0; i < 16; ++i)
    {
        m.m[i] = static_cast<float>(i);
    }
    const Mat4 t = Mat4::transpose(m);
    // element(row,col) == m[col*4+row], so transpose(m)[col*4+row] == m[row*4+col].
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            REQUIRE(t.m[col * 4 + row] == Approx(m.m[row * 4 + col]));
        }
    }
}

TEST_CASE("transpose of identity is identity, and transposing twice round-trips", "[math]")
{
    const Mat4 id = Mat4::identity();
    const Mat4 t = Mat4::transpose(id);
    for (int i = 0; i < 16; ++i)
    {
        REQUIRE(t.m[i] == Approx(id.m[i]));
    }

    const Mat4 r = Mat4::rotation(mosaic::foundation::math::Quat{0, 0.70710678f, 0, 0.70710678f});
    const Mat4 rtt = Mat4::transpose(Mat4::transpose(r));
    for (int i = 0; i < 16; ++i)
    {
        REQUIRE(rtt.m[i] == Approx(r.m[i]));
    }
}
