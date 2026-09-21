# Rendering — Shadows v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add real-time shadowing for the single directional light: a depth-only shadow map rendered from the light's point of view, sampled during the existing forward shading pass, hard-edged (no PCF), fixed-size orthographic frustum.

**Architecture:** Extend the existing direct-submission renderer with one more fixed pass (shadow depth → existing scene pass), following the exact `RenderDevice`-hook pattern every prior rendering slice used (default-no-op virtuals, only the Vulkan backend implements them). No `RenderGraph`, no new ECS component — shadow casting is implicit whenever a `DirectionalLight` exists.

**Tech Stack:** C++20, Vulkan 1.x (via `modules/render_vulkan`), GLSL → SPIR-V (glslc), Catch2.

**Spec:** `docs/superpowers/specs/2026-09-21-render-shadows-v1-design.md`

## Global Constraints

- Directional-light-only shadows (no point-light shadows in v1).
- Hard-edged shadows (single depth sample, no PCF).
- Fixed-size orthographic shadow frustum, centered on the origin (no scene/camera fitting, no cascades).
- Extend direct submission (fixed pass order, one command buffer, one submit) — do not build a `RenderGraph`.
- Shadow map resolution is a fixed implementation constant (not configurable in v1).
- `DrawParams` push-constant growth (144 → 208 bytes) must be guarded by the same device-limit `static_assert`/runtime-check pattern the point-light slice established.
- A fragment outside the fixed shadow frustum must degrade to "not shadowed" (fully lit), never error.

---

### Task 1: `Mat4::orthographic`

**Files:**
- Modify: `foundation/math/mat4.hpp` (declare + implement `orthographic`)
- Test: `tests/unit/math/projection_test.cpp`

**Interfaces:**
- Consumes: nothing new (uses existing `Mat4` struct, column-major `m[16]` layout).
- Produces: `Mat4::orthographic(float left, float right, float bottom, float top, float near_z, float far_z) -> Mat4`, consumed by Task 2.

- [ ] **Step 1: Write the failing test**

Add to `tests/unit/math/projection_test.cpp` (same file as the existing `perspective`/`look_at` tests):

```cpp
TEST_CASE("orthographic has the expected column-major entries (RH, Z in [0,1])", "[math]")
{
    // A symmetric 10x10 box, near 1, far 21.
    Mat4 o = Mat4::orthographic(-5.0f, 5.0f, -5.0f, 5.0f, 1.0f, 21.0f);
    REQUIRE(o.m[0] == Approx(2.0f / 10.0f));                 // 2/(right-left)
    REQUIRE(o.m[5] == Approx(2.0f / 10.0f));                 // 2/(top-bottom)
    REQUIRE(o.m[10] == Approx(1.0f / (1.0f - 21.0f)));       // 1/(near-far)
    REQUIRE(o.m[12] == Approx(0.0f));                        // -(right+left)/(right-left), symmetric => 0
    REQUIRE(o.m[13] == Approx(0.0f));                        // -(top+bottom)/(top-bottom), symmetric => 0
    REQUIRE(o.m[14] == Approx(-1.0f / (21.0f - 1.0f)));      // -near/(far-near)
    REQUIRE(o.m[15] == Approx(1.0f));                        // affine (no perspective divide)
}

TEST_CASE("orthographic maps the near-plane origin ray to clip z=0 and far to z=1", "[math]")
{
    Mat4 o = Mat4::orthographic(-1.0f, 1.0f, -1.0f, 1.0f, 1.0f, 11.0f);
    // A point at view-space z=-1 (near plane, RH looks down -Z) maps to clip z=0.
    Vec3 near_pt = mosaic::foundation::math::transform_point(o, Vec3{0.0f, 0.0f, -1.0f});
    REQUIRE(near_pt.z == Approx(0.0f).margin(1e-5));
    // A point at view-space z=-11 (far plane) maps to clip z=1.
    Vec3 far_pt = mosaic::foundation::math::transform_point(o, Vec3{0.0f, 0.0f, -11.0f});
    REQUIRE(far_pt.z == Approx(1.0f).margin(1e-5));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target mosaic_unit_tests 2>&1 | tail -30`
Expected: compile error — `Mat4::orthographic` is not a member of `Mat4`.

- [ ] **Step 3: Declare and implement `orthographic`**

In `foundation/math/mat4.hpp`, add the declaration next to the existing ones:

```cpp
    static Mat4 perspective(float fovy_radians, float aspect, float near_z, float far_z);
    static Mat4 look_at(const Vec3& eye, const Vec3& center, const Vec3& up);
    static Mat4 orthographic(float left, float right, float bottom, float top, float near_z, float far_z);
    static Mat4 inverse_affine(const Mat4& m);
```

Add the implementation directly after `Mat4::perspective`'s definition:

```cpp
inline Mat4 Mat4::orthographic(float left, float right, float bottom, float top,
                               float near_z, float far_z)
{
    Mat4 out = identity();
    out.m[0] = 2.0f / (right - left);
    out.m[5] = 2.0f / (top - bottom);
    out.m[10] = 1.0f / (near_z - far_z);
    out.m[12] = -(right + left) / (right - left);
    out.m[13] = -(top + bottom) / (top - bottom);
    out.m[14] = -near_z / (far_z - near_z);
    return out;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target mosaic_unit_tests && ./build/tests/mosaic_unit_tests "[math]"`
Expected: all `[math]`-tagged tests pass, including the two new ones.

- [ ] **Step 5: Commit**

```bash
git add foundation/math/mat4.hpp tests/unit/math/projection_test.cpp
git commit -m "feat(math): add Mat4::orthographic

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
```

---

### Task 2: Directional-light shadow view-projection helper

**Files:**
- Create: `engine/render/shadow.hpp`
- Test: `tests/unit/render/shadow_test.cpp`

**Interfaces:**
- Consumes: `Mat4::orthographic`, `Mat4::look_at` (Task 1), `mosaic::engine::render::DirectionalLight` (existing).
- Produces: `mosaic::engine::render::directional_light_view_projection(const DirectionalLight&) -> Mat4`, consumed by Task 3.

**Design note (from the spec):** the light's view matrix looks along `direction` from a point pulled back along `-direction` from the origin by the frustum's own half-extent (so the fixed volume always sits in front of the near plane, deterministically — never scene-dependent).

- [ ] **Step 1: Write the failing test**

Create `tests/unit/render/shadow_test.cpp`:

```cpp
#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/render/shadow.hpp"

using Catch::Approx;
using mosaic::engine::render::DirectionalLight;
using mosaic::engine::render::directional_light_view_projection;
using mosaic::foundation::math::Mat4;
using mosaic::foundation::math::Vec3;

TEST_CASE("directional_light_view_projection maps the origin into the shadow frustum's clip volume", "[render]")
{
    DirectionalLight light{Vec3{0.0f, -1.0f, 0.0f}, Vec3{1, 1, 1}}; // straight down
    Mat4 lvp = directional_light_view_projection(light);
    Vec3 origin_clip = mosaic::foundation::math::transform_point(lvp, Vec3{0.0f, 0.0f, 0.0f});
    // The world origin is inside the fixed frustum by construction -> clip xyz in [-1,1]x[-1,1]x[0,1].
    REQUIRE(origin_clip.x >= -1.0f); REQUIRE(origin_clip.x <= 1.0f);
    REQUIRE(origin_clip.y >= -1.0f); REQUIRE(origin_clip.y <= 1.0f);
    REQUIRE(origin_clip.z >= 0.0f);  REQUIRE(origin_clip.z <= 1.0f);
}

TEST_CASE("directional_light_view_projection changes with light direction", "[render]")
{
    Mat4 down = directional_light_view_projection(DirectionalLight{Vec3{0, -1, 0}, Vec3{1, 1, 1}});
    Mat4 sideways = directional_light_view_projection(DirectionalLight{Vec3{1, -1, 0}, Vec3{1, 1, 1}});
    bool any_different = false;
    for (int i = 0; i < 16; ++i)
    {
        if (std::abs(down.m[i] - sideways.m[i]) > 1e-4f) { any_different = true; break; }
    }
    REQUIRE(any_different);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target mosaic_unit_tests 2>&1 | tail -30`
Expected: compile error — `engine/render/shadow.hpp` does not exist.

- [ ] **Step 3: Implement the helper**

Create `engine/render/shadow.hpp`:

```cpp
#pragma once

#include <cmath>

#include "engine/render/directional_light.hpp"
#include "foundation/math/mat4.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::engine::render
{

// Fixed-size shadow frustum, v1: a box generously sized for the current demo/test
// scenes, centered on the origin. Not scene/camera-fitted -- see the design doc's
// Non-goals (docs/superpowers/specs/2026-09-21-render-shadows-v1-design.md) and
// docs/project/deferred-work-backlog.md's Rendering section for the deferred
// scene-fitted/cascaded follow-up.
constexpr float kShadowFrustumHalfExtent = 15.0f;
constexpr float kShadowNearZ = 0.1f;
constexpr float kShadowFarZ = 2.0f * kShadowFrustumHalfExtent + kShadowNearZ;

// The light's combined view-projection matrix for the fixed v1 shadow frustum.
// `light_position` is pulled back along -direction from the origin by the
// frustum's own half-extent (plus the near-plane offset), so the fixed volume
// always sits entirely in front of the near plane -- a deterministic function of
// direction alone, never scene-dependent.
inline mosaic::foundation::math::Mat4
directional_light_view_projection(const DirectionalLight& light)
{
    using mosaic::foundation::math::Mat4;
    using mosaic::foundation::math::Vec3;
    using mosaic::foundation::math::normalize;

    const Vec3 dir = normalize(light.direction);
    const Vec3 light_position = Vec3{0.0f, 0.0f, 0.0f} -
                                dir * (kShadowFrustumHalfExtent + kShadowNearZ);
    // An up vector not parallel to `dir` -- fall back to world +X if `dir` is
    // (near-)vertical, mirroring the degenerate-up handling every other look_at
    // call site in this codebase already assumes callers avoid.
    Vec3 up{0.0f, 1.0f, 0.0f};
    if (std::abs(mosaic::foundation::math::dot(dir, up)) > 0.999f)
    {
        up = Vec3{1.0f, 0.0f, 0.0f};
    }
    const Mat4 view = Mat4::look_at(light_position, light_position + dir, up);
    const Mat4 proj = Mat4::orthographic(
        -kShadowFrustumHalfExtent, kShadowFrustumHalfExtent,
        -kShadowFrustumHalfExtent, kShadowFrustumHalfExtent,
        kShadowNearZ, kShadowFarZ);
    return proj * view;
}

} // namespace mosaic::engine::render
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target mosaic_unit_tests && ./build/tests/mosaic_unit_tests "[render]" -c "directional_light_view_projection*"`

If Catch2's `-c` section filter doesn't match test-case names, instead run:
`./build/tests/mosaic_unit_tests "directional_light_view_projection maps the origin into the shadow frustum's clip volume" "directional_light_view_projection changes with light direction"`

Expected: both new tests pass.

- [ ] **Step 5: Commit**

```bash
git add engine/render/shadow.hpp tests/unit/render/shadow_test.cpp
git commit -m "feat(render): add directional_light_view_projection (fixed shadow frustum)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
```

---

### Task 3: `RenderDevice` shadow hooks + `RenderSystem` wiring

**Files:**
- Modify: `engine/render/render_device.hpp` (`DrawParams::light_mvp`, three new default-no-op virtuals)
- Modify: `engine/render/render_system.hpp` (shadow-pass loop before the main loop)
- Modify: `tests/unit/render/render_device_hooks_test.cpp` (default-no-op test)
- Modify: `tests/unit/render/render_system_test.cpp` (shadow-pass wiring test)

**Interfaces:**
- Consumes: `directional_light_view_projection` (Task 2).
- Produces: `RenderDevice::begin_shadow_pass()`, `RenderDevice::draw_shadow(MeshHandle, const Mat4&)`, `RenderDevice::end_shadow_pass()`, `DrawParams::light_mvp` — all consumed by Task 4 (the Vulkan backend).

- [ ] **Step 1: Write the failing tests**

In `tests/unit/render/render_device_hooks_test.cpp`, add (after the existing viewport-hooks test):

```cpp
TEST_CASE("shadow hooks default to no-op", "[render][shadow]")
{
    MinimalDevice device;
    const mosaic::foundation::math::Mat4 mat = mosaic::foundation::math::Mat4::identity();
    device.begin_shadow_pass();
    device.draw_shadow(1, mat);
    device.end_shadow_pass();
    // No assertion beyond "must not throw" -- these are pure no-ops on a base device.
}
```

Add the needed include at the top of that file: `#include "foundation/math/mat4.hpp"`.

In `tests/unit/render/render_system_test.cpp`, extend `FakeRenderDevice` (in the anonymous namespace near the top) with shadow-call recording:

```cpp
    struct ShadowDrawCall { MeshHandle mesh; Mat4 light_mvp; };
    std::vector<ShadowDrawCall> shadow_draws;
    int shadow_pass_begins = 0;
    int shadow_pass_ends = 0;
    void begin_shadow_pass() override { ++shadow_pass_begins; }
    void draw_shadow(MeshHandle m, const Mat4& light_mvp) override
    {
        shadow_draws.push_back({m, light_mvp});
    }
    void end_shadow_pass() override { ++shadow_pass_ends; }
```

(Add this member block directly after the existing `draw()` override, inside `FakeRenderDevice`.)

Then add a new test case after `"RenderSystem produces lit DrawParams from Material + DirectionalLight"`:

```cpp
TEST_CASE("RenderSystem runs a shadow pass before the main draw, with the right light_mvp", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);

    add_camera(world, Vec3{0, 0, 4});

    auto light = world.create_entity();
    world.add_component<DirectionalLight>(light, DirectionalLight{Vec3{0, -1, 0}, Vec3{1, 1, 1}});

    const Mat4 mesh_world = Mat4::translation(Vec3{1.0f, 0.0f, 0.0f});
    const AssetID mesh_id = register_cube(registry);
    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{mesh_world});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id});

    rs.render(world, device, registry);

    REQUIRE(device.shadow_pass_begins == 1);
    REQUIRE(device.shadow_pass_ends == 1);
    REQUIRE(device.shadow_draws.size() == 1);
    REQUIRE(device.shadow_draws[0].mesh == device.draws[0].mesh);

    // light_mvp == light_view_proj * mesh_world; light_mvp in DrawParams (main pass)
    // must match the shadow pass's own light_mvp exactly -- same light, same mesh.
    const Mat4 light_vp = mosaic::engine::render::directional_light_view_projection(
        DirectionalLight{Vec3{0, -1, 0}, Vec3{1, 1, 1}});
    const Mat4 expected = light_vp * mesh_world;
    for (int i = 0; i < 16; ++i)
    {
        REQUIRE(device.shadow_draws[0].light_mvp.m[i] == Approx(expected.m[i]).margin(1e-4));
        REQUIRE(device.draws[0].params.light_mvp.m[i] == Approx(expected.m[i]).margin(1e-4));
    }
}
```

Add `#include "engine/render/shadow.hpp"` to this test file's includes if not already present via `render_system.hpp`.

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --target mosaic_unit_tests 2>&1 | tail -40`
Expected: compile errors — `begin_shadow_pass`/`draw_shadow`/`end_shadow_pass` are not virtual members of `RenderDevice`, `DrawParams` has no `light_mvp` member.

- [ ] **Step 3: Add the `RenderDevice` hooks and `DrawParams::light_mvp`**

In `engine/render/render_device.hpp`, add `light_mvp` to `DrawParams` (after `point_range`):

```cpp
struct DrawParams
{
    mosaic::foundation::math::Mat4 mvp;
    mosaic::foundation::math::Vec3 light_direction;
    mosaic::foundation::math::Vec3 light_color;
    mosaic::foundation::math::Vec3 base_color;
    mosaic::foundation::math::Vec3 point_position;
    mosaic::foundation::math::Vec3 point_color;
    float                          point_range = 0.0f; // 0 == no point light (disabled)
    mosaic::foundation::math::Mat4 light_mvp; // light_view_proj * model, for shadow sampling
};
```

Add the three shadow hooks to `RenderDevice`, near the existing viewport hooks (after `virtual void end_viewport_pass() {}` / `virtual ViewportTextureId viewport_texture() const { return 0; }`):

**Deviation from the design spec, noted here rather than silently changed:** the spec's `RenderDevice` interface sketch wrote `begin_shadow_pass(const Mat4& light_view_proj)`. Grounding this in the real Vulkan backend (Task 4) showed the parameter would be unused — `draw_shadow`'s own `light_mvp` argument already carries the fully-composed per-draw matrix, and the shadow pass's `VkRenderPassBeginInfo` needs no light data at all. Dropped for YAGNI; `light_view_proj` is fully available to any future backend that does need it as a `RenderSystem`-local variable already, unchanged by this simplification.

```cpp
    // Shadow pass: render depth-only geometry from the directional light's point of
    // view into a dedicated shadow map. Default no-op (a backend that doesn't
    // implement shadows simply never populates one; the main pass's shadow lookup
    // then must degrade to "fully lit" on the shader side, not fail).
    virtual void begin_shadow_pass() {}
    virtual void draw_shadow(MeshHandle /*mesh*/, const mosaic::foundation::math::Mat4& /*light_mvp*/) {}
    virtual void end_shadow_pass() {}
```

- [ ] **Step 4: Wire the shadow pass into `RenderSystem::render`**

In `engine/render/render_system.hpp`, add the include:

```cpp
#include "engine/render/shadow.hpp"
```

At the top of `render(...)`, immediately after `const PointLightState point = active_point_light(world);` and before the existing `world.for_each<MeshRenderer>(...)` main-pass loop, insert the shadow pass:

```cpp
        const Mat4 light_view_proj = directional_light_view_projection(light);

        device.begin_shadow_pass();
        world.for_each<MeshRenderer>(
            [&](mosaic::foundation::EntityID e, MeshRenderer& mr)
            {
                const MeshHandle mesh = mesh_cache_.get(mr.mesh, registry, device);
                if (mesh == 0) { return; }
                const auto* wt = world.get_component<transform::WorldTransform>(e);
                if (wt == nullptr) { return; }
                device.draw_shadow(mesh, light_view_proj * wt->matrix);
            });
        device.end_shadow_pass();
```

Then, in the existing main-pass loop's `device.draw(...)` call, add `light_view_proj * wt->matrix` as the new `light_mvp` field:

```cpp
                device.draw(mesh, texture,
                            DrawParams{projection * view * wt->matrix, light_obj,
                                       light.color, base_color, point_position, point_color,
                                       point_range, light_view_proj * wt->matrix});
```

(This changes the existing `DrawParams{...}` aggregate-init call site — add the one new trailing argument in the same position `light_mvp` was declared in Step 3.)

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build --target mosaic_unit_tests && ./build/tests/mosaic_unit_tests "[render]"`
Expected: all `[render]`-tagged tests pass, including the two new ones. Also run the full suite to catch any other `DrawParams{...}` aggregate-init call sites broken by the new field:

Run: `./build/tests/mosaic_unit_tests`
Expected: same pass count as before this task, plus the new tests (the one known pre-existing threaded-ScriptSystem flake aside).

- [ ] **Step 6: Commit**

```bash
git add engine/render/render_device.hpp engine/render/render_system.hpp tests/unit/render/render_device_hooks_test.cpp tests/unit/render/render_system_test.cpp
git commit -m "feat(render): RenderDevice shadow hooks + RenderSystem shadow-pass wiring

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
```

---

### Task 4: Vulkan backend — shadow map, pipeline, shaders

**Files:**
- Modify: `modules/render_vulkan/vulkan_render_device.cpp`
- Create: `modules/render_vulkan/shaders/shadow.vert`
- Modify: `modules/render_vulkan/shaders/cube.vert`
- Modify: `modules/render_vulkan/shaders/cube.frag`
- Modify: `modules/render_vulkan/CMakeLists.txt` (add `shadow.vert` to the shader-compile list)

**Interfaces:**
- Consumes: `RenderDevice::begin_shadow_pass`/`draw_shadow`/`end_shadow_pass`, `DrawParams::light_mvp` (Task 3).
- Produces: nothing consumed by later C++ tasks (renders shadows on the GPU).

> No unit test — this is GPU/shader code with no device in the test build, exactly like the point-light slice's Vulkan task. The deliverable is verified by a clean build (shaders compile, `static_assert` holds) and a functional windowed/editor run in Task 5.

- [ ] **Step 1: Add shadow-map member state**

Near the existing depth-buffer members (search for `VkImage depth_image_ = VK_NULL_HANDLE;`), add:

```cpp
    static constexpr uint32_t kShadowMapSize = 2048; // fixed resolution, v1 (see design doc)
    VkImage        shadow_image_ = VK_NULL_HANDLE;
    VkDeviceMemory shadow_memory_ = VK_NULL_HANDLE;
    VkImageView    shadow_view_ = VK_NULL_HANDLE;
    VkSampler      shadow_sampler_ = VK_NULL_HANDLE;
    VkRenderPass   shadow_render_pass_ = VK_NULL_HANDLE;
    VkFramebuffer  shadow_framebuffer_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout shadow_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool      shadow_descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet       shadow_descriptor_set_ = VK_NULL_HANDLE;
    VkPipelineLayout shadow_pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline       shadow_pipeline_ = VK_NULL_HANDLE;
```

- [ ] **Step 2: Create the shadow map image, view, sampler, render pass, framebuffer**

Add a new method, mirroring `create_depth_resources()` (fixed `kShadowMapSize` instead of `extent_`, `SAMPLED_BIT` added so the main pass can read it, `STORE_OP_STORE` since the depth must survive into the next pass, `finalLayout` transitions straight to `SHADER_READ_ONLY_OPTIMAL`):

```cpp
    // Fixed-resolution, created once (never recreated on window resize — shadow
    // resolution is independent of window/viewport size in v1).
    void create_shadow_resources()
    {
        VkImageCreateInfo img{};
        img.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        img.imageType = VK_IMAGE_TYPE_2D;
        img.extent.width = kShadowMapSize;
        img.extent.height = kShadowMapSize;
        img.extent.depth = 1;
        img.mipLevels = 1;
        img.arrayLayers = 1;
        img.format = kDepthFormat;
        img.tiling = VK_IMAGE_TILING_OPTIMAL;
        img.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        img.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        img.samples = VK_SAMPLE_COUNT_1_BIT;
        img.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vk_check(vkCreateImage(device_, &img, nullptr, &shadow_image_), "vkCreateImage(shadow)");

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(device_, shadow_image_, &req);
        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex =
            find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vk_check(vkAllocateMemory(device_, &alloc, nullptr, &shadow_memory_),
                 "vkAllocateMemory(shadow)");
        vk_check(vkBindImageMemory(device_, shadow_image_, shadow_memory_, 0),
                 "vkBindImageMemory(shadow)");

        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = shadow_image_;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = kDepthFormat;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        view.subresourceRange.baseMipLevel = 0;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.baseArrayLayer = 0;
        view.subresourceRange.layerCount = 1;
        vk_check(vkCreateImageView(device_, &view, nullptr, &shadow_view_),
                 "vkCreateImageView(shadow)");

        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = VK_FILTER_NEAREST; // hard shadows, v1 -- no PCF, so no filtering benefit
        si.minFilter = VK_FILTER_NEAREST;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE; // max depth outside the frustum -> never occluding
        si.maxLod = 1.0f;
        vk_check(vkCreateSampler(device_, &si, nullptr, &shadow_sampler_),
                 "vkCreateSampler(shadow)");

        VkAttachmentDescription depth{};
        depth.format = kDepthFormat;
        depth.samples = VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference depth_ref{};
        depth_ref.attachment = 0;
        depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.pDepthStencilAttachment = &depth_ref;

        std::array<VkSubpassDependency, 2> deps{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass = 0;
        deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp.attachmentCount = 1;
        rp.pAttachments = &depth;
        rp.subpassCount = 1;
        rp.pSubpasses = &subpass;
        rp.dependencyCount = static_cast<uint32_t>(deps.size());
        rp.pDependencies = deps.data();
        vk_check(vkCreateRenderPass(device_, &rp, nullptr, &shadow_render_pass_),
                 "vkCreateRenderPass(shadow)");

        VkFramebufferCreateInfo fb{};
        fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass = shadow_render_pass_;
        fb.attachmentCount = 1;
        fb.pAttachments = &shadow_view_;
        fb.width = kShadowMapSize;
        fb.height = kShadowMapSize;
        fb.layers = 1;
        vk_check(vkCreateFramebuffer(device_, &fb, nullptr, &shadow_framebuffer_),
                 "vkCreateFramebuffer(shadow)");
    }
```

- [ ] **Step 3: Descriptor set for sampling the shadow map in the main pass**

Add a method creating a second descriptor set layout/pool/set (set = 1 in the main pipeline, distinct from the per-texture set-0 layout the material texture already uses):

```cpp
    // Set 1 in the main pipeline: a single, permanent descriptor pointing at the
    // shadow map, shared by every draw call this frame (unlike set 0's per-texture
    // sets) -- allocated once, written once (the shadow map is never recreated).
    void create_shadow_descriptor()
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layout{};
        layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout.bindingCount = 1;
        layout.pBindings = &binding;
        vk_check(vkCreateDescriptorSetLayout(device_, &layout, nullptr, &shadow_set_layout_),
                 "vkCreateDescriptorSetLayout(shadow)");

        VkDescriptorPoolSize pool_size{};
        pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_size.descriptorCount = 1;
        VkDescriptorPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &pool_size;
        pool.maxSets = 1;
        vk_check(vkCreateDescriptorPool(device_, &pool, nullptr, &shadow_descriptor_pool_),
                 "vkCreateDescriptorPool(shadow)");

        VkDescriptorSetAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc.descriptorPool = shadow_descriptor_pool_;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &shadow_set_layout_;
        vk_check(vkAllocateDescriptorSets(device_, &alloc, &shadow_descriptor_set_),
                 "vkAllocateDescriptorSets(shadow)");

        VkDescriptorImageInfo image_info{};
        image_info.sampler = shadow_sampler_;
        image_info.imageView = shadow_view_;
        image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = shadow_descriptor_set_;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &image_info;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }
```

- [ ] **Step 4: Grow `PushConstants`, add the device push-constant-budget check**

Replace the `PushConstants` struct and its `static_assert`:

```cpp
struct PushConstants
{
    float mvp[16];
    float light_direction[4]; // vec3 + pad
    float light_color[4];
    float base_color[4];
    float point_position[4]; // xyz = object-space pos, w = range
    float point_color[4];    // xyz = color * intensity
    float light_mvp[16];     // light_view_proj * model, for shadow sampling
};
static_assert(sizeof(PushConstants) == 208, "push constant layout must match the shader");
```

In `pick_physical_device()`, the device push-constant-budget check already added by the point-light slice compares against `sizeof(PushConstants)` — no change needed there beyond the struct growing (the existing `if (sizeof(PushConstants) > props.limits.maxPushConstantsSize)` check now guards 208 bytes automatically). Update the comment above it:

```cpp
            // 208-byte push constants (mvp + lighting + shadow light-space matrix)
            // exceed Vulkan's guaranteed 128-byte minimum but fit desktop GPUs (256).
            // Fail loud rather than let validation catch it downstream.
```

Add a small shadow-pipeline push constant too (light_mvp only, for the depth-only pass):

```cpp
struct ShadowPushConstants
{
    float light_mvp[16];
};
static_assert(sizeof(ShadowPushConstants) == 64, "shadow push constant layout must match the shader");
```

- [ ] **Step 5: Create the shadow pipeline**

Add a method mirroring `create_pipeline()`'s structure, but vertex-stage-only (no fragment shader — valid for a subpass with no color attachments), reusing the same `Vertex` binding/attribute layout as the main pipeline for simplicity (only position is used by the shader, but a single shared vertex buffer layout avoids a second mesh-buffer binding path):

```cpp
    void create_shadow_pipeline()
    {
        std::vector<char> vert_code = read_file(std::string(MOSAIC_SHADER_DIR) + "/shadow.vert.spv");
        VkShaderModule vert = create_shader_module(vert_code);

        VkPipelineShaderStageCreateInfo vs{};
        vs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vs.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vs.module = vert;
        vs.pName = "main";

        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(Vertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription pos_attr{};
        pos_attr.location = 0;
        pos_attr.binding = 0;
        pos_attr.format = VK_FORMAT_R32G32B32_SFLOAT;
        pos_attr.offset = static_cast<uint32_t>(offsetof(Vertex, position));

        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &binding;
        vi.vertexAttributeDescriptionCount = 1;
        vi.pVertexAttributeDescriptions = &pos_attr;

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkViewport vp{};
        vp.width = static_cast<float>(kShadowMapSize);
        vp.height = static_cast<float>(kShadowMapSize);
        vp.maxDepth = 1.0f;
        VkRect2D scissor{};
        scissor.extent = {kShadowMapSize, kShadowMapSize};
        VkPipelineViewportStateCreateInfo vps{};
        vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vps.viewportCount = 1;
        vps.pViewports = &vp;
        vps.scissorCount = 1;
        vps.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE; // avoid peter-panning from front-face-only culling
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable = VK_TRUE;
        ds.depthWriteEnable = VK_TRUE;
        ds.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 0; // no color attachments in this subpass

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pcr.size = sizeof(ShadowPushConstants);

        VkPipelineLayoutCreateInfo pl{};
        pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pl.pushConstantRangeCount = 1;
        pl.pPushConstantRanges = &pcr;
        vk_check(vkCreatePipelineLayout(device_, &pl, nullptr, &shadow_pipeline_layout_),
                 "vkCreatePipelineLayout(shadow)");

        VkGraphicsPipelineCreateInfo gp{};
        gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gp.stageCount = 1;
        gp.pStages = &vs;
        gp.pVertexInputState = &vi;
        gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vps;
        gp.pRasterizationState = &rs;
        gp.pMultisampleState = &ms;
        gp.pDepthStencilState = &ds;
        gp.pColorBlendState = &cb;
        gp.layout = shadow_pipeline_layout_;
        gp.renderPass = shadow_render_pass_;
        gp.subpass = 0;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr,
                                           &shadow_pipeline_),
                 "vkCreateGraphicsPipelines(shadow)");

        vkDestroyShaderModule(device_, vert, nullptr);
    }
```

- [ ] **Step 6: Extend the main pipeline layout with the shadow descriptor set**

Find `create_pipeline()`'s `VkPipelineLayoutCreateInfo` construction (search for the block that sets `pl.pSetLayouts = &texture_set_layout_;` or similar single-set-layout wiring) and change it to include both set layouts:

```cpp
        std::array<VkDescriptorSetLayout, 2> set_layouts{texture_set_layout_, shadow_set_layout_};
        VkPipelineLayoutCreateInfo pl{};
        pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pl.setLayoutCount = static_cast<uint32_t>(set_layouts.size());
        pl.pSetLayouts = set_layouts.data();
        pl.pushConstantRangeCount = 1;
        pl.pPushConstantRanges = &pcr; // existing push-constant range, unchanged shape
        vk_check(vkCreatePipelineLayout(device_, &pl, nullptr, &pipeline_layout_),
                 "vkCreatePipelineLayout");
```

(Keep the existing `pcr`/push-constant-range variable name and construction exactly as it already is in that function — only `pl.setLayoutCount`/`pl.pSetLayouts` change, from one layout to two.)

- [ ] **Step 7: Implement `begin_shadow_pass` / `draw_shadow` / `end_shadow_pass`**

Add near the existing `draw()` method:

```cpp
    void begin_shadow_pass() override
    {
        VkClearValue clear{};
        clear.depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = shadow_render_pass_;
        rp.framebuffer = shadow_framebuffer_;
        rp.renderArea.extent = {kShadowMapSize, kShadowMapSize};
        rp.clearValueCount = 1;
        rp.pClearValues = &clear;
        vkCmdBeginRenderPass(cmd_[frame_], &rp, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_pipeline_);
    }

    void draw_shadow(MeshHandle mesh, const Mat4& light_mvp) override
    {
        if (mesh == 0 || mesh > meshes_.size()) { return; }
        const MeshBuffers& mb = meshes_[mesh - 1];
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd_[frame_], 0, 1, &mb.vbuf, &offset);
        vkCmdBindIndexBuffer(cmd_[frame_], mb.ibuf, 0, VK_INDEX_TYPE_UINT32);

        ShadowPushConstants pc{};
        std::memcpy(pc.light_mvp, light_mvp.m.data(), sizeof(pc.light_mvp));
        vkCmdPushConstants(cmd_[frame_], shadow_pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(pc), &pc);
        vkCmdDrawIndexed(cmd_[frame_], mb.index_count, 1, 0, 0, 0);
    }

    void end_shadow_pass() override { vkCmdEndRenderPass(cmd_[frame_]); }
```

(`cmd_[frame_]` is the existing per-frame command buffer already used by every other draw method in this class — no new command-buffer management needed. Because `begin_shadow_pass`/`draw_shadow`/`end_shadow_pass` are called by `RenderSystem::render` before any main-pass call, the shadow render pass is fully recorded — begin, N draws, end — before `ensure_swapchain_pass()`/`begin_viewport_pass()` lazily opens the main pass on the first `draw()` call, giving the fixed pass order the design calls for with no extra ordering logic needed.)

- [ ] **Step 8: Copy `light_mvp` into the main pass's `PushConstants` in `draw()`**

In the existing `draw()` method, after the existing `pc.point_color[2] = params.point_color.z;` line and before `vkCmdPushConstants(cmd_[frame_], pipeline_layout_, ...)`, add:

```cpp
        std::memcpy(pc.light_mvp, params.light_mvp.m.data(), sizeof(pc.light_mvp));
```

Also bind the shadow descriptor set (set 1) alongside the existing texture descriptor set (set 0) — find the existing `vkCmdBindDescriptorSets(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, 1, &textures_[tex_index].set, 0, nullptr);` call and add immediately after it:

```cpp
        vkCmdBindDescriptorSets(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline_layout_, 1, 1, &shadow_descriptor_set_, 0, nullptr);
```

- [ ] **Step 9: Wire construction, destruction, and the CMake shader list**

In the constructor, after `create_descriptor_infrastructure();` and before `create_pipeline();`, add:

```cpp
        create_shadow_resources();
        create_shadow_descriptor();
```

and after `create_pipeline();`, add:

```cpp
        create_shadow_pipeline();
```

In the destructor, add cleanup (mirroring the existing offscreen-target teardown block — place it right after `destroy_offscreen_target();` and its related `vp_*` destroys, before the mesh-buffer teardown loop):

```cpp
            if (shadow_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, shadow_pipeline_, nullptr); }
            if (shadow_pipeline_layout_ != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, shadow_pipeline_layout_, nullptr); }
            if (shadow_descriptor_pool_ != VK_NULL_HANDLE) { vkDestroyDescriptorPool(device_, shadow_descriptor_pool_, nullptr); }
            if (shadow_set_layout_ != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(device_, shadow_set_layout_, nullptr); }
            if (shadow_framebuffer_ != VK_NULL_HANDLE) { vkDestroyFramebuffer(device_, shadow_framebuffer_, nullptr); }
            if (shadow_render_pass_ != VK_NULL_HANDLE) { vkDestroyRenderPass(device_, shadow_render_pass_, nullptr); }
            if (shadow_sampler_ != VK_NULL_HANDLE) { vkDestroySampler(device_, shadow_sampler_, nullptr); }
            if (shadow_view_ != VK_NULL_HANDLE) { vkDestroyImageView(device_, shadow_view_, nullptr); }
            if (shadow_image_ != VK_NULL_HANDLE) { vkDestroyImage(device_, shadow_image_, nullptr); }
            if (shadow_memory_ != VK_NULL_HANDLE) { vkFreeMemory(device_, shadow_memory_, nullptr); }
```

In `modules/render_vulkan/CMakeLists.txt`, add `shadow.vert` to the `foreach` shader list (it has no matching `.frag` — the depth-only pipeline uses no fragment shader stage):

```cmake
foreach(_stage cube.vert cube.frag shadow.vert particle.vert particle.frag ribbon.vert ribbon.frag decal.vert decal.frag screen_effect.vert screen_effect.frag ui.vert ui.frag ui_image.vert ui_image.frag ui_text.vert ui_text.frag world_text.vert world_text.frag)
```

- [ ] **Step 10: Write the shadow vertex shader**

Create `modules/render_vulkan/shaders/shadow.vert`:

```glsl
#version 450
layout(location = 0) in vec3 inPosition;
layout(push_constant) uniform Push {
    mat4 light_mvp;
} pc;
void main()
{
    gl_Position = pc.light_mvp * vec4(inPosition, 1.0);
}
```

- [ ] **Step 11: Update the main vertex shader to output light-space position**

Replace `modules/render_vulkan/shaders/cube.vert`:

```glsl
#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 light_direction; // object space, .xyz
    vec4 light_color;     // .xyz
    vec4 base_color;      // .xyz
    vec4 point_position;  // object space .xyz, .w = range
    vec4 point_color;     // .xyz = color * intensity
    mat4 light_mvp;       // light_view_proj * model, for shadow sampling
} pc;
layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragUV;
layout(location = 2) out vec3 fragPos;
layout(location = 3) out vec4 fragLightSpacePos;
void main()
{
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    fragNormal = inNormal; // object space; lights are provided in object space too
    fragUV = inUV;
    fragPos = inPosition;  // object space
    fragLightSpacePos = pc.light_mvp * vec4(inPosition, 1.0);
}
```

- [ ] **Step 12: Update the main fragment shader to sample the shadow map**

Replace `modules/render_vulkan/shaders/cube.frag`:

```glsl
#version 450
layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec3 fragPos;
layout(location = 3) in vec4 fragLightSpacePos;
layout(set = 0, binding = 0) uniform sampler2D tex;
layout(set = 1, binding = 0) uniform sampler2D shadowMap;
layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 light_direction;
    vec4 light_color;
    vec4 base_color;
    vec4 point_position; // object space .xyz, .w = range
    vec4 point_color;    // .xyz = color * intensity
    mat4 light_mvp;
} pc;
layout(location = 0) out vec4 outColor;

// Hard shadow (v1, no PCF): 1.0 = fully lit, 0.0 = in shadow. A fragment whose
// light-clip-space position falls outside the fixed frustum's [0,1] UV range is
// treated as not shadowed (fully lit) -- see the design doc's Shading section.
float shadow_factor()
{
    vec3 proj = fragLightSpacePos.xyz / fragLightSpacePos.w;
    vec2 uv = proj.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || proj.z > 1.0)
    {
        return 1.0;
    }
    const float bias = 0.0015; // constant bias; slope-scaling deferred (see design doc)
    float closest = texture(shadowMap, uv).r;
    return (proj.z - bias > closest) ? 0.0 : 1.0;
}

void main()
{
    vec3 n = normalize(fragNormal);
    vec3 l = normalize(pc.light_direction.xyz);
    float lambert = max(0.0, dot(n, -l)) * shadow_factor();
    vec3 albedo = texture(tex, fragUV).rgb * pc.base_color.xyz;

    // Point light (object space): windowed inverse-square attenuation. Unaffected
    // by shadows in v1 -- only the directional light casts shadows.
    vec3  toL   = pc.point_position.xyz - fragPos;
    float d     = length(toL);
    float range = pc.point_position.w;
    float atten = 0.0;
    if (range > 0.0)
    {
        float win = clamp(1.0 - pow(d / range, 4.0), 0.0, 1.0);
        atten = (win * win) / (d * d + 1.0);
    }
    float pl = max(0.0, dot(n, toL / max(d, 1e-4))) * atten;

    vec3 color = albedo * (0.1 + lambert * pc.light_color.xyz)
               + albedo * pl * pc.point_color.xyz;
    outColor = vec4(color, 1.0);
}
```

- [ ] **Step 13: Build**

Run: `cmake --build build 2>&1 | tail -60`
Expected: clean build — both shaders compile via `glslc`, `static_assert(sizeof(PushConstants) == 208)` and `static_assert(sizeof(ShadowPushConstants) == 64)` hold, no link errors.

- [ ] **Step 14: Commit**

```bash
git add modules/render_vulkan/vulkan_render_device.cpp modules/render_vulkan/shaders/shadow.vert modules/render_vulkan/shaders/cube.vert modules/render_vulkan/shaders/cube.frag modules/render_vulkan/CMakeLists.txt
git commit -m "feat(render): Vulkan shadow map (depth pass, pipeline, shaders)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
```

---

### Task 5: End-to-end verification + build handoff

**Files:**
- Modify: `docs/project/next-steps.md` (prepend the shipment entry)

**Interfaces:**
- Consumes: everything from Tasks 1-4.
- Produces: nothing (terminal task).

- [ ] **Step 1: Full unit suite**

Run: `cmake --build build --target mosaic_unit_tests && ./build/tests/mosaic_unit_tests 2>&1 | tail -15`
Expected: same pass count as the pre-existing baseline plus the new Task 1/2/3 tests (the one known pre-existing threaded-ScriptSystem flake aside).

Also run via `ctest` (matches the CI gate exactly):
Run: `ctest --preset debug --output-on-failure -E "threaded ScriptSystem applies script writes|ScriptWorker runs a job on its thread"`
Expected: all tests pass (these two are excluded from the CI gate for the pre-existing, unrelated flake reasons recorded in `docs/project/deferred-work-backlog.md`'s Tech debt section — not something this task should investigate).

- [ ] **Step 2: Functional windowed run**

Run: `PROJ=$(mktemp -d)/shadow_demo && ./build/app/mosaic create "$PROJ" && ./build/app/mosaic run --window "$PROJ" --duration 2`
Expected: exit 0, no error output on stderr. (The window's visual output cannot be screenshotted in this sandbox — functional exit is the acceptance, per the design doc's Testing & verification section. Confirming shadows *look* correct needs the user's own hardware.)

- [ ] **Step 3: Functional editor run**

Run: `PROJ=$(mktemp -d)/shadow_demo_editor && ./build/app/mosaic create "$PROJ" && timeout 3 ./build/app/mosaic editor "$PROJ"; echo "exit=$?"`
Expected: clean startup (no crash, no validation-layer errors in stderr) for the ~3 seconds before `timeout` sends SIGTERM — this is now a 3-pass frame (shadow → offscreen viewport → swapchain blit) and must remain stable.

- [ ] **Step 4: Update the build handoff**

Prepend an entry to the top of `docs/project/next-steps.md` (after the file's header, before the current top entry), in that file's established dense per-entry voice — describe what shipped (directional-only hard shadows, fixed frustum, direct-submission extended to a 3rd pass), the push-constant growth risk and its guard, test counts, and functional verification results. Follow the exact structure of the entries already at the top of that file.

- [ ] **Step 5: Update the deferred-work backlog**

In `docs/project/deferred-work-backlog.md`'s Rendering section, mark the "Shadows — v1" entry as shipped (mirroring how prior shipped items in that file are annotated — e.g. `~~item~~` with a **SHIPPED** note and date), leaving the nested deferred-upgrade sub-bullets (PCF, point-light shadows, scene-fitted frustum, configurable resolution) exactly as they are, since none of those are built by this plan.

- [ ] **Step 6: Commit**

```bash
git add docs/project/next-steps.md docs/project/deferred-work-backlog.md
git commit -m "docs: record shadows v1 shipment

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
```
