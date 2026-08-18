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
