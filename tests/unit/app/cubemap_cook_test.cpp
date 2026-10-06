// Review Focus 1, and the highest-value tests in this slice.
//
// A face mapped to the wrong axis, or a flipped V, produces a rotated or mirrored sky.
// That reads as a questionable asset rather than a defect, and CI has no GPU -- so the
// direction math is where correctness has to be established. This is the whole reason
// the conversion happens on the CPU at import rather than on the GPU at load.

#include <array>
#include <cmath>
#include <cstdint>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/asset/cubemap_cook.hpp"

using Catch::Approx;
using mosaic::app::cube_face_direction;
using mosaic::app::equirect_to_uv;
using mosaic::engine::render::Image;
using mosaic::engine::render::ImageFormat;
using mosaic::foundation::math::Vec3;

TEST_CASE("each cube face's centre points down its own axis", "[asset]")
{
    // Vulkan layer order: +X, -X, +Y, -Y, +Z, -Z. A wrong order rotates the sky; a
    // wrong sign mirrors it. Neither looks like a bug from inside the engine.
    const Vec3 expected[6] = {
        { 1,  0,  0}, {-1,  0,  0},
        { 0,  1,  0}, { 0, -1,  0},
        { 0,  0,  1}, { 0,  0, -1},
    };
    for (int face = 0; face < 6; ++face)
    {
        INFO("face " << face);
        const Vec3 d = cube_face_direction(face, 0.5f, 0.5f);
        REQUIRE(d.x == Approx(expected[face].x).margin(1e-5));
        REQUIRE(d.y == Approx(expected[face].y).margin(1e-5));
        REQUIRE(d.z == Approx(expected[face].z).margin(1e-5));
    }
}

TEST_CASE("every cube face direction is unit length", "[asset]")
{
    // equirect_to_uv does acos(d.y), which is only meaningful on a unit vector -- an
    // unnormalised direction silently distorts the vertical mapping near the poles.
    for (int face = 0; face < 6; ++face)
    {
        for (float u : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
        {
            for (float v : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
            {
                const Vec3 d = cube_face_direction(face, u, v);
                INFO("face " << face << " u " << u << " v " << v);
                REQUIRE(std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z)
                        == Approx(1.0f).margin(1e-5));
            }
        }
    }
}

TEST_CASE("the equirect mapping puts the poles and the equator where they belong", "[asset]")
{
    // v comes from acos(d.y): +Y is the TOP of the equirect (v = 0), -Y the bottom. A
    // flipped v turns the sky upside down, which is the single most likely mistake here.
    float u = 0, v = 0;
    equirect_to_uv(Vec3{0, 1, 0}, u, v);
    REQUIRE(v == Approx(0.0f).margin(1e-5));
    equirect_to_uv(Vec3{0, -1, 0}, u, v);
    REQUIRE(v == Approx(1.0f).margin(1e-5));
    equirect_to_uv(Vec3{0, 0, 1}, u, v);
    REQUIRE(v == Approx(0.5f).margin(1e-5)); // equator
    equirect_to_uv(Vec3{1, 0, 0}, u, v);
    REQUIRE(u == Approx(0.5f).margin(1e-5)); // atan2(0,1) = 0, plus the 0.5 offset
}

TEST_CASE("direction to equirect UV and back is stable", "[asset]")
{
    // A round trip catches a swapped u/v or a wrong constant that the fixed points above
    // would not: those check six directions, this checks the mapping's shape.
    for (int face = 0; face < 6; ++face)
    {
        const Vec3 d = cube_face_direction(face, 0.3f, 0.7f);
        float u = 0, v = 0;
        equirect_to_uv(d, u, v);
        const float theta = (u - 0.5f) * 2.0f * 3.14159265f;
        const float phi = v * 3.14159265f;
        const Vec3 back{std::sin(phi) * std::cos(theta), std::cos(phi),
                        std::sin(phi) * std::sin(theta)};
        INFO("face " << face);
        REQUIRE(back.x == Approx(d.x).margin(1e-4));
        REQUIRE(back.y == Approx(d.y).margin(1e-4));
        REQUIRE(back.z == Approx(d.z).margin(1e-4));
    }
}

TEST_CASE("a synthetic equirect converts to faces with the expected colours", "[asset]")
{
    // The end-to-end check with a KNOWN image: paint each region a face will sample a
    // distinct colour, convert, and assert each face's centre carries its colour. This
    // catches a u/v swap or an off-by-one in the sampling that the pure direction tests
    // cannot see.
    Image eq;
    eq.width = 64;
    eq.height = 32;
    eq.format = ImageFormat::Rgba8;
    eq.pixels.assign(64u * 32u * 4u, 0);
    auto put = [&](int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        const std::size_t i = (static_cast<std::size_t>(y) * 64 + x) * 4;
        eq.pixels[i] = r; eq.pixels[i + 1] = g; eq.pixels[i + 2] = b; eq.pixels[i + 3] = 255;
    };
    for (int x = 0; x < 64; ++x)
    {
        for (int y = 0; y < 32; ++y)
        {
            if (y < 4)        { put(x, y, 255, 255, 0); }        // +Y top
            else if (y >= 28) { put(x, y, 0, 255, 255); }        // -Y bottom
            else if (x < 8 || x >= 56) { put(x, y, 255, 0, 0); } // -X, u near 0 and 1
            else if (x < 24)  { put(x, y, 0, 0, 255); }          // -Z
            else if (x < 40)  { put(x, y, 0, 255, 0); }          // +X, u near 0.5
            else              { put(x, y, 255, 0, 255); }        // +Z
        }
    }

    const auto faces = mosaic::app::equirect_to_cubemap(eq, 16);
    auto centre = [&](int f) {
        const Image& img = faces[static_cast<std::size_t>(f)];
        const std::size_t i = (static_cast<std::size_t>(8) * 16 + 8) * 4;
        return std::array<std::uint8_t, 3>{img.pixels[i], img.pixels[i + 1], img.pixels[i + 2]};
    };
    REQUIRE(centre(0) == std::array<std::uint8_t, 3>{0, 255, 0});   // +X green
    REQUIRE(centre(1) == std::array<std::uint8_t, 3>{255, 0, 0});   // -X red
    REQUIRE(centre(2) == std::array<std::uint8_t, 3>{255, 255, 0}); // +Y yellow
    REQUIRE(centre(3) == std::array<std::uint8_t, 3>{0, 255, 255}); // -Y cyan
    REQUIRE(centre(4) == std::array<std::uint8_t, 3>{255, 0, 255}); // +Z magenta
    REQUIRE(centre(5) == std::array<std::uint8_t, 3>{0, 0, 255});   // -Z blue
}

TEST_CASE("a cubemap round-trips through cook and uncook", "[asset]")
{
    std::array<Image, 6> faces;
    for (int f = 0; f < 6; ++f)
    {
        auto& img = faces[static_cast<std::size_t>(f)];
        img.width = 2;
        img.height = 2;
        img.format = ImageFormat::Rgba8;
        img.pixels.assign(2u * 2u * 4u, static_cast<std::uint8_t>(f + 1));
    }
    const auto restored = mosaic::app::uncook_cubemap(mosaic::app::cook_cubemap(faces));
    REQUIRE(restored.has_value());
    for (int f = 0; f < 6; ++f)
    {
        INFO("face " << f);
        REQUIRE((*restored)[static_cast<std::size_t>(f)].pixels
                == faces[static_cast<std::size_t>(f)].pixels);
        REQUIRE((*restored)[static_cast<std::size_t>(f)].width == 2);
    }
    REQUIRE_FALSE(mosaic::app::uncook_cubemap("not a cubemap").has_value());
    REQUIRE_FALSE(mosaic::app::uncook_cubemap("").has_value());
    REQUIRE_FALSE(mosaic::app::uncook_cubemap("MOSAICUBE1short").has_value());
}

TEST_CASE("each face's CORNERS pin its orientation, not just its axis", "[asset]")
{
    // The gap the slice-4b review found, and it mattered: every other test here is
    // evaluated at the face CENTRE, where u = v = 0.5 makes both in-face coordinates
    // zero -- so only the major axis is exercised. Rotating a face 180 degrees
    // (Vec3{1, b, a}) or mirroring it (Vec3{1, b, -a}) left the whole suite green while
    // producing a visibly wrong sky on a GPU.
    //
    // The corners are where the in-face axes actually have magnitude, so these values
    // pin the sign of BOTH of them for every face. Derived from Vulkan's cube-map face
    // selection table, not from this implementation's output.
    struct Case { int face; float u; float v; Vec3 expect; };
    const Case cases[] = {
        {0, 0.0f, 0.0f, {+0.577350f, +0.577350f, +0.577350f}}, // +X corner (0,0)
        {0, 1.0f, 0.0f, {+0.577350f, +0.577350f, -0.577350f}}, // +X corner (1,0)
        {0, 0.0f, 1.0f, {+0.577350f, -0.577350f, +0.577350f}}, // +X corner (0,1)
        {1, 0.0f, 0.0f, {-0.577350f, +0.577350f, -0.577350f}}, // -X corner (0,0)
        {1, 1.0f, 0.0f, {-0.577350f, +0.577350f, +0.577350f}}, // -X corner (1,0)
        {1, 0.0f, 1.0f, {-0.577350f, -0.577350f, -0.577350f}}, // -X corner (0,1)
        {2, 0.0f, 0.0f, {-0.577350f, +0.577350f, -0.577350f}}, // +Y corner (0,0)
        {2, 1.0f, 0.0f, {+0.577350f, +0.577350f, -0.577350f}}, // +Y corner (1,0)
        {2, 0.0f, 1.0f, {-0.577350f, +0.577350f, +0.577350f}}, // +Y corner (0,1)
        {3, 0.0f, 0.0f, {-0.577350f, -0.577350f, +0.577350f}}, // -Y corner (0,0)
        {3, 1.0f, 0.0f, {+0.577350f, -0.577350f, +0.577350f}}, // -Y corner (1,0)
        {3, 0.0f, 1.0f, {-0.577350f, -0.577350f, -0.577350f}}, // -Y corner (0,1)
        {4, 0.0f, 0.0f, {-0.577350f, +0.577350f, +0.577350f}}, // +Z corner (0,0)
        {4, 1.0f, 0.0f, {+0.577350f, +0.577350f, +0.577350f}}, // +Z corner (1,0)
        {4, 0.0f, 1.0f, {-0.577350f, -0.577350f, +0.577350f}}, // +Z corner (0,1)
        {5, 0.0f, 0.0f, {+0.577350f, +0.577350f, -0.577350f}}, // -Z corner (0,0)
        {5, 1.0f, 0.0f, {-0.577350f, +0.577350f, -0.577350f}}, // -Z corner (1,0)
        {5, 0.0f, 1.0f, {+0.577350f, -0.577350f, -0.577350f}}, // -Z corner (0,1)
    };
    for (const auto& c : cases)
    {
        INFO("face " << c.face << " at (" << c.u << ", " << c.v << ")");
        const Vec3 d = cube_face_direction(c.face, c.u, c.v);
        REQUIRE(d.x == Approx(c.expect.x).margin(1e-5));
        REQUIRE(d.y == Approx(c.expect.y).margin(1e-5));
        REQUIRE(d.z == Approx(c.expect.z).margin(1e-5));
    }
}

TEST_CASE("an imported cubemap can be loaded back from its cooked blob", "[asset]")
{
    // The review found the import half shipped without the load half: a .hdr cooked to
    // a .cubemap that load_project_assets had no branch for, so it fell through to the
    // generic-blob path looking for a ".data" file that was never written. The asset
    // was silently unusable and uncook_cubemap had no caller outside tests.
    //
    // This pins the round trip at the boundary that matters -- the type registered must
    // be the one RenderSystem resolves an environment to.
    Image eq;
    eq.width = 16;
    eq.height = 8;
    eq.format = ImageFormat::Rgba16F;
    eq.pixels.assign(16u * 8u * 8u, 0x11);

    const auto faces = mosaic::app::equirect_to_cubemap(eq, 8);
    const std::string blob = mosaic::app::cook_cubemap(faces);
    const auto back = mosaic::app::uncook_cubemap(blob);
    REQUIRE(back.has_value());
    for (const auto& f : *back)
    {
        REQUIRE(f.width == 8);
        REQUIRE(f.height == 8);
        REQUIRE(f.format == ImageFormat::Rgba16F);
        REQUIRE(f.pixels.size() == 8u * 8u * 8u);
    }
}

TEST_CASE("cook_cubemap refuses faces that disagree", "[asset]")
{
    // cook_cubemap wrote faces[0]'s size and format into the header and then appended
    // all six regardless, so a mismatched array produced a blob uncook_cubemap would
    // reject -- failing at the wrong end, with a message about corruption rather than
    // about the caller's mistake.
    std::array<Image, 6> faces;
    for (auto& f : faces)
    {
        f.width = 4; f.height = 4; f.format = ImageFormat::Rgba8;
        f.pixels.assign(4u * 4u * 4u, 7);
    }
    faces[3].width = 8; // one face out of step
    faces[3].pixels.assign(8u * 8u * 4u, 7);
    REQUIRE(mosaic::app::cook_cubemap(faces).empty());
}
