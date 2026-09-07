#pragma once

#include <vector>

#include "engine/render/render_device.hpp" // RibbonVertex
#include "foundation/math/vec3.hpp"

namespace mosaic::engine::vfx
{
using mosaic::foundation::math::Vec3;

// Append a camera-facing ribbon (triangle LIST, 6 verts per segment) for `points`
// (oldest -> newest) to `out`. `width` is the head (newest) width; width and alpha taper
// from ~0 at the oldest end to full at the newest (frac = (i+1)/n). Fewer than 2 points
// appends nothing. `camera_pos` orients each segment to face the viewer.
inline void build_ribbon(const std::vector<Vec3>& points, const Vec3& camera_pos, float width,
                         const Vec3& color, std::vector<mosaic::engine::render::RibbonVertex>& out)
{
    const int n = static_cast<int>(points.size());
    if (n < 2) { return; }

    auto half_at  = [&](int j) { return 0.5f * width * (static_cast<float>(j) + 1.0f) / n; };
    auto alpha_at = [&](int j) { return (static_cast<float>(j) + 1.0f) / n; };

    for (int i = 0; i + 1 < n; ++i)
    {
        const Vec3 seg = mosaic::foundation::math::normalize(points[i + 1] - points[i]);
        Vec3 to_cam = mosaic::foundation::math::normalize(camera_pos - points[i]);
        Vec3 side = mosaic::foundation::math::cross(seg, to_cam);
        if (mosaic::foundation::math::length(side) < 1e-5f)
        {
            side = mosaic::foundation::math::cross(seg, Vec3{0.0f, 1.0f, 0.0f});
        }
        if (mosaic::foundation::math::length(side) < 1e-5f)
        {
            side = mosaic::foundation::math::cross(seg, Vec3{1.0f, 0.0f, 0.0f});
        }
        side = mosaic::foundation::math::normalize(side);

        const Vec3 s_i  = side * half_at(i);
        const Vec3 s_i1 = side * half_at(i + 1);
        const mosaic::engine::render::RibbonVertex a0{points[i] + s_i, color, alpha_at(i)};
        const mosaic::engine::render::RibbonVertex a1{points[i] - s_i, color, alpha_at(i)};
        const mosaic::engine::render::RibbonVertex b0{points[i + 1] + s_i1, color, alpha_at(i + 1)};
        const mosaic::engine::render::RibbonVertex b1{points[i + 1] - s_i1, color, alpha_at(i + 1)};

        out.push_back(a0); out.push_back(a1); out.push_back(b1); // triangle 1
        out.push_back(a0); out.push_back(b1); out.push_back(b0); // triangle 2
    }
}
} // namespace mosaic::engine::vfx
