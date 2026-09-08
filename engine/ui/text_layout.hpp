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
                                         mosaic::foundation::math::Vec3 color, float alpha)
{
    std::vector<TextQuad> out;
    if (font.line_height <= 0.0f || aspect <= 0.0f) { return out; }

    const float scale = height / font.line_height; // atlas px -> normalized Y
    const float sx    = scale / aspect;            // atlas px -> normalized X

    // Measure total advance width (present glyphs only).
    float total_adv = 0.0f;
    for (unsigned char c : text)
    {
        const auto it = font.glyphs.find(static_cast<std::uint32_t>(c));
        if (it != font.glyphs.end()) { total_adv += it->second.advance; }
    }
    const float w = total_adv * sx;

    const Rect box = resolve_rect(anchor, pos, mosaic::foundation::math::Vec2{w, height});
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
