#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "foundation/math/mat4.hpp"
#include "foundation/math/vec3.hpp"

using mosaic::foundation::math::Mat4;
using mosaic::foundation::math::Vec3;
using Catch::Approx;

TEST_CASE("Vec3 dot/cross/normalize", "[math]")
{
    REQUIRE(mosaic::foundation::math::dot(Vec3{1, 2, 3}, Vec3{4, 5, 6}) == Approx(32.0f));
    Vec3 c = mosaic::foundation::math::cross(Vec3{1, 0, 0}, Vec3{0, 1, 0});
    REQUIRE(c.x == Approx(0.0f));
    REQUIRE(c.y == Approx(0.0f));
    REQUIRE(c.z == Approx(1.0f));
    Vec3 n = mosaic::foundation::math::normalize(Vec3{0, 3, 0});
    REQUIRE(n.y == Approx(1.0f));
    REQUIRE(mosaic::foundation::math::length(Vec3{3, 4, 0}) == Approx(5.0f));
}

TEST_CASE("perspective has the expected column-major entries (RH, Z in [0,1])", "[math]")
{
    // fovy = 90deg => tan(45deg) = 1; aspect 1; near 1, far 101
    const float fovy = 1.57079633f; // pi/2
    Mat4 p = Mat4::perspective(fovy, 1.0f, 1.0f, 101.0f);
    REQUIRE(p.m[0] == Approx(1.0f));                    // 1/(aspect*tanHalf)
    REQUIRE(p.m[5] == Approx(1.0f));                    // 1/tanHalf
    REQUIRE(p.m[10] == Approx(101.0f / (1.0f - 101.0f))); // far/(near-far)
    REQUIRE(p.m[11] == Approx(-1.0f));
    REQUIRE(p.m[14] == Approx(-(101.0f * 1.0f) / (101.0f - 1.0f)));
    REQUIRE(p.m[15] == Approx(0.0f)); // perspective is not affine
}

TEST_CASE("look_at maps the eye to the origin and looks down -Z", "[math]")
{
    Mat4 v = Mat4::look_at(Vec3{0, 0, 5}, Vec3{0, 0, 0}, Vec3{0, 1, 0});
    // The world origin sits 5 units in front of the camera => view-space (0,0,-5).
    Vec3 origin_in_view = mosaic::foundation::math::transform_point(v, Vec3{0, 0, 0});
    REQUIRE(origin_in_view.x == Approx(0.0f));
    REQUIRE(origin_in_view.y == Approx(0.0f));
    REQUIRE(origin_in_view.z == Approx(-5.0f));
    // The eye maps to the view-space origin.
    Vec3 eye_in_view = mosaic::foundation::math::transform_point(v, Vec3{0, 0, 5});
    REQUIRE(eye_in_view.x == Approx(0.0f));
    REQUIRE(eye_in_view.y == Approx(0.0f));
    REQUIRE(eye_in_view.z == Approx(0.0f));
}

TEST_CASE("orthographic has the expected column-major entries (RH, Z in [0,1])", "[math]")
{
    // A symmetric 10x10 box, near 1, far 21.
    Mat4 o = Mat4::orthographic(-5.0f, 5.0f, -5.0f, 5.0f, 1.0f, 21.0f);
    REQUIRE(o.m[0] == Approx(2.0f / 10.0f));                 // 2/(right-left)
    REQUIRE(o.m[5] == Approx(2.0f / 10.0f));                 // 2/(top-bottom)
    REQUIRE(o.m[10] == Approx(1.0f / (1.0f - 21.0f)));       // 1/(near-far)
    REQUIRE(o.m[12] == Approx(0.0f));                        // -(right+left)/(right-left), symmetric => 0
    REQUIRE(o.m[13] == Approx(0.0f));                        // -(top+bottom)/(top-bottom), symmetric => 0
    REQUIRE(o.m[14] == Approx(-1.0f / (21.0f - 1.0f)));      // -near/(far-near)
    REQUIRE(o.m[15] == Approx(1.0f));                        // affine (no perspective divide)
}

TEST_CASE("orthographic maps the near-plane origin ray to clip z=0 and far to z=1", "[math]")
{
    Mat4 o = Mat4::orthographic(-1.0f, 1.0f, -1.0f, 1.0f, 1.0f, 11.0f);
    // A point at view-space z=-1 (near plane, RH looks down -Z) maps to clip z=0.
    Vec3 near_pt = mosaic::foundation::math::transform_point(o, Vec3{0.0f, 0.0f, -1.0f});
    REQUIRE(near_pt.z == Approx(0.0f).margin(1e-5));
    // A point at view-space z=-11 (far plane) maps to clip z=1.
    Vec3 far_pt = mosaic::foundation::math::transform_point(o, Vec3{0.0f, 0.0f, -11.0f});
    REQUIRE(far_pt.z == Approx(1.0f).margin(1e-5));
}
