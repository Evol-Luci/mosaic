#include <optional>
#include <vector>

#include <vulkan/vulkan.h>

#include <catch2/catch_test_macros.hpp>

#include "modules/render_vulkan/vulkan_swapchain_support.hpp"

using namespace mosaic::render::vk;

TEST_CASE("choose_surface_format prefers B8G8R8A8_SRGB + SRGB_NONLINEAR", "[render]")
{
    std::vector<VkSurfaceFormatKHR> candidates{
        {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        {VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    };
    VkSurfaceFormatKHR chosen = choose_surface_format(candidates);
    REQUIRE(chosen.format == VK_FORMAT_B8G8R8A8_SRGB);
    REQUIRE(chosen.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
}

TEST_CASE("choose_surface_format falls back to the first candidate", "[render]")
{
    std::vector<VkSurfaceFormatKHR> candidates{
        {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    };
    REQUIRE(choose_surface_format(candidates).format == VK_FORMAT_R8G8B8A8_UNORM);
}

TEST_CASE("choose_present_mode returns MAILBOX when available, else FIFO", "[render]")
{
    REQUIRE(choose_present_mode({VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_MAILBOX_KHR})
            == VK_PRESENT_MODE_MAILBOX_KHR);
    REQUIRE(choose_present_mode({VK_PRESENT_MODE_FIFO_KHR}) == VK_PRESENT_MODE_FIFO_KHR);
    REQUIRE(choose_present_mode({}) == VK_PRESENT_MODE_FIFO_KHR); // FIFO always supported
}

TEST_CASE("clamp_extent uses currentExtent when the surface fixes it", "[render]")
{
    VkSurfaceCapabilitiesKHR caps{};
    caps.currentExtent = {800, 600};
    caps.minImageExtent = {1, 1};
    caps.maxImageExtent = {4096, 4096};
    VkExtent2D e = clamp_extent({1920, 1080}, caps);
    REQUIRE(e.width == 800);
    REQUIRE(e.height == 600);
}

TEST_CASE("clamp_extent clamps the desired extent to caps when free", "[render]")
{
    VkSurfaceCapabilitiesKHR caps{};
    caps.currentExtent = {0xFFFFFFFF, 0xFFFFFFFF}; // "surface lets us choose"
    caps.minImageExtent = {640, 480};
    caps.maxImageExtent = {1280, 720};
    REQUIRE(clamp_extent({320, 200}, caps).width == 640);
    REQUIRE(clamp_extent({320, 200}, caps).height == 480);
    REQUIRE(clamp_extent({4000, 4000}, caps).width == 1280);
    REQUIRE(clamp_extent({4000, 4000}, caps).height == 720);
}

TEST_CASE("pick_queue_families finds graphics and present indices", "[render]")
{
    std::vector<VkQueueFamilyProperties> families(2);
    families[0].queueFlags = 0;                      // not graphics
    families[1].queueFlags = VK_QUEUE_GRAPHICS_BIT;  // graphics
    std::vector<VkBool32> present{VK_TRUE, VK_FALSE}; // present on family 0

    QueueFamilies q = pick_queue_families(families, present);
    REQUIRE(q.complete());
    REQUIRE(q.graphics.value() == 1u);
    REQUIRE(q.present.value() == 0u);
}

TEST_CASE("pick_queue_families reports incomplete when graphics is missing", "[render]")
{
    std::vector<VkQueueFamilyProperties> families(1);
    families[0].queueFlags = 0;
    std::vector<VkBool32> present{VK_TRUE};
    REQUIRE_FALSE(pick_queue_families(families, present).complete());
}
