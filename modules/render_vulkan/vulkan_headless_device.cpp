#include "modules/render_vulkan/vulkan_headless_device.hpp"

#include "foundation/logging/log.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace mosaic::render::vk
{
namespace
{

std::atomic<std::uint32_t> g_total_validation_messages{0};

void vk_check(VkResult r, const char* what)
{
    if (r != VK_SUCCESS)
    {
        throw std::runtime_error(std::string(what) + " failed: VkResult " +
                                 std::to_string(static_cast<int>(r)));
    }
}

bool validation_layer_available(const char* name)
{
    std::uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    for (const auto& l : layers)
    {
        if (std::strcmp(l.layerName, name) == 0)
        {
            return true;
        }
    }
    return false;
}

VkResult create_debug_messenger(VkInstance instance,
                                const VkDebugUtilsMessengerCreateInfoEXT* info,
                                VkDebugUtilsMessengerEXT* out)
{
    auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    if (fn == nullptr)
    {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
    return fn(instance, info, nullptr, out);
}

void destroy_debug_messenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger) noexcept
{
    auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (fn != nullptr)
    {
        fn(instance, messenger, nullptr);
    }
}

} // namespace

VulkanHeadlessDevice::VulkanHeadlessDevice()
{
    try
    {
        create_instance();
        setup_debug_messenger();
        pick_physical_device();
        create_logical_device();
    }
    catch (...)
    {
        cleanup();
        throw;
    }
}

VulkanHeadlessDevice::~VulkanHeadlessDevice()
{
    cleanup();
}

std::uint32_t VulkanHeadlessDevice::total_validation_message_count() noexcept
{
    return g_total_validation_messages.load(std::memory_order_relaxed);
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanHeadlessDevice::debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* user)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        const char* msg = (data && data->pMessage) ? data->pMessage : "(no message)";
        if (user != nullptr)
        {
            auto* self = static_cast<VulkanHeadlessDevice*>(user);
            ++self->validation_messages_;
        }
        g_total_validation_messages.fetch_add(1, std::memory_order_relaxed);

        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        {
            mosaic::foundation::log::error("vulkan", "{}", msg);
        }
        else
        {
            mosaic::foundation::log::warn("vulkan", "{}", msg);
        }
    }
    return VK_FALSE;
}

void VulkanHeadlessDevice::create_instance()
{
    const char* validation_layer = "VK_LAYER_KHRONOS_validation";
    validation_enabled_ = validation_layer_available(validation_layer);

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "mosaic_headless";
    app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app.pEngineName = "mosaic";
    app.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app.apiVersion = VK_API_VERSION_1_2;

    const char* debug_ext = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;

    if (validation_enabled_)
    {
        ci.enabledLayerCount = 1;
        ci.ppEnabledLayerNames = &validation_layer;
        ci.enabledExtensionCount = 1;
        ci.ppEnabledExtensionNames = &debug_ext;
    }
    else
    {
        mosaic::foundation::log::warn(
            "vulkan", "validation layer '{}' not available; continuing without it",
            validation_layer);
    }

    vk_check(vkCreateInstance(&ci, nullptr, &instance_), "vkCreateInstance");
}

void VulkanHeadlessDevice::setup_debug_messenger()
{
    if (!validation_enabled_)
    {
        return;
    }

    VkDebugUtilsMessengerCreateInfoEXT dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    dci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    dci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    dci.pfnUserCallback = debug_callback;
    dci.pUserData = this;

    vk_check(create_debug_messenger(instance_, &dci, &messenger_),
             "vkCreateDebugUtilsMessengerEXT");
}

void VulkanHeadlessDevice::pick_physical_device()
{
    std::uint32_t count = 0;
    vk_check(vkEnumeratePhysicalDevices(instance_, &count, nullptr),
             "vkEnumeratePhysicalDevices");
    if (count == 0)
    {
        throw std::runtime_error("no Vulkan physical devices found");
    }

    std::vector<VkPhysicalDevice> devices(count);
    vk_check(vkEnumeratePhysicalDevices(instance_, &count, devices.data()),
             "vkEnumeratePhysicalDevices");

    for (VkPhysicalDevice dev : devices)
    {
        std::uint32_t qcount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qcount, nullptr);
        std::vector<VkQueueFamilyProperties> families(qcount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qcount, families.data());

        for (std::uint32_t i = 0; i < qcount; ++i)
        {
            if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                physical_ = dev;
                graphics_family_ = i;
                return;
            }
        }
    }

    throw std::runtime_error(
        "no suitable Vulkan physical device (need a graphics queue family)");
}

void VulkanHeadlessDevice::create_logical_device()
{
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo qi{};
    qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qi.queueFamilyIndex = graphics_family_;
    qi.queueCount = 1;
    qi.pQueuePriorities = &priority;

    VkPhysicalDeviceFeatures features{};

    VkDeviceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount = 1;
    ci.pQueueCreateInfos = &qi;
    ci.enabledExtensionCount = 0;
    ci.ppEnabledExtensionNames = nullptr;
    ci.pEnabledFeatures = &features;

    vk_check(vkCreateDevice(physical_, &ci, nullptr, &device_), "vkCreateDevice");

    vkGetDeviceQueue(device_, graphics_family_, 0, &graphics_queue_);
    if (graphics_queue_ == VK_NULL_HANDLE)
    {
        throw std::runtime_error("vkGetDeviceQueue returned VK_NULL_HANDLE");
    }
}

void VulkanHeadlessDevice::cleanup() noexcept
{
    if (device_ != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(device_);
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    if (messenger_ != VK_NULL_HANDLE)
    {
        destroy_debug_messenger(instance_, messenger_);
        messenger_ = VK_NULL_HANDLE;
    }
    if (instance_ != VK_NULL_HANDLE)
    {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
}

} // namespace mosaic::render::vk
