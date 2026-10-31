#include "modules/render_vulkan/vulkan_render_device.hpp"

#include "engine/render/render_device.hpp"
#include "foundation/logging/log.hpp"
#include "tests/gpu/validation_capture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace
{

std::uint8_t linear_to_srgb8(float linear)
{
    const float clamped = std::clamp(linear, 0.0f, 1.0f);
    const float srgb = (clamped <= 0.0031308f)
                           ? (12.92f * clamped)
                           : (1.055f * std::pow(clamped, 1.0f / 2.4f) - 0.055f);
    return static_cast<std::uint8_t>(std::lround(srgb * 255.0f));
}

std::uint8_t linear_to_unorm8(float linear)
{
    const float clamped = std::clamp(linear, 0.0f, 1.0f);
    return static_cast<std::uint8_t>(std::lround(clamped * 255.0f));
}

} // namespace

TEST_CASE("gpu offscreen VulkanRenderDevice clears and reads back 64x64 sRGB pixels across two frames",
          "[gpu][render][vulkan]")
{
    using mosaic::engine::render::Color;
    using mosaic::render::vk::OffscreenDevice;
    using mosaic::render::vk::create_offscreen_vulkan_render_device;

    constexpr std::uint32_t kWidth = 64;
    constexpr std::uint32_t kHeight = 64;
    constexpr std::size_t kPixelCount =
        static_cast<std::size_t>(kWidth) * static_cast<std::size_t>(kHeight);
    // A DIFFERENT colour per frame, and this is the whole point of running twice.
    // kFramesInFlight is 2, so frame 0 renders into images_[0] and frame 1 into
    // images_[1]. With the same colour both times, a read_pixels() that returned the
    // STALE image -- or an end_frame() that rendered both frames into images_[0] --
    // would produce byte-identical output and pass. Distinct colours are what give the
    // second iteration its teeth.
    constexpr std::array<Color, 2> kClearColors{
        Color{0.25f, 0.5f, 0.75f, 1.0f},
        Color{0.75f, 0.125f, 0.5f, 1.0f},
    };
    constexpr std::array<std::array<std::uint8_t, 4>, 2> kExpectedSrgb8{
        std::array<std::uint8_t, 4>{137, 188, 225, 255},
        std::array<std::uint8_t, 4>{225, 99, 188, 255},
    };

    for (std::size_t c = 0; c < kClearColors.size(); ++c)
    {
        const std::array<std::uint8_t, 4> computed{
            linear_to_srgb8(kClearColors[c].r),
            linear_to_srgb8(kClearColors[c].g),
            linear_to_srgb8(kClearColors[c].b),
            linear_to_unorm8(kClearColors[c].a),
        };
        INFO("colour index " << c);
        REQUIRE(computed == kExpectedSrgb8[c]);
    }

    auto capture_sink = mosaic::gpu_test::install_validation_capture();

    {
        OffscreenDevice offscreen = create_offscreen_vulkan_render_device(kWidth, kHeight);
        REQUIRE(offscreen.device != nullptr);
        REQUIRE(offscreen.readback != nullptr);

        for (std::size_t frame = 0; frame < kClearColors.size(); ++frame)
        {
            const std::array<std::uint8_t, 4>& expected = kExpectedSrgb8[frame];
            offscreen.device->clear(kClearColors[frame]);
            offscreen.device->begin_frame();
            offscreen.device->end_frame();

            const std::vector<std::uint8_t> pixels = offscreen.readback->read_pixels();
            REQUIRE(pixels.size() == kPixelCount * 4u);

            for (std::size_t i = 0; i < kPixelCount; ++i)
            {
                const std::uint8_t r = pixels[i * 4u + 0u];
                const std::uint8_t g = pixels[i * 4u + 1u];
                const std::uint8_t b = pixels[i * 4u + 2u];
                const std::uint8_t a = pixels[i * 4u + 3u];
                INFO("frame=" << frame << " pixel=" << i
                     << " r=" << static_cast<unsigned>(r)
                     << " g=" << static_cast<unsigned>(g)
                     << " b=" << static_cast<unsigned>(b)
                     << " a=" << static_cast<unsigned>(a));
                REQUIRE(std::abs(static_cast<int>(r) - static_cast<int>(expected[0])) <= 1);
                REQUIRE(std::abs(static_cast<int>(g) - static_cast<int>(expected[1])) <= 1);
                REQUIRE(std::abs(static_cast<int>(b) - static_cast<int>(expected[2])) <= 1);
                REQUIRE(std::abs(static_cast<int>(a) - static_cast<int>(expected[3])) <= 1);
            }
        }
    }

    const std::vector<std::string> validation_messages = capture_sink->messages();
    mosaic::foundation::log::clear_sinks();
    mosaic::gpu_test::require_validation_observed_and_silent(validation_messages);
}
