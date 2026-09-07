# VFX V2 — ribbon trails — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render trails as a connected, camera-facing, tapered ribbon (replacing the V1 billboard chain), via a new alpha-blended Vulkan triangle pipeline.

**Architecture:** The engine CPU-expands each trail's point history into world-space triangles (`build_ribbon`, headless-testable) and submits them through a new `draw_ribbons` device seam. The Vulkan side is a trivial world-space triangle pipeline (view_proj push constant; per-vertex pos/color/alpha), modeled on the particle pipeline. `TrailRenderer`/`TrailSystem` are unchanged.

**Tech Stack:** C++20, Vulkan (glslc via CMake), Catch2. No new dependencies.

**Spec:** [docs/superpowers/specs/2026-09-07-vfx-ribbon-trails-design.md](2026-09-07-vfx-ribbon-trails-design.md)

## Global Constraints

- CPU-expand in the engine; the Vulkan pipeline just transforms pre-expanded world-space triangles (no billboard/instance math).
- `build_ribbon` is pure; `< 2` points appends nothing; taper `frac = (i+1)/n` (newest full, oldest faint); side `= normalize(cross(seg_dir, dir_to_camera))` with fallbacks `cross(seg,{0,1,0})` then `cross(seg,{1,0,0})`.
- Ribbon **replaces** billboard-chain trail rendering; particles still use `draw_particles`.
- `RibbonVertex { Vec3 position; Vec3 color; float alpha; }` in `render_device.hpp`; triangle **LIST** (6 verts/segment) so all trails concatenate into one draw.
- GPU output is verified by the maintainer on hardware; here: headless geometry + `FakeRenderDevice` call + build + `mosaic run --window` exit 0.
- Vulkan pipeline modeled exactly on the particle pipeline (two variants: swapchain + viewport; alpha blend; depth test on, depth write off).
- Tests `[vfx]`/`[render]`; build target `mosaic_unit_tests`; preset `build/debug`.

---

### Task 1: `RibbonVertex` seam + `build_ribbon` geometry

**Files:**
- Modify: `engine/render/render_device.hpp` (add `RibbonVertex`, `RibbonDrawParams`, `draw_ribbons` no-op)
- Create: `engine/vfx/ribbon.hpp` (`build_ribbon`)
- Create: `tests/unit/vfx/ribbon_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `mosaic::engine::render::RibbonVertex{Vec3 position; Vec3 color; float alpha}`, `RibbonDrawParams{Mat4 view_proj}`, `RenderDevice::draw_ribbons(...)`; `mosaic::engine::vfx::build_ribbon(const std::vector<Vec3>&, const Vec3& camera_pos, float width, const Vec3& color, std::vector<RibbonVertex>& out)`.

- [ ] **Step 1: Add the ribbon seam to `render_device.hpp`**

In `engine/render/render_device.hpp`, after the `ParticleDrawParams` struct, add:

```cpp
// One vertex of a ribbon strip (pre-expanded, world-space). alpha is the taper factor.
struct RibbonVertex
{
    mosaic::foundation::math::Vec3 position;
    mosaic::foundation::math::Vec3 color;
    float                          alpha = 1.0f;
};

// Camera data for ribbons. view_proj = projection * view (vertices are already world-space).
struct RibbonDrawParams
{
    mosaic::foundation::math::Mat4 view_proj;
};
```

and in `class RenderDevice`, after the `draw_particles` declaration, add:

```cpp
    // Draw pre-expanded, alpha-blended ribbon triangles (world-space). Default no-op so
    // headless devices and test fakes need not implement it (like draw_particles).
    virtual void draw_ribbons(const std::vector<RibbonVertex>& /*vertices*/,
                              const RibbonDrawParams& /*params*/) {}
```

- [ ] **Step 2: Create `engine/vfx/ribbon.hpp`**

```cpp
#pragma once

#include <vector>

#include "engine/render/render_device.hpp" // RibbonVertex
#include "foundation/math/vec3.hpp"

namespace mosaic::engine::vfx
{
using mosaic::foundation::math::Vec3;

// Append a camera-facing ribbon (triangle LIST, 6 verts per segment) for `points`
// (oldest -> newest) to `out`. `width` is the head (newest) width; width and alpha taper
// from ~0 at the oldest end to full at the newest (frac = (i+1)/n). Fewer than 2 points
// appends nothing. `camera_pos` orients each segment to face the viewer.
inline void build_ribbon(const std::vector<Vec3>& points, const Vec3& camera_pos, float width,
                         const Vec3& color, std::vector<mosaic::engine::render::RibbonVertex>& out)
{
    const int n = static_cast<int>(points.size());
    if (n < 2) { return; }

    auto half_at  = [&](int j) { return 0.5f * width * (static_cast<float>(j) + 1.0f) / n; };
    auto alpha_at = [&](int j) { return (static_cast<float>(j) + 1.0f) / n; };

    for (int i = 0; i + 1 < n; ++i)
    {
        const Vec3 seg = mosaic::foundation::math::normalize(points[i + 1] - points[i]);
        Vec3 to_cam = mosaic::foundation::math::normalize(camera_pos - points[i]);
        Vec3 side = mosaic::foundation::math::cross(seg, to_cam);
        if (mosaic::foundation::math::length(side) < 1e-5f)
        {
            side = mosaic::foundation::math::cross(seg, Vec3{0.0f, 1.0f, 0.0f});
        }
        if (mosaic::foundation::math::length(side) < 1e-5f)
        {
            side = mosaic::foundation::math::cross(seg, Vec3{1.0f, 0.0f, 0.0f});
        }
        side = mosaic::foundation::math::normalize(side);

        const Vec3 s_i  = side * half_at(i);
        const Vec3 s_i1 = side * half_at(i + 1);
        const mosaic::engine::render::RibbonVertex a0{points[i] + s_i, color, alpha_at(i)};
        const mosaic::engine::render::RibbonVertex a1{points[i] - s_i, color, alpha_at(i)};
        const mosaic::engine::render::RibbonVertex b0{points[i + 1] + s_i1, color, alpha_at(i + 1)};
        const mosaic::engine::render::RibbonVertex b1{points[i + 1] - s_i1, color, alpha_at(i + 1)};

        out.push_back(a0); out.push_back(a1); out.push_back(b1); // triangle 1
        out.push_back(a0); out.push_back(b1); out.push_back(b0); // triangle 2
    }
}
} // namespace mosaic::engine::vfx
```

- [ ] **Step 3: Create `tests/unit/vfx/ribbon_test.cpp`**

```cpp
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "engine/vfx/ribbon.hpp"

using Catch::Approx;
using mosaic::engine::render::RibbonVertex;
using mosaic::engine::vfx::build_ribbon;
using mosaic::foundation::math::Vec3;

TEST_CASE("build_ribbon emits 6 vertices per segment; <2 points emits nothing", "[vfx]")
{
    std::vector<RibbonVertex> out;
    build_ribbon({}, Vec3{0, 5, 0}, 1.0f, Vec3{1, 1, 1}, out);
    REQUIRE(out.empty());
    build_ribbon({Vec3{0, 0, 0}}, Vec3{0, 5, 0}, 1.0f, Vec3{1, 1, 1}, out);
    REQUIRE(out.empty());
    build_ribbon({Vec3{0, 0, 0}, Vec3{1, 0, 0}}, Vec3{0.5f, 5, 0}, 1.0f, Vec3{1, 1, 1}, out);
    REQUIRE(out.size() == 6);                 // one segment -> one quad
    std::vector<RibbonVertex> three;
    build_ribbon({Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{2, 0, 0}}, Vec3{1, 5, 0}, 1.0f,
                 Vec3{1, 1, 1}, three);
    REQUIRE(three.size() == 12);              // two segments -> two quads
}

TEST_CASE("build_ribbon tapers alpha and width toward the newest point", "[vfx]")
{
    std::vector<RibbonVertex> out;
    // 4 points along +X; camera above. n=4 -> alphas 0.25,0.5,0.75,1.0 by point index.
    build_ribbon({Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{2, 0, 0}, Vec3{3, 0, 0}}, Vec3{1.5f, 5, 0},
                 2.0f, Vec3{1, 1, 1}, out);
    // First emitted vertex is at the oldest end (point 0): alpha = 1/4.
    REQUIRE(out.front().alpha == Approx(0.25f));
    // Last emitted vertex belongs to the newest point (point 3): alpha = 1.0.
    REQUIRE(out.back().alpha == Approx(1.0f));
}

TEST_CASE("build_ribbon offsets perpendicular to the segment and the view direction", "[vfx]")
{
    std::vector<RibbonVertex> out;
    // Segment along +X, camera straight above (+Y): side should be along Z.
    build_ribbon({Vec3{0, 0, 0}, Vec3{1, 0, 0}}, Vec3{0.5f, 5, 0}, 2.0f, Vec3{1, 1, 1}, out);
    const Vec3 seg{1, 0, 0};
    const Vec3 center{0, 0, 0};
    const Vec3 offset = out[0].position - center; // a0 = point0 + side*half
    REQUIRE(mosaic::foundation::math::dot(offset, seg) == Approx(0.0f).margin(1e-4)); // perp to segment
    REQUIRE(offset.y == Approx(0.0f).margin(1e-4));                                    // perp to view (up)
    REQUIRE(std::abs(offset.z) > 0.1f);                                                // spread along Z
}
```

- [ ] **Step 4: Add the test file to `tests/CMakeLists.txt`**

After `  unit/vfx/trail_system_test.cpp`, add:

```cmake
  unit/vfx/ribbon_test.cpp
```

- [ ] **Step 5: Build + run**

Run: `cmake --build build/debug --target mosaic_unit_tests && ctest --test-dir build/debug -R "build_ribbon" --output-on-failure`
Expected: PASS — the three geometry cases.

- [ ] **Step 6: Commit**

```bash
git add engine/render/render_device.hpp engine/vfx/ribbon.hpp tests/unit/vfx/ribbon_test.cpp tests/CMakeLists.txt
git commit -m "feat(vfx): RibbonVertex/draw_ribbons seam + build_ribbon geometry

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: RenderSystem ribbon pass

**Files:**
- Modify: `engine/render/render_system.hpp` (trail → ribbon pass)
- Modify: `tests/unit/render/render_system_test.cpp` (FakeRenderDevice + replace the V1 trail test)

**Interfaces:**
- Consumes: `build_ribbon`, `RibbonVertex`/`RibbonDrawParams`, `draw_ribbons`, `TrailSystem::points_for` (Task 1 + V1).

- [ ] **Step 1: Include the ribbon header**

In `engine/render/render_system.hpp`, add near the other includes:

```cpp
#include "engine/vfx/ribbon.hpp"
```

- [ ] **Step 2: Remove the trail-billboard sub-loop and add a ribbon pass**

In `engine/render/render_system.hpp`, in the billboard block, **delete** the trail loop
added in V1 (the `if (trails != nullptr) { world.for_each<...TrailRenderer>( ... push_back
ParticleInstance ... ); }` sub-block) so the particle batch is particles-only again. Keep
`auto* trails = world.get_system<mosaic::engine::vfx::TrailSystem>();`.

Then, immediately after the `if (!instances.empty()) { device.draw_particles(...); }` line
(still inside the `if (particles != nullptr || trails != nullptr)` block), add the ribbon
pass:

```cpp
            if (trails != nullptr)
            {
                const Vec3 cam_pos{cam_world.m[12], cam_world.m[13], cam_world.m[14]};
                std::vector<RibbonVertex> ribbon;
                world.for_each<mosaic::engine::vfx::TrailRenderer>(
                    [&](mosaic::foundation::EntityID e, mosaic::engine::vfx::TrailRenderer& tr)
                    {
                        const auto* pts = trails->points_for(e);
                        if (pts == nullptr) { return; }
                        mosaic::engine::vfx::build_ribbon(*pts, cam_pos, tr.size, tr.color, ribbon);
                    });
                if (!ribbon.empty())
                {
                    device.draw_ribbons(ribbon, RibbonDrawParams{projection * view});
                }
            }
```

(`cam_world`, `projection`, `view` are already in scope from the billboard block.)

- [ ] **Step 3: Extend `FakeRenderDevice` + replace the V1 trail test**

In `tests/unit/render/render_system_test.cpp`, in `FakeRenderDevice` (after the
`particle_draws` members) add:

```cpp
    struct RibbonDrawCall { std::vector<RibbonVertex> vertices; RibbonDrawParams params; };
    std::vector<RibbonDrawCall> ribbon_draws;
    void draw_ribbons(const std::vector<RibbonVertex>& vertices,
                      const RibbonDrawParams& params) override
    {
        ribbon_draws.push_back({vertices, params});
    }
```

Then **replace** the existing `TEST_CASE("RenderSystem draws a trail as tapered billboards", "[render]")`
(added in V1) with:

```cpp
TEST_CASE("RenderSystem draws a trail as a ribbon", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);
    add_camera(world, Vec3{0, 0, 4});
    world.add_system<mosaic::engine::vfx::TrailSystem>();

    auto e = world.create_entity();
    world.add_component<mosaic::engine::transform::Transform>(
        e, mosaic::engine::transform::Transform{});
    mosaic::engine::vfx::TrailRenderer tr;
    tr.size            = 0.2f;
    tr.color           = Vec3{0.1f, 0.2f, 0.3f};
    tr.sample_interval = 0.05f;
    tr.max_points      = 10;
    world.add_component<mosaic::engine::vfx::TrailRenderer>(e, tr);

    for (int i = 0; i < 4; ++i) { world.tick(0.05f); } // seed + 3 samples -> 4 points -> 3 segments

    rs.render(world, device, registry);
    REQUIRE(device.ribbon_draws.size() == 1);
    REQUIRE(device.ribbon_draws[0].vertices.size() == 6 * 3); // 6 verts/segment, 3 segments
    // Trails no longer contribute to the particle batch.
    REQUIRE(device.particle_draws.empty());
}
```

- [ ] **Step 4: Build + run render tests**

Run: `cmake --build build/debug --target mosaic_unit_tests && ctest --test-dir build/debug -R "RenderSystem|draws a trail as a ribbon" --output-on-failure`
Expected: PASS — the new ribbon test + all existing particle `[render]` cases (unchanged).

- [ ] **Step 5: Commit**

```bash
git add engine/render/render_system.hpp tests/unit/render/render_system_test.cpp
git commit -m "feat(vfx): render trails as ribbons (draw_ribbons) instead of billboards

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: Vulkan ribbon pipeline + verify + handoff

**Files:**
- Create: `modules/render_vulkan/shaders/ribbon.vert`, `modules/render_vulkan/shaders/ribbon.frag`
- Modify: `modules/render_vulkan/CMakeLists.txt` (glslc list)
- Modify: `modules/render_vulkan/vulkan_render_device.cpp` (push struct, pipeline, draw, members, init, cleanup)
- Modify: `docs/project/next-steps.md`

**Interfaces:**
- Consumes: `RibbonVertex`/`RibbonDrawParams`/`draw_ribbons` (Task 1).

- [ ] **Step 1: Create the shaders**

`modules/render_vulkan/shaders/ribbon.vert`:

```glsl
#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;
layout(location = 2) in float inAlpha;

layout(push_constant) uniform Push { mat4 view_proj; } pc;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out float fragAlpha;

void main()
{
    gl_Position = pc.view_proj * vec4(inPos, 1.0);
    fragColor = inColor;
    fragAlpha = inAlpha;
}
```

`modules/render_vulkan/shaders/ribbon.frag`:

```glsl
#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in float fragAlpha;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(fragColor, fragAlpha);
}
```

- [ ] **Step 2: Compile the shaders in CMake**

In `modules/render_vulkan/CMakeLists.txt`, change the glslc `foreach` line:

```cmake
foreach(_stage cube.vert cube.frag particle.vert particle.frag)
```

to:

```cmake
foreach(_stage cube.vert cube.frag particle.vert particle.frag ribbon.vert ribbon.frag)
```

- [ ] **Step 3: Add the `RibbonPush` struct**

In `modules/render_vulkan/vulkan_render_device.cpp`, after the `ParticlePush` struct + its
`static_assert`, add:

```cpp
struct RibbonPush
{
    float view_proj[16];
};
static_assert(sizeof(RibbonPush) == 64, "ribbon push constant layout must match the shader");
```

- [ ] **Step 4: Add member handles + per-frame vertex buffers**

In `modules/render_vulkan/vulkan_render_device.cpp`, after the
`VkPipelineLayout particle_pipeline_layout_ = VK_NULL_HANDLE;` member, add:

```cpp
    VkPipeline       ribbon_pipeline_ = VK_NULL_HANDLE;
    VkPipeline       vp_ribbon_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout ribbon_pipeline_layout_ = VK_NULL_HANDLE;
```

and after the `std::array<InstanceBuffer, kFramesInFlight> particle_instances_{};` member, add:

```cpp
    std::array<InstanceBuffer, kFramesInFlight> ribbon_vertices_{};
```

- [ ] **Step 5: Create the pipeline at init**

In `modules/render_vulkan/vulkan_render_device.cpp`, after the `create_particle_pipeline();`
call in the constructor, add:

```cpp
        create_ribbon_pipeline();
```

Then add the `create_ribbon_pipeline` method (place it right after `create_particle_pipeline`'s
closing brace):

```cpp
    void create_ribbon_pipeline()
    {
        std::vector<char> vert_code = read_file(std::string(MOSAIC_SHADER_DIR) + "/ribbon.vert.spv");
        std::vector<char> frag_code = read_file(std::string(MOSAIC_SHADER_DIR) + "/ribbon.frag.spv");
        VkShaderModule vert = create_shader_module(vert_code);
        VkShaderModule frag = create_shader_module(frag_code);

        VkPipelineShaderStageCreateInfo vs{};
        vs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vs.stage = VK_SHADER_STAGE_VERTEX_BIT; vs.module = vert; vs.pName = "main";
        VkPipelineShaderStageCreateInfo fs{};
        fs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fs.stage = VK_SHADER_STAGE_FRAGMENT_BIT; fs.module = frag; fs.pName = "main";
        VkPipelineShaderStageCreateInfo stages[] = {vs, fs};

        // Per-vertex binding: pre-expanded world-space ribbon triangles.
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(mosaic::engine::render::RibbonVertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 3> attrs{};
        attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[0].offset = offsetof(mosaic::engine::render::RibbonVertex, position);
        attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[1].offset = offsetof(mosaic::engine::render::RibbonVertex, color);
        attrs[2].location = 2; attrs[2].binding = 0; attrs[2].format = VK_FORMAT_R32_SFLOAT;
        attrs[2].offset = offsetof(mosaic::engine::render::RibbonVertex, alpha);

        VkPipelineVertexInputStateCreateInfo vin{};
        vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vin.vertexBindingDescriptionCount = 1;
        vin.pVertexBindingDescriptions = &binding;
        vin.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
        vin.pVertexAttributeDescriptions = attrs.data();

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
        rast.cullMode = VK_CULL_MODE_NONE; // camera-facing ribbon; winding irrelevant

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
        push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        push.offset = 0; push.size = sizeof(RibbonPush);

        VkPipelineLayoutCreateInfo layout{};
        layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout.setLayoutCount = 0;
        layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &push;
        vk_check(vkCreatePipelineLayout(device_, &layout, nullptr, &ribbon_pipeline_layout_),
                 "vkCreatePipelineLayout(ribbon)");

        VkGraphicsPipelineCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pi.stageCount = 2; pi.pStages = stages;
        pi.pVertexInputState = &vin; pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp; pi.pRasterizationState = &rast;
        pi.pMultisampleState = &ms; pi.pDepthStencilState = &depth;
        pi.pColorBlendState = &cb; pi.pDynamicState = &dyn;
        pi.layout = ribbon_pipeline_layout_;
        pi.renderPass = render_pass_; pi.subpass = 0;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &ribbon_pipeline_),
                 "vkCreateGraphicsPipelines(ribbon)");

        pi.renderPass = vp_render_pass_;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &vp_ribbon_pipeline_),
                 "vkCreateGraphicsPipelines(ribbon offscreen)");

        vkDestroyShaderModule(device_, frag, nullptr);
        vkDestroyShaderModule(device_, vert, nullptr);
    }
```

- [ ] **Step 6: Implement `draw_ribbons`**

In `modules/render_vulkan/vulkan_render_device.cpp`, right after the `draw_particles` method's
closing brace, add:

```cpp
    void draw_ribbons(const std::vector<mosaic::engine::render::RibbonVertex>& vertices,
                      const mosaic::engine::render::RibbonDrawParams& params) override
    {
        if (!viewport_pass_open_) { ensure_swapchain_pass(); }
        if (frame_skipped_ || vertices.empty()) { return; }

        const VkDeviceSize bytes = vertices.size() * sizeof(vertices[0]);
        InstanceBuffer& vb = ribbon_vertices_[frame_];
        if (vb.capacity < bytes)
        {
            if (vb.buf != VK_NULL_HANDLE) { vkDestroyBuffer(device_, vb.buf, nullptr); }
            if (vb.mem != VK_NULL_HANDLE) { vkFreeMemory(device_, vb.mem, nullptr); }
            create_buffer(bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          vb.buf, vb.mem);
            vb.capacity = bytes;
        }
        void* mapped = nullptr;
        vkMapMemory(device_, vb.mem, 0, bytes, 0, &mapped);
        std::memcpy(mapped, vertices.data(), static_cast<size_t>(bytes));
        vkUnmapMemory(device_, vb.mem);

        vkCmdBindPipeline(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                          viewport_pass_open_ ? vp_ribbon_pipeline_ : ribbon_pipeline_);

        RibbonPush pc{};
        std::memcpy(pc.view_proj, params.view_proj.m.data(), sizeof(pc.view_proj));
        vkCmdPushConstants(cmd_[frame_], ribbon_pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(pc), &pc);

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd_[frame_], 0, 1, &vb.buf, &offset);
        vkCmdDraw(cmd_[frame_], static_cast<uint32_t>(vertices.size()), 1, 0, 0);
    }
```

- [ ] **Step 7: Destroy the pipeline + buffers in teardown**

In `modules/render_vulkan/vulkan_render_device.cpp`, in the cleanup block, right after the
lines that destroy `particle_pipeline_layout_` (before the `for (auto& ib : particle_instances_)`
loop), add:

```cpp
            if (vp_ribbon_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, vp_ribbon_pipeline_, nullptr); }
            if (ribbon_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, ribbon_pipeline_, nullptr); }
            if (ribbon_pipeline_layout_ != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, ribbon_pipeline_layout_, nullptr); }
```

and, right after the existing `for (auto& ib : particle_instances_) { ... }` cleanup loop, add:

```cpp
            for (auto& vb : ribbon_vertices_)
            {
                if (vb.buf != VK_NULL_HANDLE) { vkDestroyBuffer(device_, vb.buf, nullptr); }
                if (vb.mem != VK_NULL_HANDLE) { vkFreeMemory(device_, vb.mem, nullptr); }
            }
```

- [ ] **Step 8: Build the module + full suite**

Run: `cmake --build build/debug --target mosaic_unit_tests && ctest --test-dir build/debug`
Expected: PASS (glslc compiles the ribbon shaders; the module links). If the threaded `ScriptWorker` case flakes, re-run `--rerun-failed`.

- [ ] **Step 9: Build the app + smoke run**

Run: `cmake --build build/debug --target mosaic && ./build/debug/app/mosaic run --window --ticks 90 2>&1 | tail -6; echo "exit: ${PIPESTATUS[0]}"`
Expected: exit 0, no new Vulkan validation errors. (The falling sphere now leaves a smooth ribbon tail — the maintainer confirms the visual on hardware.)

- [ ] **Step 10: Update the handoff doc**

In `docs/project/next-steps.md`: update the VFX entry — trails now render as a connected
**ribbon** (V2): engine CPU-expands camera-facing tapered triangles (`build_ribbon`), a new
neutral `draw_ribbons` seam, and an alpha-blended Vulkan triangle pipeline
(`ribbon.vert`/`.frag`, modeled on the particle pipeline). Note the visual is maintainer-verified
on hardware. Keep VFX follow-ups (per-trail texture/UVs, width/color-over-length curves, miter
joins) and the next VFX slices (decals, screen/post-processing). Update the top "Last updated".

- [ ] **Step 11: Commit**

```bash
git add modules/render_vulkan docs/project/next-steps.md
git commit -m "feat(vfx): Vulkan ribbon pipeline + draw_ribbons; trails render as ribbons

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Self-Review

**Spec coverage:**
- `RibbonVertex`/`RibbonDrawParams`/`draw_ribbons` no-op seam → Task 1 Step 1. ✓
- `build_ribbon` (CPU-expand, taper, camera-facing side + fallbacks, <2 no-op) → Task 1 Step 2; tests Step 3. ✓
- RenderSystem ribbon pass replacing trail billboards → Task 2 Steps 1-2; test Step 3 (ribbon draw, no trail in particle batch). ✓
- Vulkan pipeline (shaders, glslc, push, per-vertex input triangle-list, alpha blend, swapchain+viewport, draw_ribbons upload, cleanup) → Task 3 Steps 1-7. ✓
- Build + smoke; visual = maintainer → Task 3 Steps 8-9. ✓

**Placeholder scan:** No TBD/TODO; every code step has complete code. ✓

**Type consistency:** `RibbonVertex{Vec3 position; Vec3 color; float alpha}` identical in `render_device.hpp`, `build_ribbon`, the Vulkan attrs (`offsetof`), and the fake device; `RibbonDrawParams{Mat4 view_proj}`; `RibbonPush{float[16]}` (64 bytes) matches the shader `mat4`; `draw_ribbons(const std::vector<RibbonVertex>&, const RibbonDrawParams&)` identical across the interface, fake, and Vulkan override; `build_ribbon(points, camera_pos, width, color, out)` matches the render-pass call; `ribbon_vertices_` uses the existing `InstanceBuffer`/`create_buffer` machinery. ✓
