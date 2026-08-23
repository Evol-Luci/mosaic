#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include "engine/render/render_device.hpp"
#include "modules/render_vulkan/vulkan_render_device.hpp"
#include "modules/render_vulkan/vulkan_swapchain_support.hpp"
#include "foundation/logging/log.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
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
using mosaic::engine::render::DrawParams;
using mosaic::engine::render::MeshHandle;
using mosaic::engine::render::RenderDevice;
using mosaic::engine::render::Vertex;
using mosaic::foundation::math::Mat4;

constexpr int kFramesInFlight = 2;

struct PushConstants
{
    float mvp[16];
    float light_direction[4]; // vec3 + pad
    float light_color[4];
    float base_color[4];
    float point_position[4]; // xyz = object-space pos, w = range
    float point_color[4];    // xyz = color * intensity
};
static_assert(sizeof(PushConstants) == 144, "push constant layout must match the shader");

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
        create_depth_resources();
        create_render_pass();
        create_framebuffers();
        create_descriptor_infrastructure();
        create_pipeline();
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

            if (ui_ready_)
            {
                ImGui_ImplVulkan_Shutdown();
                ImGui_ImplGlfw_Shutdown();
                ImGui::DestroyContext();
                ui_ready_ = false;
            }
            if (imgui_pool_ != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(device_, imgui_pool_, nullptr);
                imgui_pool_ = VK_NULL_HANDLE;
            }

            // Device-local mesh buffers (4 Vulkan objects each) come down first.
            for (MeshBuffers& mesh : meshes_)
            {
                if (mesh.vbuf != VK_NULL_HANDLE) { vkDestroyBuffer(device_, mesh.vbuf, nullptr); }
                if (mesh.vmem != VK_NULL_HANDLE) { vkFreeMemory(device_, mesh.vmem, nullptr); }
                if (mesh.ibuf != VK_NULL_HANDLE) { vkDestroyBuffer(device_, mesh.ibuf, nullptr); }
                if (mesh.imem != VK_NULL_HANDLE) { vkFreeMemory(device_, mesh.imem, nullptr); }
            }
            meshes_.clear();

            // Textures (sampler, view, image, memory), then the pool (which
            // frees the descriptor sets), then the set layout.
            for (Texture& tex : textures_)
            {
                if (tex.sampler != VK_NULL_HANDLE) { vkDestroySampler(device_, tex.sampler, nullptr); }
                if (tex.view != VK_NULL_HANDLE) { vkDestroyImageView(device_, tex.view, nullptr); }
                if (tex.image != VK_NULL_HANDLE) { vkDestroyImage(device_, tex.image, nullptr); }
                if (tex.memory != VK_NULL_HANDLE) { vkFreeMemory(device_, tex.memory, nullptr); }
            }
            textures_.clear();

            if (descriptor_pool_ != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
            }
            if (texture_set_layout_ != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(device_, texture_set_layout_, nullptr);
            }

            if (pipeline_ != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(device_, pipeline_, nullptr);
            }
            if (pipeline_layout_ != VK_NULL_HANDLE)
            {
                vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
            }

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

    MeshHandle create_mesh(const std::vector<Vertex>& vertices,
                           const std::vector<std::uint32_t>& indices) override
    {
        if (vertices.empty() || indices.empty())
        {
            return 0; // nothing to upload
        }

        MeshBuffers mesh{};
        mesh.index_count = static_cast<uint32_t>(indices.size());

        const VkDeviceSize vsize = sizeof(Vertex) * vertices.size();
        const VkDeviceSize isize = sizeof(std::uint32_t) * indices.size();

        upload_via_staging(vertices.data(), vsize,
                           VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           mesh.vbuf, mesh.vmem);
        upload_via_staging(indices.data(), isize,
                           VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           mesh.ibuf, mesh.imem);

        meshes_.push_back(mesh);
        return static_cast<MeshHandle>(meshes_.size()); // handle == index + 1
    }

    // Upload an RGBA8 image to a device-local sampled VkImage and wire it to a
    // descriptor set so the fragment shader can sample it. Returns a 1-based
    // handle (index into textures_ + 1); 0 for an empty image.
    mosaic::engine::render::TextureHandle
    create_texture(const mosaic::engine::render::Image& image) override
    {
        if (image.pixels.empty())
        {
            return 0u;
        }

        const VkDeviceSize size = static_cast<VkDeviceSize>(image.pixels.size());

        // 1) Staging buffer (host-visible) holding the pixel bytes.
        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory staging_mem = VK_NULL_HANDLE;
        create_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      staging, staging_mem);

        void* mapped = nullptr;
        vk_check(vkMapMemory(device_, staging_mem, 0, size, 0, &mapped), "vkMapMemory(texture)");
        std::memcpy(mapped, image.pixels.data(), static_cast<std::size_t>(size));
        vkUnmapMemory(device_, staging_mem);

        // 2) Device-local image (sRGB, sampled + transfer destination).
        Texture tex{};

        VkImageCreateInfo img{};
        img.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        img.imageType = VK_IMAGE_TYPE_2D;
        img.extent.width = image.width;
        img.extent.height = image.height;
        img.extent.depth = 1;
        img.mipLevels = 1;
        img.arrayLayers = 1;
        img.format = VK_FORMAT_R8G8B8A8_SRGB;
        img.tiling = VK_IMAGE_TILING_OPTIMAL;
        img.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        img.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        img.samples = VK_SAMPLE_COUNT_1_BIT;
        img.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vk_check(vkCreateImage(device_, &img, nullptr, &tex.image), "vkCreateImage(texture)");

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(device_, tex.image, &req);
        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex =
            find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vk_check(vkAllocateMemory(device_, &alloc, nullptr, &tex.memory),
                 "vkAllocateMemory(texture)");
        vk_check(vkBindImageMemory(device_, tex.image, tex.memory, 0),
                 "vkBindImageMemory(texture)");

        // 3) One-time submit: transition UNDEFINED->TRANSFER_DST, copy, then
        //    TRANSFER_DST->SHADER_READ_ONLY so the fragment stage can sample it.
        VkCommandBuffer cmd = begin_single_time_commands();

        VkImageMemoryBarrier to_transfer{};
        to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_transfer.image = tex.image;
        to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        to_transfer.subresourceRange.baseMipLevel = 0;
        to_transfer.subresourceRange.levelCount = 1;
        to_transfer.subresourceRange.baseArrayLayer = 0;
        to_transfer.subresourceRange.layerCount = 1;
        to_transfer.srcAccessMask = 0;
        to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &to_transfer);

        VkBufferImageCopy copy{};
        copy.bufferOffset = 0;
        copy.bufferRowLength = 0;
        copy.bufferImageHeight = 0;
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.mipLevel = 0;
        copy.imageSubresource.baseArrayLayer = 0;
        copy.imageSubresource.layerCount = 1;
        copy.imageOffset = {0, 0, 0};
        copy.imageExtent = {image.width, image.height, 1};
        vkCmdCopyBufferToImage(cmd, staging, tex.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        VkImageMemoryBarrier to_read{};
        to_read.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        to_read.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_read.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        to_read.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_read.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_read.image = tex.image;
        to_read.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        to_read.subresourceRange.baseMipLevel = 0;
        to_read.subresourceRange.levelCount = 1;
        to_read.subresourceRange.baseArrayLayer = 0;
        to_read.subresourceRange.layerCount = 1;
        to_read.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_read.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &to_read);

        end_single_time_commands(cmd);

        vkDestroyBuffer(device_, staging, nullptr);
        vkFreeMemory(device_, staging_mem, nullptr);

        // 4) View + sampler.
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = tex.image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = VK_FORMAT_R8G8B8A8_SRGB;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.baseMipLevel = 0;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.baseArrayLayer = 0;
        view.subresourceRange.layerCount = 1;
        vk_check(vkCreateImageView(device_, &view, nullptr, &tex.view),
                 "vkCreateImageView(texture)");

        VkSamplerCreateInfo sampler{};
        sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler.magFilter = VK_FILTER_LINEAR;
        sampler.minFilter = VK_FILTER_LINEAR;
        sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler.anisotropyEnable = VK_FALSE;
        sampler.maxAnisotropy = 1.0f;
        sampler.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        sampler.unnormalizedCoordinates = VK_FALSE;
        sampler.compareEnable = VK_FALSE;
        sampler.compareOp = VK_COMPARE_OP_ALWAYS;
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler.mipLodBias = 0.0f;
        sampler.minLod = 0.0f;
        sampler.maxLod = 0.0f;
        vk_check(vkCreateSampler(device_, &sampler, nullptr, &tex.sampler),
                 "vkCreateSampler(texture)");

        // 5) Allocate + write the descriptor set (binding 0: this image+sampler).
        VkDescriptorSetAllocateInfo set_alloc{};
        set_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        set_alloc.descriptorPool = descriptor_pool_;
        set_alloc.descriptorSetCount = 1;
        set_alloc.pSetLayouts = &texture_set_layout_;
        vk_check(vkAllocateDescriptorSets(device_, &set_alloc, &tex.set),
                 "vkAllocateDescriptorSets(texture)");

        VkDescriptorImageInfo image_info{};
        image_info.sampler = tex.sampler;
        image_info.imageView = tex.view;
        image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = tex.set;
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &image_info;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

        textures_.push_back(tex);
        return static_cast<mosaic::engine::render::TextureHandle>(textures_.size());
    }

    void draw(MeshHandle mesh, mosaic::engine::render::TextureHandle texture,
              const mosaic::engine::render::DrawParams& params) override
    {
        if (frame_skipped_ || mesh == 0 || mesh > meshes_.size())
        {
            return;
        }

        // Map the 1-based handle to an index; fall back to the first texture
        // (the default white, always created first) for 0 or out-of-range.
        const std::uint32_t tex_index =
            (texture == 0 || texture > textures_.size()) ? 0u : texture - 1u;
        if (!textures_.empty())
        {
            vkCmdBindDescriptorSets(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_layout_, 0, 1, &textures_[tex_index].set, 0,
                                    nullptr);
        }

        const MeshBuffers& mb = meshes_[mesh - 1];
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd_[frame_], 0, 1, &mb.vbuf, &offset);
        vkCmdBindIndexBuffer(cmd_[frame_], mb.ibuf, 0, VK_INDEX_TYPE_UINT32);

        PushConstants pc{};
        std::memcpy(pc.mvp, params.mvp.m.data(), sizeof(pc.mvp));
        pc.light_direction[0] = params.light_direction.x;
        pc.light_direction[1] = params.light_direction.y;
        pc.light_direction[2] = params.light_direction.z;
        pc.light_color[0] = params.light_color.x;
        pc.light_color[1] = params.light_color.y;
        pc.light_color[2] = params.light_color.z;
        pc.base_color[0] = params.base_color.x;
        pc.base_color[1] = params.base_color.y;
        pc.base_color[2] = params.base_color.z;
        pc.point_position[0] = params.point_position.x;
        pc.point_position[1] = params.point_position.y;
        pc.point_position[2] = params.point_position.z;
        pc.point_position[3] = params.point_range;
        pc.point_color[0] = params.point_color.x;
        pc.point_color[1] = params.point_color.y;
        pc.point_color[2] = params.point_color.z;
        vkCmdPushConstants(cmd_[frame_], pipeline_layout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(pc), &pc);
        vkCmdDrawIndexed(cmd_[frame_], mb.index_count, 1, 0, 0, 0);
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

        // Negative-height viewport flips Y so that our right-handed, Y-up world
        // maps to Vulkan's Y-down clip space (origin top-left) — the cube reads
        // right-side up without transposing the projection.
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = static_cast<float>(extent_.height);
        viewport.width = static_cast<float>(extent_.width);
        viewport.height = -static_cast<float>(extent_.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd_[frame_], 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = extent_;
        vkCmdSetScissor(cmd_[frame_], 0, 1, &scissor);

        // Two clear values: color (from the last clear() call) + depth reset to 1.0.
        std::array<VkClearValue, 2> clears{};
        clears[0].color = {{clear_color_.r, clear_color_.g, clear_color_.b, clear_color_.a}};
        clears[1].depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = render_pass_;
        rp.framebuffer = framebuffers_[image_index_];
        rp.renderArea.offset = {0, 0};
        rp.renderArea.extent = extent_;
        rp.clearValueCount = static_cast<uint32_t>(clears.size());
        rp.pClearValues = clears.data();
        vkCmdBeginRenderPass(cmd_[frame_], &rp, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    }

    void clear(const Color& c) override
    {
        // The render pass (begun in begin_frame) clears via loadOp, so clear()
        // only records the color to use for the next begin_frame's clear values.
        clear_color_ = c;
    }

    void end_frame() override
    {
        if (frame_skipped_)
        {
            frame_skipped_ = false;
            return;
        }

        vkCmdEndRenderPass(cmd_[frame_]);

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

    void init_ui() override
    {
        if (ui_ready_) { return; }
        // Dedicated descriptor pool for ImGui (combined-image-samplers for its textures).
        VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64};
        VkDescriptorPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool.maxSets = 64;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &pool_size;
        vk_check(vkCreateDescriptorPool(device_, &pool, nullptr, &imgui_pool_),
                 "vkCreateDescriptorPool(imgui)");

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        ImGui::StyleColorsDark();

        ImGui_ImplGlfw_InitForVulkan(window_, true);

        ImGui_ImplVulkan_InitInfo info{};
        info.Instance = instance_;
        info.PhysicalDevice = physical_;
        info.Device = device_;
        info.QueueFamily = graphics_family_;
        info.Queue = graphics_queue_;
        info.DescriptorPool = imgui_pool_;
        info.RenderPass = render_pass_;
        info.MinImageCount = kFramesInFlight;
        info.ImageCount = kFramesInFlight;
        info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        ImGui_ImplVulkan_Init(&info);

        ui_ready_ = true;
    }

    void begin_ui_frame() override
    {
        if (!ui_ready_) { return; }
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void end_ui_frame() override
    {
        if (!ui_ready_) { return; }
        ImGui::Render(); // always, to keep NewFrame/Render balanced even on a skipped frame
        if (!frame_skipped_)
        {
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd_[frame_]);
        }
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

            // The renderer's 144-byte push constants exceed Vulkan's guaranteed 128-byte
            // minimum (desktop GPUs offer 256). Treat an insufficient budget as "device
            // not suitable" and keep looking, rather than failing on the first candidate —
            // a multi-GPU host may still have a capable device further down the list.
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(dev, &props);
            if (sizeof(PushConstants) > props.limits.maxPushConstantsSize)
            {
                continue;
            }

            physical_ = dev;
            graphics_family_ = *q.graphics;
            present_family_ = *q.present;
            return;
        }

        throw std::runtime_error(
            "no suitable Vulkan physical device (need graphics+present+swapchain and "
            "maxPushConstantsSize >= the renderer's push constants)");
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

    // Pick a memory type that satisfies `type_bits` (from a resource's
    // requirements) and has all requested property flags.
    uint32_t find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags props) const
    {
        VkPhysicalDeviceMemoryProperties mem{};
        vkGetPhysicalDeviceMemoryProperties(physical_, &mem);
        for (uint32_t i = 0; i < mem.memoryTypeCount; ++i)
        {
            if ((type_bits & (1u << i)) != 0u &&
                (mem.memoryTypes[i].propertyFlags & props) == props)
            {
                return i;
            }
        }
        throw std::runtime_error("no suitable Vulkan memory type");
    }

    // Depth buffer: part of the recreatable swapchain group (sized to extent_).
    void create_depth_resources()
    {
        VkImageCreateInfo img{};
        img.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        img.imageType = VK_IMAGE_TYPE_2D;
        img.extent.width = extent_.width;
        img.extent.height = extent_.height;
        img.extent.depth = 1;
        img.mipLevels = 1;
        img.arrayLayers = 1;
        img.format = kDepthFormat;
        img.tiling = VK_IMAGE_TILING_OPTIMAL;
        img.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        img.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        img.samples = VK_SAMPLE_COUNT_1_BIT;
        img.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vk_check(vkCreateImage(device_, &img, nullptr, &depth_image_), "vkCreateImage(depth)");

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(device_, depth_image_, &req);
        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex =
            find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vk_check(vkAllocateMemory(device_, &alloc, nullptr, &depth_memory_),
                 "vkAllocateMemory(depth)");
        vk_check(vkBindImageMemory(device_, depth_image_, depth_memory_, 0),
                 "vkBindImageMemory(depth)");

        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = depth_image_;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = kDepthFormat;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        view.subresourceRange.baseMipLevel = 0;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.baseArrayLayer = 0;
        view.subresourceRange.layerCount = 1;
        vk_check(vkCreateImageView(device_, &view, nullptr, &depth_view_),
                 "vkCreateImageView(depth)");
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

        VkAttachmentDescription depth{};
        depth.format = kDepthFormat;
        depth.samples = VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depth_ref{};
        depth_ref.attachment = 1;
        depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_ref;
        subpass.pDepthStencilAttachment = &depth_ref;

        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.srcAccessMask = 0;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        std::array<VkAttachmentDescription, 2> attachments{color, depth};

        VkRenderPassCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = static_cast<uint32_t>(attachments.size());
        ci.pAttachments = attachments.data();
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
            std::array<VkImageView, 2> attachments{views_[i], depth_view_};

            VkFramebufferCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            ci.renderPass = render_pass_;
            ci.attachmentCount = static_cast<uint32_t>(attachments.size());
            ci.pAttachments = attachments.data();
            ci.width = extent_.width;
            ci.height = extent_.height;
            ci.layers = 1;
            vk_check(vkCreateFramebuffer(device_, &ci, nullptr, &framebuffers_[i]),
                     "vkCreateFramebuffer");
        }
    }

    // --- descriptor infrastructure ----------------------------------------

    // One descriptor set layout (binding 0: a combined image+sampler visible to
    // the fragment stage) and a pool to allocate per-texture sets from. Created
    // once after the framebuffers and before the pipeline (the layout feeds the
    // pipeline layout as set 0).
    void create_descriptor_infrastructure()
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layout{};
        layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout.bindingCount = 1;
        layout.pBindings = &binding;
        vk_check(vkCreateDescriptorSetLayout(device_, &layout, nullptr, &texture_set_layout_),
                 "vkCreateDescriptorSetLayout");

        VkDescriptorPoolSize pool_size{};
        pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_size.descriptorCount = kMaxTextures;

        VkDescriptorPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &pool_size;
        pool.maxSets = kMaxTextures;
        vk_check(vkCreateDescriptorPool(device_, &pool, nullptr, &descriptor_pool_),
                 "vkCreateDescriptorPool");
    }

    // --- pipeline ---------------------------------------------------------

    static std::vector<char> read_file(const std::string& path)
    {
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (!file.is_open())
        {
            throw std::runtime_error("failed to open shader file: " + path);
        }
        const auto size = static_cast<std::size_t>(file.tellg());
        std::vector<char> buffer(size);
        file.seekg(0);
        file.read(buffer.data(), static_cast<std::streamsize>(size));
        return buffer;
    }

    VkShaderModule create_shader_module(const std::vector<char>& code)
    {
        VkShaderModuleCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = code.size();
        ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule module = VK_NULL_HANDLE;
        vk_check(vkCreateShaderModule(device_, &ci, nullptr, &module), "vkCreateShaderModule");
        return module;
    }

    // Created once after the render pass; destroyed in the dtor before the device.
    void create_pipeline()
    {
        std::vector<char> vert_code = read_file(std::string(MOSAIC_SHADER_DIR) + "/cube.vert.spv");
        std::vector<char> frag_code = read_file(std::string(MOSAIC_SHADER_DIR) + "/cube.frag.spv");
        VkShaderModule vert = create_shader_module(vert_code);
        VkShaderModule frag = create_shader_module(frag_code);

        VkPipelineShaderStageCreateInfo vs{};
        vs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vs.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vs.module = vert;
        vs.pName = "main";

        VkPipelineShaderStageCreateInfo fs{};
        fs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fs.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fs.module = frag;
        fs.pName = "main";

        VkPipelineShaderStageCreateInfo stages[] = {vs, fs};

        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(Vertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 3> attrs{};
        attrs[0].location = 0;
        attrs[0].binding = 0;
        attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[0].offset = static_cast<uint32_t>(offsetof(Vertex, position));
        attrs[1].location = 1;
        attrs[1].binding = 0;
        attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[1].offset = static_cast<uint32_t>(offsetof(Vertex, normal));
        attrs[2].location = 2;
        attrs[2].binding = 0;
        attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[2].offset = static_cast<uint32_t>(offsetof(Vertex, uv));

        VkPipelineVertexInputStateCreateInfo vin{};
        vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vin.vertexBindingDescriptionCount = 1;
        vin.pVertexBindingDescriptions = &binding;
        vin.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
        vin.pVertexAttributeDescriptions = attrs.data();

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        ia.primitiveRestartEnable = VK_FALSE;

        // Viewport/scissor are dynamic (set each frame); counts still fixed at 1.
        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1;
        vp.scissorCount = 1;

        VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2;
        dyn.pDynamicStates = dyn_states;

        VkPipelineRasterizationStateCreateInfo rast{};
        rast.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rast.depthClampEnable = VK_FALSE;
        rast.rasterizerDiscardEnable = VK_FALSE;
        rast.polygonMode = VK_POLYGON_MODE_FILL;
        rast.lineWidth = 1.0f;
        rast.cullMode = VK_CULL_MODE_BACK_BIT;
        rast.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rast.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        ms.sampleShadingEnable = VK_FALSE;

        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS;
        depth.depthBoundsTestEnable = VK_FALSE;
        depth.stencilTestEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState blend{};
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blend.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.logicOpEnable = VK_FALSE;
        cb.attachmentCount = 1;
        cb.pAttachments = &blend;

        VkPushConstantRange push{};
        push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        push.offset = 0;
        push.size = sizeof(PushConstants);

        VkPipelineLayoutCreateInfo layout{};
        layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &texture_set_layout_;
        layout.pushConstantRangeCount = 1;
        layout.pPushConstantRanges = &push;
        vk_check(vkCreatePipelineLayout(device_, &layout, nullptr, &pipeline_layout_),
                 "vkCreatePipelineLayout");

        VkGraphicsPipelineCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pi.stageCount = 2;
        pi.pStages = stages;
        pi.pVertexInputState = &vin;
        pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp;
        pi.pRasterizationState = &rast;
        pi.pMultisampleState = &ms;
        pi.pDepthStencilState = &depth;
        pi.pColorBlendState = &cb;
        pi.pDynamicState = &dyn;
        pi.layout = pipeline_layout_;
        pi.renderPass = render_pass_;
        pi.subpass = 0;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &pipeline_),
                 "vkCreateGraphicsPipelines");

        // Modules are baked into the pipeline; safe to release immediately.
        vkDestroyShaderModule(device_, frag, nullptr);
        vkDestroyShaderModule(device_, vert, nullptr);
    }

    // --- buffers / one-time transfers -------------------------------------

    void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                       VkMemoryPropertyFlags props, VkBuffer& buffer, VkDeviceMemory& memory)
    {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = size;
        bi.usage = usage;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vk_check(vkCreateBuffer(device_, &bi, nullptr, &buffer), "vkCreateBuffer");

        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(device_, buffer, &req);
        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = find_memory_type(req.memoryTypeBits, props);
        vk_check(vkAllocateMemory(device_, &alloc, nullptr, &memory), "vkAllocateMemory(buffer)");
        vk_check(vkBindBufferMemory(device_, buffer, memory, 0), "vkBindBufferMemory");
    }

    VkCommandBuffer begin_single_time_commands()
    {
        VkCommandBufferAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandPool = command_pool_;
        alloc.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        vk_check(vkAllocateCommandBuffers(device_, &alloc, &cmd),
                 "vkAllocateCommandBuffers(single)");

        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vk_check(vkBeginCommandBuffer(cmd, &begin), "vkBeginCommandBuffer(single)");
        return cmd;
    }

    void end_single_time_commands(VkCommandBuffer cmd)
    {
        vk_check(vkEndCommandBuffer(cmd), "vkEndCommandBuffer(single)");

        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        vk_check(vkQueueSubmit(graphics_queue_, 1, &submit, VK_NULL_HANDLE),
                 "vkQueueSubmit(single)");
        vk_check(vkQueueWaitIdle(graphics_queue_), "vkQueueWaitIdle(single)");
        vkFreeCommandBuffers(device_, command_pool_, 1, &cmd);
    }

    // Create a device-local buffer of `size` bytes filled from `src` via a
    // host-visible staging buffer + a one-time-submit copy on the graphics queue.
    void upload_via_staging(const void* src, VkDeviceSize size, VkBufferUsageFlags usage,
                            VkBuffer& out_buffer, VkDeviceMemory& out_memory)
    {
        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory staging_mem = VK_NULL_HANDLE;
        create_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      staging, staging_mem);

        void* mapped = nullptr;
        vk_check(vkMapMemory(device_, staging_mem, 0, size, 0, &mapped), "vkMapMemory(staging)");
        std::memcpy(mapped, src, static_cast<std::size_t>(size));
        vkUnmapMemory(device_, staging_mem);

        create_buffer(size, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, out_buffer, out_memory);

        VkCommandBuffer cmd = begin_single_time_commands();
        VkBufferCopy copy{};
        copy.size = size;
        vkCmdCopyBuffer(cmd, staging, out_buffer, 1, &copy);
        end_single_time_commands(cmd);

        vkDestroyBuffer(device_, staging, nullptr);
        vkFreeMemory(device_, staging_mem, nullptr);
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

        // Depth resources go before the color views (both are extent-sized).
        if (depth_view_ != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device_, depth_view_, nullptr);
            depth_view_ = VK_NULL_HANDLE;
        }
        if (depth_image_ != VK_NULL_HANDLE)
        {
            vkDestroyImage(device_, depth_image_, nullptr);
            depth_image_ = VK_NULL_HANDLE;
        }
        if (depth_memory_ != VK_NULL_HANDLE)
        {
            vkFreeMemory(device_, depth_memory_, nullptr);
            depth_memory_ = VK_NULL_HANDLE;
        }

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
        create_depth_resources();
        create_framebuffers();
        // render pass and pipeline are format-stable, so they are not recreated
    }

    // --- members ----------------------------------------------------------

    GLFWwindow* window_ = nullptr;
    VkDescriptorPool imgui_pool_ = VK_NULL_HANDLE;
    bool             ui_ready_ = false;
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

    static constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchain_format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_ = {0, 0};
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;

    // Depth buffer — part of the recreatable swapchain group.
    VkImage depth_image_ = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory_ = VK_NULL_HANDLE;
    VkImageView depth_view_ = VK_NULL_HANDLE;

    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;

    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    // Descriptor infrastructure for sampled textures (set 0, binding 0).
    static constexpr uint32_t kMaxTextures = 64;
    VkDescriptorSetLayout texture_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;

    // A device texture: image + backing memory, view, sampler, and the
    // descriptor set that binds them for sampling.
    struct Texture
    {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE; // freed with the pool
    };
    std::vector<Texture> textures_; // handle == index + 1; 0 is invalid

    // A device mesh: vertex + index buffers (device-local) and the index count.
    struct MeshBuffers
    {
        VkBuffer vbuf = VK_NULL_HANDLE;
        VkDeviceMemory vmem = VK_NULL_HANDLE;
        VkBuffer ibuf = VK_NULL_HANDLE;
        VkDeviceMemory imem = VK_NULL_HANDLE;
        uint32_t index_count = 0;
    };
    std::vector<MeshBuffers> meshes_; // handle == index + 1; 0 is invalid

    Color clear_color_{0.0f, 0.0f, 0.0f, 1.0f}; // stored by clear(), used by begin_frame

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
