#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "foundation/math/mat4.hpp"
#include "foundation/math/quat.hpp"
#include "foundation/math/vec3.hpp"

using mosaic::foundation::math::Mat4;
using mosaic::foundation::math::Quat;
using mosaic::foundation::math::Vec3;
using Catch::Approx;

TEST_CASE("inverse_affine of a pure translation negates it", "[math]")
{
    Mat4 inv = Mat4::inverse_affine(Mat4::translation(Vec3{1, 2, 3}));
    REQUIRE(inv.m[12] == Approx(-1.0f));
    REQUIRE(inv.m[13] == Approx(-2.0f));
    REQUIRE(inv.m[14] == Approx(-3.0f));
    REQUIRE(inv.m[0] == Approx(1.0f)); // linear part identity
    REQUIRE(inv.m[5] == Approx(1.0f));
    REQUIRE(inv.m[10] == Approx(1.0f));
}

TEST_CASE("inverse_affine(m) * m is identity for a TRS matrix", "[math]")
{
    // A non-trivial TRS: translate, rotate 90deg about Y, scale.
    const Quat ry{0.0f, 0.70710678f, 0.0f, 0.70710678f}; // 90deg about Y
    Mat4 m = Mat4::trs(Vec3{2, -3, 5}, ry, Vec3{2, 0.5f, 1.5f});
    Mat4 prod = Mat4::inverse_affine(m) * m;
    for (int col = 0; col < 4; ++col)
    {
        for (int row = 0; row < 4; ++row)
        {
            const float expected = (row == col) ? 1.0f : 0.0f;
            REQUIRE(prod.m[col * 4 + row] == Approx(expected).margin(1e-5));
        }
    }
}

TEST_CASE("inverse_affine of a singular linear part returns identity", "[math]")
{
    Mat4 zero_scale = Mat4::scale(Vec3{0, 0, 0}); // non-invertible 3x3
    Mat4 inv = Mat4::inverse_affine(zero_scale);
    REQUIRE(inv.m[0] == Approx(1.0f));
    REQUIRE(inv.m[5] == Approx(1.0f));
    REQUIRE(inv.m[10] == Approx(1.0f));
    REQUIRE(inv.m[15] == Approx(1.0f));
}
