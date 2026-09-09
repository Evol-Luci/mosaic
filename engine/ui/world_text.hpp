#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "engine/render/render_device.hpp" // WorldGlyphVertex
#include "engine/ui/font_atlas.hpp"
#include "engine/ui/text_layout.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::engine::ui
{
// Expand `text` into camera-facing world-space glyph triangles (6 verts/glyph, LIST),
// centered horizontally + vertically on anchor_world. cam_right/cam_up orient the
// billboard; size scales line-height units to world units. Reuses layout_text (local
// line-height units: frame {0,0,1,1}, aspect 1, height 1). Empty text -> no verts.
inline std::vector<mosaic::engine::render::WorldGlyphVertex> build_world_text(
    const FontAtlas& font, const std::string& text,
    mosaic::foundation::math::Vec3 anchor_world,
    mosaic::foundation::math::Vec3 cam_right, mosaic::foundation::math::Vec3 cam_up,
    float size, mosaic::foundation::math::Vec3 color, float alpha)
{
    std::vector<mosaic::engine::render::WorldGlyphVertex> out;
    const std::vector<TextQuad> quads = layout_text(
        font, text, Anchor::TopLeft, mosaic::foundation::math::Vec2{0.0f, 0.0f},
        1.0f /*height*/, 1.0f /*aspect*/, color, alpha);
    if (quads.empty()) { return out; }

    float max_x = 0.0f;
    for (const auto& q : quads) { max_x = std::max(max_x, q.rect.x + q.rect.w); }
    const float cx0 = max_x * 0.5f; // horizontal center
    const float cy0 = 0.5f;         // vertical center (local box ~1 line tall)

    auto emit = [&](float lx, float ly, float u, float vv)
    {
        const float ox = (lx - cx0) * size;  // along cam_right
        const float oy = (cy0 - ly) * size;  // along cam_up (local-down -> world-up)
        mosaic::engine::render::WorldGlyphVertex vert;
        vert.position = mosaic::foundation::math::Vec3{
            anchor_world.x + ox * cam_right.x + oy * cam_up.x,
            anchor_world.y + ox * cam_right.y + oy * cam_up.y,
            anchor_world.z + ox * cam_right.z + oy * cam_up.z};
        vert.u = u; vert.v = vv; vert.color = color; vert.alpha = alpha;
        out.push_back(vert);
    };

    for (const auto& q : quads)
    {
        const float x0 = q.rect.x, y0 = q.rect.y, x1 = q.rect.x + q.rect.w, y1 = q.rect.y + q.rect.h;
        emit(x0, y0, q.u0, q.v0); emit(x1, y0, q.u1, q.v0); emit(x0, y1, q.u0, q.v1);
        emit(x1, y0, q.u1, q.v0); emit(x1, y1, q.u1, q.v1); emit(x0, y1, q.u0, q.v1);
    }
    return out;
}
} // namespace mosaic::engine::ui
