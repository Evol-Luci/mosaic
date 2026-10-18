#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include "engine/render/mip_levels.hpp"
#include "engine/render/render_device.hpp"
#include "modules/render_vulkan/vulkan_render_device.hpp"
#include "modules/render_vulkan/vulkan_bloom_levels.hpp"
#include "modules/render_vulkan/vulkan_swapchain_support.hpp"
#include "foundation/logging/log.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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
    // xyz world space. .w is the IBL enable flag -- NOT unused padding any more. A
    // future field that takes this slot silently disables image-based lighting.
    float camera_position[4];
    float light_view_proj[16];
    // Exposure in .x, .yzw unused. APPENDED at the end rather than grouped with
    // camera_position where it belongs logically, and not packed into one of the
    // documented-unused .w slots above, for two separate reasons.
    //
    // Not a .w slot: camera_position.w is currently demonstrating what that costs. It
    // holds the IBL-enable flag, and the comment above it had to warn that a future field
    // claiming the slot would silently disable image-based lighting. This slice IS that
    // future field, so it does not take one.
    //
    // At the END: the assertion below checks the struct's TOTAL SIZE and never its field
    // offsets. Inserting mid-struct would shift light_view_proj's offset, and if only one
    // side of the C++/GLSL boundary were updated the assertion could not see it -- the
    // symptom would be a wrong shadow matrix, not a failed build. Appending cannot shift
    // anything that already exists.
    float exposure[4];
};
static_assert(sizeof(FrameLightingUbo) == 208, "UBO layout must match the shader block");
// Offsets, not just the total size -- because the size assertion above cannot see a
// reordering. Move `exposure` from the end to just after `camera_position` and leave the
// GLSL alone: sizeof stays 208, every GLSL-side contract test stays green, and the shader
// then reads light_view_proj's first row as the exposure scalar and the real exposure as
// the matrix's last row. A wrong brightness AND a wrong shadow projection, from a change
// whose only intent was tidying field grouping.
//
// This is cheap only because FrameLightingUbo is standard-layout (every member is a float
// array), so offsetof is well-defined, and because these assertions live in the same
// translation unit as the struct. The shader-side contract test's earlier claim that the
// C++ half needed generated code to pin was wrong; found in review 2026-10-07.
static_assert(offsetof(FrameLightingUbo, light_direction) == 0);
static_assert(offsetof(FrameLightingUbo, light_color) == 16);
static_assert(offsetof(FrameLightingUbo, point_position) == 32);
static_assert(offsetof(FrameLightingUbo, point_color) == 48);
static_assert(offsetof(FrameLightingUbo, spot_position) == 64);
static_assert(offsetof(FrameLightingUbo, spot_direction) == 80);
static_assert(offsetof(FrameLightingUbo, spot_color) == 96);
static_assert(offsetof(FrameLightingUbo, camera_position) == 112);
static_assert(offsetof(FrameLightingUbo, light_view_proj) == 128);
static_assert(offsetof(FrameLightingUbo, exposure) == 192);

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
    // The skybox cannot read exposure from the per-frame UBO: it does not bind that set at
    // all (its set 0 is the environment cubemap) and takes everything else through push
    // constants. It must scale by the SAME value the lit pass does, or the sky and the
    // geometry drift apart in brightness -- which reads as an authoring complaint rather
    // than a bug.
    float exposure;
};
static_assert(sizeof(SkyboxPush) == 76, "skybox push layout must match skybox.frag");

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
    // Sub-region UVs: u0, v0, u1, v1, resolved on the CPU by engine/ui/ui_fit.hpp. Mirrors
    // UiTextPush below, which has carried a uv[4] for glyph sub-rects since the text slice
    // -- this is the same mechanism, not a new one. Declared BETWEEN rect and tint to match
    // the shader's block member order, which is what the sizes and offsets must agree on.
    float uv[4];
    float tint[4];
};
static_assert(sizeof(UiImagePush) == 48, "ui-image push constant must match the shader");

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
        create_hdr_render_pass();
        create_bloom_render_pass();
        create_framebuffers();
        // SWAPCHAIN-SIZED: the scene renders once at full resolution and the editor samples
        // the resolved result into whatever panel size it has.
        create_hdr_target(extent_.width, extent_.height);
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
        create_resolve_pipeline();
        create_bloom_pipelines();
        create_ui_pipeline();
        create_ui_image_pipeline();
        create_ui_text_pipeline();
        create_command_pool_and_buffers();
        // AFTER the command pool: uploading the stand-ins goes through
        // begin_single_time_commands, which needs it. Placing this earlier segfaulted
        // with "commandPool must be a valid VkCommandPool handle".
        create_default_ibl_resources();
        // AFTER the command pool for THE SAME REASON, and it cost the same segfault to
        // learn twice: the chain is cleared and transitioned once at creation, through
        // begin_single_time_commands. It sat beside create_hdr_target at first -- that line
        // is the obvious template for a swapchain-sized resource, and it is ABOVE the pool,
        // because the HDR target submits nothing.
        //
        // create_bloom_pipelines ran earlier and allocated the N+1 descriptor sets, but
        // could only write the HDR view: the mip views did not exist yet. Hence the second
        // write here, the same shape write_hdr_descriptor uses -- harmless before the
        // resource exists, called again once it does.
        create_bloom_chain(extent_.width, extent_.height);
        write_bloom_descriptors();
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
            destroy_hdr_target();
            destroy_bloom_chain();
            if (bloom_render_pass_ != VK_NULL_HANDLE)
            {
                vkDestroyRenderPass(device_, bloom_render_pass_, nullptr);
            }
            if (resolve_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, resolve_pipeline_, nullptr); }
            if (vp_resolve_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, vp_resolve_pipeline_, nullptr); }
            if (resolve_pipeline_layout_ != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, resolve_pipeline_layout_, nullptr); }
            if (bloom_prefilter_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, bloom_prefilter_pipeline_, nullptr); }
            if (bloom_down_pipeline_ != VK_NULL_HANDLE)      { vkDestroyPipeline(device_, bloom_down_pipeline_, nullptr); }
            if (bloom_up_pipeline_ != VK_NULL_HANDLE)        { vkDestroyPipeline(device_, bloom_up_pipeline_, nullptr); }
            if (bloom_pipeline_layout_ != VK_NULL_HANDLE)    { vkDestroyPipelineLayout(device_, bloom_pipeline_layout_, nullptr); }
            // Frees every bloom set with it -- the pool owns them.
            if (bloom_descriptor_pool_ != VK_NULL_HANDLE)    { vkDestroyDescriptorPool(device_, bloom_descriptor_pool_, nullptr); }
            // Frees hdr_set_ with it -- the pool owns the set, and nothing else allocates here.
            if (hdr_descriptor_pool_ != VK_NULL_HANDLE) { vkDestroyDescriptorPool(device_, hdr_descriptor_pool_, nullptr); }
            if (hdr_sampler_ != VK_NULL_HANDLE) { vkDestroySampler(device_, hdr_sampler_, nullptr); }
            if (hdr_render_pass_ != VK_NULL_HANDLE)
            {
                vkDestroyRenderPass(device_, hdr_render_pass_, nullptr);
            }
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
            if (ibl_set_layout_ != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(device_, ibl_set_layout_, nullptr);
                ibl_set_layout_ = VK_NULL_HANDLE;
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

            if (particle_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, particle_pipeline_, nullptr); }
            if (particle_pipeline_layout_ != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, particle_pipeline_layout_, nullptr); }
            if (ribbon_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, ribbon_pipeline_, nullptr); }
            if (ribbon_pipeline_layout_ != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, ribbon_pipeline_layout_, nullptr); }
            if (world_text_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, world_text_pipeline_, nullptr); }
            if (world_text_pipeline_layout_ != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, world_text_pipeline_layout_, nullptr); }
            if (decal_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, decal_pipeline_, nullptr); }
            if (decal_pipeline_layout_ != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, decal_pipeline_layout_, nullptr); }
            if (screen_effect_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, screen_effect_pipeline_, nullptr); }
            if (skybox_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, skybox_pipeline_, nullptr); skybox_pipeline_ = VK_NULL_HANDLE; }
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

    // A cubemap WITH a mip chain, for the prefiltered-specular cube whose levels are
    // convolutions at increasing roughness. Deliberately does NOT reuse sub-slice 4a's
    // blit ladder: blitting the base down would replace five convolutions with a blur
    // that still looks plausible, which is the worst kind of wrong.
    mosaic::engine::render::TextureHandle
    create_cubemap(const std::vector<std::array<mosaic::engine::render::Image, 6>>& levels) override
    {
        if (levels.empty()) { return 0u; }
        const auto& base = levels[0][0];
        if (base.pixels.empty() || base.width == 0 || base.width != base.height) { return 0u; }
        // A chain cannot be longer than the number of halvings the base allows, or
        // mipLevels violates VUID-VkImageCreateInfo-mipLevels-00958.
        if (levels.size() > mosaic::engine::render::mip_levels_for(base.width, base.width))
        {
            mosaic::foundation::log::error(
                "render", "cubemap chain has {} levels but a {}-wide base allows at most {}",
                levels.size(), base.width,
                mosaic::engine::render::mip_levels_for(base.width, base.width));
            return 0u;
        }
        if (textures_.size() >= kMaxTextures)
        {
            mosaic::foundation::log::error(
                "render", "texture cap reached ({}); raise kMaxTextures", kMaxTextures);
            return 0u;
        }

        // Every level checked against the size its position implies, and against the
        // stride its own format implies. A level of the wrong size sizes the staging
        // buffer differently from what the copy regions read -- an overrun, which
        // validation does not reliably catch.
        VkDeviceSize total = 0;
        for (std::size_t m = 0; m < levels.size(); ++m)
        {
            const std::uint32_t want =
                mosaic::engine::render::mip_extent_for(base.width, static_cast<std::uint32_t>(m));
            for (const auto& f : levels[m])
            {
                if (f.width != want || f.height != want || f.format != base.format
                    || f.pixels.size() != static_cast<std::size_t>(want) * want
                                              * mosaic::engine::render::bytes_per_pixel(f.format))
                {
                    mosaic::foundation::log::error(
                        "render", "cubemap mip level {} is not {}x{} in the base format", m,
                        want, want);
                    return 0u;
                }
                total += static_cast<VkDeviceSize>(f.pixels.size());
            }
        }

        const VkFormat fmt = (base.format == mosaic::engine::render::ImageFormat::Rgba16F)
                                 ? VK_FORMAT_R16G16B16A16_SFLOAT
                                 : VK_FORMAT_R8G8B8A8_SRGB;
        Texture tex{};
        tex.is_cube = true;

        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory staging_mem = VK_NULL_HANDLE;
        create_buffer(total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      staging, staging_mem);
        void* mapped = nullptr;
        vk_check(vkMapMemory(device_, staging_mem, 0, total, 0, &mapped), "vkMapMemory(cube chain)");
        std::vector<VkBufferImageCopy> copies;
        copies.reserve(levels.size() * 6);
        VkDeviceSize offset = 0;
        for (std::size_t m = 0; m < levels.size(); ++m)
        {
            const std::uint32_t sz =
                mosaic::engine::render::mip_extent_for(base.width, static_cast<std::uint32_t>(m));
            for (std::uint32_t face = 0; face < 6; ++face)
            {
                const auto& img = levels[m][face];
                std::memcpy(static_cast<std::uint8_t*>(mapped) + offset, img.pixels.data(),
                            img.pixels.size());
                VkBufferImageCopy copy{};
                copy.bufferOffset = offset;
                copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copy.imageSubresource.mipLevel = static_cast<std::uint32_t>(m);
                copy.imageSubresource.baseArrayLayer = face;
                copy.imageSubresource.layerCount = 1;
                copy.imageOffset = {0, 0, 0};
                copy.imageExtent = {sz, sz, 1};
                copies.push_back(copy);
                offset += static_cast<VkDeviceSize>(img.pixels.size());
            }
        }
        vkUnmapMemory(device_, staging_mem);

        VkImageCreateInfo chain{};
        chain.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        chain.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        chain.imageType = VK_IMAGE_TYPE_2D;
        chain.extent = {base.width, base.height, 1};
        chain.mipLevels = static_cast<std::uint32_t>(levels.size());
        chain.arrayLayers = 6;
        chain.format = fmt;
        chain.tiling = VK_IMAGE_TILING_OPTIMAL;
        chain.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        chain.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        chain.samples = VK_SAMPLE_COUNT_1_BIT;
        chain.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vk_check(vkCreateImage(device_, &chain, nullptr, &tex.image), "vkCreateImage(cube chain)");

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(device_, tex.image, &req);
        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex =
            find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vk_check(vkAllocateMemory(device_, &alloc, nullptr, &tex.memory),
                 "vkAllocateMemory(cube chain)");
        vk_check(vkBindImageMemory(device_, tex.image, tex.memory, 0),
                 "vkBindImageMemory(cube chain)");

        VkCommandBuffer cmd = begin_single_time_commands();
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = tex.image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        // ALL levels and ALL layers together: a level left out is sampled in the wrong
        // layout, which is undefined contents plus a validation error.
        barrier.subresourceRange.levelCount = static_cast<std::uint32_t>(levels.size());
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 6;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &barrier);
        vkCmdCopyBufferToImage(cmd, staging, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<std::uint32_t>(copies.size()), copies.data());
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
                             1, &barrier);
        end_single_time_commands(cmd);
        vkDestroyBuffer(device_, staging, nullptr);
        vkFreeMemory(device_, staging_mem, nullptr);

        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = tex.image;
        view.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        view.format = fmt;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.baseMipLevel = 0;
        view.subresourceRange.levelCount = static_cast<std::uint32_t>(levels.size());
        view.subresourceRange.baseArrayLayer = 0;
        view.subresourceRange.layerCount = 6;
        vk_check(vkCreateImageView(device_, &view, nullptr, &tex.view),
                 "vkCreateImageView(cube chain)");

        VkSamplerCreateInfo sampler{};
        sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler.magFilter = VK_FILTER_LINEAR;
        sampler.minFilter = VK_FILTER_LINEAR;
        sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.anisotropyEnable = VK_FALSE;
        sampler.maxAnisotropy = 1.0f;
        sampler.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        sampler.unnormalizedCoordinates = VK_FALSE;
        sampler.compareEnable = VK_FALSE;
        sampler.compareOp = VK_COMPARE_OP_ALWAYS;
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler.mipLodBias = 0.0f;
        sampler.minLod = 0.0f;
        // The shader indexes roughness across the chain with textureLod, so every level
        // must be reachable.
        sampler.maxLod = VK_LOD_CLAMP_NONE;
        vk_check(vkCreateSampler(device_, &sampler, nullptr, &tex.sampler),
                 "vkCreateSampler(cube chain)");

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
        // .w carries the IBL enable flag in this vec4's documented padding, so the UBO
        // does not grow by 16 bytes for one boolean. Written here rather than in
        // set_image_based_lighting because the UBO is rewritten every frame anyway.
        ubo.camera_position[3] = ibl_enabled_ ? 1.0f : 0.0f;
        ubo.camera_position[0] = fl.camera_position.x;
        ubo.camera_position[1] = fl.camera_position.y;
        ubo.camera_position[2] = fl.camera_position.z;
        // .yzw stay zero from the struct's value-initialisation above, matching how every
        // other documented-unused slot here is left alone.
        ubo.exposure[0] = fl.exposure;
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
        // World space records into the HDR scene pass and nowhere else. RenderSystem opens
        // it before any of these; if it is not open there is no linear target to draw
        // into, and recording here would land in the destination pass with the curve
        // already applied to everything behind it.
        if (!in_hdr_pass_) { return; }
        // The HDR pass bound the scene pipeline when it opened; this draw inherits it, the
        // way it used to inherit the one ensure_swapchain_pass bound.
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
            // Set 3: image-based lighting. Always bound -- create_default_ibl_resources
            // points it at 1x1 stand-ins at startup, because the shader samples these
            // unconditionally and an unbound descriptor is undefined.
            vkCmdBindDescriptorSets(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_layout_, 3, 1, &ibl_set_, 0, nullptr);
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
        // World space records into the HDR scene pass and nowhere else. RenderSystem opens
        // it before any of these; if it is not open there is no linear target to draw
        // into, and recording here would land in the destination pass with the curve
        // already applied to everything behind it.
        if (!in_hdr_pass_) { return; }
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
                          particle_pipeline_);

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
        // World space records into the HDR scene pass and nowhere else. RenderSystem opens
        // it before any of these; if it is not open there is no linear target to draw
        // into, and recording here would land in the destination pass with the curve
        // already applied to everything behind it.
        if (!in_hdr_pass_) { return; }
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
                          ribbon_pipeline_);

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
        // World space records into the HDR scene pass and nowhere else. RenderSystem opens
        // it before any of these; if it is not open there is no linear target to draw
        // into, and recording here would land in the destination pass with the curve
        // already applied to everything behind it.
        if (!in_hdr_pass_) { return; }
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
                          world_text_pipeline_);
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
        // World space records into the HDR scene pass and nowhere else. RenderSystem opens
        // it before any of these; if it is not open there is no linear target to draw
        // into, and recording here would land in the destination pass with the curve
        // already applied to everything behind it.
        if (!in_hdr_pass_) { return; }
        if (frame_skipped_ || decals.empty() || textures_.empty()) { return; }

        vkCmdBindPipeline(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                          decal_pipeline_);

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

    void set_image_based_lighting(mosaic::engine::render::TextureHandle irradiance,
                                  mosaic::engine::render::TextureHandle prefiltered,
                                  mosaic::engine::render::TextureHandle brdf_lut) override
    {
        auto valid = [&](mosaic::engine::render::TextureHandle h) {
            return h != 0 && h <= textures_.size();
        };
        if (!valid(irradiance) || !valid(prefiltered) || !valid(brdf_lut))
        {
            mosaic::foundation::log::error(
                "render", "set_image_based_lighting given an invalid handle; IBL stays off");
            return;
        }
        if (ibl_sets_allocated_ >= kMaxIblSets)
        {
            // Keeping the previous set is safe and visible: the environment simply does
            // not change. Rewriting the live one would be the unsafe alternative.
            mosaic::foundation::log::error(
                "render", "IBL set cap reached ({}); keeping the previous environment",
                kMaxIblSets);
            return;
        }

        // A FRESH set, never a rewrite of the live one -- see ibl_set_layout_'s comment.
        VkDescriptorSetAllocateInfo ibl_alloc{};
        ibl_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ibl_alloc.descriptorPool = descriptor_pool_;
        ibl_alloc.descriptorSetCount = 1;
        ibl_alloc.pSetLayouts = &ibl_set_layout_;
        VkDescriptorSet fresh_set = VK_NULL_HANDLE;
        vk_check(vkAllocateDescriptorSets(device_, &ibl_alloc, &fresh_set),
                 "vkAllocateDescriptorSets(ibl)");
        ++ibl_sets_allocated_;

        const mosaic::engine::render::TextureHandle handles[3] = {irradiance, prefiltered,
                                                                  brdf_lut};
        VkDescriptorImageInfo infos[3]{};
        VkWriteDescriptorSet writes[3]{};
        for (int i = 0; i < 3; ++i)
        {
            const Texture& t = textures_[handles[i] - 1];
            infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            infos[i].imageView = t.view;
            infos[i].sampler = t.sampler;
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = fresh_set;
            writes[i].dstBinding = static_cast<std::uint32_t>(i);
            writes[i].dstArrayElement = 0;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].descriptorCount = 1;
            writes[i].pImageInfo = &infos[i];
        }
        vkUpdateDescriptorSets(device_, 3, writes, 0, nullptr);
        ibl_set_ = fresh_set;
        ibl_enabled_ = true;
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
        // World space records into the HDR scene pass and nowhere else. RenderSystem opens
        // it before any of these; if it is not open there is no linear target to draw
        // into, and recording here would land in the destination pass with the curve
        // already applied to everything behind it.
        if (!in_hdr_pass_) { return; }
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
                          skybox_pipeline_);
        vkCmdBindDescriptorSets(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                skybox_pipeline_layout_, 0, 1, &textures_[index].set, 0, nullptr);

        SkyboxPush push{};
        std::memcpy(push.inv_view_rotation, params.inverse_view_rotation.m.data(),
                    sizeof(push.inv_view_rotation));
        push.tan_half_fov_y = params.tan_half_fov_y;
        push.aspect = params.aspect;
        push.exposure = params.exposure;
        vkCmdPushConstants(cmd_[frame_], skybox_pipeline_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(push), &push);

        // Three vertices, no buffers: the vertex shader builds a fullscreen triangle
        // from gl_VertexIndex.
        vkCmdDraw(cmd_[frame_], 3, 1, 0, 0);
    }

    void draw_screen_effect(const mosaic::engine::render::ScreenEffectParams& params) override
    {
        // INSIDE the HDR pass, deliberately: the vignette attenuates light, which is what
        // a real lens does before exposure. Outside the resolve it would be a post-hoc
        // darkening of the already-mapped image -- defensible, and recorded in the spec as
        // the alternative, but not the physically coherent place.
        // World space records into the HDR scene pass and nowhere else. RenderSystem opens
        // it before any of these; if it is not open there is no linear target to draw
        // into, and recording here would land in the destination pass with the curve
        // already applied to everything behind it.
        if (!in_hdr_pass_) { return; }
        if (frame_skipped_) { return; }

        vkCmdBindPipeline(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                          screen_effect_pipeline_);

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
        ensure_destination_pass();
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
        ensure_destination_pass();
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
            pc.uv[0] = q.u0; pc.uv[1] = q.v0; pc.uv[2] = q.u1; pc.uv[3] = q.v1;
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
        ensure_destination_pass();
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
        viewport_pass_requested_ = false;
        in_hdr_pass_ = false;
    }

    // Begins the swapchain render pass once per frame (idempotent). Sets the
    // negative-height (Y-flip) viewport/scissor. A skipped frame (swapchain out-of-date)
    // never opens a pass. Binds nothing: world space does not draw here any more, and
    // every method that does binds its own pipeline.
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
        // NO pipeline bound, the same change ensure_viewport_pass took. `pipeline_` is
        // built against hdr_render_pass_ now, which is NOT render-pass-compatible with
        // this one -- the attachment formats differ, R16G16B16A16_SFLOAT against
        // B8G8R8A8_SRGB. Binding it here is inert only for as long as every draw in this
        // pass binds its own first, which all four do today; the next one that inherits
        // instead gets undefined rendering. Found in review 2026-10-16.
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

    // REQUESTS the offscreen viewport pass; it is opened lazily, exactly as the swapchain
    // pass has been since the editor path landed. It cannot be opened here any more: the
    // editor wraps the WHOLE of RenderSystem::render between begin_viewport_pass and
    // end_viewport_pass, and render() now opens the HDR scene pass inside that. A render
    // pass cannot begin while another is recording, so an eager begin here would nest one
    // inside the other -- a validation error on the first editor frame.
    //
    // Lazy instead of suspend/resume because the file already had the idiom:
    // ensure_swapchain_pass() exists for the same reason, and one rule for both
    // destinations is what keeps the window and editor paths on one code path.
    void begin_viewport_pass(const Color& clear) override
    {
        if (frame_skipped_ || vp_framebuffer_ == VK_NULL_HANDLE)
        {
            return;
        }
        vp_clear_ = clear;
        viewport_pass_requested_ = true;
    }

    // Opens the requested viewport pass once per frame (idempotent), the mirror of
    // ensure_swapchain_pass. No scene pipeline is bound: nothing world-space draws here
    // any more, only the resolve and the UI, and each binds its own.
    void ensure_viewport_pass()
    {
        if (viewport_pass_open_ || !viewport_pass_requested_ || frame_skipped_ ||
            vp_framebuffer_ == VK_NULL_HANDLE)
        {
            return;
        }
        const Color clear = vp_clear_;

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
        viewport_pass_open_ = true;
    }

    void end_viewport_pass() override
    {
        // Open it if nothing did. An editor frame that drew nothing must still clear the
        // panel target and leave it in SHADER_READ_ONLY_OPTIMAL for ImGui to composite;
        // skipping the pass entirely would leave the previous frame's image, or an
        // undefined one on the first frame. end_frame() does the same for the swapchain.
        ensure_viewport_pass();
        viewport_pass_requested_ = false;
        if (!viewport_pass_open_)
        {
            return;
        }
        vkCmdEndRenderPass(cmd_[frame_]);
        viewport_pass_open_ = false;
    }

    // Opens whichever pass this frame's destination draws land in: the editor's offscreen
    // panel target when begin_viewport_pass asked for one, the swapchain otherwise. The
    // resolve and the three UI draws call this; world-space draws do not, because they go
    // into the HDR pass instead.
    // Mirrors hdr_resolve.frag's push block byte for byte. vec2 first: it has the larger
    // alignment, so putting the float ahead of it would cost four bytes of padding and,
    // worse, make the two layouts disagree the moment someone reorders one side.
    struct ResolvePush
    {
        float inv_dest[2];
        float exposure;
        float bloom_intensity;
    };

    // Mirrors bloom_prefilter.frag and bloom_blur.frag byte for byte -- ONE block for all
    // three bloom pipelines, so they share one pipeline layout. The two vec2s come first
    // for the alignment reason ResolvePush gives; threshold and knee are read only by the
    // prefilter, and declared by the blur so the layouts cannot disagree.
    struct BloomPush
    {
        float inv_dest[2];
        float inv_src[2];
        float threshold;
        float knee;
    };

    void ensure_destination_pass()
    {
        if (viewport_pass_requested_) { ensure_viewport_pass(); }
        else                          { ensure_swapchain_pass(); }
    }

    void begin_hdr_scene_pass(const Color& clear) override
    {
        if (frame_skipped_ || in_hdr_pass_ || hdr_framebuffer_ == VK_NULL_HANDLE)
        {
            return;
        }

        // Negative height, matching the swapchain and viewport passes: the scene's
        // projection math assumes the Y-flip. hdr_resolve.frag says what a screen-space
        // shader must do about it, and why it takes its UV from gl_FragCoord.
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = static_cast<float>(hdr_extent_.height);
        viewport.width = static_cast<float>(hdr_extent_.width);
        viewport.height = -static_cast<float>(hdr_extent_.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd_[frame_], 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = hdr_extent_;
        vkCmdSetScissor(cmd_[frame_], 0, 1, &scissor);

        std::array<VkClearValue, 2> clears{};
        clears[0].color = {{clear.r, clear.g, clear.b, clear.a}};
        clears[1].depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = hdr_render_pass_;
        rp.framebuffer = hdr_framebuffer_;
        rp.renderArea.offset = {0, 0};
        rp.renderArea.extent = hdr_extent_;
        rp.clearValueCount = static_cast<uint32_t>(clears.size());
        rp.pClearValues = clears.data();
        vkCmdBeginRenderPass(cmd_[frame_], &rp, VK_SUBPASS_CONTENTS_INLINE);

        // Binds the scene pipeline, as ensure_swapchain_pass used to: draw() is the one
        // recording method that does NOT bind its own, so it inherits whatever the pass
        // left bound. Every other world-space draw binds before it draws.
        vkCmdBindPipeline(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        in_hdr_pass_ = true;
    }

    void end_hdr_scene_pass() override
    {
        // Guarded both ways: a double-begin or an end without a begin is a validation
        // error rather than a visible bug, which is the kind that only appears on a real
        // device and never in the suite.
        if (!in_hdr_pass_) { return; }
        vkCmdEndRenderPass(cmd_[frame_]);
        in_hdr_pass_ = false;
    }

    void build_bloom(const mosaic::engine::render::BloomParams& params) override
    {
        // Opens bloom_levels_ render passes of its own, and vkCmdBeginRenderPass inside a
        // pass is a validation error. RenderSystem calls this between end_hdr_scene_pass
        // and draw_hdr_resolve, a window where nothing is recording -- but the viewport
        // pass is LAZILY opened, so that is a property of today's call order rather than of
        // the API, and cheap to guarantee here.
        if (frame_skipped_ || in_hdr_pass_ || swapchain_pass_open_ || viewport_pass_open_)
        {
            return;
        }
        if (bloom_levels_ == 0 || bloom_prefilter_pipeline_ == VK_NULL_HANDLE)
        {
            return;
        }

        // Prefilter: the HDR scene -> mip 0, with the threshold and knee applied on the
        // way. It IS the first downsample; a full-resolution bright-pass would cost a
        // 1920x1080 target for a result that is immediately halved.
        record_bloom_pass(bloom_prefilter_pipeline_, /*dest=*/0, /*source_set=*/0,
                          /*src_extent=*/hdr_extent_, params);

        // Downsample: mip i -> mip i+1. Written `i + 1 < bloom_levels_` rather than
        // `i < bloom_levels_ - 1` because bloom_levels_ is UNSIGNED and can be 1, where the
        // subtraction wraps.
        for (uint32_t i = 0; i + 1 < bloom_levels_; ++i)
        {
            record_bloom_pass(bloom_down_pipeline_, /*dest=*/i + 1, /*source_set=*/i + 1,
                              bloom_extents_[i], params);
        }

        // Upsample, smallest first: mip i+1 -> mip i, ADDING into what the downsample left
        // there. Descending with `i-- > 0` so the unsigned counter never has to hold -1.
        for (uint32_t i = bloom_levels_ - 1; i-- > 0;)
        {
            record_bloom_pass(bloom_up_pipeline_, /*dest=*/i, /*source_set=*/i + 2,
                              bloom_extents_[i + 1], params);
        }
        // Mip 0 is now the finished glow, and the resolve's set 1 points at it.
    }

    // One step of the chain: open the pass on `dest`'s framebuffer, bind the pipeline and
    // the source set, push the two reciprocal extents, draw the triangle, close.
    //
    // `source_set` indexes bloom_sets_, where 0 is the HDR view and i+1 is mip i.
    void record_bloom_pass(VkPipeline pipe, uint32_t dest, uint32_t source_set,
                           VkExtent2D src_extent,
                           const mosaic::engine::render::BloomParams& params)
    {
        const VkExtent2D dst_extent = bloom_extents_[dest];

        // POSITIVE height, unlike every other pass in this backend. Nothing here uses a
        // projection matrix, and every bloom shader takes its uv from gl_FragCoord -- which
        // is framebuffer space and unaffected by the viewport's sign. The Y-flip would
        // therefore change nothing, and a negative height that changes nothing is a line
        // the next reader has to prove harmless.
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(dst_extent.width);
        viewport.height = static_cast<float>(dst_extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd_[frame_], 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = dst_extent;
        vkCmdSetScissor(cmd_[frame_], 0, 1, &scissor);

        VkRenderPassBeginInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = bloom_render_pass_;
        rp.framebuffer = bloom_fbs_[dest];
        rp.renderArea.offset = {0, 0};
        rp.renderArea.extent = dst_extent;
        // No clear values: the pass is LOAD_OP_LOAD, because the upsample adds into what is
        // already there.
        vkCmdBeginRenderPass(cmd_[frame_], &rp, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        vkCmdBindDescriptorSets(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                bloom_pipeline_layout_, 0, 1, &bloom_sets_[source_set],
                                0, nullptr);

        BloomPush pc{};
        // inv_dest turns gl_FragCoord into uv; inv_src sizes the kernel's tap offsets.
        // Reciprocals rather than the extents, because both are used per fragment and the
        // division is not.
        pc.inv_dest[0] = 1.0f / static_cast<float>(dst_extent.width);
        pc.inv_dest[1] = 1.0f / static_cast<float>(dst_extent.height);
        pc.inv_src[0] = 1.0f / static_cast<float>(src_extent.width);
        pc.inv_src[1] = 1.0f / static_cast<float>(src_extent.height);
        pc.threshold = params.threshold;
        pc.knee = params.knee;
        vkCmdPushConstants(cmd_[frame_], bloom_pipeline_layout_,
                           VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

        vkCmdDraw(cmd_[frame_], 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd_[frame_]);
    }

    void draw_hdr_resolve(float exposure, float bloom_intensity) override
    {
        // Opens the DESTINATION pass -- the swapchain, or the editor's panel target. The
        // resolve never begins a pass of its own: it is a fullscreen triangle into
        // whatever is recording, which is what keeps the window and editor on one code
        // path. Had it been a pass, it would have had to know its destination.
        ensure_destination_pass();
        if (frame_skipped_ || hdr_set_ == VK_NULL_HANDLE) { return; }

        // The same selector the UI draws use, for the same reason: the destination pass
        // differs between the two paths, and a pipeline must be compatible with the pass
        // it draws in.
        VkPipeline pipe = viewport_pass_open_ ? vp_resolve_pipeline_ : resolve_pipeline_;
        if (pipe == VK_NULL_HANDLE) { return; }
        vkCmdBindPipeline(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        // TWO sets: the scene at 0, the bloom chain's mip 0 at 1. hdr_resolve.frag samples
        // both unconditionally, so slot 1 must be bound even when there is no chain -- a
        // minimised window gives bloom_levels_ == 0, and an unbound set is a validation
        // error and on some drivers a crash. The HDR set stands in there; the intensity is
        // 0 on that path anyway, so what it reads is multiplied away.
        const VkDescriptorSet bloom_src =
            (bloom_sets_.empty() || bloom_levels_ == 0) ? hdr_set_ : bloom_sets_[1];
        const VkDescriptorSet sets[2] = {hdr_set_, bloom_src};
        vkCmdBindDescriptorSets(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                resolve_pipeline_layout_, 0, 2, sets, 0, nullptr);

        // The DESTINATION's extent, not the HDR target's. They differ in the editor: the
        // scene target is swapchain-sized and the panel is whatever the user dragged it
        // to, and gl_FragCoord spans the destination. Normalising by the source instead
        // crops the top-left of the render rather than scaling it, which `mosaic run
        // --window` can never show because there the two are equal. Same selector as the
        // pipeline above, deliberately: one question answered once.
        const VkExtent2D dest = viewport_pass_open_ ? vp_extent_ : extent_;
        if (dest.width == 0 || dest.height == 0) { return; }
        ResolvePush pc{};
        pc.inv_dest[0] = 1.0f / static_cast<float>(dest.width);
        pc.inv_dest[1] = 1.0f / static_cast<float>(dest.height);
        pc.exposure = exposure;
        pc.bloom_intensity = bloom_intensity;
        vkCmdPushConstants(cmd_[frame_], resolve_pipeline_layout_,
                           VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDraw(cmd_[frame_], 3, 1, 0, 0); // fullscreen triangle
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
        // SYNCHRONIZATION VALIDATION IS A SEPARATE OPT-IN, and it is off unless asked for.
        //
        // Enabling VK_LAYER_KHRONOS_validation gets API-legality checking and nothing
        // about barriers. For four render slices this project's gate was "ctest plus a
        // validation-layer grep on both entry points", and that grep could not have
        // reported a wrong barrier, a missing subpass dependency, or an intra-frame
        // read-after-write on one image -- it was silent, not clean. Found by the bloom
        // branch review on 2026-10-17, after bloom's own subpass dependency had to be
        // widened on reasoning alone because nothing automated could confirm it.
        //
        // Opt-in rather than always-on because it is genuinely expensive, not a free flag.
        // The point is that it is now REACHABLE, which it previously was not at any price.
        const char* sync_env = std::getenv("MOSAIC_VK_SYNC_VALIDATION");
        const bool  want_sync =
            sync_env != nullptr && sync_env[0] != '\0' && std::strcmp(sync_env, "0") != 0;

        const VkValidationFeatureEnableEXT sync_feature =
            VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
        VkValidationFeaturesEXT features{};
        features.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
        features.enabledValidationFeatureCount = 1;
        features.pEnabledValidationFeatures = &sync_feature;

        if (validation_layer_available(validation_layer))
        {
            ci.enabledLayerCount = 1;
            ci.ppEnabledLayerNames = &validation_layer;
            // Chaining the messenger info covers instance create/destroy too.
            populate_debug_info(dbg);
            if (want_sync)
            {
                // A CHAIN, not a replacement: overwriting ci.pNext would drop the
                // messenger and silently lose every message it covers during instance
                // creation -- a net reduction in what the gate sees, while appearing to
                // add to it.
                features.pNext = &dbg;
                ci.pNext = &features;
                // Logged so that a clean run PROVES WHAT WAS CHECKED. The failure this
                // guards against is not a missing check but a false claim about one:
                // "zero validation errors" was reported as evidence about synchronisation
                // while the feature was off. Now the same log the grep reads says so.
                mosaic::foundation::log::info(
                    "vulkan", "synchronization validation ENABLED "
                              "(MOSAIC_VK_SYNC_VALIDATION); expect it to be slower");
            }
            else
            {
                ci.pNext = &dbg;
            }
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
        //
        // deps[0]'s DESTINATION scope names the DEPTH stage and access as well as the
        // colour ones, and synchronization validation is what caught the difference.
        // vkCmdBeginRenderPass does two things to the depth attachment in order: the
        // UNDEFINED -> DEPTH_STENCIL_ATTACHMENT_OPTIMAL transition, which WRITES, and then
        // `loadOp = CLEAR`, which writes again. Naming only COLOR_ATTACHMENT_OUTPUT covers
        // the colour attachment and says nothing about depth, leaving those two unordered:
        // "WRITE_AFTER_WRITE hazard detected: attachment loadOp access is not synchronized
        // with the attachment layout transition", ten times a run.
        //
        // create_render_pass (the swapchain) already had this right; this follows it, and
        // a contract test now holds all three together.
        std::array<VkSubpassDependency, 2> deps{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
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

    // The HDR scene pass. A copy of create_offscreen_render_pass above with ONE changed
    // line -- the colour format -- because that is genuinely the only difference: same depth
    // attachment, same subpass, same two dependencies, and the second of those serves the
    // same purpose here (making the colour image visible to the fragment shader that samples
    // it, which is now the resolve rather than ImGui).
    void create_hdr_render_pass()
    {
        VkAttachmentDescription color{};
        // THE ONLY DIFFERENCE: a float format, so values above 1.0 survive the pass instead
        // of clamping at the attachment. That headroom is the whole point -- without it a
        // "bright" particle cannot be bright, and bloom has nothing to find.
        color.format = VK_FORMAT_R16G16B16A16_SFLOAT;
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
        // color image visible to the fragment shader sampling it in the resolve).
        //
        // deps[0]'s DESTINATION scope names the DEPTH stage and access as well as the
        // colour ones, and synchronization validation is what caught the difference.
        // vkCmdBeginRenderPass does two things to the depth attachment in order: the
        // UNDEFINED -> DEPTH_STENCIL_ATTACHMENT_OPTIMAL transition, which WRITES, and then
        // `loadOp = CLEAR`, which writes again. Naming only COLOR_ATTACHMENT_OUTPUT covers
        // the colour attachment and says nothing about depth, leaving those two unordered:
        // "WRITE_AFTER_WRITE hazard detected: attachment loadOp access is not synchronized
        // with the attachment layout transition", ten times a run.
        //
        // create_render_pass (the swapchain) already had this right; this follows it, and
        // a contract test now holds all three together.
        std::array<VkSubpassDependency, 2> deps{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
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

        vk_check(vkCreateRenderPass(device_, &ci, nullptr, &hdr_render_pass_),
                 "vkCreateRenderPass(hdr)");

        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = VK_FILTER_LINEAR;
        si.minFilter = VK_FILTER_LINEAR;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.maxLod = 1.0f;
        vk_check(vkCreateSampler(device_, &si, nullptr, &hdr_sampler_),
                 "vkCreateSampler(hdr)");
    }

    // The chain's one render pass, shared by all three bloom pipelines.
    //
    // LOAD, not CLEAR, because the upsample ADDS INTO the level it targets: clearing would
    // discard the downsampled content it is adding to, and the result would be only the
    // smallest mip smeared over the image. SHADER_READ_ONLY_OPTIMAL at both ends because
    // the next step samples what this one wrote -- and because the resolve samples mip 0
    // once the whole chain is done, with no barrier of its own.
    void create_bloom_render_pass()
    {
        VkAttachmentDescription color{};
        color.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference color_ref{};
        color_ref.attachment = 0;
        color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_ref;
        // NO depth attachment: every pass here is a fullscreen triangle with depth testing
        // off, and a depth buffer per mip would be six allocations for nothing.

        // Two dependencies, and deps[0]'s source scope names BOTH hazards this pass has
        // -- which is wider than the HDR pass's, deliberately.
        //
        // The HDR pass only ever follows a shader read of its target (the previous frame's
        // resolve), so FRAGMENT_SHADER/SHADER_READ is the whole story there. A bloom pass
        // is not in that position. The upsample targeting mip k LOADS and adds into what
        // the DOWNSAMPLE targeting mip k wrote, which is a COLOR_ATTACHMENT_WRITE: a
        // write-after-write and a load-after-write, not a write-after-read. Naming only
        // the shader-read scope would leave those covered solely by CHAINING through an
        // intermediate shader read that never happens for that subresource -- a valid but
        // extremely subtle reading of the spec, for a frame shape that occurs on every
        // frame once bloom_levels_ >= 2.
        //
        // Found by the whole-branch review, and the standard gate CANNOT see it: this
        // backend enables VK_LAYER_KHRONOS_validation with no VkValidationFeaturesEXT, so
        // synchronization validation is off and a clean run is silent here. Naming both
        // scopes costs nothing.
        std::array<VkSubpassDependency, 2> deps{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_SHADER_READ_BIT;
        deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        // READ as well as WRITE, and synchronization validation is what caught the
        // difference. vkCmdBeginRenderPass does two things here in order: the
        // initialLayout -> COLOR_ATTACHMENT_OPTIMAL transition, which WRITES, and then
        // `loadOp = LOAD`, which READS. Naming only the write leaves those two
        // unordered -- "READ_AFTER_WRITE hazard detected: attachment loadOp access is not
        // synchronized with the attachment layout transition", six times a frame, once per
        // mip.
        //
        // Only LOAD passes have this hazard, which is why no other pass in this backend
        // reports it. The source scope above was widened for a DIFFERENT gap in the same
        // dependency (the downsample's write before the upsample's load); two careful
        // readings missed this one, and the layer found it the first time it was asked.
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass = 0;
        deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = 1;
        ci.pAttachments = &color;
        ci.subpassCount = 1;
        ci.pSubpasses = &subpass;
        ci.dependencyCount = static_cast<uint32_t>(deps.size());
        ci.pDependencies = deps.data();
        vk_check(vkCreateRenderPass(device_, &ci, nullptr, &bloom_render_pass_),
                 "vkCreateRenderPass(bloom)");
    }

    void destroy_bloom_chain()
    {
        // ONE framebuffer and ONE view per mip. Destroying only the image would leak up to
        // six of each on every resize -- and a resize is something a user does by dragging
        // a window edge, so "every resize" means continuously.
        for (VkFramebuffer fb : bloom_fbs_)
        {
            if (fb != VK_NULL_HANDLE) { vkDestroyFramebuffer(device_, fb, nullptr); }
        }
        bloom_fbs_.clear();
        for (VkImageView v : bloom_views_)
        {
            if (v != VK_NULL_HANDLE) { vkDestroyImageView(device_, v, nullptr); }
        }
        bloom_views_.clear();
        bloom_extents_.clear();
        if (bloom_image_ != VK_NULL_HANDLE)
        {
            vkDestroyImage(device_, bloom_image_, nullptr);
            bloom_image_ = VK_NULL_HANDLE;
        }
        if (bloom_mem_ != VK_NULL_HANDLE)
        {
            vkFreeMemory(device_, bloom_mem_, nullptr);
            bloom_mem_ = VK_NULL_HANDLE;
        }
        bloom_levels_ = 0;
    }

    // `width` and `height` are the SWAPCHAIN's, not the chain's: one place owns the
    // halving, so two call sites cannot disagree about whether they already halved.
    void create_bloom_chain(uint32_t width, uint32_t height)
    {
        bloom_levels_ = mosaic::render::vk::bloom_level_count(width, height);
        if (bloom_levels_ == 0)
        {
            return; // a minimised window; the same thing create_hdr_target does
        }

        uint32_t w = width / 2 > 0 ? width / 2 : 1;
        uint32_t h = height / 2 > 0 ? height / 2 : 1;

        VkImageCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.extent = {w, h, 1};
        ci.mipLevels = bloom_levels_;
        ci.arrayLayers = 1;
        ci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        // TRANSFER_DST is for the one-time clear below, and is not optional: without it
        // vkCmdClearColorImage is a validation error at startup, on a real device, and
        // never in CI.
        ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                   VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vk_check(vkCreateImage(device_, &ci, nullptr, &bloom_image_),
                 "vkCreateImage(bloom)");
        bind_image_memory(bloom_image_, bloom_mem_, "bloom");

        for (uint32_t i = 0; i < bloom_levels_; ++i)
        {
            VkImageViewCreateInfo vi{};
            vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vi.image = bloom_image_;
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = VK_FORMAT_R16G16B16A16_SFLOAT;
            vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vi.subresourceRange.baseMipLevel = i;
            // ONE level per view, deliberately. A view spanning the whole chain would let
            // the sampler pick a mip by its own lod calculation, and every tap here must
            // read the exact level the loop meant.
            vi.subresourceRange.levelCount = 1;
            vi.subresourceRange.baseArrayLayer = 0;
            vi.subresourceRange.layerCount = 1;
            VkImageView view = VK_NULL_HANDLE;
            vk_check(vkCreateImageView(device_, &vi, nullptr, &view),
                     "vkCreateImageView(bloom mip)");
            bloom_views_.push_back(view);

            VkFramebufferCreateInfo fb{};
            fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fb.renderPass = bloom_render_pass_;
            fb.attachmentCount = 1;
            fb.pAttachments = &view;
            fb.width = w;
            fb.height = h;
            fb.layers = 1;
            VkFramebuffer framebuffer = VK_NULL_HANDLE;
            vk_check(vkCreateFramebuffer(device_, &fb, nullptr, &framebuffer),
                     "vkCreateFramebuffer(bloom mip)");
            bloom_fbs_.push_back(framebuffer);
            bloom_extents_.push_back(VkExtent2D{w, h});

            w = w / 2 > 0 ? w / 2 : 1;
            h = h / 2 > 0 ? h / 2 : 1;
        }

        // THE LAYOUT HAZARD, and it is invisible to the build and to the whole headless
        // suite: hdr_resolve.frag samples this image UNCONDITIONALLY, so on frame 1 -- and
        // on any frame after a skipped build -- it would otherwise read an image in
        // UNDEFINED layout. That is a validation error on a real device and nothing here.
        //
        // CLEARED as well as transitioned, which is not belt-and-braces. Freshly allocated
        // device memory can read as NaN, and NaN * 0.0 is NaN -- so the whole "intensity 0
        // contributes 0" argument needs this memory to genuinely be zero, not merely
        // readable.
        VkCommandBuffer cmd = begin_single_time_commands();

        VkImageSubresourceRange all{};
        all.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        all.baseMipLevel = 0;
        all.levelCount = bloom_levels_;
        all.baseArrayLayer = 0;
        all.layerCount = 1;

        VkImageMemoryBarrier to_dst{};
        to_dst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        to_dst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        to_dst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_dst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_dst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_dst.image = bloom_image_;
        to_dst.subresourceRange = all;
        to_dst.srcAccessMask = 0;
        to_dst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                             1, &to_dst);

        VkClearColorValue black{};
        black.float32[0] = 0.0f; black.float32[1] = 0.0f;
        black.float32[2] = 0.0f; black.float32[3] = 1.0f;
        vkCmdClearColorImage(cmd, bloom_image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             &black, 1, &all);

        VkImageMemoryBarrier to_read = to_dst;
        to_read.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        // EVERY mip, not just mip 0: the bloom render pass declares
        // SHADER_READ_ONLY_OPTIMAL as its initialLayout, so a mip that arrives in any
        // other layout is a validation error the first time the loop targets it.
        to_read.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        to_read.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_read.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &to_read);

        end_single_time_commands(cmd);
    }

    void destroy_hdr_target()
    {
        // No ImGui descriptor to remove -- that is the one thing destroy_offscreen_target
        // does that this must not copy, since nothing composites the HDR target directly.
        if (hdr_framebuffer_ != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(device_, hdr_framebuffer_, nullptr);
            hdr_framebuffer_ = VK_NULL_HANDLE;
        }
        if (hdr_color_view_ != VK_NULL_HANDLE) { vkDestroyImageView(device_, hdr_color_view_, nullptr); hdr_color_view_ = VK_NULL_HANDLE; }
        if (hdr_color_ != VK_NULL_HANDLE)      { vkDestroyImage(device_, hdr_color_, nullptr); hdr_color_ = VK_NULL_HANDLE; }
        if (hdr_color_mem_ != VK_NULL_HANDLE)  { vkFreeMemory(device_, hdr_color_mem_, nullptr); hdr_color_mem_ = VK_NULL_HANDLE; }
        if (hdr_depth_view_ != VK_NULL_HANDLE) { vkDestroyImageView(device_, hdr_depth_view_, nullptr); hdr_depth_view_ = VK_NULL_HANDLE; }
        if (hdr_depth_ != VK_NULL_HANDLE)      { vkDestroyImage(device_, hdr_depth_, nullptr); hdr_depth_ = VK_NULL_HANDLE; }
        if (hdr_depth_mem_ != VK_NULL_HANDLE)  { vkFreeMemory(device_, hdr_depth_mem_, nullptr); hdr_depth_mem_ = VK_NULL_HANDLE; }
        hdr_extent_ = {0, 0};
    }

    void create_hdr_target(uint32_t width, uint32_t height)
    {
        VkImageCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.extent = {width, height, 1};
        ci.mipLevels = 1;
        ci.arrayLayers = 1;
        ci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vk_check(vkCreateImage(device_, &ci, nullptr, &hdr_color_), "vkCreateImage(hdr color)");
        bind_image_memory(hdr_color_, hdr_color_mem_, "hdr color");
        hdr_color_view_ =
            make_image_view(hdr_color_, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);

        VkImageCreateInfo di = ci;
        di.format = kDepthFormat;
        di.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        vk_check(vkCreateImage(device_, &di, nullptr, &hdr_depth_), "vkCreateImage(hdr depth)");
        bind_image_memory(hdr_depth_, hdr_depth_mem_, "hdr depth");
        hdr_depth_view_ = make_image_view(hdr_depth_, kDepthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

        std::array<VkImageView, 2> attachments{hdr_color_view_, hdr_depth_view_};
        VkFramebufferCreateInfo fb{};
        fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass = hdr_render_pass_;
        fb.attachmentCount = static_cast<uint32_t>(attachments.size());
        fb.pAttachments = attachments.data();
        fb.width = width;
        fb.height = height;
        fb.layers = 1;
        vk_check(vkCreateFramebuffer(device_, &fb, nullptr, &hdr_framebuffer_),
                 "vkCreateFramebuffer(hdr)");
        hdr_extent_ = {width, height};
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

        // A fourth set layout for image-based lighting: irradiance cube, prefiltered
        // cube, BRDF LUT. Its own set rather than three more bindings on the per-frame
        // set, because the environment resolves DURING render() with two frames in
        // flight -- writing into a per-frame set would mutate one the other frame may
        // still be reading. This set is never rewritten; a new one is allocated and the
        // handle swapped, which is safe because the old set stays valid for any command
        // buffer still referencing it.
        VkDescriptorSetLayoutBinding ibl_bindings[3]{};
        for (int i = 0; i < 3; ++i)
        {
            ibl_bindings[i].binding = static_cast<std::uint32_t>(i);
            ibl_bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            ibl_bindings[i].descriptorCount = 1;
            ibl_bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        VkDescriptorSetLayoutCreateInfo ibl_layout{};
        ibl_layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        ibl_layout.bindingCount = 3;
        ibl_layout.pBindings = ibl_bindings;
        vk_check(vkCreateDescriptorSetLayout(device_, &ibl_layout, nullptr, &ibl_set_layout_),
                 "vkCreateDescriptorSetLayout(ibl)");

        // The pool feeds both: kMaxTextures single-binding sets plus kMaxMaterialSets
        // two-binding ones, plus kMaxIblSets three-binding IBL sets.
        VkDescriptorPoolSize pool_size{};
        pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_size.descriptorCount = kMaxTextures + kMaxMaterialSets * 2 + kMaxIblSets * 3;

        VkDescriptorPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &pool_size;
        pool.maxSets = kMaxTextures + kMaxMaterialSets + kMaxIblSets;
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

        // Four sets: material, shadow, per-frame UBO, IBL. Vulkan guarantees at least
        // four bound descriptor sets, so this sits exactly at the floor -- a fifth needs
        // a device limit check first.
        std::array<VkDescriptorSetLayout, 4> set_layouts{material_set_layout_, shadow_set_layout_,
                                                         frame_ubo_set_layout_, ibl_set_layout_};
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
        // ONE pipeline, not a pair. The scene renders into the HDR pass whatever its final
        // destination is -- the swapchain in `mosaic run --window`, the editor's viewport
        // target after the resolve -- so the variant that existed only to be compatible
        // with vp_render_pass_ has nothing left to be compatible with. Only the resolve
        // and the UI, which draw into the destination, still need a pair.
        pi.renderPass = hdr_render_pass_;
        pi.subpass = 0;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &pipeline_),
                 "vkCreateGraphicsPipelines");

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
        // ONE pipeline, not a pair. The scene renders into the HDR pass whatever its final
        // destination is -- the swapchain in `mosaic run --window`, the editor's viewport
        // target after the resolve -- so the variant that existed only to be compatible
        // with vp_render_pass_ has nothing left to be compatible with. Only the resolve
        // and the UI, which draw into the destination, still need a pair.
        pi.renderPass = hdr_render_pass_; pi.subpass = 0;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &particle_pipeline_),
                 "vkCreateGraphicsPipelines(particle)");

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
        // ONE pipeline, not a pair. The scene renders into the HDR pass whatever its final
        // destination is -- the swapchain in `mosaic run --window`, the editor's viewport
        // target after the resolve -- so the variant that existed only to be compatible
        // with vp_render_pass_ has nothing left to be compatible with. Only the resolve
        // and the UI, which draw into the destination, still need a pair.
        pi.renderPass = hdr_render_pass_; pi.subpass = 0;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &ribbon_pipeline_),
                 "vkCreateGraphicsPipelines(ribbon)");

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
        // ONE pipeline, not a pair. The scene renders into the HDR pass whatever its final
        // destination is -- the swapchain in `mosaic run --window`, the editor's viewport
        // target after the resolve -- so the variant that existed only to be compatible
        // with vp_render_pass_ has nothing left to be compatible with. Only the resolve
        // and the UI, which draw into the destination, still need a pair.
        pi.renderPass = hdr_render_pass_; pi.subpass = 0;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &world_text_pipeline_),
                 "vkCreateGraphicsPipelines(world text)");

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
        // ONE pipeline, not a pair. The scene renders into the HDR pass whatever its final
        // destination is -- the swapchain in `mosaic run --window`, the editor's viewport
        // target after the resolve -- so the variant that existed only to be compatible
        // with vp_render_pass_ has nothing left to be compatible with. Only the resolve
        // and the UI, which draw into the destination, still need a pair.
        pi.renderPass = hdr_render_pass_; pi.subpass = 0;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &decal_pipeline_),
                 "vkCreateGraphicsPipelines(decal)");

        vkDestroyShaderModule(device_, frag, nullptr);
        vkDestroyShaderModule(device_, vert, nullptr);
    }

    // 1x1 stand-ins bound into set 3 at startup, so the set is ALWAYS valid.
    //
    // The lit shader samples all three unconditionally -- it decides whether to USE the
    // result from a uniform, not whether to sample -- and sampling an unbound descriptor
    // is undefined behaviour rather than merely a black pixel. Black for the two cubes
    // and a neutral (1, 0) for the LUT, so if the enable flag were ever wrong the result
    // would be an unlit surface rather than a garbage one.
    void create_default_ibl_resources()
    {
        mosaic::engine::render::Image black;
        black.width = 1;
        black.height = 1;
        black.format = mosaic::engine::render::ImageFormat::Rgba16F;
        black.pixels.assign(8, 0);
        std::array<mosaic::engine::render::Image, 6> faces;
        for (auto& f : faces) { f = black; }
        ibl_default_cube_ = create_cubemap(faces);

        mosaic::engine::render::Image lut = black;
        // scale = 1, bias = 0: specular would be exactly F0 if it were ever used.
        const std::uint16_t one = 0x3C00; // half 1.0
        lut.pixels[0] = static_cast<std::uint8_t>(one & 0xFF);
        lut.pixels[1] = static_cast<std::uint8_t>(one >> 8);
        ibl_default_lut_ = create_texture(lut, mosaic::engine::render::TextureColorSpace::Linear);

        if (ibl_default_cube_ == 0 || ibl_default_lut_ == 0)
        {
            // Unreachable today -- textures_ is nearly empty here -- but if it ever
            // happened, ibl_set_ would stay null and every lit draw would bind a null
            // descriptor set, which is worse than the uploads failing. Fail loudly.
            throw std::runtime_error(
                "failed to create the default image-based-lighting resources");
        }
        {
            set_image_based_lighting(ibl_default_cube_, ibl_default_cube_, ibl_default_lut_);
            // The defaults are not a real environment: the shader must still take the
            // flat-ambient fallback until a genuine one arrives.
            ibl_enabled_ = false;
        }
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
        // ONE pipeline, not a pair. The scene renders into the HDR pass whatever its final
        // destination is -- the swapchain in `mosaic run --window`, the editor's viewport
        // target after the resolve -- so the variant that existed only to be compatible
        // with vp_render_pass_ has nothing left to be compatible with. Only the resolve
        // and the UI, which draw into the destination, still need a pair.
        pi.renderPass = hdr_render_pass_; pi.subpass = 0;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &skybox_pipeline_),
                 "vkCreateGraphicsPipelines(skybox)");

        vkDestroyShaderModule(device_, frag, nullptr);
        vkDestroyShaderModule(device_, vert, nullptr);
    }

    // The fullscreen tone-map resolve: samples the HDR scene target and writes display
    // colour into the destination. A PAIR, unlike the scene pipelines, because the
    // destination pass differs between `mosaic run --window` and the editor -- the same
    // reason the three UI pipelines keep theirs.
    void create_resolve_pipeline()
    {
        // Its own pool and its own set, one descriptor, allocated once. Not drawn from
        // descriptor_pool_, whose maxSets is budgeted exactly for textures, materials and
        // IBL -- borrowing a set there would starve the last texture at some scene size
        // rather than fail here.
        VkDescriptorPoolSize pool_size{};
        pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_size.descriptorCount = 1;
        VkDescriptorPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &pool_size;
        pool.maxSets = 1;
        vk_check(vkCreateDescriptorPool(device_, &pool, nullptr, &hdr_descriptor_pool_),
                 "vkCreateDescriptorPool(hdr)");

        VkDescriptorSetAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc.descriptorPool = hdr_descriptor_pool_;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &texture_set_layout_; // one combined image+sampler at binding 0
        vk_check(vkAllocateDescriptorSets(device_, &alloc, &hdr_set_),
                 "vkAllocateDescriptorSets(hdr)");
        write_hdr_descriptor();

        std::vector<char> vert_code = read_file(std::string(MOSAIC_SHADER_DIR) + "/fullscreen_triangle.vert.spv");
        std::vector<char> frag_code = read_file(std::string(MOSAIC_SHADER_DIR) + "/hdr_resolve.frag.spv");
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
        depth.depthTestEnable = VK_FALSE;  // it IS the background
        depth.depthWriteEnable = VK_FALSE;
        depth.depthCompareOp = VK_COMPARE_OP_ALWAYS;

        VkPipelineColorBlendAttachmentState blend{};
        // NO BLENDING, and alpha IS written as 1.0. The resolve replaces the destination
        // rather than compositing onto it -- it is the first thing drawn there. Writing
        // opaque alpha matters for the editor: the panel target's alpha is respected when
        // ImGui::Image composites it, and screen_effect.frag's comment records what an
        // unwritten alpha looked like there (an apparent inverted vignette).
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blend.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &blend;

        VkPushConstantRange push{};
        push.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        push.offset = 0; push.size = sizeof(ResolvePush);

        VkPipelineLayoutCreateInfo layout{};
        layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        // TWO sets, both the one-combined-image-sampler layout: the scene at 0, the bloom
        // chain at 1. A second BINDING in set 0 would have needed a second set layout, and
        // texture_set_layout_ is shared with every texture in the engine.
        const VkDescriptorSetLayout resolve_layouts[2] = {texture_set_layout_,
                                                          texture_set_layout_};
        layout.setLayoutCount = 2; layout.pSetLayouts = resolve_layouts;
        layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &push;
        vk_check(vkCreatePipelineLayout(device_, &layout, nullptr, &resolve_pipeline_layout_),
                 "vkCreatePipelineLayout(resolve)");

        VkGraphicsPipelineCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pi.stageCount = 2; pi.pStages = stages;
        pi.pVertexInputState = &vin; pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp; pi.pRasterizationState = &rast;
        pi.pMultisampleState = &ms; pi.pDepthStencilState = &depth;
        pi.pColorBlendState = &cb; pi.pDynamicState = &dyn;
        pi.layout = resolve_pipeline_layout_;
        pi.renderPass = render_pass_; pi.subpass = 0;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &resolve_pipeline_),
                 "vkCreateGraphicsPipelines(resolve)");

        pi.renderPass = vp_render_pass_;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &vp_resolve_pipeline_),
                 "vkCreateGraphicsPipelines(resolve offscreen)");

        vkDestroyShaderModule(device_, frag, nullptr);
        vkDestroyShaderModule(device_, vert, nullptr);
    }

    void create_bloom_pipelines()
    {
        // ITS OWN POOL, sized for the MAXIMUM chain plus the HDR view, so a resize that
        // grows the level count never has to reallocate it. Not descriptor_pool_, whose
        // maxSets is budgeted exactly for textures, materials and IBL -- borrowing a set
        // there would starve the last texture at some scene size rather than fail here.
        VkDescriptorPoolSize pool_size{};
        pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_size.descriptorCount = mosaic::render::vk::kBloomMaxLevels + 1;
        VkDescriptorPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &pool_size;
        pool.maxSets = mosaic::render::vk::kBloomMaxLevels + 1;
        vk_check(vkCreateDescriptorPool(device_, &pool, nullptr, &bloom_descriptor_pool_),
                 "vkCreateDescriptorPool(bloom)");

        std::vector<VkDescriptorSetLayout> layouts(
            mosaic::render::vk::kBloomMaxLevels + 1, texture_set_layout_);
        bloom_sets_.resize(layouts.size());
        VkDescriptorSetAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc.descriptorPool = bloom_descriptor_pool_;
        alloc.descriptorSetCount = static_cast<uint32_t>(layouts.size());
        alloc.pSetLayouts = layouts.data();
        vk_check(vkAllocateDescriptorSets(device_, &alloc, bloom_sets_.data()),
                 "vkAllocateDescriptorSets(bloom)");
        write_bloom_descriptors();

        std::vector<char> vert_code =
            read_file(std::string(MOSAIC_SHADER_DIR) + "/fullscreen_triangle.vert.spv");
        std::vector<char> pre_code =
            read_file(std::string(MOSAIC_SHADER_DIR) + "/bloom_prefilter.frag.spv");
        std::vector<char> blur_code =
            read_file(std::string(MOSAIC_SHADER_DIR) + "/bloom_blur.frag.spv");
        VkShaderModule vert = create_shader_module(vert_code);
        VkShaderModule pre  = create_shader_module(pre_code);
        VkShaderModule blur = create_shader_module(blur_code);

        VkPipelineShaderStageCreateInfo vs{};
        vs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vs.stage = VK_SHADER_STAGE_VERTEX_BIT; vs.module = vert; vs.pName = "main";
        VkPipelineShaderStageCreateInfo fs{};
        fs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fs.stage = VK_SHADER_STAGE_FRAGMENT_BIT; fs.pName = "main";

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
        rast.cullMode = VK_CULL_MODE_NONE;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable = VK_FALSE;
        depth.depthWriteEnable = VK_FALSE;
        depth.depthCompareOp = VK_COMPARE_OP_ALWAYS;

        VkPipelineColorBlendAttachmentState opaque{};
        opaque.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        opaque.blendEnable = VK_FALSE;
        VkPipelineColorBlendStateCreateInfo cb_opaque{};
        cb_opaque.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb_opaque.attachmentCount = 1; cb_opaque.pAttachments = &opaque;

        // THE ONE DIFFERENCE between the downsample and the upsample, which share a shader.
        // Without (ONE, ONE) the upsample REPLACES the level it targets instead of adding
        // to it, and the finished glow is only the smallest mip scaled up -- a soft,
        // uniform wash with none of the chain's detail, which looks deliberate.
        VkPipelineColorBlendAttachmentState additive = opaque;
        additive.blendEnable = VK_TRUE;
        additive.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        additive.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        additive.colorBlendOp = VK_BLEND_OP_ADD;
        // Alpha is never read from this image; kept at (ONE, ZERO) so it stays 1.0 rather
        // than accumulating to a number nothing means.
        additive.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        additive.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        additive.alphaBlendOp = VK_BLEND_OP_ADD;
        VkPipelineColorBlendStateCreateInfo cb_additive{};
        cb_additive.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb_additive.attachmentCount = 1; cb_additive.pAttachments = &additive;

        VkPushConstantRange push{};
        push.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        push.offset = 0; push.size = sizeof(BloomPush);

        VkPipelineLayoutCreateInfo layout{};
        layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout.setLayoutCount = 1; layout.pSetLayouts = &texture_set_layout_;
        layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &push;
        vk_check(vkCreatePipelineLayout(device_, &layout, nullptr, &bloom_pipeline_layout_),
                 "vkCreatePipelineLayout(bloom)");

        VkPipelineShaderStageCreateInfo stages[2] = {vs, fs};
        VkGraphicsPipelineCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pi.stageCount = 2; pi.pStages = stages;
        pi.pVertexInputState = &vin; pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp; pi.pRasterizationState = &rast;
        pi.pMultisampleState = &ms; pi.pDepthStencilState = &depth;
        pi.pDynamicState = &dyn;
        pi.layout = bloom_pipeline_layout_;
        // ONE render pass for all three, which is what lets a single loop drive the chain.
        pi.renderPass = bloom_render_pass_; pi.subpass = 0;

        stages[1].module = pre;
        pi.pColorBlendState = &cb_opaque;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr,
                                           &bloom_prefilter_pipeline_),
                 "vkCreateGraphicsPipelines(bloom prefilter)");

        stages[1].module = blur;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr,
                                           &bloom_down_pipeline_),
                 "vkCreateGraphicsPipelines(bloom down)");

        pi.pColorBlendState = &cb_additive;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr,
                                           &bloom_up_pipeline_),
                 "vkCreateGraphicsPipelines(bloom up)");

        vkDestroyShaderModule(device_, blur, nullptr);
        vkDestroyShaderModule(device_, pre, nullptr);
        vkDestroyShaderModule(device_, vert, nullptr);
    }

    // Points every bloom set at the view it samples. Called at creation and again after
    // every recreate_swapchain -- which rebuilds the chain, freeing every view these sets
    // were written with. That is EXACTLY the defect #256 shipped (a freed image view still
    // bound, found by a reading review after a run with zero validation errors), and here
    // there are N+1 chances to forget rather than one.
    void write_bloom_descriptors()
    {
        if (bloom_sets_.empty()) { return; }

        // EACH SET IS WRITTEN BY ITS OWN INDEX, never by position in a packed vector.
        //
        // This used to push the HDR entry CONDITIONALLY into `infos` and then pair
        // bloom_sets_[i] with infos[i] positionally. Skipping that push with
        // bloom_levels_ > 0 would have shifted the whole mapping -- set 0 gets mip 0, set 1
        // gets mip 1, every source_set in build_bloom reads one mip too deep, and the
        // resolve's "mip 0" becomes mip 1. The result is a glow that looks slightly wrong,
        // which is a failure mode neither a test nor a validation layer can see.
        //
        // It was unreachable on both call paths, and that is exactly why it is worth
        // removing rather than documenting: the coupling was one reorder away from being
        // live. Found by the whole-branch review.
        std::vector<VkDescriptorImageInfo> infos(bloom_sets_.size());
        std::vector<VkWriteDescriptorSet>  writes;
        writes.reserve(bloom_sets_.size());

        // pImageInfo is a POINTER into `infos`, which is sized ONCE above and never grown,
        // so no write can be left dangling by a reallocation.
        auto point_set_at = [&](std::size_t set_index, VkImageView view)
        {
            if (view == VK_NULL_HANDLE) { return; }
            VkDescriptorImageInfo& info = infos[set_index];
            info.sampler = hdr_sampler_; // LINEAR, CLAMP_TO_EDGE -- the chain wants both
            info.imageView = view;
            info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet w{};
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = bloom_sets_[set_index];
            w.dstBinding = 0;
            w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w.pImageInfo = &info;
            writes.push_back(w);
        };

        // Set 0 is the HDR scene, which the prefilter samples.
        point_set_at(0, hdr_color_view_);
        // Set i + 1 is mip i. A set left unwritten here keeps whatever it held; sets above
        // bloom_levels_ are never bound, because no source_set in build_bloom exceeds
        // bloom_levels_.
        for (uint32_t i = 0; i < bloom_levels_; ++i)
        {
            point_set_at(static_cast<std::size_t>(i) + 1, bloom_views_[i]);
        }
        if (writes.empty()) { return; }
        vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
    }

    // Points the resolve's one descriptor at the current HDR colour view. Called at
    // creation and again after every recreate_swapchain, which rebuilds the target and so
    // invalidates the view the set was written with. A no-op before the set exists: the
    // target is created before the descriptor infrastructure that this set's layout comes
    // from, so the first write is the one create_resolve_pipeline does itself.
    void write_hdr_descriptor()
    {
        if (hdr_set_ == VK_NULL_HANDLE || hdr_color_view_ == VK_NULL_HANDLE) { return; }
        VkDescriptorImageInfo info{};
        info.sampler = hdr_sampler_;
        info.imageView = hdr_color_view_;
        info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = hdr_set_;
        w.dstBinding = 0;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &info;
        vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
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
        // ONE pipeline, not a pair. The scene renders into the HDR pass whatever its final
        // destination is -- the swapchain in `mosaic run --window`, the editor's viewport
        // target after the resolve -- so the variant that existed only to be compatible
        // with vp_render_pass_ has nothing left to be compatible with. Only the resolve
        // and the UI, which draw into the destination, still need a pair.
        pi.renderPass = hdr_render_pass_; pi.subpass = 0;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &screen_effect_pipeline_),
                 "vkCreateGraphicsPipelines(screen_effect)");

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
        // The HDR target is swapchain-sized, so it is stale the moment the swapchain is.
        // A stale one is sampled at the wrong resolution and shows as a smeared or cropped
        // frame on every resize.
        destroy_hdr_target();
        destroy_bloom_chain();
        create_hdr_target(extent_.width, extent_.height);
        create_bloom_chain(extent_.width, extent_.height);
        write_bloom_descriptors();
        // The new target is a new image view, so the resolve's descriptor still points at
        // the freed one. Rewriting it here is the whole fix; forgetting it is a
        // use-after-free the validation layers report and nothing else would.
        write_hdr_descriptor();
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
    // IBL sets are allocated fresh on every set_image_based_lighting rather than
    // rewritten, so the count is "how many times the environment may be set in a run",
    // not "how many environments exist". create_default_ibl_resources consumes the first
    // one at startup, so the real budget is 7 CHANGES after that -- generous for a
    // feature that is set once at load. Exhaustion keeps the previous set rather than
    // corrupting anything.
    static constexpr uint32_t kMaxIblSets = 8;
    VkDescriptorSetLayout texture_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout material_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout ibl_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorSet ibl_set_ = VK_NULL_HANDLE;
    std::uint32_t ibl_sets_allocated_ = 0;
    // 1x1 stand-ins so set 3 is ALWAYS valid: the shader samples all three unconditionally
    // and an unbound descriptor is undefined, not merely dark.
    mosaic::engine::render::TextureHandle ibl_default_cube_ = 0;
    mosaic::engine::render::TextureHandle ibl_default_lut_ = 0;
    bool ibl_enabled_ = false;
    VkPipeline skybox_pipeline_ = VK_NULL_HANDLE;
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

    VkPipeline       particle_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout particle_pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline       ribbon_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout ribbon_pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline       world_text_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout world_text_pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline       decal_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout decal_pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline       screen_effect_pipeline_ = VK_NULL_HANDLE;
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

    // --- the linear HDR scene target -------------------------------------------------
    // Same lifecycle as the offscreen viewport target above: created with the swapchain,
    // destroyed with it, recreated on resize, and the same finalLayout trick
    // (SHADER_READ_ONLY_OPTIMAL) so the resolve samples it with no manual barrier.
    //
    // Its OWN depth, because a framebuffer binds specific image views and this is a different
    // render pass from the viewport's -- sharing would mean one pass writing depth the other
    // declared LOAD_OP_CLEAR on.
    //
    // SWAPCHAIN-SIZED, not panel-sized: the scene is rendered once at full resolution and the
    // editor samples the RESOLVED result into whatever panel size it has. Sizing this to the
    // panel would re-render the scene at panel resolution and fork the two paths again.
    VkRenderPass    hdr_render_pass_ = VK_NULL_HANDLE;
    VkImage         hdr_color_ = VK_NULL_HANDLE;
    VkDeviceMemory  hdr_color_mem_ = VK_NULL_HANDLE;
    VkImageView     hdr_color_view_ = VK_NULL_HANDLE;
    VkImage         hdr_depth_ = VK_NULL_HANDLE;
    VkDeviceMemory  hdr_depth_mem_ = VK_NULL_HANDLE;
    VkImageView     hdr_depth_view_ = VK_NULL_HANDLE;
    VkFramebuffer   hdr_framebuffer_ = VK_NULL_HANDLE;
    VkSampler       hdr_sampler_ = VK_NULL_HANDLE;      // persistent across resizes
    VkExtent2D      hdr_extent_ = {0, 0};
    VkDescriptorPool hdr_descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet  hdr_set_ = VK_NULL_HANDLE;   // the resolve's view of the scene target
    VkPipelineLayout resolve_pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline       resolve_pipeline_ = VK_NULL_HANDLE;    // into the swapchain pass
    VkPipeline       vp_resolve_pipeline_ = VK_NULL_HANDLE; // into the editor's panel pass
    bool            in_hdr_pass_ = false;

    // --- bloom chain ---------------------------------------------------------------
    // ONE image with bloom_levels_ mips at HALF the swapchain's resolution, plus one view
    // and one framebuffer per mip -- a framebuffer binds a specific view, so the per-mip
    // vectors are not an optimisation, they are the API.
    VkRenderPass               bloom_render_pass_ = VK_NULL_HANDLE;
    VkImage                    bloom_image_ = VK_NULL_HANDLE;
    VkDeviceMemory             bloom_mem_ = VK_NULL_HANDLE;
    std::vector<VkImageView>   bloom_views_;
    std::vector<VkFramebuffer> bloom_fbs_;
    std::vector<VkExtent2D>    bloom_extents_;
    std::uint32_t              bloom_levels_ = 0;
    VkDescriptorPool             bloom_descriptor_pool_ = VK_NULL_HANDLE;
    // bloom_sets_[0] is the HDR scene view; bloom_sets_[i + 1] is mip i. One per distinct
    // SOURCE: the downsample reads mips 0..N-2 and the upsample reads 1..N-1, so between
    // them every level is sampled -- including the smallest, which nothing downsamples
    // from.
    std::vector<VkDescriptorSet> bloom_sets_;
    VkPipelineLayout             bloom_pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline                   bloom_prefilter_pipeline_ = VK_NULL_HANDLE;
    VkPipeline                   bloom_down_pipeline_ = VK_NULL_HANDLE;
    VkPipeline                   bloom_up_pipeline_ = VK_NULL_HANDLE;
    // The viewport pass is REQUESTED by begin_viewport_pass and opened lazily by
    // ensure_viewport_pass, so the HDR scene pass can open and close first without
    // nesting inside it. vp_clear_ carries the clear colour across that gap.
    bool            viewport_pass_requested_ = false;
    Color           vp_clear_{0.0f, 0.0f, 0.0f, 1.0f};
};

} // namespace

std::unique_ptr<mosaic::engine::render::RenderDevice>
create_vulkan_render_device(GLFWwindow* window)
{
    return std::make_unique<VulkanRenderDevice>(window);
}

} // namespace mosaic::render::vk
