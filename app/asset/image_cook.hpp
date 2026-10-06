#pragma once

#include <cctype>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>

#include "engine/render/image.hpp"

namespace mosaic::app
{
// IEEE 754 binary16 from a binary32, as the raw 16 bits.
//
// Round-toward-zero on the mantissa, deliberately: it cannot turn a finite input into
// an infinity, which matters for a radiance value the shader will go on to multiply.
// Values beyond half's largest finite (65504) clamp to it rather than becoming inf, for
// the same reason. Subnormals flush to zero -- a sky has no use for 1e-9, and carrying
// them would cost precision care for nothing.
inline std::uint16_t float_to_half(float f)
{
    std::uint32_t bits = 0;
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    std::memcpy(&bits, &f, sizeof(bits));

    const std::uint16_t sign = static_cast<std::uint16_t>((bits >> 16) & 0x8000u);
    const std::int32_t exp = static_cast<std::int32_t>((bits >> 23) & 0xFFu) - 127;
    const std::uint32_t mant = bits & 0x7FFFFFu;

    if (exp == 128) // inf or NaN
    {
        // NaN keeps a non-zero mantissa so it stays NaN; inf clamps to the largest
        // finite half, consistent with the overflow rule below.
        return static_cast<std::uint16_t>(sign | (mant != 0 ? 0x7E00u : 0x7BFFu));
    }
    if (exp > 15) { return static_cast<std::uint16_t>(sign | 0x7BFFu); } // clamp, not inf
    if (exp < -14) { return sign; }                                      // flush subnormals

    const std::uint16_t e = static_cast<std::uint16_t>((exp + 15) & 0x1F);
    const std::uint16_t m = static_cast<std::uint16_t>(mant >> 13); // toward zero
    return static_cast<std::uint16_t>(sign | (e << 10) | m);
}

// The inverse of float_to_half. Needed wherever half pixels are blended, because
// arithmetic on the raw bit patterns would be meaningless on an exponent field.
//
// Exact for every NORMAL finite half -- binary32 covers binary16's whole range. The
// round trip is NOT exact for a subnormal half, because float_to_half flushes
// subnormals to zero; this direction still decodes them properly, for data this engine
// did not write. (The earlier wording claimed exactness "in both directions for every
// finite half", which is false for exactly that case. Review 2026-10-05.)
inline float half_to_float(std::uint16_t h)
{
    const std::uint32_t sign = static_cast<std::uint32_t>(h & 0x8000u) << 16;
    const std::uint32_t exp = (h >> 10) & 0x1Fu;
    const std::uint32_t mant = h & 0x3FFu;

    std::uint32_t bits = 0;
    if (exp == 0)
    {
        // Zero or subnormal. float_to_half flushes subnormals to zero, so a subnormal
        // can only arrive from data this engine did not write; normalise it properly
        // rather than silently treating it as zero.
        if (mant != 0)
        {
            std::uint32_t e = 127 - 15 + 1;
            std::uint32_t m = mant;
            while ((m & 0x400u) == 0) { m <<= 1; --e; }
            m &= 0x3FFu;
            bits = sign | (e << 23) | (m << 13);
        }
        else
        {
            bits = sign;
        }
    }
    else if (exp == 31)
    {
        bits = sign | 0x7F800000u | (mant << 13); // inf or NaN
    }
    else
    {
        bits = sign | ((exp - 15 + 127) << 23) | (mant << 13);
    }

    float f = 0.0f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

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

// The cooked on-disk form: "MOSAIMG2" + u32 width + u32 height + u32 format + pixels.
//
// MOSAIMG1 was the same without the format tag and implicitly Rgba8. uncook_image still
// reads it, because every texture cooked before the HDR path existed is one -- dropping
// that branch would stop existing projects loading, and the symptom would be a missing
// texture rather than an error anyone would trace back to a format change.
inline std::string cook_image(const mosaic::engine::render::Image& image)
{
    std::string out;
    out.reserve(20 + image.pixels.size());
    out += "MOSAIMG2";
    auto put_u32 = [&](std::uint32_t v) {
        out.push_back(static_cast<char>(v & 0xFF));
        out.push_back(static_cast<char>((v >> 8) & 0xFF));
        out.push_back(static_cast<char>((v >> 16) & 0xFF));
        out.push_back(static_cast<char>((v >> 24) & 0xFF));
    };
    put_u32(image.width);
    put_u32(image.height);
    put_u32(static_cast<std::uint32_t>(image.format));
    out.append(reinterpret_cast<const char*>(image.pixels.data()), image.pixels.size());
    return out;
}

// Inverse of cook_image. Accepts MOSAIMG2 (with a format tag) and MOSAIMG1 (implicitly
// Rgba8). nullopt on a bad magic, an unknown format, truncation, or a size mismatch.
inline std::optional<mosaic::engine::render::Image> uncook_image(const std::string& bytes)
{
    using mosaic::engine::render::ImageFormat;
    const bool v2 = bytes.size() >= 8 && bytes.compare(0, 8, "MOSAIMG2") == 0;
    const bool v1 = bytes.size() >= 8 && bytes.compare(0, 8, "MOSAIMG1") == 0;
    if (!v1 && !v2) { return std::nullopt; }
    const std::size_t header = v2 ? 20u : 16u;
    if (bytes.size() < header) { return std::nullopt; }

    auto get_u32 = [&](std::size_t off) -> std::uint32_t {
        return static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[off])) |
               (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[off + 1])) << 8) |
               (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[off + 2])) << 16) |
               (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[off + 3])) << 24);
    };
    const std::uint32_t w = get_u32(8);
    const std::uint32_t h = get_u32(12);
    if (w == 0 || h == 0) { return std::nullopt; }

    ImageFormat format = ImageFormat::Rgba8;
    if (v2)
    {
        const std::uint32_t tag = get_u32(16);
        if (tag > static_cast<std::uint32_t>(ImageFormat::Rgba16F)) { return std::nullopt; }
        format = static_cast<ImageFormat>(tag);
    }

    // Overflow guard, by DIVISION rather than by computing the product and checking it.
    // w and h are u32 so w*h cannot overflow a u64 -- but multiplying that by the
    // bytes-per-pixel can: at w = h = 0x80000000 the product is 2^64 exactly, which
    // wraps to 0 and then matches a zero-length payload. The pre-existing test for
    // exactly this caught it when the HDR format tag was added.
    const std::uint64_t pixel_count =
        static_cast<std::uint64_t>(w) * static_cast<std::uint64_t>(h);
    const std::uint64_t bpp = static_cast<std::uint64_t>(bytes_per_pixel(format));
    if (pixel_count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) / bpp)
    {
        return std::nullopt;
    }
    const std::size_t need = static_cast<std::size_t>(pixel_count * bpp);
    if (bytes.size() - header != need) { return std::nullopt; }

    mosaic::engine::render::Image img;
    img.width = w;
    img.height = h;
    img.format = format;
    img.pixels.assign(bytes.begin() + static_cast<std::ptrdiff_t>(header), bytes.end());
    return img;
}
} // namespace mosaic::app
