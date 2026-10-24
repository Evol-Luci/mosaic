#pragma once

#include <cstdint>
#include <vulkan/vulkan.h>

namespace mosaic::render::vk
{

// Surfaceless Vulkan device for headless rendering and GPU tests.
// Owns, and destroys in reverse order: instance, debug messenger (when
// VK_LAYER_KHRONOS_validation is available), and logical device.
// Throws std::runtime_error on any Vulkan setup failure.
class VulkanHeadlessDevice
{
public:
    VulkanHeadlessDevice();
    ~VulkanHeadlessDevice();

    VulkanHeadlessDevice(const VulkanHeadlessDevice&) = delete;
    VulkanHeadlessDevice& operator=(const VulkanHeadlessDevice&) = delete;
    VulkanHeadlessDevice(VulkanHeadlessDevice&&) = delete;
    VulkanHeadlessDevice& operator=(VulkanHeadlessDevice&&) = delete;

    VkInstance instance() const noexcept { return instance_; }
    VkPhysicalDevice physical_device() const noexcept { return physical_; }
    VkDevice device() const noexcept { return device_; }
    VkQueue graphics_queue() const noexcept { return graphics_queue_; }
    std::uint32_t graphics_family() const noexcept { return graphics_family_; }

    bool validation_enabled() const noexcept { return validation_enabled_; }
    std::uint32_t validation_message_count() const noexcept { return validation_messages_; }
    static std::uint32_t total_validation_message_count() noexcept;

private:
    static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT type,
        const VkDebugUtilsMessengerCallbackDataEXT* data,
        void* user);

    void create_instance();
    void setup_debug_messenger();
    void pick_physical_device();
    void create_logical_device();
    void cleanup() noexcept;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    std::uint32_t graphics_family_ = 0;
    bool validation_enabled_ = false;
    std::uint32_t validation_messages_ = 0;
};

} // namespace mosaic::render::vk
