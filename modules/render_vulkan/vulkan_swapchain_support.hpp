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
