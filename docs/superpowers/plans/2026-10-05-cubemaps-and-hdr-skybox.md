# Cubemaps, HDR Import, and a Skybox Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Load a real `.hdr`, convert it to a cubemap at import, and draw it as a skybox behind the demo.

**Architecture:** `Image` gains an `ImageFormat` tag while keeping byte storage, so half-floats flow through the existing asset path unchanged. A new `cubemap_cook` converts equirectangular to six faces on the CPU at import time, cooked to a `MOSAICUBE1` blob. The device gains `create_cubemap`, and a vertexless fullscreen-triangle pipeline draws the sky after opaque geometry.

**Tech Stack:** C++20, Vulkan, GLSL 450, Catch2, stb_image (vendored), CMake presets.

**Spec:** `docs/superpowers/specs/2026-10-05-cubemaps-and-hdr-skybox-design.md`

## Global Constraints

- Face order is Vulkan's layer order, verbatim: **+X, −X, +Y, −Y, +Z, −Z**.
- Equirect mapping: `u = atan2(d.z, d.x) / (2π) + 0.5`, `v = acos(clamp(d.y, -1, 1)) / π`.
- Face size is **512**, fixed. Source is 2048×1024, which is exactly 1:1 (90° of a 2048-wide equirect is 512 px).
- `ImageFormat::Rgba8` is the default; every existing caller must keep working untouched.
- `MOSAIMG1` blobs must still load, as `Rgba8`. `MOSAIMG2` is the new write format.
- HDR pixels are **RGBA16F** (half), stored in `Image::pixels` as bytes. Size rule: `w*h*4` for `Rgba8`, `w*h*8` for `Rgba16F`.
- The skybox shader **must** apply `tonemap_aces`, textually identical to `cube.frag`'s copy.
- Never run `git add -A`; stage explicit paths. `docs/project/advancement-plans-and-evaluation.md` is the user's and must never be staged.

## Review Focus

Five input classes the spec implies that the tasks' own tests would not all reach. Each has a test assigned to the task that owns it.

1. **A face mapped to the wrong axis, or a flipped V.** Produces a rotated or mirrored sky — reads as a questionable asset, not a defect, and no GPU in CI can see it. Test in Task 2.
2. **An `Rgba16F` image reaching a code path that assumes 4 bytes per pixel.** `Image` has had one stride for its whole life; `cook_image`, `uncook_image`, `create_texture` and the mip ladder all compute sizes from it. Test in Task 1.
3. **An already-cooked `MOSAIMG1` asset after the format change.** Every texture on disk is one. Silent breakage here means existing projects stop loading. Test in Task 1.
4. **A cubemap handle passed to a 2D draw path, or vice versa.** They share `TextureHandle` and the texture table; binding a cube view where a 2D view is expected is a validation error at best. Test in Task 3.
5. **The skybox drawn over opaque geometry instead of behind it.** A depth-state mistake hides the whole scene, or hides the sky entirely. Test in Task 4.

---

### Task 1: The HDR image path

**Files:**
- Modify: `engine/render/image.hpp`
- Modify: `app/asset/image_cook.hpp`, `app/asset/image_cook.cpp`
- Modify: `modules/render_vulkan/vulkan_render_device.cpp` (format selection, stride)
- Test: `tests/unit/app/image_cook_test.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `enum class ImageFormat { Rgba8, Rgba16F };`, `Image::format`, `std::size_t bytes_per_pixel(ImageFormat)`, and `MOSAIMG2` cook/uncook.

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/app/image_cook_test.cpp`:

```cpp
TEST_CASE("an Rgba16F image round-trips through cook and uncook", "[asset]")
{
    // Review Focus 2. Image has had one stride for its whole life, and cook_image,
    // uncook_image and the device all compute sizes from it. A format that is 8 bytes
    // per pixel is the first thing to challenge that.
    using mosaic::engine::render::Image;
    using mosaic::engine::render::ImageFormat;
    Image img;
    img.width = 2;
    img.height = 1;
    img.format = ImageFormat::Rgba16F;
    img.pixels.assign(2u * 1u * 8u, 0);
    img.pixels[0] = 0x00; img.pixels[1] = 0x3C; // half 1.0, little-endian
    img.pixels[8] = 0x00; img.pixels[9] = 0x42; // half 3.0

    const auto restored = mosaic::app::uncook_image(mosaic::app::cook_image(img));
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

    const auto img = mosaic::app::uncook_image(blob);
    REQUIRE(img.has_value());
    REQUIRE(img->format == mosaic::engine::render::ImageFormat::Rgba8);
    REQUIRE(img->width == 1);
    REQUIRE(img->pixels.size() == 4);
    REQUIRE(img->pixels[1] == 20);
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

    // At least one channel above 1.0 must survive. The committed 2K copy peaks at
    // about 3.5; asserting ">1.0" rather than a specific peak keeps this from breaking
    // if the asset is ever re-derived at another size.
    bool any_above_one = false;
    for (std::size_t i = 0; i + 1 < img->pixels.size(); i += 2)
    {
        const std::uint16_t h =
            static_cast<std::uint16_t>(img->pixels[i] | (img->pixels[i + 1] << 8));
        // half > 1.0 means a biased exponent above 15 -- bits 10..14 greater than 0x0F.
        if (((h >> 10) & 0x1F) > 15 && (h & 0x8000) == 0) { any_above_one = true; break; }
    }
    REQUIRE(any_above_one);
}

TEST_CASE("bytes_per_pixel matches the two formats", "[asset]")
{
    using mosaic::engine::render::bytes_per_pixel;
    using mosaic::engine::render::ImageFormat;
    REQUIRE(bytes_per_pixel(ImageFormat::Rgba8) == 4);
    REQUIRE(bytes_per_pixel(ImageFormat::Rgba16F) == 8);
}
```

- [ ] **Step 2: Run them to verify they fail**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20 2>&1 | grep -E "error:" | head -3
```

Expected: compile errors — `ImageFormat` is not a member of `mosaic::engine::render`, `Image` has no member `format`.

- [ ] **Step 3: Extend Image**

In `engine/render/image.hpp`:

```cpp
// What a pixel in Image::pixels means.
//
// Rgba8: four bytes per pixel, the historical and default form.
// Rgba16F: four HALF floats per pixel, eight bytes. Half-floats are bytes, so the
// storage does not change shape -- only the stride does. This exists because an HDR
// environment carries values above 1.0 (that is the point of .hdr: an LDR sky clips
// the sun), and RGBA16F is the standard runtime format for one -- half the memory of
// RGBA32F, natively filterable, with ample mantissa for a sky.
enum class ImageFormat { Rgba8, Rgba16F };

constexpr std::size_t bytes_per_pixel(ImageFormat f)
{
    return (f == ImageFormat::Rgba16F) ? 8u : 4u;
}

struct Image
{
    std::uint32_t             width = 0;
    std::uint32_t             height = 0;
    ImageFormat               format = ImageFormat::Rgba8; // default keeps every caller correct
    std::vector<std::uint8_t> pixels; // width*height*bytes_per_pixel(format)
};
```

Add `#include <cstddef>`.

- [ ] **Step 4: Teach cook/uncook the format**

In `app/asset/image_cook.hpp`, `cook_image` writes `"MOSAIMG2"` plus a `u32` format tag after height, then pixels. `uncook_image` accepts both magics:

```cpp
// Reads MOSAIMG2 (with a format tag) and MOSAIMG1 (implicitly Rgba8). Every texture
// cooked before the HDR path existed is a MOSAIMG1, so dropping that branch would
// silently stop existing projects loading -- a missing texture rather than an error.
```

`MOSAIMG1`'s header is 16 bytes, `MOSAIMG2`'s is 20. Validate `bytes.size() - header == w*h*bytes_per_pixel(format)` in both cases, and reject an unknown format tag.

- [ ] **Step 5: Load HDR in read_image**

In `app/asset/image_cook.cpp`, remove `#define STBI_NO_HDR` (and its comment, which is now wrong). Before the LDR load:

```cpp
    if (stbi_is_hdr_from_memory(data, len) != 0)
    {
        int hw = 0, hh = 0, hc = 0;
        float* f = stbi_loadf_from_memory(data, len, &hw, &hh, &hc, 4);
        if (f == nullptr) { return std::nullopt; }
        const std::unique_ptr<float, void (*)(void*)> owned_f{f, stbi_image_free};
        if (!image_dimensions_are_sane(static_cast<std::uint64_t>(hw < 0 ? 0 : hw),
                                       static_cast<std::uint64_t>(hh < 0 ? 0 : hh)))
        {
            return std::nullopt;
        }
        mosaic::engine::render::Image img;
        img.width = static_cast<std::uint32_t>(hw);
        img.height = static_cast<std::uint32_t>(hh);
        img.format = mosaic::engine::render::ImageFormat::Rgba16F;
        const std::size_t count = static_cast<std::size_t>(hw) * hh * 4u;
        try { img.pixels.resize(count * 2u); }
        catch (const std::bad_alloc&) { return std::nullopt; }
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::uint16_t h = float_to_half(f[i]);
            img.pixels[i * 2 + 0] = static_cast<std::uint8_t>(h & 0xFF);
            img.pixels[i * 2 + 1] = static_cast<std::uint8_t>(h >> 8);
        }
        return img;
    }
```

Add `float_to_half` to `image_cook.hpp` as a `constexpr`-friendly free function — IEEE 754 binary16, round-toward-zero on the mantissa, clamping to half's max (65504) on overflow and flushing subnormals to zero. Document that round-toward-zero is chosen because it cannot produce an infinity from a finite input, which matters for a value the shader will multiply.

- [ ] **Step 6: Honour the format in the device**

In `modules/render_vulkan/vulkan_render_device.cpp`, `tex_format` becomes:

```cpp
        const VkFormat tex_format =
            (image.format == mosaic::engine::render::ImageFormat::Rgba16F)
                ? VK_FORMAT_R16G16B16A16_SFLOAT
                : ((space == mosaic::engine::render::TextureColorSpace::Linear)
                       ? VK_FORMAT_R8G8B8A8_UNORM
                       : VK_FORMAT_R8G8B8A8_SRGB);
```

with a comment that `Rgba16F` is **always linear** — a float format has no sRGB variant, and asking for `Srgb` with one is a category error rather than a choice. Replace the staging-buffer size computation's hardcoded `* 4` with `* bytes_per_pixel(image.format)`.

- [ ] **Step 7: Run the tests to verify they pass**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20 2>&1 | grep -cE "error:"
./build/debug/tests/mosaic_unit_tests "[asset]" 2>&1 | tail -3
```

Expected: `0` errors, all `[asset]` pass.

- [ ] **Step 8: Mutation probe — prove the MOSAIMG1 branch is load-bearing**

```bash
cp app/asset/image_cook.hpp /tmp/ic.bak
# Make uncook_image reject MOSAIMG1 (what dropping the compatibility branch looks like).
sed -i 's|bytes.compare(0, 8, "MOSAIMG1") != 0|true|' app/asset/image_cook.hpp
cmake --build --preset debug --target mosaic_unit_tests -j 20 2>&1 | grep -cE "error:"
./build/debug/tests/mosaic_unit_tests "[asset]" 2>&1 | tail -2
cp /tmp/ic.bak app/asset/image_cook.hpp && rm /tmp/ic.bak
```

Expected: FAIL at the `MOSAIMG1` case, then green on restore. If it passes, the compatibility branch is untested — fix the test before continuing.

- [ ] **Step 9: Full gate and commit**

```bash
ctest --preset debug --output-on-failure 2>&1 | tail -3
git add engine/render/image.hpp app/asset/image_cook.hpp app/asset/image_cook.cpp \
        modules/render_vulkan/vulkan_render_device.cpp tests/unit/app/image_cook_test.cpp
git commit -m "feat(asset): load Radiance HDR as RGBA16F through a format-tagged Image"
```

---

### Task 2: Equirect → cubemap at import

**Files:**
- Create: `app/asset/cubemap_cook.hpp`, `app/asset/cubemap_cook.cpp`
- Modify: `app/asset/asset_io.hpp` (dispatch `.hdr` to the cubemap cook)
- Modify: `app/CMakeLists.txt`
- Test: `tests/unit/app/cubemap_cook_test.cpp` (create), `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Image`, `ImageFormat`, `bytes_per_pixel` from Task 1.
- Produces:
  - `mosaic::foundation::math::Vec3 cube_face_direction(int face, float u, float v);` — `u`,`v` in `[0,1]` across the face, returns a **normalised** direction.
  - `void equirect_to_uv(const Vec3& dir, float& u, float& v);`
  - `std::array<Image, 6> equirect_to_cubemap(const Image& equirect, std::uint32_t face_size);`
  - `std::string cook_cubemap(const std::array<Image, 6>&);` / `std::optional<std::array<Image, 6>> uncook_cubemap(const std::string&);`

- [ ] **Step 1: Write the failing direction tests**

Create `tests/unit/app/cubemap_cook_test.cpp`:

```cpp
// Review Focus 1, and the highest-value tests in this slice.
//
// A face mapped to the wrong axis, or a flipped V, produces a rotated or mirrored sky.
// That reads as a questionable asset rather than a defect, and CI has no GPU, so the
// direction math is where correctness has to be established. This is the whole reason
// the conversion happens on the CPU at import rather than on the GPU at load.

#include <array>
#include <cmath>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "app/asset/cubemap_cook.hpp"

using mosaic::app::cube_face_direction;
using mosaic::app::equirect_to_uv;
using mosaic::foundation::math::Vec3;
using Catch::Approx;

TEST_CASE("each cube face's centre points down its own axis", "[asset]")
{
    // Vulkan layer order: +X, -X, +Y, -Y, +Z, -Z. Getting this order wrong rotates the
    // sky; getting a sign wrong mirrors it.
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
    // The equirect mapping does acos(d.y), which is only meaningful on a unit vector --
    // an unnormalised direction silently distorts the vertical mapping near the poles.
    for (int face = 0; face < 6; ++face)
    {
        for (float u : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
        {
            for (float v : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
            {
                const Vec3 d = cube_face_direction(face, u, v);
                INFO("face " << face << " u " << u << " v " << v);
                REQUIRE(std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z) == Approx(1.0f).margin(1e-5));
            }
        }
    }
}

TEST_CASE("the equirect mapping puts the poles and the centre where they belong", "[asset]")
{
    // v is derived from acos(d.y): +Y is the TOP of the equirect (v = 0), -Y the bottom.
    // A flipped v turns the sky upside down, which is the single most likely mistake
    // and looks like an asset problem rather than a bug.
    float u = 0, v = 0;
    equirect_to_uv(Vec3{0, 1, 0}, u, v);
    REQUIRE(v == Approx(0.0f).margin(1e-5));
    equirect_to_uv(Vec3{0, -1, 0}, u, v);
    REQUIRE(v == Approx(1.0f).margin(1e-5));
    equirect_to_uv(Vec3{0, 0, 0 + 1}, u, v);
    REQUIRE(v == Approx(0.5f).margin(1e-5)); // equator
    // u wraps at +X: atan2(0, 1) = 0 -> 0.5 by the +0.5 offset.
    equirect_to_uv(Vec3{1, 0, 0}, u, v);
    REQUIRE(u == Approx(0.5f).margin(1e-5));
}

TEST_CASE("direction to equirect UV and back is stable", "[asset]")
{
    // A round trip catches a swapped u/v or a wrong constant that the fixed points
    // above would not: those check six directions, this checks the mapping's shape.
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
    // The end-to-end check, with a KNOWN image: paint the equirect so that each region
    // a face will sample is a distinct colour, convert, and assert each face's centre
    // texel carries its colour. This catches a u/v swap or an off-by-one in the
    // sampling that the pure direction tests cannot.
    using mosaic::engine::render::Image;
    using mosaic::engine::render::ImageFormat;
    Image eq;
    eq.width = 64;
    eq.height = 32;
    eq.format = ImageFormat::Rgba8;
    eq.pixels.assign(64u * 32u * 4u, 0);
    // Top band -> +Y, bottom band -> -Y, and four vertical quarters around the equator
    // for +X, +Z, -X, -Z (in increasing u from the +X seam).
    auto put = [&](int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        const std::size_t i = (static_cast<std::size_t>(y) * 64 + x) * 4;
        eq.pixels[i] = r; eq.pixels[i + 1] = g; eq.pixels[i + 2] = b; eq.pixels[i + 3] = 255;
    };
    for (int x = 0; x < 64; ++x)
    {
        for (int y = 0; y < 32; ++y)
        {
            if (y < 4)        { put(x, y, 255, 255, 0); }   // +Y top
            else if (y >= 28) { put(x, y, 0, 255, 255); }   // -Y bottom
            else if (x < 8 || x >= 56) { put(x, y, 255, 0, 0); }   // -X  (u near 0/1)
            else if (x < 24)  { put(x, y, 0, 0, 255); }     // -Z
            else if (x < 40)  { put(x, y, 0, 255, 0); }     // +X  (u near 0.5)
            else              { put(x, y, 255, 0, 255); }   // +Z
        }
    }

    const auto faces = mosaic::app::equirect_to_cubemap(eq, 16);
    auto centre = [&](int f) {
        const Image& img = faces[static_cast<std::size_t>(f)];
        const std::size_t i = (static_cast<std::size_t>(8) * 16 + 8) * 4;
        return std::array<std::uint8_t, 3>{img.pixels[i], img.pixels[i + 1], img.pixels[i + 2]};
    };
    REQUIRE(centre(0) == std::array<std::uint8_t, 3>{0, 255, 0});     // +X green
    REQUIRE(centre(1) == std::array<std::uint8_t, 3>{255, 0, 0});     // -X red
    REQUIRE(centre(2) == std::array<std::uint8_t, 3>{255, 255, 0});   // +Y yellow
    REQUIRE(centre(3) == std::array<std::uint8_t, 3>{0, 255, 255});   // -Y cyan
    REQUIRE(centre(4) == std::array<std::uint8_t, 3>{255, 0, 255});   // +Z magenta
    REQUIRE(centre(5) == std::array<std::uint8_t, 3>{0, 0, 255});     // -Z blue
}

TEST_CASE("a cubemap round-trips through cook and uncook", "[asset]")
{
    using mosaic::engine::render::Image;
    using mosaic::engine::render::ImageFormat;
    std::array<Image, 6> faces;
    for (int f = 0; f < 6; ++f)
    {
        faces[static_cast<std::size_t>(f)].width = 2;
        faces[static_cast<std::size_t>(f)].height = 2;
        faces[static_cast<std::size_t>(f)].format = ImageFormat::Rgba8;
        faces[static_cast<std::size_t>(f)].pixels.assign(2u * 2u * 4u,
                                                         static_cast<std::uint8_t>(f + 1));
    }
    const auto restored = mosaic::app::uncook_cubemap(mosaic::app::cook_cubemap(faces));
    REQUIRE(restored.has_value());
    for (int f = 0; f < 6; ++f)
    {
        INFO("face " << f);
        REQUIRE((*restored)[static_cast<std::size_t>(f)].pixels
                == faces[static_cast<std::size_t>(f)].pixels);
    }
    REQUIRE_FALSE(mosaic::app::uncook_cubemap("not a cubemap").has_value());
    REQUIRE_FALSE(mosaic::app::uncook_cubemap("").has_value());
}
```

Register it in `tests/CMakeLists.txt`.

- [ ] **Step 2: Run them to verify they fail**

Expected: compile errors — `cubemap_cook.hpp` does not exist.

- [ ] **Step 3: Write the header**

`app/asset/cubemap_cook.hpp` declares the five functions above, with a comment block explaining the face order and the mapping, and stating that the conversion is done here on the CPU rather than on the GPU **so that the direction math is testable without a GPU**.

- [ ] **Step 4: Implement the direction and mapping functions**

```cpp
// u,v run 0..1 across the face. Vulkan's cube convention has +Y up and the faces laid
// out so that a direction's major axis selects the face; these are the standard
// per-face basis vectors, with the (2u-1), (2v-1) remap putting the face centre at 0.
Vec3 cube_face_direction(int face, float u, float v)
{
    const float a = 2.0f * u - 1.0f;
    const float b = 2.0f * v - 1.0f;
    Vec3 d{};
    switch (face)
    {
        case 0: d = Vec3{ 1.0f,   -b,   -a}; break; // +X
        case 1: d = Vec3{-1.0f,   -b,    a}; break; // -X
        case 2: d = Vec3{    a, 1.0f,    b}; break; // +Y
        case 3: d = Vec3{    a,-1.0f,   -b}; break; // -Y
        case 4: d = Vec3{    a,   -b, 1.0f}; break; // +Z
        default: d = Vec3{  -a,   -b,-1.0f}; break; // -Z
    }
    const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    return Vec3{d.x / len, d.y / len, d.z / len};
}

void equirect_to_uv(const Vec3& dir, float& u, float& v)
{
    constexpr float kPi = 3.14159265358979323846f;
    u = std::atan2(dir.z, dir.x) / (2.0f * kPi) + 0.5f;
    const float y = (dir.y < -1.0f) ? -1.0f : ((dir.y > 1.0f) ? 1.0f : dir.y);
    v = std::acos(y) / kPi;
}
```

`equirect_to_cubemap` loops faces × texels, samples the equirect **bilinearly** with wrapping in u and clamping in v, and writes the face image in the source's format. Bilinear rather than nearest because a 1:1 source still lands between texels at face edges, and nearest produces visible stair-stepping along the diagonals.

- [ ] **Step 5: Implement cook/uncook**

`"MOSAICUBE1"` + `u32` face size + `u32` format tag + six faces back to back. `uncook_cubemap` validates the magic, the tag, and that the payload is exactly `6 * size * size * bytes_per_pixel(format)`.

- [ ] **Step 6: Run the tests to verify they pass**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20 2>&1 | grep -cE "error:"
./build/debug/tests/mosaic_unit_tests "[asset]" 2>&1 | tail -3
```

Expected: `0` errors, all pass. **If the synthetic-equirect case fails, do not adjust the expected colours to match the output** — that converts a real orientation bug into a passing test. Fix the basis vectors.

- [ ] **Step 7: Mutation probe — prove the orientation tests bite**

```bash
cp app/asset/cubemap_cook.cpp /tmp/cc.bak
sed -i 's|v = std::acos(y) / kPi;|v = 1.0f - std::acos(y) / kPi;|' app/asset/cubemap_cook.cpp
cmake --build --preset debug --target mosaic_unit_tests -j 20 2>&1 | grep -cE "error:"
./build/debug/tests/mosaic_unit_tests "[asset]" 2>&1 | tail -2
cp /tmp/cc.bak app/asset/cubemap_cook.cpp && rm /tmp/cc.bak
```

Expected: FAIL (a flipped V — the upside-down sky), green on restore.

- [ ] **Step 8: Dispatch `.hdr` to the cubemap cook**

In `app/asset/asset_io.hpp`, add a branch before the generic image case: `.hdr` → `read_image`, then `equirect_to_cubemap(img, 512)`, then `cook_cubemap`, with `blob_ext = ".cubemap"` and `asset_type = "cubemap"`.

- [ ] **Step 9: Full gate and commit**

```bash
ctest --preset debug --output-on-failure 2>&1 | tail -3
git add app/asset/cubemap_cook.hpp app/asset/cubemap_cook.cpp app/asset/asset_io.hpp \
        app/CMakeLists.txt tests/unit/app/cubemap_cook_test.cpp tests/CMakeLists.txt
git commit -m "feat(asset): convert an equirectangular HDR to a cubemap at import"
```

---

### Task 3: `create_cubemap` in the render device

**Files:**
- Modify: `engine/render/render_device.hpp`
- Modify: `modules/render_vulkan/vulkan_render_device.cpp`
- Modify: the five `RenderDevice` test doubles
- Test: `tests/unit/render/texture_colorspace_contract_test.cpp`

**Interfaces:**
- Consumes: `Image`/`ImageFormat` from Task 1.
- Produces: `virtual TextureHandle create_cubemap(const std::array<Image, 6>& faces);` with a default implementation returning 0, so only the Vulkan backend must implement it.

- [ ] **Step 1: Write the failing contract test**

Append to `tests/unit/render/texture_colorspace_contract_test.cpp`:

```cpp
TEST_CASE("the backend creates cubemaps with the cube flags and view type", "[render]")
{
    // Review Focus 4. A cubemap shares TextureHandle and the texture table with 2D
    // images, so the only things that distinguish it are the create flag, the layer
    // count and the view type -- and all three must agree. Two out of three is a
    // validation error at best and a wrong binding at worst, and CI has no GPU.
    const std::string src =
        mosaic::tests::normalize_ws(mosaic::tests::code_only(read_backend()));
    REQUIRE(src.find("VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT") != std::string::npos);
    REQUIRE(src.find("VK_IMAGE_VIEW_TYPE_CUBE") != std::string::npos);
    REQUIRE(src.find("cube.arrayLayers = 6;") != std::string::npos);
    REQUIRE(src.find("cube_view.subresourceRange.layerCount = 6;") != std::string::npos);
    // Cube sampling must clamp, or the faces show seams at their edges.
    REQUIRE(src.find("cube_sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE")
            != std::string::npos);
}
```

- [ ] **Step 2: Run it to verify it fails.** Expected: FAIL on the first assertion.

- [ ] **Step 3: Declare the interface**

In `engine/render/render_device.hpp`:

```cpp
    // Uploads six square faces as a cubemap, in Vulkan's layer order:
    // +X, -X, +Y, -Y, +Z, -Z. Returns a handle from the same space as create_texture --
    // a cubemap IS a texture to everything above the device; only the view type differs.
    // Default returns 0 so non-rendering test doubles need not implement it.
    virtual TextureHandle create_cubemap(const std::array<Image, 6>& /*faces*/) { return 0; }
```

Add `#include <array>`.

- [ ] **Step 4: Implement it in the Vulkan backend**

One image, `arrayLayers = 6`, `VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT`, `mipLevels = 1`. Stage all six faces into one buffer and issue six `VkBufferImageCopy` regions (one per `baseArrayLayer`), transition all six layers together, create a `VK_IMAGE_VIEW_TYPE_CUBE` view with `layerCount = 6`, and a sampler clamping on all three axes. Reuse the existing `textures_` entry type so the handle space is shared.

Name the locals `cube`, `cube_view`, `cube_sampler` to match the contract test.

- [ ] **Step 5: Run the test to verify it passes.** Then **Step 6**, full gate, and commit:

```bash
git add engine/render/render_device.hpp modules/render_vulkan/vulkan_render_device.cpp \
        tests/unit/render/texture_colorspace_contract_test.cpp
git commit -m "feat(render): upload six faces as a cubemap"
```

---

### Task 4: The skybox pipeline and shaders

**Files:**
- Create: `modules/render_vulkan/shaders/skybox.vert`, `modules/render_vulkan/shaders/skybox.frag`
- Modify: `modules/render_vulkan/vulkan_render_device.cpp`, `engine/render/render_device.hpp`, `engine/render/render_system.hpp`
- Modify: `modules/render_vulkan/CMakeLists.txt:18` — append `skybox.vert skybox.frag` to the `foreach(_stage ...)` list, which is what compiles each shader to `.spv` and adds it to the build's dependencies. A shader not in that list is silently never compiled.
- Test: `tests/unit/render/shader_tonemap_contract_test.cpp`, `tests/unit/render/texture_colorspace_contract_test.cpp`

**Interfaces:**
- Consumes: `create_cubemap`'s handle from Task 3.
- Produces: `virtual void draw_skybox(TextureHandle cubemap, const SkyboxParams&) {}` and
  `struct SkyboxParams { Mat4 inverse_view_rotation; float tan_half_fov_y; float aspect; };`

- [ ] **Step 1: Write the failing contract tests**

```cpp
TEST_CASE("the skybox tone maps with the same curve as the lit pass", "[render]")
{
    // Slice 1 justified tone-mapping cube.frag ALONE on the grounds that every other
    // colour-producing shader is unlit and cannot exceed 1.0. A skybox sampling HDR
    // radiance can -- it is the first shader for which that reasoning does not hold,
    // and "bright regions roll off rather than clip" is a success criterion of this
    // slice. The two copies must stay identical; a shared GLSL include would be better
    // but the shader build has no include step, so this test is what holds them.
    const std::string lit = mosaic::tests::code_only(read_shader("cube.frag"));
    const std::string sky = mosaic::tests::code_only(read_shader("skybox.frag"));
    auto body = [](const std::string& s) {
        const std::size_t b = s.find("vec3 tonemap_aces(vec3 x)");
        REQUIRE(b != std::string::npos);
        const std::size_t e = s.find('}', b);
        REQUIRE(e != std::string::npos);
        return mosaic::tests::normalize_ws(s.substr(b, e - b));
    };
    REQUIRE(body(lit) == body(sky));
    REQUIRE(sky.find("tonemap_aces(") != std::string::npos);
}

TEST_CASE("the skybox draws behind the scene and does not write depth", "[render]")
{
    // Review Focus 5. depthWrite on would let the sky occlude everything drawn after
    // it; a LESS compare at the far plane would reject every fragment and show no sky
    // at all. Both are one-token mistakes with a total visual effect.
    const std::string src =
        mosaic::tests::normalize_ws(mosaic::tests::code_only(read_backend()));
    REQUIRE(src.find("sky_depth.depthWriteEnable = VK_FALSE;") != std::string::npos);
    REQUIRE(src.find("sky_depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;")
            != std::string::npos);
    // The fullscreen triangle sits exactly on the far plane.
    const std::string vert = mosaic::tests::code_only(read_shader("skybox.vert"));
    REQUIRE(vert.find("gl_Position = vec4(pos, 1.0, 1.0)") != std::string::npos);
}
```

- [ ] **Step 2: Run them to verify they fail.** Expected: FAIL — `skybox.frag` does not exist, so `read_shader` fails its `REQUIRE(in.good())`.

- [ ] **Step 3: Write the shaders**

`skybox.vert` — a vertexless fullscreen triangle, following `screen_effect.vert`'s precedent, emitting NDC so the fragment shader can build a ray:

```glsl
#version 450
layout(location = 0) out vec2 fragNdc;
void main()
{
    // Fullscreen triangle from gl_VertexIndex, the same idiom as screen_effect.vert.
    vec2 pos = vec2((gl_VertexIndex == 1) ? 3.0 : -1.0, (gl_VertexIndex == 2) ? 3.0 : -1.0);
    fragNdc = pos;
    // z = w puts every fragment exactly on the far plane, so with LESS_OR_EQUAL and
    // depth writes off this fills only pixels nothing else wrote.
    gl_Position = vec4(pos, 1.0, 1.0);
}
```

`skybox.frag` samples the cubemap along a ray built from the NDC, the camera's inverse view rotation and the field of view, then tone maps. **`Mat4` has only `inverse_affine`, not a general inverse, and a projection matrix is not affine** — so the ray is built from the camera basis and the FOV rather than from an inverted view-projection. Copy `tonemap_aces` from `cube.frag` verbatim, with a comment saying the contract test holds the two identical and why a shared include is not available.

- [ ] **Step 4: Add the pipeline and the draw entry point**

`SkyboxParams` carries `inverse_view_rotation` (from `Mat4::inverse_affine(view)` with translation zeroed — the sky must not move with the camera), `tan_half_fov_y` and `aspect`. `RenderSystem` derives the last two from the projection matrix it already has: `tan_half_fov_y = 1.0f / projection.m[5]`, `aspect = projection.m[5] / projection.m[0]` — documented at the call site, because reading parameters back out of a matrix is non-obvious.

The pipeline binds set 0 = the cubemap's descriptor set, no vertex input, `vkCmdDraw(cmd, 3, 1, 0, 0)`, depth state as pinned above.

Call `draw_skybox` in `RenderSystem::render` **after** the opaque mesh loop and **before** the decal pass, with a comment giving the reason: drawing it after opaque means it only shades pixels that survived, and drawing it before the transparent passes means they blend over it correctly.

- [ ] **Step 5: Run the tests, then the gate and both entry points**

```bash
cmake --build --preset debug -j 20 2>&1 | grep -cE "error:"
ctest --preset debug --output-on-failure 2>&1 | tail -3
timeout 40 ./build/debug/app/mosaic run --window --duration 8; echo "RC=$?"
timeout 40 ./build/debug/app/mosaic run --window --script-thread --duration 8; echo "RC=$?"
```

Expected: 100% tests, both exit 0, **zero Vulkan validation errors** — the only check that sees a descriptor or pipeline mismatch.

- [ ] **Step 6: Commit**

```bash
git add modules/render_vulkan/shaders/skybox.vert modules/render_vulkan/shaders/skybox.frag \
        modules/render_vulkan/vulkan_render_device.cpp engine/render/render_device.hpp \
        engine/render/render_system.hpp tests/unit/render/shader_tonemap_contract_test.cpp \
        tests/unit/render/texture_colorspace_contract_test.cpp
git commit -m "feat(render): draw an HDR cubemap as a tone-mapped skybox"
```

---

### Task 5: Wire the demo to the committed environment

**Files:**
- Modify: `app/run/demo_scene.hpp`
- Test: `tests/unit/app/demo_scene_test.cpp`

**Interfaces:**
- Consumes: everything above.

- [ ] **Step 1: Write the failing test**

`setup_runtime` is the registration entry point (`app/run/demo_scene.hpp:177`); this
copies the idiom of the existing case at `tests/unit/app/demo_scene_test.cpp:70-86`,
which already asserts `registry.contains(kCheckerTextureId)` the same way.

```cpp
TEST_CASE("setup_runtime registers the environment cubemap", "[app]")
{
    // The slice's visible deliverable. Without this the whole path is built and
    // nothing uses it -- exactly the situation the skybox was pulled into this slice
    // to avoid.
    World world;
    AssetRegistry registry;
    mosaic::foundation::reflection::TypeRegistry     reg;
    mosaic::engine::reflection::ComponentReflection  types;
    mosaic::engine::reflection::register_builtin_components(reg, types);
    auto runtime = setup_runtime(
        world, registry, types,
        [] { return mosaic::script::python::create_python_script_engine(); });

    REQUIRE(runtime.render_system != nullptr);
    REQUIRE(registry.contains(mosaic::app::kEnvironmentCubemapId));
}
```

Add `using mosaic::app::kEnvironmentCubemapId;` beside the file's existing
`using mosaic::app::kCheckerTextureId;` at line 36.

- [ ] **Step 2: Run it to verify it fails.**

- [ ] **Step 3: Load and register the environment**

Add `kEnvironmentCubemapId` beside the other asset ids (an unused 16-byte array; `0xE1` is free). Load `assets/backgrounds/space00_2k.hdr` with `load_scene_file`, convert with `equirect_to_cubemap(img, 512)`, and register the six faces. On failure, log a warning naming the cwd-relative path and continue — the demo must still run without a sky, the same degradation the roughness map already uses.

Hand the cubemap to the renderer wherever the demo supplies per-frame render state.

- [ ] **Step 4: Run it, then the full gate and both entry points.**

- [ ] **Step 5: Record the visual check and stop**

No test here can show the sky *looks* right. Write to the ledger and tell the human partner to expect:

1. **A starfield behind the scene**, replacing the flat grey-blue clear colour.
2. **No seams** at face edges, and **nothing upside down or mirrored** — a flipped V is the most likely bug and reads as a bad asset.
3. **The scene itself unchanged.** The skybox only fills pixels nothing else wrote; if objects look different, the depth state is wrong.
4. **The metal sphere still near-black.** The environment has 0.2% mean radiance and nothing consumes it for lighting yet — IBL is 4c. This is expected and is recorded in `assets/backgrounds/README.md`.

- [ ] **Step 6: Commit**

```bash
git add app/run/demo_scene.hpp tests/unit/app/demo_scene_test.cpp
git commit -m "feat(demo): load the space environment and draw it as the skybox"
```
