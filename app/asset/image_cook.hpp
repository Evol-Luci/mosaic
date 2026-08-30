#pragma once

#include <cctype>
#include <cstdint>
#include <optional>
#include <string>

#include "engine/render/image.hpp"

namespace mosaic::app
{
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
        if (s.empty()) { return false; }
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
    const std::size_t need = static_cast<std::size_t>(w) * h * 4;
    if (bytes.size() - 16 != need) { return std::nullopt; }
    mosaic::engine::render::Image img;
    img.width = w;
    img.height = h;
    img.pixels.assign(bytes.begin() + 16, bytes.end());
    return img;
}
} // namespace mosaic::app
