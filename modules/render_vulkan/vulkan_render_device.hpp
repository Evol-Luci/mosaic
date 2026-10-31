#pragma once

#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

#include "engine/render/render_device.hpp"
#include "modules/render_vulkan/vulkan_ui_draw_stats.hpp"

struct GLFWwindow; // fwd-decl; no GLFW include leaks to callers

namespace mosaic::render::vk
{

struct WindowTarget
{
    GLFWwindow* window = nullptr;
};

struct OffscreenTarget
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

using RenderTarget = std::variant<WindowTarget, OffscreenTarget>;

// Implemented by the Vulkan render device when it was built on an
// OffscreenTarget. read_pixels() must be called after end_frame().
class OffscreenReadback
{
public:
    virtual ~OffscreenReadback() = default;
    // Tightly packed, row-major, 4 bytes per pixel in R,G,B,A order,
    // width*height*4 bytes. The offscreen image is B8G8R8A8, so this
    // SWIZZLES to RGBA; callers never see BGRA.
    virtual std::vector<std::uint8_t> read_pixels() = 0;
};

// Implemented by the Vulkan render device on EITHER target. The counters are the only
// way a test can assert that UI draws actually reached a command buffer; see
// vulkan_ui_draw_stats.hpp for why that assertion had to be added.
class UiDrawStatsSource
{
public:
    virtual ~UiDrawStatsSource() = default;
    // Cumulative over the device's whole life, not per frame: nothing resets them. A
    // caller wanting a per-frame delta reads the counter before and after.
    virtual const UiDrawStats& ui_draw_stats() const = 0;
};

struct OffscreenDevice
{
    std::unique_ptr<mosaic::engine::render::RenderDevice> device;
    OffscreenReadback* readback = nullptr; // non-owning; points into *device
    UiDrawStatsSource* ui_stats = nullptr; // non-owning; points into *device
};

OffscreenDevice create_offscreen_vulkan_render_device(std::uint32_t width,
                                                      std::uint32_t height);

// Whether the most recently created Vulkan render device in this process had
// VK_LAYER_KHRONOS_validation enabled AND a debug-utils messenger routing its
// messages into the engine log under the "vulkan" category.
//
// EXISTS FOR THE GPU TESTS, and specifically so that "no validation messages" can
// stop being a claim that is true for free. The layer is enabled only `#ifndef
// NDEBUG` and only when the loader enumerates it, so a release build, a machine
// without the layer package, or a run under VK_LOADER_LAYERS_DISABLE all produce a
// message count of zero having checked nothing. Assert this BEFORE asserting the
// count, or the count is decoration. Returns false in any build where the layer was
// not enabled -- it never guesses from the extension or the messenger alone, because
// the messenger is created either way.
//
// THAT CONJUNCTION IS A CONVENTION, NOT AN ENFORCED PROPERTY: the published value is
// held by one line in setup_debug_messenger and by code review, and NO TEST GUARDS IT.
// Measured 2026-10-30: publishing `true` unconditionally -- claiming observation on
// messenger creation alone -- leaves all 12 GPU tests PASSING with the layer present,
// and once the backend's "layer not available" WARN no longer lands in the capture
// sink it leaves ten of them green having observed nothing. Do not read the paragraph
// above as a guarantee. The fix is the per-device accessor shape that
// VulkanHeadlessDevice::validation_enabled() already has -- the only check that caught
// either fault -- and it is deferred-work-backlog.md, Rendering, "the value published
// into g_validation_observed is itself guarded by NOTHING".
bool validation_messages_are_observed() noexcept;

// Create a Vulkan-backed RenderDevice that presents to `window`'s surface.
// `window` must outlive the returned device. Throws std::runtime_error on any
// Vulkan/GLFW setup failure.
std::unique_ptr<mosaic::engine::render::RenderDevice>
create_vulkan_render_device(GLFWwindow* window);
} // namespace mosaic::render::vk
