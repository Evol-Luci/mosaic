#pragma once

#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

#include "engine/render/render_device.hpp"

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

struct OffscreenDevice
{
    std::unique_ptr<mosaic::engine::render::RenderDevice> device;
    OffscreenReadback* readback = nullptr; // non-owning; points into *device
};

OffscreenDevice create_offscreen_vulkan_render_device(std::uint32_t width,
                                                      std::uint32_t height);

// Create a Vulkan-backed RenderDevice that presents to `window`'s surface.
// `window` must outlive the returned device. Throws std::runtime_error on any
// Vulkan/GLFW setup failure.
std::unique_ptr<mosaic::engine::render::RenderDevice>
create_vulkan_render_device(GLFWwindow* window);
} // namespace mosaic::render::vk
