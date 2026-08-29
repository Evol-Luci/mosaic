#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

#include "engine/render/render_device.hpp"

namespace mosaic::engine::render
{
// A UV sphere centered at the origin, `radius` in all directions. `segments` controls the
// tessellation: it is the number of longitude slices; latitude uses segments/2 stacks. Each
// vertex carries its outward normal (position / radius) and a (longitude, latitude) uv.
// Winding matches make_cube_mesh: the RH normal of each triangle points inward, which the
// CLOCKWISE-front, back-face-culling pipeline renders as an outward-facing surface.
inline void make_sphere_mesh(std::vector<Vertex>&        out_vertices,
                             std::vector<std::uint32_t>& out_indices,
                             float                       radius   = 0.5f,
                             std::uint32_t               segments = 24)
{
    using mosaic::foundation::math::Vec2;
    using mosaic::foundation::math::Vec3;
    constexpr float pi = 3.14159265358979323846f;

    const std::uint32_t slices = segments < 3 ? 3 : segments;
    const std::uint32_t stacks = slices / 2 < 2 ? 2 : slices / 2;

    out_vertices.clear();
    out_vertices.reserve((stacks + 1) * (slices + 1));
    // Rings from the north pole (phi=0) down to the south pole (phi=pi).
    for (std::uint32_t i = 0; i <= stacks; ++i)
    {
        const float phi     = pi * static_cast<float>(i) / static_cast<float>(stacks);
        const float sin_phi = std::sin(phi);
        const float cos_phi = std::cos(phi);
        for (std::uint32_t j = 0; j <= slices; ++j)
        {
            const float theta = 2.0f * pi * static_cast<float>(j) / static_cast<float>(slices);
            const Vec3  n{sin_phi * std::cos(theta), cos_phi, sin_phi * std::sin(theta)};
            const Vec2  uv{static_cast<float>(j) / static_cast<float>(slices),
                          static_cast<float>(i) / static_cast<float>(stacks)};
            out_vertices.push_back({{n.x * radius, n.y * radius, n.z * radius}, n, uv});
        }
    }

    out_indices.clear();
    out_indices.reserve(stacks * slices * 6);
    const std::uint32_t stride = slices + 1;
    for (std::uint32_t i = 0; i < stacks; ++i)
        for (std::uint32_t j = 0; j < slices; ++j)
        {
            const std::uint32_t a = i * stride + j;       // this ring, this slice
            const std::uint32_t b = (i + 1) * stride + j; // next ring (down), this slice
            out_indices.insert(out_indices.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
}
} // namespace mosaic::engine::render
