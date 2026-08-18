# Rendering 36a — Device + Window + Swapchain (Clear-Screen) — Design

**Issue:** [#42 — 36a: Render device + window + swapchain bring-up (clear-screen)](https://github.com/Evol-Luci/mosaic/issues/42)
**Tracking:** [#36 Rendering](https://github.com/Evol-Luci/mosaic/issues/36) · **Decomposition:** [2026-08-17-rendering-decomposition.md](2026-08-17-rendering-decomposition.md)
**Status:** Accepted
**Date:** 2026-08-17

## Purpose

The first *visible* slice: stand up a window and a Vulkan backend behind a
backend-neutral render interface, and clear the screen to a color every frame from
the runtime loop. It establishes the `RenderDevice` interface location and the
`modules/render_vulkan/` module that every later rendering slice (36b–36e) extends.
No geometry, camera, materials, or render graph yet.

## Architectural spine (inherited from the decomposition)

- **Backend-neutral interface.** Engine/app code targets a `RenderDevice` interface;
  **no engine/app code touches Vulkan** except the backend module and the concrete
  factory call — the [render boundary](../../architecture/boundaries.md#3-the-render-boundary).
- **Vulkan under `modules/render_vulkan/`** (a new module tier above `engine/`).
- **GLFW** for the window/surface (backend/app tier; never `engine/`/`foundation/`).
- **RenderGraph is deferred to 36e** — 36a submits directly behind the interface.

## Components & layout

### `engine/render/render_device.hpp` (neutral interface, header-only)

```cpp
namespace mosaic::engine::render
{
struct Color { float r, g, b, a; };

// Backend-neutral rendering device. A backend (e.g. Vulkan) implements it; engine
// and app code never see the backend's types. 36a clears the screen; 36b adds
// draw() between clear() and end_frame().
class RenderDevice
{
public:
    virtual ~RenderDevice() = default;
    virtual void begin_frame() = 0;       // acquire the next swapchain image
    virtual void clear(const Color&) = 0; // clear the acquired image to a color
    virtual void end_frame() = 0;         // submit + present
};
} // namespace mosaic::engine::render
```

No Vulkan or GLFW includes. Header-only; part of the engine target's include surface.

### `modules/render_vulkan/` (the Vulkan backend — `mosaic_render_vulkan` static lib)

- **`vulkan_render_device.{hpp,cpp}`** — `VulkanRenderDevice : RenderDevice`, plus the
  factory `std::unique_ptr<RenderDevice> create_vulkan_render_device(GLFWwindow*)`.
  The factory header is what the app includes to construct the backend; it exposes
  only the factory + neutral return type (the concrete class can stay in the .cpp /
  a private header).
- **`vulkan_swapchain_support.{hpp,cpp}`** — the **headlessly-testable pure helpers**
  (see Testing): `choose_surface_format`, `choose_present_mode`, `clamp_extent`,
  `pick_queue_families`. These take plain candidate data (vectors / structs), no live
  device, so they unit-test without a GPU.
- Links `Vulkan::Vulkan`, `glfw`, and `mosaic_engine` (for the neutral interface);
  privately uses `foundation/logging`.

### `app/` (window ownership + windowed mode)

- The app owns the `GLFWwindow` (app-tier). A new **`--window`** flag opens a window
  and renders; **without it, behavior is exactly the current headless path** (so the
  existing `--ticks` smoke/CI runs keep working with no display).
- `main` (windowed mode): `glfwInit`; create the window; `create_vulkan_render_device(win)`;
  set the pump's `on_frame` to `[glfwPollEvents; device->begin_frame(); device->clear(bg); device->end_frame()]`
  and `stop_requested` to `[glfwWindowShouldClose(win) || g_stop]`; run the pump;
  then ordered teardown.

## Loop integration

`PumpDeps` (in `app/run/frame_pump.hpp`) gains one field:

```cpp
struct PumpDeps
{
    std::function<double()>     now;
    std::function<void(double)> sleep;
    std::function<bool()>       stop_requested;
    std::function<void()>       on_frame;   // NEW: per-frame hook, may be empty
};
```

`FramePump::run` calls it once per iteration **after `advance`, before pacing**,
guarded: `if (deps_.on_frame) { deps_.on_frame(); }`. This is additive — existing
`PumpDeps` aggregate-inits (three fields) leave `on_frame` empty, so all existing
tests and the headless path are unchanged. The pump stays rendering-agnostic; it
knows nothing of windows or Vulkan.

## Vulkan bring-up (inside the backend)

1. **Instance** — extensions from `glfwGetRequiredInstanceExtensions`; in **debug**
   builds enable `VK_LAYER_KHRONOS_validation` and a debug-utils messenger that
   routes messages through `foundation/logging` (`log::warn`/`log::error`, category
   `"vulkan"`). Release builds omit both.
2. **Surface** — `glfwCreateWindowSurface(instance, window, ...)`.
3. **Physical device** — first device that has a graphics queue family, a
   present-capable queue family, the swapchain extension, and ≥1 surface format and
   present mode (`pick_queue_families` + a suitability check).
4. **Logical device + queues** — graphics and present queues (may be the same family).
5. **Swapchain group** (the recreatable unit): `choose_surface_format` (prefer
   `B8G8R8A8_SRGB` + `SRGB_NONLINEAR`, else first), `choose_present_mode`
   (`MAILBOX` → `FIFO` fallback), `clamp_extent` (framebuffer size clamped to surface
   caps), image views, a render pass (`loadOp=CLEAR`, `storeOp=STORE`, final layout
   `PRESENT_SRC_KHR`), per-image framebuffers.
6. **Commands + sync** — a command pool, and per frame-in-flight: a command buffer,
   image-available + render-finished semaphores, and an in-flight fence.

No graphics pipeline and **no shaders** — the clear is the render pass's clear
load-op.

## Per-frame flow & resize

- **`begin_frame`**: wait the in-flight fence; `vkAcquireNextImageKHR`. If it returns
  `VK_ERROR_OUT_OF_DATE_KHR` → recreate the swapchain and retry/skip the frame.
- **`clear(color)`**: begin the command buffer + render pass on the acquired
  framebuffer with `color` as the clear value; end the render pass. (36b records
  draws here before ending.)
- **`end_frame`**: end + submit the command buffer (wait image-available, signal
  render-finished, fence on completion); `vkQueuePresentKHR` (wait render-finished).
  If present returns `OUT_OF_DATE`/`SUBOPTIMAL`, or the resize flag is set → recreate.
- **Resize**: a GLFW framebuffer-size callback sets a `resized` flag on the device
  (the backend holds the `GLFWwindow*`, so it re-queries the size itself).
  **Recreation**: `vkDeviceWaitIdle`; destroy the old swapchain, image views, and
  framebuffers; rebuild from the current framebuffer size. **Minimize (extent 0×0)**:
  the recreation path waits (`glfwWaitEvents`) until the framebuffer is non-zero
  before rebuilding, so no frames are rendered to a zero-area surface.

## Error handling

- A `vk_check(result, "context")` helper throws `std::runtime_error` (with the
  `VkResult` name) on any non-`VK_SUCCESS` during setup. `main` catches it, reports
  through the status reporter (`error`), tears down whatever exists, and returns a
  non-zero exit code — no window/instance left dangling.
- `VK_ERROR_OUT_OF_DATE_KHR` / `VK_SUBOPTIMAL_KHR` are **expected control flow**
  (recreate), not errors.
- **Ordered teardown**: `vkDeviceWaitIdle` → swapchain group (framebuffers, views,
  swapchain) → sync objects → command pool → logical device → surface → debug
  messenger → instance → `glfwDestroyWindow` → `glfwTerminate`. RAII wrappers (or a
  strict destructor order) enforce this.

## Testing

The GPU/window path cannot run in CI (no display/device), so **the logic that can be
tested headlessly is extracted into pure free functions** and unit-tested; the visible
result is a manual smoke run.

- **`tests/unit/render/` — `[render]`** (pure helpers, no Vulkan device):
  - `choose_surface_format` — prefers `B8G8R8A8_SRGB`/`SRGB_NONLINEAR`; falls back to
    the first candidate; handles a single-`UNDEFINED` candidate list.
  - `choose_present_mode` — returns `MAILBOX` when present, else `FIFO`.
  - `clamp_extent` — clamps a desired extent to `[minImageExtent, maxImageExtent]`;
    honors the `currentExtent != 0xFFFFFFFF` "fixed" case.
  - `pick_queue_families` — finds graphics + present indices from a family list;
    reports "not suitable" when either is missing.
- **`tests/unit/app/` — `[app]`**: the `FramePump` `on_frame` hook fires exactly once
  per iteration, and is safely skipped when empty (guards the additive change).
- **Manual verification** (documented in the issue/PR, not automated): `mosaic run
  --window` opens a window that clears to the background color; resizing recreates the
  swapchain (no validation errors); closing the window exits cleanly (0).

## CMake

- Root `CMakeLists.txt`: `find_package(Vulkan REQUIRED)`; FetchContent `glfw`
  (`GIT_TAG 3.4`), with `GLFW_BUILD_DOCS/TESTS/EXAMPLES` off; `add_subdirectory(modules/render_vulkan)`
  after `engine`.
- `modules/render_vulkan/CMakeLists.txt`: `add_library(mosaic_render_vulkan STATIC
  vulkan_render_device.cpp vulkan_swapchain_support.cpp)`; `target_link_libraries
  PUBLIC mosaic_engine PRIVATE Vulkan::Vulkan glfw`; `target_include_directories
  PUBLIC ${CMAKE_SOURCE_DIR}`; `cxx_std_20`.
- `app/CMakeLists.txt`: link `mosaic_render_vulkan` into `mosaic_app`.
- `tests/CMakeLists.txt`: link `mosaic_render_vulkan`; add `unit/render/*` sources
  (and the `[app]` on_frame case to the existing app test file).

## Acceptance criteria (from #42)

- [ ] A window opens and the runtime loop clears it to a color every frame
      (`mosaic run --window`).
- [ ] Per-frame sync is correct — no validation-layer errors in debug; resize
      recreates the swapchain; minimize pauses cleanly.
- [ ] Clean shutdown — device/swapchain/window destroyed in order, no leaks; closing
      the window exits 0.
- [ ] Headlessly-testable logic (format/present-mode/extent/queue-family selection,
      and the pump `on_frame` hook) has unit tests; the visible result is verified by
      a documented smoke run.

## Tier / dependency contract

- `engine/render/` (neutral interface) is engine-tier and pulls in no Vulkan/GLFW.
- `modules/render_vulkan/` is a new **module tier** above `engine/`; it depends on
  `mosaic_engine` + `Vulkan::Vulkan` + `glfw`, and privately on `foundation/logging`.
- `foundation/` and `engine/` gain **no** Vulkan/GLFW dependency. The app is allowed
  to name the concrete backend at construction (via the factory) but drives it only
  through the neutral `RenderDevice` interface.
- Depends on the app/CLI shell #35 (window/main + `FramePump`), the runtime loop #22,
  and `foundation/logging` #33 (validation output).
