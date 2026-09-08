#pragma once

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

#include "engine/ui/font_atlas.hpp"

namespace mosaic::app
{
// Rasterize ASCII 32..126 from a TTF/OTF byte blob at `pixel_size` into a FontAtlas.
// nullopt on an unparseable font. Deterministic for a given (bytes, pixel_size).
std::optional<mosaic::engine::ui::FontAtlas>
read_ttf(const std::string& bytes, float pixel_size);

namespace detail_font
{
inline void put_u32(std::string& out, std::uint32_t v)
{
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
    out.push_back(static_cast<char>((v >> 16) & 0xFF));
    out.push_back(static_cast<char>((v >> 24) & 0xFF));
}
inline void put_f32(std::string& out, float f)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &f, 4);
    put_u32(out, bits);
}
inline std::uint32_t get_u32(const std::string& in, std::size_t off)
{
    return static_cast<std::uint32_t>(static_cast<unsigned char>(in[off])) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(in[off + 1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(in[off + 2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(in[off + 3])) << 24);
}
inline float get_f32(const std::string& in, std::size_t off)
{
    const std::uint32_t bits = get_u32(in, off);
    float f = 0;
    std::memcpy(&f, &bits, 4);
    return f;
}
} // namespace detail_font

inline std::string cook_font(const mosaic::engine::ui::FontAtlas& font)
{
    using namespace detail_font;
    std::string out;
    out += "MOSAFNT1";
    put_f32(out, font.pixel_size);
    put_f32(out, font.ascent);
    put_f32(out, font.descent);
    put_f32(out, font.line_height);
    put_u32(out, font.atlas.width);
    put_u32(out, font.atlas.height);
    put_u32(out, static_cast<std::uint32_t>(font.glyphs.size()));
    for (const auto& [cp, g] : font.glyphs)
    {
        put_u32(out, cp);
        put_f32(out, g.u0); put_f32(out, g.v0); put_f32(out, g.u1); put_f32(out, g.v1);
        put_f32(out, g.advance); put_f32(out, g.x_off); put_f32(out, g.y_off);
        put_f32(out, g.width); put_f32(out, g.height);
    }
    out.append(reinterpret_cast<const char*>(font.atlas.pixels.data()),
               font.atlas.pixels.size());
    return out;
}

inline std::optional<mosaic::engine::ui::FontAtlas> uncook_font(const std::string& bytes)
{
    using namespace detail_font;
    // magic(8) + 4 f32(16) + 3 u32(12) = 36-byte header minimum.
    if (bytes.size() < 36 || bytes.compare(0, 8, "MOSAFNT1") != 0) { return std::nullopt; }
    mosaic::engine::ui::FontAtlas font;
    std::size_t off = 8;
    font.pixel_size  = get_f32(bytes, off); off += 4;
    font.ascent      = get_f32(bytes, off); off += 4;
    font.descent     = get_f32(bytes, off); off += 4;
    font.line_height = get_f32(bytes, off); off += 4;
    font.atlas.width  = get_u32(bytes, off); off += 4;
    font.atlas.height = get_u32(bytes, off); off += 4;
    const std::uint32_t glyph_count = get_u32(bytes, off); off += 4;

    constexpr std::size_t kGlyphBytes = 4 + 9 * 4; // u32 cp + 9 f32
    const std::size_t px_bytes =
        static_cast<std::size_t>(font.atlas.width) * font.atlas.height * 4u;
    if (bytes.size() != off + glyph_count * kGlyphBytes + px_bytes) { return std::nullopt; }

    for (std::uint32_t i = 0; i < glyph_count; ++i)
    {
        const std::uint32_t cp = get_u32(bytes, off); off += 4;
        mosaic::engine::ui::Glyph g;
        g.u0 = get_f32(bytes, off); off += 4; g.v0 = get_f32(bytes, off); off += 4;
        g.u1 = get_f32(bytes, off); off += 4; g.v1 = get_f32(bytes, off); off += 4;
        g.advance = get_f32(bytes, off); off += 4;
        g.x_off = get_f32(bytes, off); off += 4; g.y_off = get_f32(bytes, off); off += 4;
        g.width = get_f32(bytes, off); off += 4; g.height = get_f32(bytes, off); off += 4;
        font.glyphs.emplace(cp, g);
    }
    font.atlas.pixels.assign(bytes.begin() + static_cast<std::ptrdiff_t>(off), bytes.end());
    return font;
}
} // namespace mosaic::app
