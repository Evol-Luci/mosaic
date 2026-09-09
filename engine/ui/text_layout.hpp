#pragma once

#include <string>
#include <vector>

#include "engine/ui/font_atlas.hpp"
#include "engine/ui/ui_rect.hpp"
#include "foundation/math/vec2.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::engine::ui
{
struct TextQuad
{
    Rect  rect;
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    mosaic::foundation::math::Vec3 color;
    float alpha = 1.0f;
};

// Single-line, left-to-right layout in normalized screen space. See the U2b spec.
inline std::vector<TextQuad> layout_text(const FontAtlas& font, const std::string& text,
                                         Anchor anchor, mosaic::foundation::math::Vec2 pos,
                                         float height, float aspect,
                                         mosaic::foundation::math::Vec3 color, float alpha,
                                         const Rect& frame = Rect{0.0f, 0.0f, 1.0f, 1.0f})
{
    std::vector<TextQuad> out;
    if (font.line_height <= 0.0f || aspect <= 0.0f) { return out; }

    // `height` is a fraction of the frame's height (the screen when frame = {0,0,1,1}),
    // so nested text scales with its parent. scale maps atlas px -> normalized Y.
    const float scale = (height * frame.h) / font.line_height;
    const float sx    = scale / aspect;            // atlas px -> normalized X (aspect-correct)

    // Measure total advance width (present glyphs only).
    float total_adv = 0.0f;
    for (unsigned char c : text)
    {
        const auto it = font.glyphs.find(static_cast<std::uint32_t>(c));
        if (it != font.glyphs.end()) { total_adv += it->second.advance; }
    }
    const float w = total_adv * sx;

    // Pass the box size as frame-fractions so resolve_in maps it back to the right screen
    // rect: box.w = (w/frame.w)*frame.w = w, box.h = height*frame.h. Screen frame is a no-op.
    const float box_w = (frame.w > 0.0f) ? w / frame.w : w;
    const Rect box = resolve_in(frame, anchor, pos, mosaic::foundation::math::Vec2{box_w, height});
    float pen_x = box.x;
    const float baseline = box.y + font.ascent * scale;

    for (unsigned char c : text)
    {
        const auto it = font.glyphs.find(static_cast<std::uint32_t>(c));
        if (it == font.glyphs.end()) { continue; } // skip unknown codepoints entirely
        const Glyph& g = it->second;
        if (g.width > 0.0f && g.height > 0.0f)
        {
            TextQuad q;
            q.rect = Rect{pen_x + g.x_off * sx, baseline + g.y_off * scale,
                          g.width * sx, g.height * scale};
            q.u0 = g.u0; q.v0 = g.v0; q.u1 = g.u1; q.v1 = g.v1;
            q.color = color;
            q.alpha = alpha;
            out.push_back(q);
        }
        pen_x += g.advance * sx;
    }
    return out;
}
} // namespace mosaic::engine::ui
