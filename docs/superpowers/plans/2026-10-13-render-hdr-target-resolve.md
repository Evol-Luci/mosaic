# Offscreen HDR Target and Fullscreen Resolve Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Everything world-space renders into an offscreen linear-HDR target and is tone-mapped once by a fullscreen resolve; the UI draws onto the resolved image.

**Architecture:** Three default-no-op hooks on `RenderDevice`; `RenderSystem::render` places the boundary; the Vulkan backend adds an `R16G16B16A16_SFLOAT` target following the existing offscreen-viewport lifecycle, and a resolve pipeline reusing `screen_effect.vert`'s fullscreen triangle. `tonemap_aces` moves out of the object shaders into the resolve.

**Tech Stack:** C++20, Vulkan, GLSL (glslc at build time), Catch2, CMake/Ninja.

**Spec:** `docs/superpowers/specs/2026-10-13-render-hdr-target-resolve-design.md`

## Global Constraints

- Tier contract: `engine/` depends only on `foundation/`; `modules/render_vulkan` implements the neutral `RenderDevice` and is never included by `engine/`. Enforced by `tools/ci/check_dependency_rules.py`.
- New device hooks are `virtual` with `{}` bodies — a device implementing none renders untone-mapped, which is acceptable because only the Vulkan backend produces pixels.
- Shaders compile to SPIR-V at **build** time via `glslc`; a new shader must be added wherever the existing ones are registered in `modules/render_vulkan/CMakeLists.txt`.
- HDR format is `VK_FORMAT_R16G16B16A16_SFLOAT`, already used elsewhere in `vulkan_render_device.cpp`.
- Catch2 test names: no leading dashes, no unbalanced `[`, no commas.
- Build `cmake --build build/debug -j4`; suite `ctest` from `build/debug`; lints `python3 tools/ci/check_dependency_rules.py` and `python3 tools/ci/check_test_case_names.py`.
- Mutation probes: restore a probed file with `mv` **then `touch` it**, or ninja keeps the mutant binary.
- Commit messages end with the session's two attribution lines.

## Review Focus

Five conditions the spec implies that no happy-path test exercises, most likely to bite first:

1. **A World with no camera** — every freshly created one. `render()` has no early return; the sequence must run once with the default exposure, not be skipped. Task 2, Step 7.
2. **A frame with no UI at all** — `mosaic run --window` on a scene without a HUD. The resolve must still happen, or the screen is black. Task 2, Step 6.
3. **A frame with no world content** — UI only. `begin`/`end`/resolve must still bracket correctly rather than emitting an unbalanced pass. Task 2, Step 6.
4. **The editor path, where the resolve's destination is the viewport target, not the swapchain** — the thing that breaks if the resolve is made a pass rather than a draw. Task 4, Step 4.
5. **Window resize** — the HDR target must be recreated with the swapchain, or it is sampled at a stale size. Task 3, Step 5.

---

### Task 1: The three device hooks

**Files:**
- Modify: `engine/render/render_device.hpp` (beside the viewport hooks, ~line 314)
- Test: `tests/unit/render/render_device_hooks_test.cpp`

**Interfaces:**
- Produces: `RenderDevice::begin_hdr_scene_pass(const Color&)`, `end_hdr_scene_pass()`, `draw_hdr_resolve(float exposure)`, all virtual with empty bodies. Tasks 2–4 call and implement them.

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/render/render_device_hooks_test.cpp`:

```cpp
TEST_CASE("hdr scene hooks default to no-op", "[render][hdr]")
{
    // Same contract the viewport hooks carry: a device that implements none of these must
    // still be constructible and callable. The consequence -- that such a device renders
    // untone-mapped -- is acceptable because only the Vulkan backend produces pixels; every
    // other implementation in this tree records calls and draws nothing.
    MinimalDevice device;
    device.begin_hdr_scene_pass(Color{0.0f, 0.0f, 0.0f, 1.0f});
    device.draw_hdr_resolve(1.0f);
    device.end_hdr_scene_pass();
    SUCCEED("the default hooks are callable and do nothing");
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build/debug --target mosaic_unit_tests -j4`
Expected: FAIL to compile — `begin_hdr_scene_pass` is not a member of `RenderDevice`.

- [ ] **Step 3: Add the hooks**

In `engine/render/render_device.hpp`, directly after the viewport hooks block:

```cpp
    // --- offscreen HDR scene target -------------------------------------------------
    // World-space rendering goes into a linear HDR target; draw_hdr_resolve then tone maps
    // it as one image. Tone mapping used to live in cube.frag and skybox.frag, which meant
    // decals, particles, ribbons and world text were never tone mapped at all and a "bright"
    // particle could not be bright -- it was written into a buffer whose ceiling is 1.0,
    // leaving no headroom for bloom to find.
    //
    // draw_hdr_resolve is a DRAW, not a pass, and that is load-bearing: it renders a
    // fullscreen triangle into whatever colour target is currently bound. The swapchain in
    // `mosaic run --window`, the offscreen viewport target in the editor. Neither caller
    // needs to know which, so both stay on one code path -- which is what 37d worked to
    // achieve and a resolve-as-a-pass would have undone.
    //
    // Default no-ops, like the viewport hooks: a device that implements none renders
    // untone-mapped. Acceptable because only the Vulkan backend produces pixels.
    virtual void begin_hdr_scene_pass(const Color& /*clear*/) {}
    virtual void end_hdr_scene_pass() {}
    virtual void draw_hdr_resolve(float /*exposure*/) {}
```

- [ ] **Step 4: Run to verify it passes**

Run: `./build/debug/tests/mosaic_unit_tests "[render][hdr]"`
Expected: PASS, 1 test case.

- [ ] **Step 5: Commit**

```bash
git add engine/render/render_device.hpp tests/unit/render/render_device_hooks_test.cpp
git commit -m "feat(render): default-no-op hooks for an offscreen HDR scene target"
```

---

### Task 2: `RenderSystem` places the boundary

**Files:**
- Modify: `engine/render/render_system.hpp` (`render()`: after the light resolution ~line 174, and before the HUD pass ~line 408)
- Test: `tests/unit/render/render_system_test.cpp` (extend `FakeRenderDevice`, add cases)

**Interfaces:**
- Consumes: the three hooks from Task 1.
- Produces: the call ordering Tasks 3–4 implement against. `FakeRenderDevice` gains `hdr_calls` (a `std::vector<std::string>` of `"begin"`, `"end"`, `"resolve"`), `hdr_exposures`, and an `ordered_calls` log that interleaves UI and HDR calls so a test can assert *relative* order.

- [ ] **Step 1: Extend `FakeRenderDevice` to record ordering**

In `tests/unit/render/render_system_test.cpp`, add to `FakeRenderDevice`:

```cpp
    // A single ordered log, because the thing under test is RELATIVE order: "the resolve
    // happens after the world draws and before the UI ones". Separate per-call vectors
    // cannot express that -- they record counts, not sequence.
    std::vector<std::string> ordered_calls;
    std::vector<float>       hdr_exposures;

    void begin_hdr_scene_pass(const Color&) override { ordered_calls.push_back("hdr_begin"); }
    void end_hdr_scene_pass() override { ordered_calls.push_back("hdr_end"); }
    void draw_hdr_resolve(float exposure) override
    {
        ordered_calls.push_back("hdr_resolve");
        hdr_exposures.push_back(exposure);
    }

    // Index of the first occurrence, or -1. Comparing indices is how the tests below say
    // "before" and "after" without depending on how many draws happened in between.
    int index_of(const std::string& name) const
    {
        for (std::size_t i = 0; i < ordered_calls.size(); ++i)
        {
            if (ordered_calls[i] == name) { return static_cast<int>(i); }
        }
        return -1;
    }
```

Then add `ordered_calls.push_back(...)` to the existing recorders, so world and UI draws land in the same log:
- `draw` → `"draw"`
- `draw_skybox` → `"skybox"`
- `draw_decals` → `"decals"`
- `draw_particles` → `"particles"`
- `draw_ribbons` → `"ribbons"`
- `draw_screen_effect` → `"screen_effect"`
- `draw_world_text` → `"world_text"`
- `draw_ui` → `"ui"`
- `draw_ui_image` → `"ui_image"`
- `draw_ui_text` → `"ui_text"`

Leave each existing per-call vector in place; the current tests read them.

- [ ] **Step 2: Write the failing ordering test**

```cpp
TEST_CASE("world-space drawing happens inside the hdr pass and the UI after the resolve",
          "[render][hdr]")
{
    // THE BOUNDARY, which is the whole slice. A UiPanel's colour is authored -- {0.1,0.6,0.9}
    // is a value someone typed and expects to see -- so pushing it through ACES would make
    // what appears on screen depend on how bright the scene behind it is.
    AssetRegistry registry;
    World         world;
    RenderSystem  rs;
    add_camera(world, Vec3{0, 0, 5});
    const AssetID mesh_id = register_cube(registry);
    auto          mesh    = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{Mat4::translation(Vec3{0, 0, 0})});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id});

    auto    e = world.create_entity();
    UiPanel p;
    p.anchor = static_cast<int>(mosaic::engine::ui::Anchor::TopLeft);
    p.pos    = {0.1f, 0.1f};
    p.size   = {0.2f, 0.2f};
    world.add_component<UiPanel>(e, p);

    FakeRenderDevice device;
    rs.render(world, device, registry);

    const int begin   = device.index_of("hdr_begin");
    const int draw    = device.index_of("draw");
    const int end     = device.index_of("hdr_end");
    const int resolve = device.index_of("hdr_resolve");
    const int ui      = device.index_of("ui");

    REQUIRE(begin >= 0);
    REQUIRE(draw > begin);     // the mesh is inside the HDR pass
    REQUIRE(end > draw);       // the pass closes after the world draws
    REQUIRE(resolve > end);    // the resolve follows it
    REQUIRE(ui > resolve);     // and the UI lands on the resolved image
}
```

- [ ] **Step 3: Run to verify it fails**

Run: `./build/debug/tests/mosaic_unit_tests "[render][hdr]"`
Expected: FAIL — `hdr_begin` is never recorded, so `begin` is -1.

- [ ] **Step 4: Place the boundary**

In `render()`, immediately after `device.set_frame_lighting(frame_lighting);`:

```cpp
        // EVERYTHING WORLD-SPACE FROM HERE goes into the linear HDR target: opaque meshes,
        // the skybox, decals, particles, ribbons, the vignette and world text. They are tone
        // mapped together by the resolve below, which is what gives a bright particle
        // somewhere above 1.0 to live until bloom can find it.
        //
        // The clear is the same colour the swapchain pass used to take; nothing downstream
        // reads it, since the resolve writes every pixel.
        device.begin_hdr_scene_pass(Color{0.0f, 0.0f, 0.0f, 1.0f});
```

Then immediately before the `// HUD pass:` comment:

```cpp
        // END OF WORLD SPACE. The UI draws below land on the RESOLVED image, deliberately:
        // UI colours are authored rather than captured, so tone mapping them would make the
        // HUD dim whenever the scene got bright.
        device.end_hdr_scene_pass();
        // A draw, not a pass -- into whatever target is bound. The swapchain in
        // `mosaic run --window`, the viewport offscreen target in the editor.
        device.draw_hdr_resolve(exposure);
```

- [ ] **Step 5: Run to verify it passes**

Run: `./build/debug/tests/mosaic_unit_tests "[render][hdr]"`
Expected: PASS, 2 test cases.

- [ ] **Step 6: Add the empty-scene cases (Review Focus 2 and 3)**

```cpp
TEST_CASE("a scene with no UI still resolves", "[render][hdr]")
{
    // `mosaic run --window` on a scene with no HUD. If the resolve were emitted by the UI
    // path, this frame would show the untouched swapchain -- a black screen.
    AssetRegistry registry;
    World         world;
    RenderSystem  rs;
    add_camera(world, Vec3{0, 0, 5});

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.index_of("hdr_resolve") >= 0);
    REQUIRE(device.index_of("ui") == -1);
}

TEST_CASE("a frame with no world content still brackets the hdr pass", "[render][hdr]")
{
    // UI only. An unbalanced begin without an end would leave the backend in a pass it never
    // closes, which is a validation error rather than a visual one -- and therefore the kind
    // that only shows up on a real device.
    AssetRegistry registry;
    World         world;
    RenderSystem  rs;
    add_camera(world, Vec3{0, 0, 5});

    auto    e = world.create_entity();
    UiPanel p;
    p.anchor = static_cast<int>(mosaic::engine::ui::Anchor::TopLeft);
    p.size   = {0.2f, 0.2f};
    world.add_component<UiPanel>(e, p);

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.index_of("hdr_begin") >= 0);
    REQUIRE(device.index_of("hdr_end") > device.index_of("hdr_begin"));
    REQUIRE(device.index_of("hdr_resolve") > device.index_of("hdr_end"));
    REQUIRE(device.index_of("ui") > device.index_of("hdr_resolve"));
}
```

- [ ] **Step 7: Add the no-camera and exposure cases (Review Focus 1)**

```cpp
TEST_CASE("a world with no camera still runs the sequence once", "[render][hdr]")
{
    // The state every freshly created World is in. render() has NO early return for a
    // missing camera -- it falls back to a default view and projection -- so the pass
    // structure is unconditional, and a conditional resolve would make this a special case.
    AssetRegistry registry;
    World         world;
    RenderSystem  rs;

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.index_of("hdr_begin") >= 0);
    REQUIRE(device.index_of("hdr_resolve") >= 0);
    REQUIRE(device.hdr_exposures.size() == 1);
    REQUIRE(device.hdr_exposures[0] == Approx(1.0f)); // Camera::exposure's default
}

TEST_CASE("the resolve receives the active camera's exposure", "[render][hdr]")
{
    // Exposure is passed to the resolve rather than read from the lighting UBO inside the
    // backend: it is a property of the resolve now, and a parameter is testable where a UBO
    // field read is not.
    AssetRegistry registry;
    World         world;
    RenderSystem  rs;
    const auto    cam = add_camera(world, Vec3{0, 0, 5});
    world.get_component<mosaic::engine::render::Camera>(cam)->exposure = 2.5f;

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.hdr_exposures.size() == 1);
    REQUIRE(device.hdr_exposures[0] == Approx(2.5f));
}
```

`add_camera` is declared `World::EntityID add_camera(World&, Vec3)` at
`tests/unit/render/render_system_test.cpp:209`, so the returned entity is usable directly.

- [ ] **Step 8: Add the vignette and world-text placement case**

```cpp
TEST_CASE("the vignette and world text are inside the hdr pass", "[render][hdr]")
{
    // Both are world-space by the spec's boundary. The vignette attenuates light, which is
    // what a real lens does BEFORE exposure, so it belongs before the curve rather than as a
    // post-hoc darkening of the final image.
    AssetRegistry registry;
    World         world;
    RenderSystem  rs;
    add_camera(world, Vec3{0, 0, 5});

    auto e = world.create_entity();
    world.add_component<mosaic::engine::vfx::ScreenEffect>(
        e, mosaic::engine::vfx::ScreenEffect{Vec3{0, 0, 0}, 0.0f, 0.4f});

    FakeRenderDevice device;
    rs.render(world, device, registry);

    const int effect = device.index_of("screen_effect");
    REQUIRE(effect > device.index_of("hdr_begin"));
    REQUIRE(effect < device.index_of("hdr_end"));
}
```

- [ ] **Step 9: Run the mutation probes**

For each, edit `engine/render/render_system.hpp`, rebuild, run `./build/debug/tests/mosaic_unit_tests "[render][hdr]"`, confirm FAIL, then restore **and `touch`** the file:

1. The resolve moved above `end_hdr_scene_pass()` — expect red.
2. `end_hdr_scene_pass()` moved below the HUD passes — expect red.
3. `begin_hdr_scene_pass` moved below the opaque mesh pass — expect red.
4. `draw_hdr_resolve(exposure)` becomes `draw_hdr_resolve(1.0f)` — expect the exposure test red.
5. `end_hdr_scene_pass()` deleted — expect red.

Any GREEN is a real gap: add the distinguishing test before moving on.

- [ ] **Step 10: Full suite, then commit**

```bash
cmake --build build/debug -j4 && (cd build/debug && ctest)
git add engine/render/render_system.hpp tests/unit/render/render_system_test.cpp
git commit -m "feat(render): RenderSystem brackets world-space drawing in an HDR pass"
```

---

### Task 3: The Vulkan HDR target

**Files:**
- Modify: `modules/render_vulkan/vulkan_render_device.hpp` (declare the target + overrides)
- Modify: `modules/render_vulkan/vulkan_render_device.cpp` (create, destroy, recreate, record)

**Interfaces:**
- Consumes: the hooks from Task 1.
- Produces: a working HDR target. Task 4 adds the resolve pipeline that samples it.

- [ ] **Step 1: Add the target's members**

The offscreen viewport target is the template throughout this task: its render pass is
`create_offscreen_render_pass` (`vulkan_render_device.cpp:2659`), its images are
`create_offscreen_target` (:2758), and it is torn down by `destroy_offscreen_target` (:2737).
Read those three before writing, then mirror them.

In `vulkan_render_device.hpp`, beside the `vp_*` members:

```cpp
    // The linear HDR scene target. Same lifecycle as the offscreen viewport target above --
    // created with the swapchain, destroyed with it, recreated on resize -- and the same
    // finalLayout trick: SHADER_READ_ONLY_OPTIMAL so the resolve can sample it without a
    // manual barrier.
    VkImage        hdr_color_      = VK_NULL_HANDLE;
    VkDeviceMemory hdr_color_mem_  = VK_NULL_HANDLE;
    VkImageView    hdr_color_view_ = VK_NULL_HANDLE;
    VkImage        hdr_depth_      = VK_NULL_HANDLE;
    VkDeviceMemory hdr_depth_mem_  = VK_NULL_HANDLE;
    VkImageView    hdr_depth_view_ = VK_NULL_HANDLE;
    VkFramebuffer  hdr_framebuffer_ = VK_NULL_HANDLE;
    VkRenderPass   hdr_render_pass_ = VK_NULL_HANDLE;
    VkSampler      hdr_sampler_     = VK_NULL_HANDLE;
    bool           in_hdr_pass_     = false;
```

> **Its own depth, not the viewport's.** `create_offscreen_target` creates a depth image per
> target for the same reason: a framebuffer binds specific views, and the HDR pass and the
> viewport pass are different render passes with different framebuffers. Sharing would mean
> one pass writing depth the other declared `LOAD_OP_CLEAR` on.

- [ ] **Step 2: Create the render pass**

A copy of `create_offscreen_render_pass` with one changed line — the colour format:

```cpp
    void create_hdr_render_pass()
    {
        VkAttachmentDescription color{};
        // THE ONLY DIFFERENCE from create_offscreen_render_pass: a float format, so values
        // above 1.0 survive the pass instead of clamping. That headroom is the whole point.
        color.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        // Sampled by the resolve, so hand it over already in the layout the sampler wants.
        color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
```

and from there identical to `create_offscreen_render_pass`'s depth attachment, subpass, and
two dependencies — copy them verbatim, including the comment explaining the second
dependency, since it serves the same purpose here (making the colour image visible to the
fragment shader that samples it).

Call it from the same place `create_offscreen_render_pass()` is called (:294).

- [ ] **Step 3: Create and destroy the images**

Mirroring `create_offscreen_target`, which already has the helpers:

```cpp
    void create_hdr_target(uint32_t width, uint32_t height)
    {
        VkImageCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.extent = {width, height, 1};
        ci.mipLevels = 1;
        ci.arrayLayers = 1;
        ci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vk_check(vkCreateImage(device_, &ci, nullptr, &hdr_color_), "vkCreateImage(hdr color)");
        bind_image_memory(hdr_color_, hdr_color_mem_, "hdr color");
        hdr_color_view_ =
            make_image_view(hdr_color_, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);

        VkImageCreateInfo di = ci;
        di.format = kDepthFormat;
        di.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        vk_check(vkCreateImage(device_, &di, nullptr, &hdr_depth_), "vkCreateImage(hdr depth)");
        bind_image_memory(hdr_depth_, hdr_depth_mem_, "hdr depth");
        hdr_depth_view_ = make_image_view(hdr_depth_, kDepthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

        std::array<VkImageView, 2> attachments{hdr_color_view_, hdr_depth_view_};
        VkFramebufferCreateInfo fb{};
        fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass = hdr_render_pass_;
        fb.attachmentCount = static_cast<uint32_t>(attachments.size());
        fb.pAttachments = attachments.data();
        fb.width = width;
        fb.height = height;
        fb.layers = 1;
        vk_check(vkCreateFramebuffer(device_, &fb, nullptr, &hdr_framebuffer_),
                 "vkCreateFramebuffer(hdr)");
    }
```

`destroy_hdr_target` mirrors `destroy_offscreen_target` — same six handles, same order, each
reset to `VK_NULL_HANDLE`. It has no ImGui descriptor to remove, which is the one thing the
viewport version does that this must not copy.

The sampler is created once at device setup, not per target: `VK_FILTER_LINEAR`,
`VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE`.

- [ ] **Step 4: Implement the two pass hooks**

```cpp
    void begin_hdr_scene_pass(const mosaic::engine::render::Color& clear) override
    {
        if (in_hdr_pass_ || hdr_framebuffer_ == VK_NULL_HANDLE) { return; }
        std::array<VkClearValue, 2> clears{};
        clears[0].color = {{clear.r, clear.g, clear.b, clear.a}};
        clears[1].depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        bi.renderPass = hdr_render_pass_;
        bi.framebuffer = hdr_framebuffer_;
        bi.renderArea.extent = swapchain_extent_;
        bi.clearValueCount = static_cast<uint32_t>(clears.size());
        bi.pClearValues = clears.data();
        vkCmdBeginRenderPass(current_cmd(), &bi, VK_SUBPASS_CONTENTS_INLINE);
        in_hdr_pass_ = true;
    }

    void end_hdr_scene_pass() override
    {
        // Guarded both ways: a double-begin or an end without a begin is a validation error
        // rather than a visible bug, which is the kind that only appears on a real device.
        if (!in_hdr_pass_) { return; }
        vkCmdEndRenderPass(current_cmd());
        in_hdr_pass_ = false;
    }
```

> **Implementer:** `current_cmd()` and `swapchain_extent_` are the names assumed for the
> frame's command buffer and the swapchain size. Grep for how `begin_viewport_pass` reaches
> both and use whatever it uses — this file's conventions win over the names here.

- [ ] **Step 5: Recreate on resize (Review Focus 5)**

Call `destroy_hdr_target()` then `create_hdr_target(w, h)` from `recreate_swapchain()`
(:4429), wherever it does the same for the viewport target. A stale-sized HDR target is
sampled at the wrong resolution and shows as a smeared or cropped frame on every resize.

**Not unit-testable** — no test creates a `VkDevice`. Verify by running
`mosaic run --window` and resizing, and say so in the commit message.

- [ ] **Step 6: Build and run the whole suite**

```bash
cmake --build build/debug -j4 && (cd build/debug && ctest)
```
Expected: PASS. Nothing in the suite touches Vulkan, so this is a compile gate plus a
no-regression check.

- [ ] **Step 7: Commit**

```bash
git add modules/render_vulkan/vulkan_render_device.hpp modules/render_vulkan/vulkan_render_device.cpp
git commit -m "feat(render): an R16G16B16A16_SFLOAT scene target in the Vulkan backend"
```

---

### Task 4: The resolve, and tone mapping moves

**Files:**
- Create: `modules/render_vulkan/shaders/hdr_resolve.frag`
- Modify: `modules/render_vulkan/shaders/cube.frag` (remove 3 `tonemap_aces` sites + the exposure multiply)
- Modify: `modules/render_vulkan/shaders/skybox.frag` (remove 2)
- Modify: `modules/render_vulkan/CMakeLists.txt` (register the new shader)
- Modify: `modules/render_vulkan/vulkan_render_device.cpp` (resolve pipeline + `draw_hdr_resolve`)

**Interfaces:**
- Consumes: the HDR target from Task 3.
- Produces: the finished pipeline.

- [ ] **Step 1: Write the resolve fragment shader**

Create `modules/render_vulkan/shaders/hdr_resolve.frag`, moving `tonemap_aces` verbatim out
of `cube.frag` — including its comments, which explain why the curve floors its own input:

```glsl
#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D hdrScene;

layout(push_constant) uniform Resolve { float exposure; } pc;

// ACES filmic approximation (Narkowicz), moved here from cube.frag. It used to run per
// object, which meant decals, particles, ribbons and world text were never tone mapped at
// all -- they blended against a buffer that had already been through the curve. Running it
// once over the whole scene is what makes a value above 1.0 meaningful, and therefore what
// makes bloom possible.
vec3 tonemap_aces(vec3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    x = max(x, vec3(0.0));
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    vec3 hdr = texture(hdrScene, fragUV).rgb;
    // Exposure scales the value ENTERING the curve, as it did in cube.frag. Applied after,
    // it would scale an already-clamped [0,1] value, so it could darken but never brighten
    // -- which is most of what exposure is for.
    outColor = vec4(tonemap_aces(hdr * pc.exposure), 1.0);
}
```

Reuse `screen_effect.vert` as the vertex stage rather than writing a second fullscreen
triangle.

- [ ] **Step 2: Register the shader**

Add `hdr_resolve.frag` wherever `screen_effect.frag` is listed in
`modules/render_vulkan/CMakeLists.txt`. Build and confirm the `.spv` is produced; a missing
registration fails at pipeline creation, not at build.

- [ ] **Step 3: Remove tone mapping from the object shaders**

In `cube.frag`, delete `tonemap_aces` and its three call sites, and the exposure multiply;
the shader now writes **linear HDR**. Leave a comment where the call was:

```glsl
    // No tone mapping here: this writes LINEAR HDR into the scene target, and
    // hdr_resolve.frag maps the whole frame once. Doing it per object is what left decals,
    // particles, ribbons and world text untouched by the curve.
```

Same in `skybox.frag` for its two sites. Remove the now-unused `exposure` field from the
push-constant block **only if nothing else reads it** — check before deleting, since the
block's layout is shared with the C++ side and a mismatch is silent corruption.

- [ ] **Step 4: Implement `draw_hdr_resolve` (Review Focus 4)**

```cpp
    void draw_hdr_resolve(float exposure) override;
```

It binds the resolve pipeline, binds a descriptor set sampling `hdr_view_` through
`hdr_sampler_`, pushes `exposure`, and draws 3 vertices.

**It must NOT begin a render pass.** It draws into whatever pass is currently recording —
the swapchain pass in `mosaic run --window`, the offscreen viewport pass in the editor. That
is the whole reason the hook is a draw: the two paths stay identical, and the resolve never
needs to know its destination.

The resolve pipeline therefore needs to be compatible with **both** render passes. If their
formats differ, create one pipeline per pass and select on which is recording; note in a
comment which case applies here rather than leaving a reader to work it out.

- [ ] **Step 5: Build, full suite, lints**

```bash
cmake --build build/debug -j4
(cd build/debug && ctest)
python3 tools/ci/check_dependency_rules.py && echo deps-ok
python3 tools/ci/check_test_case_names.py && echo names-ok
```

- [ ] **Step 6: Verify on a real device**

```bash
./build/debug/app/mosaic run --ticks 5 --json >/dev/null; echo "run=$?"
timeout 60 ./build/debug/app/mosaic run --window --ticks 60 >/dev/null 2>/tmp/w.err; echo "window=$?"
grep -iE 'validation|error' /tmp/w.err | head
timeout 60 ./build/debug/app/mosaic editor --ticks 40 >/dev/null 2>/tmp/e.err; echo "editor=$?"
grep -iE 'validation|error' /tmp/e.err | head
```

Expected: all exit 0, and **no Vulkan validation errors** — the backend logs them through the
engine logger, so they appear here. A layout-transition or render-pass-compatibility mistake
shows up as a validation error and nothing else; this command is the only gate that catches
it.

- [ ] **Step 7: Commit**

```bash
git add modules/render_vulkan/ docs/
git commit -m "feat(render): tone map once in a fullscreen resolve, not per object"
```

---

### Task 5: Documentation

**Files:**
- Modify: `docs/project/deferred-work-backlog.md`
- Modify: `docs/project/next-steps.md`
- Modify: `docs/design/rendering.md` if it describes where tone mapping happens — **check**, since a doc that still says "tone mapped in the object shader" would be wrong in exactly the way this session keeps finding.

- [ ] **Step 1: Strike the backlog item**

Mark the 🟡 "offscreen HDR render target plus a fullscreen resolve pass" as SHIPPED, naming the boundary and that bloom is now unblocked.

- [ ] **Step 2: Record what this slice deferred**

In the tiered format:
- **Bloom itself** — now unblocked; the next slice.
- **The RenderGraph, deferred again**, with bloom's mip chain named as the probable real trigger since its pass count varies with resolution — the first genuinely *dynamic* need, which is the bar the backlog set.
- **The four newly-tone-mapped effects may need retuning** — decals, particles, ribbons, world text. Only a human at the window can say; queued for the next verification pass.
- **A configurable tone-mapping curve** — ACES is now in one place, which makes swapping it cheap, but choosing a second curve is its own question.

- [ ] **Step 3: Write the next-steps entry**

Covering: why per-object tone mapping was incoherent and why bloom made it fatal; the boundary and why UI sits outside it; that the resolve is a draw rather than a pass and what that buys; the accepted appearance shift; what the probes found; and that the image itself is unverified pending a human pass.

- [ ] **Step 4: Full gate, then commit and PR**

```bash
cmake --build build/debug -j4 && (cd build/debug && ctest)
git add docs/
git commit -m "docs(render): record the HDR target and resolve, and what it deferred"
git push -u origin feat/render-hdr-target-resolve
```

Then `gh pr create`, covering the above plus the gate numbers. Close by naming what only eyes can judge: whether the image reads correctly, and whether the four newly-tone-mapped effects need retuning.

Wait for `build-and-test` to pass, then squash-merge and delete the branch.
