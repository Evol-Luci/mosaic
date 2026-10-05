#pragma once

#include <cctype>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

#include "engine/render/image.hpp"

namespace mosaic::app
{
// Decodes PNG/JPEG/BMP/TGA via stb_image, falling back to read_ppm so the
// dependency-free format keeps working. nullopt on any failure -- malformed input is
// reachable from a project directory, so this must not throw or partially decode.
//
// Not inline, unlike everything else in this header: stb_image's implementation macro
// belongs in exactly one translation unit (image_cook.cpp), the same arrangement
// stb_truetype already has in font_cook.cpp.
std::optional<mosaic::engine::render::Image> read_image(const std::string& bytes);

// The largest image read_image will decode, in pixels. 64 megapixels is an 8192x8192
// texture -- larger than anything this engine can usefully sample, and far larger than
// any asset it ships.
//
// The ceiling exists because a few kilobytes of PNG can declare enormous dimensions: a
// 20000x20000 header decodes to 1.6 GB inside stb, and copying it into an Image asks
// for as much again. That is a decompression bomb, read_image is the first untrusted
// parser in the asset path, and the function is documented never to throw. Checking the
// HEADER before decoding is what makes the refusal cost nothing.
inline constexpr std::uint64_t kMaxImagePixels = 64ull * 1024ull * 1024ull;

// Whether an image of these dimensions is within the ceiling. Pure, and separated from
// read_image so the POLICY can be tested exactly: a test that fed read_image a header
// without pixel data passed whether or not the ceiling existed, because the decode
// failed first -- it proved nothing. Found in review 2026-10-04.
inline constexpr bool image_dimensions_are_sane(std::uint64_t w, std::uint64_t h)
{
    return w > 0 && h > 0 && w * h <= kMaxImagePixels;
}

// Parse a binary PPM (P6): "P6" <ws> width <ws> height <ws> maxval <single-ws> <RGB bytes>.
// Produces an RGBA Image (alpha 255). nullopt on malformed input or maxval != 255. Comments
// (# lines) are not supported (v0.1). Little dependency: only <cctype>.
inline std::optional<mosaic::engine::render::Image> read_ppm(const std::string& bytes)
{
    std::size_t pos = 0;
    auto skip_ws = [&] {
        while (pos < bytes.size() && std::isspace(static_cast<unsigned char>(bytes[pos]))) { ++pos; }
    };
    auto token = [&]() -> std::string {
        skip_ws();
        const std::size_t start = pos;
        while (pos < bytes.size() && !std::isspace(static_cast<unsigned char>(bytes[pos]))) { ++pos; }
        return bytes.substr(start, pos - start);
    };
    auto to_uint = [](const std::string& s, std::uint32_t& out) -> bool {
        if (s.empty() || s.size() > 10) { return false; }
        std::uint32_t v = 0;
        for (char c : s) { if (c < '0' || c > '9') { return false; } v = v * 10 + static_cast<std::uint32_t>(c - '0'); }
        out = v; return true;
    };

    if (token() != "P6") { return std::nullopt; }
    std::uint32_t w = 0, h = 0, maxval = 0;
    if (!to_uint(token(), w) || !to_uint(token(), h) || !to_uint(token(), maxval)) { return std::nullopt; }
    if (maxval != 255 || w == 0 || h == 0) { return std::nullopt; }
    // Exactly one whitespace byte separates the header from the pixel data.
    if (pos >= bytes.size() || !std::isspace(static_cast<unsigned char>(bytes[pos]))) { return std::nullopt; }
    ++pos;

    // Guard against overflow: w*h could wrap to 0 for huge values.
    if (static_cast<std::uint64_t>(w) * static_cast<std::uint64_t>(h) >
        (static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) / 4u))
    {
        return std::nullopt;
    }
    const std::size_t need = static_cast<std::size_t>(w) * h * 3;
    if (bytes.size() - pos < need) { return std::nullopt; }

    mosaic::engine::render::Image img;
    img.width = w;
    img.height = h;
    img.pixels.resize(static_cast<std::size_t>(w) * h * 4);
    for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h; ++i)
    {
        img.pixels[i * 4 + 0] = static_cast<std::uint8_t>(bytes[pos + i * 3 + 0]);
        img.pixels[i * 4 + 1] = static_cast<std::uint8_t>(bytes[pos + i * 3 + 1]);
        img.pixels[i * 4 + 2] = static_cast<std::uint8_t>(bytes[pos + i * 3 + 2]);
        img.pixels[i * 4 + 3] = 255;
    }
    return img;
}

// The cooked on-disk form: "MOSAIMG1" + u32 width + u32 height (LE) + RGBA bytes.
inline std::string cook_image(const mosaic::engine::render::Image& image)
{
    std::string out;
    out.reserve(16 + image.pixels.size());
    out += "MOSAIMG1";
    auto put_u32 = [&](std::uint32_t v) {
        out.push_back(static_cast<char>(v & 0xFF));
        out.push_back(static_cast<char>((v >> 8) & 0xFF));
        out.push_back(static_cast<char>((v >> 16) & 0xFF));
        out.push_back(static_cast<char>((v >> 24) & 0xFF));
    };
    put_u32(image.width);
    put_u32(image.height);
    out.append(reinterpret_cast<const char*>(image.pixels.data()), image.pixels.size());
    return out;
}

// Inverse of cook_image. nullopt on a bad magic, truncation, or a size mismatch.
inline std::optional<mosaic::engine::render::Image> uncook_image(const std::string& bytes)
{
    if (bytes.size() < 16 || bytes.compare(0, 8, "MOSAIMG1") != 0) { return std::nullopt; }
    auto get_u32 = [&](std::size_t off) -> std::uint32_t {
        return static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[off])) |
               (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[off + 1])) << 8) |
               (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[off + 2])) << 16) |
               (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[off + 3])) << 24);
    };
    const std::uint32_t w = get_u32(8);
    const std::uint32_t h = get_u32(12);
    if (w == 0 || h == 0) { return std::nullopt; }
    // Guard against overflow: w*h could wrap to 0 for huge values.
    if (static_cast<std::uint64_t>(w) * static_cast<std::uint64_t>(h) >
        (static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) / 4u))
    {
        return std::nullopt;
    }
    const std::size_t need = static_cast<std::size_t>(w) * h * 4;
    if (bytes.size() - 16 != need) { return std::nullopt; }
    mosaic::engine::render::Image img;
    img.width = w;
    img.height = h;
    img.pixels.assign(bytes.begin() + 16, bytes.end());
    return img;
}
} // namespace mosaic::app
