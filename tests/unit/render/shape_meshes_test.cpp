#include <cmath>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/render/capsule.hpp"
#include "engine/render/render_device.hpp"
#include "engine/render/sphere.hpp"

using mosaic::engine::render::make_capsule_mesh;
using mosaic::engine::render::make_sphere_mesh;
using mosaic::engine::render::Vertex;
using Catch::Approx;

namespace
{
float vlen(const mosaic::foundation::math::Vec3& v)
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}
} // namespace

TEST_CASE("make_sphere_mesh: points lie on the radius with unit outward normals", "[render]")
{
    std::vector<Vertex>        v;
    std::vector<std::uint32_t> i;
    make_sphere_mesh(v, i, 0.5f, 24);

    REQUIRE(v.size() > 3);
    REQUIRE(i.size() % 3 == 0); // whole triangles
    REQUIRE(i.size() >= 3);

    for (const auto& vert : v)
    {
        // Every vertex sits on the sphere surface (radius 0.5) ...
        REQUIRE(vlen(vert.position) == Approx(0.5f).margin(1e-4));
        // ... with a unit-length normal that points radially outward (position / radius).
        REQUIRE(vlen(vert.normal) == Approx(1.0f).margin(1e-4));
        REQUIRE(vert.normal.x == Approx(vert.position.x / 0.5f).margin(1e-4));
        REQUIRE(vert.normal.y == Approx(vert.position.y / 0.5f).margin(1e-4));
    }

    // Every index is in range.
    for (const std::uint32_t idx : i)
    {
        REQUIRE(idx < v.size());
    }
}

TEST_CASE("make_sphere_mesh: radius scales the surface", "[render]")
{
    std::vector<Vertex>        v;
    std::vector<std::uint32_t> i;
    make_sphere_mesh(v, i, 2.0f, 16);
    float max_r = 0.0f;
    for (const auto& vert : v)
    {
        max_r = std::max(max_r, vlen(vert.position));
    }
    REQUIRE(max_r == Approx(2.0f).margin(1e-4));
}

TEST_CASE("make_capsule_mesh: spans y = +/-(half_height+radius) with unit normals", "[render]")
{
    std::vector<Vertex>        v;
    std::vector<std::uint32_t> i;
    make_capsule_mesh(v, i, 0.5f, 0.5f, 24);

    REQUIRE(v.size() > 3);
    REQUIRE(i.size() % 3 == 0);

    float max_y = -1e9f;
    float min_y = 1e9f;
    float max_xz = 0.0f;
    for (const auto& vert : v)
    {
        REQUIRE(vlen(vert.normal) == Approx(1.0f).margin(1e-4)); // unit normals
        max_y = std::max(max_y, vert.position.y);
        min_y = std::min(min_y, vert.position.y);
        max_xz = std::max(max_xz, std::sqrt(vert.position.x * vert.position.x +
                                            vert.position.z * vert.position.z));
    }
    // Hemispherical caps reach y = +/-(half_height + radius) = +/-1.0.
    REQUIRE(max_y == Approx(1.0f).margin(1e-4));
    REQUIRE(min_y == Approx(-1.0f).margin(1e-4));
    // The widest cross-section is the cylinder radius (0.5).
    REQUIRE(max_xz == Approx(0.5f).margin(1e-4));

    for (const std::uint32_t idx : i)
    {
        REQUIRE(idx < v.size());
    }
}
