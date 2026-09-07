#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "engine/vfx/ribbon.hpp"

using Catch::Approx;
using mosaic::engine::render::RibbonVertex;
using mosaic::engine::vfx::build_ribbon;
using mosaic::foundation::math::Vec3;

TEST_CASE("build_ribbon emits 6 vertices per segment; <2 points emits nothing", "[vfx]")
{
    std::vector<RibbonVertex> out;
    build_ribbon({}, Vec3{0, 5, 0}, 1.0f, Vec3{1, 1, 1}, out);
    REQUIRE(out.empty());
    build_ribbon({Vec3{0, 0, 0}}, Vec3{0, 5, 0}, 1.0f, Vec3{1, 1, 1}, out);
    REQUIRE(out.empty());
    build_ribbon({Vec3{0, 0, 0}, Vec3{1, 0, 0}}, Vec3{0.5f, 5, 0}, 1.0f, Vec3{1, 1, 1}, out);
    REQUIRE(out.size() == 6);                 // one segment -> one quad
    std::vector<RibbonVertex> three;
    build_ribbon({Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{2, 0, 0}}, Vec3{1, 5, 0}, 1.0f,
                 Vec3{1, 1, 1}, three);
    REQUIRE(three.size() == 12);              // two segments -> two quads
}

TEST_CASE("build_ribbon tapers alpha and width toward the newest point", "[vfx]")
{
    std::vector<RibbonVertex> out;
    // 4 points along +X; camera above. n=4 -> alphas 0.25,0.5,0.75,1.0 by point index.
    build_ribbon({Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{2, 0, 0}, Vec3{3, 0, 0}}, Vec3{1.5f, 5, 0},
                 2.0f, Vec3{1, 1, 1}, out);
    // First emitted vertex is at the oldest end (point 0): alpha = 1/4.
    REQUIRE(out.front().alpha == Approx(0.25f));
    // Last emitted vertex belongs to the newest point (point 3): alpha = 1.0.
    REQUIRE(out.back().alpha == Approx(1.0f));
}

TEST_CASE("build_ribbon offsets perpendicular to the segment and the view direction", "[vfx]")
{
    std::vector<RibbonVertex> out;
    // Segment along +X, camera straight above (+Y): side should be along Z.
    build_ribbon({Vec3{0, 0, 0}, Vec3{1, 0, 0}}, Vec3{0.5f, 5, 0}, 2.0f, Vec3{1, 1, 1}, out);
    const Vec3 seg{1, 0, 0};
    const Vec3 center{0, 0, 0};
    const Vec3 offset = out[0].position - center; // a0 = point0 + side*half
    REQUIRE(mosaic::foundation::math::dot(offset, seg) == Approx(0.0f).margin(1e-4)); // perp to segment
    REQUIRE(offset.y == Approx(0.0f).margin(1e-4));                                    // perp to view (up)
    REQUIRE(std::abs(offset.z) > 0.1f);                                                // spread along Z
}
