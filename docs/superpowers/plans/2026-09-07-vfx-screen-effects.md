# VFX V4 — screen effects (fullscreen overlay) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A `ScreenEffect` component drives a fullscreen overlay (fade/tint/vignette), completing the VFX pillar.

**Architecture:** `RenderSystem` finds the first `ScreenEffect` and, if active, calls a new `draw_screen_effect` seam last in the pass. The Vulkan side is a fullscreen-triangle, alpha-blended, depth-off pipeline (no vertex buffer, no descriptor set) whose fragment composites a tint-fade and a radial vignette into one blend. Overlay-only — no scene sampling / render-graph change.

**Tech Stack:** C++20, Vulkan (glslc via CMake), Catch2. No new dependencies.

**Spec:** [docs/superpowers/specs/2026-09-07-vfx-screen-effects-design.md](2026-09-07-vfx-screen-effects-design.md)

## Global Constraints

- Overlay-only (fade/tint/vignette); no scene-texture sampling. Drawn **last** (over meshes/decals/particles/ribbons).
- `ScreenEffect { Vec3 tint; float tint_strength; float vignette; }`, defaults all zero (no effect); registered as a builtin.
- RenderSystem uses the **first** `ScreenEffect`; skips the draw when `tint_strength == 0 && vignette == 0`.
- Vulkan: fullscreen triangle (`vkCmdDraw(3,1,0,0)`), no vertex input, no descriptor set; alpha blend (same factors as particles); depth test + write **off**; `VK_CULL_MODE_NONE`; two variants (swapchain + viewport); `ScreenEffectPush { float tint[4]; float params[4]; }` (32 bytes; tint[3]=strength, params[0]=vignette), fragment stage.
- Frag composite: `vig = smoothstep(0.5,1.0, distance(uv,0.5)/0.7071) * vignette`; `a = clamp(strength + vig, 0,1)`; `rgb = a>0 ? tint*strength/a : tint`; `outColor = vec4(rgb, a)`.
- GPU visual = maintainer's check; here headless wiring + build + `run --window` exit 0.
- **This slice DOES update `docs/project/next-steps.md`** (the VFX-complete wrap-up).
- Tests `[vfx]`/`[render]`; build target `mosaic_unit_tests`; preset `build/debug`.

---

### Task 1: `ScreenEffect` component + seam

**Files:**
- Create: `engine/vfx/screen_effect.hpp`
- Modify: `engine/render/render_device.hpp` (`ScreenEffectParams` + `draw_screen_effect` no-op)
- Modify: `engine/reflection/builtin_components.hpp`
- Create: `tests/unit/vfx/screen_effect_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `mosaic::engine::vfx::ScreenEffect{ Vec3 tint; float tint_strength; float vignette; }` + `reflect`; `mosaic::engine::render::ScreenEffectParams{ Vec3 tint; float tint_strength; float vignette; }`; `RenderDevice::draw_screen_effect(const ScreenEffectParams&)`.

- [ ] **Step 1: Create `engine/vfx/screen_effect.hpp`**

```cpp
#pragma once

#include "foundation/math/vec3.hpp"

namespace mosaic::engine::vfx
{
using mosaic::foundation::math::Vec3;

// Drives a fullscreen overlay: fade/flash/grade (tint + tint_strength) and vignette.
// Defaults = no visible effect. A normal component, so scripts can animate tint_strength.
struct ScreenEffect
{
    Vec3  tint          = Vec3{0, 0, 0}; // color the screen blends toward
    float tint_strength = 0.0f;          // 0 = none, 1 = fully tint
    float vignette      = 0.0f;          // 0 = none, 1 = strong edge darkening
};

template <class V>
void reflect(V& visitor, ScreenEffect& s)
{
    visitor.member("tint", s.tint);
    visitor.member("tint_strength", s.tint_strength);
    visitor.member("vignette", s.vignette);
}
} // namespace mosaic::engine::vfx
```

- [ ] **Step 2: Add the seam to `render_device.hpp`**

In `engine/render/render_device.hpp`, after the `DecalInstance` struct, add:

```cpp
// Fullscreen post overlay parameters (fade/tint/vignette).
struct ScreenEffectParams
{
    mosaic::foundation::math::Vec3 tint;
    float                          tint_strength = 0.0f;
    float                          vignette      = 0.0f;
};
```

and in `class RenderDevice`, after `draw_decals`, add:

```cpp
    // Draw a fullscreen fade/tint/vignette overlay. Default no-op (like draw_particles).
    virtual void draw_screen_effect(const ScreenEffectParams& /*params*/) {}
```

- [ ] **Step 3: Register `ScreenEffect` as a builtin**

In `engine/reflection/builtin_components.hpp`, add the include:

```cpp
#include "engine/vfx/screen_effect.hpp"
```

and, after the `Decal` registration line, add:

```cpp
    types.register_component<mosaic::engine::vfx::ScreenEffect>(registry, "ScreenEffect");
```

- [ ] **Step 4: Create `tests/unit/vfx/screen_effect_test.cpp`**

```cpp
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/vfx/screen_effect.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reflect_value.hpp"

using Catch::Approx;
using mosaic::engine::vfx::ScreenEffect;
using mosaic::foundation::math::Vec3;

TEST_CASE("ScreenEffect round-trips through reflection", "[vfx]")
{
    ScreenEffect s;
    s.tint          = Vec3{1.0f, 0.2f, 0.1f};
    s.tint_strength = 0.7f;
    s.vignette      = 0.3f;

    const auto value = mosaic::foundation::serialization::to_value(s);
    ScreenEffect back;
    mosaic::foundation::serialization::from_value(value, back);

    REQUIRE(back.tint.x == Approx(1.0f));
    REQUIRE(back.tint_strength == Approx(0.7f));
    REQUIRE(back.vignette == Approx(0.3f));
}

TEST_CASE("ScreenEffect is a registered built-in component", "[vfx]")
{
    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    REQUIRE(types.has("ScreenEffect"));
}
```

- [ ] **Step 5: Add the test file to `tests/CMakeLists.txt`**

After `  unit/vfx/decal_test.cpp`, add:

```cmake
  unit/vfx/screen_effect_test.cpp
```

- [ ] **Step 6: Build + run**

Run: `cmake --build build/debug --target mosaic_unit_tests && ctest --test-dir build/debug -R "ScreenEffect" --output-on-failure`
Expected: PASS — reflection round-trip + builtin registration.

- [ ] **Step 7: Commit**

```bash
git add engine/vfx/screen_effect.hpp engine/render/render_device.hpp engine/reflection/builtin_components.hpp tests/unit/vfx/screen_effect_test.cpp tests/CMakeLists.txt
git commit -m "feat(vfx): ScreenEffect component + draw_screen_effect seam

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: RenderSystem overlay pass

**Files:**
- Modify: `engine/render/render_system.hpp`
- Modify: `tests/unit/render/render_system_test.cpp`

**Interfaces:**
- Consumes: `ScreenEffect`, `ScreenEffectParams`/`draw_screen_effect` (Task 1).

- [ ] **Step 1: Include the header**

In `engine/render/render_system.hpp`, add near the other includes:

```cpp
#include "engine/vfx/screen_effect.hpp"
```

- [ ] **Step 2: Add the overlay pass at the end of `render`**

In `engine/render/render_system.hpp`, at the very end of the `render(...)` method body
(after the billboard/decal passes, immediately before the method's closing brace), add:

```cpp
        // Screen-effect overlay: fullscreen fade/tint/vignette, drawn last (over everything).
        {
            bool done = false;
            world.for_each<mosaic::engine::vfx::ScreenEffect>(
                [&](mosaic::foundation::EntityID, mosaic::engine::vfx::ScreenEffect& se)
                {
                    if (done) { return; }
                    done = true;
                    if (se.tint_strength > 0.0f || se.vignette > 0.0f)
                    {
                        device.draw_screen_effect(
                            ScreenEffectParams{se.tint, se.tint_strength, se.vignette});
                    }
                });
        }
```

- [ ] **Step 3: Extend `FakeRenderDevice` + add render tests**

In `tests/unit/render/render_system_test.cpp`, add the using after the decal using:

```cpp
using mosaic::engine::render::ScreenEffectParams;
```

In `FakeRenderDevice`, after the `decal_draws` members, add:

```cpp
    std::vector<ScreenEffectParams> screen_effect_draws;
    void draw_screen_effect(const ScreenEffectParams& params) override
    {
        screen_effect_draws.push_back(params);
    }
```

Then append:

```cpp
TEST_CASE("RenderSystem draws a screen effect when active", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);
    add_camera(world, Vec3{0, 0, 4});

    auto e = world.create_entity();
    mosaic::engine::vfx::ScreenEffect se;
    se.tint          = Vec3{1.0f, 0.0f, 0.0f};
    se.tint_strength = 0.5f;
    world.add_component<mosaic::engine::vfx::ScreenEffect>(e, se);

    rs.render(world, device, registry);
    REQUIRE(device.screen_effect_draws.size() == 1);
    REQUIRE(device.screen_effect_draws[0].tint_strength == Approx(0.5f));
    REQUIRE(device.screen_effect_draws[0].tint.x == Approx(1.0f));
}

TEST_CASE("RenderSystem skips an inactive (all-zero) screen effect", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);
    add_camera(world, Vec3{0, 0, 4});

    auto e = world.create_entity();
    world.add_component<mosaic::engine::vfx::ScreenEffect>(e, mosaic::engine::vfx::ScreenEffect{});

    rs.render(world, device, registry);
    REQUIRE(device.screen_effect_draws.empty());
}
```

- [ ] **Step 4: Build + run render tests**

Run: `cmake --build build/debug --target mosaic_unit_tests && ctest --test-dir build/debug -R "RenderSystem|screen effect" --output-on-failure`
Expected: PASS — the two overlay cases + all existing `[render]` cases.

- [ ] **Step 5: Commit**

```bash
git add engine/render/render_system.hpp tests/unit/render/render_system_test.cpp
git commit -m "feat(vfx): RenderSystem screen-effect overlay pass (draw_screen_effect)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: Vulkan overlay pipeline + demo + VFX wrap-up

**Files:**
- Create: `modules/render_vulkan/shaders/screen_effect.vert`, `.../screen_effect.frag`
- Modify: `modules/render_vulkan/CMakeLists.txt`
- Modify: `modules/render_vulkan/vulkan_render_device.cpp`
- Modify: `app/run/demo_scene.hpp`
- Modify: `docs/project/next-steps.md`

**Interfaces:**
- Consumes: `ScreenEffectParams`/`draw_screen_effect` (Task 1).

- [ ] **Step 1: Create the shaders**

`modules/render_vulkan/shaders/screen_effect.vert`:

```glsl
#version 450

layout(location = 0) out vec2 fragUV;

void main()
{
    // Fullscreen triangle from gl_VertexIndex (0,1,2).
    vec2 pos = vec2((gl_VertexIndex == 1) ? 3.0 : -1.0, (gl_VertexIndex == 2) ? 3.0 : -1.0);
    gl_Position = vec4(pos, 0.0, 1.0);
    fragUV = pos * 0.5 + 0.5;
}
```

`modules/render_vulkan/shaders/screen_effect.frag`:

```glsl
#version 450

layout(push_constant) uniform Push { vec4 tint; vec4 params; } pc; // tint.w = strength, params.x = vignette

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main()
{
    float strength = pc.tint.w;
    float vignette = pc.params.x;
    float r   = distance(fragUV, vec2(0.5)) / 0.7071;
    float vig = smoothstep(0.5, 1.0, r) * vignette;
    float a   = clamp(strength + vig, 0.0, 1.0);
    vec3  rgb = (a > 0.0) ? (pc.tint.rgb * strength) / a : pc.tint.rgb; // black*vig adds 0 to rgb
    outColor  = vec4(rgb, a);
}
```

- [ ] **Step 2: Compile the shaders in CMake**

In `modules/render_vulkan/CMakeLists.txt`, extend the glslc `foreach` list to end with `decal.vert decal.frag screen_effect.vert screen_effect.frag`.

- [ ] **Step 3: Add the `ScreenEffectPush` struct**

In `modules/render_vulkan/vulkan_render_device.cpp`, after the `DecalPush` struct + its
`static_assert`, add:

```cpp
struct ScreenEffectPush
{
    float tint[4];   // rgb = tint, a = strength
    float params[4]; // x = vignette
};
static_assert(sizeof(ScreenEffectPush) == 32, "screen-effect push constant must match the shader");
```

- [ ] **Step 4: Add member handles**

After the `decal_pipeline_layout_` member, add:

```cpp
    VkPipeline       screen_effect_pipeline_ = VK_NULL_HANDLE;
    VkPipeline       vp_screen_effect_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout screen_effect_pipeline_layout_ = VK_NULL_HANDLE;
```

- [ ] **Step 5: Create the pipeline at init + the method**

After the `create_decal_pipeline();` call in the constructor, add `create_screen_effect_pipeline();`.
Then add the method after `create_decal_pipeline`'s closing brace:

```cpp
    void create_screen_effect_pipeline()
    {
        std::vector<char> vert_code = read_file(std::string(MOSAIC_SHADER_DIR) + "/screen_effect.vert.spv");
        std::vector<char> frag_code = read_file(std::string(MOSAIC_SHADER_DIR) + "/screen_effect.frag.spv");
        VkShaderModule vert = create_shader_module(vert_code);
        VkShaderModule frag = create_shader_module(frag_code);

        VkPipelineShaderStageCreateInfo vs{};
        vs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vs.stage = VK_SHADER_STAGE_VERTEX_BIT; vs.module = vert; vs.pName = "main";
        VkPipelineShaderStageCreateInfo fs{};
        fs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fs.stage = VK_SHADER_STAGE_FRAGMENT_BIT; fs.module = frag; fs.pName = "main";
        VkPipelineShaderStageCreateInfo stages[] = {vs, fs};

        VkPipelineVertexInputStateCreateInfo vin{};
        vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO; // no inputs

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
        rast.cullMode = VK_CULL_MODE_NONE;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable = VK_FALSE;  // overlay is always on top
        depth.depthWriteEnable = VK_FALSE;
        depth.depthCompareOp = VK_COMPARE_OP_ALWAYS;

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
        push.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        push.offset = 0; push.size = sizeof(ScreenEffectPush);

        VkPipelineLayoutCreateInfo layout{};
        layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout.setLayoutCount = 0;
        layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &push;
        vk_check(vkCreatePipelineLayout(device_, &layout, nullptr, &screen_effect_pipeline_layout_),
                 "vkCreatePipelineLayout(screen_effect)");

        VkGraphicsPipelineCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pi.stageCount = 2; pi.pStages = stages;
        pi.pVertexInputState = &vin; pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp; pi.pRasterizationState = &rast;
        pi.pMultisampleState = &ms; pi.pDepthStencilState = &depth;
        pi.pColorBlendState = &cb; pi.pDynamicState = &dyn;
        pi.layout = screen_effect_pipeline_layout_;
        pi.renderPass = render_pass_; pi.subpass = 0;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &screen_effect_pipeline_),
                 "vkCreateGraphicsPipelines(screen_effect)");

        pi.renderPass = vp_render_pass_;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &vp_screen_effect_pipeline_),
                 "vkCreateGraphicsPipelines(screen_effect offscreen)");

        vkDestroyShaderModule(device_, frag, nullptr);
        vkDestroyShaderModule(device_, vert, nullptr);
    }
```

- [ ] **Step 6: Implement `draw_screen_effect`**

After the `draw_decals` method's closing brace, add:

```cpp
    void draw_screen_effect(const mosaic::engine::render::ScreenEffectParams& params) override
    {
        if (!viewport_pass_open_) { ensure_swapchain_pass(); }
        if (frame_skipped_) { return; }

        vkCmdBindPipeline(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                          viewport_pass_open_ ? vp_screen_effect_pipeline_ : screen_effect_pipeline_);

        ScreenEffectPush pc{};
        pc.tint[0] = params.tint.x; pc.tint[1] = params.tint.y; pc.tint[2] = params.tint.z;
        pc.tint[3] = params.tint_strength;
        pc.params[0] = params.vignette;
        vkCmdPushConstants(cmd_[frame_], screen_effect_pipeline_layout_,
                           VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

        vkCmdDraw(cmd_[frame_], 3, 1, 0, 0);
    }
```

- [ ] **Step 7: Destroy the pipeline in teardown**

In the cleanup block, right after the decal pipeline destroys, add:

```cpp
            if (vp_screen_effect_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, vp_screen_effect_pipeline_, nullptr); }
            if (screen_effect_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, screen_effect_pipeline_, nullptr); }
            if (screen_effect_pipeline_layout_ != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, screen_effect_pipeline_layout_, nullptr); }
```

- [ ] **Step 8: Add the demo ScreenEffect**

In `app/run/demo_scene.hpp`, add the include near the top:

```cpp
#include "engine/vfx/screen_effect.hpp"
```

Then, in `build_default_scene`, immediately after the camera's `AudioListener` is added
(`world.add_component<mosaic::engine::audio::AudioListener>(camera, ...);`), add:

```cpp
    world.add_component<mosaic::engine::vfx::ScreenEffect>(camera,
        mosaic::engine::vfx::ScreenEffect{Vec3{0, 0, 0}, 0.0f, 0.4f}); // subtle vignette
```

(Attaches to the existing camera entity — default-scene counts are unchanged.)

- [ ] **Step 9: Build + full suite**

Run: `cmake --build build/debug --target mosaic_unit_tests && ctest --test-dir build/debug`
Expected: PASS (glslc compiles the screen-effect shaders; counts unchanged — the effect is on the existing camera). Re-run the `ScriptWorker` flake if needed.

- [ ] **Step 10: Build the app + smoke run**

Run: `cmake --build build/debug --target mosaic && ./build/debug/app/mosaic run --window --ticks 90 2>&1 | tail -6; echo "exit: ${PIPESTATUS[0]}"`
Expected: exit 0, no new Vulkan validation errors. (A subtle vignette now darkens the frame edges — maintainer confirms the visual.)

- [ ] **Step 11: Update the handoff doc (VFX pillar complete)**

In `docs/project/next-steps.md`: add a **VFX — V4 (screen effects) DONE** note and mark
the **VFX pillar complete (trails, decals, screen effects)**. Screen effects: a
`ScreenEffect` component (tint/tint_strength/vignette) drawn as a fullscreen alpha
overlay (fade/flash/grade + vignette) via a new `draw_screen_effect` seam + a
fullscreen-triangle Vulkan pipeline; demo vignette on the camera. Note VFX follow-ups:
scene-sampling post (bloom/blur/DOF), projected decals, ribbon texture/curves. Update
the top "Last updated" summary. **Remaining Phase-4 subsystems: UI, networking.**

- [ ] **Step 12: Commit**

```bash
git add modules/render_vulkan app/run/demo_scene.hpp docs/project/next-steps.md
git commit -m "feat(vfx): Vulkan fullscreen screen-effect pipeline + demo vignette; VFX pillar complete

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Self-Review

**Spec coverage:**
- `ScreenEffect` reflected component + builtin → Task 1 Steps 1,3; tests Step 4. ✓
- `ScreenEffectParams` + `draw_screen_effect` no-op seam → Task 1 Step 2. ✓
- RenderSystem uses first ScreenEffect, skips all-zero, draws last → Task 2 Step 2; tests Step 3. ✓
- Vulkan fullscreen-triangle overlay pipeline (no inputs/descriptors, depth off, alpha blend, composite frag) → Task 3 Steps 1-7. ✓
- Demo camera vignette (counts unchanged) → Task 3 Step 8. ✓
- Build + smoke; visual = maintainer → Task 3 Steps 9-10. ✓
- next-steps VFX-complete wrap-up in this slice → Task 3 Step 11. ✓

**Placeholder scan:** No TBD/TODO; every code step has complete code. ✓

**Type consistency:** `ScreenEffect{Vec3 tint; float tint_strength; float vignette}` and `ScreenEffectParams{Vec3 tint; float tint_strength; float vignette}` used identically in the component, render pass, fake device, and Vulkan override; `draw_screen_effect(const ScreenEffectParams&)` consistent; `ScreenEffectPush{float[4],float[4]}` (32) matches the shader push (`vec4`+`vec4`); pipeline needs no descriptor set / vertex input (fullscreen triangle, `vkCmdDraw(3,...)`). ✓
