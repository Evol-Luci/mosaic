#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "app/asset/image_cook.hpp"
#include "engine/render/image.hpp"

using mosaic::app::cook_image;
using mosaic::app::read_ppm;
using mosaic::app::uncook_image;
using mosaic::engine::render::Image;

namespace
{
// A 2x1 binary PPM: header "P6 2 1 255\n" then 2 RGB pixels (red, green).
std::string tiny_ppm()
{
    std::string s = "P6\n2 1\n255\n";
    const unsigned char px[] = {255, 0, 0, 0, 255, 0};
    s.append(reinterpret_cast<const char*>(px), sizeof(px));
    return s;
}
} // namespace

TEST_CASE("read_ppm parses a binary P6 image to RGBA", "[app]")
{
    auto img = read_ppm(tiny_ppm());
    REQUIRE(img.has_value());
    REQUIRE(img->width == 2);
    REQUIRE(img->height == 1);
    REQUIRE(img->pixels.size() == 2 * 1 * 4);
    // pixel 0 = red, opaque
    REQUIRE(img->pixels[0] == 255); REQUIRE(img->pixels[1] == 0);
    REQUIRE(img->pixels[2] == 0);   REQUIRE(img->pixels[3] == 255);
    // pixel 1 = green, opaque
    REQUIRE(img->pixels[4] == 0);   REQUIRE(img->pixels[5] == 255);
    REQUIRE(img->pixels[6] == 0);   REQUIRE(img->pixels[7] == 255);
}

TEST_CASE("read_ppm rejects malformed input", "[app]")
{
    REQUIRE_FALSE(read_ppm("not a ppm").has_value());
    REQUIRE_FALSE(read_ppm("P6\n2 2\n255\nXYZ").has_value()); // too few pixel bytes
    REQUIRE_FALSE(read_ppm("P6\n2 2\n128\n").has_value());     // maxval != 255
}

TEST_CASE("cook_image and uncook_image round-trip an Image", "[app]")
{
    Image src; src.width = 2; src.height = 1;
    src.pixels = {255, 0, 0, 255,  0, 255, 0, 255};
    const std::string blob = cook_image(src);
    REQUIRE(blob.compare(0, 8, "MOSAIMG1") == 0);

    auto back = uncook_image(blob);
    REQUIRE(back.has_value());
    REQUIRE(back->width == 2);
    REQUIRE(back->height == 1);
    REQUIRE(back->pixels == src.pixels);

    REQUIRE_FALSE(uncook_image("MOSAIMG1short").has_value()); // truncated
    REQUIRE_FALSE(uncook_image("BADMAGIC01234567").has_value());
}

TEST_CASE("uncook_image rejects zero dimensions and wrong-length payload", "[app]")
{
    auto put_u32_le = [](std::string& s, std::uint32_t v) {
        s.push_back(static_cast<char>(v & 0xFF));
        s.push_back(static_cast<char>((v >> 8) & 0xFF));
        s.push_back(static_cast<char>((v >> 16) & 0xFF));
        s.push_back(static_cast<char>((v >> 24) & 0xFF));
    };

    // Zero-width image: valid magic, h=1, but w=0 → nullopt.
    {
        std::string blob = "MOSAIMG1";
        put_u32_le(blob, 0u); // w = 0
        put_u32_le(blob, 1u); // h = 1
        // no pixels (0 * 1 * 4 = 0 bytes) — size check would pass, but zero-dim guard fires first
        REQUIRE_FALSE(uncook_image(blob).has_value());
    }

    // Wrong-length payload: valid magic, w=2, h=1 (claims 8 RGBA bytes), but only 4 appended.
    {
        std::string blob = "MOSAIMG1";
        put_u32_le(blob, 2u); // w = 2
        put_u32_le(blob, 1u); // h = 1  → need = 8 bytes
        blob.append(4, '\x00'); // only 4 bytes
        REQUIRE_FALSE(uncook_image(blob).has_value());
    }

    // Overflow guard: w = h = 0x80000000 → w*h*4 wraps to 0 on 64-bit; guard catches it.
    {
        std::string blob = "MOSAIMG1";
        put_u32_le(blob, 0x80000000u);
        put_u32_le(blob, 0x80000000u);
        // no pixel bytes
        REQUIRE_FALSE(uncook_image(blob).has_value());
    }
}

namespace
{
std::string read_fixture(const char* name)
{
    const std::string path = std::string(MOSAIC_TEST_DIR) + "/fixtures/" + name;
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
} // namespace

TEST_CASE("read_image decodes a PNG to RGBA with known pixels", "[asset]")
{
    // Values chosen so a channel swap or a row flip is visible rather than plausible:
    // row y has G = y*80 and B = 255 throughout.
    const auto img = mosaic::app::read_image(read_fixture("gradient.png"));
    REQUIRE(img.has_value());
    REQUIRE(img->width == 4);
    REQUIRE(img->height == 4);
    REQUIRE(img->pixels.size() == 4u * 4u * 4u);
    REQUIRE(img->pixels[0] == 0);
    REQUIRE(img->pixels[1] == 0);
    REQUIRE(img->pixels[2] == 255);
    REQUIRE(img->pixels[3] == 255);
    // Row 2 pixel 0 has G = 160. Catches a vertical flip, which stb_image can be
    // configured to do and which would otherwise look like a correct decode.
    REQUIRE(img->pixels[(2 * 4 + 0) * 4 + 1] == 160);
}

TEST_CASE("read_image still accepts a PPM, so the fallback works", "[asset]")
{
    // read_ppm is the only decoder with no third-party code in its path, which makes
    // it the honest fixture for everything downstream of decode. It must keep working.
    const auto img = mosaic::app::read_image(tiny_ppm());
    REQUIRE(img.has_value());
    REQUIRE(img->width == 2);
    REQUIRE(img->pixels[0] == 255); // red
    REQUIRE(img->pixels[5] == 255); // green
}

TEST_CASE("read_image rejects malformed input instead of throwing", "[asset]")
{
    // Review Focus 4. stb_image is the first untrusted-input parser in the asset path:
    // a project directory can now hold a hostile file. Every one of these must be
    // nullopt -- not a throw, not a half-decoded Image, not a read past the buffer.
    REQUIRE_FALSE(mosaic::app::read_image("").has_value());
    REQUIRE_FALSE(mosaic::app::read_image("not an image at all").has_value());
    REQUIRE_FALSE(mosaic::app::read_image(std::string("\x89PNG\r\n\x1a\n", 8)).has_value());
    const std::string full = read_fixture("gradient.png");
    REQUIRE_FALSE(mosaic::app::read_image(full.substr(0, full.size() / 2)).has_value());
}

TEST_CASE("a PNG survives the cook/uncook round trip", "[asset]")
{
    // The chain the importer actually runs: decode -> MOSAIMG1 -> decode. If this
    // drifts, an imported texture is wrong on disk while read_image's own test passes.
    const auto decoded = mosaic::app::read_image(read_fixture("gradient.png"));
    REQUIRE(decoded.has_value());
    const auto restored = mosaic::app::uncook_image(mosaic::app::cook_image(*decoded));
    REQUIRE(restored.has_value());
    REQUIRE(restored->width == decoded->width);
    REQUIRE(restored->height == decoded->height);
    REQUIRE(restored->pixels == decoded->pixels);
}
