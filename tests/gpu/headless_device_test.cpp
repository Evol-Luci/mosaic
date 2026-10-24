#include "modules/render_vulkan/vulkan_headless_device.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("gpu headless Vulkan device creates and destroys cleanly twice in one process",
          "[gpu][render][vulkan]")
{
    using mosaic::render::vk::VulkanHeadlessDevice;

    const std::uint32_t baseline_total =
        VulkanHeadlessDevice::total_validation_message_count();

    // First creation and teardown.
    {
        VulkanHeadlessDevice dev;
        REQUIRE(dev.instance() != VK_NULL_HANDLE);
        REQUIRE(dev.physical_device() != VK_NULL_HANDLE);
        REQUIRE(dev.device() != VK_NULL_HANDLE);
        REQUIRE(dev.graphics_queue() != VK_NULL_HANDLE);
        // MUST come before the message-count assertion below, and is not redundant with it.
        // Measured 2026-10-24: with the layer disabled (VK_LOADER_LAYERS_DISABLE='*') this
        // whole case still passed all 12 assertions, because no messenger is created, the
        // callback never fires and the count is trivially 0 -- the case reported "validation
        // clean" on a run where validation never executed. The count assertion only means
        // something while validation is actually on, so assert that it is.
        REQUIRE(dev.validation_enabled());
        REQUIRE(dev.validation_message_count() == 0u);
    }
    REQUIRE(VulkanHeadlessDevice::total_validation_message_count() == baseline_total);

    // Second creation and teardown sequentially in the same process.
    {
        VulkanHeadlessDevice dev;
        REQUIRE(dev.instance() != VK_NULL_HANDLE);
        REQUIRE(dev.physical_device() != VK_NULL_HANDLE);
        REQUIRE(dev.device() != VK_NULL_HANDLE);
        REQUIRE(dev.graphics_queue() != VK_NULL_HANDLE);
        // MUST come before the message-count assertion below, and is not redundant with it.
        // Measured 2026-10-24: with the layer disabled (VK_LOADER_LAYERS_DISABLE='*') this
        // whole case still passed all 12 assertions, because no messenger is created, the
        // callback never fires and the count is trivially 0 -- the case reported "validation
        // clean" on a run where validation never executed. The count assertion only means
        // something while validation is actually on, so assert that it is.
        REQUIRE(dev.validation_enabled());
        REQUIRE(dev.validation_message_count() == 0u);
    }
    REQUIRE(VulkanHeadlessDevice::total_validation_message_count() == baseline_total);
}
