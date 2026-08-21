#pragma once

#include <cstdint>
#include <vector>

#include "engine/render/render_device.hpp"

namespace mosaic::engine::render
{
// A unit cube centered at the origin (±0.5), 4 vertices per face (24 total) so each
// face carries a flat color, 36 indices (2 triangles per face, CCW outward).
inline void make_cube_mesh(std::vector<Vertex>& out_vertices,
                           std::vector<std::uint32_t>& out_indices)
{
    using mosaic::foundation::math::Vec3;
    const Vec3 red{1, 0, 0}, green{0, 1, 0}, blue{0, 0, 1};
    const Vec3 yellow{1, 1, 0}, cyan{0, 1, 1}, magenta{1, 0, 1};
    const float h = 0.5f;

    // 6 faces × 4 corners; winding is CCW when viewed from outside (front face CCW).
    out_vertices = {
        // +X (red)
        {{h, -h, -h}, red}, {{h, -h, h}, red}, {{h, h, h}, red}, {{h, h, -h}, red},
        // -X (green)
        {{-h, -h, h}, green}, {{-h, -h, -h}, green}, {{-h, h, -h}, green}, {{-h, h, h}, green},
        // +Y (blue)
        {{-h, h, -h}, blue}, {{h, h, -h}, blue}, {{h, h, h}, blue}, {{-h, h, h}, blue},
        // -Y (yellow)
        {{-h, -h, h}, yellow}, {{h, -h, h}, yellow}, {{h, -h, -h}, yellow}, {{-h, -h, -h}, yellow},
        // +Z (cyan)
        {{h, -h, h}, cyan}, {{-h, -h, h}, cyan}, {{-h, h, h}, cyan}, {{h, h, h}, cyan},
        // -Z (magenta)
        {{-h, -h, -h}, magenta}, {{h, -h, -h}, magenta}, {{h, h, -h}, magenta}, {{-h, h, -h}, magenta},
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
