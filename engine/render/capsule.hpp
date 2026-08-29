#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

#include "engine/render/render_device.hpp"

namespace mosaic::engine::render
{
// A capsule centered at the origin, aligned along local Y: a cylinder of `radius` spanning
// y = [-half_height, +half_height] capped by two hemispheres, so the whole shape reaches
// y = ±(half_height + radius). This matches the Box3D CapsuleCollider, whose hemisphere
// centers sit at y = ±half_height. `segments` is the number of longitude slices; each
// hemisphere uses segments/4 rings. Winding matches make_cube_mesh (RH normal points inward).
inline void make_capsule_mesh(std::vector<Vertex>&        out_vertices,
                              std::vector<std::uint32_t>& out_indices,
                              float                       radius      = 0.5f,
                              float                       half_height = 0.5f,
                              std::uint32_t               segments    = 24)
{
    using mosaic::foundation::math::Vec2;
    using mosaic::foundation::math::Vec3;
    constexpr float pi = 3.14159265358979323846f;

    const std::uint32_t slices    = segments < 3 ? 3 : segments;
    const std::uint32_t hemiRings = segments / 4 < 1 ? 1 : segments / 4;

    // Emit one ring of (slices+1) vertices at latitude `lat` (from the hemisphere's own
    // center), offset so the hemisphere is centered at y = center_y. lat > 0 bulges up.
    auto push_ring = [&](float lat, float center_y, float v_coord)
    {
        const float ring_r = radius * std::cos(lat);
        const float ring_y = center_y + radius * std::sin(lat);
        const float sin_lat = std::sin(lat);
        const float cos_lat = std::cos(lat);
        for (std::uint32_t j = 0; j <= slices; ++j)
        {
            const float theta = 2.0f * pi * static_cast<float>(j) / static_cast<float>(slices);
            const Vec3  n{cos_lat * std::cos(theta), sin_lat, cos_lat * std::sin(theta)};
            const Vec2  uv{static_cast<float>(j) / static_cast<float>(slices), v_coord};
            out_vertices.push_back({{ring_r * std::cos(theta), ring_y, ring_r * std::sin(theta)}, n, uv});
        }
    };

    out_vertices.clear();
    // Rings run top-to-bottom so the winding matches make_sphere_mesh / make_cube_mesh.
    const std::uint32_t rings = 2 * (hemiRings + 1);
    out_vertices.reserve(rings * (slices + 1));
    // Top hemisphere: north pole (lat=+pi/2) down to the equator (lat=0), centered at +half_height.
    for (std::uint32_t k = 0; k <= hemiRings; ++k)
    {
        const float t = static_cast<float>(k) / static_cast<float>(hemiRings);
        push_ring((pi / 2.0f) * (1.0f - t), half_height, 0.5f * (1.0f - t));
    }
    // Bottom hemisphere: equator (lat=0) down to the south pole (lat=-pi/2), centered at -half_height.
    for (std::uint32_t k = 0; k <= hemiRings; ++k)
    {
        const float t = static_cast<float>(k) / static_cast<float>(hemiRings);
        push_ring(-(pi / 2.0f) * t, -half_height, 0.5f - 0.5f * t);
    }

    out_indices.clear();
    out_indices.reserve((rings - 1) * slices * 6);
    const std::uint32_t stride = slices + 1;
    for (std::uint32_t i = 0; i + 1 < rings; ++i)
        for (std::uint32_t j = 0; j < slices; ++j)
        {
            const std::uint32_t a = i * stride + j;       // this ring, this slice
            const std::uint32_t b = (i + 1) * stride + j; // next ring (down), this slice
            out_indices.insert(out_indices.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
}
} // namespace mosaic::engine::render
