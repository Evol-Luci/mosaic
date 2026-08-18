#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include "engine/render/render_device.hpp"
#include "modules/render_vulkan/vulkan_render_device.hpp"
#include "modules/render_vulkan/vulkan_swapchain_support.hpp"
#include "foundation/logging/log.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

// NOTE: intentionally NO `namespace log = mosaic::foundation::log;` — a `log`
// alias shadows the C `::log` builtin and trips a compiler warning. Every log
// call below is fully qualified as `mosaic::foundation::log::...`.

namespace mosaic::render::vk
{
namespace
{

using mosaic::engine::render::Color;
using mosaic::engine::render::RenderDevice;

constexpr int kFramesInFlight = 2;

// Throw on any non-success VkResult so bring-up failures surface as exceptions
// (the factory contract is "throws std::runtime_error on failure").
void vk_check(VkResult r, const char* what)
{
    if (r != VK_SUCCESS)
    {
        throw std::runtime_error(std::string(what) + " failed: VkResult " +
                                 std::to_string(static_cast<int>(r)));
    }
}

#ifndef NDEBUG
// Route validation-layer messages to the engine log. Returns VK_FALSE so the
// offending call is NOT aborted (per the debug-utils contract).
VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* /*user*/)
{
    const char* msg = (data && data->pMessage) ? data->pMessage : "(no message)";
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
        mosaic::foundation::log::error("vulkan", "{}", msg);
    }
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        mosaic::foundation::log::warn("vulkan", "{}", msg);
    }
    return VK_FALSE;
}

void populate_debug_info(VkDebugUtilsMessengerCreateInfoEXT& info)
{
    info = {};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = debug_callback;
}

// vkCreate/DestroyDebugUtilsMessengerEXT are extension entry points and must be
// loaded via vkGetInstanceProcAddr rather than linked directly.
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

void destroy_debug_messenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger)
{
    auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (fn != nullptr)
    {
        fn(instance, messenger, nullptr);
    }
}

bool validation_layer_available(const char* name)
{
    uint32_t count = 0;
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
#endif // !NDEBUG

// Vulkan implementation of the neutral RenderDevice. Owns everything it creates
// (except the window, owned by the app) and tears it down in reverse order.
class VulkanRenderDevice final : public RenderDevice
{
public:
    explicit VulkanRenderDevice(GLFWwindow* window) : window_(window)
    {
        create_instance();
#ifndef NDEBUG
        setup_debug_messenger();
#endif
        vk_check(glfwCreateWindowSurface(instance_, window_, nullptr, &surface_),
                 "glfwCreateWindowSurface");
        pick_physical_device();
        create_logical_device();
        create_swapchain();
        create_image_views();
        create_render_pass();
        create_framebuffers();
        create_command_pool_and_buffers();
        create_sync_objects();

        glfwSetWindowUserPointer(window_, this);
        glfwSetFramebufferSizeCallback(window_, framebuffer_resize_callback);
    }

    VulkanRenderDevice(const VulkanRenderDevice&) = delete;
    VulkanRenderDevice& operator=(const VulkanRenderDevice&) = delete;

    ~VulkanRenderDevice() override
    {
        if (device_ != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(device_);

            for (int i = 0; i < kFramesInFlight; ++i)
            {
                if (render_finished_[i] != VK_NULL_HANDLE)
                {
                    vkDestroySemaphore(device_, render_finished_[i], nullptr);
                }
                if (image_available_[i] != VK_NULL_HANDLE)
                {
                    vkDestroySemaphore(device_, image_available_[i], nullptr);
                }
                if (in_flight_[i] != VK_NULL_HANDLE)
                {
                    vkDestroyFence(device_, in_flight_[i], nullptr);
                }
            }

            if (command_pool_ != VK_NULL_HANDLE)
            {
                vkDestroyCommandPool(device_, command_pool_, nullptr);
            }

            destroy_swapchain_objects(); // framebuffers, then views, then swapchain

            if (render_pass_ != VK_NULL_HANDLE)
            {
                vkDestroyRenderPass(device_, render_pass_, nullptr);
            }

            vkDestroyDevice(device_, nullptr);
        }

        if (surface_ != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(instance_, surface_, nullptr);
        }
#ifndef NDEBUG
        if (messenger_ != VK_NULL_HANDLE)
        {
            destroy_debug_messenger(instance_, messenger_);
        }
#endif
        if (instance_ != VK_NULL_HANDLE)
        {
            vkDestroyInstance(instance_, nullptr);
        }
    }

    // --- RenderDevice interface -------------------------------------------

    mosaic::engine::render::MeshHandle
    create_mesh(const std::vector<mosaic::engine::render::Vertex>&,
                const std::vector<std::uint32_t>&) override
    {
        throw std::runtime_error("VulkanRenderDevice::create_mesh not implemented (36b Task 3)");
    }

    void draw(mosaic::engine::render::MeshHandle,
              const mosaic::foundation::math::Mat4&) override
    {
        throw std::runtime_error("VulkanRenderDevice::draw not implemented (36b Task 3)");
    }

    void begin_frame() override
    {
        frame_skipped_ = false;

        vkWaitForFences(device_, 1, &in_flight_[frame_], VK_TRUE, UINT64_MAX);

        VkResult r = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                                           image_available_[frame_], VK_NULL_HANDLE,
                                           &image_index_);
        if (r == VK_ERROR_OUT_OF_DATE_KHR)
        {
            recreate_swapchain();
            frame_skipped_ = true; // end_frame will no-op this frame
            return;
        }
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
        {
            vk_check(r, "vkAcquireNextImageKHR");
        }

        // Only reset the fence once we know we will submit work this frame,
        // otherwise a skipped frame would leave the fence unsignaled forever.
        vkResetFences(device_, 1, &in_flight_[frame_]);
        vkResetCommandBuffer(cmd_[frame_], 0);

        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vk_check(vkBeginCommandBuffer(cmd_[frame_], &begin), "vkBeginCommandBuffer");
    }

    void clear(const Color& c) override
    {
        if (frame_skipped_)
        {
            return; // no image was acquired this frame
        }

        VkClearValue clear_value{};
        clear_value.color = {{c.r, c.g, c.b, c.a}};

        VkRenderPassBeginInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = render_pass_;
        rp.framebuffer = framebuffers_[image_index_];
        rp.renderArea.offset = {0, 0};
        rp.renderArea.extent = extent_;
        rp.clearValueCount = 1;
        rp.pClearValues = &clear_value;

        // 36a draws nothing: the CLEAR loadOp does the work, so begin+end the
        // render pass immediately. 36b will insert draws before end.
        vkCmdBeginRenderPass(cmd_[frame_], &rp, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdEndRenderPass(cmd_[frame_]);
    }

    void end_frame() override
    {
        if (frame_skipped_)
        {
            frame_skipped_ = false;
            return;
        }

        vk_check(vkEndCommandBuffer(cmd_[frame_]), "vkEndCommandBuffer");

        VkSemaphore          wait_sems[]   = {image_available_[frame_]};
        VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        VkSemaphore          signal_sems[] = {render_finished_[frame_]};

        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = wait_sems;
        submit.pWaitDstStageMask = wait_stages;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd_[frame_];
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = signal_sems;
        vk_check(vkQueueSubmit(graphics_queue_, 1, &submit, in_flight_[frame_]),
                 "vkQueueSubmit");

        VkPresentInfoKHR present{};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = signal_sems;
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain_;
        present.pImageIndices = &image_index_;

        VkResult pr = vkQueuePresentKHR(present_queue_, &present);
        if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR || framebuffer_resized_)
        {
            framebuffer_resized_ = false;
            recreate_swapchain();
        }
        else
        {
            vk_check(pr, "vkQueuePresentKHR");
        }

        frame_ = (frame_ + 1) % kFramesInFlight;
    }

private:
    // --- static GLFW callback ---------------------------------------------

    static void framebuffer_resize_callback(GLFWwindow* w, int /*width*/, int /*height*/)
    {
        auto* self = static_cast<VulkanRenderDevice*>(glfwGetWindowUserPointer(w));
        self->framebuffer_resized_ = true;
    }

    // --- construction steps -----------------------------------------------

    void create_instance()
    {
        VkApplicationInfo app{};
        app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName = "mosaic";
        app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        app.pEngineName = "mosaic";
        app.engineVersion = VK_MAKE_VERSION(0, 1, 0);
        app.apiVersion = VK_API_VERSION_1_2;

        uint32_t glfw_count = 0;
        const char** glfw_exts = glfwGetRequiredInstanceExtensions(&glfw_count);
        if (glfw_exts == nullptr)
        {
            throw std::runtime_error(
                "glfwGetRequiredInstanceExtensions returned null (no Vulkan loader?)");
        }
        std::vector<const char*> extensions(glfw_exts, glfw_exts + glfw_count);

        VkInstanceCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pApplicationInfo = &app;

#ifndef NDEBUG
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        const char* validation_layer = "VK_LAYER_KHRONOS_validation";
        VkDebugUtilsMessengerCreateInfoEXT dbg{};
        if (validation_layer_available(validation_layer))
        {
            ci.enabledLayerCount = 1;
            ci.ppEnabledLayerNames = &validation_layer;
            // Chaining the messenger info covers instance create/destroy too.
            populate_debug_info(dbg);
            ci.pNext = &dbg;
        }
        else
        {
            mosaic::foundation::log::warn(
                "vulkan", "validation layer '{}' not available; continuing without it",
                validation_layer);
        }
#endif

        ci.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        ci.ppEnabledExtensionNames = extensions.data();

        vk_check(vkCreateInstance(&ci, nullptr, &instance_), "vkCreateInstance");
    }

#ifndef NDEBUG
    void setup_debug_messenger()
    {
        VkDebugUtilsMessengerCreateInfoEXT info;
        populate_debug_info(info);
        vk_check(create_debug_messenger(instance_, &info, &messenger_),
                 "vkCreateDebugUtilsMessengerEXT");
    }
#endif

    static bool device_supports_swapchain(VkPhysicalDevice dev)
    {
        uint32_t count = 0;
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &count, nullptr);
        std::vector<VkExtensionProperties> exts(count);
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &count, exts.data());
        for (const auto& e : exts)
        {
            if (std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
            {
                return true;
            }
        }
        return false;
    }

    void pick_physical_device()
    {
        uint32_t count = 0;
        vkEnumeratePhysicalDevices(instance_, &count, nullptr);
        if (count == 0)
        {
            throw std::runtime_error("no Vulkan physical devices found");
        }
        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(instance_, &count, devices.data());

        for (VkPhysicalDevice dev : devices)
        {
            uint32_t qcount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(dev, &qcount, nullptr);
            std::vector<VkQueueFamilyProperties> families(qcount);
            vkGetPhysicalDeviceQueueFamilyProperties(dev, &qcount, families.data());

            std::vector<VkBool32> present_support(qcount, VK_FALSE);
            for (uint32_t i = 0; i < qcount; ++i)
            {
                vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface_, &present_support[i]);
            }

            QueueFamilies q = pick_queue_families(families, present_support);
            if (!q.complete())
            {
                continue;
            }
            if (!device_supports_swapchain(dev))
            {
                continue;
            }

            uint32_t fmt_count = 0;
            vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface_, &fmt_count, nullptr);
            uint32_t pm_count = 0;
            vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface_, &pm_count, nullptr);
            if (fmt_count == 0 || pm_count == 0)
            {
                continue;
            }

            physical_ = dev;
            graphics_family_ = *q.graphics;
            present_family_ = *q.present;
            return;
        }

        throw std::runtime_error(
            "no suitable Vulkan physical device (need graphics+present+swapchain)");
    }

    void create_logical_device()
    {
        const float priority = 1.0f;
        std::vector<uint32_t> unique_families = {graphics_family_};
        if (present_family_ != graphics_family_)
        {
            unique_families.push_back(present_family_);
        }

        std::vector<VkDeviceQueueCreateInfo> queue_infos;
        queue_infos.reserve(unique_families.size());
        for (uint32_t fam : unique_families)
        {
            VkDeviceQueueCreateInfo qi{};
            qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            qi.queueFamilyIndex = fam;
            qi.queueCount = 1;
            qi.pQueuePriorities = &priority;
            queue_infos.push_back(qi);
        }

        const char* device_ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        VkPhysicalDeviceFeatures features{};

        VkDeviceCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        ci.queueCreateInfoCount = static_cast<uint32_t>(queue_infos.size());
        ci.pQueueCreateInfos = queue_infos.data();
        ci.enabledExtensionCount = 1;
        ci.ppEnabledExtensionNames = &device_ext;
        ci.pEnabledFeatures = &features;

        vk_check(vkCreateDevice(physical_, &ci, nullptr, &device_), "vkCreateDevice");

        vkGetDeviceQueue(device_, graphics_family_, 0, &graphics_queue_);
        vkGetDeviceQueue(device_, present_family_, 0, &present_queue_);
    }

    // --- recreatable swapchain group --------------------------------------

    void create_swapchain()
    {
        VkSurfaceCapabilitiesKHR caps{};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &caps);

        uint32_t fmt_count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &fmt_count, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(fmt_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &fmt_count, formats.data());

        uint32_t pm_count = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &pm_count, nullptr);
        std::vector<VkPresentModeKHR> present_modes(pm_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &pm_count,
                                                  present_modes.data());

        VkSurfaceFormatKHR surface_format = choose_surface_format(formats);
        VkPresentModeKHR present_mode = choose_present_mode(present_modes);

        int fb_w = 0, fb_h = 0;
        glfwGetFramebufferSize(window_, &fb_w, &fb_h);
        VkExtent2D desired{static_cast<uint32_t>(fb_w), static_cast<uint32_t>(fb_h)};
        extent_ = clamp_extent(desired, caps);

        uint32_t image_count = caps.minImageCount + 1;
        if (caps.maxImageCount > 0 && image_count > caps.maxImageCount)
        {
            image_count = caps.maxImageCount;
        }

        VkSwapchainCreateInfoKHR ci{};
        ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        ci.surface = surface_;
        ci.minImageCount = image_count;
        ci.imageFormat = surface_format.format;
        ci.imageColorSpace = surface_format.colorSpace;
        ci.imageExtent = extent_;
        ci.imageArrayLayers = 1;
        ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        uint32_t indices[] = {graphics_family_, present_family_};
        if (graphics_family_ != present_family_)
        {
            ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            ci.queueFamilyIndexCount = 2;
            ci.pQueueFamilyIndices = indices;
        }
        else
        {
            ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        ci.preTransform = caps.currentTransform;
        ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        ci.presentMode = present_mode;
        ci.clipped = VK_TRUE;
        ci.oldSwapchain = VK_NULL_HANDLE;

        vk_check(vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain_),
                 "vkCreateSwapchainKHR");

        swapchain_format_ = surface_format.format;

        uint32_t actual = 0;
        vkGetSwapchainImagesKHR(device_, swapchain_, &actual, nullptr);
        images_.resize(actual);
        vkGetSwapchainImagesKHR(device_, swapchain_, &actual, images_.data());
    }

    void create_image_views()
    {
        views_.resize(images_.size());
        for (std::size_t i = 0; i < images_.size(); ++i)
        {
            VkImageViewCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            ci.image = images_[i];
            ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            ci.format = swapchain_format_;
            ci.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            ci.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            ci.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            ci.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            ci.subresourceRange.baseMipLevel = 0;
            ci.subresourceRange.levelCount = 1;
            ci.subresourceRange.baseArrayLayer = 0;
            ci.subresourceRange.layerCount = 1;
            vk_check(vkCreateImageView(device_, &ci, nullptr, &views_[i]),
                     "vkCreateImageView");
        }
    }

    // Format-stable: created once in the ctor, never recreated on resize.
    void create_render_pass()
    {
        VkAttachmentDescription color{};
        color.format = swapchain_format_;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference color_ref{};
        color_ref.attachment = 0;
        color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_ref;

        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.srcAccessMask = 0;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = 1;
        ci.pAttachments = &color;
        ci.subpassCount = 1;
        ci.pSubpasses = &subpass;
        ci.dependencyCount = 1;
        ci.pDependencies = &dep;

        vk_check(vkCreateRenderPass(device_, &ci, nullptr, &render_pass_),
                 "vkCreateRenderPass");
    }

    void create_framebuffers()
    {
        framebuffers_.resize(views_.size());
        for (std::size_t i = 0; i < views_.size(); ++i)
        {
            VkImageView attachments[] = {views_[i]};

            VkFramebufferCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            ci.renderPass = render_pass_;
            ci.attachmentCount = 1;
            ci.pAttachments = attachments;
            ci.width = extent_.width;
            ci.height = extent_.height;
            ci.layers = 1;
            vk_check(vkCreateFramebuffer(device_, &ci, nullptr, &framebuffers_[i]),
                     "vkCreateFramebuffer");
        }
    }

    void create_command_pool_and_buffers()
    {
        VkCommandPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool.queueFamilyIndex = graphics_family_;
        vk_check(vkCreateCommandPool(device_, &pool, nullptr, &command_pool_),
                 "vkCreateCommandPool");

        VkCommandBufferAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc.commandPool = command_pool_;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = kFramesInFlight;
        vk_check(vkAllocateCommandBuffers(device_, &alloc, cmd_.data()),
                 "vkAllocateCommandBuffers");
    }

    void create_sync_objects()
    {
        VkSemaphoreCreateInfo sem{};
        sem.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fence{};
        fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        // Signaled so the first begin_frame's wait returns immediately.
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (int i = 0; i < kFramesInFlight; ++i)
        {
            vk_check(vkCreateSemaphore(device_, &sem, nullptr, &image_available_[i]),
                     "vkCreateSemaphore");
            vk_check(vkCreateSemaphore(device_, &sem, nullptr, &render_finished_[i]),
                     "vkCreateSemaphore");
            vk_check(vkCreateFence(device_, &fence, nullptr, &in_flight_[i]),
                     "vkCreateFence");
        }
    }

    // --- teardown / recreate ----------------------------------------------

    void destroy_swapchain_objects()
    {
        for (VkFramebuffer fb : framebuffers_)
        {
            vkDestroyFramebuffer(device_, fb, nullptr);
        }
        framebuffers_.clear();

        for (VkImageView view : views_)
        {
            vkDestroyImageView(device_, view, nullptr);
        }
        views_.clear();

        if (swapchain_ != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(device_, swapchain_, nullptr);
            swapchain_ = VK_NULL_HANDLE;
        }
    }

    void recreate_swapchain()
    {
        // Block while minimized (zero-sized framebuffer) — a 0x0 swapchain is invalid.
        int w = 0, h = 0;
        glfwGetFramebufferSize(window_, &w, &h);
        while (w == 0 || h == 0)
        {
            glfwWaitEvents();
            glfwGetFramebufferSize(window_, &w, &h);
        }

        vkDeviceWaitIdle(device_);

        destroy_swapchain_objects();

        create_swapchain();
        create_image_views();
        create_framebuffers();
        // render pass is format-stable, so it is intentionally not recreated
    }

    // --- members ----------------------------------------------------------

    GLFWwindow* window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
#ifndef NDEBUG
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
#endif
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    VkQueue present_queue_ = VK_NULL_HANDLE;
    uint32_t graphics_family_ = 0;
    uint32_t present_family_ = 0;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchain_format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_ = {0, 0};
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;

    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;

    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, kFramesInFlight> cmd_{};

    VkSemaphore image_available_[kFramesInFlight] = {};
    VkSemaphore render_finished_[kFramesInFlight] = {};
    VkFence in_flight_[kFramesInFlight] = {};

    uint32_t frame_ = 0;
    uint32_t image_index_ = 0;
    bool framebuffer_resized_ = false;
    bool frame_skipped_ = false;
};

} // namespace

std::unique_ptr<mosaic::engine::render::RenderDevice>
create_vulkan_render_device(GLFWwindow* window)
{
    return std::make_unique<VulkanRenderDevice>(window);
}

} // namespace mosaic::render::vk
