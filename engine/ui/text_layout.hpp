#pragma once

#include <string>
#include <vector>

#include "engine/ui/font_atlas.hpp"
#include "engine/ui/ui_rect.hpp"
#include "engine/ui/utf8.hpp"
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

// Resolves one codepoint to the glyph that should be drawn for it, or nullptr for "draw
// nothing". Used by BOTH of layout_text's passes, so the measure pass and the place pass
// cannot disagree about which codepoints occupy space. See the U11a slice of the U11 spec.
//
// C0 controls and DEL keep the silent skip they have always had: a visible mark where the
// author meant whitespace or nothing would be worse than dropping it. Every *printable*
// codepoint the atlas lacks resolves, in order, to U+FFFD if the atlas has it (the cook bakes
// it only when the font provides it), else to '?' (which every 32-126 atlas has), else to
// nothing.
inline const Glyph* resolve_glyph(const FontAtlas& font, std::uint32_t cp)
{
    if (cp < 0x20u || cp == 0x7Fu) { return nullptr; } // C0 + DEL: silently skipped
    const auto it = font.glyphs.find(cp);
    if (it != font.glyphs.end()) { return &it->second; }
    const auto rep = font.glyphs.find(kReplacementCodepoint);
    if (rep != font.glyphs.end()) { return &rep->second; }
    const auto qm = font.glyphs.find(static_cast<std::uint32_t>('?'));
    if (qm != font.glyphs.end()) { return &qm->second; }
    return nullptr;
}

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

    // Decode to codepoints ONCE, then run both passes over the same codepoints. Iterating
    // the std::string per byte dropped every byte of a multi-byte character and re-centred
    // the string as if it had been shorter: a label authored "Cafe-with-acute" rendered as
    // the three bytes "Caf".
    const std::vector<std::uint32_t> codepoints = decode_utf8(text);

    // Measure total advance width (drawable glyphs only, replacements included).
    float total_adv = 0.0f;
    for (const std::uint32_t cp : codepoints)
    {
        if (const Glyph* g = resolve_glyph(font, cp)) { total_adv += g->advance; }
    }
    const float w = total_adv * sx;

    // Pass the box size as frame-fractions so resolve_in maps it back to the right screen
    // rect: box.w = (w/frame.w)*frame.w = w, box.h = height*frame.h. Screen frame is a no-op.
    const float box_w = (frame.w > 0.0f) ? w / frame.w : w;
    const Rect box = resolve_in(frame, anchor, pos, mosaic::foundation::math::Vec2{box_w, height});
    float pen_x = box.x;
    const float baseline = box.y + font.ascent * scale;

    for (const std::uint32_t cp : codepoints)
    {
        const Glyph* resolved = resolve_glyph(font, cp);
        if (resolved == nullptr) { continue; } // a control character draws and advances nothing
        const Glyph& g = *resolved;
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
