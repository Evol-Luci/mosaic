#pragma once

#include <cstdint>
#include <vector>

#include "engine/render/render_device.hpp"

namespace mosaic::engine::render
{
// A unit cube centered at the origin (±0.5), 4 vertices per face (24 total) with the
// face's outward normal, 36 indices (2 triangles per face, CCW outward).
inline void make_cube_mesh(std::vector<Vertex>& out_vertices,
                           std::vector<std::uint32_t>& out_indices)
{
    using mosaic::foundation::math::Vec3;
    const float h = 0.5f;
    const Vec3 px{1, 0, 0}, nx{-1, 0, 0}, py{0, 1, 0}, ny{0, -1, 0}, pz{0, 0, 1}, nz{0, 0, -1};

    out_vertices = {
        // +X
        {{h, -h, -h}, px}, {{h, -h, h}, px}, {{h, h, h}, px}, {{h, h, -h}, px},
        // -X
        {{-h, -h, h}, nx}, {{-h, -h, -h}, nx}, {{-h, h, -h}, nx}, {{-h, h, h}, nx},
        // +Y
        {{-h, h, -h}, py}, {{h, h, -h}, py}, {{h, h, h}, py}, {{-h, h, h}, py},
        // -Y
        {{-h, -h, h}, ny}, {{h, -h, h}, ny}, {{h, -h, -h}, ny}, {{-h, -h, -h}, ny},
        // +Z
        {{h, -h, h}, pz}, {{-h, -h, h}, pz}, {{-h, h, h}, pz}, {{h, h, h}, pz},
        // -Z
        {{-h, -h, -h}, nz}, {{h, -h, -h}, nz}, {{h, h, -h}, nz}, {{-h, h, -h}, nz},
    };
    out_indices.clear();
    out_indices.reserve(36);
    for (std::uint32_t face = 0; face < 6; ++face)
    {
        const std::uint32_t b = face * 4;
        out_indices.insert(out_indices.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
    }
}
} // namespace mosaic::engine::render
