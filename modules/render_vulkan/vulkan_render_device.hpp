#pragma once

#include <cstddef>
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

// Plain-integer counts of the PER-CALL staging chunks the backend keeps for the three
// world-space draw paths, summed over every frame-in-flight slot. `frames_in_flight` is
// reported so a caller can compute the expected total without knowing the backend's
// constant.
//
// This exists so the per-call counter resets in begin_frame() can be asserted on
// BEHAVIOUR. Dropping a reset grows these vectors every frame and does not change a
// single pixel, so no pixel comparison can see it -- see
// tests/gpu/offscreen_staging_chunk_reset_test.cpp.
struct StagingChunkCounts
{
    std::size_t particle = 0;
    std::size_t ribbon = 0;
    std::size_t world_text = 0;
    std::size_t frames_in_flight = 0;
};

// Implemented by the Vulkan render device, same shape as OffscreenReadback above: an
// abstract class holding no Vulkan or GLFW type, handed out as a non-owning pointer.
class StagingChunkInspector
{
public:
    virtual ~StagingChunkInspector() = default;
    virtual StagingChunkCounts staging_chunk_counts() const = 0;
};

struct OffscreenDevice
{
    std::unique_ptr<mosaic::engine::render::RenderDevice> device;
    OffscreenReadback* readback = nullptr; // non-owning; points into *device
    UiDrawStatsSource* ui_stats = nullptr; // non-owning; points into *device
    StagingChunkInspector* chunks = nullptr; // non-owning; points into *device
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
// THE PUBLISHED VALUE IS GUARDED, since 2026-11-06, by
// tests/gpu/validation_observed_flag_test.cpp. Keep it that way: before that test existed,
// publishing `true` unconditionally -- claiming observation on messenger creation alone,
// which happens either way -- left all 12 GPU tests PASSING with the layer present
// (measured 2026-10-30), and with the backend's "layer not available" WARN out of the
// capture sink it left ten of them green having observed nothing. With the guard in place
// that same mutant turns exactly ONE of 17 red and the other 16 stay green, which is the
// shape to expect if you break this line: the ten tests that CONSUME the flag cannot
// detect a wrong value, because they only ever observe the enabled case, where
// validation_layer_enabled_ is already true and the wrong line publishes the right answer
// by accident.
//
// The guard works by comparing this value against two sources OUTSIDE this file -- the
// loader's own vkEnumerateInstanceLayerProperties, and VK_LOADER_LAYERS_DISABLE, which the
// loader reads at vkCreateInstance time so a test can flip "was the layer enabled" between
// two devices in one process without touching code or uninstalling anything.
//
// An earlier version of this comment predicted the fix would be "the per-device accessor
// shape that VulkanHeadlessDevice::validation_enabled() already has -- the only check that
// caught either fault". That was the right observation with the wrong cause, and building
// it would have produced a guard that still could not fail. What saved those two cases was
// not per-device-ness; it was that they read a DIFFERENT VARIABLE set by a SECOND copy of
// the layer-enablement logic, which a single-line edit over here does not reach. That is
// redundancy, and it protects only until someone edits both copies. A per-device accessor
// publishing a wrong value would be exactly as unguarded as this global one.
bool validation_messages_are_observed() noexcept;

// Create a Vulkan-backed RenderDevice that presents to `window`'s surface.
// `window` must outlive the returned device. Throws std::runtime_error on any
// Vulkan/GLFW setup failure.
std::unique_ptr<mosaic::engine::render::RenderDevice>
create_vulkan_render_device(GLFWwindow* window);
} // namespace mosaic::render::vk
