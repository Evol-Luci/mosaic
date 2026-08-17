#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "foundation/math/quat.hpp"
#include "foundation/math/vec3.hpp"

using Catch::Approx;
using mosaic::foundation::math::from_axis_angle;
using mosaic::foundation::math::normalized;
using mosaic::foundation::math::Quat;
using mosaic::foundation::math::Vec3;

TEST_CASE("Quat default and identity are (0,0,0,1)", "[math]")
{
    const Quat def;
    REQUIRE(def.x == 0.0f);
    REQUIRE(def.w == 1.0f);
    REQUIRE(Quat::identity().w == 1.0f);
}

TEST_CASE("from_axis_angle about Z by 90 degrees", "[math]")
{
    const float pi = 3.14159265358979323846f;
    const Quat q = from_axis_angle(Vec3{0.0f, 0.0f, 1.0f}, pi / 2.0f);
    REQUIRE(q.z == Approx(std::sin(pi / 4.0f)));
    REQUIRE(q.w == Approx(std::cos(pi / 4.0f)));
}

TEST_CASE("normalized yields unit length", "[math]")
{
    const Quat q = normalized(Quat{1.0f, 2.0f, 3.0f, 4.0f});
    const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    REQUIRE(len == Approx(1.0f));
}

TEST_CASE("identity compose leaves a quaternion unchanged", "[math]")
{
    const Quat q = from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, 1.0f);
    const Quat r = Quat::identity() * q;
    REQUIRE(r.x == Approx(q.x));
    REQUIRE(r.y == Approx(q.y));
    REQUIRE(r.z == Approx(q.z));
    REQUIRE(r.w == Approx(q.w));
}
