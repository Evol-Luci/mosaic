#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include "engine/render/mip_levels.hpp"
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
#include <unordered_map>

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
    float model[16];         // object -> world
    float normal_matrix[16]; // transpose(inverse(model)); only the upper 3x3 is used
    float base_color[4];     // xyz
    // Push-constant blocks are std430: these two floats pack tight after the vec4.
    float metallic;
    float roughness;
};
static_assert(sizeof(PushConstants) == 216, "push constant layout must match the shader");

// std140 layout, matching the FrameLighting uniform block in cube.vert/cube.frag.
struct FrameLightingUbo
{
    float light_direction[4]; // xyz, w unused
    float light_color[4];     // xyz, w unused
    float point_position[4];  // xyz, w = range
    float point_color[4];     // xyz, w unused
    float spot_position[4];   // xyz, w = range
    float spot_direction[4];  // xyz, w = cos(outer_angle)
    float spot_color[4];      // xyz, w = cos(inner_angle)
    float camera_position[4]; // xyz world space, w unused
    float light_view_proj[16];
};
static_assert(sizeof(FrameLightingUbo) == 192, "UBO layout must match the shader block");

struct ShadowPushConstants
{
    float light_mvp[16];
};
static_assert(sizeof(ShadowPushConstants) == 64, "shadow push constant layout must match the shader");

struct ParticlePush
{
    float view_proj[16];
    float camera_right[4]; // vec3 + pad
    float camera_up[4];    // vec3 + pad
};
static_assert(sizeof(ParticlePush) == 96, "particle push constant layout must match the shader");

struct RibbonPush
{
    float view_proj[16];
};
static_assert(sizeof(RibbonPush) == 64, "ribbon push constant layout must match the shader");

struct WorldTextPush
{
    float view_proj[16];
};
static_assert(sizeof(WorldTextPush) == 64, "world text push constant layout must match the shader");

struct DecalPush
{
    float mvp[16];
    float color[4]; // rgb = tint, a = alpha
};
static_assert(sizeof(DecalPush) == 80, "decal push constant layout must match the shader");

struct SkyboxPush
{
    float inv_view_rotation[16]; // world-from-view, translation removed
    float tan_half_fov_y;
    float aspect;
};
static_assert(sizeof(SkyboxPush) == 72, "skybox push layout must match skybox.frag");

struct ScreenEffectPush
{
    float tint[4];   // rgb = tint, a = strength
    float params[4]; // x = vignette
};
static_assert(sizeof(ScreenEffectPush) == 32, "screen-effect push constant must match the shader");

struct UiPush
{
    float rect[4];  // x, y, w, h
    float color[4]; // rgb + alpha
};
static_assert(sizeof(UiPush) == 32, "ui push constant must match the shader");

struct UiImagePush
{
    float rect[4];
    float tint[4];
};
static_assert(sizeof(UiImagePush) == 32, "ui-image push constant must match the shader");

struct UiTextPush
{
    float rect[4];
    float uv[4];
    float color[4];
};
static_assert(sizeof(UiTextPush) == 48, "ui-text push constant must match the shader");

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
        create_offscreen_render_pass();
        create_framebuffers();
        create_descriptor_infrastructure();
        create_shadow_resources();
        create_shadow_descriptor();
        create_frame_lighting_resources();
        create_pipeline();
        create_shadow_pipeline();
        create_particle_pipeline();
        create_ribbon_pipeline();
        create_world_text_pipeline();
        create_decal_pipeline();
        create_skybox_pipeline();
        create_screen_effect_pipeline();
        create_ui_pipeline();
        create_ui_image_pipeline();
        create_ui_text_pipeline();
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

            // Offscreen viewport target — free while the ImGui-Vulkan backend is still
            // up (destroy_offscreen_target calls ImGui_ImplVulkan_RemoveTexture).
            destroy_offscreen_target();
            if (vp_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, vp_pipeline_, nullptr); }
            if (vp_sampler_ != VK_NULL_HANDLE) { vkDestroySampler(device_, vp_sampler_, nullptr); }
            if (vp_render_pass_ != VK_NULL_HANDLE)
            {
                vkDestroyRenderPass(device_, vp_render_pass_, nullptr);
            }

            if (shadow_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, shadow_pipeline_, nullptr); }
            if (shadow_pipeline_layout_ != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, shadow_pipeline_layout_, nullptr); }
            if (shadow_descriptor_pool_ != VK_NULL_HANDLE) { vkDestroyDescriptorPool(device_, shadow_descriptor_pool_, nullptr); }
            if (shadow_set_layout_ != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(device_, shadow_set_layout_, nullptr); }
            if (shadow_framebuffer_ != VK_NULL_HANDLE) { vkDestroyFramebuffer(device_, shadow_framebuffer_, nullptr); }
            if (shadow_render_pass_ != VK_NULL_HANDLE) { vkDestroyRenderPass(device_, shadow_render_pass_, nullptr); }
            if (shadow_sampler_ != VK_NULL_HANDLE) { vkDestroySampler(device_, shadow_sampler_, nullptr); }
            if (shadow_view_ != VK_NULL_HANDLE) { vkDestroyImageView(device_, shadow_view_, nullptr); }
            if (shadow_image_ != VK_NULL_HANDLE) { vkDestroyImage(device_, shadow_image_, nullptr); }
            if (shadow_memory_ != VK_NULL_HANDLE) { vkFreeMemory(device_, shadow_memory_, nullptr); }

            if (frame_ubo_pool_ != VK_NULL_HANDLE) { vkDestroyDescriptorPool(device_, frame_ubo_pool_, nullptr); }
            if (frame_ubo_set_layout_ != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(device_, frame_ubo_set_layout_, nullptr); }
            for (int i = 0; i < kFramesInFlight; ++i)
            {
                if (frame_ubo_memory_[i] != VK_NULL_HANDLE) { vkUnmapMemory(device_, frame_ubo_memory_[i]); }
                if (frame_ubo_[i] != VK_NULL_HANDLE) { vkDestroyBuffer(device_, frame_ubo_[i], nullptr); }
                if (frame_ubo_memory_[i] != VK_NULL_HANDLE) { vkFreeMemory(device_, frame_ubo_memory_[i], nullptr); }
            }

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
            if (material_set_layout_ != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(device_, material_set_layout_, nullptr);
                material_set_layout_ = VK_NULL_HANDLE;
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

            if (vp_particle_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, vp_particle_pipeline_, nullptr); }
            if (particle_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, particle_pipeline_, nullptr); }
            if (particle_pipeline_layout_ != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, particle_pipeline_layout_, nullptr); }
            if (vp_ribbon_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, vp_ribbon_pipeline_, nullptr); }
            if (ribbon_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, ribbon_pipeline_, nullptr); }
            if (ribbon_pipeline_layout_ != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, ribbon_pipeline_layout_, nullptr); }
            if (vp_world_text_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, vp_world_text_pipeline_, nullptr); }
            if (world_text_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, world_text_pipeline_, nullptr); }
            if (world_text_pipeline_layout_ != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, world_text_pipeline_layout_, nullptr); }
            if (vp_decal_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, vp_decal_pipeline_, nullptr); }
            if (decal_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, decal_pipeline_, nullptr); }
            if (decal_pipeline_layout_ != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, decal_pipeline_layout_, nullptr); }
            if (vp_screen_effect_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, vp_screen_effect_pipeline_, nullptr); }
            if (screen_effect_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, screen_effect_pipeline_, nullptr); }
            if (skybox_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, skybox_pipeline_, nullptr); skybox_pipeline_ = VK_NULL_HANDLE; }
            if (vp_skybox_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, vp_skybox_pipeline_, nullptr); vp_skybox_pipeline_ = VK_NULL_HANDLE; }
            if (skybox_pipeline_layout_ != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, skybox_pipeline_layout_, nullptr); skybox_pipeline_layout_ = VK_NULL_HANDLE; }
            if (screen_effect_pipeline_layout_ != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, screen_effect_pipeline_layout_, nullptr); }
            if (vp_ui_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, vp_ui_pipeline_, nullptr); }
            if (ui_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, ui_pipeline_, nullptr); }
            if (ui_pipeline_layout_ != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, ui_pipeline_layout_, nullptr); }
            if (vp_ui_image_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, vp_ui_image_pipeline_, nullptr); }
            if (ui_image_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, ui_image_pipeline_, nullptr); }
            if (ui_image_pipeline_layout_ != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, ui_image_pipeline_layout_, nullptr); }
            if (vp_ui_text_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, vp_ui_text_pipeline_, nullptr); }
            if (ui_text_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, ui_text_pipeline_, nullptr); }
            if (ui_text_pipeline_layout_ != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, ui_text_pipeline_layout_, nullptr); }
            for (auto& ib : particle_instances_)
            {
                if (ib.buf != VK_NULL_HANDLE) { vkDestroyBuffer(device_, ib.buf, nullptr); }
                if (ib.mem != VK_NULL_HANDLE) { vkFreeMemory(device_, ib.mem, nullptr); }
            }
            for (auto& vb : ribbon_vertices_)
            {
                if (vb.buf != VK_NULL_HANDLE) { vkDestroyBuffer(device_, vb.buf, nullptr); }
                if (vb.mem != VK_NULL_HANDLE) { vkFreeMemory(device_, vb.mem, nullptr); }
            }
            for (auto& vb : world_text_vertices_)
            {
                if (vb.buf != VK_NULL_HANDLE) { vkDestroyBuffer(device_, vb.buf, nullptr); }
                if (vb.mem != VK_NULL_HANDLE) { vkFreeMemory(device_, vb.mem, nullptr); }
            }

            for (VkSemaphore s : render_finished_)
            {
                if (s != VK_NULL_HANDLE)
                {
                    vkDestroySemaphore(device_, s, nullptr);
                }
            }
            render_finished_.clear();

            for (int i = 0; i < kFramesInFlight; ++i)
            {
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
    // No default argument here: defaults are not inherited, so restating one would mean
    // the default in force depends on the static type of the reference held. The base
    // declaration in render_device.hpp is the contract.
    create_texture(const mosaic::engine::render::Image& image,
                   mosaic::engine::render::TextureColorSpace space) override
    {
        // The buffer must match the stride its own format implies. Nothing checked this
        // before the 8-byte Rgba16F stride existed, and a mismatch sizes the staging
        // buffer at half what the copy region reads -- an overrun rather than an error.
        if (image.pixels.size()
            != static_cast<std::size_t>(image.width) * image.height
                   * mosaic::engine::render::bytes_per_pixel(image.format))
        {
            mosaic::foundation::log::error(
                "render", "image pixel buffer does not match its {}x{} size and format",
                image.width, image.height);
            return 0u;
        }
        if (image.pixels.empty() || image.width == 0 || image.height == 0)
        {
            // A zero dimension is its own case, not just an empty-pixels one: it would
            // reach mipLevels = 0 (VUID-VkImageCreateInfo-mipLevels-00947) and a final
            // barrier at baseMipLevel = 0u - 1. Turning it into a 0 handle here is what
            // makes mip_levels_for's documented contract true. Review 2026-10-05.
            return 0u;
        }
        // The pool budgets kMaxTextures single-binding sets. Without this bound the
        // 65th texture quietly succeeded by eating the MATERIAL-set budget, and the
        // failure surfaced much later as a material-set allocation throwing
        // VK_ERROR_OUT_OF_POOL_MEMORY from the middle of command-buffer recording --
        // blaming material pairs for a texture-count problem. Fail here, where the
        // caller can see what it did. Found in review 2026-10-04.
        if (textures_.size() >= kMaxTextures)
        {
            mosaic::foundation::log::error(
                "render", "texture cap reached ({}); raise kMaxTextures", kMaxTextures);
            return 0u;
        }

        // Computed ONCE: the image and its view must agree on the format, and two
        // independent ternaries is how they come to disagree. Linear is for data maps
        // (metallic/roughness); see TextureColorSpace for what sRGB does to them.
        // Rgba16F ignores the colour space on purpose: a float format has no sRGB
        // variant, so it is always linear and asking for Srgb with one is a category
        // error rather than a choice.
        const VkFormat tex_format =
            (image.format == mosaic::engine::render::ImageFormat::Rgba16F)
                ? VK_FORMAT_R16G16B16A16_SFLOAT
                : ((space == mosaic::engine::render::TextureColorSpace::Linear)
                       ? VK_FORMAT_R8G8B8A8_UNORM
                       : VK_FORMAT_R8G8B8A8_SRGB);

        // Mip levels, gated on the device actually supporting a linear-filter blit for
        // this format. Near-universal for R8G8B8A8, but it is a QUERY, not a guarantee:
        // blitting with VK_FILTER_LINEAR against a format lacking the feature is
        // undefined, not an error, so fall back to a single level rather than produce
        // garbage on some driver.
        VkFormatProperties fmt_props{};
        vkGetPhysicalDeviceFormatProperties(physical_, tex_format, &fmt_props);
        // All THREE features the blit actually requires, not just the filter one:
        // BLIT_SRC on the source (VUID-vkCmdBlitImage-srcImage-01999), BLIT_DST on the
        // destination (-dstImage-02000), and SAMPLED_IMAGE_FILTER_LINEAR because the
        // filter is VK_FILTER_LINEAR (-filter-02001). Here source and destination are
        // the same image, so one query covers both. Checking only the filter bit left
        // the gate unable to establish that the blit was legal at all -- and a driver
        // advertising the filter but not BLIT_DST for the _SRGB variant, whose
        // mandatory-feature guarantee is thinner than _UNORM's, would have taken the
        // multi-level path and made every blit an invalid call. Review 2026-10-05.
        constexpr VkFormatFeatureFlags kBlitFeatures =
            VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT
            | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        const bool can_blit_linear =
            (fmt_props.optimalTilingFeatures & kBlitFeatures) == kBlitFeatures;
        const std::uint32_t levels =
            can_blit_linear
                ? mosaic::engine::render::mip_levels_for(image.width, image.height)
                : 1u;

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
        img.mipLevels = levels;
        img.arrayLayers = 1;
        img.format = tex_format;
        img.tiling = VK_IMAGE_TILING_OPTIMAL;
        img.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        // TRANSFER_SRC as well as DST: each level is the blit SOURCE for the next one.
        img.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                    | VK_IMAGE_USAGE_SAMPLED_BIT;
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
        to_transfer.subresourceRange.levelCount = levels;
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

        // Generate levels 1..n-1 by halving the previous level. Each iteration moves the
        // SOURCE level to TRANSFER_SRC, blits it down, and leaves the destination in
        // TRANSFER_DST for the next pass. Dimensions floor at 1 independently, which is
        // what makes a non-square texture's chain correct.
        for (std::uint32_t level = 1; level < levels; ++level)
        {
            // Extents come from mip_extent_for, which is unit-tested against Vulkan's
            // own max(1, base >> level) for every awkward shape. Restating the halving
            // here is what let a dropped floor-at-1 guard produce a zero-extent blit on
            // a non-square texture with every test green. Review 2026-10-05.
            const auto src_w =
                static_cast<std::int32_t>(mosaic::engine::render::mip_extent_for(image.width, level - 1));
            const auto src_h =
                static_cast<std::int32_t>(mosaic::engine::render::mip_extent_for(image.height, level - 1));
            const auto dst_w =
                static_cast<std::int32_t>(mosaic::engine::render::mip_extent_for(image.width, level));
            const auto dst_h =
                static_cast<std::int32_t>(mosaic::engine::render::mip_extent_for(image.height, level));

            VkImageMemoryBarrier src_barrier{};
            src_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            src_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            src_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            src_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            src_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            src_barrier.image = tex.image;
            src_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            src_barrier.subresourceRange.baseMipLevel = level - 1;
            src_barrier.subresourceRange.levelCount = 1;
            src_barrier.subresourceRange.baseArrayLayer = 0;
            src_barrier.subresourceRange.layerCount = 1;
            src_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            src_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                                 1, &src_barrier);

            VkImageBlit blit{};
            blit.srcOffsets[0] = {0, 0, 0};
            blit.srcOffsets[1] = {src_w, src_h, 1};
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = level - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount = 1;
            blit.dstOffsets[0] = {0, 0, 0};
            blit.dstOffsets[1] = {dst_w, dst_h, 1};
            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = level;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount = 1;
            vkCmdBlitImage(cmd, tex.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, tex.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

            // That source level is finished; move it to its final layout now, so the
            // sweep below only has to cover the last level.
            VkImageMemoryBarrier src_done = src_barrier;
            src_done.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            src_done.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            src_done.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            src_done.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                                 nullptr, 1, &src_done);

        }

        // Only the LAST level is still in TRANSFER_DST: every earlier one was moved to
        // SHADER_READ_ONLY as the loop finished with it.
        VkImageMemoryBarrier to_read{};
        to_read.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        to_read.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_read.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        to_read.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_read.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_read.image = tex.image;
        to_read.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        to_read.subresourceRange.baseMipLevel = levels - 1;
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
        view.format = tex_format;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.baseMipLevel = 0;
        view.subresourceRange.levelCount = levels;
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
        // Without this the chain exists but is never sampled: maxLod 0 pins every fetch
        // to the base level, which is exactly the aliasing this change removes.
        //
        // VK_LOD_CLAMP_NONE rather than a computed number. The highest valid index is
        // levels - 1, so `levels` was one too many -- harmless, because the selected
        // level is clamped again to the view's levelCount - 1, but it stated an
        // arithmetic relationship it did not mean. This says "all of them", which is
        // what is actually intended, and cannot drift from the level count.
        // Review 2026-10-05.
        sampler.maxLod = VK_LOD_CLAMP_NONE;
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

    // Six square faces as one cubemap image. Shares textures_ and the handle space with
    // create_texture: above the device a cubemap IS a texture, and only the view type
    // differs. One mip level -- the prefiltered specular cube that IBL needs is built by
    // convolution, not by blitting this one down, and a skybox barely minifies.
    mosaic::engine::render::TextureHandle
    create_cubemap(const std::array<mosaic::engine::render::Image, 6>& faces) override
    {
        const auto& f0 = faces[0];
        if (f0.pixels.empty() || f0.width == 0 || f0.width != f0.height) { return 0u; }
        for (const auto& f : faces)
        {
            // A mismatched face would upload as garbage at a plausible-looking size
            // rather than failing, so refuse rather than guess.
            if (f.width != f0.width || f.height != f0.height || f.format != f0.format
                || f.pixels.size() != f0.pixels.size()
                || f.pixels.size() != static_cast<std::size_t>(f.width) * f.height
                                          * mosaic::engine::render::bytes_per_pixel(f.format))
            {
                mosaic::foundation::log::error(
                    "render", "cubemap faces disagree in size or format; not uploading");
                return 0u;
            }
        }
        if (textures_.size() >= kMaxTextures)
        {
            mosaic::foundation::log::error(
                "render", "texture cap reached ({}); raise kMaxTextures", kMaxTextures);
            return 0u;
        }

        const VkFormat cube_format =
            (f0.format == mosaic::engine::render::ImageFormat::Rgba16F)
                ? VK_FORMAT_R16G16B16A16_SFLOAT
                : VK_FORMAT_R8G8B8A8_SRGB;

        Texture tex{};
        tex.is_cube = true;
        const VkDeviceSize face_size = static_cast<VkDeviceSize>(f0.pixels.size());
        const VkDeviceSize total = face_size * 6u;

        // All six faces in ONE staging buffer, copied by six regions -- one buffer and
        // one submit rather than six of each.
        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory staging_mem = VK_NULL_HANDLE;
        create_buffer(total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      staging, staging_mem);
        void* mapped = nullptr;
        vk_check(vkMapMemory(device_, staging_mem, 0, total, 0, &mapped), "vkMapMemory(cubemap)");
        for (std::size_t i = 0; i < 6; ++i)
        {
            std::memcpy(static_cast<std::uint8_t*>(mapped) + i * f0.pixels.size(),
                        faces[i].pixels.data(), faces[i].pixels.size());
        }
        vkUnmapMemory(device_, staging_mem);

        VkImageCreateInfo cube{};
        cube.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        // CUBE_COMPATIBLE is what makes a 6-layer 2D array legal to view as a cube.
        cube.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        cube.imageType = VK_IMAGE_TYPE_2D;
        cube.extent = {f0.width, f0.height, 1};
        cube.mipLevels = 1;
        cube.arrayLayers = 6;
        cube.format = cube_format;
        cube.tiling = VK_IMAGE_TILING_OPTIMAL;
        cube.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        cube.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        cube.samples = VK_SAMPLE_COUNT_1_BIT;
        cube.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vk_check(vkCreateImage(device_, &cube, nullptr, &tex.image), "vkCreateImage(cubemap)");

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(device_, tex.image, &req);
        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex =
            find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vk_check(vkAllocateMemory(device_, &alloc, nullptr, &tex.memory),
                 "vkAllocateMemory(cubemap)");
        vk_check(vkBindImageMemory(device_, tex.image, tex.memory, 0),
                 "vkBindImageMemory(cubemap)");

        VkCommandBuffer cmd = begin_single_time_commands();

        // All six layers transitioned TOGETHER. Missing a layer leaves it in the wrong
        // layout when sampled -- undefined contents plus a validation error.
        VkImageMemoryBarrier cube_barrier{};
        cube_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        cube_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        cube_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        cube_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        cube_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        cube_barrier.image = tex.image;
        cube_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        cube_barrier.subresourceRange.baseMipLevel = 0;
        cube_barrier.subresourceRange.levelCount = 1;
        cube_barrier.subresourceRange.baseArrayLayer = 0;
        cube_barrier.subresourceRange.layerCount = 6;
        cube_barrier.srcAccessMask = 0;
        cube_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &cube_barrier);

        std::array<VkBufferImageCopy, 6> copies{};
        for (std::uint32_t i = 0; i < 6; ++i)
        {
            copies[i].bufferOffset = static_cast<VkDeviceSize>(i) * face_size;
            copies[i].bufferRowLength = 0;
            copies[i].bufferImageHeight = 0;
            copies[i].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copies[i].imageSubresource.mipLevel = 0;
            copies[i].imageSubresource.baseArrayLayer = i;
            copies[i].imageSubresource.layerCount = 1;
            copies[i].imageOffset = {0, 0, 0};
            copies[i].imageExtent = {f0.width, f0.height, 1};
        }
        vkCmdCopyBufferToImage(cmd, staging, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<std::uint32_t>(copies.size()), copies.data());

        cube_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        cube_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        cube_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        cube_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
                             1, &cube_barrier);
        end_single_time_commands(cmd);

        vkDestroyBuffer(device_, staging, nullptr);
        vkFreeMemory(device_, staging_mem, nullptr);

        VkImageViewCreateInfo cube_view{};
        cube_view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        cube_view.image = tex.image;
        cube_view.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        cube_view.format = cube_format;
        cube_view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        cube_view.subresourceRange.baseMipLevel = 0;
        cube_view.subresourceRange.levelCount = 1;
        cube_view.subresourceRange.baseArrayLayer = 0;
        cube_view.subresourceRange.layerCount = 6;
        vk_check(vkCreateImageView(device_, &cube_view, nullptr, &tex.view),
                 "vkCreateImageView(cubemap)");

        VkSamplerCreateInfo cube_sampler{};
        cube_sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        cube_sampler.magFilter = VK_FILTER_LINEAR;
        cube_sampler.minFilter = VK_FILTER_LINEAR;
        // Clamp on every axis, W included: cube sampling is seamless in Vulkan, but a
        // repeating address mode still shows at the edges when filtering crosses a face.
        cube_sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        cube_sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        cube_sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        cube_sampler.anisotropyEnable = VK_FALSE;
        cube_sampler.maxAnisotropy = 1.0f;
        cube_sampler.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        cube_sampler.unnormalizedCoordinates = VK_FALSE;
        cube_sampler.compareEnable = VK_FALSE;
        cube_sampler.compareOp = VK_COMPARE_OP_ALWAYS;
        cube_sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        cube_sampler.mipLodBias = 0.0f;
        cube_sampler.minLod = 0.0f;
        cube_sampler.maxLod = 0.0f;
        vk_check(vkCreateSampler(device_, &cube_sampler, nullptr, &tex.sampler),
                 "vkCreateSampler(cubemap)");

        // A descriptor set of its own, exactly as create_texture allocates one: the
        // skybox pass binds set 0 = this cubemap, and without this there would be
        // nothing to bind.
        VkDescriptorSetAllocateInfo cube_set_alloc{};
        cube_set_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        cube_set_alloc.descriptorPool = descriptor_pool_;
        cube_set_alloc.descriptorSetCount = 1;
        cube_set_alloc.pSetLayouts = &texture_set_layout_;
        vk_check(vkAllocateDescriptorSets(device_, &cube_set_alloc, &tex.set),
                 "vkAllocateDescriptorSets(cubemap)");

        VkDescriptorImageInfo cube_info{};
        cube_info.sampler = tex.sampler;
        cube_info.imageView = tex.view;
        cube_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet cube_write{};
        cube_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        cube_write.dstSet = tex.set;
        cube_write.dstBinding = 0;
        cube_write.dstArrayElement = 0;
        cube_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        cube_write.descriptorCount = 1;
        cube_write.pImageInfo = &cube_info;
        vkUpdateDescriptorSets(device_, 1, &cube_write, 0, nullptr);

        textures_.push_back(tex);
        return static_cast<mosaic::engine::render::TextureHandle>(textures_.size());
    }

    // One descriptor set per (base_color, metallic_roughness) texture pair, created on
    // first use and cached for the device's lifetime. Set ownership moved off the
    // texture table in PBR slice 3: a lit material needs two images visible at once,
    // and binding a single set per draw is what keeps the per-draw cost unchanged.
    // The per-texture sets above still exist and still serve the UI, text and decal
    // pipelines, which sample one image.
    VkDescriptorSet material_set(std::uint32_t base_color, std::uint32_t mr)
    {
        const std::uint64_t key =
            (static_cast<std::uint64_t>(base_color) << 32) | static_cast<std::uint64_t>(mr);
        if (auto it = material_sets_.find(key); it != material_sets_.end())
        {
            return it->second;
        }
        if (material_sets_.size() >= kMaxMaterialSets)
        {
            // Skipping the draw is deliberate: reusing another pair's set would draw the
            // wrong texture on an arbitrary object and read as an asset mistake. But the
            // object vanishing is only diagnosable if the log stays readable, and this
            // runs per draw per frame -- 60xN lines a second, burying whatever else is
            // wrong. Latch it. Found in review 2026-10-04.
            if (!material_cap_reported_)
            {
                material_cap_reported_ = true;
                mosaic::foundation::log::error(
                    "render",
                    "material-set cap reached ({} distinct texture pairs); further lit draws "
                    "with new pairs will be skipped. Raise kMaxMaterialSets.",
                    kMaxMaterialSets);
            }
            return VK_NULL_HANDLE;
        }

        VkDescriptorSetAllocateInfo set_alloc{};
        set_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        set_alloc.descriptorPool = descriptor_pool_;
        set_alloc.descriptorSetCount = 1;
        set_alloc.pSetLayouts = &material_set_layout_;
        VkDescriptorSet set = VK_NULL_HANDLE;
        vk_check(vkAllocateDescriptorSets(device_, &set_alloc, &set),
                 "vkAllocateDescriptorSets(material)");

        VkDescriptorImageInfo infos[2]{};
        infos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        infos[0].imageView = textures_[base_color].view;
        infos[0].sampler = textures_[base_color].sampler;
        infos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        infos[1].imageView = textures_[mr].view;
        infos[1].sampler = textures_[mr].sampler;

        VkWriteDescriptorSet writes[2]{};
        for (int i = 0; i < 2; ++i)
        {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set;
            writes[i].dstBinding = static_cast<std::uint32_t>(i);
            writes[i].dstArrayElement = 0;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].descriptorCount = 1;
            writes[i].pImageInfo = &infos[i];
        }
        vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);
        material_sets_.emplace(key, set);
        return set;
    }


    void set_frame_lighting(const mosaic::engine::render::FrameLighting& fl) override
    {
        FrameLightingUbo ubo{};
        ubo.light_direction[0] = fl.light_direction.x;
        ubo.light_direction[1] = fl.light_direction.y;
        ubo.light_direction[2] = fl.light_direction.z;
        ubo.light_color[0] = fl.light_color.x;
        ubo.light_color[1] = fl.light_color.y;
        ubo.light_color[2] = fl.light_color.z;
        ubo.point_position[0] = fl.point_position.x;
        ubo.point_position[1] = fl.point_position.y;
        ubo.point_position[2] = fl.point_position.z;
        ubo.point_position[3] = fl.point_range;
        ubo.point_color[0] = fl.point_color.x;
        ubo.point_color[1] = fl.point_color.y;
        ubo.point_color[2] = fl.point_color.z;
        ubo.spot_position[0] = fl.spot_position.x;
        ubo.spot_position[1] = fl.spot_position.y;
        ubo.spot_position[2] = fl.spot_position.z;
        ubo.spot_position[3] = fl.spot_range;
        ubo.spot_direction[0] = fl.spot_direction.x;
        ubo.spot_direction[1] = fl.spot_direction.y;
        ubo.spot_direction[2] = fl.spot_direction.z;
        ubo.spot_direction[3] = fl.spot_cos_outer;
        ubo.spot_color[0] = fl.spot_color.x;
        ubo.spot_color[1] = fl.spot_color.y;
        ubo.spot_color[2] = fl.spot_color.z;
        ubo.camera_position[0] = fl.camera_position.x;
        ubo.camera_position[1] = fl.camera_position.y;
        ubo.camera_position[2] = fl.camera_position.z;
        ubo.spot_color[3] = fl.spot_cos_inner;
        std::memcpy(ubo.light_view_proj, fl.light_view_proj.m.data(), sizeof(ubo.light_view_proj));

        // frame_ is the current frame-in-flight index (advanced at the end of
        // end_frame), so this always targets the buffer this frame will read.
        std::memcpy(frame_ubo_mapped_[frame_], &ubo, sizeof(ubo));
    }

    void begin_shadow_pass() override
    {
        VkClearValue clear{};
        clear.depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = shadow_render_pass_;
        rp.framebuffer = shadow_framebuffer_;
        rp.renderArea.extent = {kShadowMapSize, kShadowMapSize};
        rp.clearValueCount = 1;
        rp.pClearValues = &clear;
        vkCmdBeginRenderPass(cmd_[frame_], &rp, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_pipeline_);
    }

    void draw_shadow(MeshHandle mesh, const Mat4& light_mvp) override
    {
        if (mesh == 0 || mesh > meshes_.size()) { return; }
        const MeshBuffers& mb = meshes_[mesh - 1];
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd_[frame_], 0, 1, &mb.vbuf, &offset);
        vkCmdBindIndexBuffer(cmd_[frame_], mb.ibuf, 0, VK_INDEX_TYPE_UINT32);

        ShadowPushConstants pc{};
        std::memcpy(pc.light_mvp, light_mvp.m.data(), sizeof(pc.light_mvp));
        vkCmdPushConstants(cmd_[frame_], shadow_pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(pc), &pc);
        vkCmdDrawIndexed(cmd_[frame_], mb.index_count, 1, 0, 0, 0);
    }

    void end_shadow_pass() override { vkCmdEndRenderPass(cmd_[frame_]); }

    void draw(MeshHandle mesh, mosaic::engine::render::TextureHandle texture,
              const mosaic::engine::render::DrawParams& params) override
    {
        // A viewport pass (if open) already bound its own pipeline/viewport; otherwise
        // the swapchain pass must be active before recording draws.
        if (!viewport_pass_open_)
        {
            ensure_swapchain_pass();
        }
        if (frame_skipped_ || mesh == 0 || mesh > meshes_.size())
        {
            return;
        }

        // Map the 1-based handle to an index; fall back to the first texture
        // (the default white, always created first) for 0 or out-of-range.
        const std::uint32_t tex_index =
            (texture == 0 || texture > textures_.size()) ? 0u : texture - 1u;
        // Same 1-based handle rule for the metallic/roughness map. RenderSystem already
        // substitutes a 1x1 linear white for a material with no map, so index 0 here is
        // a fallback for a corrupt handle rather than the untextured path.
        const std::uint32_t mr_index =
            (params.metallic_roughness == 0 || params.metallic_roughness > textures_.size())
                ? 0u
                : params.metallic_roughness - 1u;
        if (!textures_.empty())
        {
            const VkDescriptorSet mat_set = material_set(tex_index, mr_index);
            if (mat_set == VK_NULL_HANDLE) { return; }
            vkCmdBindDescriptorSets(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_layout_, 0, 1, &mat_set, 0, nullptr);
            vkCmdBindDescriptorSets(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_layout_, 1, 1, &shadow_descriptor_set_, 0, nullptr);
            vkCmdBindDescriptorSets(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_layout_, 2, 1, &frame_ubo_set_[frame_], 0, nullptr);
        }

        const MeshBuffers& mb = meshes_[mesh - 1];
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd_[frame_], 0, 1, &mb.vbuf, &offset);
        vkCmdBindIndexBuffer(cmd_[frame_], mb.ibuf, 0, VK_INDEX_TYPE_UINT32);

        PushConstants pc{};
        std::memcpy(pc.mvp, params.mvp.m.data(), sizeof(pc.mvp));
        std::memcpy(pc.model, params.model.m.data(), sizeof(pc.model));
        std::memcpy(pc.normal_matrix, params.normal_matrix.m.data(), sizeof(pc.normal_matrix));
        pc.base_color[0] = params.base_color.x;
        pc.base_color[1] = params.base_color.y;
        pc.base_color[2] = params.base_color.z;
        pc.metallic = params.metallic;
        pc.roughness = params.roughness;
        vkCmdPushConstants(cmd_[frame_], pipeline_layout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(pc), &pc);
        vkCmdDrawIndexed(cmd_[frame_], mb.index_count, 1, 0, 0, 0);
    }

    void draw_particles(const std::vector<mosaic::engine::render::ParticleInstance>& instances,
                        const mosaic::engine::render::ParticleDrawParams& params) override
    {
        if (!viewport_pass_open_)
        {
            ensure_swapchain_pass();
        }
        if (frame_skipped_ || instances.empty())
        {
            return;
        }

        // Grow this frame's instance buffer if needed, then upload.
        const VkDeviceSize bytes = instances.size() * sizeof(instances[0]);
        InstanceBuffer& ib = particle_instances_[frame_];
        if (ib.capacity < bytes)
        {
            if (ib.buf != VK_NULL_HANDLE) { vkDestroyBuffer(device_, ib.buf, nullptr); }
            if (ib.mem != VK_NULL_HANDLE) { vkFreeMemory(device_, ib.mem, nullptr); }
            create_buffer(bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          ib.buf, ib.mem);
            ib.capacity = bytes;
        }
        void* mapped = nullptr;
        vkMapMemory(device_, ib.mem, 0, bytes, 0, &mapped);
        std::memcpy(mapped, instances.data(), static_cast<size_t>(bytes));
        vkUnmapMemory(device_, ib.mem);

        vkCmdBindPipeline(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                          viewport_pass_open_ ? vp_particle_pipeline_ : particle_pipeline_);

        ParticlePush pc{};
        std::memcpy(pc.view_proj, params.view_proj.m.data(), sizeof(pc.view_proj));
        pc.camera_right[0] = params.camera_right.x;
        pc.camera_right[1] = params.camera_right.y;
        pc.camera_right[2] = params.camera_right.z;
        pc.camera_up[0] = params.camera_up.x;
        pc.camera_up[1] = params.camera_up.y;
        pc.camera_up[2] = params.camera_up.z;
        vkCmdPushConstants(cmd_[frame_], particle_pipeline_layout_,
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd_[frame_], 0, 1, &ib.buf, &offset);
        vkCmdDraw(cmd_[frame_], 6, static_cast<uint32_t>(instances.size()), 0, 0);
    }

    void draw_ribbons(const std::vector<mosaic::engine::render::RibbonVertex>& vertices,
                      const mosaic::engine::render::RibbonDrawParams& params) override
    {
        if (!viewport_pass_open_) { ensure_swapchain_pass(); }
        if (frame_skipped_ || vertices.empty()) { return; }

        const VkDeviceSize bytes = vertices.size() * sizeof(vertices[0]);
        InstanceBuffer& vb = ribbon_vertices_[frame_];
        if (vb.capacity < bytes)
        {
            if (vb.buf != VK_NULL_HANDLE) { vkDestroyBuffer(device_, vb.buf, nullptr); }
            if (vb.mem != VK_NULL_HANDLE) { vkFreeMemory(device_, vb.mem, nullptr); }
            create_buffer(bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          vb.buf, vb.mem);
            vb.capacity = bytes;
        }
        void* mapped = nullptr;
        vkMapMemory(device_, vb.mem, 0, bytes, 0, &mapped);
        std::memcpy(mapped, vertices.data(), static_cast<size_t>(bytes));
        vkUnmapMemory(device_, vb.mem);

        vkCmdBindPipeline(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                          viewport_pass_open_ ? vp_ribbon_pipeline_ : ribbon_pipeline_);

        RibbonPush pc{};
        std::memcpy(pc.view_proj, params.view_proj.m.data(), sizeof(pc.view_proj));
        vkCmdPushConstants(cmd_[frame_], ribbon_pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(pc), &pc);

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd_[frame_], 0, 1, &vb.buf, &offset);
        vkCmdDraw(cmd_[frame_], static_cast<uint32_t>(vertices.size()), 1, 0, 0);
    }

    void draw_world_text(const std::vector<mosaic::engine::render::WorldGlyphVertex>& verts,
                         const mosaic::foundation::math::Mat4& view_proj,
                         mosaic::engine::render::TextureHandle atlas) override
    {
        if (!viewport_pass_open_) { ensure_swapchain_pass(); }
        if (frame_skipped_ || verts.empty() || textures_.empty()) { return; }

        const VkDeviceSize bytes = verts.size() * sizeof(verts[0]);
        InstanceBuffer& vb = world_text_vertices_[frame_];
        if (vb.capacity < bytes)
        {
            if (vb.buf != VK_NULL_HANDLE) { vkDestroyBuffer(device_, vb.buf, nullptr); }
            if (vb.mem != VK_NULL_HANDLE) { vkFreeMemory(device_, vb.mem, nullptr); }
            create_buffer(bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          vb.buf, vb.mem);
            vb.capacity = bytes;
        }
        void* mapped = nullptr;
        vkMapMemory(device_, vb.mem, 0, bytes, 0, &mapped);
        std::memcpy(mapped, verts.data(), static_cast<size_t>(bytes));
        vkUnmapMemory(device_, vb.mem);

        const std::uint32_t tex_index = (atlas == 0 || atlas > textures_.size()) ? 0u : atlas - 1u;
        vkCmdBindPipeline(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                          viewport_pass_open_ ? vp_world_text_pipeline_ : world_text_pipeline_);
        vkCmdBindDescriptorSets(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                world_text_pipeline_layout_, 0, 1, &textures_[tex_index].set, 0, nullptr);
        WorldTextPush pc{};
        std::memcpy(pc.view_proj, view_proj.m.data(), sizeof(pc.view_proj));
        vkCmdPushConstants(cmd_[frame_], world_text_pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(pc), &pc);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd_[frame_], 0, 1, &vb.buf, &offset);
        vkCmdDraw(cmd_[frame_], static_cast<uint32_t>(verts.size()), 1, 0, 0);
    }

    void draw_decals(const std::vector<mosaic::engine::render::DecalInstance>& decals) override
    {
        if (!viewport_pass_open_) { ensure_swapchain_pass(); }
        if (frame_skipped_ || decals.empty() || textures_.empty()) { return; }

        vkCmdBindPipeline(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                          viewport_pass_open_ ? vp_decal_pipeline_ : decal_pipeline_);

        for (const auto& d : decals)
        {
            const std::uint32_t tex_index =
                (d.texture == 0 || d.texture > textures_.size()) ? 0u : d.texture - 1u;
            vkCmdBindDescriptorSets(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    decal_pipeline_layout_, 0, 1, &textures_[tex_index].set, 0,
                                    nullptr);
            DecalPush pc{};
            std::memcpy(pc.mvp, d.mvp.m.data(), sizeof(pc.mvp));
            pc.color[0] = d.color.x; pc.color[1] = d.color.y; pc.color[2] = d.color.z;
            pc.color[3] = d.alpha;
            vkCmdPushConstants(cmd_[frame_], decal_pipeline_layout_,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               sizeof(pc), &pc);
            vkCmdDraw(cmd_[frame_], 6, 1, 0, 0);
        }
    }

    void draw_skybox(mosaic::engine::render::TextureHandle cubemap,
                     const mosaic::engine::render::SkyboxParams& params) override
    {
        // The same prologue every other recording method here has, and the absence of
        // which is undefined behaviour rather than a visual bug: on a resize or minimise
        // begin_frame takes the OUT_OF_DATE path, sets frame_skipped_ and returns before
        // vkBeginCommandBuffer, so recording into the command buffer is illegal. And a
        // scene with an environment but no drawn meshes would reach here with no render
        // pass open at all. Found in review 2026-10-05.
        if (!viewport_pass_open_) { ensure_swapchain_pass(); }
        if (frame_skipped_) { return; }
        if (cubemap == 0 || cubemap > textures_.size()) { return; }
        const std::uint32_t index = cubemap - 1u;
        if (!textures_[index].is_cube)
        {
            // A 2D handle bound to skybox.frag's samplerCube. The plan named this
            // (Review Focus 4) and only a source-text contract guarded it; this is the
            // runtime half. Review 2026-10-05.
            mosaic::foundation::log::error(
                "render", "draw_skybox given a 2D texture handle; skipping the sky");
            return;
        }

        vkCmdBindPipeline(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                          viewport_pass_open_ ? vp_skybox_pipeline_ : skybox_pipeline_);
        vkCmdBindDescriptorSets(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                skybox_pipeline_layout_, 0, 1, &textures_[index].set, 0, nullptr);

        SkyboxPush push{};
        std::memcpy(push.inv_view_rotation, params.inverse_view_rotation.m.data(),
                    sizeof(push.inv_view_rotation));
        push.tan_half_fov_y = params.tan_half_fov_y;
        push.aspect = params.aspect;
        vkCmdPushConstants(cmd_[frame_], skybox_pipeline_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(push), &push);

        // Three vertices, no buffers: the vertex shader builds a fullscreen triangle
        // from gl_VertexIndex.
        vkCmdDraw(cmd_[frame_], 3, 1, 0, 0);
    }

    void draw_screen_effect(const mosaic::engine::render::ScreenEffectParams& params) override
    {
        if (!viewport_pass_open_) { ensure_swapchain_pass(); }
        if (frame_skipped_) { return; }

        vkCmdBindPipeline(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                          viewport_pass_open_ ? vp_screen_effect_pipeline_ : screen_effect_pipeline_);

        ScreenEffectPush pc{};
        pc.tint[0] = params.tint.x; pc.tint[1] = params.tint.y; pc.tint[2] = params.tint.z;
        pc.tint[3] = params.tint_strength;
        pc.params[0] = params.vignette;
        vkCmdPushConstants(cmd_[frame_], screen_effect_pipeline_layout_,
                           VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

        vkCmdDraw(cmd_[frame_], 3, 1, 0, 0);
    }

    void draw_ui(const std::vector<mosaic::engine::render::UiQuad>& quads) override
    {
        if (quads.empty()) { return; }
        if (!viewport_pass_open_) { ensure_swapchain_pass(); }
        if (frame_skipped_) { return; }

        VkPipeline pipe = viewport_pass_open_ ? vp_ui_pipeline_ : ui_pipeline_;
        vkCmdBindPipeline(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        for (const auto& q : quads)
        {
            UiPush pc{};
            pc.rect[0] = q.rect.x; pc.rect[1] = q.rect.y;
            pc.rect[2] = q.rect.w; pc.rect[3] = q.rect.h;
            pc.color[0] = q.color.x; pc.color[1] = q.color.y;
            pc.color[2] = q.color.z; pc.color[3] = q.alpha;
            vkCmdPushConstants(cmd_[frame_], ui_pipeline_layout_,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(UiPush), &pc);
            vkCmdDraw(cmd_[frame_], 6, 1, 0, 0); // 6 verts = one quad
        }
    }

    void draw_ui_image(const std::vector<mosaic::engine::render::UiImageQuad>& quads,
                       mosaic::engine::render::TextureHandle texture) override
    {
        if (quads.empty()) { return; }
        if (!viewport_pass_open_) { ensure_swapchain_pass(); }
        if (frame_skipped_ || textures_.empty()) { return; }

        const std::uint32_t tex_index =
            (texture == 0 || texture > textures_.size()) ? 0u : texture - 1u;
        VkPipeline pipe = viewport_pass_open_ ? vp_ui_image_pipeline_ : ui_image_pipeline_;
        vkCmdBindPipeline(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        vkCmdBindDescriptorSets(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                ui_image_pipeline_layout_, 0, 1, &textures_[tex_index].set, 0, nullptr);
        for (const auto& q : quads)
        {
            UiImagePush pc{};
            pc.rect[0] = q.rect.x; pc.rect[1] = q.rect.y; pc.rect[2] = q.rect.w; pc.rect[3] = q.rect.h;
            pc.tint[0] = q.tint.x; pc.tint[1] = q.tint.y; pc.tint[2] = q.tint.z; pc.tint[3] = q.alpha;
            vkCmdPushConstants(cmd_[frame_], ui_image_pipeline_layout_,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(UiImagePush), &pc);
            vkCmdDraw(cmd_[frame_], 6, 1, 0, 0);
        }
    }

    void draw_ui_text(const std::vector<mosaic::engine::render::UiGlyphQuad>& quads,
                      mosaic::engine::render::TextureHandle atlas) override
    {
        if (quads.empty()) { return; }
        if (!viewport_pass_open_) { ensure_swapchain_pass(); }
        if (frame_skipped_ || textures_.empty()) { return; }

        const std::uint32_t tex_index =
            (atlas == 0 || atlas > textures_.size()) ? 0u : atlas - 1u;
        VkPipeline pipe = viewport_pass_open_ ? vp_ui_text_pipeline_ : ui_text_pipeline_;
        vkCmdBindPipeline(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        vkCmdBindDescriptorSets(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                ui_text_pipeline_layout_, 0, 1, &textures_[tex_index].set, 0, nullptr);
        for (const auto& q : quads)
        {
            UiTextPush pc{};
            pc.rect[0] = q.rect.x; pc.rect[1] = q.rect.y; pc.rect[2] = q.rect.w; pc.rect[3] = q.rect.h;
            pc.uv[0] = q.u0; pc.uv[1] = q.v0; pc.uv[2] = q.u1; pc.uv[3] = q.v1;
            pc.color[0] = q.color.x; pc.color[1] = q.color.y; pc.color[2] = q.color.z; pc.color[3] = q.alpha;
            vkCmdPushConstants(cmd_[frame_], ui_text_pipeline_layout_,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(UiTextPush), &pc);
            vkCmdDraw(cmd_[frame_], 6, 1, 0, 0);
        }
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

        // The swapchain render pass is now begun lazily (ensure_swapchain_pass), so the
        // editor can record an offscreen viewport pass before it. Reset per-frame state.
        swapchain_pass_open_ = false;
        viewport_pass_open_  = false;
    }

    // Begins the swapchain render pass once per frame (idempotent). Sets the
    // negative-height (Y-flip) viewport/scissor and binds the scene pipeline. A
    // skipped frame (swapchain out-of-date) never opens a pass.
    void ensure_swapchain_pass()
    {
        if (swapchain_pass_open_ || frame_skipped_)
        {
            return;
        }

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
        swapchain_pass_open_ = true;
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

        ensure_swapchain_pass(); // empty scene still clears + transitions to PRESENT_SRC
        vkCmdEndRenderPass(cmd_[frame_]);
        swapchain_pass_open_ = false;

        vk_check(vkEndCommandBuffer(cmd_[frame_]), "vkEndCommandBuffer");

        VkSemaphore          wait_sems[]   = {image_available_[frame_]};
        VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        VkSemaphore          signal_sems[] = {render_finished_[image_index_]};

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

    // No default here -- a default on both the base and this override is a
    // well-known C++ footgun (virtual dispatch uses the static type's default,
    // not the dynamic one), so the base class alone declares one.
    void init_ui(const std::string& ini_path, mosaic::engine::render::UiTheme theme) override
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
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        if (!ini_path.empty())
        {
            // io.IniFilename is a raw pointer ImGui holds onto for the context's
            // whole lifetime -- imgui_ini_path_ (a member, not a local) is what
            // keeps it alive.
            imgui_ini_path_ = ini_path;
            io.IniFilename = imgui_ini_path_.c_str();
        }
        apply_ui_theme(theme);

        ImGui_ImplGlfw_InitForVulkan(window_, true);

        ImGui_ImplVulkan_InitInfo info{};
        info.Instance = instance_;
        info.PhysicalDevice = physical_;
        info.Device = device_;
        info.QueueFamily = graphics_family_;
        info.Queue = graphics_queue_;
        info.DescriptorPool = imgui_pool_;
        info.RenderPass = render_pass_;
        info.MinImageCount = static_cast<uint32_t>(images_.size());
        info.ImageCount = static_cast<uint32_t>(images_.size());
        info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        ImGui_ImplVulkan_Init(&info);

        ui_ready_ = true;
    }

    void set_ui_theme(mosaic::engine::render::UiTheme theme) override
    {
        if (!ui_ready_) { return; }
        apply_ui_theme(theme);
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
            ensure_swapchain_pass(); // ImGui always records into an open swapchain pass
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd_[frame_]);
        }
    }

    void set_viewport_size(std::uint32_t width, std::uint32_t height) override
    {
        if (width == 0 || height == 0)
        {
            return;
        }
        if (vp_framebuffer_ != VK_NULL_HANDLE && width == vp_extent_.width &&
            height == vp_extent_.height)
        {
            return; // unchanged
        }
        vkDeviceWaitIdle(device_); // resizes are transient (panel drag); idle is fine for v0.1
        destroy_offscreen_target();
        create_offscreen_target(width, height);
    }

    void begin_viewport_pass(const Color& clear) override
    {
        if (frame_skipped_ || vp_framebuffer_ == VK_NULL_HANDLE)
        {
            return;
        }

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = static_cast<float>(vp_extent_.height);
        viewport.width = static_cast<float>(vp_extent_.width);
        viewport.height = -static_cast<float>(vp_extent_.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd_[frame_], 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = vp_extent_;
        vkCmdSetScissor(cmd_[frame_], 0, 1, &scissor);

        std::array<VkClearValue, 2> clears{};
        clears[0].color = {{clear.r, clear.g, clear.b, clear.a}};
        clears[1].depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = vp_render_pass_;
        rp.framebuffer = vp_framebuffer_;
        rp.renderArea.offset = {0, 0};
        rp.renderArea.extent = vp_extent_;
        rp.clearValueCount = static_cast<uint32_t>(clears.size());
        rp.pClearValues = clears.data();
        vkCmdBeginRenderPass(cmd_[frame_], &rp, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, vp_pipeline_);
        viewport_pass_open_ = true;
    }

    void end_viewport_pass() override
    {
        if (!viewport_pass_open_)
        {
            return;
        }
        vkCmdEndRenderPass(cmd_[frame_]);
        viewport_pass_open_ = false;
    }

    mosaic::engine::render::ViewportTextureId viewport_texture() const override
    {
        return reinterpret_cast<mosaic::engine::render::ViewportTextureId>(vp_imgui_tex_);
    }

private:
    // Applies one of ImGui's own built-in color schemes -- shared by init_ui()'s
    // one-time startup application and set_ui_theme()'s live runtime switch, so
    // the two can never drift into applying the mapping differently.
    static void apply_ui_theme(mosaic::engine::render::UiTheme theme)
    {
        switch (theme)
        {
        case mosaic::engine::render::UiTheme::Light: ImGui::StyleColorsLight(); break;
        case mosaic::engine::render::UiTheme::Classic: ImGui::StyleColorsClassic(); break;
        case mosaic::engine::render::UiTheme::Dark:
        default: ImGui::StyleColorsDark(); break;
        }
    }

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

            // 256-byte push constants (mvp + lighting + shadow light-space matrix +
            // spot light) exceed Vulkan's guaranteed 128-byte minimum and sit exactly
            // at the documented desktop-GPU ceiling (256) -- zero margin left for
            // anything added after the spot-light slice without revisiting this.
            // Fail loud rather than let validation catch it downstream.
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

    // Fixed-resolution, created once (never recreated on window resize — shadow
    // resolution is independent of window/viewport size in v1).
    void create_shadow_resources()
    {
        VkImageCreateInfo img{};
        img.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        img.imageType = VK_IMAGE_TYPE_2D;
        img.extent.width = kShadowMapSize;
        img.extent.height = kShadowMapSize;
        img.extent.depth = 1;
        img.mipLevels = 1;
        img.arrayLayers = 1;
        img.format = kDepthFormat;
        img.tiling = VK_IMAGE_TILING_OPTIMAL;
        img.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        img.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        img.samples = VK_SAMPLE_COUNT_1_BIT;
        img.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vk_check(vkCreateImage(device_, &img, nullptr, &shadow_image_), "vkCreateImage(shadow)");

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(device_, shadow_image_, &req);
        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex =
            find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vk_check(vkAllocateMemory(device_, &alloc, nullptr, &shadow_memory_),
                 "vkAllocateMemory(shadow)");
        vk_check(vkBindImageMemory(device_, shadow_image_, shadow_memory_, 0),
                 "vkBindImageMemory(shadow)");

        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = shadow_image_;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = kDepthFormat;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        view.subresourceRange.baseMipLevel = 0;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.baseArrayLayer = 0;
        view.subresourceRange.layerCount = 1;
        vk_check(vkCreateImageView(device_, &view, nullptr, &shadow_view_),
                 "vkCreateImageView(shadow)");

        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = VK_FILTER_NEAREST; // hard shadows, v1 -- no PCF, so no filtering benefit
        si.minFilter = VK_FILTER_NEAREST;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE; // max depth outside the frustum -> never occluding
        si.maxLod = 1.0f;
        vk_check(vkCreateSampler(device_, &si, nullptr, &shadow_sampler_),
                 "vkCreateSampler(shadow)");

        VkAttachmentDescription depth{};
        depth.format = kDepthFormat;
        depth.samples = VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference depth_ref{};
        depth_ref.attachment = 0;
        depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.pDepthStencilAttachment = &depth_ref;

        std::array<VkSubpassDependency, 2> deps{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass = 0;
        deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp.attachmentCount = 1;
        rp.pAttachments = &depth;
        rp.subpassCount = 1;
        rp.pSubpasses = &subpass;
        rp.dependencyCount = static_cast<uint32_t>(deps.size());
        rp.pDependencies = deps.data();
        vk_check(vkCreateRenderPass(device_, &rp, nullptr, &shadow_render_pass_),
                 "vkCreateRenderPass(shadow)");

        VkFramebufferCreateInfo fb{};
        fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass = shadow_render_pass_;
        fb.attachmentCount = 1;
        fb.pAttachments = &shadow_view_;
        fb.width = kShadowMapSize;
        fb.height = kShadowMapSize;
        fb.layers = 1;
        vk_check(vkCreateFramebuffer(device_, &fb, nullptr, &shadow_framebuffer_),
                 "vkCreateFramebuffer(shadow)");
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

    // --- offscreen viewport target (37d) ----------------------------------

    void bind_image_memory(VkImage image, VkDeviceMemory& mem, const char* what)
    {
        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(device_, image, &req);
        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex =
            find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vk_check(vkAllocateMemory(device_, &alloc, nullptr, &mem),
                 (std::string("vkAllocateMemory(") + what + ")").c_str());
        vk_check(vkBindImageMemory(device_, image, mem, 0),
                 (std::string("vkBindImageMemory(") + what + ")").c_str());
    }

    VkImageView make_image_view(VkImage image, VkFormat format, VkImageAspectFlags aspect)
    {
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.subresourceRange.aspectMask = aspect;
        view.subresourceRange.baseMipLevel = 0;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.baseArrayLayer = 0;
        view.subresourceRange.layerCount = 1;
        VkImageView out = VK_NULL_HANDLE;
        vk_check(vkCreateImageView(device_, &view, nullptr, &out), "vkCreateImageView");
        return out;
    }

    // Format-stable: color = swapchain_format_ (so the scene pipeline stays compatible),
    // finalLayout SHADER_READ_ONLY_OPTIMAL so ImGui can sample the result directly.
    void create_offscreen_render_pass()
    {
        VkAttachmentDescription color{};
        color.format = swapchain_format_;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

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

        // Two dependencies: external -> subpass (start), subpass -> external (make the
        // color image visible to the fragment shader sampling it in the ImGui pass).
        std::array<VkSubpassDependency, 2> deps{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass = 0;
        deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        std::array<VkAttachmentDescription, 2> attachments{color, depth};

        VkRenderPassCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = static_cast<uint32_t>(attachments.size());
        ci.pAttachments = attachments.data();
        ci.subpassCount = 1;
        ci.pSubpasses = &subpass;
        ci.dependencyCount = static_cast<uint32_t>(deps.size());
        ci.pDependencies = deps.data();

        vk_check(vkCreateRenderPass(device_, &ci, nullptr, &vp_render_pass_),
                 "vkCreateRenderPass(offscreen)");

        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = VK_FILTER_LINEAR;
        si.minFilter = VK_FILTER_LINEAR;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.maxLod = 1.0f;
        vk_check(vkCreateSampler(device_, &si, nullptr, &vp_sampler_),
                 "vkCreateSampler(offscreen)");
    }

    void destroy_offscreen_target()
    {
        if (vp_imgui_tex_ != VK_NULL_HANDLE)
        {
            ImGui_ImplVulkan_RemoveTexture(vp_imgui_tex_);
            vp_imgui_tex_ = VK_NULL_HANDLE;
        }
        if (vp_framebuffer_ != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(device_, vp_framebuffer_, nullptr);
            vp_framebuffer_ = VK_NULL_HANDLE;
        }
        if (vp_color_view_ != VK_NULL_HANDLE) { vkDestroyImageView(device_, vp_color_view_, nullptr); vp_color_view_ = VK_NULL_HANDLE; }
        if (vp_color_ != VK_NULL_HANDLE)      { vkDestroyImage(device_, vp_color_, nullptr); vp_color_ = VK_NULL_HANDLE; }
        if (vp_color_mem_ != VK_NULL_HANDLE)  { vkFreeMemory(device_, vp_color_mem_, nullptr); vp_color_mem_ = VK_NULL_HANDLE; }
        if (vp_depth_view_ != VK_NULL_HANDLE) { vkDestroyImageView(device_, vp_depth_view_, nullptr); vp_depth_view_ = VK_NULL_HANDLE; }
        if (vp_depth_ != VK_NULL_HANDLE)      { vkDestroyImage(device_, vp_depth_, nullptr); vp_depth_ = VK_NULL_HANDLE; }
        if (vp_depth_mem_ != VK_NULL_HANDLE)  { vkFreeMemory(device_, vp_depth_mem_, nullptr); vp_depth_mem_ = VK_NULL_HANDLE; }
        vp_extent_ = {0, 0};
    }

    void create_offscreen_target(uint32_t width, uint32_t height)
    {
        // --- color image (sampled + color attachment) ---
        VkImageCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.extent = {width, height, 1};
        ci.mipLevels = 1;
        ci.arrayLayers = 1;
        ci.format = swapchain_format_;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vk_check(vkCreateImage(device_, &ci, nullptr, &vp_color_), "vkCreateImage(vp color)");
        bind_image_memory(vp_color_, vp_color_mem_, "vp color");
        vp_color_view_ = make_image_view(vp_color_, swapchain_format_, VK_IMAGE_ASPECT_COLOR_BIT);

        // --- depth image ---
        VkImageCreateInfo di = ci;
        di.format = kDepthFormat;
        di.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        vk_check(vkCreateImage(device_, &di, nullptr, &vp_depth_), "vkCreateImage(vp depth)");
        bind_image_memory(vp_depth_, vp_depth_mem_, "vp depth");
        vp_depth_view_ = make_image_view(vp_depth_, kDepthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

        // --- framebuffer ---
        std::array<VkImageView, 2> attachments{vp_color_view_, vp_depth_view_};
        VkFramebufferCreateInfo fb{};
        fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass = vp_render_pass_;
        fb.attachmentCount = static_cast<uint32_t>(attachments.size());
        fb.pAttachments = attachments.data();
        fb.width = width;
        fb.height = height;
        fb.layers = 1;
        vk_check(vkCreateFramebuffer(device_, &fb, nullptr, &vp_framebuffer_),
                 "vkCreateFramebuffer(offscreen)");

        // --- ImGui descriptor (only if the ImGui-Vulkan backend is up) ---
        if (ui_ready_)
        {
            vp_imgui_tex_ = ImGui_ImplVulkan_AddTexture(
                vp_sampler_, vp_color_view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        vp_extent_ = {width, height};
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

    // TWO descriptor set layouts, deliberately.
    //
    // texture_set_layout_ is one combined image+sampler at binding 0, shared today by
    // FOUR pipeline layouts -- world text, decals, UI images and UI text. It was five
    // before this change, including the lit mesh pass, and that is exactly why the lit
    // pass got its own layout below rather than this one growing a binding. A set
    // allocated against it belongs to a single texture.
    //
    // material_set_layout_ adds binding 1 for the metallic/roughness map and is used by
    // the lit mesh pass ALONE, where a set belongs to a (base_color, metallic_roughness)
    // PAIR. Adding binding 1 to the shared layout instead would have changed all five
    // pipelines -- and the other four write only binding 0, so their sets would no
    // longer match the layout they were allocated from. Found while implementing PBR
    // slice 3, whose plan assumed one layout.
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

        VkDescriptorSetLayoutBinding mat_bindings[2]{};
        mat_bindings[0] = binding;
        mat_bindings[1] = binding;
        mat_bindings[1].binding = 1;

        VkDescriptorSetLayoutCreateInfo mat_layout{};
        mat_layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        mat_layout.bindingCount = 2;
        mat_layout.pBindings = mat_bindings;
        vk_check(vkCreateDescriptorSetLayout(device_, &mat_layout, nullptr, &material_set_layout_),
                 "vkCreateDescriptorSetLayout(material)");

        // The pool feeds both: kMaxTextures single-binding sets plus kMaxMaterialSets
        // two-binding ones.
        VkDescriptorPoolSize pool_size{};
        pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_size.descriptorCount = kMaxTextures + kMaxMaterialSets * 2;

        VkDescriptorPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &pool_size;
        pool.maxSets = kMaxTextures + kMaxMaterialSets;
        vk_check(vkCreateDescriptorPool(device_, &pool, nullptr, &descriptor_pool_),
                 "vkCreateDescriptorPool");
    }

    // Set 1 in the main pipeline: a single, permanent descriptor pointing at the
    // shadow map, shared by every draw call this frame (unlike set 0's per-texture
    // sets) -- allocated once, written once (the shadow map is never recreated).
    void create_shadow_descriptor()
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
        vk_check(vkCreateDescriptorSetLayout(device_, &layout, nullptr, &shadow_set_layout_),
                 "vkCreateDescriptorSetLayout(shadow)");

        VkDescriptorPoolSize pool_size{};
        pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_size.descriptorCount = 1;
        VkDescriptorPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &pool_size;
        pool.maxSets = 1;
        vk_check(vkCreateDescriptorPool(device_, &pool, nullptr, &shadow_descriptor_pool_),
                 "vkCreateDescriptorPool(shadow)");

        VkDescriptorSetAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc.descriptorPool = shadow_descriptor_pool_;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &shadow_set_layout_;
        vk_check(vkAllocateDescriptorSets(device_, &alloc, &shadow_descriptor_set_),
                 "vkAllocateDescriptorSets(shadow)");

        VkDescriptorImageInfo image_info{};
        image_info.sampler = shadow_sampler_;
        image_info.imageView = shadow_view_;
        image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = shadow_descriptor_set_;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &image_info;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }

    // Set 2 in the main pipeline: per-frame world-space lighting. Visible to BOTH
    // stages -- the vertex shader reads light_view_proj to compute the shadow
    // lookup position, the fragment shader reads the lights themselves.
    void create_frame_lighting_resources()
    {
        for (int i = 0; i < kFramesInFlight; ++i)
        {
            create_buffer(sizeof(FrameLightingUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          frame_ubo_[i], frame_ubo_memory_[i]);
            vk_check(vkMapMemory(device_, frame_ubo_memory_[i], 0, sizeof(FrameLightingUbo), 0,
                                 &frame_ubo_mapped_[i]),
                     "vkMapMemory(frame lighting ubo)");
            std::memset(frame_ubo_mapped_[i], 0, sizeof(FrameLightingUbo));
        }

        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layout{};
        layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout.bindingCount = 1;
        layout.pBindings = &binding;
        vk_check(vkCreateDescriptorSetLayout(device_, &layout, nullptr, &frame_ubo_set_layout_),
                 "vkCreateDescriptorSetLayout(frame lighting)");

        VkDescriptorPoolSize pool_size{};
        pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_size.descriptorCount = kFramesInFlight;
        VkDescriptorPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &pool_size;
        pool.maxSets = kFramesInFlight;
        vk_check(vkCreateDescriptorPool(device_, &pool, nullptr, &frame_ubo_pool_),
                 "vkCreateDescriptorPool(frame lighting)");

        for (int i = 0; i < kFramesInFlight; ++i)
        {
            VkDescriptorSetAllocateInfo alloc{};
            alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            alloc.descriptorPool = frame_ubo_pool_;
            alloc.descriptorSetCount = 1;
            alloc.pSetLayouts = &frame_ubo_set_layout_;
            vk_check(vkAllocateDescriptorSets(device_, &alloc, &frame_ubo_set_[i]),
                     "vkAllocateDescriptorSets(frame lighting)");

            VkDescriptorBufferInfo info{};
            info.buffer = frame_ubo_[i];
            info.offset = 0;
            info.range = sizeof(FrameLightingUbo);

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = frame_ubo_set_[i];
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write.pBufferInfo = &info;
            vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
        }
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

    void create_shadow_pipeline()
    {
        std::vector<char> vert_code = read_file(std::string(MOSAIC_SHADER_DIR) + "/shadow.vert.spv");
        VkShaderModule vert = create_shader_module(vert_code);

        VkPipelineShaderStageCreateInfo vs{};
        vs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vs.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vs.module = vert;
        vs.pName = "main";

        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(Vertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription pos_attr{};
        pos_attr.location = 0;
        pos_attr.binding = 0;
        pos_attr.format = VK_FORMAT_R32G32B32_SFLOAT;
        pos_attr.offset = static_cast<uint32_t>(offsetof(Vertex, position));

        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &binding;
        vi.vertexAttributeDescriptionCount = 1;
        vi.pVertexAttributeDescriptions = &pos_attr;

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkViewport vp{};
        vp.width = static_cast<float>(kShadowMapSize);
        vp.height = static_cast<float>(kShadowMapSize);
        vp.maxDepth = 1.0f;
        VkRect2D scissor{};
        scissor.extent = {kShadowMapSize, kShadowMapSize};
        VkPipelineViewportStateCreateInfo vps{};
        vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vps.viewportCount = 1;
        vps.pViewports = &vp;
        vps.scissorCount = 1;
        vps.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE; // avoid peter-panning from front-face-only culling
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable = VK_TRUE;
        ds.depthWriteEnable = VK_TRUE;
        ds.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 0; // no color attachments in this subpass

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pcr.size = sizeof(ShadowPushConstants);

        VkPipelineLayoutCreateInfo pl{};
        pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pl.pushConstantRangeCount = 1;
        pl.pPushConstantRanges = &pcr;
        vk_check(vkCreatePipelineLayout(device_, &pl, nullptr, &shadow_pipeline_layout_),
                 "vkCreatePipelineLayout(shadow)");

        VkGraphicsPipelineCreateInfo gp{};
        gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gp.stageCount = 1;
        gp.pStages = &vs;
        gp.pVertexInputState = &vi;
        gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vps;
        gp.pRasterizationState = &rs;
        gp.pMultisampleState = &ms;
        gp.pDepthStencilState = &ds;
        gp.pColorBlendState = &cb;
        gp.layout = shadow_pipeline_layout_;
        gp.renderPass = shadow_render_pass_;
        gp.subpass = 0;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr,
                                           &shadow_pipeline_),
                 "vkCreateGraphicsPipelines(shadow)");

        vkDestroyShaderModule(device_, vert, nullptr);
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
        // Meshes are authored CCW-outward, but every pass renders with a negative-height
        // (Y-flipped) viewport, which reverses triangle winding in framebuffer space.
        // Declare CLOCKWISE as front so outward faces stay front-facing and back-face
        // culling hides the interior (otherwise the cube renders inside-out).
        rast.frontFace = VK_FRONT_FACE_CLOCKWISE;
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

        std::array<VkDescriptorSetLayout, 3> set_layouts{material_set_layout_, shadow_set_layout_,
                                                         frame_ubo_set_layout_};
        VkPipelineLayoutCreateInfo layout{};
        layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout.setLayoutCount = static_cast<uint32_t>(set_layouts.size());
        layout.pSetLayouts = set_layouts.data();
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

        // A second pipeline for the offscreen viewport pass. It is identical except for
        // the render pass: a pipeline must be render-pass-compatible with the pass it
        // draws in, and vp_render_pass_ differs from render_pass_ in its subpass
        // dependencies (2 vs 1), so the swapchain pipeline cannot be reused there.
        pi.renderPass = vp_render_pass_;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &vp_pipeline_),
                 "vkCreateGraphicsPipelines(offscreen)");

        // Modules are baked into the pipelines; safe to release immediately.
        vkDestroyShaderModule(device_, frag, nullptr);
        vkDestroyShaderModule(device_, vert, nullptr);
    }

    void create_particle_pipeline()
    {
        std::vector<char> vert_code = read_file(std::string(MOSAIC_SHADER_DIR) + "/particle.vert.spv");
        std::vector<char> frag_code = read_file(std::string(MOSAIC_SHADER_DIR) + "/particle.frag.spv");
        VkShaderModule vert = create_shader_module(vert_code);
        VkShaderModule frag = create_shader_module(frag_code);

        VkPipelineShaderStageCreateInfo vs{};
        vs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vs.stage = VK_SHADER_STAGE_VERTEX_BIT; vs.module = vert; vs.pName = "main";
        VkPipelineShaderStageCreateInfo fs{};
        fs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fs.stage = VK_SHADER_STAGE_FRAGMENT_BIT; fs.module = frag; fs.pName = "main";
        VkPipelineShaderStageCreateInfo stages[] = {vs, fs};

        // Instance-rate binding: one ParticleInstance per billboard.
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(mosaic::engine::render::ParticleInstance);
        binding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

        std::array<VkVertexInputAttributeDescription, 4> attrs{};
        attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[0].offset = offsetof(mosaic::engine::render::ParticleInstance, world_position);
        attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32_SFLOAT;
        attrs[1].offset = offsetof(mosaic::engine::render::ParticleInstance, size);
        attrs[2].location = 2; attrs[2].binding = 0; attrs[2].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[2].offset = offsetof(mosaic::engine::render::ParticleInstance, color);
        attrs[3].location = 3; attrs[3].binding = 0; attrs[3].format = VK_FORMAT_R32_SFLOAT;
        attrs[3].offset = offsetof(mosaic::engine::render::ParticleInstance, alpha);

        VkPipelineVertexInputStateCreateInfo vin{};
        vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vin.vertexBindingDescriptionCount = 1;
        vin.pVertexBindingDescriptions = &binding;
        vin.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
        vin.pVertexAttributeDescriptions = attrs.data();

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1; vp.scissorCount = 1;

        VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2; dyn.pDynamicStates = dyn_states;

        VkPipelineRasterizationStateCreateInfo rast{};
        rast.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rast.polygonMode = VK_POLYGON_MODE_FILL; rast.lineWidth = 1.0f;
        rast.cullMode = VK_CULL_MODE_NONE; // billboards face the camera; winding irrelevant

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_FALSE; // transparent: respect scene depth, don't occlude peers
        depth.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState blend{};
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blend.blendEnable = VK_TRUE;
        blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.colorBlendOp = VK_BLEND_OP_ADD;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blend.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &blend;

        VkPushConstantRange push{};
        push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        push.offset = 0; push.size = sizeof(ParticlePush);

        VkPipelineLayoutCreateInfo layout{};
        layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout.setLayoutCount = 0; // particles are untextured; no descriptor sets
        layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &push;
        vk_check(vkCreatePipelineLayout(device_, &layout, nullptr, &particle_pipeline_layout_),
                 "vkCreatePipelineLayout(particle)");

        VkGraphicsPipelineCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pi.stageCount = 2; pi.pStages = stages;
        pi.pVertexInputState = &vin; pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp; pi.pRasterizationState = &rast;
        pi.pMultisampleState = &ms; pi.pDepthStencilState = &depth;
        pi.pColorBlendState = &cb; pi.pDynamicState = &dyn;
        pi.layout = particle_pipeline_layout_;
        pi.renderPass = render_pass_; pi.subpass = 0;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &particle_pipeline_),
                 "vkCreateGraphicsPipelines(particle)");

        pi.renderPass = vp_render_pass_;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &vp_particle_pipeline_),
                 "vkCreateGraphicsPipelines(particle offscreen)");

        vkDestroyShaderModule(device_, frag, nullptr);
        vkDestroyShaderModule(device_, vert, nullptr);
    }

    void create_ribbon_pipeline()
    {
        std::vector<char> vert_code = read_file(std::string(MOSAIC_SHADER_DIR) + "/ribbon.vert.spv");
        std::vector<char> frag_code = read_file(std::string(MOSAIC_SHADER_DIR) + "/ribbon.frag.spv");
        VkShaderModule vert = create_shader_module(vert_code);
        VkShaderModule frag = create_shader_module(frag_code);

        VkPipelineShaderStageCreateInfo vs{};
        vs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vs.stage = VK_SHADER_STAGE_VERTEX_BIT; vs.module = vert; vs.pName = "main";
        VkPipelineShaderStageCreateInfo fs{};
        fs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fs.stage = VK_SHADER_STAGE_FRAGMENT_BIT; fs.module = frag; fs.pName = "main";
        VkPipelineShaderStageCreateInfo stages[] = {vs, fs};

        // Per-vertex binding: pre-expanded world-space ribbon triangles.
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(mosaic::engine::render::RibbonVertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 3> attrs{};
        attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[0].offset = offsetof(mosaic::engine::render::RibbonVertex, position);
        attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[1].offset = offsetof(mosaic::engine::render::RibbonVertex, color);
        attrs[2].location = 2; attrs[2].binding = 0; attrs[2].format = VK_FORMAT_R32_SFLOAT;
        attrs[2].offset = offsetof(mosaic::engine::render::RibbonVertex, alpha);

        VkPipelineVertexInputStateCreateInfo vin{};
        vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vin.vertexBindingDescriptionCount = 1;
        vin.pVertexBindingDescriptions = &binding;
        vin.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
        vin.pVertexAttributeDescriptions = attrs.data();

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1; vp.scissorCount = 1;

        VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2; dyn.pDynamicStates = dyn_states;

        VkPipelineRasterizationStateCreateInfo rast{};
        rast.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rast.polygonMode = VK_POLYGON_MODE_FILL; rast.lineWidth = 1.0f;
        rast.cullMode = VK_CULL_MODE_NONE; // camera-facing ribbon; winding irrelevant

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_FALSE; // transparent: respect scene depth, don't occlude peers
        depth.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState blend{};
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blend.blendEnable = VK_TRUE;
        blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.colorBlendOp = VK_BLEND_OP_ADD;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blend.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &blend;

        VkPushConstantRange push{};
        push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        push.offset = 0; push.size = sizeof(RibbonPush);

        VkPipelineLayoutCreateInfo layout{};
        layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout.setLayoutCount = 0;
        layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &push;
        vk_check(vkCreatePipelineLayout(device_, &layout, nullptr, &ribbon_pipeline_layout_),
                 "vkCreatePipelineLayout(ribbon)");

        VkGraphicsPipelineCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pi.stageCount = 2; pi.pStages = stages;
        pi.pVertexInputState = &vin; pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp; pi.pRasterizationState = &rast;
        pi.pMultisampleState = &ms; pi.pDepthStencilState = &depth;
        pi.pColorBlendState = &cb; pi.pDynamicState = &dyn;
        pi.layout = ribbon_pipeline_layout_;
        pi.renderPass = render_pass_; pi.subpass = 0;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &ribbon_pipeline_),
                 "vkCreateGraphicsPipelines(ribbon)");

        pi.renderPass = vp_render_pass_;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &vp_ribbon_pipeline_),
                 "vkCreateGraphicsPipelines(ribbon offscreen)");

        vkDestroyShaderModule(device_, frag, nullptr);
        vkDestroyShaderModule(device_, vert, nullptr);
    }

    void create_world_text_pipeline()
    {
        std::vector<char> vert_code = read_file(std::string(MOSAIC_SHADER_DIR) + "/world_text.vert.spv");
        std::vector<char> frag_code = read_file(std::string(MOSAIC_SHADER_DIR) + "/world_text.frag.spv");
        VkShaderModule vert = create_shader_module(vert_code);
        VkShaderModule frag = create_shader_module(frag_code);

        VkPipelineShaderStageCreateInfo vs{};
        vs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vs.stage = VK_SHADER_STAGE_VERTEX_BIT; vs.module = vert; vs.pName = "main";
        VkPipelineShaderStageCreateInfo fs{};
        fs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fs.stage = VK_SHADER_STAGE_FRAGMENT_BIT; fs.module = frag; fs.pName = "main";
        VkPipelineShaderStageCreateInfo stages[] = {vs, fs};

        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(mosaic::engine::render::WorldGlyphVertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 4> attrs{};
        attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[0].offset = offsetof(mosaic::engine::render::WorldGlyphVertex, position);
        attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[1].offset = offsetof(mosaic::engine::render::WorldGlyphVertex, u);
        attrs[2].location = 2; attrs[2].binding = 0; attrs[2].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[2].offset = offsetof(mosaic::engine::render::WorldGlyphVertex, color);
        attrs[3].location = 3; attrs[3].binding = 0; attrs[3].format = VK_FORMAT_R32_SFLOAT;
        attrs[3].offset = offsetof(mosaic::engine::render::WorldGlyphVertex, alpha);

        VkPipelineVertexInputStateCreateInfo vin{};
        vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vin.vertexBindingDescriptionCount = 1;
        vin.pVertexBindingDescriptions = &binding;
        vin.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
        vin.pVertexAttributeDescriptions = attrs.data();

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1; vp.scissorCount = 1;

        VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2; dyn.pDynamicStates = dyn_states;

        VkPipelineRasterizationStateCreateInfo rast{};
        rast.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rast.polygonMode = VK_POLYGON_MODE_FILL; rast.lineWidth = 1.0f;
        rast.cullMode = VK_CULL_MODE_NONE; // camera-facing ribbon; winding irrelevant

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_FALSE; // transparent: respect scene depth, don't occlude peers
        depth.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState blend{};
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blend.blendEnable = VK_TRUE;
        blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.colorBlendOp = VK_BLEND_OP_ADD;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blend.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &blend;

        VkPushConstantRange push{};
        push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        push.offset = 0; push.size = sizeof(WorldTextPush);

        VkPipelineLayoutCreateInfo layout{};
        layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &texture_set_layout_;
        layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &push;
        vk_check(vkCreatePipelineLayout(device_, &layout, nullptr, &world_text_pipeline_layout_),
                 "vkCreatePipelineLayout(world text)");

        VkGraphicsPipelineCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pi.stageCount = 2; pi.pStages = stages;
        pi.pVertexInputState = &vin; pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp; pi.pRasterizationState = &rast;
        pi.pMultisampleState = &ms; pi.pDepthStencilState = &depth;
        pi.pColorBlendState = &cb; pi.pDynamicState = &dyn;
        pi.layout = world_text_pipeline_layout_;
        pi.renderPass = render_pass_; pi.subpass = 0;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &world_text_pipeline_),
                 "vkCreateGraphicsPipelines(world text)");

        pi.renderPass = vp_render_pass_;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &vp_world_text_pipeline_),
                 "vkCreateGraphicsPipelines(world text offscreen)");

        vkDestroyShaderModule(device_, frag, nullptr);
        vkDestroyShaderModule(device_, vert, nullptr);
    }

    void create_decal_pipeline()
    {
        std::vector<char> vert_code = read_file(std::string(MOSAIC_SHADER_DIR) + "/decal.vert.spv");
        std::vector<char> frag_code = read_file(std::string(MOSAIC_SHADER_DIR) + "/decal.frag.spv");
        VkShaderModule vert = create_shader_module(vert_code);
        VkShaderModule frag = create_shader_module(frag_code);

        VkPipelineShaderStageCreateInfo vs{};
        vs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vs.stage = VK_SHADER_STAGE_VERTEX_BIT; vs.module = vert; vs.pName = "main";
        VkPipelineShaderStageCreateInfo fs{};
        fs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fs.stage = VK_SHADER_STAGE_FRAGMENT_BIT; fs.module = frag; fs.pName = "main";
        VkPipelineShaderStageCreateInfo stages[] = {vs, fs};

        // No vertex buffer: the quad is generated from gl_VertexIndex.
        VkPipelineVertexInputStateCreateInfo vin{};
        vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1; vp.scissorCount = 1;

        VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2; dyn.pDynamicStates = dyn_states;

        VkPipelineRasterizationStateCreateInfo rast{};
        rast.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rast.polygonMode = VK_POLYGON_MODE_FILL; rast.lineWidth = 1.0f;
        rast.cullMode = VK_CULL_MODE_NONE; // quad visible from both sides

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_FALSE; // transparent: respect scene depth, don't occlude peers
        depth.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState blend{};
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blend.blendEnable = VK_TRUE;
        blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.colorBlendOp = VK_BLEND_OP_ADD;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blend.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &blend;

        VkPushConstantRange push{};
        push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        push.offset = 0; push.size = sizeof(DecalPush);

        VkPipelineLayoutCreateInfo layout{};
        layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout.setLayoutCount = 1; layout.pSetLayouts = &texture_set_layout_;
        layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &push;
        vk_check(vkCreatePipelineLayout(device_, &layout, nullptr, &decal_pipeline_layout_),
                 "vkCreatePipelineLayout(decal)");

        VkGraphicsPipelineCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pi.stageCount = 2; pi.pStages = stages;
        pi.pVertexInputState = &vin; pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp; pi.pRasterizationState = &rast;
        pi.pMultisampleState = &ms; pi.pDepthStencilState = &depth;
        pi.pColorBlendState = &cb; pi.pDynamicState = &dyn;
        pi.layout = decal_pipeline_layout_;
        pi.renderPass = render_pass_; pi.subpass = 0;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &decal_pipeline_),
                 "vkCreateGraphicsPipelines(decal)");

        pi.renderPass = vp_render_pass_;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &vp_decal_pipeline_),
                 "vkCreateGraphicsPipelines(decal offscreen)");

        vkDestroyShaderModule(device_, frag, nullptr);
        vkDestroyShaderModule(device_, vert, nullptr);
    }

    void create_skybox_pipeline()
    {
        std::vector<char> vert_code = read_file(std::string(MOSAIC_SHADER_DIR) + "/skybox.vert.spv");
        std::vector<char> frag_code = read_file(std::string(MOSAIC_SHADER_DIR) + "/skybox.frag.spv");
        VkShaderModule vert = create_shader_module(vert_code);
        VkShaderModule frag = create_shader_module(frag_code);

        VkPipelineShaderStageCreateInfo vs{};
        vs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vs.stage = VK_SHADER_STAGE_VERTEX_BIT; vs.module = vert; vs.pName = "main";
        VkPipelineShaderStageCreateInfo fs{};
        fs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fs.stage = VK_SHADER_STAGE_FRAGMENT_BIT; fs.module = frag; fs.pName = "main";
        VkPipelineShaderStageCreateInfo stages[] = {vs, fs};

        VkPipelineVertexInputStateCreateInfo vin{};
        vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO; // no inputs

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1; vp.scissorCount = 1;

        VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2; dyn.pDynamicStates = dyn_states;

        VkPipelineRasterizationStateCreateInfo rast{};
        rast.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rast.polygonMode = VK_POLYGON_MODE_FILL; rast.lineWidth = 1.0f;
        rast.cullMode = VK_CULL_MODE_NONE;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        // The two settings that put the sky BEHIND the scene rather than over it.
        // depthWrite on would let it occlude everything drawn afterwards; a LESS compare
        // would reject every fragment at the far plane and show no sky at all. Both are
        // one-token mistakes with a total visual effect, which is why a contract test
        // pins them.
        VkPipelineDepthStencilStateCreateInfo sky_depth{};
        sky_depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        sky_depth.depthTestEnable = VK_TRUE;
        sky_depth.depthWriteEnable = VK_FALSE;
        sky_depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        VkPipelineColorBlendAttachmentState blend{};
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blend.blendEnable = VK_FALSE; // opaque: the sky replaces whatever cleared

        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &blend;

        VkPushConstantRange push{};
        push.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        push.offset = 0; push.size = sizeof(SkyboxPush);

        VkPipelineLayoutCreateInfo layout{};
        layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout.setLayoutCount = 1; layout.pSetLayouts = &texture_set_layout_;
        layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &push;
        vk_check(vkCreatePipelineLayout(device_, &layout, nullptr, &skybox_pipeline_layout_),
                 "vkCreatePipelineLayout(skybox)");

        VkGraphicsPipelineCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pi.stageCount = 2; pi.pStages = stages;
        pi.pVertexInputState = &vin; pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp; pi.pRasterizationState = &rast;
        pi.pMultisampleState = &ms; pi.pDepthStencilState = &sky_depth;
        pi.pColorBlendState = &cb; pi.pDynamicState = &dyn;
        pi.layout = skybox_pipeline_layout_;
        pi.renderPass = render_pass_; pi.subpass = 0;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &skybox_pipeline_),
                 "vkCreateGraphicsPipelines(skybox)");

        // The offscreen variant, so the Studio viewport shows the sky too -- every
        // other pipeline here is created in both forms for the same reason.
        pi.renderPass = vp_render_pass_;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &vp_skybox_pipeline_),
                 "vkCreateGraphicsPipelines(skybox offscreen)");

        vkDestroyShaderModule(device_, frag, nullptr);
        vkDestroyShaderModule(device_, vert, nullptr);
    }

    void create_screen_effect_pipeline()
    {
        std::vector<char> vert_code = read_file(std::string(MOSAIC_SHADER_DIR) + "/screen_effect.vert.spv");
        std::vector<char> frag_code = read_file(std::string(MOSAIC_SHADER_DIR) + "/screen_effect.frag.spv");
        VkShaderModule vert = create_shader_module(vert_code);
        VkShaderModule frag = create_shader_module(frag_code);

        VkPipelineShaderStageCreateInfo vs{};
        vs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vs.stage = VK_SHADER_STAGE_VERTEX_BIT; vs.module = vert; vs.pName = "main";
        VkPipelineShaderStageCreateInfo fs{};
        fs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fs.stage = VK_SHADER_STAGE_FRAGMENT_BIT; fs.module = frag; fs.pName = "main";
        VkPipelineShaderStageCreateInfo stages[] = {vs, fs};

        VkPipelineVertexInputStateCreateInfo vin{};
        vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO; // no inputs

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1; vp.scissorCount = 1;

        VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2; dyn.pDynamicStates = dyn_states;

        VkPipelineRasterizationStateCreateInfo rast{};
        rast.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rast.polygonMode = VK_POLYGON_MODE_FILL; rast.lineWidth = 1.0f;
        rast.cullMode = VK_CULL_MODE_NONE;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable = VK_FALSE;  // overlay is always on top
        depth.depthWriteEnable = VK_FALSE;
        depth.depthCompareOp = VK_COMPARE_OP_ALWAYS;

        VkPipelineColorBlendAttachmentState blend{};
        // Alpha is deliberately excluded from the write mask: the effect is a
        // color-only overlay on an already-opaque scene. Writing its computed
        // alpha (near 0 at the vignette's clear center, ~vignette at the dark
        // edges) into the destination is harmless for the swapchain (the OS
        // ignores its alpha) but corrupts the Studio viewport's offscreen
        // texture, whose alpha IS respected when ImGui::Image composites it
        // over the panel background — producing an apparent inverted vignette.
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT;
        blend.blendEnable = VK_TRUE;
        blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.colorBlendOp = VK_BLEND_OP_ADD;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blend.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &blend;

        VkPushConstantRange push{};
        push.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        push.offset = 0; push.size = sizeof(ScreenEffectPush);

        VkPipelineLayoutCreateInfo layout{};
        layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout.setLayoutCount = 0;
        layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &push;
        vk_check(vkCreatePipelineLayout(device_, &layout, nullptr, &screen_effect_pipeline_layout_),
                 "vkCreatePipelineLayout(screen_effect)");

        VkGraphicsPipelineCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pi.stageCount = 2; pi.pStages = stages;
        pi.pVertexInputState = &vin; pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp; pi.pRasterizationState = &rast;
        pi.pMultisampleState = &ms; pi.pDepthStencilState = &depth;
        pi.pColorBlendState = &cb; pi.pDynamicState = &dyn;
        pi.layout = screen_effect_pipeline_layout_;
        pi.renderPass = render_pass_; pi.subpass = 0;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &screen_effect_pipeline_),
                 "vkCreateGraphicsPipelines(screen_effect)");

        pi.renderPass = vp_render_pass_;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &vp_screen_effect_pipeline_),
                 "vkCreateGraphicsPipelines(screen_effect offscreen)");

        vkDestroyShaderModule(device_, frag, nullptr);
        vkDestroyShaderModule(device_, vert, nullptr);
    }

    void create_ui_pipeline()
    {
        std::vector<char> vert_code = read_file(std::string(MOSAIC_SHADER_DIR) + "/ui.vert.spv");
        std::vector<char> frag_code = read_file(std::string(MOSAIC_SHADER_DIR) + "/ui.frag.spv");
        VkShaderModule vert = create_shader_module(vert_code);
        VkShaderModule frag = create_shader_module(frag_code);

        VkPipelineShaderStageCreateInfo vs{};
        vs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vs.stage = VK_SHADER_STAGE_VERTEX_BIT; vs.module = vert; vs.pName = "main";
        VkPipelineShaderStageCreateInfo fs{};
        fs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fs.stage = VK_SHADER_STAGE_FRAGMENT_BIT; fs.module = frag; fs.pName = "main";
        VkPipelineShaderStageCreateInfo stages[] = {vs, fs};

        VkPipelineVertexInputStateCreateInfo vin{};
        vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO; // no inputs

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1; vp.scissorCount = 1;

        VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2; dyn.pDynamicStates = dyn_states;

        VkPipelineRasterizationStateCreateInfo rast{};
        rast.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rast.polygonMode = VK_POLYGON_MODE_FILL; rast.lineWidth = 1.0f;
        rast.cullMode = VK_CULL_MODE_NONE;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable = VK_FALSE;  // overlay is always on top
        depth.depthWriteEnable = VK_FALSE;
        depth.depthCompareOp = VK_COMPARE_OP_ALWAYS;

        VkPipelineColorBlendAttachmentState blend{};
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blend.blendEnable = VK_TRUE;
        blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.colorBlendOp = VK_BLEND_OP_ADD;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blend.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &blend;

        VkPushConstantRange push{};
        push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        push.offset = 0; push.size = sizeof(UiPush);

        VkPipelineLayoutCreateInfo layout{};
        layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout.setLayoutCount = 0;
        layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &push;
        vk_check(vkCreatePipelineLayout(device_, &layout, nullptr, &ui_pipeline_layout_),
                 "vkCreatePipelineLayout(ui)");

        VkGraphicsPipelineCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pi.stageCount = 2; pi.pStages = stages;
        pi.pVertexInputState = &vin; pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp; pi.pRasterizationState = &rast;
        pi.pMultisampleState = &ms; pi.pDepthStencilState = &depth;
        pi.pColorBlendState = &cb; pi.pDynamicState = &dyn;
        pi.layout = ui_pipeline_layout_;
        pi.renderPass = render_pass_; pi.subpass = 0;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &ui_pipeline_),
                 "vkCreateGraphicsPipelines(ui)");

        pi.renderPass = vp_render_pass_;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &vp_ui_pipeline_),
                 "vkCreateGraphicsPipelines(ui offscreen)");

        vkDestroyShaderModule(device_, frag, nullptr);
        vkDestroyShaderModule(device_, vert, nullptr);
    }

    void create_ui_image_pipeline()
    {
        std::vector<char> vert_code = read_file(std::string(MOSAIC_SHADER_DIR) + "/ui_image.vert.spv");
        std::vector<char> frag_code = read_file(std::string(MOSAIC_SHADER_DIR) + "/ui_image.frag.spv");
        VkShaderModule vert = create_shader_module(vert_code);
        VkShaderModule frag = create_shader_module(frag_code);

        VkPipelineShaderStageCreateInfo vs{};
        vs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vs.stage = VK_SHADER_STAGE_VERTEX_BIT; vs.module = vert; vs.pName = "main";
        VkPipelineShaderStageCreateInfo fs{};
        fs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fs.stage = VK_SHADER_STAGE_FRAGMENT_BIT; fs.module = frag; fs.pName = "main";
        VkPipelineShaderStageCreateInfo stages[] = {vs, fs};

        VkPipelineVertexInputStateCreateInfo vin{};
        vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO; // no inputs

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1; vp.scissorCount = 1;

        VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2; dyn.pDynamicStates = dyn_states;

        VkPipelineRasterizationStateCreateInfo rast{};
        rast.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rast.polygonMode = VK_POLYGON_MODE_FILL; rast.lineWidth = 1.0f;
        rast.cullMode = VK_CULL_MODE_NONE;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable = VK_FALSE;  // overlay is always on top
        depth.depthWriteEnable = VK_FALSE;
        depth.depthCompareOp = VK_COMPARE_OP_ALWAYS;

        VkPipelineColorBlendAttachmentState blend{};
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blend.blendEnable = VK_TRUE;
        blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.colorBlendOp = VK_BLEND_OP_ADD;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blend.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &blend;

        VkPushConstantRange push{};
        push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        push.offset = 0; push.size = sizeof(UiImagePush);

        VkPipelineLayoutCreateInfo layout{};
        layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &texture_set_layout_;
        layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &push;
        vk_check(vkCreatePipelineLayout(device_, &layout, nullptr, &ui_image_pipeline_layout_),
                 "vkCreatePipelineLayout(ui_image)");

        VkGraphicsPipelineCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pi.stageCount = 2; pi.pStages = stages;
        pi.pVertexInputState = &vin; pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp; pi.pRasterizationState = &rast;
        pi.pMultisampleState = &ms; pi.pDepthStencilState = &depth;
        pi.pColorBlendState = &cb; pi.pDynamicState = &dyn;
        pi.layout = ui_image_pipeline_layout_;
        pi.renderPass = render_pass_; pi.subpass = 0;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &ui_image_pipeline_),
                 "vkCreateGraphicsPipelines(ui_image)");

        pi.renderPass = vp_render_pass_;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &vp_ui_image_pipeline_),
                 "vkCreateGraphicsPipelines(ui_image offscreen)");

        vkDestroyShaderModule(device_, frag, nullptr);
        vkDestroyShaderModule(device_, vert, nullptr);
    }

    void create_ui_text_pipeline()
    {
        std::vector<char> vert_code = read_file(std::string(MOSAIC_SHADER_DIR) + "/ui_text.vert.spv");
        std::vector<char> frag_code = read_file(std::string(MOSAIC_SHADER_DIR) + "/ui_text.frag.spv");
        VkShaderModule vert = create_shader_module(vert_code);
        VkShaderModule frag = create_shader_module(frag_code);

        VkPipelineShaderStageCreateInfo vs{};
        vs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vs.stage = VK_SHADER_STAGE_VERTEX_BIT; vs.module = vert; vs.pName = "main";
        VkPipelineShaderStageCreateInfo fs{};
        fs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fs.stage = VK_SHADER_STAGE_FRAGMENT_BIT; fs.module = frag; fs.pName = "main";
        VkPipelineShaderStageCreateInfo stages[] = {vs, fs};

        VkPipelineVertexInputStateCreateInfo vin{};
        vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO; // no inputs

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1; vp.scissorCount = 1;

        VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2; dyn.pDynamicStates = dyn_states;

        VkPipelineRasterizationStateCreateInfo rast{};
        rast.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rast.polygonMode = VK_POLYGON_MODE_FILL; rast.lineWidth = 1.0f;
        rast.cullMode = VK_CULL_MODE_NONE;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable = VK_FALSE;  // overlay is always on top
        depth.depthWriteEnable = VK_FALSE;
        depth.depthCompareOp = VK_COMPARE_OP_ALWAYS;

        VkPipelineColorBlendAttachmentState blend{};
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blend.blendEnable = VK_TRUE;
        blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.colorBlendOp = VK_BLEND_OP_ADD;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blend.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &blend;

        VkPushConstantRange push{};
        push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        push.offset = 0; push.size = sizeof(UiTextPush);

        VkPipelineLayoutCreateInfo layout{};
        layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout.setLayoutCount = 1; layout.pSetLayouts = &texture_set_layout_;
        layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &push;
        vk_check(vkCreatePipelineLayout(device_, &layout, nullptr, &ui_text_pipeline_layout_),
                 "vkCreatePipelineLayout(ui_text)");

        VkGraphicsPipelineCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pi.stageCount = 2; pi.pStages = stages;
        pi.pVertexInputState = &vin; pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp; pi.pRasterizationState = &rast;
        pi.pMultisampleState = &ms; pi.pDepthStencilState = &depth;
        pi.pColorBlendState = &cb; pi.pDynamicState = &dyn;
        pi.layout = ui_text_pipeline_layout_;
        pi.renderPass = render_pass_; pi.subpass = 0;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &ui_text_pipeline_),
                 "vkCreateGraphicsPipelines(ui_text)");

        pi.renderPass = vp_render_pass_;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &vp_ui_text_pipeline_),
                 "vkCreateGraphicsPipelines(ui_text offscreen)");

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
            vk_check(vkCreateFence(device_, &fence, nullptr, &in_flight_[i]),
                     "vkCreateFence");
        }

        create_render_finished_semaphores();
    }

    // render_finished_ is sized per swapchain image and indexed by the acquired
    // image index (not frame_), so a present wait semaphore is never reused while
    // its present is still pending. Rebuilt on swapchain recreation because the
    // image count can change. Callers must ensure the device is idle first.
    void create_render_finished_semaphores()
    {
        VkSemaphoreCreateInfo sem{};
        sem.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        for (VkSemaphore s : render_finished_)
        {
            if (s != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(device_, s, nullptr);
            }
        }

        render_finished_.assign(images_.size(), VK_NULL_HANDLE);
        for (VkSemaphore& s : render_finished_)
        {
            vk_check(vkCreateSemaphore(device_, &sem, nullptr, &s),
                     "vkCreateSemaphore");
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
        // The swapchain image count can change on recreation; rebuild the per-image
        // present semaphores to match (device is idle from vkDeviceWaitIdle above).
        create_render_finished_semaphores();
        // render pass and pipeline are format-stable, so they are not recreated
    }

    // --- members ----------------------------------------------------------

    GLFWwindow* window_ = nullptr;
    VkDescriptorPool imgui_pool_ = VK_NULL_HANDLE;
    bool             ui_ready_ = false;
    std::string      imgui_ini_path_; // must outlive the ImGui context -- io.IniFilename is a raw pointer
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

    static constexpr uint32_t kShadowMapSize = 2048; // fixed resolution, v1 (see design doc)
    VkImage        shadow_image_ = VK_NULL_HANDLE;
    VkDeviceMemory shadow_memory_ = VK_NULL_HANDLE;
    VkImageView    shadow_view_ = VK_NULL_HANDLE;
    VkSampler      shadow_sampler_ = VK_NULL_HANDLE;
    VkRenderPass   shadow_render_pass_ = VK_NULL_HANDLE;
    VkFramebuffer  shadow_framebuffer_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout shadow_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool      shadow_descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet       shadow_descriptor_set_ = VK_NULL_HANDLE;
    VkPipelineLayout shadow_pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline       shadow_pipeline_ = VK_NULL_HANDLE;

    // Per-frame lighting UBO -- one per frame-in-flight, so writing frame N cannot
    // race a still-executing frame N-1 reading its own copy. Persistently mapped.
    VkBuffer              frame_ubo_[kFramesInFlight]{};
    VkDeviceMemory        frame_ubo_memory_[kFramesInFlight]{};
    void*                 frame_ubo_mapped_[kFramesInFlight]{};
    VkDescriptorSetLayout frame_ubo_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool      frame_ubo_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet       frame_ubo_set_[kFramesInFlight]{};

    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;

    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    // Descriptor infrastructure for sampled textures (set 0, binding 0).
    static constexpr uint32_t kMaxTextures = 64;
    // Distinct (base_color, metallic_roughness) pairs a scene may use. Four times
    // kMaxTextures, which bounds pairs generously for any scene this engine can build
    // today -- the demo uses fewer than ten. Exhaustion fails loudly in material_set()
    // rather than silently handing back another material's set.
    static constexpr uint32_t kMaxMaterialSets = 256;
    VkDescriptorSetLayout texture_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout material_set_layout_ = VK_NULL_HANDLE;
    VkPipeline skybox_pipeline_ = VK_NULL_HANDLE;
    VkPipeline vp_skybox_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout skybox_pipeline_layout_ = VK_NULL_HANDLE;
    // One set per (base_color_index, metallic_roughness_index) pair, keyed by the two
    // packed into a u64. Created on first use, kept for the device's lifetime, and
    // freed with the pool.
    std::unordered_map<std::uint64_t, VkDescriptorSet> material_sets_;
    bool material_cap_reported_ = false;
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
            // Which handle space this entry belongs to. They share TextureHandle, so
        // without this a cubemap bound to a sampler2D (or the reverse) is a validation
        // error at best and a garbage image at worst, with nothing to catch it.
        bool is_cube = false;
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
    // Per swapchain image, not per frame-in-flight: render_finished_ is used as the
    // present wait semaphore, which must not be reused until its present completes.
    // Since the swapchain can hold more images than frames-in-flight, indexing this
    // by frame_ would reuse a semaphore while a present on another image is pending.
    // Indexed by image_index_ instead. See swapchain_semaphore_reuse in the Vulkan guide.
    std::vector<VkSemaphore> render_finished_;
    VkFence in_flight_[kFramesInFlight] = {};

    uint32_t frame_ = 0;
    uint32_t image_index_ = 0;
    bool framebuffer_resized_ = false;
    bool frame_skipped_ = false;

    // The swapchain render pass is begun lazily (on the first swapchain-targeted draw,
    // at end_ui_frame, or at end_frame) so the editor can record an offscreen viewport
    // pass first. viewport_pass_open_ tracks whether draw() is currently targeting the
    // offscreen target instead of the swapchain.
    bool swapchain_pass_open_ = false;
    bool viewport_pass_open_  = false;

    // Offscreen viewport target (37d). Sampled by ImGui into the editor Viewport panel.
    VkExtent2D      vp_extent_ = {0, 0};
    VkRenderPass    vp_render_pass_ = VK_NULL_HANDLE; // format-stable; color = swapchain_format_
    VkPipeline      vp_pipeline_ = VK_NULL_HANDLE;    // scene pipeline built against vp_render_pass_

    VkPipeline       particle_pipeline_ = VK_NULL_HANDLE;
    VkPipeline       vp_particle_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout particle_pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline       ribbon_pipeline_ = VK_NULL_HANDLE;
    VkPipeline       vp_ribbon_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout ribbon_pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline       world_text_pipeline_ = VK_NULL_HANDLE;
    VkPipeline       vp_world_text_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout world_text_pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline       decal_pipeline_ = VK_NULL_HANDLE;
    VkPipeline       vp_decal_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout decal_pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline       screen_effect_pipeline_ = VK_NULL_HANDLE;
    VkPipeline       vp_screen_effect_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout screen_effect_pipeline_layout_ = VK_NULL_HANDLE;

    VkPipeline       ui_pipeline_ = VK_NULL_HANDLE;
    VkPipeline       vp_ui_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout ui_pipeline_layout_ = VK_NULL_HANDLE;

    VkPipeline       ui_image_pipeline_ = VK_NULL_HANDLE;
    VkPipeline       vp_ui_image_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout ui_image_pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline       ui_text_pipeline_ = VK_NULL_HANDLE;
    VkPipeline       vp_ui_text_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout ui_text_pipeline_layout_ = VK_NULL_HANDLE;

    struct InstanceBuffer { VkBuffer buf = VK_NULL_HANDLE; VkDeviceMemory mem = VK_NULL_HANDLE; VkDeviceSize capacity = 0; };
    std::array<InstanceBuffer, kFramesInFlight> particle_instances_{};
    std::array<InstanceBuffer, kFramesInFlight> ribbon_vertices_{};
    std::array<InstanceBuffer, kFramesInFlight> world_text_vertices_{};
    VkImage         vp_color_ = VK_NULL_HANDLE;
    VkDeviceMemory  vp_color_mem_ = VK_NULL_HANDLE;
    VkImageView     vp_color_view_ = VK_NULL_HANDLE;
    VkImage         vp_depth_ = VK_NULL_HANDLE;
    VkDeviceMemory  vp_depth_mem_ = VK_NULL_HANDLE;
    VkImageView     vp_depth_view_ = VK_NULL_HANDLE;
    VkFramebuffer   vp_framebuffer_ = VK_NULL_HANDLE;
    VkSampler       vp_sampler_ = VK_NULL_HANDLE;       // persistent across resizes
    VkDescriptorSet vp_imgui_tex_ = VK_NULL_HANDLE;     // from ImGui_ImplVulkan_AddTexture
};

} // namespace

std::unique_ptr<mosaic::engine::render::RenderDevice>
create_vulkan_render_device(GLFWwindow* window)
{
    return std::make_unique<VulkanRenderDevice>(window);
}

} // namespace mosaic::render::vk
