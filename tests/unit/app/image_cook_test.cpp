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
    // cook_image writes MOSAIMG2 since the HDR path landed -- it carries a format tag.
    // MOSAIMG1 is still READ, which "a MOSAIMG1 blob still loads as Rgba8" below pins;
    // this assertion is about what we write, that one about what we must keep accepting.
    REQUIRE(blob.compare(0, 8, "MOSAIMG2") == 0);

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

TEST_CASE("read_image routes PPM through read_ppm, not through stb", "[asset]")
{
    // The fallback is a stated deliverable -- read_ppm is the one decoder with no
    // third-party code in its path -- but stb_image decodes PNM too, so with its PNM
    // support left on, read_ppm was UNREACHABLE and the test claiming to cover the
    // fallback ran entirely through stb. Found in review 2026-10-04.
    //
    // maxval is how the two decoders differ observably: read_ppm requires 255, while
    // stb accepts any maxval <= 65535 and copies the bytes with NO rescaling -- so a
    // legal "P6 2 1 15" imported as a near-black texture instead of failing loudly.
    // Asserting the rejection pins that read_ppm is what handles PPM.
    std::string ppm = "P6 2 1 15 ";
    const unsigned char px[] = {1, 2, 3, 4, 5, 6};
    ppm.append(reinterpret_cast<const char*>(px), sizeof(px));
    REQUIRE_FALSE(read_ppm(ppm).has_value());
    REQUIRE_FALSE(mosaic::app::read_image(ppm).has_value());
}

TEST_CASE("the image size ceiling rejects a decompression bomb's dimensions", "[asset]")
{
    // A few kilobytes of PNG can declare enormous dimensions: 20000x20000 decodes to
    // 1.6 GB inside stb and the copy into Image asks for as much again, so a bad_alloc
    // escapes a function documented never to throw. read_image checks the HEADER before
    // decoding, which is what makes the refusal free.
    //
    // This tests the POLICY directly rather than feeding read_image a crafted file. The
    // first attempt did the latter, with a header carrying no pixel data -- and it
    // passed whether or not the ceiling existed, because the decode failed first. It
    // proved nothing. Found while writing it, review 2026-10-04.
    using mosaic::app::image_dimensions_are_sane;
    REQUIRE(image_dimensions_are_sane(4, 4));
    REQUIRE(image_dimensions_are_sane(8192, 8192));      // exactly the ceiling
    REQUIRE_FALSE(image_dimensions_are_sane(8192, 8193)); // one pixel over
    REQUIRE_FALSE(image_dimensions_are_sane(20000, 20000));
    REQUIRE_FALSE(image_dimensions_are_sane(0, 4));
    REQUIRE_FALSE(image_dimensions_are_sane(4, 0));
    // And a real image still decodes, so the ceiling is not simply refusing everything.
    REQUIRE(mosaic::app::read_image(read_fixture("gradient.png")).has_value());
}

TEST_CASE("an Rgba16F image round-trips through cook and uncook", "[asset]")
{
    // Review Focus 2. Image has had one stride for its whole life, and cook_image,
    // uncook_image and the device all compute sizes from it. A format that is 8 bytes
    // per pixel is the first thing to challenge that.
    using mosaic::engine::render::ImageFormat;
    Image img;
    img.width = 2;
    img.height = 1;
    img.format = ImageFormat::Rgba16F;
    img.pixels.assign(2u * 1u * 8u, 0);
    img.pixels[0] = 0x00; img.pixels[1] = 0x3C; // half 1.0, little-endian
    img.pixels[8] = 0x00; img.pixels[9] = 0x42; // half 3.0

    const auto restored = uncook_image(cook_image(img));
    REQUIRE(restored.has_value());
    REQUIRE(restored->format == ImageFormat::Rgba16F);
    REQUIRE(restored->width == 2);
    REQUIRE(restored->pixels == img.pixels);
}

TEST_CASE("a MOSAIMG1 blob still loads as Rgba8", "[asset]")
{
    // Review Focus 3. Every texture already cooked on disk is a MOSAIMG1. If this
    // breaks, existing projects stop loading -- and the failure is a missing texture,
    // not an error anyone would trace back to a format change.
    std::string blob = "MOSAIMG1";
    auto put_u32 = [&](std::uint32_t v) {
        blob.push_back(static_cast<char>(v & 0xFF));
        blob.push_back(static_cast<char>((v >> 8) & 0xFF));
        blob.push_back(static_cast<char>((v >> 16) & 0xFF));
        blob.push_back(static_cast<char>((v >> 24) & 0xFF));
    };
    put_u32(1);
    put_u32(1);
    const unsigned char rgba[] = {10, 20, 30, 40};
    blob.append(reinterpret_cast<const char*>(rgba), 4);

    const auto img = uncook_image(blob);
    REQUIRE(img.has_value());
    REQUIRE(img->format == mosaic::engine::render::ImageFormat::Rgba8);
    REQUIRE(img->width == 1);
    REQUIRE(img->pixels.size() == 4);
    REQUIRE(img->pixels[1] == 20);
}

TEST_CASE("bytes_per_pixel matches the two formats", "[asset]")
{
    using mosaic::engine::render::bytes_per_pixel;
    using mosaic::engine::render::ImageFormat;
    REQUIRE(bytes_per_pixel(ImageFormat::Rgba8) == 4);
    REQUIRE(bytes_per_pixel(ImageFormat::Rgba16F) == 8);
}

TEST_CASE("read_image loads a Radiance HDR as Rgba16F preserving values above 1", "[asset]")
{
    // The whole reason the HDR path exists: an LDR sky clips the sun. If values above
    // 1.0 do not survive the float-to-half conversion, the format change bought nothing.
    const std::string path =
        std::string(MOSAIC_TEST_DIR) + "/../assets/backgrounds/space00_2k.hdr";
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();

    const auto img = mosaic::app::read_image(ss.str());
    REQUIRE(img.has_value());
    REQUIRE(img->format == mosaic::engine::render::ImageFormat::Rgba16F);
    REQUIRE(img->width == 2048);
    REQUIRE(img->height == 1024);
    REQUIRE(img->pixels.size() == 2048u * 1024u * 8u);

    // At least one channel above 1.0 must survive. Asserting "> 1.0" rather than a
    // specific peak keeps this from breaking if the asset is ever re-derived at another
    // size -- downsampling lowers the peak but never removes the HDR-ness entirely.
    bool any_above_one = false;
    for (std::size_t i = 0; i + 1 < img->pixels.size(); i += 2)
    {
        const std::uint16_t h =
            static_cast<std::uint16_t>(img->pixels[i] | (img->pixels[i + 1] << 8));
        // half > 1.0: positive, and biased exponent (bits 10..14) above 15.
        if ((h & 0x8000) == 0 && ((h >> 10) & 0x1F) > 15) { any_above_one = true; break; }
    }
    REQUIRE(any_above_one);
}

TEST_CASE("float_to_half round-trips the values that matter", "[asset]")
{
    // Round-toward-zero on the mantissa is chosen deliberately: it cannot turn a finite
    // input into an infinity, which matters for a value the shader will multiply.
    using mosaic::app::float_to_half;
    REQUIRE(float_to_half(0.0f) == 0x0000);
    REQUIRE(float_to_half(1.0f) == 0x3C00);
    REQUIRE(float_to_half(2.0f) == 0x4000);
    REQUIRE(float_to_half(-1.0f) == 0xBC00);
    // Above half's maximum clamps to it rather than becoming inf.
    REQUIRE(float_to_half(1.0e9f) == 0x7BFF);   // 65504, half's largest finite
    REQUIRE(float_to_half(-1.0e9f) == 0xFBFF);
    // Subnormals flush to zero; a sky has no use for 1e-9 and they cost precision care.
    REQUIRE(float_to_half(1.0e-9f) == 0x0000);
}
