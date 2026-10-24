#include "modules/render_vulkan/vulkan_headless_device.hpp"
#include "modules/render_vulkan/vulkan_headless_readback.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("gpu offscreen clear and readback asserts all 256 pixels twice in one process",
          "[gpu][render][vulkan]")
{
    using mosaic::render::vk::Rgba8;
    using mosaic::render::vk::VulkanHeadlessDevice;
    using mosaic::render::vk::VulkanHeadlessReadback;

    constexpr std::uint32_t kWidth = 16;
    constexpr std::uint32_t kHeight = 16;
    constexpr std::size_t kPixelCount =
        static_cast<std::size_t>(kWidth) * static_cast<std::size_t>(kHeight);
    constexpr Rgba8 kExpected{0x12, 0x34, 0x56, 0x78};

    const std::uint32_t baseline_total =
        VulkanHeadlessDevice::total_validation_message_count();

    for (int run = 0; run < 2; ++run)
    {
        VulkanHeadlessDevice dev;
        // MUST come before the message-count assertion below, and is not redundant with it.
        // Without an active validation layer, no debug messenger is created and
        // validation_message_count() would trivially remain 0.
        REQUIRE(dev.validation_enabled());

        VulkanHeadlessReadback readback(dev, kWidth, kHeight);
        REQUIRE(readback.image() != VK_NULL_HANDLE);
        REQUIRE(readback.readback_buffer() != VK_NULL_HANDLE);

        const std::vector<Rgba8> pixels = readback.clear_and_readback(kExpected);
        REQUIRE(pixels.size() == kPixelCount);

        for (std::size_t i = 0; i < pixels.size(); ++i)
        {
            INFO("run=" << run << " pixel=" << i
                 << " r=" << static_cast<unsigned>(pixels[i].r)
                 << " g=" << static_cast<unsigned>(pixels[i].g)
                 << " b=" << static_cast<unsigned>(pixels[i].b)
                 << " a=" << static_cast<unsigned>(pixels[i].a));
            REQUIRE(pixels[i].r == 0x12);
            REQUIRE(pixels[i].g == 0x34);
            REQUIRE(pixels[i].b == 0x56);
            REQUIRE(pixels[i].a == 0x78);
            REQUIRE(pixels[i] == kExpected);
        }

        REQUIRE(dev.validation_enabled());
        REQUIRE(dev.validation_message_count() == 0u);
    }

    REQUIRE(VulkanHeadlessDevice::total_validation_message_count() == baseline_total);
}
