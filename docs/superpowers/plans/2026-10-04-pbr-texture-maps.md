# PBR Texture Maps and Image Import Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `metallic` and `roughness` per-texel, sampled from an artist-authored PNG, and add the PNG import needed to author one.

**Architecture:** `stb_image.h` is vendored beside the existing `stb_truetype.h` and wrapped by a new `read_image` in `app/asset/image_cook.cpp`, feeding the import pipeline that already exists. `RenderDevice::create_texture` gains a colour-space argument so data maps upload as `UNORM` rather than `SRGB`. `Material` gains one `AssetID`, `DrawParams` gains one `TextureHandle`, and Vulkan's descriptor set 0 grows a second binding, with set ownership moving from a texture to a `(base_color, metallic_roughness)` pair.

**Tech Stack:** C++20, Vulkan, GLSL 450, Catch2, stb_image (vendored header), CMake presets.

**Spec:** `docs/superpowers/specs/2026-10-04-pbr-texture-maps-design.md`

## Global Constraints

- Channel packing is glTF 2.0: **G = roughness, B = metallic**. Verbatim, not approximately.
- Sampling is **factor × texel**: `roughness = pc.roughness * mr.g`, `metallic = pc.metallic * mr.b`.
- `create_texture`'s colour-space parameter **defaults to `Srgb`**, so every existing call site keeps today's behaviour.
- Base colour maps upload `Srgb` (`VK_FORMAT_R8G8B8A8_SRGB`); metallic/roughness maps upload `Linear` (`VK_FORMAT_R8G8B8A8_UNORM`).
- An untextured material must render **identically to today**. The default metallic/roughness map is 1×1 white, so both factors pass through unchanged.
- The engine does **not** clamp `metallic`/`roughness`; the shader clamps `roughness` to `clamp(roughness, 0.045, 1.0)`. Unchanged from slice 2.
- `read_ppm` keeps working and keeps its tests.
- Never run `git add -A`; stage explicit paths. `docs/project/advancement-plans-and-evaluation.md` is the user's and must never be staged.

## Review Focus

Five input classes the spec implies but the tasks' own tests do not all exercise. Each has a test assigned to the task that owns the code.

1. **A metallic/roughness map uploaded as sRGB.** The whole slice's silent-failure mode: an authored 0.5 arrives as ~0.21, nothing crashes, nothing warns, the material looks plausible. Test in Task 2.
2. **G and B read backwards.** Swaps roughness and metallic; produces a plausible material rather than a visible defect, and no GPU in CI can catch it. Test in Task 4.
3. **A material with no metallic/roughness map.** The common real case and a different code path from having one. Must produce byte-identical `DrawParams` to today. Test in Task 3.
4. **A malformed or truncated image file.** stb_image is the first untrusted-input parser in the asset path; `read_image` must return `nullopt` rather than throw, read out of bounds, or return a half-decoded `Image`. Test in Task 1.
5. **More distinct texture pairs than the descriptor pool holds.** Pool sizing changes from "sets ≈ textures" to "sets ≈ pairs"; exhaustion must fail loudly, not silently reuse another material's set. Test in Task 4.

---

### Task 1: PNG import

**Files:**
- Create: `third_party/stb/stb_image.h` (vendored, downloaded)
- Create: `app/asset/image_cook.cpp`
- Modify: `app/asset/image_cook.hpp` (add `read_image` declaration)
- Modify: `app/asset/asset_io.hpp` (call `read_image` instead of `read_ppm`)
- Modify: `app/CMakeLists.txt` (add `asset/image_cook.cpp`)
- Test: `tests/unit/app/image_cook_test.cpp` (exists — extend)
- Test fixture: `tests/fixtures/gradient.png` (generated, committed)

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `std::optional<mosaic::engine::render::Image> mosaic::app::read_image(const std::string& bytes);` — decodes PNG/JPEG/BMP/TGA via stb_image, falls back to `read_ppm`, returns `nullopt` on any failure.

- [ ] **Step 1: Vendor stb_image**

```bash
curl -fsSL https://raw.githubusercontent.com/nothings/stb/master/stb_image.h \
  -o third_party/stb/stb_image.h
head -3 third_party/stb/stb_image.h
```

Expected: a header beginning `/* stb_image - v2.xx - public domain image loader`. If the download fails, stop — do not hand-write a decoder.

- [ ] **Step 2: Generate the PNG test fixture**

A fixture must have known pixel values, so generate rather than download. Python's zlib + struct can write a minimal PNG with no third-party dependency:

```bash
mkdir -p tests/fixtures && python3 - <<'PY'
import zlib, struct
W = H = 4
# Row y: R=0, G=y*80 (roughness ramp), B=255 (metal), A=255.
raw = b''.join(b'\x00' + bytes([0, min(y*80,255), 255, 255] * W) for y in range(H))
def chunk(t, d):
    c = t + d
    return struct.pack('>I', len(d)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
png = (b'\x89PNG\r\n\x1a\n'
       + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 6, 0, 0, 0))
       + chunk(b'IDAT', zlib.compress(raw))
       + chunk(b'IEND', b''))
open('tests/fixtures/gradient.png','wb').write(png)
print('wrote', len(png), 'bytes')
PY
```

Expected: `wrote <N> bytes`, N around 90-120.

- [ ] **Step 3: Write the failing tests**

Append to `tests/unit/app/image_cook_test.cpp`:

```cpp
TEST_CASE("read_image decodes a PNG to RGBA with known pixels", "[asset]")
{
    // Review Focus 4's companion: the happy path, with values chosen so a channel
    // swap or a row flip is visible. Row y has G = y*80, B = 255.
    const std::string path = std::string(MOSAIC_TEST_DIR) + "/fixtures/gradient.png";
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();

    const auto img = mosaic::app::read_image(ss.str());
    REQUIRE(img.has_value());
    REQUIRE(img->width == 4);
    REQUIRE(img->height == 4);
    REQUIRE(img->pixels.size() == 4u * 4u * 4u);
    // Row 0, pixel 0: R=0 G=0 B=255 A=255.
    REQUIRE(img->pixels[0] == 0);
    REQUIRE(img->pixels[1] == 0);
    REQUIRE(img->pixels[2] == 255);
    REQUIRE(img->pixels[3] == 255);
    // Row 2, pixel 0: G = 160. Catches a vertical flip, which stb_image can do.
    REQUIRE(img->pixels[(2 * 4 + 0) * 4 + 1] == 160);
}

TEST_CASE("read_image still accepts a PPM, so the fallback works", "[asset]")
{
    // read_ppm is the only decoder with no third-party code in its path, which makes
    // it the honest fixture for everything downstream of decode. It must keep working.
    std::string ppm = "P6 2 1 255 ";
    ppm.push_back(static_cast<char>(10));  ppm.push_back(static_cast<char>(20));  ppm.push_back(static_cast<char>(30));
    ppm.push_back(static_cast<char>(40));  ppm.push_back(static_cast<char>(50));  ppm.push_back(static_cast<char>(60));
    const auto img = mosaic::app::read_image(ppm);
    REQUIRE(img.has_value());
    REQUIRE(img->width == 2);
    REQUIRE(img->pixels[0] == 10);
    REQUIRE(img->pixels[4] == 40);
}

TEST_CASE("read_image rejects malformed input instead of throwing", "[asset]")
{
    // Review Focus 4. stb_image is the first untrusted-input parser in the asset
    // path: a project directory can now contain a hostile file. Every one of these
    // must be nullopt -- not a throw, not a half-decoded Image, not a read past the
    // end of the buffer.
    REQUIRE_FALSE(mosaic::app::read_image("").has_value());
    REQUIRE_FALSE(mosaic::app::read_image("not an image at all").has_value());
    // A valid PNG signature followed by nothing.
    REQUIRE_FALSE(mosaic::app::read_image(std::string("\x89PNG\r\n\x1a\n", 8)).has_value());
    // A truncated PNG: real header, IDAT cut in half.
    const std::string path = std::string(MOSAIC_TEST_DIR) + "/fixtures/gradient.png";
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string full = ss.str();
    REQUIRE_FALSE(mosaic::app::read_image(full.substr(0, full.size() / 2)).has_value());
}

TEST_CASE("a PNG survives the cook/uncook round trip", "[asset]")
{
    // The chain the importer actually runs: decode -> MOSAIMG1 -> decode. If this
    // drifts, an imported texture is wrong on disk while read_image's own test passes.
    const std::string path = std::string(MOSAIC_TEST_DIR) + "/fixtures/gradient.png";
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();

    const auto decoded = mosaic::app::read_image(ss.str());
    REQUIRE(decoded.has_value());
    const auto restored = mosaic::app::uncook_image(mosaic::app::cook_image(*decoded));
    REQUIRE(restored.has_value());
    REQUIRE(restored->width == decoded->width);
    REQUIRE(restored->height == decoded->height);
    REQUIRE(restored->pixels == decoded->pixels);
}
```

Add `#include <fstream>` and `#include <sstream>` to that file if absent.

- [ ] **Step 4: Run them to verify they fail**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20 2>&1 | grep -E "error:" | head
```

Expected: a compile error, `read_image` is not a member of `mosaic::app`. A compile failure IS the red state for a function that does not exist yet.

- [ ] **Step 5: Declare read_image**

In `app/asset/image_cook.hpp`, above `read_ppm`, add:

```cpp
// Decodes PNG/JPEG/BMP/TGA via stb_image, falling back to read_ppm so the
// dependency-free format keeps working. nullopt on any failure -- malformed input is
// reachable from a project directory, so this must not throw or partially decode.
// Not inline: stb_image's implementation macro belongs in exactly one TU
// (image_cook.cpp), the same arrangement stb_truetype already has in font_cook.cpp.
std::optional<mosaic::engine::render::Image> read_image(const std::string& bytes);
```

- [ ] **Step 6: Implement it**

Create `app/asset/image_cook.cpp`:

```cpp
#include "app/asset/image_cook.hpp"

#include <limits>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO      // we always decode from memory; no file paths reach stb
#define STBI_NO_HDR        // float HDR has no path into our RGBA8 Image
#define STBI_NO_GIF        // animation has no meaning for a texture
#include "third_party/stb/stb_image.h"

namespace mosaic::app
{
std::optional<mosaic::engine::render::Image> read_image(const std::string& bytes)
{
    if (bytes.empty()) { return std::nullopt; }

    int w = 0, h = 0, channels_in_file = 0;
    stbi_uc* decoded = stbi_load_from_memory(
        reinterpret_cast<const stbi_uc*>(bytes.data()),
        static_cast<int>(bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())
                             ? std::numeric_limits<int>::max()
                             : bytes.size()),
        &w, &h, &channels_in_file, 4); // 4 == force RGBA, which is what Image holds

    if (decoded == nullptr)
    {
        // Not a format stb understands. PPM is ours, so try it before giving up.
        return read_ppm(bytes);
    }
    if (w <= 0 || h <= 0)
    {
        stbi_image_free(decoded);
        return std::nullopt;
    }

    mosaic::engine::render::Image img;
    img.width = static_cast<std::uint32_t>(w);
    img.height = static_cast<std::uint32_t>(h);
    const std::size_t count = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
    img.pixels.assign(decoded, decoded + count);
    stbi_image_free(decoded);
    return img;
}
} // namespace mosaic::app
```

- [ ] **Step 7: Add it to the build and wire the importer**

In `app/CMakeLists.txt`, beside `asset/font_cook.cpp`, add `asset/image_cook.cpp`.

In `app/asset/asset_io.hpp`, in the import dispatch's final `else`, change the call and the message:

```cpp
            const std::optional<mosaic::engine::render::Image> img = read_image(*src);
            if (!img)
            {
                mosaic::foundation::log::error(
                    "import", "not a decodable image (PNG/JPEG/BMP/TGA, or PPM P6 maxval 255): {}",
                    source_file);
                return std::nullopt;
            }
```

- [ ] **Step 8: Run the tests to verify they pass**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20 2>&1 | grep -cE "error:"
./build/debug/tests/mosaic_unit_tests "[asset]" 2>&1 | tail -3
```

Expected: `0` errors, all `[asset]` tests pass.

- [ ] **Step 9: Run the full gate**

```bash
ctest --preset debug --output-on-failure 2>&1 | tail -3
```

Expected: 100% pass. `image_cook.hpp` stopped being header-only, so any TU that included it for `read_ppm` must still link — this is where that shows up.

- [ ] **Step 10: Commit**

```bash
git add third_party/stb/stb_image.h app/asset/image_cook.cpp app/asset/image_cook.hpp \
        app/asset/asset_io.hpp app/CMakeLists.txt tests/unit/app/image_cook_test.cpp \
        tests/fixtures/gradient.png
git commit -m "feat(asset): import PNG/JPEG via stb_image, keeping PPM as the fallback"
```

---

### Task 2: Colour-space-aware texture upload

**Files:**
- Modify: `engine/render/render_device.hpp` (the enum and the pure virtual)
- Modify: `engine/render/texture_cache.hpp` (pass the space through)
- Modify: `modules/render_vulkan/vulkan_render_device.cpp:472` (honour it)
- Modify: `tests/unit/render/texture_cache_test.cpp`, `tests/unit/studio/viewport_test.cpp`, `tests/unit/render/mesh_cache_test.cpp`, `tests/unit/render/render_system_test.cpp`, `tests/unit/render/render_device_hooks_test.cpp` (override signatures)
- Test: `tests/unit/render/texture_colorspace_contract_test.cpp` (create)

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces: `enum class TextureColorSpace { Srgb, Linear };` and
  `virtual TextureHandle create_texture(const Image&, TextureColorSpace = TextureColorSpace::Srgb) = 0;`
  plus `TextureCache::get(id, registry, device, TextureColorSpace = TextureColorSpace::Srgb)`.

- [ ] **Step 1: Write the failing contract test**

Create `tests/unit/render/texture_colorspace_contract_test.cpp`:

```cpp
// Pins the one decision in PBR slice 3 that fails silently.
//
// create_texture uploaded every texture as R8G8B8A8_SRGB. That is correct for base
// colour -- the hardware applies the sRGB EOTF on sample, so the shader gets linear
// values, which is what the BRDF needs. A metallic/roughness map is DATA, not colour:
// uploaded as sRGB, an authored roughness of 0.5 (byte 128) arrives as roughly 0.21.
// Nothing crashes, no validation layer complains, and the result looks like a
// plausible material. There is no GPU in CI, so this is a source contract -- the same
// idiom as shader_specular_contract_test.cpp.

#include <string>

#include <catch2/catch_test_macros.hpp>

#include "engine/render/render_device.hpp"
#include "tests/unit/render/shader_source.hpp"

using mosaic::engine::render::TextureColorSpace;

namespace
{
std::string read_backend()
{
    const std::string path =
        std::string(MOSAIC_TEST_DIR) + "/../modules/render_vulkan/vulkan_render_device.cpp";
    std::ifstream in(path);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
} // namespace

TEST_CASE("the colour space default keeps every existing call site on sRGB", "[render]")
{
    // The default is load-bearing: the font atlas, the default white, and every
    // base-colour texture rely on it. Changing it silently re-colours all of them.
    REQUIRE(TextureColorSpace::Srgb == TextureColorSpace{});
}

TEST_CASE("the Vulkan backend maps Linear to UNORM and Srgb to SRGB", "[render]")
{
    // Review Focus 1. Both formats must appear and be selected by the space, not
    // hardcoded. If this fails because the backend was refactored, check that a data
    // map still reaches the GPU as UNORM before relaxing it.
    const std::string src = mosaic::tests::code_only(read_backend());
    INFO("A metallic/roughness map uploaded as R8G8B8A8_SRGB has its values distorted "
         "by the sRGB EOTF: an authored 0.5 arrives as ~0.21, and the image merely "
         "looks like a different material.");
    REQUIRE(src.find("VK_FORMAT_R8G8B8A8_UNORM") != std::string::npos);
    REQUIRE(src.find("VK_FORMAT_R8G8B8A8_SRGB") != std::string::npos);
    // The format must be chosen from the parameter, not written twice as a constant.
    REQUIRE(src.find("TextureColorSpace::Linear") != std::string::npos);
}
```

Add it to `tests/CMakeLists.txt` beside the other `unit/render/` sources.

- [ ] **Step 2: Run it to verify it fails**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20 2>&1 | grep -E "error:" | head -3
```

Expected: a compile error — `TextureColorSpace` is not a member of `mosaic::engine::render`.

- [ ] **Step 3: Add the enum and change the interface**

In `engine/render/render_device.hpp`, above `create_texture`:

```cpp
// Whether a texture's bytes are COLOUR or DATA.
//
// Srgb: the hardware applies the sRGB EOTF on sample, so the shader receives linear
// values. Correct for base colour, and the historical behaviour of every texture.
//
// Linear: the bytes are numbers that happen to be stored in an image -- a
// metallic/roughness map, and every future data map. Uploading one as Srgb distorts
// it: an authored 0.5 arrives as roughly 0.21, with no crash and no validation error.
//
// The default is Srgb so that every call site predating this parameter stays correct.
// The cost of that convenience is that a FUTURE data map added by someone who has not
// read this comment gets sRGB silently; texture_colorspace_contract_test.cpp is what
// makes the decision visible when it changes.
enum class TextureColorSpace { Srgb, Linear };
```

and change the pure virtual:

```cpp
    virtual TextureHandle create_texture(const Image& image,
                                         TextureColorSpace space = TextureColorSpace::Srgb) = 0;
```

- [ ] **Step 4: Update all six overrides**

Each override must match the new signature. The five test doubles take the parameter and ignore it; the Vulkan one honours it.

In `tests/unit/render/texture_cache_test.cpp:35`, `tests/unit/render/render_system_test.cpp:91`:

```cpp
    TextureHandle create_texture(const Image& img, TextureColorSpace = TextureColorSpace::Srgb) override
```

In `tests/unit/studio/viewport_test.cpp:85`, `tests/unit/render/mesh_cache_test.cpp:42`, `tests/unit/render/render_device_hooks_test.cpp:32`:

```cpp
    TextureHandle create_texture(const Image&, TextureColorSpace = TextureColorSpace::Srgb) override { /* unchanged body */ }
```

In `modules/render_vulkan/vulkan_render_device.cpp:472`:

```cpp
    create_texture(const mosaic::engine::render::Image& image,
                   mosaic::engine::render::TextureColorSpace space =
                       mosaic::engine::render::TextureColorSpace::Srgb) override
```

and at the two format sites (lines ~504 and ~587), replace the hardcoded constant:

```cpp
        const VkFormat tex_format =
            (space == mosaic::engine::render::TextureColorSpace::Linear)
                ? VK_FORMAT_R8G8B8A8_UNORM
                : VK_FORMAT_R8G8B8A8_SRGB;
```

then use `tex_format` for both `img.format` and `view.format`. Compute it once, near the top of the function — the image and its view must agree, and two independent ternaries is how they come to disagree.

- [ ] **Step 5: Pass the space through TextureCache**

In `engine/render/texture_cache.hpp`, the cache key must include the space: the same `AssetID` could legitimately be wanted as both colour and data, and returning the colour upload for a data request is the silent failure this whole task exists to prevent.

```cpp
    TextureHandle get(mosaic::foundation::serialization::AssetID id,
                      mosaic::engine::assets::AssetRegistry& registry, RenderDevice& device,
                      TextureColorSpace space = TextureColorSpace::Srgb)
    {
        if (id == mosaic::foundation::serialization::AssetID{})
        {
            return 0;
        }
        // Keyed by (id, space): one AssetID can be wanted as both colour and data, and
        // handing back the wrong upload is exactly the distortion this parameter exists
        // to prevent.
        const Key key{id, space};
        auto it = cache_.find(key);
        if (it != cache_.end())
        {
            return it->second;
        }
        TextureHandle handle = 0;
        if (std::shared_ptr<Image> img = registry.load<Image>(id))
        {
            handle = device.create_texture(*img, space);
        }
        cache_.emplace(key, handle);
        return handle;
    }

private:
    struct Key
    {
        mosaic::foundation::serialization::AssetID id;
        TextureColorSpace space;
        bool operator==(const Key& o) const { return id == o.id && space == o.space; }
    };
    struct KeyHash
    {
        std::size_t operator()(const Key& k) const
        {
            return std::hash<mosaic::foundation::serialization::AssetID>{}(k.id)
                   ^ (static_cast<std::size_t>(k.space) << 1);
        }
    };
    std::unordered_map<Key, TextureHandle, KeyHash> cache_;
```

- [ ] **Step 6: Run the tests to verify they pass**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20 2>&1 | grep -cE "error:"
./build/debug/tests/mosaic_unit_tests "[render]" 2>&1 | tail -3
```

Expected: `0` errors, all `[render]` pass.

- [ ] **Step 7: Mutation probe — prove the contract test catches a hardcoded format**

```bash
cp modules/render_vulkan/vulkan_render_device.cpp /tmp/vd.bak
sed -i 's|? VK_FORMAT_R8G8B8A8_UNORM|? VK_FORMAT_R8G8B8A8_SRGB|' modules/render_vulkan/vulkan_render_device.cpp
./build/debug/tests/mosaic_unit_tests "[render]" 2>&1 | tail -3
cp /tmp/vd.bak modules/render_vulkan/vulkan_render_device.cpp && rm /tmp/vd.bak
```

Expected: the run FAILS (UNORM no longer present), then the restore brings it back to green. If it passes, the test is pinning prose rather than code — fix the test before continuing.

- [ ] **Step 8: Full gate and commit**

```bash
ctest --preset debug --output-on-failure 2>&1 | tail -3
git add engine/render/render_device.hpp engine/render/texture_cache.hpp \
        modules/render_vulkan/vulkan_render_device.cpp tests/CMakeLists.txt \
        tests/unit/render/texture_colorspace_contract_test.cpp \
        tests/unit/render/texture_cache_test.cpp tests/unit/render/render_system_test.cpp \
        tests/unit/render/mesh_cache_test.cpp tests/unit/render/render_device_hooks_test.cpp \
        tests/unit/studio/viewport_test.cpp
git commit -m "feat(render): upload data textures as UNORM via a colour-space parameter"
```

---

### Task 3: Material and DrawParams carry the map

**Files:**
- Modify: `engine/render/material.hpp`
- Modify: `engine/render/render_device.hpp` (`DrawParams`)
- Modify: `engine/render/render_system.hpp` (resolve and pass it; the linear default white)
- Test: `tests/unit/reflection/lighting_reflection_test.cpp`, `tests/unit/render/render_system_test.cpp`

**Interfaces:**
- Consumes: `TextureColorSpace` and the 4-argument `TextureCache::get` from Task 2.
- Produces: `Material::metallic_roughness_texture` (`AssetID`) and `DrawParams::metallic_roughness` (`TextureHandle`, default 0).

- [ ] **Step 1: Write the failing tests**

In `tests/unit/reflection/lighting_reflection_test.cpp`, extend the round-trip case — add to the `Material` setup `mat.metallic_roughness_texture = mr_id;` with a second distinct `AssetID`, and inside the `for_each<Material>` lambda:

```cpp
        REQUIRE(m.metallic_roughness_texture == mr_id); // the MR map AssetID round-trips
```

In `tests/unit/render/render_system_test.cpp`, append:

```cpp
TEST_CASE("a Material with no metallic/roughness map draws exactly as before", "[render]")
{
    // Review Focus 3. The common real case, and a different path from having a map.
    // An untextured material must be byte-identical to today's behaviour, or this
    // slice silently re-shades every existing scene.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);

    const auto e = world.create_entity();
    world.add_component<mosaic::engine::transform::Transform>(
        e, mosaic::engine::transform::Transform{});
    world.add_component<mosaic::engine::transform::WorldTransform>(
        e, mosaic::engine::transform::WorldTransform{});
    world.add_component<MeshRenderer>(e, MeshRenderer{kCubeMeshId});
    Material mat{};
    mat.metallic = 0.25f;
    mat.roughness = 0.75f;
    world.add_component<Material>(e, mat);

    rs.render(world, registry, device);

    REQUIRE(device.draws.size() == 1);
    // Nil map -> handle 0 is replaced by the 1x1 linear white default, never left at 0,
    // because the shader multiplies by it unconditionally.
    REQUIRE(device.draws[0].params.metallic_roughness != 0);
    REQUIRE(device.draws[0].params.metallic == Approx(0.25f));
    REQUIRE(device.draws[0].params.roughness == Approx(0.75f));
}

TEST_CASE("a metallic/roughness map is uploaded as linear, not sRGB", "[render]")
{
    // Review Focus 1, at the layer that chooses. The contract test in Task 2 pins the
    // backend's mapping; this pins that RenderSystem asks for Linear in the first
    // place. Both halves are needed: a correct backend asked for Srgb is still wrong.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);

    std::array<std::uint8_t, 16> mr_bytes{};
    mr_bytes[0] = 0x5A;
    const AssetID mr_id{mr_bytes};
    auto image = std::make_shared<Image>();
    image->width = 1; image->height = 1; image->pixels = {0, 128, 255, 255};
    registry.register_resource<Image>(
        mosaic::engine::assets::AssetMetadata{mr_id, "texture"}, image);

    const auto e = world.create_entity();
    world.add_component<mosaic::engine::transform::Transform>(
        e, mosaic::engine::transform::Transform{});
    world.add_component<mosaic::engine::transform::WorldTransform>(
        e, mosaic::engine::transform::WorldTransform{});
    world.add_component<MeshRenderer>(e, MeshRenderer{kCubeMeshId});
    Material mat{};
    mat.metallic_roughness_texture = mr_id;
    world.add_component<Material>(e, mat);

    rs.render(world, registry, device);

    REQUIRE(device.texture_spaces.count(mr_id) == 1);
    REQUIRE(device.texture_spaces.at(mr_id) == TextureColorSpace::Linear);
}
```

`FakeRenderDevice` needs to record the space. Add to it a `std::map<AssetID, TextureColorSpace>`-equivalent — since `create_texture` sees only an `Image`, record by uploaded pixel content instead:

```cpp
    // Records the colour space each upload asked for, keyed by the image's first pixel,
    // which is enough to tell the fixtures in these tests apart.
    std::map<std::array<std::uint8_t, 4>, TextureColorSpace> upload_spaces;
```

and in `create_texture`, before returning, record
`upload_spaces[{img.pixels[0], img.pixels[1], img.pixels[2], img.pixels[3]}] = space;`
(guarding `img.pixels.size() >= 4`). Then the assertion reads
`REQUIRE(device.upload_spaces.at({0,128,255,255}) == TextureColorSpace::Linear);`
— adjust the test above to that form, which avoids needing the device to know AssetIDs.

- [ ] **Step 2: Run them to verify they fail**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20 2>&1 | grep -E "error:" | head -3
```

Expected: compile errors — `metallic_roughness_texture` is not a member of `Material`, `metallic_roughness` is not a member of `DrawParams`.

- [ ] **Step 3: Add the Material field**

In `engine/render/material.hpp`, after `roughness`:

```cpp
    // Per-texel metallic/roughness, glTF 2.0 packing: G = roughness, B = metallic.
    // Nil = untextured, and the two scalars above apply alone. When set, each scalar
    // is a FACTOR multiplied by its texel -- which is what lets an existing scene keep
    // its appearance, since the default map is white and multiplying by 1 changes
    // nothing. base_color/texture have the same factor-times-texel relationship.
    mosaic::foundation::serialization::AssetID metallic_roughness_texture{};
```

and in `reflect`, after the `roughness` member:

```cpp
    v.member("metallic_roughness_texture", m.metallic_roughness_texture);
```

- [ ] **Step 4: Add the DrawParams field**

In `engine/render/render_device.hpp`, at the end of `DrawParams`:

```cpp
    // The metallic/roughness map's device handle. 0 means "none"; RenderSystem
    // substitutes a 1x1 linear white texture rather than leaving it 0, because the
    // shader multiplies by this unconditionally and an unbound sampler is undefined.
    TextureHandle metallic_roughness = 0;
```

- [ ] **Step 5: Resolve it in RenderSystem**

In `engine/render/render_system.hpp`'s mesh loop, beside the existing `texture` resolution:

```cpp
                TextureHandle texture = 0;
                TextureHandle mr_texture = 0;
                if (const Material* mat = world.get_component<Material>(e))
                {
                    base_color = mat->base_color;
                    metallic = mat->metallic;
                    roughness = mat->roughness;
                    texture = texture_cache_.get(mat->texture, registry, device);
                    // Linear, not Srgb: these are numbers, not a picture. See
                    // TextureColorSpace's comment for what sRGB would do to them.
                    mr_texture = texture_cache_.get(mat->metallic_roughness_texture, registry,
                                                    device, TextureColorSpace::Linear);
                }
                if (texture == 0)
                {
                    texture = default_white(device);
                }
                if (mr_texture == 0)
                {
                    mr_texture = default_linear_white(device);
                }
```

and pass it in the `DrawParams` aggregate:

```cpp
                device.draw(mesh, texture,
                            DrawParams{projection * view * wt->matrix, wt->matrix,
                                       normal_matrix, base_color, metallic, roughness,
                                       mr_texture});
```

Add the linear default beside `default_white`:

```cpp
    // A SEPARATE 1x1 white from default_white, uploaded Linear rather than Srgb.
    // White happens to be identical under both transfer functions, so sharing one
    // would work today -- and would quietly stop working the moment someone changes
    // either default to a non-extreme value. Two defaults, each correct by
    // construction, is cheaper than that bug.
    TextureHandle default_linear_white(RenderDevice& device) const
    {
        if (default_linear_white_ == 0)
        {
            Image white;
            white.width = 1;
            white.height = 1;
            white.pixels = {255, 255, 255, 255};
            default_linear_white_ = device.create_texture(white, TextureColorSpace::Linear);
        }
        return default_linear_white_;
    }
```

with `mutable TextureHandle default_linear_white_ = 0;` beside the existing `default_white_`.

- [ ] **Step 6: Run the tests to verify they pass**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20 2>&1 | grep -cE "error:"
./build/debug/tests/mosaic_unit_tests "[render],[reflection]" 2>&1 | tail -3
```

Expected: `0` errors, all pass.

- [ ] **Step 7: Full gate and commit**

```bash
ctest --preset debug --output-on-failure 2>&1 | tail -3
git add engine/render/material.hpp engine/render/render_device.hpp \
        engine/render/render_system.hpp tests/unit/reflection/lighting_reflection_test.cpp \
        tests/unit/render/render_system_test.cpp
git commit -m "feat(render): Material carries a metallic/roughness map through to DrawParams"
```

---

### Task 4: Vulkan descriptors and shader sampling

**Files:**
- Modify: `modules/render_vulkan/vulkan_render_device.cpp` (set layout, pool, per-pair sets, draw bind)
- Modify: `modules/render_vulkan/shaders/cube.frag` (binding 1, sampling)
- Modify: `modules/render_vulkan/shaders/cube.vert` (nothing — verify the Push block is untouched)
- Test: `tests/unit/render/shader_specular_contract_test.cpp`

**Interfaces:**
- Consumes: `DrawParams::metallic_roughness` from Task 3; `TextureColorSpace` from Task 2.
- Produces: nothing later tasks consume in code.

- [ ] **Step 1: Write the failing shader contract tests**

Append to `tests/unit/render/shader_specular_contract_test.cpp`:

```cpp
TEST_CASE("cube.frag samples metallic/roughness with the glTF channel packing", "[render]")
{
    // Review Focus 2, and the subtlest way this slice fails. G = roughness,
    // B = metallic is a CONVENTION, not a derivation -- there is nothing about the
    // data that makes roughness green. Read them backwards and a rough metal becomes
    // a smooth dielectric with metallic variation: both are materials, neither looks
    // like a bug, and no GPU in CI can tell. Every authoring tool exports this
    // packing, so getting it wrong makes every imported asset subtly wrong.
    const std::string frag = code_only(read_shader("cube.frag"));
    REQUIRE(frag.find("mr.g") != std::string::npos);
    REQUIRE(frag.find("mr.b") != std::string::npos);
    // Pin the ASSIGNMENT, not merely that both swizzles appear somewhere: the whole
    // failure mode is the two being crossed, and both strings survive a swap.
    REQUIRE(frag.find("pc.roughness * mr.g") != std::string::npos);
    REQUIRE(frag.find("pc.metallic * mr.b") != std::string::npos);
}

TEST_CASE("cube.frag declares the metallic/roughness sampler at set 0 binding 1", "[render]")
{
    // The base colour sampler is set 0 binding 0 and has been since textures existed.
    // A second binding in the same set is what lets one bind serve a whole material;
    // moving it to another set silently costs a descriptor slot per future map type.
    const std::string frag = code_only(read_shader("cube.frag"));
    REQUIRE(frag.find("set = 0, binding = 1") != std::string::npos);
}
```

- [ ] **Step 2: Run them to verify they fail**

```bash
./build/debug/tests/mosaic_unit_tests "[render]" 2>&1 | tail -3
```

Expected: FAIL. These read the shader at run time, so no rebuild is needed — if a rebuild seems necessary, the test is reading the wrong path.

- [ ] **Step 3: Add the sampler and sampling to cube.frag**

After the existing `layout(set = 0, binding = 0) uniform sampler2D tex;`:

```glsl
// Metallic/roughness map, glTF 2.0 packing: G = roughness, B = metallic. Uploaded
// UNORM (see TextureColorSpace) because these are numbers, not colour. An untextured
// material binds a 1x1 white texture, so both factors below multiply by 1.
layout(set = 0, binding = 1) uniform sampler2D metallicRoughnessMap;
```

In `main()`, replace the two uses of `pc.metallic` / `pc.roughness` in the `brdf` calls and the ambient with sampled values computed once, right after `albedo`:

```glsl
    // Factor x texel, the glTF convention: the push-constant scalar scales the map,
    // so an untextured material (white map) keeps the scalar's exact meaning and an
    // existing scene renders identically.
    vec3  mr        = texture(metallicRoughnessMap, fragUV).rgb;
    float metallic  = pc.metallic * mr.b;
    float roughness = pc.roughness * mr.g;
```

then replace every `pc.metallic` with `metallic` and every `pc.roughness` with `roughness` in the three `brdf(...)` calls and in `F0_amb`/`kD_amb`. Leave the `Push` block's field names alone — `cube.vert` declares the same block and the contract test added in slice 2 compares them.

- [ ] **Step 4: Run them to verify they pass**

```bash
./build/debug/tests/mosaic_unit_tests "[render]" 2>&1 | tail -3
```

Expected: all pass.

- [ ] **Step 5: Grow the descriptor set layout**

In `create_descriptor_infrastructure()`, make the layout two bindings:

```cpp
        VkDescriptorSetLayoutBinding bindings[2]{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1] = bindings[0];
        bindings[1].binding = 1;

        VkDescriptorSetLayoutCreateInfo layout{};
        layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout.bindingCount = 2;
        layout.pBindings = bindings;
```

and size the pool for pairs rather than textures:

```cpp
        // A set now belongs to a (base_color, metallic_roughness) PAIR, not to a
        // texture, so maxSets is no longer kMaxTextures. kMaxMaterialSets is the
        // number of DISTINCT pairs a scene may use; each consumes two
        // combined-image-samplers. Exhaustion fails loudly in material_set() rather
        // than silently handing back another material's set.
        VkDescriptorPoolSize pool_size{};
        pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_size.descriptorCount = kMaxMaterialSets * 2;

        VkDescriptorPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &pool_size;
        pool.maxSets = kMaxMaterialSets;
```

with `static constexpr uint32_t kMaxMaterialSets = 256;` beside `kMaxTextures`. 256 is four times `kMaxTextures`, which bounds pairs generously for any scene this engine can currently build; the demo uses fewer than ten.

- [ ] **Step 6: Allocate sets per pair, lazily**

Remove the per-texture `tex.set` allocation from `create_texture` (the texture keeps its image, view, and sampler). Add:

```cpp
    // One descriptor set per (base_color, metallic_roughness) pair, created on first
    // use and cached for the device's lifetime. Set ownership moved off the texture
    // table in PBR slice 3: a material needs two images visible at once, and binding
    // one set per draw is what keeps the per-draw cost unchanged.
    VkDescriptorSet material_set(std::uint32_t base_color, std::uint32_t mr)
    {
        const std::uint64_t key =
            (static_cast<std::uint64_t>(base_color) << 32) | static_cast<std::uint64_t>(mr);
        if (auto it = material_sets_.find(key); it != material_sets_.end())
        {
            return it->second;
        }
        if (material_sets_.size() >= kMaxMaterialSets)
        {
            // Loudly, once: silently reusing another material's set would render the
            // wrong texture on an arbitrary object and look like an asset mistake.
            mosaic::foundation::log::error(
                "render", "descriptor pool exhausted at {} material texture pairs; "
                          "raise kMaxMaterialSets", kMaxMaterialSets);
            return VK_NULL_HANDLE;
        }

        VkDescriptorSetAllocateInfo set_alloc{};
        set_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        set_alloc.descriptorPool = descriptor_pool_;
        set_alloc.descriptorSetCount = 1;
        set_alloc.pSetLayouts = &texture_set_layout_;
        VkDescriptorSet set = VK_NULL_HANDLE;
        vk_check(vkAllocateDescriptorSets(device_, &set_alloc, &set),
                 "vkAllocateDescriptorSets(material)");

        VkDescriptorImageInfo infos[2]{};
        infos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        infos[0].imageView = textures_[base_color].view;
        infos[0].sampler = textures_[base_color].sampler;
        infos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        infos[1].imageView = textures_[mr].view;
        infos[1].sampler = textures_[mr].sampler;

        VkWriteDescriptorSet writes[2]{};
        for (int i = 0; i < 2; ++i)
        {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set;
            writes[i].dstBinding = static_cast<std::uint32_t>(i);
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].descriptorCount = 1;
            writes[i].pImageInfo = &infos[i];
        }
        vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);
        material_sets_.emplace(key, set);
        return set;
    }
```

with `std::unordered_map<std::uint64_t, VkDescriptorSet> material_sets_;` as a member.

- [ ] **Step 7: Bind it in the draw path**

In `draw`, replace the set-0 bind. Resolve both handles to indices with the existing fallback rule, then:

```cpp
        const std::uint32_t base_index =
            (texture == 0 || texture > textures_.size()) ? 0u : texture - 1u;
        const std::uint32_t mr_index =
            (params.metallic_roughness == 0 || params.metallic_roughness > textures_.size())
                ? 0u
                : params.metallic_roughness - 1u;
        const VkDescriptorSet mat_set = material_set(base_index, mr_index);
        if (mat_set == VK_NULL_HANDLE) { return; }
        vkCmdBindDescriptorSets(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline_layout_, 0, 1, &mat_set, 0, nullptr);
```

leaving the shadow (set 1) and frame UBO (set 2) binds exactly as they are.

- [ ] **Step 8: Build, gate, and verify both entry points**

```bash
cmake --build --preset debug -j 20 2>&1 | grep -cE "error:"
ctest --preset debug --output-on-failure 2>&1 | tail -3
timeout 40 ./build/debug/app/mosaic run --window --duration 8; echo "RC=$?"
timeout 40 ./build/debug/app/mosaic run --window --script-thread --duration 8; echo "RC=$?"
```

Expected: `0` compile errors, 100% tests, both runs exit 0 with **zero Vulkan validation errors**. Validation is the real test here — a descriptor layout mismatch between the shader and the pipeline is reported by the validation layer and by nothing else in this plan.

- [ ] **Step 9: Commit**

```bash
git add modules/render_vulkan/vulkan_render_device.cpp \
        modules/render_vulkan/shaders/cube.frag \
        tests/unit/render/shader_specular_contract_test.cpp
git commit -m "feat(render): sample metallic/roughness per texel from a second binding"
```

---

### Task 5: The demo uses an authored map

**Files:**
- Create: `assets/textures/sphere_mr.png` (generated, committed)
- Modify: `app/run/demo_scene.hpp`
- Test: `tests/unit/app/demo_scene_test.cpp`

**Interfaces:**
- Consumes: `Material::metallic_roughness_texture` from Task 3; `read_image` from Task 1.

- [ ] **Step 1: Generate the demo map**

A roughness gradient banded across the sphere, so variation is unmistakable rather than subtle:

```bash
mkdir -p assets/textures && python3 - <<'PY'
import zlib, struct
W = H = 64
rows = []
for y in range(H):
    px = bytearray()
    for x in range(W):
        band = (x * 6) // W                 # six vertical bands
        rough = 20 + band * 40              # 20..220, mirror-ish to rough
        px += bytes([0, rough, 255, 255])   # R unused, G roughness, B metallic
    rows.append(b'\x00' + bytes(px))
def chunk(t, d):
    c = t + d
    return struct.pack('>I', len(d)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
png = (b'\x89PNG\r\n\x1a\n'
       + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 6, 0, 0, 0))
       + chunk(b'IDAT', zlib.compress(b''.join(rows)))
       + chunk(b'IEND', b''))
open('assets/textures/sphere_mr.png','wb').write(png)
print('wrote', len(png), 'bytes')
PY
```

Expected: `wrote <N> bytes`. Verify it decodes before relying on it:

```bash
python3 -c "
import zlib,struct
d=open('assets/textures/sphere_mr.png','rb').read()
print('signature ok:', d[:8]==b'\x89PNG\r\n\x1a\n', 'len', len(d))"
```

- [ ] **Step 2: Write the failing test**

Append to `tests/unit/app/demo_scene_test.cpp`:

```cpp
TEST_CASE("build_default_scene gives exactly one material a metallic/roughness map", "[app]")
{
    // The slice's success criterion: something in the demo must vary per texel, or
    // the feature ships with nothing exercising it visually -- the same reasoning that
    // made the sphere metallic in slice 2. One is deliberate: a second would not add
    // coverage, and the point is a human being able to see the variation.
    World world;
    build_default_scene(world);

    int mapped = 0;
    world.for_each<mosaic::engine::render::Material>(
        [&](World::EntityID, mosaic::engine::render::Material& m)
        {
            if (m.metallic_roughness_texture != mosaic::foundation::serialization::AssetID{})
            {
                ++mapped;
                // It must be the metal: a roughness gradient on a dielectric is far
                // less legible, since most of what you see there is diffuse.
                REQUIRE(m.metallic == Approx(1.0f));
            }
        });
    REQUIRE(mapped == 1);
}
```

- [ ] **Step 3: Run it to verify it fails**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20 2>&1 | grep -cE "error:"
./build/debug/tests/mosaic_unit_tests "build_default_scene gives exactly one material a metallic/roughness map" 2>&1 | grep -A4 FAILED
```

Expected: FAIL at `0 == 1`.

- [ ] **Step 4: Register the map and attach it to the sphere**

In `app/run/demo_scene.hpp`, beside the other texture ids:

```cpp
inline const mosaic::foundation::serialization::AssetID kSphereMrTextureId{
    std::array<std::uint8_t, 16>{0xD3}};
```

`0xD3` is unused: the existing ids are `{0xC0, 0xDE}`, `{0xA1}`, `{0xB2}` and the
font's `{0xF0}`. Add it directly below `kStripesTextureId`.

Register it from the committed PNG rather than procedurally — importing a real image file is half of what this slice delivers, so the demo must exercise that path:

```cpp
    // Loaded from a committed PNG, deliberately: every other texture here is a
    // procedural pixel loop, and a procedural shortcut would leave the import path
    // this slice adds untested by anything a human looks at.
    if (const std::optional<std::string> bytes = load_scene_file("assets/textures/sphere_mr.png"))
    {
        if (const std::optional<render::Image> img = mosaic::app::read_image(*bytes))
        {
            registry.register_resource<render::Image>(
                mosaic::engine::assets::AssetMetadata{kSphereMrTextureId, "texture"},
                std::make_shared<render::Image>(*img));
        }
    }
```

`load_scene_file` comes from `app/run/scene_io.hpp`, which `demo_scene.hpp` already includes (line 32); it returns `std::optional<std::string>` for a whole file, so no second reader is needed. Two includes must be added at the top of `demo_scene.hpp`, beside the existing `#include "app/asset/font_cook.hpp"`:

```cpp
#include "app/asset/image_cook.hpp"   // read_image for the sphere's authored MR map
#include <optional>
```

If the load fails the id stays unregistered, `TextureCache` returns 0, and the sphere falls back to the white default — a missing asset must not crash the demo.

Then on the sphere's `Material`, set `metallic_roughness_texture`:

```cpp
    render::Material sphere_mat{Vec3{0.75f, 0.18f, 0.18f}, {}, 1.0f, 0.174f};
    sphere_mat.metallic_roughness_texture = kSphereMrTextureId;
    world.add_component<render::Material>(sphere, sphere_mat);
```

Note the sphere is currently constructed with a brace-init expression inline; convert it to the named form above so the new field can be set without a positional initialiser whose order is easy to get wrong.

- [ ] **Step 5: Run it to verify it passes**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20 2>&1 | grep -cE "error:"
./build/debug/tests/mosaic_unit_tests "[app]" 2>&1 | tail -3
```

Expected: `0` errors, all `[app]` pass.

- [ ] **Step 6: Full gate and both entry points**

```bash
ctest --preset debug --output-on-failure 2>&1 | tail -3
timeout 40 ./build/debug/app/mosaic run --window --duration 8; echo "RC=$?"
timeout 40 ./build/debug/app/mosaic run --window --script-thread --duration 8; echo "RC=$?"
```

Expected: 100% tests, both exit 0, zero Vulkan validation errors.

- [ ] **Step 7: Record the visual check rather than performing it**

No test here can show the gradient reads as a gradient. The human partner has asked for the visual gate to be flagged rather than blocked on, so write to the ledger what they should look for and continue:

- The red sphere should show **six vertical bands** running from a tight, mirror-like highlight to a broad, soft one.
- The bands are **roughness only** — metallic is 255 everywhere — so every band stays red-tinted. A band that looks like a different *colour* rather than a different *finish* means G and B are crossed.
- Everything else in the scene should be **unchanged**, since every other material has a nil map and multiplies by white.

- [ ] **Step 8: Commit**

```bash
git add assets/textures/sphere_mr.png app/run/demo_scene.hpp tests/unit/app/demo_scene_test.cpp
git commit -m "feat(demo): the metal sphere gets an authored roughness map"
```
