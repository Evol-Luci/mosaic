# Studio 37d — Viewport (design)

**Tracking issue:** [#37 Studio shell](https://github.com/Evol-Luci/mosaic/issues/37).
**Decomposition:** [studio-decomposition](2026-08-22-studio-decomposition.md) (slice 37d).
**Builds on:** 37a (editor shell + neutral UI-frame hooks), 37b (hierarchy), 37c (inspector),
and the rendering subsystem (36a–36e-1: `RenderDevice`, `RenderSystem`).
**Date:** 2026-08-23.

## Summary

Embed the renderer's live 3D output in a dockable **Viewport** panel. The scene renders
into a **panel-sized offscreen color+depth target**; ImGui then samples that target into
the Viewport panel via `ImGui::Image`. This is the first slice with **two real passes**
— (1) scene → offscreen, (2) ImGui → swapchain — the condition under which the 36e
decomposition deferred the `RenderGraph`.

## Scope

**In:**
- Four **default-no-op** viewport hooks on the neutral `RenderDevice`
  (`set_viewport_size`, `begin_viewport_pass`, `end_viewport_pass`, `viewport_texture`).
- Their Vulkan implementation: an offscreen render target (color + depth), its own
  render pass/framebuffer, an ImGui-samplable descriptor, and a **lazy swapchain pass**
  refactor so the two consumers (`mosaic run --window`, `mosaic editor`) coexist without
  changing the existing device contract.
- A headlessly-testable **`ViewportPanel`** model (size tracking, resize decision, aspect,
  min-size clamp).
- Wiring: `mosaic editor` gains a live demo scene + `RenderSystem` (mirroring
  `mosaic run --window`), and the editor frame renders scene→offscreen then draws the
  Viewport panel.

**Deferred (later / YAGNI):** camera manipulation / fly-cam / orbit controls, viewport
gizmos, mouse picking (click-in-viewport to select), grid/overlay rendering, and a formal
`RenderGraph`. This slice embeds the render; interacting with it is later work.

## The render-graph decision (settled here)

The decomposition delegated the "formalize a `RenderGraph` vs. minimal direct two-pass
submission" call to this slice. **Decision: minimal direct two-pass submission; the
`RenderGraph` stays deferred.**

The two passes have a **fixed, hardcoded order** (offscreen scene, then UI), are recorded
into **one command buffer**, and are **submitted once**. The single inter-pass hazard —
the offscreen color image transitioning from color-attachment to shader-read — is handled
declaratively by the offscreen render pass's `finalLayout = SHADER_READ_ONLY_OPTIMAL`
plus a subpass dependency, not by hand-authored barriers. A `RenderGraph` earns its
complexity when the pass **set** is dynamic or large enough that manual ordering/barrier
derivation becomes error-prone (shadow-map pre-passes across multiple lights,
post-processing chains, resource aliasing). Two fixed passes do not reveal those
requirements. Building the graph now would repeat, at N=2, exactly the "a one-pass graph
was ceremony" mistake the 36e deferral warned against.

**Re-file:** the `RenderGraph` is deferred (not dropped) to the first slice with a
genuinely dynamic or ≥3-pass set — a shadow-map depth pre-pass or a post-processing chain
is the likely trigger.

## Architecture

### Neutral device hooks (engine tier — `engine/render/render_device.hpp`)

Four new virtuals, all **default no-op** so headless devices, test fakes, and the
`mosaic run` path are unaffected:

```cpp
using ViewportTextureId = std::uint64_t;   // ImTextureID-compatible opaque handle; 0 == none

// Ensure a (w x h) offscreen color+depth target exists; recreate iff the size changed.
// Sizes are in pixels; (0,0) is a no-op (a collapsed panel renders nothing).
virtual void set_viewport_size(std::uint32_t w, std::uint32_t h) {}

// Begin/end the offscreen scene pass. Between them, draw() targets the offscreen target
// instead of the swapchain. `clear` is the offscreen clear color.
virtual void begin_viewport_pass(const Color& clear) {}
virtual void end_viewport_pass() {}

// The offscreen color target as an ImGui-samplable id (valid after >= 1 viewport pass).
// 0 when there is no viewport target.
virtual ViewportTextureId viewport_texture() const { return 0; }
```

**The render boundary holds** ([boundaries.md §3](../../architecture/boundaries.md)):
these are neutral, Vulkan-free signatures; only `modules/render_vulkan` implements them.
`ViewportTextureId` is a plain integer the studio passes back to `ImGui::Image` as an
`ImTextureID` — the studio never sees a Vulkan type.

### Lazy swapchain pass (Vulkan backend — internal refactor)

Today `begin_frame()` couples "start recording" with "begin the swapchain render pass +
bind pipeline." The editor needs to record the offscreen pass **first**, before the
swapchain pass, so that coupling must break. The change is **internal to the Vulkan
device**; the neutral contract is unchanged.

- `begin_frame()` — acquire image + begin the command buffer only. **No render pass.**
- `ensure_swapchain_pass()` (private) — begins the swapchain render pass, sets the
  negative-height (Y-flip) viewport/scissor, and binds the scene pipeline, **once per
  frame**, idempotent. Called lazily from the first `draw()` that targets the swapchain,
  from `end_ui_frame()` (so ImGui always lands in the swapchain pass), and from
  `end_frame()` (so an empty scene still clears + transitions to `PRESENT_SRC_KHR`).
- `draw()` — records into the **active** pass: the offscreen pass if one is open,
  otherwise `ensure_swapchain_pass()` then the swapchain pass.
- `end_frame()` — `ensure_swapchain_pass()` (idempotent), end the swapchain pass, submit,
  present. Unchanged submit/present logic.

Consequence: **`mosaic run --window`, `app/main.cpp`, and the fake test devices need no
changes** — `begin_frame → clear → draw… → end_frame` behaves exactly as before, only
the render-pass begin moves from `begin_frame` into the lazy path.

### Offscreen target (Vulkan backend)

- **Color image:** format = `swapchain_format_` (so the **existing scene pipeline stays
  render-pass-compatible** — no second pipeline), `usage = COLOR_ATTACHMENT_BIT |
  SAMPLED_BIT`, device-local; an image view + a sampler (linear, clamp-to-edge).
- **Depth image:** `kDepthFormat` (`D32_SFLOAT`), `usage = DEPTH_STENCIL_ATTACHMENT_BIT`,
  matching the swapchain depth setup.
- **Offscreen render pass:** color `finalLayout = SHADER_READ_ONLY_OPTIMAL` (transition
  to sampleable is free at pass end) with a subpass dependency for the
  color-attachment-write → shader-read hazard; depth as in the swapchain pass. Color
  `loadOp = CLEAR` (from `begin_viewport_pass`'s clear), `storeOp = STORE`.
- **ImGui descriptor:** `ImGui_ImplVulkan_AddTexture(sampler, view,
  SHADER_READ_ONLY_OPTIMAL)` returns a `VkDescriptorSet`; `viewport_texture()` returns it
  cast to `ViewportTextureId`. On recreate, `ImGui_ImplVulkan_RemoveTexture` the old one
  first.
- **`set_viewport_size(w,h)`:** if `(w,h)` differs from the current target and is
  non-zero, `vkDeviceWaitIdle` then destroy + recreate the color/depth images, view,
  framebuffer, and ImGui descriptor at the new size. Resizes are transient (a panel drag),
  so idling is acceptable for v0.1. The offscreen viewport/scissor are sized to `(w,h)`
  with the same negative-height Y-flip as the swapchain so the sampled image reads
  upright.
- **Teardown:** offscreen resources are destroyed in the device destructor (before ImGui
  and the device), symmetric with the existing texture/mesh teardown.

### `ViewportPanel` model (studio tier — headlessly testable)

A tiny value type carrying the panel↔device sizing logic, unit-tested without a window:

```cpp
struct ViewportSize { std::uint32_t width, height; };

class ViewportPanel
{
public:
    // Record the size ImGui measured this frame (content-region avail), clamped to >= 1.
    void set_measured_size(float avail_w, float avail_h);

    ViewportSize size() const;         // current clamped size (>= 1x1)
    bool needs_resize() const;         // measured size differs from the last applied size
    void mark_applied();               // call after set_viewport_size(size()) succeeds
    float aspect() const;              // width / height (for RenderSystem::set_aspect)
};
```

Rationale: the panel size that feeds `set_viewport_size` is the size **measured during the
previous ImGui frame** — the standard one-frame-latency pattern (you cannot know the panel
size until you are inside the ImGui frame, but the offscreen pass must run *before* it).
`ViewportPanel` isolates this bookkeeping (clamp, change-detection, aspect) from the draw
so it can be tested; the imperceptible one-frame lag on active resize self-corrects.

### Editor frame ordering (`app/run/editor.cpp`)

The editor world today is idle (no meshes) — nothing would render. This slice gives it a
**live demo scene** mirroring `mosaic run --window` (cube + checkerboard texture,
directional + point light, a placed `Camera`) plus a `SpinSystem` and a `RenderSystem`, so
the Viewport actually shows the spinning, lit, textured cube. The per-frame order:

```
begin_frame
set_viewport_size(panel.size())          // previous-frame measured size
begin_viewport_pass(sceneClear)
  render_system.render(world, device, assets)   // set_aspect(panel.aspect()) first
end_viewport_pass
begin_ui_frame
  draw_ui(...)   // Viewport panel: measure avail → panel.set_measured_size(...);
                 //                 ImGui::Image(viewport_texture(), panel.size())
end_ui_frame     // ImGui → swapchain (lazy pass)
end_frame
```

The `RenderSystem` still ticks each frame; the world advances via the existing
`FramePump`/`RuntimeLoop`, so the cube spins exactly as in `mosaic run`.

### Studio Viewport panel (`studio/studio_app.cpp`)

A new `"Viewport"` panel in the layout. Its body: measure `ImGui::GetContentRegionAvail()`
→ `panel.set_measured_size(...)`; if `viewport_texture() != 0` draw
`ImGui::Image((ImTextureID)tex, {w,h})`, else a `"(no viewport)"` placeholder. The
`StudioApp` owns the `ViewportPanel` and exposes `viewport()` so `run_editor` can read
`size()`/`aspect()`/`needs_resize()` for the pre-UI offscreen pass.

## Testability boundary

Consistent with every render/studio slice, and the standing note that **the sandbox cannot
screenshot the GUI window** — functional launch is the acceptance for the draw path.

- **Unit-tested (no window):**
  - `ViewportPanel`: min-size clamp (0 → 1), `needs_resize` toggles on change and clears on
    `mark_applied`, `aspect` computation, integer rounding of fractional avail sizes.
  - A **fake `RenderDevice`** recording the hook-call sequence: assert the editor-style
    order (`set_viewport_size` → `begin_viewport_pass` → `draw`* → `end_viewport_pass` →
    UI), that `draw` between viewport begin/end is attributed to the viewport pass, and
    that `viewport_texture()` returns the fake's non-zero handle after a pass.
- **Verified functionally:** `mosaic editor` launches, exits 0, silent stderr (offscreen
  target created, scene rendered into it, ImGui samples it). Use `--duration`/`--ticks` to
  bound the run, as the other GUI checks do.

## Ground rules honored

- **Tier contract:** `engine/render` stays Vulkan-free (neutral hooks only);
  `modules/render_vulkan` is the sole implementer; `studio/` drives panels through ImGui +
  neutral hooks and never names a Vulkan type. `foundation/`/`engine/` never depend on
  `studio/` or ImGui.
- **No new third-party deps** — ImGui (docking) and the Vulkan backend are already present.
- **`RenderGraph` deferral** is re-filed, not dropped (see above).

## Acceptance

- [ ] Neutral `RenderDevice` gains the four default-no-op viewport hooks; headless/fake
      devices unaffected.
- [ ] Vulkan backend renders the scene into a panel-sized offscreen target and exposes it
      as an ImGui-samplable id; the lazy swapchain-pass refactor leaves `mosaic run
      --window` behavior unchanged.
- [ ] `ViewportPanel` unit tests green (clamp, resize decision, aspect).
- [ ] Fake-device hook-order unit test green.
- [ ] `mosaic editor` shows the live spinning cube inside a dockable Viewport panel;
      launches, exits 0, silent stderr.
- [ ] Full unit suite green.

## Depends on / relates

- **Depends on:** 37a (UI-frame hooks + editor shell), the `RenderSystem`/`RenderDevice`
  (36a–36e-1), #22 runtime loop (the frame pump that spins the world).
- **Relates:** unblocks 37e (play/stop drives the same world now visible in the viewport);
  the deferred `RenderGraph` lands in the first ≥3-pass / dynamic-pass slice.
