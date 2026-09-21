#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/render/shadow.hpp"

using Catch::Approx;
using mosaic::engine::render::DirectionalLight;
using mosaic::engine::render::directional_light_view_projection;
using mosaic::foundation::math::Mat4;
using mosaic::foundation::math::Vec3;

TEST_CASE("directional_light_view_projection maps the origin into the shadow frustum's clip volume", "[render]")
{
    DirectionalLight light{Vec3{0.0f, -1.0f, 0.0f}, Vec3{1, 1, 1}}; // straight down
    Mat4 lvp = directional_light_view_projection(light);
    Vec3 origin_clip = mosaic::foundation::math::transform_point(lvp, Vec3{0.0f, 0.0f, 0.0f});
    // The world origin is inside the fixed frustum by construction -> clip xyz in [-1,1]x[-1,1]x[0,1].
    REQUIRE(origin_clip.x >= -1.0f); REQUIRE(origin_clip.x <= 1.0f);
    REQUIRE(origin_clip.y >= -1.0f); REQUIRE(origin_clip.y <= 1.0f);
    REQUIRE(origin_clip.z >= 0.0f);  REQUIRE(origin_clip.z <= 1.0f);
}

TEST_CASE("directional_light_view_projection changes with light direction", "[render]")
{
    Mat4 down = directional_light_view_projection(DirectionalLight{Vec3{0, -1, 0}, Vec3{1, 1, 1}});
    Mat4 sideways = directional_light_view_projection(DirectionalLight{Vec3{1, -1, 0}, Vec3{1, 1, 1}});
    bool any_different = false;
    for (int i = 0; i < 16; ++i)
    {
        if (std::abs(down.m[i] - sideways.m[i]) > 1e-4f) { any_different = true; break; }
    }
    REQUIRE(any_different);
}
