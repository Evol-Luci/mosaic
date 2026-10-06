# Image-Based Lighting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the flat `0.1` ambient with image-based lighting, and prove the BRDF conserves energy with a furnace test.

**Architecture:** Irradiance and prefiltered-specular cubes are convolved from the environment on the CPU at import and cooked to a `MOSAICIBL1` blob. The BRDF LUT is integrated at startup, not cooked. The device gains a mip-chain `create_cubemap` overload and a dedicated IBL descriptor set. `cube.frag` samples all three, falling back to the flat ambient when no environment is present.

**Tech Stack:** C++20, Vulkan, GLSL 450, Catch2, CMake presets.

**Spec:** `docs/superpowers/specs/2026-10-06-image-based-lighting-design.md`

## Global Constraints

- Irradiance is **32² × 6 faces**. Prefiltered is **128² × 6 faces × 5 mip levels**, roughness `m / 4` at level `m`.
- Mip 0 of the prefiltered chain is a **resample, not a convolution** — roughness 0 is a mirror.
- The shader indexes the chain with `roughness * 4.0` through `textureLod`.
- The BRDF LUT is **256² RG16F**, computed at startup, never cooked.
- A scene with **no environment renders byte-identically to today**. The flat `0.1` path is kept as the fallback, not deleted.
- All IBL products are `ImageFormat::Rgba16F`, which is always linear.
- Face order stays Vulkan's: **+X, −X, +Y, −Y, +Z, −Z**.
- Never run `git add -A`; stage explicit paths. `docs/project/advancement-plans-and-evaluation.md` is the user's and must never be staged.

## Review Focus

1. **Energy conservation.** A uniform environment must survive both integrals unchanged. This is the furnace test and the slice's reason for existing. Task 1.
2. **A descriptor set mutated while a frame is in flight.** The environment resolves *during* `render()`, between `begin_frame` and `end_frame`, with 2 frames in flight. Writing IBL descriptors into an existing per-frame set would touch one the other frame may be using. Task 4.
3. **The no-environment path.** Every scene without an environment, and every frame before one resolves, must render exactly as today. Task 5.
4. **The roughness→LOD mapping.** `roughness * 4.0` against 5 levels is correct; `* 5.0`, or a 4-level chain, yields a material that merely looks too shiny or too blurry. Task 5.
5. **A cubemap mip chain whose level sizes disagree with its base.** The upload path trusts what it is handed; a chain with a wrong level size overruns the staging buffer. Task 4.

---

### Task 1: The convolution integrals, and the furnace test

**Files:**
- Create: `app/asset/ibl_bake.hpp`, `app/asset/ibl_bake.cpp`
- Modify: `app/CMakeLists.txt`
- Test: `tests/unit/app/ibl_bake_test.cpp` (create), `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Image`, `ImageFormat`, `cube_face_direction`, `half_to_float`, `float_to_half`.
- Produces:
  - `std::array<Image, 6> bake_irradiance(const std::array<Image, 6>& env, std::uint32_t size);`
  - `std::vector<std::array<Image, 6>> bake_prefiltered(const std::array<Image, 6>& env, std::uint32_t base_size, std::uint32_t levels);` — index `m` is mip level `m`.
  - `mosaic::foundation::math::Vec3 sample_cubemap(const std::array<Image, 6>& faces, const Vec3& dir);` — nearest-texel, used by both integrals.

- [ ] **Step 1: Write the furnace test first**

Create `tests/unit/app/ibl_bake_test.cpp`:

```cpp
// The furnace test, and the reason this slice convolves on the CPU.
//
// A furnace test puts a surface in a UNIFORM WHITE environment. A correct
// energy-conserving model returns exactly the input radiance -- the surface disappears
// into the background. Too dark means the model loses energy; too bright means it
// invents it. Slice 2's spec recorded as its honest limit that no test there could show
// the BRDF correct, and named this as what would.
//
// Because the integrals are C++ rather than shader code, this runs in CI with no GPU.

#include <array>
#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/asset/ibl_bake.hpp"

using Catch::Approx;
using mosaic::engine::render::Image;
using mosaic::engine::render::ImageFormat;

namespace
{
// Six faces of constant radiance L, in the Rgba16F form the real pipeline uses.
std::array<Image, 6> uniform_environment(std::uint32_t size, float L)
{
    std::array<Image, 6> env;
    const std::uint16_t h = mosaic::app::float_to_half(L);
    for (auto& f : env)
    {
        f.width = size;
        f.height = size;
        f.format = ImageFormat::Rgba16F;
        f.pixels.assign(static_cast<std::size_t>(size) * size * 8u, 0);
        for (std::size_t i = 0; i < static_cast<std::size_t>(size) * size; ++i)
        {
            for (int c = 0; c < 3; ++c)
            {
                f.pixels[i * 8 + c * 2 + 0] = static_cast<std::uint8_t>(h & 0xFF);
                f.pixels[i * 8 + c * 2 + 1] = static_cast<std::uint8_t>((h >> 8) & 0xFF);
            }
        }
    }
    return env;
}

float red_of(const Image& img, std::uint32_t x, std::uint32_t y)
{
    const std::size_t i = (static_cast<std::size_t>(y) * img.width + x) * 8;
    return mosaic::app::half_to_float(
        static_cast<std::uint16_t>(img.pixels[i] | (img.pixels[i + 1] << 8)));
}
} // namespace

TEST_CASE("furnace: irradiance of a uniform environment is that same uniform value", "[asset]")
{
    // The cosine-weighted hemisphere integral of a constant L, divided by pi, is L.
    // Any other answer means the integral's normalisation is wrong -- which would make
    // every diffuse surface uniformly too bright or too dark, a mistake that looks like
    // an exposure problem rather than a bug.
    const auto env = uniform_environment(16, 1.0f);
    const auto irr = mosaic::app::bake_irradiance(env, 8);
    for (int f = 0; f < 6; ++f)
    {
        for (std::uint32_t y = 0; y < 8; ++y)
        {
            for (std::uint32_t x = 0; x < 8; ++x)
            {
                INFO("face " << f << " texel " << x << "," << y);
                REQUIRE(red_of(irr[static_cast<std::size_t>(f)], x, y)
                        == Approx(1.0f).epsilon(0.02));
            }
        }
    }
}

TEST_CASE("furnace: irradiance scales linearly with input radiance", "[asset]")
{
    // Catches a normalisation that happens to be right at L = 1 -- e.g. an additive
    // offset, or a clamp -- which the test above alone would not distinguish.
    for (float L : {0.25f, 2.0f, 8.0f})
    {
        const auto irr = mosaic::app::bake_irradiance(uniform_environment(16, L), 4);
        INFO("L = " << L);
        REQUIRE(red_of(irr[0], 2, 2) == Approx(L).epsilon(0.02));
    }
}

TEST_CASE("furnace: prefiltered specular of a uniform environment is unchanged at every roughness",
          "[asset]")
{
    // The specular half. Convolving a constant with ANY normalised kernel returns that
    // constant, so every level must come back at L regardless of roughness.
    //
    // If a level comes back SHORT, that is the known single-scatter GGX energy loss the
    // backlog carries as a deferred multi-scatter item -- in which case pin the measured
    // shortfall here WITH its reason rather than widening the tolerance until it hides.
    const auto env = uniform_environment(16, 1.0f);
    const auto chain = mosaic::app::bake_prefiltered(env, 16, 5);
    REQUIRE(chain.size() == 5);
    for (std::size_t m = 0; m < chain.size(); ++m)
    {
        INFO("mip level " << m << " (roughness " << (static_cast<float>(m) / 4.0f) << ")");
        REQUIRE(red_of(chain[m][0], 0, 0) == Approx(1.0f).epsilon(0.02));
    }
}

TEST_CASE("irradiance of a single bright direction falls off with the cosine", "[asset]")
{
    // The integral's defining SHAPE, which the furnace tests cannot see: a constant
    // environment is rotationally symmetric, so they would pass even if the cosine
    // weight were missing entirely.
    std::array<Image, 6> env;
    for (auto& f : env)
    {
        f.width = 8; f.height = 8; f.format = ImageFormat::Rgba16F;
        f.pixels.assign(8u * 8u * 8u, 0);
    }
    // Light only the +Y face: a sky overhead and nothing else.
    const std::uint16_t h = mosaic::app::float_to_half(1.0f);
    for (std::size_t i = 0; i < 8u * 8u; ++i)
    {
        for (int c = 0; c < 3; ++c)
        {
            env[2].pixels[i * 8 + c * 2 + 0] = static_cast<std::uint8_t>(h & 0xFF);
            env[2].pixels[i * 8 + c * 2 + 1] = static_cast<std::uint8_t>((h >> 8) & 0xFF);
        }
    }
    const auto irr = mosaic::app::bake_irradiance(env, 4);
    // +Y (facing the light) must be brightest; -Y (facing away) must be near zero.
    const float up = red_of(irr[2], 2, 2);
    const float down = red_of(irr[3], 2, 2);
    REQUIRE(up > 0.2f);
    REQUIRE(down < up * 0.1f);
}

TEST_CASE("prefiltered roughness cannot sharpen", "[asset]")
{
    // A blur never increases local contrast. Catches a roughness-to-level mapping that
    // runs backwards -- which looks like a material that is too shiny rather than a bug.
    std::array<Image, 6> env;
    for (auto& f : env)
    {
        f.width = 16; f.height = 16; f.format = ImageFormat::Rgba16F;
        f.pixels.assign(16u * 16u * 8u, 0);
    }
    // A checkerboard on +X gives the integral something with high local variance.
    for (std::uint32_t y = 0; y < 16; ++y)
    {
        for (std::uint32_t x = 0; x < 16; ++x)
        {
            const std::uint16_t v = mosaic::app::float_to_half(((x + y) % 2) ? 1.0f : 0.0f);
            const std::size_t i = (static_cast<std::size_t>(y) * 16 + x) * 8;
            for (int c = 0; c < 3; ++c)
            {
                env[0].pixels[i + c * 2 + 0] = static_cast<std::uint8_t>(v & 0xFF);
                env[0].pixels[i + c * 2 + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
            }
        }
    }
    const auto chain = mosaic::app::bake_prefiltered(env, 16, 5);
    auto variance = [](const Image& img) {
        double mean = 0, m2 = 0;
        const std::size_t n = static_cast<std::size_t>(img.width) * img.height;
        for (std::uint32_t y = 0; y < img.height; ++y)
            for (std::uint32_t x = 0; x < img.width; ++x) { mean += red_of(img, x, y); }
        mean /= static_cast<double>(n);
        for (std::uint32_t y = 0; y < img.height; ++y)
            for (std::uint32_t x = 0; x < img.width; ++x)
            { const double d = red_of(img, x, y) - mean; m2 += d * d; }
        return m2 / static_cast<double>(n);
    };
    // Each successive level is rougher, so no level may be more contrasty than mip 0.
    const double v0 = variance(chain[0][0]);
    for (std::size_t m = 1; m < chain.size(); ++m)
    {
        INFO("mip level " << m);
        REQUIRE(variance(chain[m][0]) <= v0 + 1e-6);
    }
}
```

Register in `tests/CMakeLists.txt`.

- [ ] **Step 2: Run them to verify they fail.** Expected: compile error, `ibl_bake.hpp` does not exist.

- [ ] **Step 3: Write the header**

Declare the three functions with a comment block stating: the face order, that irradiance is cosine-weighted and divided by π, that prefiltered level `m` is roughness `m/(levels-1)`, that mip 0 is a resample, and that these live on the CPU **so the furnace test can exist at all**.

- [ ] **Step 4: Implement `sample_cubemap`**

Select the face by the largest-magnitude component of `dir`, map to that face's `(u, v)` by inverting `cube_face_direction`'s basis, and read the nearest texel. Nearest rather than bilinear is deliberate and must be commented: these integrals take thousands of samples, so filtering costs a great deal and buys almost nothing once averaged.

- [ ] **Step 5: Implement `bake_irradiance`**

For each face and texel, take `N = cube_face_direction(face, u, v)`, build a tangent basis about `N`, and sum over the hemisphere in spherical coordinates with a fixed step (0.025 rad is the conventional choice):

```
irradiance += sample_cubemap(env, L) * cos(theta) * sin(theta);
...
irradiance = PI * irradiance / sample_count;
```

The `π *` and the `/ sample_count` together are what make a uniform environment return itself — **this is exactly what the furnace test checks**, so if it fails, the normalisation is where to look, not the sampling.

- [ ] **Step 6: Implement `bake_prefiltered`**

Level 0: resample the environment to `base_size` with `sample_cubemap`, no convolution.

Levels 1..n−1: `roughness = m / (levels - 1)`, GGX importance sampling with a fixed sample count (1024 is conventional), accumulating `sample_cubemap(env, L) * NdotL` and dividing by the summed `NdotL`. Assume `N = V = R`, which is the split-sum's standing approximation and must be commented as such.

Each level is half the previous size, floored at 1 — reuse `mip_extent_for` from `engine/render/mip_levels.hpp` rather than restating the halving, for the reason that header already gives.

- [ ] **Step 7: Run the tests**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20 2>&1 | grep -cE "error:"
./build/debug/tests/mosaic_unit_tests "[asset]" 2>&1 | tail -3
```

Expected: pass. **If a furnace case fails short at high roughness, do not widen the tolerance.** Record the measured value, confirm it against the known single-scatter GGX loss, and pin it with its reason — that is the finding this slice exists to produce.

- [ ] **Step 8: Mutation probe**

```bash
cp app/asset/ibl_bake.cpp /tmp/ib.bak
sed -i 's|irradiance = kPi \* irradiance|irradiance = irradiance|' app/asset/ibl_bake.cpp
cmake --build --preset debug --target mosaic_unit_tests -j 20 2>&1 | grep -cE "error:"
./build/debug/tests/mosaic_unit_tests "[asset]" 2>&1 | tail -2
cp /tmp/ib.bak app/asset/ibl_bake.cpp && rm /tmp/ib.bak
```

Expected: FAIL (every diffuse surface π× too dark — the classic normalisation error), green on restore.

- [ ] **Step 9: Full gate and commit**

```bash
ctest --preset debug --output-on-failure 2>&1 | tail -3
git add app/asset/ibl_bake.hpp app/asset/ibl_bake.cpp app/CMakeLists.txt \
        tests/unit/app/ibl_bake_test.cpp tests/CMakeLists.txt
git commit -m "feat(asset): convolve irradiance and prefiltered specular, with a furnace test"
```

---

### Task 2: The BRDF LUT

**Files:**
- Modify: `app/asset/ibl_bake.hpp`, `app/asset/ibl_bake.cpp`
- Test: `tests/unit/app/ibl_bake_test.cpp`

**Interfaces:**
- Produces: `Image bake_brdf_lut(std::uint32_t size);` — `size²`, `Rgba16F`, red = scale, green = bias, blue/alpha zero.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("the BRDF LUT's terms stay within their analytic bounds", "[asset]")
{
    // Both split-sum terms are integrals of quantities in [0,1], so neither can leave
    // it. A value outside means the importance sampling or the Smith term is wrong, and
    // the visible result is a material that gains or loses energy with viewing angle --
    // which reads as a strange material rather than a bug.
    const auto lut = mosaic::app::bake_brdf_lut(32);
    REQUIRE(lut.width == 32);
    REQUIRE(lut.format == ImageFormat::Rgba16F);
    for (std::uint32_t y = 0; y < 32; ++y)
    {
        for (std::uint32_t x = 0; x < 32; ++x)
        {
            const std::size_t i = (static_cast<std::size_t>(y) * 32 + x) * 8;
            const float a = mosaic::app::half_to_float(
                static_cast<std::uint16_t>(lut.pixels[i] | (lut.pixels[i + 1] << 8)));
            const float b = mosaic::app::half_to_float(
                static_cast<std::uint16_t>(lut.pixels[i + 2] | (lut.pixels[i + 3] << 8)));
            INFO("texel " << x << "," << y << " scale " << a << " bias " << b);
            REQUIRE(a >= 0.0f);
            REQUIRE(a <= 1.0f);
            REQUIRE(b >= 0.0f);
            REQUIRE(b <= 1.0f);
        }
    }
}

TEST_CASE("the BRDF LUT approaches a pure Fresnel response at zero roughness", "[asset]")
{
    // At roughness 0 the microfacet distribution is a delta function, so the scale term
    // goes to 1 and the bias to 0: specular is exactly F0. This is the one corner of the
    // domain with a known closed-form answer, which makes it the honest anchor for the
    // whole table.
    const auto lut = mosaic::app::bake_brdf_lut(32);
    // Row 0 is roughness 0; sample a mid viewing angle to avoid the grazing edge.
    const std::size_t i = (static_cast<std::size_t>(0) * 32 + 24) * 8;
    const float a = mosaic::app::half_to_float(
        static_cast<std::uint16_t>(lut.pixels[i] | (lut.pixels[i + 1] << 8)));
    const float b = mosaic::app::half_to_float(
        static_cast<std::uint16_t>(lut.pixels[i + 2] | (lut.pixels[i + 3] << 8)));
    REQUIRE(a == Approx(1.0f).epsilon(0.05));
    REQUIRE(b == Approx(0.0f).margin(0.05));
}
```

- [ ] **Step 2: Run them to verify they fail.** Expected: `bake_brdf_lut` not declared.

- [ ] **Step 3: Implement it**

Standard split-sum integration: for each `(NdotV, roughness)` texel, importance-sample GGX, accumulate the Smith-geometry-weighted scale and bias terms over 1024 samples. Use the **IBL** geometry remap `k = α²/2`, **not** the direct-lighting `k = α/2` that `cube.frag` uses — comment that difference explicitly, because the two look identical and using the wrong one is a silent energy error.

- [ ] **Step 4: Run them to verify they pass**, then **Step 5**, full gate and commit:

```bash
git add app/asset/ibl_bake.hpp app/asset/ibl_bake.cpp tests/unit/app/ibl_bake_test.cpp
git commit -m "feat(asset): integrate the split-sum BRDF lookup table"
```

---

### Task 3: One cooked environment, carrying all three products

**Files:**
- Modify: `app/asset/ibl_bake.hpp`, `app/asset/ibl_bake.cpp`, `app/asset/asset_io.hpp`,
  `app/asset/cubemap_cook.hpp` (deprecate `cook_cubemap`/`uncook_cubemap`'s role as the import format),
  `engine/render/render_system.hpp`, `app/run/demo_scene.hpp`, `tests/unit/app/demo_scene_test.cpp`
- Test: `tests/unit/app/ibl_bake_test.cpp`

**Interfaces:**
- Produces:
```cpp
struct Environment
{
    std::array<mosaic::engine::render::Image, 6>              cubemap;     // the sky
    std::array<mosaic::engine::render::Image, 6>              irradiance;  // diffuse IBL
    std::vector<std::array<mosaic::engine::render::Image, 6>> prefiltered; // specular IBL, by mip
};
std::string cook_environment(const Environment&);
std::optional<Environment> uncook_environment(const std::string& bytes);
```

**Why one blob rather than two.** An `.hdr` import would otherwise produce a `.cubemap`
and a `.ibl` under a single asset id and a single `.toml` — but an `AssetID` resolves to
exactly one resource, so the second blob would need either a derived id or a sibling
metadata entry, both of which add a concept to the asset system for no gain. One blob,
one id, one registered `Environment` is simpler and removes the question.

**This supersedes `MOSAICUBE1` as the import format, which is safe to do now and will
not be later.** Verified while planning: `git ls-files` shows **zero** committed
`.cubemap` blobs and no asset `.toml` files, and the demo converts in memory rather than
through the cooked path — so nothing on disk depends on the old format. `cook_cubemap`
and `uncook_cubemap` stay (they are tested and may serve a cubemap that is not an
environment) but the importer stops writing them.

**Three call sites register or load `std::array<Image, 6>` today and must move to
`Environment`:** `app/asset/asset_io.hpp:305`, `engine/render/render_system.hpp:217`,
and `tests/unit/app/demo_scene_test.cpp:386`. Missing one is a compile error, not a
silent break.

- [ ] **Step 1: Write the failing round-trip test**

```cpp
TEST_CASE("an Environment round-trips through cook and uncook", "[asset]")
{
    Environment env;
    for (auto& f : env.cubemap)
    {
        f.width = 4; f.height = 4; f.format = ImageFormat::Rgba16F;
        f.pixels.assign(4u * 4u * 8u, 0x21);
    }
    for (auto& f : env.irradiance)
    {
        f.width = 2; f.height = 2; f.format = ImageFormat::Rgba16F;
        f.pixels.assign(2u * 2u * 8u, 0x33);
    }
    env.prefiltered.resize(3);
    for (std::size_t m = 0; m < 3; ++m)
    {
        const std::uint32_t sz = 4u >> m;
        for (auto& f : env.prefiltered[m])
        {
            f.width = sz; f.height = sz; f.format = ImageFormat::Rgba16F;
            f.pixels.assign(static_cast<std::size_t>(sz) * sz * 8u,
                            static_cast<std::uint8_t>(0x40 + m));
        }
    }

    const auto back = mosaic::app::uncook_environment(mosaic::app::cook_environment(env));
    REQUIRE(back.has_value());
    REQUIRE(back->cubemap[0].pixels == env.cubemap[0].pixels);
    REQUIRE(back->irradiance[3].pixels == env.irradiance[3].pixels);
    REQUIRE(back->prefiltered.size() == 3);
    for (std::size_t m = 0; m < 3; ++m)
    {
        INFO("mip " << m);
        REQUIRE(back->prefiltered[m][0].width == (4u >> m));
        REQUIRE(back->prefiltered[m][5].pixels == env.prefiltered[m][5].pixels);
    }
}

TEST_CASE("uncook_environment rejects malformed blobs", "[asset]")
{
    REQUIRE_FALSE(mosaic::app::uncook_environment("").has_value());
    REQUIRE_FALSE(mosaic::app::uncook_environment("not an environment").has_value());
    REQUIRE_FALSE(mosaic::app::uncook_environment("MOSAICENV1short").has_value());
    // A level count that disagrees with the payload length: the header claims more
    // prefiltered levels than the bytes can hold, which must be refused rather than
    // read past the end.
    std::string blob = "MOSAICENV1";
    auto put_u32 = [&](std::uint32_t v) {
        blob.push_back(static_cast<char>(v & 0xFF));
        blob.push_back(static_cast<char>((v >> 8) & 0xFF));
        blob.push_back(static_cast<char>((v >> 16) & 0xFF));
        blob.push_back(static_cast<char>((v >> 24) & 0xFF));
    };
    put_u32(4);   // cubemap face size
    put_u32(2);   // irradiance face size
    put_u32(4);   // prefiltered base size
    put_u32(99);  // 99 levels, with no payload at all
    put_u32(static_cast<std::uint32_t>(ImageFormat::Rgba16F));
    REQUIRE_FALSE(mosaic::app::uncook_environment(blob).has_value());
}
```

- [ ] **Step 2: Run them to verify they fail.** Expected: `Environment` is not a member of `mosaic::app`.

- [ ] **Step 3: Implement the format**

`"MOSAICENV1"` + `u32` cubemap face size + `u32` irradiance face size + `u32` prefiltered
base size + `u32` level count + `u32` format, then: six cubemap faces, six irradiance
faces, then for each level its six faces at `mip_extent_for(base, m)`.

Validate the total by **division** rather than by computing a product, the same guard
`uncook_image` and `uncook_cubemap` both needed and for the same reason — and bound the
level count against the payload before trusting it, which is what the malformed-blob test
above exercises.

- [ ] **Step 4: Import the whole environment from a `.hdr`**

In `asset_io.hpp`'s `.hdr` branch, replace the cubemap-only cook with:

```cpp
                mosaic::app::Environment env;
                env.cubemap = mosaic::app::equirect_to_cubemap(*env_img, 512);
                env.irradiance = mosaic::app::bake_irradiance(env.cubemap, 32);
                env.prefiltered = mosaic::app::bake_prefiltered(env.cubemap, 128, 5);
                blob_bytes = mosaic::app::cook_environment(env);
                blob_ext = ".env";
                asset_type = "environment";
```

and change the load branch from `"cubemap"`/`.cubemap`/`std::array<Image, 6>` to
`"environment"`/`.env`/`Environment`. Add `"environment"` to the reserved plugin asset
types beside `"cubemap"`.

**This import now takes seconds rather than milliseconds** — the prefiltered levels
dominate. Log a line before baking so it does not look like a hang.

- [ ] **Step 5: Move the three `std::array<Image, 6>` call sites to `Environment`**

`asset_io.hpp:305`, `render_system.hpp:217` and `demo_scene_test.cpp:386`. `RenderSystem`
resolves an `Environment` and uses `env->cubemap` where it currently uses the array.

- [ ] **Step 6: Run the tests, full gate, and commit**

```bash
ctest --preset debug --output-on-failure 2>&1 | tail -3
git add app/asset/ibl_bake.hpp app/asset/ibl_bake.cpp app/asset/asset_io.hpp \
        engine/render/render_system.hpp app/run/demo_scene.hpp \
        tests/unit/app/ibl_bake_test.cpp tests/unit/app/demo_scene_test.cpp
git commit -m "feat(asset): cook one environment carrying sky, irradiance and prefiltered"
```

---

### Task 4: Mip-chain cubemaps and the IBL descriptor set

**Files:**
- Modify: `engine/render/render_device.hpp`, `modules/render_vulkan/vulkan_render_device.cpp`
- Test: `tests/unit/render/texture_colorspace_contract_test.cpp`

**Interfaces:**
- Produces:
  - `virtual TextureHandle create_cubemap(const std::vector<std::array<Image, 6>>& levels) { return 0; }` — an overload; level 0 is the base.
  - `virtual void set_image_based_lighting(TextureHandle irradiance, TextureHandle prefiltered, TextureHandle brdf_lut) {}`

- [ ] **Step 1: Write the failing contract tests**

```cpp
TEST_CASE("the IBL descriptor set is allocated fresh, never rewritten in place", "[render]")
{
    // Review Focus 2, and the structural reason this is its own set rather than three
    // more bindings on the per-frame set.
    //
    // The environment resolves DURING render(), between begin_frame and end_frame, with
    // 2 frames in flight. Writing IBL descriptors into an existing per-frame set would
    // mutate one the other in-flight frame may still be reading -- undefined behaviour.
    // Allocating a new set and swapping the handle is safe, because the old set stays
    // valid for any command buffer still referencing it.
    const std::string src =
        mosaic::tests::normalize_ws(mosaic::tests::code_only(read_backend()));
    REQUIRE(src.find("ibl_set_layout_") != std::string::npos);
    REQUIRE(src.find("vkAllocateDescriptorSets(device_, &ibl_alloc, &ibl_set_)")
            != std::string::npos);
    // Four sets: material, shadow, frame UBO, IBL. Vulkan guarantees at least 4 bound
    // sets, so this is exactly at the floor and must not grow further without a check.
    REQUIRE(src.find("std::array<VkDescriptorSetLayout, 4> set_layouts") != std::string::npos);
}

TEST_CASE("a cubemap mip chain is uploaded, not generated", "[render]")
{
    // Review Focus 5. The prefiltered chain's levels are CONVOLUTIONS at increasing
    // roughness, not downsamples -- blitting level 0 down would silently replace the
    // slice's entire specular result with a blur. 4a's ladder must not be reused here.
    const std::string src =
        mosaic::tests::normalize_ws(mosaic::tests::code_only(read_backend()));
    const std::size_t at = src.find("create_cubemap(const std::vector<std::array<");
    REQUIRE(at != std::string::npos);
    const std::string body = src.substr(at, 3000);
    REQUIRE(body.find("vkCmdBlitImage") == std::string::npos);
    REQUIRE(body.find("cube_chain.mipLevels = static_cast<std::uint32_t>(levels.size());")
            != std::string::npos);
}
```

- [ ] **Step 2: Run them to verify they fail.**

- [ ] **Step 3: Implement the mip-chain overload**

Validate every level against `mip_extent_for(base, m)` and against its own format's stride — **a level whose size disagrees overruns the staging buffer**, which is Review Focus 5 and is not something validation layers reliably catch. Stage all levels × 6 faces into one buffer, one `VkBufferImageCopy` per (level, face), transition all levels and all layers together, and create a view spanning the chain with `maxLod` allowing it.

- [ ] **Step 4: Add the IBL descriptor set**

A fourth set layout: three fragment-stage combined image samplers at bindings 0–2. `set_image_based_lighting` allocates a **new** set from the pool, writes the three descriptors, and swaps `ibl_set_`; it must never rewrite a set already in use. At init, create 1×1 default resources (a black cube and a flat LUT) and point `ibl_set_` at them, so the set is always valid and the shader never samples an unbound descriptor.

Add it to the lit pipeline's `set_layouts` as set 3, and bind it in `draw` alongside the other three.

- [ ] **Step 5: Run the tests, then build and verify both entry points with validation clean.** Then commit.

```bash
git commit -m "feat(render): upload cubemap mip chains and bind the IBL descriptor set"
```

---

### Task 5: Sample IBL in the shader

**Files:**
- Modify: `modules/render_vulkan/shaders/cube.frag`, `modules/render_vulkan/vulkan_render_device.cpp` (the `FrameLighting` UBO gains an `ibl_enabled` flag)
- Test: `tests/unit/render/shader_specular_contract_test.cpp`

- [ ] **Step 1: Write the failing contract tests**

Pin: the three set-3 bindings; `textureLod(prefilteredMap, R, roughness * 4.0)` exactly (Review Focus 4 — `* 5.0` or a 4-level chain is a silent appearance change); `prefiltered * (F0_amb * ab.x + ab.y)`; and that the **flat `0.1` fallback still exists** (Review Focus 3 — deleting it breaks every scene without an environment, and nothing else would notice).

- [ ] **Step 2: Run them to verify they fail.**

- [ ] **Step 3: Change the shader**

```glsl
    vec3 F0_amb = mix(vec3(0.04), albedo, metallic);
    vec3 kD_amb = (vec3(1.0) - F0_amb) * (1.0 - metallic);

    vec3 ambient;
    if (fl.ibl_enabled > 0.5)
    {
        vec3 irradiance = texture(irradianceMap, n).rgb;
        vec3 diffuseIBL = kD_amb * albedo * irradiance;

        vec3 R = reflect(-v, n);
        // roughness * 4.0 indexes the FIVE prefiltered levels (roughness 0, .25, .5,
        // .75, 1). textureLod interpolates between them. A * 5.0 here, or a chain with
        // a different level count, produces a material that merely looks too shiny or
        // too blurry -- which reads as an authoring choice, not a bug.
        vec3 prefiltered = textureLod(prefilteredMap, R, roughness * 4.0).rgb;
        vec2 ab = texture(brdfLut, vec2(max(dot(n, v), 1e-4), roughness)).rg;
        ambient = diffuseIBL + prefiltered * (F0_amb * ab.x + ab.y);
    }
    else
    {
        // The pre-IBL ambient, kept so a scene with no environment renders exactly as
        // it did before this slice. Not dead code: it is the fallback.
        ambient = albedo * 0.1 * kD_amb;
    }
    vec3 color = ambient
               + dir_term * fl.light_color.xyz
               + ...
```

`ibl_enabled` goes in the `FrameLighting` UBO's existing padding if there is any; otherwise extend the struct and update its `static_assert`. **Check the std140 layout before assuming a free slot.**

- [ ] **Step 4: Run the tests, build, verify both entry points.** Commit.

---

### Task 6: Wire the demo and verify

**Files:**
- Modify: `app/run/demo_scene.hpp`, `engine/render/render_system.hpp`
- Test: `tests/unit/app/demo_scene_test.cpp`

- [ ] **Step 1: Write the failing test** — `setup_runtime` registers an `Environment` whose `irradiance` and `prefiltered` are populated (not just `cubemap`), and `RenderSystem` reports IBL as active once resolved. Extend the existing case at `tests/unit/app/demo_scene_test.cpp:380` rather than writing a second one; it already asserts the environment registers.

- [ ] **Step 2: Run it to verify it fails.**

- [ ] **Step 3: Bake at startup and resolve lazily**

Mirror 4b exactly. `setup_runtime` bakes the whole `Environment` in memory (the demo does
not go through the importer) and registers it. `RenderSystem` resolves it on the first
frame that has a device: `create_cubemap(env->cubemap)` for the sky as today, plus
`create_cubemap({env->irradiance})` for irradiance, `create_cubemap(env->prefiltered)` for
the chain, `create_texture(bake_brdf_lut(256), TextureColorSpace::Linear)` for the LUT,
then `set_image_based_lighting(...)`. Clear the asset id on any failure so it is attempted
once rather than every frame — the bug the 4b review found in the environment resolve.

**Baking at startup costs seconds.** Log before and after, and note in the comment that
the demo pays this on every run because it bypasses the importer; a project loading a
cooked `.env` does not.

**Switch the demo's environment to `kloofendal_2k.hdr`** — `space00_2k.hdr` has 0.2% mean radiance and cannot show that IBL works. Keep the path a single named constant so switching back is one line.

- [ ] **Step 4: Full gate, both entry points, validation clean.**

- [ ] **Step 5: Record the visual check and stop**

Tell the human partner to expect:
1. **The metal sphere reflecting the sky** — a horizon line and a sun glint, not black.
2. **Dielectrics picking up environment colour**, warm from the sun side and cool from the sky side, rather than a uniform grey wash.
3. **The skybox unchanged** — IBL changes how surfaces are lit, not what the sky looks like.
4. **Rougher bands on the sphere blurrier**, smoother ones sharper — that is the prefiltered chain being indexed correctly.

- [ ] **Step 6: Commit.**
