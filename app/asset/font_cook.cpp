#include "app/asset/font_cook.hpp"

#include <vector>

#include "engine/ui/utf8.hpp" // kReplacementCodepoint

#define STB_TRUETYPE_IMPLEMENTATION
#include "third_party/stb/stb_truetype.h"

namespace mosaic::app
{
std::optional<mosaic::engine::ui::FontAtlas>
read_ttf(const std::string& bytes, float pixel_size)
{
    using mosaic::engine::ui::FontAtlas;
    using mosaic::engine::ui::Glyph;

    const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
    stbtt_fontinfo info;
    const int offset = stbtt_GetFontOffsetForIndex(data, 0);
    if (offset < 0 || stbtt_InitFont(&info, data, offset) == 0) { return std::nullopt; }

    const float scale = stbtt_ScaleForPixelHeight(&info, pixel_size);
    int asc = 0, desc = 0, gap = 0;
    stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);

    constexpr std::uint32_t kFirst = 32, kLast = 126;
    constexpr int kAtlasW = 512, kPad = 1;

    // The cooked set: printable ASCII, plus U+FFFD *only if the font actually provides it*.
    // layout_text draws U+FFFD for any printable codepoint the atlas lacks; baking it here is
    // the one-codepoint addition U11a allows, not a general widening of the range.
    //
    // The test is stbtt_FindGlyphIndex != 0. A bbox/advance test would NOT do: for a font
    // without U+FFFD, stb falls back to glyph 0 (.notdef), which in many fonts (the bundled
    // DemoSans among them) is a drawn box with a real advance. Baking that would silently
    // smuggle .notdef in under the name U+FFFD and defeat layout_text's '?' fallback.
    std::vector<std::uint32_t> cooked;
    cooked.reserve(kLast - kFirst + 2);
    for (std::uint32_t cp = kFirst; cp <= kLast; ++cp) { cooked.push_back(cp); }
    if (stbtt_FindGlyphIndex(&info, static_cast<int>(mosaic::engine::ui::kReplacementCodepoint)) != 0)
    {
        cooked.push_back(mosaic::engine::ui::kReplacementCodepoint);
    }

    // Pass 1: metrics + shelf placement; compute atlas height.
    struct Placed { std::uint32_t cp; int gw, gh, px, py, x0, y0; float advance; };
    std::vector<Placed> placed;
    int cx = kPad, cy = kPad, row_h = 0;
    for (const std::uint32_t cp : cooked)
    {
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&info, static_cast<int>(cp), &adv, &lsb);
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetCodepointBitmapBox(&info, static_cast<int>(cp), scale, scale, &x0, &y0, &x1, &y1);
        const int gw = x1 - x0, gh = y1 - y0;
        int px = 0, py = 0;
        if (gw > 0 && gh > 0)
        {
            if (cx + gw + kPad > kAtlasW) { cx = kPad; cy += row_h + kPad; row_h = 0; }
            px = cx; py = cy;
            cx += gw + kPad;
            if (gh > row_h) { row_h = gh; }
        }
        placed.push_back(Placed{cp, gw, gh, px, py, x0, y0, adv * scale});
    }
    const int atlas_h = cy + row_h + kPad;
    if (atlas_h <= 0) { return std::nullopt; }

    FontAtlas font;
    font.pixel_size  = pixel_size;
    font.ascent      = asc * scale;
    font.descent     = desc * scale;
    font.line_height = (asc - desc + gap) * scale;
    font.atlas.width  = static_cast<std::uint32_t>(kAtlasW);
    font.atlas.height = static_cast<std::uint32_t>(atlas_h);
    font.atlas.pixels.assign(static_cast<std::size_t>(kAtlasW) * atlas_h * 4u, 0);
    // White everywhere; coverage goes in alpha.
    for (std::size_t i = 0; i < font.atlas.pixels.size(); i += 4)
    {
        font.atlas.pixels[i + 0] = 255;
        font.atlas.pixels[i + 1] = 255;
        font.atlas.pixels[i + 2] = 255;
        // alpha stays 0 until a glyph writes it
    }

    // Pass 2: rasterize coverage into the atlas alpha; fill the glyph map.
    for (const Placed& p : placed)
    {
        Glyph g;
        g.advance = p.advance;
        if (p.gw > 0 && p.gh > 0)
        {
            std::vector<unsigned char> cov(static_cast<std::size_t>(p.gw) * p.gh);
            stbtt_MakeCodepointBitmap(&info, cov.data(), p.gw, p.gh, p.gw, scale, scale,
                                      static_cast<int>(p.cp));
            for (int y = 0; y < p.gh; ++y)
            {
                for (int x = 0; x < p.gw; ++x)
                {
                    const std::size_t dst =
                        (static_cast<std::size_t>(p.py + y) * kAtlasW + (p.px + x)) * 4u + 3u;
                    font.atlas.pixels[dst] = cov[static_cast<std::size_t>(y) * p.gw + x];
                }
            }
            g.u0 = static_cast<float>(p.px) / kAtlasW;
            g.v0 = static_cast<float>(p.py) / atlas_h;
            g.u1 = static_cast<float>(p.px + p.gw) / kAtlasW;
            g.v1 = static_cast<float>(p.py + p.gh) / atlas_h;
            g.x_off  = static_cast<float>(p.x0);
            g.y_off  = static_cast<float>(p.y0);
            g.width  = static_cast<float>(p.gw);
            g.height = static_cast<float>(p.gh);
        }
        font.glyphs.emplace(p.cp, g);
    }
    return font;
}
} // namespace mosaic::app
