#pragma once

#include "modules/render_vulkan/vulkan_headless_device.hpp"

#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

namespace mosaic::render::vk
{

struct Rgba8
{
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 0;

    constexpr bool operator==(const Rgba8&) const noexcept = default;
};

// Offscreen R8G8B8A8_UNORM target and host-visible readback helper for a
// surfaceless VulkanHeadlessDevice.
//
// Owns and destroys in reverse order: command pool (created first, before any
// resource that records commands), command buffer, fence, offscreen VkImage +
// device memory, and host-visible readback VkBuffer + device memory.
class VulkanHeadlessReadback
{
public:
    VulkanHeadlessReadback(VulkanHeadlessDevice& dev,
                           std::uint32_t width,
                           std::uint32_t height);
    ~VulkanHeadlessReadback();

    VulkanHeadlessReadback(const VulkanHeadlessReadback&) = delete;
    VulkanHeadlessReadback& operator=(const VulkanHeadlessReadback&) = delete;
    VulkanHeadlessReadback(VulkanHeadlessReadback&&) = delete;
    VulkanHeadlessReadback& operator=(VulkanHeadlessReadback&&) = delete;

    std::uint32_t width() const noexcept { return width_; }
    std::uint32_t height() const noexcept { return height_; }
    VkImage image() const noexcept { return image_; }
    VkBuffer readback_buffer() const noexcept { return readback_buffer_; }

    // Transitions the offscreen image UNDEFINED -> TRANSFER_DST_OPTIMAL, clears
    // it to `clear_color`, transitions TRANSFER_DST_OPTIMAL ->
    // TRANSFER_SRC_OPTIMAL, copies it to the host-visible readback buffer,
    // waits on a fence, maps (and invalidates if non-coherent), and returns
    // all width * height RGBA pixels.
    std::vector<Rgba8> clear_and_readback(Rgba8 clear_color);

private:
    std::uint32_t find_memory_type(std::uint32_t type_filter,
                                   VkMemoryPropertyFlags required,
                                   VkMemoryPropertyFlags* out_flags) const;

    void create_command_pool_and_buffer();
    void create_fence();
    void create_offscreen_image();
    void create_readback_buffer();
    void cleanup() noexcept;

    VulkanHeadlessDevice& dev_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    VkDeviceSize buffer_size_ = 0;

    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;

    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory image_memory_ = VK_NULL_HANDLE;

    VkBuffer readback_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory readback_memory_ = VK_NULL_HANDLE;
    bool host_coherent_ = false;
};

} // namespace mosaic::render::vk
