# Rendering 36d-2 — Texture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Texture the lit cube — a `Material` references a texture by `AssetID`, resolved through the asset registry to a Vulkan sampled texture bound via a descriptor set (issue [#45](https://github.com/Evol-Luci/mosaic/issues/45), completes it).

**Architecture:** `foundation/math` gains `Vec2`; `engine/render` gains an `Image` asset resource, a `uv` on `Vertex`, `create_texture`/`draw(...,texture,...)`, a `Material.texture` AssetID, and a `TextureCache` (AssetID→TextureHandle via the registry). The Vulkan backend adds texture upload + a sampler + descriptor sets; the shader samples the texture. Demo uses a procedural checkerboard.

**Tech Stack:** C++20, Vulkan/GLFW, glslc, Catch2 v3.

## Global Constraints

- **Render boundary:** `Image`/`TextureHandle`/`Material`/`TextureCache`/`RenderSystem`/`Vertex` stay in `engine/render` (Vulkan/GLFW-free); `Vec2` in `foundation/math`. All texture/sampler/descriptor-set code is in `modules/render_vulkan/`.
- **Vertex:** `{ Vec3 position; Vec3 normal; Vec2 uv; }`. Cube UVs: each face's 4 corners → (0,0),(1,0),(1,1),(0,1).
- **Interface:** `using TextureHandle = std::uint32_t;` `create_texture(const Image&) -> TextureHandle` (empty image → 0); `draw(MeshHandle, TextureHandle, const DrawParams&)`.
- **Material:** `{ Vec3 base_color{0.8,0.8,0.8}; AssetID texture{}; }` (nil = untextured), reflected (base_color + texture).
- **TextureCache:** `get(AssetID, AssetRegistry&, RenderDevice&) -> TextureHandle`; nil/missing → 0; uploads at most once per id (cached).
- **RenderSystem:** `render(World&, RenderDevice&, AssetRegistry&)`; per material resolve texture via the cache; untextured/unresolved → a lazily-created **1×1 white default** texture (so a texture is always bound).
- **Shader:** `albedo = texture(tex, uv).rgb * base_color; out = albedo * (0.1 + lambert*light_color)`.
- **C++ standard:** `cxx_std_20`. **Test tags:** `[math]`, `[render]`, `[reflection]`.
- **No new third-party dep** (procedural checkerboard, no image decoder).

**Build & test commands:**
- Configure: `cmake --preset debug`
- Build: `cmake --build --preset debug`
- Unit tests: `./build/debug/tests/mosaic_unit_tests "[math],[render],[reflection]"`
- Full suite: `ctest --preset debug`
- Manual smoke (Tasks 3–4): `./build/debug/app/mosaic run --window`

---

### Task 1: foundation/math — Vec2

**Files:**
- Create: `foundation/math/vec2.hpp`
- Create: `tests/unit/math/vec2_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `struct mosaic::foundation::math::Vec2 { float x=0; float y=0; };` + `operator==` + `reflect`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/math/vec2_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "foundation/math/vec2.hpp"

using mosaic::foundation::math::Vec2;

TEST_CASE("Vec2 defaults to zero and compares", "[math]")
{
    Vec2 a;
    REQUIRE(a.x == 0.0f);
    REQUIRE(a.y == 0.0f);
    REQUIRE(Vec2{1.0f, 2.0f} == Vec2{1.0f, 2.0f});
    REQUIRE_FALSE(Vec2{1.0f, 2.0f} == Vec2{1.0f, 3.0f});
}
```

Add `unit/math/vec2_test.cpp` to `tests/CMakeLists.txt`.

- [ ] **Step 2: Run to verify it fails** — `cmake --preset debug && cmake --build --preset debug` → FAIL (vec2.hpp not found).

- [ ] **Step 3: Create the header** — `foundation/math/vec2.hpp`:

```cpp
#pragma once

namespace mosaic::foundation::math
{
struct Vec2
{
    float x = 0.0f;
    float y = 0.0f;
};

inline bool operator==(const Vec2& a, const Vec2& b) { return a.x == b.x && a.y == b.y; }
inline bool operator!=(const Vec2& a, const Vec2& b) { return !(a == b); }

template <class V>
void reflect(V& visitor, Vec2& v)
{
    visitor.member("x", v.x);
    visitor.member("y", v.y);
}
} // namespace mosaic::foundation::math
```

- [ ] **Step 4: Run** — `./build/debug/tests/mosaic_unit_tests "[math]"` → PASS.
- [ ] **Step 5: Full suite + commit**

```bash
git add foundation/math/vec2.hpp tests/CMakeLists.txt tests/unit/math/vec2_test.cpp
git commit -m "math: Vec2 (for texture UVs) (#45)"
```

---

### Task 2: engine/render — texture layer (Image, uv, Material.texture, TextureCache, interface)

The full engine-side texture layer + interface, unit-tested with a fake device + a real
`AssetRegistry`. Vulkan is synced minimally (create_texture returns a placeholder handle,
draw ignores the texture) — real GPU texturing is Task 3.

**Files:**
- Modify: `engine/render/render_device.hpp` (Vertex uv; TextureHandle; create_texture; draw+texture)
- Create: `engine/render/image.hpp`
- Create: `engine/render/texture_cache.hpp`
- Modify: `engine/render/material.hpp` (add texture AssetID)
- Modify: `engine/render/cube.hpp` (per-face UVs)
- Modify: `engine/render/render_system.hpp` (texture resolution + default white; render+registry)
- Modify: `modules/render_vulkan/vulkan_render_device.cpp` (create_texture placeholder; draw+texture sig; vertex attr uv @loc2)
- Modify: `app/main.cpp` (pass an AssetRegistry to render())
- Modify: `tests/unit/render/render_system_test.cpp` (fake device: create_texture + draw+texture; texture-resolution tests)
- Create: `tests/unit/render/texture_cache_test.cpp`
- Modify: `tests/unit/reflection/lighting_reflection_test.cpp` (Material.texture round-trips) — or add to it
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Vec2` (Task 1), `AssetRegistry`, `AssetID`, `DirectionalLight`/`Material`/`Camera`/`WorldTransform`.
- Produces:
  - `struct Image { std::uint32_t width, height; std::vector<std::uint8_t> pixels; };`
  - `using TextureHandle = std::uint32_t;` `create_texture(const Image&) -> TextureHandle`; `draw(MeshHandle, TextureHandle, const DrawParams&)`.
  - `struct Material { Vec3 base_color{0.8,0.8,0.8}; AssetID texture{}; };`
  - `class TextureCache { TextureHandle get(AssetID, AssetRegistry&, RenderDevice&); };`
  - `RenderSystem::render(World&, RenderDevice&, AssetRegistry&)`.
  - `make_cube_mesh` vertices gain uv.

- [ ] **Step 1: Update the interface (render_device.hpp)**

Add `#include "foundation/math/vec2.hpp"` and change `Vertex` + `RenderDevice`:

```cpp
struct Vertex
{
    mosaic::foundation::math::Vec3 position;
    mosaic::foundation::math::Vec3 normal;
    mosaic::foundation::math::Vec2 uv;
};

using MeshHandle = std::uint32_t;
using TextureHandle = std::uint32_t; // opaque; 0 == invalid/default

// (DrawParams unchanged from 36d-1)

class RenderDevice
{
public:
    virtual ~RenderDevice() = default;
    virtual void begin_frame() = 0;
    virtual void clear(const Color&) = 0;
    virtual void end_frame() = 0;
    virtual MeshHandle create_mesh(const std::vector<Vertex>&, const std::vector<std::uint32_t>&) = 0;
    virtual TextureHandle create_texture(const Image& image) = 0;
    virtual void draw(MeshHandle mesh, TextureHandle texture, const DrawParams& params) = 0;
};
```

`Image` is defined in `image.hpp`; forward-include it: add `#include "engine/render/image.hpp"` at the top of `render_device.hpp` (Image is a plain struct, no cycle).

- [ ] **Step 2: Write the failing tests**

Create `engine/render/image.hpp` first is needed to compile tests; but per TDD write tests
now (they fail until Steps 4–7).

Create `tests/unit/render/texture_cache_test.cpp`:

```cpp
#include <cstdint>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/assets/asset_registry.hpp"
#include "engine/render/image.hpp"
#include "engine/render/render_device.hpp"
#include "engine/render/texture_cache.hpp"
#include "foundation/serialization/reference.hpp"

using mosaic::engine::assets::AssetRegistry;
using mosaic::engine::assets::AssetMetadata;
using mosaic::engine::render::Color;
using mosaic::engine::render::DrawParams;
using mosaic::engine::render::Image;
using mosaic::engine::render::MeshHandle;
using mosaic::engine::render::RenderDevice;
using mosaic::engine::render::TextureCache;
using mosaic::engine::render::TextureHandle;
using mosaic::engine::render::Vertex;
using mosaic::foundation::serialization::AssetID;

namespace
{
class CountingDevice : public RenderDevice
{
public:
    int texture_uploads = 0;
    void begin_frame() override {}
    void clear(const Color&) override {}
    void end_frame() override {}
    MeshHandle create_mesh(const std::vector<Vertex>&, const std::vector<std::uint32_t>&) override { return 1; }
    TextureHandle create_texture(const Image& img) override
    {
        if (img.pixels.empty()) return 0;
        return static_cast<TextureHandle>(++texture_uploads);
    }
    void draw(MeshHandle, TextureHandle, const DrawParams&) override {}
};

AssetID make_id(std::uint8_t tag)
{
    std::array<std::uint8_t, 16> b{};
    b[0] = tag;
    return AssetID{b};
}

std::shared_ptr<Image> checker()
{
    auto img = std::make_shared<Image>();
    img->width = 1;
    img->height = 1;
    img->pixels = {255, 255, 255, 255};
    return img;
}
} // namespace

TEST_CASE("TextureCache uploads once and caches by AssetID", "[render]")
{
    AssetRegistry registry;
    const AssetID id = make_id(1);
    registry.register_resource<Image>(AssetMetadata{id, "texture"}, checker());

    CountingDevice device;
    TextureCache cache;

    const TextureHandle h1 = cache.get(id, registry, device);
    const TextureHandle h2 = cache.get(id, registry, device);
    REQUIRE(h1 != 0);
    REQUIRE(h2 == h1);
    REQUIRE(device.texture_uploads == 1); // uploaded once, then cached
}

TEST_CASE("TextureCache returns 0 for a nil or unregistered id", "[render]")
{
    AssetRegistry registry;
    CountingDevice device;
    TextureCache cache;
    REQUIRE(cache.get(AssetID{}, registry, device) == 0);        // nil
    REQUIRE(cache.get(make_id(9), registry, device) == 0);       // unregistered
    REQUIRE(device.texture_uploads == 0);
}
```

> Note: `AssetMetadata`'s aggregate initializer must match its actual field order — open
> `engine/assets/asset_metadata.hpp` and use the correct braces (fields are roughly
> `{uuid, type, version, source, dependencies, metadata/import_settings}`; adapt the brace
> list to the real struct; only `uuid` and `type` matter here).

Extend the RenderSystem test (`tests/unit/render/render_system_test.cpp`) fake device to the
new interface (`create_texture` + `draw(mesh, texture, params)` recording the texture), and
add a test: a mesh whose `Material.texture` is a registered `AssetID` records a draw whose
`TextureHandle` is the cache's handle; an untextured material records a non-zero default
handle. (Update `rs.render(world, device)` calls to `rs.render(world, device, registry)`.)

Add a `Material.texture` round-trip assertion to `tests/unit/reflection/lighting_reflection_test.cpp`
(set a non-nil `AssetID`, save/load, assert preserved).

Add the new test source to `tests/CMakeLists.txt`.

- [ ] **Step 3: Run to verify it fails** — `cmake --build --preset debug` → FAIL (image.hpp/texture_cache.hpp missing; interface/fake mismatch).

- [ ] **Step 4: Create image.hpp**

```cpp
#pragma once
#include <cstdint>
#include <vector>
namespace mosaic::engine::render
{
// CPU RGBA8 image (row-major, 4 bytes/pixel). The asset resource a texture AssetID
// resolves to; uploaded by RenderDevice::create_texture.
struct Image
{
    std::uint32_t             width = 0;
    std::uint32_t             height = 0;
    std::vector<std::uint8_t> pixels; // width*height*4
};
} // namespace mosaic::engine::render
```

- [ ] **Step 5: Material gains texture** (`engine/render/material.hpp`):

```cpp
#pragma once
#include "foundation/math/vec3.hpp"
#include "foundation/serialization/reference.hpp"
namespace mosaic::engine::render
{
struct Material
{
    mosaic::foundation::math::Vec3          base_color{0.8f, 0.8f, 0.8f};
    mosaic::foundation::serialization::AssetID texture{}; // nil = untextured
};
template <class V> void reflect(V& v, Material& m)
{
    v.member("base_color", m.base_color);
    v.member("texture", m.texture);
}
} // namespace mosaic::engine::render
```

- [ ] **Step 6: TextureCache** (`engine/render/texture_cache.hpp`):

```cpp
#pragma once
#include <unordered_map>
#include "engine/assets/asset_registry.hpp"
#include "engine/render/image.hpp"
#include "engine/render/render_device.hpp"
#include "foundation/serialization/reference.hpp"
namespace mosaic::engine::render
{
// Resolves a texture AssetID to a device TextureHandle, uploading via the registry's
// Image at most once per id. Nil/missing/failed -> 0.
class TextureCache
{
public:
    TextureHandle get(mosaic::foundation::serialization::AssetID id,
                      mosaic::engine::assets::AssetRegistry& registry, RenderDevice& device)
    {
        if (id == mosaic::foundation::serialization::AssetID{})
        {
            return 0;
        }
        auto it = cache_.find(id);
        if (it != cache_.end())
        {
            return it->second;
        }
        TextureHandle handle = 0;
        if (std::shared_ptr<Image> img = registry.load<Image>(id))
        {
            handle = device.create_texture(*img);
        }
        cache_.emplace(id, handle);
        return handle;
    }
private:
    std::unordered_map<mosaic::foundation::serialization::AssetID, TextureHandle> cache_;
};
} // namespace mosaic::engine::render
```

(Verify `AssetRegistry`'s namespace/`load<T>` signature against `engine/assets/asset_registry.hpp`
and adapt the `using`/calls if they differ.)

- [ ] **Step 7: Cube UVs** — in `engine/render/cube.hpp`, add `Vec2` UVs to each vertex. Each
  face's four corners get `(0,0),(1,0),(1,1),(0,1)` in the same corner order as the positions.
  Add `using mosaic::foundation::math::Vec2;` and give every vertex a third field, e.g. the +X
  face becomes:

```cpp
        {{h, -h, -h}, px, {0, 0}}, {{h, -h, h}, px, {1, 0}}, {{h, h, h}, px, {1, 1}}, {{h, h, -h}, px, {0, 1}},
```

  Apply the same `{0,0},{1,0},{1,1},{0,1}` to all six faces (matching each face's existing
  4-corner order).

- [ ] **Step 8: RenderSystem texture resolution** — in `engine/render/render_system.hpp`:
  add `#include "engine/assets/asset_registry.hpp"` and `#include "engine/render/texture_cache.hpp"`;
  change `render` to `render(World& world, RenderDevice& device, mosaic::engine::assets::AssetRegistry& registry)`;
  add members `mutable TextureCache texture_cache_;` and `mutable TextureHandle default_white_ = 0;`.
  In the mesh loop, after computing `base_color`, resolve the texture:

```cpp
                TextureHandle texture = 0;
                if (const Material* mat = world.get_component<Material>(e))
                {
                    // (base_color already read from mat above)
                    texture = texture_cache_.get(mat->texture, registry, device);
                }
                if (texture == 0)
                {
                    texture = default_white(device);
                }
                device.draw(mr.mesh, texture, DrawParams{projection * view * wt->matrix,
                                                         light_obj, light.color, base_color});
```

  and a helper:

```cpp
    TextureHandle default_white(RenderDevice& device) const
    {
        if (default_white_ == 0)
        {
            Image white;
            white.width = 1;
            white.height = 1;
            white.pixels = {255, 255, 255, 255};
            default_white_ = device.create_texture(white);
        }
        return default_white_;
    }
```

- [ ] **Step 9: Vulkan minimal sync** — in `modules/render_vulkan/vulkan_render_device.cpp`:
  add a placeholder `create_texture` and update `draw`'s signature (ignore the texture for
  now — real texturing is Task 3), and add the uv vertex attribute:

```cpp
    // Task 3 replaces this with a real upload + descriptor set.
    mosaic::engine::render::TextureHandle
    create_texture(const mosaic::engine::render::Image& image) override
    {
        return image.pixels.empty() ? 0u : 1u; // placeholder handle; not yet uploaded
    }

    void draw(MeshHandle mesh, mosaic::engine::render::TextureHandle /*texture*/,
              const mosaic::engine::render::DrawParams& params) override
    {
        // (body identical to the 36d-1 draw: guard, bind vbuf/ibuf, push PushConstants, drawIndexed)
    }
```

  And add the uv attribute (change the attribute array size to 3 and add loc2):

```cpp
        std::array<VkVertexInputAttributeDescription, 3> attrs{};
        // attrs[0] position (loc0), attrs[1] normal (loc1) unchanged
        attrs[2].location = 2;
        attrs[2].binding = 0;
        attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[2].offset = static_cast<uint32_t>(offsetof(Vertex, uv));
```

  (Update `vertexAttributeDescriptionCount` to `attrs.size()`.)

- [ ] **Step 10: main.cpp — pass a registry** — in `app/main.cpp`, in the windowed block,
  create `mosaic::engine::assets::AssetRegistry asset_registry;` (add the include) and change the
  `on_frame` render call to `render_system->render(world, *device, asset_registry)`. (Task 4 puts
  a checkerboard in this registry + sets `Material.texture`.)

- [ ] **Step 11: Build + tests + full suite** — `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[render],[reflection]"` then `ctest --preset debug`. Expected: green (windowed run = lit-untextured cube, since create_texture is a placeholder and draw ignores the texture).

- [ ] **Step 12: Commit**

```bash
git add engine/render/render_device.hpp engine/render/image.hpp engine/render/texture_cache.hpp engine/render/material.hpp engine/render/cube.hpp engine/render/render_system.hpp modules/render_vulkan/vulkan_render_device.cpp app/main.cpp tests/unit/render/render_system_test.cpp tests/unit/render/texture_cache_test.cpp tests/unit/reflection/lighting_reflection_test.cpp tests/CMakeLists.txt
git commit -m "render 36d-2: Image, Vertex uv, Material.texture, TextureCache, texture interface (#45)"
```

---

### Task 3: Vulkan texture — upload, sampler, descriptor sets, sampling shader

Make the backend actually texture. **GPU code, no unit tests** — build + smoke. Detailed spec
(the descriptor-set machinery), like the earlier GPU tasks.

**Files:**
- Modify: `modules/render_vulkan/vulkan_render_device.cpp`
- Modify: `modules/render_vulkan/shaders/cube.vert`, `cube.frag`

**Interfaces:**
- Consumes: `Image`, `TextureHandle`, `draw(mesh, texture, params)` (Task 2).
- Produces: real `create_texture` + texture binding (no public API change).

- [ ] **Step 1: Descriptor infrastructure (init)** — add members and create at device init
  (after the render pass / before the pipeline):
  - `VkDescriptorSetLayout texture_set_layout_` — one binding: `{binding=0, type=COMBINED_IMAGE_SAMPLER,
    count=1, stageFlags=FRAGMENT}`.
  - `VkDescriptorPool descriptor_pool_` — `poolSizes={{COMBINED_IMAGE_SAMPLER, 64}}`, `maxSets=64`.
  - A `struct Texture { VkImage image; VkDeviceMemory memory; VkImageView view; VkSampler sampler;
    VkDescriptorSet set; };` and `std::vector<Texture> textures_`.
  - **Pipeline layout** gains the set layout: `setLayoutCount=1, pSetLayouts=&texture_set_layout_`
    (keep the existing push-constant range).

- [ ] **Step 2: Real `create_texture`** — replace the Task-2 placeholder:
  - Return 0 if `image.pixels.empty()`.
  - Staging buffer (`HOST_VISIBLE|COHERENT`, size = pixels.size()); map + memcpy + unmap.
  - `VkImage` (`VK_FORMAT_R8G8B8A8_SRGB`, extent {width,height,1}, `usage=TRANSFER_DST|SAMPLED`,
    `tiling=OPTIMAL`, `initialLayout=UNDEFINED`); device-local memory (reuse `find_memory_type`); bind.
  - One-time-submit (reuse `begin_single_time_commands`/`end_single_time_commands` from 36b):
    barrier `UNDEFINED→TRANSFER_DST_OPTIMAL`; `vkCmdCopyBufferToImage`; barrier
    `TRANSFER_DST_OPTIMAL→SHADER_READ_ONLY_OPTIMAL` (src/dst stages TRANSFER→FRAGMENT_SHADER).
  - Destroy the staging buffer after the submit completes.
  - `VkImageView` (2D, R8G8B8A8_SRGB, COLOR aspect). `VkSampler` (`magFilter=LINEAR`,
    `minFilter=LINEAR`, `addressMode*=REPEAT`, no anisotropy).
  - Allocate a `VkDescriptorSet` from `descriptor_pool_` with `texture_set_layout_`; update it:
    `VkDescriptorImageInfo{sampler, view, SHADER_READ_ONLY_OPTIMAL}`, `VkWriteDescriptorSet`
    (binding 0, COMBINED_IMAGE_SAMPLER). Push `Texture{...}` to `textures_`; return `textures_.size()`.

- [ ] **Step 3: `draw` binds the texture set** — before the push/draw, bind the set (guard the
  handle; the default white always exists so a valid set is available):

```cpp
    void draw(MeshHandle mesh, mosaic::engine::render::TextureHandle texture,
              const mosaic::engine::render::DrawParams& params) override
    {
        if (frame_skipped_ || mesh == 0 || mesh > meshes_.size()) return;
        const std::uint32_t tex_index = (texture == 0 || texture > textures_.size()) ? 0 : texture - 1;
        // If there are no textures at all, skip binding (shouldn't happen: default white is created).
        if (!textures_.empty())
        {
            vkCmdBindDescriptorSets(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_,
                                    0, 1, &textures_[tex_index].set, 0, nullptr);
        }
        // ... existing bind vbuf/ibuf, push PushConstants (VERTEX|FRAGMENT), vkCmdDrawIndexed ...
    }
```

- [ ] **Step 4: Shaders** — `cube.vert` add `layout(location=2) in vec2 inUV; layout(location=1)
  out vec2 fragUV;` and `fragUV = inUV;` (keep `fragNormal` at location 0). `cube.frag`:

```glsl
#version 450
layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;
layout(set = 0, binding = 0) uniform sampler2D tex;
layout(push_constant) uniform Push { mat4 mvp; vec4 light_direction; vec4 light_color; vec4 base_color; } pc;
layout(location = 0) out vec4 outColor;
void main()
{
    vec3 n = normalize(fragNormal);
    vec3 l = normalize(pc.light_direction.xyz);
    float lambert = max(0.0, dot(n, -l));
    vec3 albedo = texture(tex, fragUV).rgb * pc.base_color.xyz;
    outColor = vec4(albedo * (0.1 + lambert * pc.light_color.xyz), 1.0);
}
```

- [ ] **Step 5: Teardown** — in the dtor, before destroying the logical device, destroy each
  `Texture` (sampler, view, image, free memory), then `descriptor_pool_`, then
  `texture_set_layout_`. (Descriptor sets are freed with the pool.)

- [ ] **Step 6: Build (no unit tests) + full suite** — `cmake --preset debug && cmake --build
  --preset debug && ctest --preset debug`. Expected: warning-clean, glslc recompiles both
  shaders, full suite green (unchanged count; GPU untested).

- [ ] **Step 7: Commit**

```bash
git add modules/render_vulkan/vulkan_render_device.cpp modules/render_vulkan/shaders/cube.vert modules/render_vulkan/shaders/cube.frag
git commit -m "render 36d-2: Vulkan texture upload + sampler + descriptor sets + sampling shader (#45)"
```

---

### Task 4: Demo — checkerboard texture via the asset registry

**Files:**
- Modify: `app/main.cpp`

- [ ] **Step 1: Generate + register a checkerboard, texture the cube** — in the windowed block,
  build a procedural checkerboard `Image` and register it in the `asset_registry` from Task 2,
  then point the cube's `Material.texture` at its `AssetID`:

```cpp
        // Procedural checkerboard texture (64x64, two shades), registered as an asset.
        auto checker = std::make_shared<render::Image>();
        checker->width = 64; checker->height = 64;
        checker->pixels.resize(64 * 64 * 4);
        for (std::uint32_t y = 0; y < 64; ++y)
            for (std::uint32_t x = 0; x < 64; ++x)
            {
                const bool on = ((x / 8) + (y / 8)) % 2 == 0;
                const std::uint8_t c = on ? 230 : 60;
                std::uint8_t* p = &checker->pixels[(y * 64 + x) * 4];
                p[0] = c; p[1] = c; p[2] = c; p[3] = 255;
            }
        std::array<std::uint8_t, 16> tex_uuid{}; tex_uuid[0] = 0xC0; tex_uuid[1] = 0xDE;
        const mosaic::foundation::serialization::AssetID tex_id{tex_uuid};
        asset_registry.register_resource<render::Image>(
            mosaic::engine::assets::AssetMetadata{tex_id, "texture"}, checker);
```

  and give the cube's `Material` the texture id (replace the plain `Material{...}` add from 36d-1):

```cpp
        world.add_component<render::Material>(cube_entity,
            render::Material{Vec3{1.0F, 1.0F, 1.0F}, tex_id}); // white tint so the texture shows true colors
```

  (Adapt the `AssetMetadata{...}` brace list to the real struct field order — see
  `engine/assets/asset_metadata.hpp`.)

- [ ] **Step 2: Build + full suite** — `cmake --build --preset debug && ctest --preset debug` → green.

- [ ] **Step 3: Manual smoke (display + GPU)** — `./build/debug/app/mosaic run --window` →
  the spinning cube shows a **checkerboard texture**, lit by the directional light (checker
  darker on faces away from the light); resizing keeps rendering; closing exits 0. Headless
  `./build/debug/app/mosaic run --ticks 5` → exit 0.

- [ ] **Step 4: Commit**

```bash
git add app/main.cpp
git commit -m "render 36d-2: windowed demo textures the cube with a checkerboard asset (#45)"
```

---

## Self-Review

**Spec coverage:**

| Spec item | Task |
| --- | --- |
| `Vec2` | Task 1 |
| `Vertex` uv + cube UVs | Task 2 |
| `Image` asset resource | Task 2 |
| `create_texture` + `draw`+texture interface | Task 2 (iface) + Task 3 (GPU) |
| `Material.texture` AssetID (reflected) | Task 2 |
| `TextureCache` (cache once; nil/missing→0) | Task 2 |
| `RenderSystem` texture resolution + default white + registry | Task 2 |
| Vulkan upload + sampler + descriptor sets + sampling shader | Task 3 |
| Demo checkerboard via asset registry | Task 4 |
| TextureCache + cube UVs + Material round-trip unit-tested | Task 2 |
| No new third-party dep | Tasks 1–4 (procedural) |

No spec item is unmapped.

**Placeholder scan:** Tasks 1, 2, 4 have complete code. Task 3 is a detailed GPU spec (every
Vulkan object + the upload/barrier/descriptor steps), matching the earlier GPU-task style —
un-unit-testable code written against the Vulkan spec; the non-obvious parts (layout
transitions, descriptor set update, binding) are concrete. The two `AssetMetadata{...}` brace
lists are flagged to be adapted to the real struct.

**Type consistency:** `Image{width,height,pixels}`, `TextureHandle`, `create_texture(const Image&)`,
`draw(MeshHandle, TextureHandle, const DrawParams&)`, `Material{base_color, texture}`,
`TextureCache::get(AssetID, AssetRegistry&, RenderDevice&)`, `RenderSystem::render(World&,
RenderDevice&, AssetRegistry&)`, and `default_white` are named consistently across tasks/tests.
The Task-2 placeholder `create_texture`/texture-ignoring `draw` share the exact signatures the
Task-3 real versions use. Namespaces: `mosaic::engine::render`, `mosaic::engine::assets`,
`mosaic::foundation::{math,serialization}`.
