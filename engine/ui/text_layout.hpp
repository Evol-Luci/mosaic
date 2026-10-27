#pragma once

#include <cstddef>
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

// One laid-out line: a half-open range `[begin, end)` of the DECODED CODEPOINT VECTOR, plus
// the total advance of its drawable glyphs in ATLAS PIXELS.
//
// The range indexes codepoints, not bytes, and that is the whole reason U11a had to land
// first: every break point below is one of these indices, so a break can only ever fall
// between characters. A byte-indexed version of exactly this code could split a multi-byte
// character in half and drop both halves as unknown -- a worse defect than the one U11a fixed.
struct TextLine
{
    std::size_t begin   = 0;
    std::size_t end     = 0;
    float       advance = 0.0f; // atlas pixels, drawable glyphs only (replacements included)
};

// Split `codepoints` into lines: always at '\n', and -- when `max_advance > 0` -- wherever a
// line would exceed `max_advance` atlas pixels.
//
// THIS IS THE ONLY PLACE A LINE IS MEASURED. layout_text's place pass reads these advances
// rather than re-accumulating them, so the measure pass and the place pass cannot disagree
// about a line's width; U11a established that invariant for a whole string, and a second
// accumulation loop is exactly how it would be lost per line.
//
// Break policy, in the order the search prefers:
//   1. a space on the current line (a SOFT break): the WHOLE RUN of spaces at that point
//      belongs to neither line -- it is dropped, so a wrapped line is neither padded nor
//      measured with the whitespace that caused the wrap. "AAA AAA" and "AAA   AAA" wrap to
//      exactly the same two lines.
//   2. no space to break at (a FORCED break): break immediately before the codepoint that
//      does not fit. This is the long-word policy, chosen deliberately over letting the word
//      overflow: since U9b, an overflowing label is MASKED by its parent's clip rather than
//      merely ugly, so overflow would make an authored wrap width silently lose text. It is a
//      break between codepoints, never inside one, which is what the U11 gate asks for.
// Either way AT LEAST ONE CODEPOINT IS PLACED PER LINE (`i > begin`, and the soft break is
// only taken when `space > begin`). Without that, a codepoint wider than the whole wrap width
// would close an empty line and leave the cursor where it was, forever.
//
// '\r' is NOT a break: it keeps U11a's silent control-character skip, so "\r\n" is one line
// break rather than two. Every '\n' opens a line, including a trailing one -- "A\n" is two
// lines, the second empty, which is what a text editor shows and what makes the height
// predictable from the newline count alone.
inline std::vector<TextLine> break_lines(const FontAtlas& font,
                                         const std::vector<std::uint32_t>& codepoints,
                                         float max_advance)
{
    static constexpr std::size_t  kNoBreak  = static_cast<std::size_t>(-1);
    static constexpr std::uint32_t kNewline = 0x0Au;
    static constexpr std::uint32_t kSpace   = 0x20u;

    std::vector<TextLine> lines;
    const bool            wrap = max_advance > 0.0f;
    const std::size_t     n    = codepoints.size();

    std::size_t begin        = 0;        // first codepoint of the line being built
    float       adv          = 0.0f;     // advance accumulated since `begin`
    std::size_t space        = kNoBreak; // last space seen on this line
    float       adv_at_space = 0.0f;     // advance up to, and not including, that space

    std::size_t i = 0;
    while (i < n)
    {
        const std::uint32_t cp = codepoints[i];
        if (cp == kNewline)
        {
            lines.push_back(TextLine{begin, i, adv});
            ++i;
            begin = i; adv = 0.0f; space = kNoBreak; adv_at_space = 0.0f;
            continue;
        }

        const Glyph* g = resolve_glyph(font, cp);
        const float  a = (g != nullptr) ? g->advance : 0.0f;

        if (wrap && i > begin && adv + a > max_advance)
        {
            if (space != kNoBreak && space > begin)
            {
                lines.push_back(TextLine{begin, space, adv_at_space});
                std::size_t next = space;
                while (next < n && codepoints[next] == kSpace) { ++next; } // drop the run
                i = next; begin = next;
            }
            else
            {
                lines.push_back(TextLine{begin, i, adv});
                begin = i; // `i` is unchanged: this codepoint opens the new line
            }
            adv = 0.0f; space = kNoBreak; adv_at_space = 0.0f;
            // Re-walk from `begin`. `i` strictly increased on the soft path and `begin`
            // strictly increased on the forced path, so the loop always makes progress.
            continue;
        }

        if (cp == kSpace && (i == begin || codepoints[i - 1] != kSpace))
        {
            // THE CANDIDATE IS THE FIRST SPACE OF THE RUN, not the last one seen. Pointing it
            // at the last would close the line at "AAA  " rather than "AAA", leaving a
            // trailing space inside the line's measured advance -- invisible for a
            // left-anchored line and a visible half-space shift for a centred one. The run
            // is dropped from both sides, so it must be excluded from both measurements.
            space        = i;
            adv_at_space = adv;
        }
        adv += a;
        ++i;
    }
    lines.push_back(TextLine{begin, n, adv});
    return lines;
}

// Multi-line, left-to-right layout in normalized screen space. See the U2b spec, and the
// U11b slice for the line breaking.
//
// `wrap_width` is a maximum line width as a FRACTION OF THE FRAME'S WIDTH, matching `pos` and
// `height`, which are already frame-fractions; `<= 0` means no wrap, which is every caller
// that does not opt in. '\n' is honoured whether or not a wrap width is set.
//
// ANCHORING RESOLVES PER LINE, not over the block. The block box is anchored once at
// {widest line, lines x height}, and each line is then offset inside it by
// (widest - own width) x the anchor's horizontal band. That is algebraically the same as
// anchoring each line independently -- and for a single line the offset is EXACTLY 0.0f, which
// is what keeps the no-wrap single-line path bit-identical to the pre-U11b arithmetic. Every
// label authored today takes that path.
inline std::vector<TextQuad> layout_text(const FontAtlas& font, const std::string& text,
                                         Anchor anchor, mosaic::foundation::math::Vec2 pos,
                                         float height, float aspect,
                                         mosaic::foundation::math::Vec3 color, float alpha,
                                         const Rect& frame = Rect{0.0f, 0.0f, 1.0f, 1.0f},
                                         float wrap_width = 0.0f)
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

    // Break into lines, then measure each. `wrap_width` is a frame-fraction; the break search
    // runs in atlas pixels, which is the unit glyph advances are in, so convert once here.
    // sx == 0 (a zero `height`) would divide by zero and every glyph is zero-wide anyway, so
    // it falls through to "no wrap".
    const float max_advance =
        (wrap_width > 0.0f && sx > 0.0f) ? (wrap_width * frame.w) / sx : 0.0f;
    const std::vector<TextLine> lines = break_lines(font, codepoints, max_advance);

    // The block is as wide as its widest line and as tall as its line count.
    float widest = 0.0f;
    for (const TextLine& ln : lines)
    {
        if (ln.advance > widest) { widest = ln.advance; }
    }
    const float w = widest * sx;

    // Pass the box size as frame-fractions so resolve_in maps it back to the right screen
    // rect: box.w = (w/frame.w)*frame.w = w, box.h = box_h*frame.h. Screen frame is a no-op.
    // A single line gives box_h == 1.0f * height, which is exactly `height`.
    const float box_w = (frame.w > 0.0f) ? w / frame.w : w;
    const float box_h = static_cast<float>(lines.size()) * height;
    const Rect  box =
        resolve_in(frame, anchor, pos, mosaic::foundation::math::Vec2{box_w, box_h});

    // The anchor's horizontal band, reused as the per-line alignment fraction. 0 = flush
    // left inside the block, 0.5 = centred on the block, 1 = flush right -- which for the
    // widest line is a zero offset in all three cases.
    //
    // The offset is taken against `widest` (a measured advance) rather than against `box.w`
    // (which resolve_in produced as (w/frame.w)*frame.w, NOT bit-identically `w` for a
    // frame width that is not a power of two). Using box.w would cost the single-line path
    // its bit-identity for a nested label, for no gain.
    const int   band  = static_cast<int>(anchor) % 3;
    const float align = (band == 0) ? 0.0f : ((band == 1) ? 0.5f : 1.0f);
    const float line_h = font.line_height * scale; // one line's height in normalized Y

    for (std::size_t li = 0; li < lines.size(); ++li)
    {
        const TextLine& ln = lines[li];
        float       pen_x = box.x + (widest - ln.advance) * sx * align;
        const float baseline =
            box.y + static_cast<float>(li) * line_h + font.ascent * scale;

        for (std::size_t k = ln.begin; k < ln.end; ++k)
        {
            const Glyph* resolved = resolve_glyph(font, codepoints[k]);
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
    }
    return out;
}
} // namespace mosaic::engine::ui
