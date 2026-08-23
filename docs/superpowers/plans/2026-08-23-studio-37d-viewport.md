# Studio 37d — Viewport Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Embed the renderer's live 3D output in a dockable Studio "Viewport" panel via a panel-sized offscreen render target.

**Architecture:** The neutral `RenderDevice` gains four default-no-op viewport hooks; the Vulkan backend implements them with an offscreen color+depth target sampled by ImGui, enabled by a lazy-swapchain-pass refactor so `mosaic run --window` and `mosaic editor` share one device without changing the device contract. A headless `ViewportPanel` model + a `render_scene_to_viewport` seam carry the testable sizing/ordering logic; the editor renders a live demo scene into the offscreen target each frame, then draws it in the panel.

**Tech Stack:** C++20, Vulkan (`modules/render_vulkan`), Dear ImGui (docking) via `imgui_impl_vulkan`, Catch2, CMake.

## Global Constraints

- **C++20**, header-only where the surrounding tier is (studio `hierarchy.hpp`/`selection.hpp` are header-only).
- **Tier contract** ([ARCHITECTURE.md](../../architecture/ARCHITECTURE.md), [boundaries.md §3](../../architecture/boundaries.md)): `engine/render` stays Vulkan-free (neutral signatures only); **only `modules/render_vulkan`** touches Vulkan/ImGui-Vulkan; `studio/` drives panels through ImGui + neutral hooks and **never names a Vulkan type**; `foundation/`/`engine/` never depend on `studio/` or ImGui.
- **No new third-party dependencies** — ImGui (docking) and the Vulkan backend are already vendored.
- **Math conventions (fixed):** column-major, right-handed, Y-up, radians. The offscreen viewport uses the same negative-height (Y-flip) viewport as the swapchain so the sampled image is upright.
- **Render-graph decision (settled in the spec):** minimal direct two-pass submission; the `RenderGraph` stays deferred to the first dynamic/≥3-pass slice. Do **not** build a graph object here.
- **Catch2 test names must not start with `--`** (a leading `--` breaks `catch_discover_tests`/`ctest`).
- **GUI verification is functional only:** the sandbox cannot screenshot the window. Acceptance for a draw path = `mosaic editor`/`mosaic run --window` launches, exits 0, silent stderr (bound the run with `--ticks`/`--duration`).
- **Build/test commands:**
  - Configure/build: `cmake --build build -j` (presets already configured; if not: `cmake --preset default && cmake --build build -j`).
  - Run unit tests: `ctest --test-dir build --output-on-failure`.

---

## File Structure

- `engine/render/render_device.hpp` — **modify.** Add `ViewportTextureId` + four default-no-op viewport hooks.
- `studio/viewport.hpp` — **create.** Header-only `ViewportSize`, `ViewportPanel`, and the `render_scene_to_viewport` seam. No ImGui, no Vulkan.
- `modules/render_vulkan/vulkan_render_device.cpp` — **modify.** Lazy swapchain pass; offscreen target + hook implementations.
- `app/run/demo_scene.hpp` — **create.** Shared `SpinSystem` + `build_demo_scene(...)` returning a `RenderSystem`, used by both `mosaic run` and `mosaic editor`.
- `app/main.cpp` — **modify.** Use `build_demo_scene` (removes the inline scene duplication).
- `app/run/editor.cpp` — **modify.** Live demo scene + `RenderSystem`; per-frame offscreen pass + Viewport panel wiring.
- `studio/studio_app.{hpp,cpp}` — **modify.** A `ViewportPanel` member + a `"Viewport"` panel body; expose the panel + a per-frame scene-render hook to `run_editor`.
- `studio/studio_layout.cpp` — **modify.** Register the `"Viewport"` panel.
- `tests/unit/studio/viewport_test.cpp` — **create.** `ViewportPanel` + `render_scene_to_viewport` unit tests.
- `tests/unit/render/render_device_hooks_test.cpp` — **create.** Default-no-op hook contract test.
- `tests/CMakeLists.txt` — **modify.** Register the two new test files.

---

## Task 1: Neutral RenderDevice viewport hooks

**Files:**
- Modify: `engine/render/render_device.hpp`
- Create: `tests/unit/render/render_device_hooks_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `mosaic::engine::render::ViewportTextureId` (alias for `std::uint64_t`); virtual methods on `RenderDevice`: `void set_viewport_size(std::uint32_t w, std::uint32_t h)`, `void begin_viewport_pass(const Color&)`, `void end_viewport_pass()`, `ViewportTextureId viewport_texture() const` — all default no-op / return 0.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/render/render_device_hooks_test.cpp`:

```cpp
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/render/render_device.hpp"

using mosaic::engine::render::Color;
using mosaic::engine::render::DrawParams;
using mosaic::engine::render::Image;
using mosaic::engine::render::MeshHandle;
using mosaic::engine::render::RenderDevice;
using mosaic::engine::render::TextureHandle;
using mosaic::engine::render::Vertex;
using mosaic::engine::render::ViewportTextureId;

namespace
{
// A device implementing only the required pure virtuals; the viewport hooks
// keep their base-class defaults.
class MinimalDevice : public RenderDevice
{
public:
    void       begin_frame() override {}
    void       clear(const Color&) override {}
    void       end_frame() override {}
    MeshHandle create_mesh(const std::vector<Vertex>&, const std::vector<std::uint32_t>&) override
    {
        return 1;
    }
    TextureHandle create_texture(const Image&) override { return 1; }
    void          draw(MeshHandle, TextureHandle, const DrawParams&) override {}
};
} // namespace

TEST_CASE("viewport hooks default to no-op and no texture", "[render][viewport]")
{
    MinimalDevice device;
    // Default-no-op: these must be safe to call and must not throw.
    device.set_viewport_size(640, 480);
    device.begin_viewport_pass(Color{0, 0, 0, 1});
    device.end_viewport_pass();
    // No offscreen target on a base device.
    REQUIRE(device.viewport_texture() == static_cast<ViewportTextureId>(0));
}
```

Register it in `tests/CMakeLists.txt` — add this line to the `add_executable(mosaic_unit_tests ...)` source list, next to the other `unit/render/...` entries:

```cmake
  unit/render/render_device_hooks_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build -j 2>&1 | tail -20`
Expected: compile error — `set_viewport_size` / `begin_viewport_pass` / `end_viewport_pass` / `viewport_texture` are not members of `RenderDevice`, and `ViewportTextureId` is undeclared.

- [ ] **Step 3: Add the hooks to the neutral interface**

In `engine/render/render_device.hpp`, add the type alias near `TextureHandle`:

```cpp
using ViewportTextureId = std::uint64_t; // ImTextureID-compatible; 0 == none
```

Then, inside `class RenderDevice`, after the existing UI hooks (`init_ui`/`begin_ui_frame`/`end_ui_frame`), add:

```cpp
    // Offscreen viewport hooks. Default no-ops so headless devices and test fakes
    // need not implement them. The editor renders the scene into a panel-sized
    // offscreen target (set_viewport_size + begin/end_viewport_pass), then samples
    // it in the UI via viewport_texture().
    virtual void set_viewport_size(std::uint32_t /*width*/, std::uint32_t /*height*/) {}
    virtual void begin_viewport_pass(const Color& /*clear*/) {}
    virtual void end_viewport_pass() {}
    virtual ViewportTextureId viewport_texture() const { return 0; }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R "viewport hooks default"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/render/render_device.hpp tests/unit/render/render_device_hooks_test.cpp tests/CMakeLists.txt
git commit -m "studio 37d: neutral RenderDevice viewport hooks (default no-op) (#37)"
```

---

## Task 2: ViewportPanel sizing model

**Files:**
- Create: `studio/viewport.hpp`
- Create: `tests/unit/studio/viewport_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `mosaic::studio::ViewportSize { std::uint32_t width; std::uint32_t height; }`; `mosaic::studio::ViewportPanel` with `void set_measured_size(float w, float h)`, `ViewportSize size() const`, `bool needs_resize() const`, `void mark_applied()`, `float aspect() const`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/studio/viewport_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "studio/viewport.hpp"

using Catch::Approx;
using mosaic::studio::ViewportPanel;
using mosaic::studio::ViewportSize;

TEST_CASE("ViewportPanel starts wanting a resize with a 1x1 min size", "[studio][viewport]")
{
    ViewportPanel panel;
    REQUIRE(panel.size().width == 1u);
    REQUIRE(panel.size().height == 1u);
    REQUIRE(panel.needs_resize()); // nothing applied yet
    REQUIRE(panel.aspect() == Approx(1.0f));
}

TEST_CASE("ViewportPanel records measured size and detects change", "[studio][viewport]")
{
    ViewportPanel panel;
    panel.set_measured_size(800.0f, 600.0f);
    REQUIRE(panel.size().width == 800u);
    REQUIRE(panel.size().height == 600u);
    REQUIRE(panel.aspect() == Approx(800.0f / 600.0f));
    REQUIRE(panel.needs_resize());

    panel.mark_applied();
    REQUIRE_FALSE(panel.needs_resize());

    panel.set_measured_size(800.0f, 600.0f); // same
    REQUIRE_FALSE(panel.needs_resize());

    panel.set_measured_size(1024.0f, 768.0f); // changed
    REQUIRE(panel.needs_resize());
}

TEST_CASE("ViewportPanel clamps degenerate and fractional sizes", "[studio][viewport]")
{
    ViewportPanel panel;
    panel.set_measured_size(0.0f, 0.0f);
    REQUIRE(panel.size().width == 1u);
    REQUIRE(panel.size().height == 1u);

    panel.set_measured_size(-5.0f, 3.9f);
    REQUIRE(panel.size().width == 1u);   // negative -> 1
    REQUIRE(panel.size().height == 3u);  // truncated toward zero

    panel.set_measured_size(100.9f, 50.2f);
    REQUIRE(panel.size().width == 100u);
    REQUIRE(panel.size().height == 50u);
}
```

Register it in `tests/CMakeLists.txt` next to the other `unit/studio/...` entries:

```cmake
  unit/studio/viewport_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build -j 2>&1 | tail -20`
Expected: compile error — `studio/viewport.hpp` not found.

- [ ] **Step 3: Write the ViewportPanel header**

Create `studio/viewport.hpp`:

```cpp
#pragma once

#include <cstdint>

namespace mosaic::studio
{

struct ViewportSize
{
    std::uint32_t width;
    std::uint32_t height;
};

// Tracks the Viewport panel's measured content size vs. the size last applied to the
// device's offscreen target, so the editor can decide when to resize. One-frame
// latency: the size measured during frame N feeds the offscreen pass of frame N+1.
class ViewportPanel
{
public:
    // Record ImGui's content-region avail for this frame. NaN / < 1 / negative inputs
    // floor to 1 so the offscreen target is never degenerate; fractional sizes truncate.
    void set_measured_size(float avail_w, float avail_h)
    {
        measured_.width  = to_extent(avail_w);
        measured_.height = to_extent(avail_h);
    }

    ViewportSize size() const { return measured_; }

    bool needs_resize() const
    {
        return measured_.width != applied_.width || measured_.height != applied_.height;
    }

    void mark_applied() { applied_ = measured_; }

    float aspect() const
    {
        return static_cast<float>(measured_.width) / static_cast<float>(measured_.height);
    }

private:
    static std::uint32_t to_extent(float v)
    {
        if (!(v >= 1.0f)) // catches NaN, negatives, and < 1
        {
            return 1u;
        }
        return static_cast<std::uint32_t>(v);
    }

    ViewportSize measured_{1, 1};
    ViewportSize applied_{0, 0}; // 0,0 == nothing applied yet -> first frame needs a resize
};

} // namespace mosaic::studio
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R "ViewportPanel"`
Expected: PASS (3 cases).

- [ ] **Step 5: Commit**

```bash
git add studio/viewport.hpp tests/unit/studio/viewport_test.cpp tests/CMakeLists.txt
git commit -m "studio 37d: ViewportPanel sizing model (#37)"
```

---

## Task 3: The `render_scene_to_viewport` ordering seam

**Files:**
- Modify: `studio/viewport.hpp`
- Modify: `tests/unit/studio/viewport_test.cpp`

**Interfaces:**
- Consumes: `ViewportPanel` (Task 2); `mosaic::engine::render::RenderDevice`, `Color` (Task 1).
- Produces: `void mosaic::studio::render_scene_to_viewport(engine::render::RenderDevice& device, const ViewportPanel& panel, const engine::render::Color& clear, const std::function<void(engine::render::RenderDevice&)>& draw_scene)` — records `set_viewport_size` → `begin_viewport_pass` → `draw_scene` → `end_viewport_pass` in order; renders nothing for a degenerate (0-width or 0-height) size.

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/studio/viewport_test.cpp`:

```cpp
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "engine/render/render_device.hpp"

using mosaic::engine::render::Color;
using mosaic::engine::render::DrawParams;
using mosaic::engine::render::Image;
using mosaic::engine::render::MeshHandle;
using mosaic::engine::render::RenderDevice;
using mosaic::engine::render::TextureHandle;
using mosaic::engine::render::Vertex;
using mosaic::engine::render::ViewportTextureId;

namespace
{
// Records the sequence of device calls the viewport seam makes.
class RecordingDevice : public RenderDevice
{
public:
    std::vector<std::string> calls;
    ViewportTextureId        tex = 7; // non-zero sentinel

    void          begin_frame() override {}
    void          clear(const Color&) override {}
    void          end_frame() override {}
    MeshHandle    create_mesh(const std::vector<Vertex>&, const std::vector<std::uint32_t>&) override
    {
        return 1;
    }
    TextureHandle create_texture(const Image&) override { return 1; }
    void          draw(MeshHandle, TextureHandle, const DrawParams&) override
    {
        calls.emplace_back("draw");
    }
    void set_viewport_size(std::uint32_t w, std::uint32_t h) override
    {
        calls.emplace_back("size:" + std::to_string(w) + "x" + std::to_string(h));
    }
    void              begin_viewport_pass(const Color&) override { calls.emplace_back("begin_vp"); }
    void              end_viewport_pass() override { calls.emplace_back("end_vp"); }
    ViewportTextureId viewport_texture() const override { return tex; }
};
} // namespace

TEST_CASE("render_scene_to_viewport records the offscreen pass in order", "[studio][viewport]")
{
    RecordingDevice device;
    mosaic::studio::ViewportPanel panel;
    panel.set_measured_size(640.0f, 480.0f);

    mosaic::studio::render_scene_to_viewport(
        device, panel, Color{0.1f, 0.1f, 0.12f, 1.0f},
        [](RenderDevice& d) { d.draw(1, 1, DrawParams{}); });

    REQUIRE(device.calls == std::vector<std::string>{"size:640x480", "begin_vp", "draw", "end_vp"});
    REQUIRE(device.viewport_texture() == static_cast<ViewportTextureId>(7));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build -j 2>&1 | tail -20`
Expected: compile error — `render_scene_to_viewport` is not a member of `mosaic::studio`.

- [ ] **Step 3: Add the seam to the header**

In `studio/viewport.hpp`, add the includes at the top (below `<cstdint>`):

```cpp
#include <functional>

#include "engine/render/render_device.hpp"
```

Then add, inside `namespace mosaic::studio` after `ViewportPanel`:

```cpp
// Records the offscreen scene pass in the canonical order. `draw_scene` is invoked
// between begin_viewport_pass and end_viewport_pass. A degenerate size renders nothing.
inline void render_scene_to_viewport(
    engine::render::RenderDevice&                                    device,
    const ViewportPanel&                                            panel,
    const engine::render::Color&                                    clear,
    const std::function<void(engine::render::RenderDevice&)>&       draw_scene)
{
    const ViewportSize s = panel.size();
    if (s.width == 0 || s.height == 0)
    {
        return;
    }
    device.set_viewport_size(s.width, s.height);
    device.begin_viewport_pass(clear);
    draw_scene(device);
    device.end_viewport_pass();
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R "render_scene_to_viewport"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add studio/viewport.hpp tests/unit/studio/viewport_test.cpp
git commit -m "studio 37d: render_scene_to_viewport ordering seam (#37)"
```

---

## Task 4: Vulkan lazy swapchain pass (no behavior change)

Refactor `begin_frame` so it no longer begins the swapchain render pass; a private
`ensure_swapchain_pass()` begins it lazily. This is a **pure refactor** — `mosaic run
--window` must render identically. It is the enabling change for the offscreen pass in
Task 5.

**Files:**
- Modify: `modules/render_vulkan/vulkan_render_device.cpp`

**Interfaces:**
- Produces (private, internal): `void ensure_swapchain_pass()`; members `bool swapchain_pass_open_`, `bool viewport_pass_open_`.

- [ ] **Step 1: Add state members**

In the `// --- members ---` block (near `frame_skipped_`), add:

```cpp
    bool swapchain_pass_open_ = false;
    bool viewport_pass_open_  = false;
```

- [ ] **Step 2: Split `begin_frame` — stop beginning the render pass there**

Replace the body of `begin_frame()` from the comment `// Negative-height viewport ...`
through `vkCmdBindPipeline(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);`
(the viewport/scissor/clears/render-pass-begin/pipeline-bind block) with:

```cpp
        swapchain_pass_open_ = false;
        viewport_pass_open_  = false;
```

so `begin_frame()` ends right after `vk_check(vkBeginCommandBuffer(...), ...)` plus those
two resets. Keep the acquire/fence/reset/`vkBeginCommandBuffer` code above unchanged.

- [ ] **Step 3: Add `ensure_swapchain_pass()`**

Add this private method (place it just above `void begin_frame()` or near the other pass
helpers):

```cpp
    // Begins the swapchain render pass once per frame (idempotent). Sets the
    // negative-height (Y-flip) viewport/scissor and binds the scene pipeline. A
    // skipped frame (swapchain out-of-date) never opens a pass.
    void ensure_swapchain_pass()
    {
        if (swapchain_pass_open_ || frame_skipped_)
        {
            return;
        }

        VkViewport viewport{};
        viewport.x        = 0.0f;
        viewport.y        = static_cast<float>(extent_.height);
        viewport.width    = static_cast<float>(extent_.width);
        viewport.height   = -static_cast<float>(extent_.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd_[frame_], 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = extent_;
        vkCmdSetScissor(cmd_[frame_], 0, 1, &scissor);

        std::array<VkClearValue, 2> clears{};
        clears[0].color        = {{clear_color_.r, clear_color_.g, clear_color_.b, clear_color_.a}};
        clears[1].depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo rp{};
        rp.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass        = render_pass_;
        rp.framebuffer       = framebuffers_[image_index_];
        rp.renderArea.offset = {0, 0};
        rp.renderArea.extent = extent_;
        rp.clearValueCount   = static_cast<uint32_t>(clears.size());
        rp.pClearValues      = clears.data();
        vkCmdBeginRenderPass(cmd_[frame_], &rp, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        swapchain_pass_open_ = true;
    }
```

- [ ] **Step 4: Make `draw` open the swapchain pass lazily**

At the very top of `draw(MeshHandle mesh, TextureHandle texture, const DrawParams& params)`
(before any `vkCmd...`), add:

```cpp
        if (!viewport_pass_open_)
        {
            ensure_swapchain_pass();
        }
        if (frame_skipped_)
        {
            return;
        }
```

(If a viewport pass is open, its pipeline/viewport are already bound; otherwise the
swapchain pass must be active before recording draws.)

- [ ] **Step 5: End the swapchain pass in `end_frame`, guaranteed**

In `end_frame()`, replace the line `vkCmdEndRenderPass(cmd_[frame_]);` (right after the
`frame_skipped_` early-return block) with:

```cpp
        ensure_swapchain_pass(); // empty scene still clears + transitions to PRESENT_SRC
        vkCmdEndRenderPass(cmd_[frame_]);
        swapchain_pass_open_ = false;
```

- [ ] **Step 6: Render ImGui inside the swapchain pass**

In `end_ui_frame()`, before `ImGui_ImplVulkan_RenderDrawData(...)`, add
`ensure_swapchain_pass();` so ImGui always records into an open swapchain pass:

```cpp
    void end_ui_frame() override
    {
        if (!ui_ready_) { return; }
        ImGui::Render();
        if (!frame_skipped_)
        {
            ensure_swapchain_pass();
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd_[frame_]);
        }
    }
```

- [ ] **Step 7: Build**

Run: `cmake --build build -j 2>&1 | tail -20`
Expected: builds clean.

- [ ] **Step 8: Verify no regression (unit tests + functional)**

Run: `ctest --test-dir build --output-on-failure`
Expected: all existing tests PASS.

Run: `./build/mosaic run --window --ticks 30; echo "exit=$?"`
Expected: `exit=0`, no error output on stderr (the spinning textured cube renders as before — verified functionally per the standing GUI practice; not screenshotted).

- [ ] **Step 9: Commit**

```bash
git add modules/render_vulkan/vulkan_render_device.cpp
git commit -m "studio 37d: lazy swapchain render pass (enables offscreen pass) (#37)"
```

---

## Task 5: Vulkan offscreen target + viewport hooks

Implement the four neutral hooks in the Vulkan backend: a panel-sized offscreen
color+depth target, its own render pass/framebuffer, and an ImGui-samplable descriptor.

**Files:**
- Modify: `modules/render_vulkan/vulkan_render_device.cpp`

**Interfaces:**
- Consumes: `ensure_swapchain_pass()`, `viewport_pass_open_`, `frame_skipped_` (Task 4); `find_memory_type`, `vk_check`, `kDepthFormat`, `swapchain_format_`, `pipeline_`, `render_pass_` (existing).
- Produces: overrides `set_viewport_size`, `begin_viewport_pass`, `end_viewport_pass`, `viewport_texture`.

- [ ] **Step 1: Add offscreen members**

In the `// --- members ---` block add:

```cpp
    // Offscreen viewport target (37d). Sampled by ImGui into the editor Viewport panel.
    VkExtent2D      vp_extent_ = {0, 0};
    VkRenderPass    vp_render_pass_ = VK_NULL_HANDLE; // format-stable; color = swapchain_format_
    VkImage         vp_color_ = VK_NULL_HANDLE;
    VkDeviceMemory  vp_color_mem_ = VK_NULL_HANDLE;
    VkImageView     vp_color_view_ = VK_NULL_HANDLE;
    VkImage         vp_depth_ = VK_NULL_HANDLE;
    VkDeviceMemory  vp_depth_mem_ = VK_NULL_HANDLE;
    VkImageView     vp_depth_view_ = VK_NULL_HANDLE;
    VkFramebuffer   vp_framebuffer_ = VK_NULL_HANDLE;
    VkSampler       vp_sampler_ = VK_NULL_HANDLE; // persistent across resizes
    VkDescriptorSet vp_imgui_tex_ = VK_NULL_HANDLE; // from ImGui_ImplVulkan_AddTexture
```

- [ ] **Step 2: Create the offscreen render pass in the constructor**

Add a private method:

```cpp
    // Format-stable: color = swapchain_format_ (so the scene pipeline stays compatible),
    // finalLayout SHADER_READ_ONLY_OPTIMAL so ImGui can sample the result directly.
    void create_offscreen_render_pass()
    {
        VkAttachmentDescription color{};
        color.format         = swapchain_format_;
        color.samples        = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference color_ref{};
        color_ref.attachment = 0;
        color_ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription depth{};
        depth.format         = kDepthFormat;
        depth.samples        = VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        depth.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depth_ref{};
        depth_ref.attachment = 1;
        depth_ref.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount    = 1;
        subpass.pColorAttachments       = &color_ref;
        subpass.pDepthStencilAttachment = &depth_ref;

        // Two dependencies: external -> subpass (start), subpass -> external (make the
        // color image visible to the fragment shader sampling it in the ImGui pass).
        std::array<VkSubpassDependency, 2> deps{};
        deps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass    = 0;
        deps[0].srcStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        deps[0].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass    = 0;
        deps[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        std::array<VkAttachmentDescription, 2> attachments{color, depth};

        VkRenderPassCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = static_cast<uint32_t>(attachments.size());
        ci.pAttachments    = attachments.data();
        ci.subpassCount    = 1;
        ci.pSubpasses      = &subpass;
        ci.dependencyCount = static_cast<uint32_t>(deps.size());
        ci.pDependencies   = deps.data();

        vk_check(vkCreateRenderPass(device_, &ci, nullptr, &vp_render_pass_),
                 "vkCreateRenderPass(offscreen)");

        VkSamplerCreateInfo si{};
        si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter    = VK_FILTER_LINEAR;
        si.minFilter    = VK_FILTER_LINEAR;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.maxLod       = 1.0f;
        vk_check(vkCreateSampler(device_, &si, nullptr, &vp_sampler_),
                 "vkCreateSampler(offscreen)");
    }
```

Call it in the constructor right after `create_render_pass();` (near line 156). Locate the
constructor step list and add `create_offscreen_render_pass();` after the `create_render_pass();` line.

- [ ] **Step 3: Add offscreen target create/destroy helpers**

Add two private methods:

```cpp
    void destroy_offscreen_target()
    {
        if (vp_imgui_tex_ != VK_NULL_HANDLE)
        {
            ImGui_ImplVulkan_RemoveTexture(vp_imgui_tex_);
            vp_imgui_tex_ = VK_NULL_HANDLE;
        }
        if (vp_framebuffer_ != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(device_, vp_framebuffer_, nullptr);
            vp_framebuffer_ = VK_NULL_HANDLE;
        }
        if (vp_color_view_ != VK_NULL_HANDLE) { vkDestroyImageView(device_, vp_color_view_, nullptr); vp_color_view_ = VK_NULL_HANDLE; }
        if (vp_color_ != VK_NULL_HANDLE)      { vkDestroyImage(device_, vp_color_, nullptr); vp_color_ = VK_NULL_HANDLE; }
        if (vp_color_mem_ != VK_NULL_HANDLE)  { vkFreeMemory(device_, vp_color_mem_, nullptr); vp_color_mem_ = VK_NULL_HANDLE; }
        if (vp_depth_view_ != VK_NULL_HANDLE) { vkDestroyImageView(device_, vp_depth_view_, nullptr); vp_depth_view_ = VK_NULL_HANDLE; }
        if (vp_depth_ != VK_NULL_HANDLE)      { vkDestroyImage(device_, vp_depth_, nullptr); vp_depth_ = VK_NULL_HANDLE; }
        if (vp_depth_mem_ != VK_NULL_HANDLE)  { vkFreeMemory(device_, vp_depth_mem_, nullptr); vp_depth_mem_ = VK_NULL_HANDLE; }
        vp_extent_ = {0, 0};
    }

    void create_offscreen_target(uint32_t width, uint32_t height)
    {
        // --- color image (sampled + color attachment) ---
        VkImageCreateInfo ci{};
        ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType     = VK_IMAGE_TYPE_2D;
        ci.extent        = {width, height, 1};
        ci.mipLevels     = 1;
        ci.arrayLayers   = 1;
        ci.format        = swapchain_format_;
        ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ci.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ci.samples       = VK_SAMPLE_COUNT_1_BIT;
        ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        vk_check(vkCreateImage(device_, &ci, nullptr, &vp_color_), "vkCreateImage(vp color)");
        bind_image_memory(vp_color_, vp_color_mem_, "vp color");
        vp_color_view_ = make_image_view(vp_color_, swapchain_format_, VK_IMAGE_ASPECT_COLOR_BIT);

        // --- depth image ---
        VkImageCreateInfo di = ci;
        di.format = kDepthFormat;
        di.usage  = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        vk_check(vkCreateImage(device_, &di, nullptr, &vp_depth_), "vkCreateImage(vp depth)");
        bind_image_memory(vp_depth_, vp_depth_mem_, "vp depth");
        vp_depth_view_ = make_image_view(vp_depth_, kDepthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

        // --- framebuffer ---
        std::array<VkImageView, 2> attachments{vp_color_view_, vp_depth_view_};
        VkFramebufferCreateInfo    fb{};
        fb.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass      = vp_render_pass_;
        fb.attachmentCount = static_cast<uint32_t>(attachments.size());
        fb.pAttachments    = attachments.data();
        fb.width           = width;
        fb.height          = height;
        fb.layers          = 1;
        vk_check(vkCreateFramebuffer(device_, &fb, nullptr, &vp_framebuffer_),
                 "vkCreateFramebuffer(offscreen)");

        // --- ImGui descriptor (only if the ImGui-Vulkan backend is up) ---
        if (ui_ready_)
        {
            vp_imgui_tex_ = ImGui_ImplVulkan_AddTexture(
                vp_sampler_, vp_color_view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        vp_extent_ = {width, height};
    }
```

These use two small helpers. If `bind_image_memory` / `make_image_view` do not already
exist in the file, add them (check first with a search for `make_image_view` and
`bind_image_memory`); otherwise reuse the existing ones. Minimal versions:

```cpp
    void bind_image_memory(VkImage image, VkDeviceMemory& mem, const char* what)
    {
        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(device_, image, &req);
        VkMemoryAllocateInfo alloc{};
        alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize  = req.size;
        alloc.memoryTypeIndex = find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vk_check(vkAllocateMemory(device_, &alloc, nullptr, &mem),
                 (std::string("vkAllocateMemory(") + what + ")").c_str());
        vk_check(vkBindImageMemory(device_, image, mem, 0),
                 (std::string("vkBindImageMemory(") + what + ")").c_str());
    }

    VkImageView make_image_view(VkImage image, VkFormat format, VkImageAspectFlags aspect)
    {
        VkImageViewCreateInfo view{};
        view.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image                           = image;
        view.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        view.format                          = format;
        view.subresourceRange.aspectMask     = aspect;
        view.subresourceRange.baseMipLevel   = 0;
        view.subresourceRange.levelCount     = 1;
        view.subresourceRange.baseArrayLayer = 0;
        view.subresourceRange.layerCount     = 1;
        VkImageView out = VK_NULL_HANDLE;
        vk_check(vkCreateImageView(device_, &view, nullptr, &out), "vkCreateImageView");
        return out;
    }
```

- [ ] **Step 4: Implement the four hooks**

Add the overrides (near the UI hooks):

```cpp
    void set_viewport_size(std::uint32_t width, std::uint32_t height) override
    {
        if (width == 0 || height == 0)
        {
            return;
        }
        if (vp_framebuffer_ != VK_NULL_HANDLE && width == vp_extent_.width &&
            height == vp_extent_.height)
        {
            return; // unchanged
        }
        vkDeviceWaitIdle(device_); // resizes are transient (panel drag); idle is fine for v0.1
        destroy_offscreen_target();
        create_offscreen_target(width, height);
    }

    void begin_viewport_pass(const Color& clear) override
    {
        if (frame_skipped_ || vp_framebuffer_ == VK_NULL_HANDLE)
        {
            return;
        }

        VkViewport viewport{};
        viewport.x        = 0.0f;
        viewport.y        = static_cast<float>(vp_extent_.height);
        viewport.width    = static_cast<float>(vp_extent_.width);
        viewport.height   = -static_cast<float>(vp_extent_.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd_[frame_], 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = vp_extent_;
        vkCmdSetScissor(cmd_[frame_], 0, 1, &scissor);

        std::array<VkClearValue, 2> clears{};
        clears[0].color        = {{clear.r, clear.g, clear.b, clear.a}};
        clears[1].depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo rp{};
        rp.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass        = vp_render_pass_;
        rp.framebuffer       = vp_framebuffer_;
        rp.renderArea.offset = {0, 0};
        rp.renderArea.extent = vp_extent_;
        rp.clearValueCount   = static_cast<uint32_t>(clears.size());
        rp.pClearValues      = clears.data();
        vkCmdBeginRenderPass(cmd_[frame_], &rp, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        viewport_pass_open_ = true;
    }

    void end_viewport_pass() override
    {
        if (!viewport_pass_open_)
        {
            return;
        }
        vkCmdEndRenderPass(cmd_[frame_]);
        viewport_pass_open_ = false;
    }

    ViewportTextureId viewport_texture() const override
    {
        return reinterpret_cast<ViewportTextureId>(vp_imgui_tex_);
    }
```

Note: `viewport_texture()` returns the `VkDescriptorSet` reinterpreted as the opaque
`ViewportTextureId` (this is exactly what `ImTextureID` holds for the ImGui-Vulkan
backend). `ViewportTextureId` from `render_device.hpp` must be brought into scope — the
file already uses `mosaic::engine::render` types via `using` or qualified names; qualify
as needed.

- [ ] **Step 5: Destroy the offscreen target on teardown**

In the destructor, before the ImGui shutdown and before `vkDestroyRenderPass(device_,
render_pass_, ...)`, add:

```cpp
        destroy_offscreen_target();
        if (vp_sampler_ != VK_NULL_HANDLE) { vkDestroySampler(device_, vp_sampler_, nullptr); }
        if (vp_render_pass_ != VK_NULL_HANDLE) { vkDestroyRenderPass(device_, vp_render_pass_, nullptr); }
```

(Ensure this runs after `vkDeviceWaitIdle(device_)` which the destructor already calls
before tearing resources down. `destroy_offscreen_target` calls
`ImGui_ImplVulkan_RemoveTexture`, so it must run while the ImGui-Vulkan backend is still
initialized — i.e. before `ImGui_ImplVulkan_Shutdown()`.)

- [ ] **Step 6: Build**

Run: `cmake --build build -j 2>&1 | tail -25`
Expected: builds clean.

- [ ] **Step 7: Verify no regression**

Run: `ctest --test-dir build --output-on-failure`
Expected: all tests PASS.

Run: `./build/mosaic run --window --ticks 30; echo "exit=$?"`
Expected: `exit=0`, silent stderr (the non-editor path never calls the viewport hooks; unchanged).

- [ ] **Step 8: Commit**

```bash
git add modules/render_vulkan/vulkan_render_device.cpp
git commit -m "studio 37d: Vulkan offscreen viewport target + hooks (#37)"
```

---

## Task 6: Editor wiring — live scene + Viewport panel

Give `mosaic editor` a live demo scene rendered into the offscreen target and shown in a
dockable Viewport panel. Extract the demo scene into a shared header so `mosaic run` and
`mosaic editor` build the same scene (removes duplication).

**Files:**
- Create: `app/run/demo_scene.hpp`
- Modify: `app/main.cpp`
- Modify: `app/run/editor.cpp`
- Modify: `studio/studio_app.hpp`, `studio/studio_app.cpp`
- Modify: `studio/studio_layout.cpp`

**Interfaces:**
- Produces: `mosaic::app::SpinSystem`; `mosaic::app::DemoScene { std::shared_ptr<engine::render::RenderSystem> render_system; }`; `DemoScene mosaic::app::build_demo_scene(engine::World& world, engine::render::RenderDevice& device, engine::assets::AssetRegistry& registry)` — adds a `SpinSystem`, a textured cube, directional + point light, and a placed `Camera`, and returns a `RenderSystem`. The caller adds `TransformSystem` and ticks the world.
- Produces: `StudioApp::viewport()` → `ViewportPanel&`; `StudioApp::draw_ui` renders a `"Viewport"` panel via `viewport_texture()`.

- [ ] **Step 1: Extract the demo scene into a shared header**

Create `app/run/demo_scene.hpp`, moving the `SpinSystem` class out of `app/main.cpp` and
capturing the scene block (`app/main.cpp` lines ~181–240):

```cpp
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "engine/assets/asset_metadata.hpp"
#include "engine/assets/asset_registry.hpp"
#include "engine/ecs/system.hpp"
#include "engine/render/camera.hpp"
#include "engine/render/cube.hpp"
#include "engine/render/directional_light.hpp"
#include "engine/render/image.hpp"
#include "engine/render/material.hpp"
#include "engine/render/mesh_renderer.hpp"
#include "engine/render/point_light.hpp"
#include "engine/render/render_device.hpp"
#include "engine/render/render_system.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::app
{

// Spins every entity that has a MeshRenderer (the cube), leaving the camera still.
class SpinSystem : public mosaic::engine::ecs::System
{
public:
    void update(mosaic::engine::World& world, float dt) override
    {
        namespace m = mosaic::foundation::math;
        angle_ += dt * 0.9F;
        const float   half = angle_ * 0.5F;
        const m::Quat spin{0.0F, std::sin(half), 0.0F, std::cos(half)};
        world.for_each<mosaic::engine::transform::Transform>(
            [&](mosaic::foundation::EntityID e, mosaic::engine::transform::Transform& t)
            {
                if (!world.has_component<mosaic::engine::render::MeshRenderer>(e))
                {
                    return;
                }
                t.rotation = spin;
            });
    }

private:
    float angle_ = 0.0F;
};

struct DemoScene
{
    std::shared_ptr<mosaic::engine::render::RenderSystem> render_system;
};

// Builds the standard v0.1 demo (textured spinning cube, directional + point light,
// placed camera) into `world`, uploading the cube mesh + checkerboard texture through
// `device`/`registry`. Adds a SpinSystem. The caller is responsible for adding a
// TransformSystem and ticking the world.
inline DemoScene build_demo_scene(mosaic::engine::World&                   world,
                                  mosaic::engine::render::RenderDevice&    device,
                                  mosaic::engine::assets::AssetRegistry&   registry)
{
    namespace render    = mosaic::engine::render;
    namespace transform = mosaic::engine::transform;
    using mosaic::foundation::math::Vec3;

    world.add_system<SpinSystem>();

    std::vector<render::Vertex>  cube_v;
    std::vector<std::uint32_t>   cube_i;
    render::make_cube_mesh(cube_v, cube_i);
    const render::MeshHandle cube = device.create_mesh(cube_v, cube_i);

    const auto cube_entity = world.create_entity();
    world.add_component<transform::Transform>(cube_entity, transform::Transform{});
    world.add_component<transform::WorldTransform>(
        cube_entity, transform::WorldTransform{mosaic::foundation::math::Mat4::identity()});
    world.add_component<render::MeshRenderer>(cube_entity, render::MeshRenderer{cube});

    auto checker    = std::make_shared<render::Image>();
    checker->width  = 64;
    checker->height = 64;
    checker->pixels.resize(64 * 64 * 4);
    for (std::uint32_t y = 0; y < 64; ++y)
        for (std::uint32_t x = 0; x < 64; ++x)
        {
            const bool         on = ((x / 8) + (y / 8)) % 2 == 0;
            const std::uint8_t c  = on ? 230 : 60;
            std::uint8_t*      p  = &checker->pixels[(y * 64 + x) * 4];
            p[0] = c; p[1] = c; p[2] = c; p[3] = 255;
        }
    std::array<std::uint8_t, 16> tex_uuid{};
    tex_uuid[0] = 0xC0;
    tex_uuid[1] = 0xDE;
    const mosaic::foundation::serialization::AssetID tex_id{tex_uuid};
    registry.register_resource<render::Image>(
        mosaic::engine::assets::AssetMetadata{tex_id, "texture"}, checker);
    world.add_component<render::Material>(cube_entity, render::Material{Vec3{1.0F, 1.0F, 1.0F}, tex_id});

    const auto light_entity = world.create_entity();
    world.add_component<render::DirectionalLight>(
        light_entity, render::DirectionalLight{Vec3{-0.5F, -1.0F, -0.3F}, Vec3{1.0F, 1.0F, 1.0F}});

    const auto point_light_entity = world.create_entity();
    world.add_component<transform::Transform>(
        point_light_entity,
        transform::Transform{Vec3{1.5F, 1.0F, 1.5F}, mosaic::foundation::math::Quat::identity(),
                             Vec3{1.0F, 1.0F, 1.0F}, {}});
    world.add_component<transform::WorldTransform>(
        point_light_entity,
        transform::WorldTransform{mosaic::foundation::math::Mat4::translation(Vec3{1.5F, 1.0F, 1.5F})});
    world.add_component<render::PointLight>(point_light_entity,
                                            render::PointLight{Vec3{1.0F, 0.85F, 0.6F}, 3.0F, 6.0F});

    const auto camera_entity = world.create_entity();
    world.add_component<transform::Transform>(
        camera_entity,
        transform::Transform{Vec3{0.0F, 0.0F, 4.0F}, mosaic::foundation::math::Quat::identity(),
                             Vec3{1.0F, 1.0F, 1.0F}, {}});
    world.add_component<transform::WorldTransform>(
        camera_entity,
        transform::WorldTransform{mosaic::foundation::math::Mat4::translation(Vec3{0.0F, 0.0F, 4.0F})});
    world.add_component<render::Camera>(camera_entity, render::Camera{});

    return DemoScene{std::make_shared<render::RenderSystem>()};
}

} // namespace mosaic::app
```

- [ ] **Step 2: Refactor `app/main.cpp` onto the shared header**

In `app/main.cpp`: delete the `SpinSystem` class (lines ~52–73) and replace the inline
scene block (the `world.add_system<SpinSystem>();` through the `auto render_system =
std::make_shared<render::RenderSystem>();` lines, ~181–240) with:

```cpp
        auto render_system = build_demo_scene(world, *render_device, asset_registry).render_system;
```

Add `#include "app/run/demo_scene.hpp"` to the includes and remove now-unused includes only
if the compiler flags them (leave them otherwise — they are harmless). The `on_frame`
lambda and everything else stay as-is.

- [ ] **Step 3: Build + verify `mosaic run` unchanged**

Run: `cmake --build build -j 2>&1 | tail -20 && ./build/mosaic run --window --ticks 30; echo "exit=$?"`
Expected: builds clean; `exit=0`, silent stderr (same spinning cube as before).

- [ ] **Step 4: Commit the extraction**

```bash
git add app/run/demo_scene.hpp app/main.cpp
git commit -m "studio 37d: extract shared build_demo_scene helper (#37)"
```

- [ ] **Step 5: Register the Viewport panel in the layout**

In `studio/studio_layout.cpp`, add a `"Viewport"` panel alongside the existing
`"Hierarchy"`/`"Inspector"` panels (match the exact construction pattern already there —
find where `panels_` is populated and add an entry named `"Viewport"`, `open = true`).

- [ ] **Step 6: Add a ViewportPanel to StudioApp + a Viewport panel body**

In `studio/studio_app.hpp`, add the include `#include "studio/viewport.hpp"`, a member
`ViewportPanel viewport_;`, and an accessor `ViewportPanel& viewport() { return viewport_; }`.

In `studio/studio_app.cpp`, extend the panel dispatch in `draw_ui` (the
`if (panel.name == "Hierarchy") ... else if (... "Inspector") ...` chain) with:

```cpp
            else if (panel.name == "Viewport")
            {
                draw_viewport(device);
            }
```

`draw_ui` needs the device to read `viewport_texture()`. Add a `RenderDevice& device`
parameter to `draw_ui` (and thread it from `run_editor`). Add the method:

```cpp
void StudioApp::draw_viewport(mosaic::engine::render::RenderDevice& device)
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    viewport_.set_measured_size(avail.x, avail.y);

    const auto tex = device.viewport_texture();
    if (tex != 0)
    {
        const ViewportSize s = viewport_.size();
        ImGui::Image(reinterpret_cast<ImTextureID>(tex),
                     ImVec2(static_cast<float>(s.width), static_cast<float>(s.height)));
    }
    else
    {
        ImGui::TextUnformatted("(no viewport)");
    }
}
```

Declare `draw_viewport(mosaic::engine::render::RenderDevice&)` in `studio_app.hpp` and add
`#include "engine/render/render_device.hpp"` there. Update the `draw_ui` signature in the
header to accept `mosaic::engine::render::RenderDevice& device` as its first parameter.

- [ ] **Step 7: Wire the editor frame — offscreen pass before the UI**

In `app/run/editor.cpp`:

1. Add includes: `#include "app/run/demo_scene.hpp"`, `#include "engine/transform/transform_system.hpp"`, `#include "engine/assets/asset_registry.hpp"`, `#include "studio/viewport.hpp"`.
2. Replace the idle demo world (lines ~87–103) with a live scene:

```cpp
    engine::assets::AssetRegistry asset_registry;
    engine::World                 world;
    world.add_system<engine::transform::TransformSystem>();
    auto render_system = build_demo_scene(world, *device, asset_registry).render_system;
```

3. Replace the `on_frame` lambda (lines ~120–128) with the two-pass sequence:

```cpp
    deps.on_frame = [window, &device, &studio, &world, &reflection, render_system, &asset_registry] {
        glfwPollEvents();
        device->begin_frame();
        device->clear(engine::render::Color{0.10f, 0.10f, 0.12f, 1.0f});

        // Pass 1: scene -> panel-sized offscreen target.
        mosaic::studio::ViewportPanel& vp = studio.viewport();
        if (vp.needs_resize())
        {
            device->set_viewport_size(vp.size().width, vp.size().height);
            vp.mark_applied();
        }
        render_system->set_aspect(vp.aspect());
        mosaic::studio::render_scene_to_viewport(
            *device, vp, engine::render::Color{0.05f, 0.05f, 0.07f, 1.0f},
            [&](engine::render::RenderDevice& d) { render_system->render(world, d, asset_registry); });

        // Pass 2: ImGui UI (samples the offscreen target in the Viewport panel).
        device->begin_ui_frame();
        studio.draw_ui(*device, world, reflection);
        device->end_ui_frame();
        device->end_frame();
    };
```

(Note `render_scene_to_viewport` already calls `set_viewport_size`; the explicit
`needs_resize`/`mark_applied` above keeps the `ViewportPanel`'s applied-size bookkeeping in
sync so it reflects reality — `set_viewport_size` on the device is idempotent for an
unchanged size.)

- [ ] **Step 8: Build**

Run: `cmake --build build -j 2>&1 | tail -25`
Expected: builds clean.

- [ ] **Step 9: Verify the full suite + the editor functionally**

Run: `ctest --test-dir build --output-on-failure`
Expected: all tests PASS (including the new Task 1–3 tests).

Run: `./build/mosaic editor --ticks 60; echo "exit=$?"`
Expected: `exit=0`, silent stderr. The editor window shows the spinning textured cube
inside a dockable Viewport panel (verified functionally; the sandbox cannot screenshot the
GUI). If `--ticks` is not honored by the editor path, use `timeout 5 ./build/mosaic editor; echo "exit=$?"` and expect a clean shutdown.

- [ ] **Step 10: Commit**

```bash
git add app/run/editor.cpp studio/studio_app.hpp studio/studio_app.cpp studio/studio_layout.cpp
git commit -m "studio 37d: editor viewport panel + offscreen scene pass (#37)"
```

---

## Task 7: Update the build handoff (next-steps)

**Files:**
- Modify: `docs/project/next-steps.md`

- [ ] **Step 1: Mark 37d complete and point at 37e**

Update `docs/project/next-steps.md`: change the "Last updated" line and the Studio status
so **37d (viewport) is built**, and the **next buildable is 37e (play/stop + console)**.
Note that the RenderGraph remained deferred (direct two-pass submission was chosen) and
re-file it to the first dynamic/≥3-pass slice. Update the Studio suggested-order lines
(37a ✓ → 37b ✓ → 37c ✓ → 37d ✓ → **37e**).

- [ ] **Step 2: Commit (rides on the feature branch, not its own PR)**

```bash
git add docs/project/next-steps.md
git commit -m "next-steps: Studio 37d viewport built; next is 37e (play/stop + console) (#37)"
```

---

## Self-Review

**Spec coverage:**
- Neutral hooks (spec §Architecture / neutral device hooks) → Task 1. ✓
- Lazy swapchain pass (spec §lazy swapchain pass) → Task 4. ✓
- Offscreen target + ImGui descriptor (spec §offscreen target) → Task 5. ✓
- `ViewportPanel` model (spec §ViewportPanel model) → Task 2. ✓
- `render_scene_to_viewport` + fake-device hook-order test (spec §testability) → Task 3. ✓
- Editor demo scene + frame ordering + Viewport panel (spec §editor frame ordering / studio Viewport panel) → Task 6. ✓
- Render-graph stays deferred (spec §render-graph decision) → Global Constraints + Task 7 re-file. ✓
- Functional GUI acceptance (spec §testability) → Tasks 4/5/6 verification steps. ✓

**Placeholder scan:** No TBD/TODO; every code step shows complete code; commands have expected output. ✓

**Type consistency:** `ViewportTextureId` (`std::uint64_t`) used identically in Tasks 1/3/5/6; `ViewportSize`/`ViewportPanel`/`render_scene_to_viewport` signatures match across Tasks 2/3/6; `build_demo_scene`/`DemoScene`/`SpinSystem` consistent across Tasks 1(main)/6; hook names (`set_viewport_size`, `begin_viewport_pass`, `end_viewport_pass`, `viewport_texture`) identical everywhere. ✓

**One open verification risk (flagged, not blocking):** whether `mosaic editor` honors
`--ticks` for a bounded headless run, and whether the sandbox has a display for GLFW window
creation. The prior render slices ran `mosaic run --window` successfully, so a display
exists; if `--ticks` is not wired for the editor path, Task 6 Step 9 falls back to a
`timeout`-bounded run. Neither changes the design.
