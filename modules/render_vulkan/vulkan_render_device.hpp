#pragma once

#include <memory>
#include <variant>

#include "engine/render/render_device.hpp"

struct GLFWwindow; // fwd-decl; no GLFW include leaks to callers

namespace mosaic::render::vk
{

struct WindowTarget
{
    GLFWwindow* window = nullptr;
};

using RenderTarget = std::variant<WindowTarget>;

// Create a Vulkan-backed RenderDevice that presents to `window`'s surface.
// `window` must outlive the returned device. Throws std::runtime_error on any
// Vulkan/GLFW setup failure.
std::unique_ptr<mosaic::engine::render::RenderDevice>
create_vulkan_render_device(GLFWwindow* window);
} // namespace mosaic::render::vk
