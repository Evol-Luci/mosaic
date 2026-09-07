# VFX V3 — quad decals — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A `Decal` component renders a world-oriented, alpha-blended textured quad on a surface, placed by its `WorldTransform`.

**Architecture:** `RenderSystem` composes each decal's MVP (`projection*view*world`) and resolves its texture via the existing `TextureCache`, submitting `DecalInstance`s through a new `draw_decals` seam. The Vulkan side is a textured, alpha-blended, shader-generated-quad pipeline that reuses the existing `texture_set_layout_` (binds each decal's per-`Texture` descriptor set like the mesh path) — no vertex buffer, no matrix math in the device.

**Tech Stack:** C++20, Vulkan (glslc via CMake), Catch2. No new dependencies.

**Spec:** [docs/superpowers/specs/2026-09-07-vfx-decals-design.md](2026-09-07-vfx-decals-design.md)

## Global Constraints

- Decals are textured + alpha-blended; depth-test on, depth-write off; `VK_CULL_MODE_NONE`; `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST`; two pipeline variants (swapchain + viewport), modeled on the particle pipeline.
- The engine composes `DecalInstance.mvp = projection * view * WorldTransform.matrix` (no Mat4 math in the Vulkan layer); the device just memcpy's it into the push constant.
- The decal pipeline reuses `texture_set_layout_` at set 0; per decal it binds `textures_[handle-1].set` (handle 0 / out-of-range → `textures_[0]`, the default white), exactly like `draw()`.
- Quad is shader-generated (6 corners, local XY plane, z=0); UV = corner + 0.5.
- `DecalPush { float mvp[16]; float color[4]; }` (80 bytes; color.w = alpha), stages vertex|fragment.
- GPU visual is the maintainer's check; here: headless geometry/wiring + build + `run --window` exit 0.
- **This slice does NOT update `docs/project/next-steps.md`** (the VFX wrap-up next-steps edit rides in the screen-effects PR).
- Tests `[vfx]`/`[render]`; build target `mosaic_unit_tests`; preset `build/debug`.

---

### Task 1: `Decal` component + `DecalInstance` seam

**Files:**
- Create: `engine/vfx/decal.hpp`
- Modify: `engine/render/render_device.hpp` (`DecalInstance` + `draw_decals` no-op)
- Modify: `engine/reflection/builtin_components.hpp` (include + register)
- Create: `tests/unit/vfx/decal_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `mosaic::engine::vfx::Decal{ AssetID texture; Vec3 color; float alpha; }` + `reflect`; `mosaic::engine::render::DecalInstance{ Mat4 mvp; TextureHandle texture; Vec3 color; float alpha; }`; `RenderDevice::draw_decals(const std::vector<DecalInstance>&)`.

- [ ] **Step 1: Create `engine/vfx/decal.hpp`**

```cpp
#pragma once

#include "foundation/math/vec3.hpp"
#include "foundation/serialization/reference.hpp" // AssetID

namespace mosaic::engine::vfx
{
using mosaic::foundation::math::Vec3;

// A world-oriented, alpha-blended textured quad on a surface (scorch marks, blob
// shadows, signs). The entity's Transform (-> WorldTransform) places/orients/sizes it.
struct Decal
{
    mosaic::foundation::serialization::AssetID texture{};                 // nil = default white
    Vec3                                       color = Vec3{1, 1, 1};     // tint * texture
    float                                      alpha = 1.0f;              // opacity multiplier
};

template <class V>
void reflect(V& visitor, Decal& d)
{
    visitor.member("texture", d.texture);
    visitor.member("color", d.color);
    visitor.member("alpha", d.alpha);
}
} // namespace mosaic::engine::vfx
```

- [ ] **Step 2: Add the `DecalInstance` seam to `render_device.hpp`**

In `engine/render/render_device.hpp`, after the `RibbonDrawParams` struct, add:

```cpp
// One decal quad: mvp = projection * view * world (composed by the RenderSystem).
struct DecalInstance
{
    mosaic::foundation::math::Mat4 mvp;
    TextureHandle                  texture = 0; // 0 / out-of-range -> default white
    mosaic::foundation::math::Vec3 color;
    float                          alpha = 1.0f;
};
```

and in `class RenderDevice`, after `draw_ribbons`, add:

```cpp
    // Draw alpha-blended textured quads (decals). Default no-op (like draw_particles).
    virtual void draw_decals(const std::vector<DecalInstance>& /*decals*/) {}
```

- [ ] **Step 3: Register `Decal` as a builtin**

In `engine/reflection/builtin_components.hpp`, add the include:

```cpp
#include "engine/vfx/decal.hpp"
```

and, after the `TrailRenderer` registration line in `register_builtin_components`, add:

```cpp
    types.register_component<mosaic::engine::vfx::Decal>(registry, "Decal");
```

- [ ] **Step 4: Create `tests/unit/vfx/decal_test.cpp`**

```cpp
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/vfx/decal.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reference.hpp"
#include "foundation/serialization/reflect_value.hpp"

using Catch::Approx;
using mosaic::engine::vfx::Decal;
using mosaic::foundation::math::Vec3;

TEST_CASE("Decal round-trips through reflection", "[vfx]")
{
    Decal d;
    d.texture = mosaic::foundation::serialization::AssetID::from_string(
        "00000000-0000-0000-0000-0000000000d1");
    d.color = Vec3{0.8f, 0.2f, 0.1f};
    d.alpha = 0.6f;

    const auto value = mosaic::foundation::serialization::to_value(d);
    Decal back;
    mosaic::foundation::serialization::from_value(value, back);

    REQUIRE(back.texture == d.texture);
    REQUIRE(back.color.x == Approx(0.8f));
    REQUIRE(back.alpha == Approx(0.6f));
}

TEST_CASE("Decal is a registered built-in component", "[vfx]")
{
    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    REQUIRE(types.has("Decal"));
}
```

- [ ] **Step 5: Add the test file to `tests/CMakeLists.txt`**

After `  unit/vfx/ribbon_test.cpp`, add:

```cmake
  unit/vfx/decal_test.cpp
```

- [ ] **Step 6: Build + run**

Run: `cmake --build build/debug --target mosaic_unit_tests && ctest --test-dir build/debug -R "Decal" --output-on-failure`
Expected: PASS — reflection round-trip + builtin registration.

- [ ] **Step 7: Commit**

```bash
git add engine/vfx/decal.hpp engine/render/render_device.hpp engine/reflection/builtin_components.hpp tests/unit/vfx/decal_test.cpp tests/CMakeLists.txt
git commit -m "feat(vfx): Decal component + DecalInstance/draw_decals seam

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: RenderSystem decal pass

**Files:**
- Modify: `engine/render/render_system.hpp` (decal pass)
- Modify: `tests/unit/render/render_system_test.cpp` (FakeRenderDevice + test)

**Interfaces:**
- Consumes: `Decal`, `DecalInstance`/`draw_decals`, `WorldTransform`, `texture_cache_` (existing member).

- [ ] **Step 1: Include the decal header**

In `engine/render/render_system.hpp`, add near the other includes:

```cpp
#include "engine/vfx/decal.hpp"
```

- [ ] **Step 2: Add the decal pass after the mesh loop**

In `engine/render/render_system.hpp`, immediately after the `world.for_each<MeshRenderer>(...)` mesh loop closes (before the billboard/particle block), add:

```cpp
        // Decal pass: alpha-blended textured quads placed by WorldTransform. After opaque
        // meshes so they blend onto surfaces; MVP composed here (device does no matrix math).
        {
            std::vector<DecalInstance> decals;
            world.for_each<mosaic::engine::vfx::Decal>(
                [&](mosaic::foundation::EntityID e, mosaic::engine::vfx::Decal& d)
                {
                    const auto* wt = world.get_component<transform::WorldTransform>(e);
                    if (wt == nullptr) { return; }
                    const TextureHandle tex = texture_cache_.get(d.texture, registry, device);
                    decals.push_back(DecalInstance{projection * view * wt->matrix, tex, d.color, d.alpha});
                });
            if (!decals.empty()) { device.draw_decals(decals); }
        }
```

(`projection`, `view`, `registry`, `device` are already in scope in `render`.)

- [ ] **Step 3: Extend `FakeRenderDevice` + add the render test**

In `tests/unit/render/render_system_test.cpp`, add the using-declaration after the ribbon usings:

```cpp
using mosaic::engine::render::DecalInstance;
```

In `FakeRenderDevice`, after the `ribbon_draws` members, add:

```cpp
    std::vector<std::vector<DecalInstance>> decal_draws;
    void draw_decals(const std::vector<DecalInstance>& decals) override
    {
        decal_draws.push_back(decals);
    }
```

Then append the test:

```cpp
TEST_CASE("RenderSystem emits a decal instance per Decal entity", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);
    add_camera(world, Vec3{0, 0, 4});

    auto e = world.create_entity();
    world.add_component<WorldTransform>(e, WorldTransform{Mat4::identity()}); // as the mesh tests do
    mosaic::engine::vfx::Decal d;
    d.color = Vec3{0.3f, 0.6f, 0.9f};
    d.alpha = 0.5f;
    world.add_component<mosaic::engine::vfx::Decal>(e, d);

    rs.render(world, device, registry);

    REQUIRE(device.decal_draws.size() == 1);
    REQUIRE(device.decal_draws[0].size() == 1);
    REQUIRE(device.decal_draws[0][0].color.x == Approx(0.3f));
    REQUIRE(device.decal_draws[0][0].alpha == Approx(0.5f));
}
```

- [ ] **Step 4: Build + run render tests**

Run: `cmake --build build/debug --target mosaic_unit_tests && ctest --test-dir build/debug -R "RenderSystem|emits a decal" --output-on-failure`
Expected: PASS — the decal render test + all existing `[render]` cases.

- [ ] **Step 5: Commit**

```bash
git add engine/render/render_system.hpp tests/unit/render/render_system_test.cpp
git commit -m "feat(vfx): RenderSystem decal pass (draw_decals from WorldTransform)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: Vulkan decal pipeline + demo + counts

**Files:**
- Create: `modules/render_vulkan/shaders/decal.vert`, `modules/render_vulkan/shaders/decal.frag`
- Modify: `modules/render_vulkan/CMakeLists.txt`
- Modify: `modules/render_vulkan/vulkan_render_device.cpp`
- Modify: `app/run/demo_scene.hpp` (demo decal entity)
- Modify: `tests/unit/app/demo_scene_test.cpp`, `tests/unit/app/populate_world_test.cpp`, `tests/unit/app/bundle_test.cpp`, `tests/unit/app/scene_save_roundtrip_test.cpp` (counts 10 → 11)

**Interfaces:**
- Consumes: `DecalInstance`/`draw_decals` (Task 1); `texture_set_layout_`, `textures_`, `create_shader_module`, `read_file` (existing).

- [ ] **Step 1: Create the shaders**

`modules/render_vulkan/shaders/decal.vert`:

```glsl
#version 450

layout(push_constant) uniform Push { mat4 mvp; vec4 color; } pc; // color.w = alpha

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragColor;

// A unit quad on the local XY plane (z = 0), two triangles.
const vec2 kCorners[6] = vec2[6](
    vec2(-0.5, -0.5), vec2( 0.5, -0.5), vec2( 0.5,  0.5),
    vec2(-0.5, -0.5), vec2( 0.5,  0.5), vec2(-0.5,  0.5)
);

void main()
{
    vec2 c = kCorners[gl_VertexIndex];
    gl_Position = pc.mvp * vec4(c, 0.0, 1.0);
    fragUV = c + vec2(0.5);
    fragColor = pc.color;
}
```

`modules/render_vulkan/shaders/decal.frag`:

```glsl
#version 450

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 t = texture(tex, fragUV);
    outColor = vec4(t.rgb * fragColor.rgb, t.a * fragColor.w);
}
```

- [ ] **Step 2: Compile the shaders in CMake**

In `modules/render_vulkan/CMakeLists.txt`, extend the glslc `foreach` list to end with `ribbon.vert ribbon.frag decal.vert decal.frag`.

- [ ] **Step 3: Add the `DecalPush` struct**

In `modules/render_vulkan/vulkan_render_device.cpp`, after the `RibbonPush` struct + its
`static_assert`, add:

```cpp
struct DecalPush
{
    float mvp[16];
    float color[4]; // rgb = tint, a = alpha
};
static_assert(sizeof(DecalPush) == 80, "decal push constant layout must match the shader");
```

- [ ] **Step 4: Add member handles**

After the `ribbon_pipeline_layout_` member, add:

```cpp
    VkPipeline       decal_pipeline_ = VK_NULL_HANDLE;
    VkPipeline       vp_decal_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout decal_pipeline_layout_ = VK_NULL_HANDLE;
```

- [ ] **Step 5: Create the pipeline at init + the method**

After the `create_ribbon_pipeline();` call in the constructor, add `create_decal_pipeline();`.
Then add the method after `create_ribbon_pipeline`'s closing brace:

```cpp
    void create_decal_pipeline()
    {
        std::vector<char> vert_code = read_file(std::string(MOSAIC_SHADER_DIR) + "/decal.vert.spv");
        std::vector<char> frag_code = read_file(std::string(MOSAIC_SHADER_DIR) + "/decal.frag.spv");
        VkShaderModule vert = create_shader_module(vert_code);
        VkShaderModule frag = create_shader_module(frag_code);

        VkPipelineShaderStageCreateInfo vs{};
        vs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vs.stage = VK_SHADER_STAGE_VERTEX_BIT; vs.module = vert; vs.pName = "main";
        VkPipelineShaderStageCreateInfo fs{};
        fs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fs.stage = VK_SHADER_STAGE_FRAGMENT_BIT; fs.module = frag; fs.pName = "main";
        VkPipelineShaderStageCreateInfo stages[] = {vs, fs};

        // No vertex buffer: the quad is generated from gl_VertexIndex.
        VkPipelineVertexInputStateCreateInfo vin{};
        vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1; vp.scissorCount = 1;

        VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2; dyn.pDynamicStates = dyn_states;

        VkPipelineRasterizationStateCreateInfo rast{};
        rast.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rast.polygonMode = VK_POLYGON_MODE_FILL; rast.lineWidth = 1.0f;
        rast.cullMode = VK_CULL_MODE_NONE; // quad visible from both sides

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_FALSE; // transparent: respect scene depth, don't occlude peers
        depth.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState blend{};
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blend.blendEnable = VK_TRUE;
        blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.colorBlendOp = VK_BLEND_OP_ADD;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blend.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &blend;

        VkPushConstantRange push{};
        push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        push.offset = 0; push.size = sizeof(DecalPush);

        VkPipelineLayoutCreateInfo layout{};
        layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout.setLayoutCount = 1; layout.pSetLayouts = &texture_set_layout_;
        layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &push;
        vk_check(vkCreatePipelineLayout(device_, &layout, nullptr, &decal_pipeline_layout_),
                 "vkCreatePipelineLayout(decal)");

        VkGraphicsPipelineCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pi.stageCount = 2; pi.pStages = stages;
        pi.pVertexInputState = &vin; pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp; pi.pRasterizationState = &rast;
        pi.pMultisampleState = &ms; pi.pDepthStencilState = &depth;
        pi.pColorBlendState = &cb; pi.pDynamicState = &dyn;
        pi.layout = decal_pipeline_layout_;
        pi.renderPass = render_pass_; pi.subpass = 0;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &decal_pipeline_),
                 "vkCreateGraphicsPipelines(decal)");

        pi.renderPass = vp_render_pass_;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &vp_decal_pipeline_),
                 "vkCreateGraphicsPipelines(decal offscreen)");

        vkDestroyShaderModule(device_, frag, nullptr);
        vkDestroyShaderModule(device_, vert, nullptr);
    }
```

- [ ] **Step 6: Implement `draw_decals`**

After the `draw_ribbons` method's closing brace, add:

```cpp
    void draw_decals(const std::vector<mosaic::engine::render::DecalInstance>& decals) override
    {
        if (!viewport_pass_open_) { ensure_swapchain_pass(); }
        if (frame_skipped_ || decals.empty() || textures_.empty()) { return; }

        vkCmdBindPipeline(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                          viewport_pass_open_ ? vp_decal_pipeline_ : decal_pipeline_);

        for (const auto& d : decals)
        {
            const std::uint32_t tex_index =
                (d.texture == 0 || d.texture > textures_.size()) ? 0u : d.texture - 1u;
            vkCmdBindDescriptorSets(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    decal_pipeline_layout_, 0, 1, &textures_[tex_index].set, 0,
                                    nullptr);
            DecalPush pc{};
            std::memcpy(pc.mvp, d.mvp.m.data(), sizeof(pc.mvp));
            pc.color[0] = d.color.x; pc.color[1] = d.color.y; pc.color[2] = d.color.z;
            pc.color[3] = d.alpha;
            vkCmdPushConstants(cmd_[frame_], decal_pipeline_layout_,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               sizeof(pc), &pc);
            vkCmdDraw(cmd_[frame_], 6, 1, 0, 0);
        }
    }
```

- [ ] **Step 7: Destroy the pipeline in teardown**

In the cleanup block, right after the ribbon pipeline destroys, add:

```cpp
            if (vp_decal_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, vp_decal_pipeline_, nullptr); }
            if (decal_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, decal_pipeline_, nullptr); }
            if (decal_pipeline_layout_ != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, decal_pipeline_layout_, nullptr); }
```

- [ ] **Step 8: Add the demo decal + bump the counts**

In `app/run/demo_scene.hpp`, in `build_default_scene`, after the ground entity block, add a decal on the ground:

```cpp
    // A decal lying flat on the ground (rotated so the local XY quad faces up).
    const auto decal = world.create_entity();
    world.add_component<transform::Transform>(decal,
        transform::Transform{Vec3{0, 0.02f, 0},
                             Quat::from_axis_angle(Vec3{1, 0, 0}, -1.5707963f),
                             Vec3{3, 3, 1}, {}});
    world.add_component<mosaic::engine::vfx::Decal>(decal,
        mosaic::engine::vfx::Decal{kStripesTextureId, Vec3{1.0f, 0.9f, 0.6f}, 0.85f});
```

Add the include near the top of `app/run/demo_scene.hpp`:

```cpp
#include "engine/vfx/decal.hpp"
```

Then update the four default-scene count assertions from `10` to `11`:
- `tests/unit/app/demo_scene_test.cpp`: `REQUIRE(transforms == 10);` → `11` (and its comment).
- `tests/unit/app/populate_world_test.cpp`: `REQUIRE(transforms == 10);` → `11`.
- `tests/unit/app/bundle_test.cpp`: `... == 10);` → `11` (and the comment).
- `tests/unit/app/scene_save_roundtrip_test.cpp`: `REQUIRE(b == 10);` → `11` (and the comment).

- [ ] **Step 9: Build + full suite**

Run: `cmake --build build/debug --target mosaic_unit_tests && ctest --test-dir build/debug`
Expected: PASS (glslc compiles the decal shaders; the four count tests now expect 11). Re-run the `ScriptWorker` flake if needed.

- [ ] **Step 10: Build the app + smoke run**

Run: `cmake --build build/debug --target mosaic && ./build/debug/app/mosaic run --window --ticks 90 2>&1 | tail -6; echo "exit: ${PIPESTATUS[0]}"`
Expected: exit 0, no new Vulkan validation errors. (A striped decal now lies on the ground — maintainer confirms the visual.)

- [ ] **Step 11: Commit**

```bash
git add modules/render_vulkan app/run/demo_scene.hpp tests/unit/app/demo_scene_test.cpp tests/unit/app/populate_world_test.cpp tests/unit/app/bundle_test.cpp tests/unit/app/scene_save_roundtrip_test.cpp
git commit -m "feat(vfx): Vulkan decal pipeline + draw_decals; demo ground decal

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Self-Review

**Spec coverage:**
- `Decal` reflected component (texture/color/alpha) + builtin → Task 1 Steps 1,3; tests Step 4. ✓
- `DecalInstance` + `draw_decals` no-op seam → Task 1 Step 2. ✓
- RenderSystem decal pass: resolve texture via `texture_cache_`, compose `projection*view*world`, submit → Task 2 Step 2; test Step 3. ✓
- Vulkan textured alpha-blended shader-quad pipeline reusing `texture_set_layout_`, per-decal descriptor bind + push, no vertex buffer → Task 3 Steps 1-7. ✓
- Demo ground decal + count-test updates (10→11 ×4) → Task 3 Step 8. ✓
- Build + smoke; visual = maintainer → Task 3 Steps 9-10. ✓
- next-steps NOT touched this slice (rides in the screen-effects PR). ✓

**Placeholder scan:** No TBD/TODO; every code step has complete code. ✓

**Type consistency:** `DecalInstance{Mat4 mvp; TextureHandle texture; Vec3 color; float alpha}` identical in `render_device.hpp`, the render pass, the fake device, and the Vulkan override; `draw_decals(const std::vector<DecalInstance>&)` consistent; `DecalPush{float[16],float[4]}` (80) matches the shader push (`mat4`+`vec4`); pipeline reuses `texture_set_layout_` + `textures_[i].set` exactly like `draw()`; `Quat::from_axis_angle` matches `foundation/math/quat.hpp`. ✓
