# Rendering 36a — Clear-Screen Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A window + Vulkan backend behind a backend-neutral `RenderDevice` interface that clears the screen each frame from the runtime loop (`mosaic run --window`) — issue [#42](https://github.com/Evol-Luci/mosaic/issues/42).

**Architecture:** A header-only neutral interface in `engine/render/`; a `mosaic_render_vulkan` static lib under a new `modules/` tier implementing it with GLFW + Vulkan (instance/device/swapchain/render-pass-clear/per-frame-sync + resize recreation, debug validation routed through logging); the app gains a `--window` mode that drives the device through an additive `FramePump` per-frame hook.

**Tech Stack:** C++20, Vulkan (`find_package(Vulkan)`), GLFW (FetchContent), Catch2 v3.

## Global Constraints

- **Render boundary:** no `engine/` or `app/` code includes Vulkan or GLFW headers, except the `modules/render_vulkan/` backend and the single factory call site. Engine/app drive rendering only through `mosaic::engine::render::RenderDevice`.
- **Neutral interface:** `engine/render/render_device.hpp`, namespace `mosaic::engine::render`; `Color{float r,g,b,a}`; `RenderDevice` with pure virtuals `begin_frame()`, `clear(const Color&)`, `end_frame()`.
- **Backend:** `modules/render_vulkan/`, `mosaic_render_vulkan` STATIC lib; links `PUBLIC mosaic_engine PRIVATE Vulkan::Vulkan glfw`; factory `std::unique_ptr<mosaic::engine::render::RenderDevice> mosaic::render::vk::create_vulkan_render_device(GLFWwindow*)`.
- **Windowing:** GLFW only, in the backend/app tier. `--window` is opt-in; without it the current headless behavior is unchanged (keeps the `--ticks` smoke/CI path).
- **Validation:** debug builds enable `VK_LAYER_KHRONOS_validation` + a debug messenger routing to `foundation/logging` (category `"vulkan"`); release omits both.
- **Resize:** handled inside the backend — GLFW framebuffer-size callback sets a flag; `VK_ERROR_OUT_OF_DATE_KHR`/`VK_SUBOPTIMAL_KHR` and the flag trigger swapchain recreation (`vkDeviceWaitIdle`, destroy old, rebuild clamped to caps); minimize (0×0) pauses via `glfwWaitEvents`.
- **No shaders / no pipeline** in 36a — the clear is the render pass `loadOp=CLEAR`.
- **C++ standard:** `cxx_std_20`, no extensions. **Test tags:** `[render]` (pure helpers), `[app]` (pump hook).

**Environment (verified on this machine):** `libvulkan.so` + `vulkan.h` present; GLFW build deps (Wayland + X11) present; a live display (`WAYLAND_DISPLAY`/`DISPLAY`) for the smoke run. A working GPU ICD is required only for the manual smoke run, not for the build or the unit tests.

**Build & test commands:**
- Configure (first time / after CMake edits — fetches GLFW): `cmake --preset debug`
- Build: `cmake --build --preset debug`
- Run render/app unit tests: `./build/debug/tests/mosaic_unit_tests "[render],[app]"`
- Full suite: `ctest --preset debug`
- Manual smoke (Task 2/3): `./build/debug/app/mosaic run --window`

---

### Task 1: Neutral interface, module scaffolding, and pure swapchain helpers

Establishes the interface, the `modules/render_vulkan` build, and the
headlessly-testable selection logic — the fully unit-tested foundation the device
builds on. No live Vulkan device is used here.

**Files:**
- Create: `engine/render/render_device.hpp`
- Modify: `CMakeLists.txt` (root — `find_package(Vulkan)`, FetchContent GLFW, `add_subdirectory(modules/render_vulkan)`)
- Create: `modules/render_vulkan/CMakeLists.txt`
- Create: `modules/render_vulkan/vulkan_swapchain_support.hpp`
- Create: `modules/render_vulkan/vulkan_swapchain_support.cpp`
- Create: `tests/unit/render/swapchain_support_test.cpp`
- Modify: `tests/CMakeLists.txt` (link `mosaic_render_vulkan` + `Vulkan::Vulkan`; add the test source)

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `struct mosaic::engine::render::Color { float r, g, b, a; };`
  - `class mosaic::engine::render::RenderDevice { virtual ~; virtual void begin_frame()=0; virtual void clear(const Color&)=0; virtual void end_frame()=0; };`
  - In `namespace mosaic::render::vk`:
    - `VkSurfaceFormatKHR choose_surface_format(const std::vector<VkSurfaceFormatKHR>&);`
    - `VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR>&);`
    - `VkExtent2D clamp_extent(VkExtent2D desired, const VkSurfaceCapabilitiesKHR&);`
    - `struct QueueFamilies { std::optional<uint32_t> graphics; std::optional<uint32_t> present; bool complete() const; };`
    - `QueueFamilies pick_queue_families(const std::vector<VkQueueFamilyProperties>&, const std::vector<VkBool32>& present_support);`

- [ ] **Step 1: Write the neutral interface header**

Create `engine/render/render_device.hpp`:

```cpp
#pragma once

namespace mosaic::engine::render
{

// A color in linear-ish 0..1 components; the backend maps it to the swapchain.
struct Color
{
    float r, g, b, a;
};

// Backend-neutral rendering device. A backend (Vulkan today) implements it;
// engine and app code never see the backend's types. 36a clears the screen;
// 36b will add draw() between clear() and end_frame().
class RenderDevice
{
public:
    virtual ~RenderDevice() = default;

    virtual void begin_frame() = 0;        // acquire the next swapchain image
    virtual void clear(const Color&) = 0;  // clear the acquired image to a color
    virtual void end_frame() = 0;          // submit + present
};

} // namespace mosaic::engine::render
```

- [ ] **Step 2: Write the failing helper tests**

Create `tests/unit/render/swapchain_support_test.cpp`:

```cpp
#include <optional>
#include <vector>

#include <vulkan/vulkan.h>

#include <catch2/catch_test_macros.hpp>

#include "modules/render_vulkan/vulkan_swapchain_support.hpp"

using namespace mosaic::render::vk;

TEST_CASE("choose_surface_format prefers B8G8R8A8_SRGB + SRGB_NONLINEAR", "[render]")
{
    std::vector<VkSurfaceFormatKHR> candidates{
        {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        {VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    };
    VkSurfaceFormatKHR chosen = choose_surface_format(candidates);
    REQUIRE(chosen.format == VK_FORMAT_B8G8R8A8_SRGB);
    REQUIRE(chosen.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
}

TEST_CASE("choose_surface_format falls back to the first candidate", "[render]")
{
    std::vector<VkSurfaceFormatKHR> candidates{
        {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    };
    REQUIRE(choose_surface_format(candidates).format == VK_FORMAT_R8G8B8A8_UNORM);
}

TEST_CASE("choose_present_mode returns MAILBOX when available, else FIFO", "[render]")
{
    REQUIRE(choose_present_mode({VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_MAILBOX_KHR})
            == VK_PRESENT_MODE_MAILBOX_KHR);
    REQUIRE(choose_present_mode({VK_PRESENT_MODE_FIFO_KHR}) == VK_PRESENT_MODE_FIFO_KHR);
    REQUIRE(choose_present_mode({}) == VK_PRESENT_MODE_FIFO_KHR); // FIFO always supported
}

TEST_CASE("clamp_extent uses currentExtent when the surface fixes it", "[render]")
{
    VkSurfaceCapabilitiesKHR caps{};
    caps.currentExtent = {800, 600};
    caps.minImageExtent = {1, 1};
    caps.maxImageExtent = {4096, 4096};
    VkExtent2D e = clamp_extent({1920, 1080}, caps);
    REQUIRE(e.width == 800);
    REQUIRE(e.height == 600);
}

TEST_CASE("clamp_extent clamps the desired extent to caps when free", "[render]")
{
    VkSurfaceCapabilitiesKHR caps{};
    caps.currentExtent = {0xFFFFFFFF, 0xFFFFFFFF}; // "surface lets us choose"
    caps.minImageExtent = {640, 480};
    caps.maxImageExtent = {1280, 720};
    REQUIRE(clamp_extent({320, 200}, caps).width == 640);
    REQUIRE(clamp_extent({320, 200}, caps).height == 480);
    REQUIRE(clamp_extent({4000, 4000}, caps).width == 1280);
    REQUIRE(clamp_extent({4000, 4000}, caps).height == 720);
}

TEST_CASE("pick_queue_families finds graphics and present indices", "[render]")
{
    std::vector<VkQueueFamilyProperties> families(2);
    families[0].queueFlags = 0;                      // not graphics
    families[1].queueFlags = VK_QUEUE_GRAPHICS_BIT;  // graphics
    std::vector<VkBool32> present{VK_TRUE, VK_FALSE}; // present on family 0

    QueueFamilies q = pick_queue_families(families, present);
    REQUIRE(q.complete());
    REQUIRE(q.graphics.value() == 1u);
    REQUIRE(q.present.value() == 0u);
}

TEST_CASE("pick_queue_families reports incomplete when graphics is missing", "[render]")
{
    std::vector<VkQueueFamilyProperties> families(1);
    families[0].queueFlags = 0;
    std::vector<VkBool32> present{VK_TRUE};
    REQUIRE_FALSE(pick_queue_families(families, present).complete());
}
```

- [ ] **Step 3: Wire CMake (root + module + tests)**

In the root `CMakeLists.txt`, after the existing `find_package(Threads REQUIRED)` add:

```cmake
find_package(Vulkan REQUIRED)

FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG        3.4
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)
```

And add the module subdirectory after `add_subdirectory(engine)`:

```cmake
add_subdirectory(modules/render_vulkan)
```

Create `modules/render_vulkan/CMakeLists.txt`:

```cmake
add_library(mosaic_render_vulkan STATIC
  vulkan_swapchain_support.cpp
  vulkan_render_device.cpp
)
target_include_directories(mosaic_render_vulkan PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(mosaic_render_vulkan
  PUBLIC  mosaic_engine
  PRIVATE Vulkan::Vulkan glfw mosaic_foundation
)
target_compile_features(mosaic_render_vulkan PUBLIC cxx_std_20)
```

> Note: `vulkan_render_device.cpp` is created in Task 2. To let Task 1 build and
> test on its own, create a **temporary empty `vulkan_render_device.cpp`** now
> containing only `// implemented in Task 2` — Task 2 fills it in. (A STATIC lib
> with an empty TU links fine.)

In `tests/CMakeLists.txt`: add `mosaic_render_vulkan` **and** `Vulkan::Vulkan` to the
`target_link_libraries(mosaic_unit_tests PRIVATE ...)` list (the test includes
`<vulkan/vulkan.h>`, and Vulkan is PRIVATE on the module, so the test needs its own
link), and add the source to the `add_executable(mosaic_unit_tests ...)` list:

```cmake
  unit/render/swapchain_support_test.cpp
```

- [ ] **Step 4: Run the tests to verify they fail**

Run: `cmake --preset debug && cmake --build --preset debug`
Expected: FAIL — `modules/render_vulkan/vulkan_swapchain_support.hpp` not found.

- [ ] **Step 5: Write the helper header**

Create `modules/render_vulkan/vulkan_swapchain_support.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <vulkan/vulkan.h>

namespace mosaic::render::vk
{

// Graphics + present queue family indices selected for a physical device/surface.
struct QueueFamilies
{
    std::optional<std::uint32_t> graphics;
    std::optional<std::uint32_t> present;
    bool complete() const { return graphics.has_value() && present.has_value(); }
};

// Prefer B8G8R8A8_SRGB / SRGB_NONLINEAR; fall back to the first candidate.
VkSurfaceFormatKHR choose_surface_format(const std::vector<VkSurfaceFormatKHR>& candidates);

// Prefer MAILBOX (low-latency triple buffering); FIFO is always available.
VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR>& candidates);

// If the surface fixes the extent (currentExtent != UINT32_MAX) use it; otherwise
// clamp `desired` to [minImageExtent, maxImageExtent].
VkExtent2D clamp_extent(VkExtent2D desired, const VkSurfaceCapabilitiesKHR& caps);

// First graphics-capable family for `graphics`; first present-capable for `present`
// (present_support[i] == VK_TRUE iff family i can present to the surface).
QueueFamilies pick_queue_families(const std::vector<VkQueueFamilyProperties>& families,
                                  const std::vector<VkBool32>& present_support);

} // namespace mosaic::render::vk
```

- [ ] **Step 6: Write the helper implementation**

Create `modules/render_vulkan/vulkan_swapchain_support.cpp`:

```cpp
#include "modules/render_vulkan/vulkan_swapchain_support.hpp"

#include <algorithm>

namespace mosaic::render::vk
{

VkSurfaceFormatKHR choose_surface_format(const std::vector<VkSurfaceFormatKHR>& candidates)
{
    for (const auto& f : candidates)
    {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return f;
        }
    }
    return candidates.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_SRGB,
                                                   VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}
                              : candidates.front();
}

VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR>& candidates)
{
    for (auto m : candidates)
    {
        if (m == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            return m;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR; // guaranteed by the spec
}

VkExtent2D clamp_extent(VkExtent2D desired, const VkSurfaceCapabilitiesKHR& caps)
{
    if (caps.currentExtent.width != 0xFFFFFFFFu)
    {
        return caps.currentExtent;
    }
    desired.width = std::clamp(desired.width, caps.minImageExtent.width,
                               caps.maxImageExtent.width);
    desired.height = std::clamp(desired.height, caps.minImageExtent.height,
                                caps.maxImageExtent.height);
    return desired;
}

QueueFamilies pick_queue_families(const std::vector<VkQueueFamilyProperties>& families,
                                  const std::vector<VkBool32>& present_support)
{
    QueueFamilies q;
    for (std::uint32_t i = 0; i < families.size(); ++i)
    {
        if (!q.graphics && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
        {
            q.graphics = i;
        }
        if (!q.present && i < present_support.size() && present_support[i] == VK_TRUE)
        {
            q.present = i;
        }
    }
    return q;
}

} // namespace mosaic::render::vk
```

- [ ] **Step 7: Run the tests to verify they pass**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[render]"`
Expected: PASS — all `[render]` cases green.

- [ ] **Step 8: Run the full suite + commit**

Run: `ctest --preset debug` (expected: green).

```bash
git add engine/render/render_device.hpp CMakeLists.txt modules/render_vulkan/CMakeLists.txt modules/render_vulkan/vulkan_swapchain_support.hpp modules/render_vulkan/vulkan_swapchain_support.cpp modules/render_vulkan/vulkan_render_device.cpp tests/CMakeLists.txt tests/unit/render/swapchain_support_test.cpp
git commit -m "render 36a: neutral RenderDevice, render_vulkan module, swapchain helpers (#42)"
```

---

### Task 2: VulkanRenderDevice + factory (bring-up, per-frame, resize)

The Vulkan implementation of `RenderDevice`. **This is GPU code and cannot be
unit-tested** (no device/display in CI); it is verified by a clean build (validation
layers must be silent) and the manual smoke run. TDD does not apply — the deliverable
is a correct, validation-clean implementation. Use the exact structure below.

**Files:**
- Create: `modules/render_vulkan/vulkan_render_device.hpp` (factory declaration)
- Replace: `modules/render_vulkan/vulkan_render_device.cpp` (the Task 1 stub → full impl)

**Interfaces:**
- Consumes from Task 1: `mosaic::engine::render::{RenderDevice, Color}`, and the `mosaic::render::vk` helpers.
- Produces:
  - `std::unique_ptr<mosaic::engine::render::RenderDevice> mosaic::render::vk::create_vulkan_render_device(GLFWwindow* window);`

- [ ] **Step 1: Write the factory header**

Create `modules/render_vulkan/vulkan_render_device.hpp`:

```cpp
#pragma once

#include <memory>

#include "engine/render/render_device.hpp"

struct GLFWwindow; // fwd-decl; no GLFW include leaks to callers

namespace mosaic::render::vk
{
// Create a Vulkan-backed RenderDevice that presents to `window`'s surface.
// `window` must outlive the returned device. Throws std::runtime_error on any
// Vulkan/GLFW setup failure.
std::unique_ptr<mosaic::engine::render::RenderDevice>
create_vulkan_render_device(GLFWwindow* window);
} // namespace mosaic::render::vk
```

- [ ] **Step 2: Implement `vulkan_render_device.cpp`**

Replace the stub `modules/render_vulkan/vulkan_render_device.cpp` with a
`VulkanRenderDevice` class implementing `RenderDevice` plus the factory. Implement it
exactly to this structure (standard Vulkan bring-up; every object and the frame
algorithm are specified):

Includes/prelude:
- `#define GLFW_INCLUDE_VULKAN` then `<GLFW/glfw3.h>`; `<vulkan/vulkan.h>`;
  `"engine/render/render_device.hpp"`, `"modules/render_vulkan/vulkan_render_device.hpp"`,
  `"modules/render_vulkan/vulkan_swapchain_support.hpp"`, `"foundation/logging/log.hpp"`;
  `<array> <cstdint> <cstring> <stdexcept> <string> <vector>`.
- `namespace log = mosaic::foundation::log;` **is NOT allowed** — fully qualify
  `mosaic::foundation::log::warn/error` (avoid the `::log` builtin-shadow footgun).
- `constexpr int kFramesInFlight = 2;`
- A `vk_check(VkResult r, const char* what)` free function: if `r != VK_SUCCESS` throw
  `std::runtime_error(std::string(what) + " failed: VkResult " + std::to_string(r))`.

`VulkanRenderDevice` members: `GLFWwindow* window_`; `VkInstance instance_`;
`VkDebugUtilsMessengerEXT messenger_` (debug only); `VkSurfaceKHR surface_`;
`VkPhysicalDevice physical_`; `VkDevice device_`; `VkQueue graphics_queue_`,
`present_queue_`; `uint32_t graphics_family_`, `present_family_`;
`VkSwapchainKHR swapchain_`; `VkFormat swapchain_format_`; `VkExtent2D extent_`;
`std::vector<VkImage> images_`; `std::vector<VkImageView> views_`;
`VkRenderPass render_pass_`; `std::vector<VkFramebuffer> framebuffers_`;
`VkCommandPool command_pool_`; `std::array<VkCommandBuffer, kFramesInFlight> cmd_`;
per-frame sync arrays `image_available_[kFramesInFlight]`,
`render_finished_[kFramesInFlight]`, `in_flight_[kFramesInFlight]`;
`uint32_t frame_ = 0`; `uint32_t image_index_ = 0`; `bool framebuffer_resized_ = false`.

Construction sequence (ctor takes `GLFWwindow*`, calls these in order; each wrapped in
`vk_check` where it returns a `VkResult`):
1. `create_instance()` — `VkApplicationInfo` (apiVersion `VK_API_VERSION_1_2`);
   instance extensions = `glfwGetRequiredInstanceExtensions(...)`, **plus**
   `VK_EXT_DEBUG_UTILS_EXTENSION_NAME` in debug. In debug set
   `ppEnabledLayerNames = {"VK_LAYER_KHRONOS_validation"}` (guard: only if the layer
   is present via `vkEnumerateInstanceLayerProperties`; if absent, log a warning and
   proceed without it).
2. `setup_debug_messenger()` (debug only) — `vkCreateDebugUtilsMessengerEXT` (loaded
   via `vkGetInstanceProcAddr`) with a static callback that maps severity to
   `mosaic::foundation::log::error` (ERROR) / `::warn` (WARNING) on category
   `"vulkan"`, returning `VK_FALSE`.
3. `glfwCreateWindowSurface(instance_, window_, nullptr, &surface_)` → `vk_check`.
4. `pick_physical_device()` — enumerate; for each: query queue families +
   per-family `vkGetPhysicalDeviceSurfaceSupportKHR` → `pick_queue_families`; require
   the swapchain device extension (`vkEnumerateDeviceExtensionProperties`) and a
   non-empty surface format + present-mode list; choose the first suitable. Store
   `graphics_family_`/`present_family_`. Throw if none suitable.
5. `create_logical_device()` — one `VkDeviceQueueCreateInfo` per unique family
   (graphics/present may coincide), priority 1.0; enable the swapchain extension;
   `vkCreateDevice`; retrieve `graphics_queue_`/`present_queue_`.
6. `create_swapchain()` (see the recreatable group below).
7. `create_image_views()`, `create_render_pass()`, `create_framebuffers()`.
8. `create_command_pool_and_buffers()` — pool on `graphics_family_`
   (`RESET_COMMAND_BUFFER` flag), allocate `kFramesInFlight` primary buffers.
9. `create_sync_objects()` — `kFramesInFlight` of each: two semaphores, one fence
   created **signaled** (`VK_FENCE_CREATE_SIGNALED_BIT`).

`create_swapchain()` (the recreatable group): query `VkSurfaceCapabilitiesKHR`,
formats, present modes; `choose_surface_format` / `choose_present_mode`;
`extent_ = clamp_extent({fbW, fbH}, caps)` where `fbW/fbH` come from
`glfwGetFramebufferSize`; `imageCount = caps.minImageCount + 1` (clamp to
`maxImageCount` if non-zero); `VkSwapchainCreateInfoKHR` with
`imageUsage = COLOR_ATTACHMENT`, sharing mode `CONCURRENT` across
{graphics,present} families if they differ else `EXCLUSIVE`,
`preTransform = caps.currentTransform`, `compositeAlpha = OPAQUE`,
`clipped = VK_TRUE`, `oldSwapchain = VK_NULL_HANDLE`; store `swapchain_format_` and
retrieve `images_`.

`create_render_pass()`: single color attachment, format `swapchain_format_`,
`loadOp = CLEAR`, `storeOp = STORE`, `initialLayout = UNDEFINED`,
`finalLayout = PRESENT_SRC_KHR`; one subpass with the color attachment ref
(`COLOR_ATTACHMENT_OPTIMAL`); a subpass dependency from `VK_SUBPASS_EXTERNAL`
(`srcStage = COLOR_ATTACHMENT_OUTPUT`, `dstStage = COLOR_ATTACHMENT_OUTPUT`,
`dstAccess = COLOR_ATTACHMENT_WRITE`).

The three `RenderDevice` methods:

- `begin_frame()`:
  ```
  vkWaitForFences(device_, 1, &in_flight_[frame_], VK_TRUE, UINT64_MAX);
  VkResult r = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                                     image_available_[frame_], VK_NULL_HANDLE, &image_index_);
  if (r == VK_ERROR_OUT_OF_DATE_KHR) { recreate_swapchain(); return; } // skip this frame
  else if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) vk_check(r, "vkAcquireNextImageKHR");
  vkResetFences(device_, 1, &in_flight_[frame_]);
  vkResetCommandBuffer(cmd_[frame_], 0);
  // begin cmd buffer; begin render pass on framebuffers_[image_index_] with a stored clear color
  ```
  Store the clear color set by `clear()`; if `begin_frame` skipped due to recreate,
  set a `frame_skipped_` flag so `end_frame` no-ops.
- `clear(const Color& c)`: record `VkClearValue{{c.r,c.g,c.b,c.a}}` into the render
  pass begin (store it on the object; the actual `vkCmdBeginRenderPass` with this
  clear happens in `begin_frame` after acquire, OR do begin-render-pass here — keep
  begin_frame doing acquire + begin-cmd, and `clear` doing
  `vkCmdBeginRenderPass(...clearValue...)` immediately followed by
  `vkCmdEndRenderPass` since 36a draws nothing). Simplest correct 36a ordering:
  **begin_frame** = wait/acquire/reset/begin-cmd; **clear** = begin render pass with
  the clear value + end render pass; **end_frame** = end cmd, submit, present.
- `end_frame()`:
  ```
  if (frame_skipped_) { frame_skipped_ = false; return; }
  vkEndCommandBuffer(cmd_[frame_]);
  submit: waitSemaphores={image_available_[frame_]}, waitStages={COLOR_ATTACHMENT_OUTPUT},
          commandBuffers={cmd_[frame_]}, signalSemaphores={render_finished_[frame_]},
          fence=in_flight_[frame_];
  present: waitSemaphores={render_finished_[frame_]}, swapchains={swapchain_}, &image_index_;
  VkResult pr = vkQueuePresentKHR(present_queue_, &presentInfo);
  if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR || framebuffer_resized_)
  { framebuffer_resized_ = false; recreate_swapchain(); }
  else vk_check(pr, "vkQueuePresentKHR");
  frame_ = (frame_ + 1) % kFramesInFlight;
  ```

`recreate_swapchain()`:
```
int w = 0, h = 0; glfwGetFramebufferSize(window_, &w, &h);
while (w == 0 || h == 0) { glfwWaitEvents(); glfwGetFramebufferSize(window_, &w, &h); } // minimized
vkDeviceWaitIdle(device_);
destroy framebuffers_, views_, swapchain_ (in that order);
create_swapchain(); create_image_views(); create_framebuffers();
```
(The render pass is format-stable, so it is not recreated.)

Resize callback: in the ctor, `glfwSetWindowUserPointer(window_, this)` and
`glfwSetFramebufferSizeCallback(window_, [](GLFWwindow* w, int, int){
static_cast<VulkanRenderDevice*>(glfwGetWindowUserPointer(w))->framebuffer_resized_ = true; })`.

Destructor (ordered): `vkDeviceWaitIdle(device_)`; destroy sync objects; command
pool; framebuffers; image views; swapchain; render pass; logical device; surface;
debug messenger (debug); instance. (The window is owned by the app, not destroyed
here.)

Factory:
```cpp
std::unique_ptr<mosaic::engine::render::RenderDevice>
create_vulkan_render_device(GLFWwindow* window)
{
    return std::make_unique<VulkanRenderDevice>(window);
}
```

- [ ] **Step 3: Build (no unit tests — GPU code)**

Run: `cmake --build --preset debug`
Expected: compiles and links clean, no warnings.

- [ ] **Step 4: Full suite (must stay green — this task adds no tests but must not break the build)**

Run: `ctest --preset debug`
Expected: PASS (the existing suite; render/app unit tests unchanged).

> Manual smoke is deferred to Task 3 (which adds the `--window` path that constructs
> this device). Do not add a temporary main here.

- [ ] **Step 5: Commit**

```bash
git add modules/render_vulkan/vulkan_render_device.hpp modules/render_vulkan/vulkan_render_device.cpp
git commit -m "render 36a: VulkanRenderDevice bring-up, per-frame clear, resize (#42)"
```

---

### Task 3: FramePump on_frame hook + `--window` app mode

Wire the device into the app: an additive per-frame hook on the pump (unit-tested),
a `--window` flag, and the windowed `main` path (manual smoke).

**Files:**
- Modify: `app/run/frame_pump.hpp` (add `on_frame` to `PumpDeps`; call it)
- Modify: `tests/unit/app/frame_pump_test.cpp` (append the on_frame case)
- Modify: `app/cli/options.hpp` + `app/cli/options.cpp` (add `--window`)
- Modify: `app/main.cpp` (windowed path)
- Modify: `app/CMakeLists.txt` (link `mosaic_render_vulkan` + `glfw` into `mosaic_app`)

**Interfaces:**
- Consumes: `mosaic::render::vk::create_vulkan_render_device(GLFWwindow*)` (Task 2), `mosaic::engine::render::{RenderDevice,Color}` (Task 1), existing `PumpDeps`/`FramePump`/`Options`.
- Produces: `Options::window` (bool); `PumpDeps::on_frame` (`std::function<void()>`).

- [ ] **Step 1: Write the failing pump test**

Append to `tests/unit/app/frame_pump_test.cpp`:

```cpp
TEST_CASE("the on_frame hook fires once per iteration, skipped when empty", "[app]")
{
    World world;
    RuntimeLoop loop(1.0F / 60.0F, 1);

    Options options;
    options.ticks = 3;
    options.fixed_hz = 60.0;

    StepClock clock{0.0, 1.0};
    int frames = 0;
    PumpDeps deps{std::ref(clock), [](double) {}, [] { return false; }};
    deps.on_frame = [&frames] { ++frames; };

    CapturingReporter reporter;
    FramePump pump(loop, deps, reporter, options);
    RunSummary summary = pump.run(world);

    REQUIRE(summary.stopped_by == "ticks");
    REQUIRE(frames == static_cast<int>(loop.tick_count())); // one hook call per iteration
    REQUIRE(frames >= 3);
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake --build --preset debug`
Expected: FAIL — `PumpDeps` has no member `on_frame`.

- [ ] **Step 3: Add the hook to the pump**

In `app/run/frame_pump.hpp`, add the field to `PumpDeps` (after `stop_requested`):

```cpp
    std::function<void()>       on_frame; // per-frame hook (render/poll); may be empty
```

And in `FramePump::run`, immediately after the `loop_.advance(world, static_cast<float>(real_dt));` line, insert:

```cpp
            // Per-frame side effects (e.g. poll window events + render). Empty in
            // headless mode.
            if (deps_.on_frame)
            {
                deps_.on_frame();
            }
```

- [ ] **Step 4: Run the pump test**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[app]"`
Expected: PASS — the new case plus all existing `[app]` cases green.

- [ ] **Step 5: Add the `--window` flag**

In `app/cli/options.hpp`, add to `Options` (after `json`):

```cpp
    bool window = false; // --window : open a render window (else headless)
```

In `app/cli/options.cpp`, add to the `run` subcommand options (next to `--json`):

```cpp
    run->add_flag("--window", result.options.window, "Open a render window (Vulkan)");
```

- [ ] **Step 6: Add the windowed path to main.cpp**

In `app/main.cpp`, add includes:

```cpp
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "engine/render/render_device.hpp"
#include "modules/render_vulkan/vulkan_render_device.hpp"
```

After the scene-load block and before constructing `RuntimeLoop`, branch on
`options.window`. Replace the existing pump-construction/run tail with:

```cpp
    engine::runtime::RuntimeLoop loop(1.0F / static_cast<float>(options.fixed_hz),
                                      options.ticks ? 1 : 8);
    std::signal(SIGINT, on_sigint);

    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    PumpDeps deps;
    deps.now = [start] {
        return std::chrono::duration<double>(clock::now() - start).count();
    };
    deps.sleep = [](double seconds) {
        std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
    };

    GLFWwindow*                                            glfw_window = nullptr;
    std::unique_ptr<mosaic::engine::render::RenderDevice>  render_device;

    if (options.window)
    {
        if (glfwInit() != GLFW_TRUE)
        {
            reporter->error("glfwInit failed");
            return 1;
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // Vulkan, no GL context
        glfw_window = glfwCreateWindow(1280, 720, "Mosaic", nullptr, nullptr);
        if (glfw_window == nullptr)
        {
            reporter->error("glfwCreateWindow failed");
            glfwTerminate();
            return 1;
        }
        try
        {
            render_device = mosaic::render::vk::create_vulkan_render_device(glfw_window);
        }
        catch (const std::exception& e)
        {
            reporter->error(std::string("render init failed: ") + e.what());
            glfwDestroyWindow(glfw_window);
            glfwTerminate();
            return 1;
        }

        auto* device = render_device.get();
        deps.on_frame = [glfw_window, device] {
            glfwPollEvents();
            device->begin_frame();
            device->clear(mosaic::engine::render::Color{0.1F, 0.1F, 0.12F, 1.0F});
            device->end_frame();
        };
        deps.stop_requested = [glfw_window] {
            return g_stop.load() || glfwWindowShouldClose(glfw_window) == GLFW_TRUE;
        };
    }
    else
    {
        deps.stop_requested = [] { return g_stop.load(); };
    }

    FramePump pump(loop, std::move(deps), *reporter, options);
    RunSummary summary = pump.run(world);
    reporter->summary(summary);

    if (options.window)
    {
        render_device.reset();          // device destroyed before the window
        glfwDestroyWindow(glfw_window);
        glfwTerminate();
    }
    return 0;
}
```

(Delete the old `deps`/`FramePump`/`return 0` tail this replaces — do not leave a
duplicate.)

- [ ] **Step 7: Link the backend into the app**

In `app/CMakeLists.txt`, add `mosaic_render_vulkan` and `glfw` to the
`target_link_libraries(mosaic_app PUBLIC ...)` list. (The app calls the factory and
GLFW directly in the windowed path.)

- [ ] **Step 8: Build + full suite + manual smoke**

Run: `cmake --build --preset debug && ctest --preset debug`
Expected: PASS — full suite green (the new `[app]` on_frame case included).

Manual smoke (requires a display + GPU driver):
`./build/debug/app/mosaic run --window`
Expected: a 1280×720 window opens and shows a solid dark-blue clear color; resizing
the window keeps it rendering (no validation errors in the terminal); closing the
window exits with code 0. Also confirm `./build/debug/app/mosaic run --ticks 5`
(headless, no `--window`) still runs and exits 0 as before.

- [ ] **Step 9: Commit**

```bash
git add app/run/frame_pump.hpp tests/unit/app/frame_pump_test.cpp app/cli/options.hpp app/cli/options.cpp app/main.cpp app/CMakeLists.txt
git commit -m "render 36a: FramePump on_frame hook + mosaic run --window (#42)"
```

---

## Self-Review

**Spec coverage** (against `docs/superpowers/specs/2026-08-17-render-36a-clear-screen-design.md`):

| Spec item | Task |
| --- | --- |
| Neutral `RenderDevice` (begin/clear/end) + `Color` | Task 1 |
| `modules/render_vulkan` module + CMake (Vulkan, GLFW) | Task 1 (scaffold), Task 2 (impl) |
| Pure helpers (format/present-mode/extent/queue-family) unit-tested | Task 1 |
| VulkanRenderDevice bring-up (instance/device/swapchain/render-pass/sync) | Task 2 |
| Debug validation → logging | Task 2 (debug messenger) |
| Per-frame clear via render-pass loadOp | Task 2 (`clear`) |
| Resize recreation + minimize pause | Task 2 (`recreate_swapchain` + callback) |
| Ordered teardown | Task 2 (destructor) |
| `FramePump` on_frame hook (additive, unit-tested) | Task 3 |
| `--window` opt-in; headless unchanged | Task 3 (options + main branch) |
| Manual smoke verification | Task 3 (Step 8) |
| Render boundary (no Vulkan/GLFW in engine/app except backend + factory site) | Tasks 1–3 (engine iface Vulkan-free; app touches GLFW/factory only in the `--window` path) |

No spec item is unmapped.

**Placeholder scan:** Task 1 and Task 3 carry complete code. Task 2 is a detailed
implementation specification (every Vulkan object, creation parameter, and the exact
frame/resize/teardown algorithms) rather than 700 lines of transcribed boilerplate —
a deliberate choice because the code is un-unit-testable GPU code the implementer
writes against the Vulkan spec, and exact inline boilerplate in markdown cannot be
compile-checked. The non-obvious parts (acquire/present out-of-date handling, resize,
messenger→logging, teardown order) are specified concretely.

**Type consistency:** `RenderDevice::{begin_frame,clear,end_frame}`, `Color{r,g,b,a}`,
`create_vulkan_render_device(GLFWwindow*) -> unique_ptr<RenderDevice>`, the
`mosaic::render::vk` helpers (`choose_surface_format`/`choose_present_mode`/
`clamp_extent`/`pick_queue_families`/`QueueFamilies`), `PumpDeps::on_frame`, and
`Options::window` are named identically across tasks and tests. Namespaces:
interface `mosaic::engine::render`, backend `mosaic::render::vk`, app `mosaic::app`.
