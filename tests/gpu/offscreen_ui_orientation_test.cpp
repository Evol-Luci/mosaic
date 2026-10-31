#include "modules/render_vulkan/vulkan_render_device.hpp"

#include "engine/render/render_device.hpp"
#include "engine/ui/ui_rect.hpp"
#include "foundation/logging/log.hpp"
#include "tests/gpu/validation_capture.hpp"
#include "foundation/math/vec3.hpp"

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

std::array<std::uint8_t, 4> expected_srgb8(float r, float g, float b, float a)
{
    return {
        linear_to_srgb8(r),
        linear_to_srgb8(g),
        linear_to_srgb8(b),
        linear_to_unorm8(a),
    };
}

std::string format_pixel(const std::vector<std::uint8_t>& pixels,
                         std::uint32_t width,
                         std::uint32_t x,
                         std::uint32_t y)
{
    const std::size_t idx =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
         static_cast<std::size_t>(x)) *
        4u;
    return "(" + std::to_string(pixels[idx + 0u]) + ", " +
           std::to_string(pixels[idx + 1u]) + ", " +
           std::to_string(pixels[idx + 2u]) + ", " +
           std::to_string(pixels[idx + 3u]) + ")";
}

void require_pixel_near(const std::vector<std::uint8_t>& pixels,
                        std::uint32_t width,
                        std::uint32_t x,
                        std::uint32_t y,
                        const std::array<std::uint8_t, 4>& expected,
                        const char* label)
{
    const std::size_t idx =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
         static_cast<std::size_t>(x)) *
        4u;
    const std::uint8_t r = pixels[idx + 0u];
    const std::uint8_t g = pixels[idx + 1u];
    const std::uint8_t b = pixels[idx + 2u];
    const std::uint8_t a = pixels[idx + 3u];
    INFO(label << " at (" << x << ", " << y << ")"
               << " actual=(" << static_cast<unsigned>(r) << ", "
               << static_cast<unsigned>(g) << ", "
               << static_cast<unsigned>(b) << ", "
               << static_cast<unsigned>(a) << ")"
               << " expected=(" << static_cast<unsigned>(expected[0]) << ", "
               << static_cast<unsigned>(expected[1]) << ", "
               << static_cast<unsigned>(expected[2]) << ", "
               << static_cast<unsigned>(expected[3]) << ")");
    REQUIRE(std::abs(static_cast<int>(r) - static_cast<int>(expected[0])) <= 1);
    REQUIRE(std::abs(static_cast<int>(g) - static_cast<int>(expected[1])) <= 1);
    REQUIRE(std::abs(static_cast<int>(b) - static_cast<int>(expected[2])) <= 1);
    REQUIRE(std::abs(static_cast<int>(a) - static_cast<int>(expected[3])) <= 1);
}

} // namespace

TEST_CASE("gpu offscreen draw_ui places top-left origin quads in the expected corners",
          "[gpu][render][vulkan]")
{
    using mosaic::engine::render::Color;
    using mosaic::engine::render::UiQuad;
    using mosaic::engine::ui::Rect;
    using mosaic::foundation::math::Vec3;
    using mosaic::render::vk::OffscreenDevice;
    using mosaic::render::vk::create_offscreen_vulkan_render_device;

    constexpr std::uint32_t kWidth = 128;
    constexpr std::uint32_t kHeight = 128;
    constexpr std::size_t kPixelCount =
        static_cast<std::size_t>(kWidth) * static_cast<std::size_t>(kHeight);

    // Clear and both quad colours differ in every RGB channel so background and
    // either quad can never be confused.
    constexpr Color kClearColor{0.125f, 0.25f, 0.5f, 1.0f};
    constexpr Vec3 kTopLeftColor{0.75f, 0.5f, 0.125f};
    constexpr Vec3 kBottomRightColor{0.25f, 0.75f, 0.875f};

    const std::array<std::uint8_t, 4> expected_clear =
        expected_srgb8(kClearColor.r, kClearColor.g, kClearColor.b, kClearColor.a);
    const std::array<std::uint8_t, 4> expected_top_left =
        expected_srgb8(kTopLeftColor.x, kTopLeftColor.y, kTopLeftColor.z, 1.0f);
    const std::array<std::uint8_t, 4> expected_bottom_right =
        expected_srgb8(kBottomRightColor.x, kBottomRightColor.y, kBottomRightColor.z, 1.0f);

    REQUIRE(expected_clear == std::array<std::uint8_t, 4>{99, 137, 188, 255});
    REQUIRE(expected_top_left == std::array<std::uint8_t, 4>{225, 188, 99, 255});
    REQUIRE(expected_bottom_right == std::array<std::uint8_t, 4>{137, 225, 240, 255});

    auto capture_sink = mosaic::gpu_test::install_validation_capture();

    {
        OffscreenDevice offscreen = create_offscreen_vulkan_render_device(kWidth, kHeight);
        REQUIRE(offscreen.device != nullptr);
        REQUIRE(offscreen.readback != nullptr);

        const UiQuad top_left_quad{
            Rect{0.0f, 0.0f, 0.25f, 0.25f},
            kTopLeftColor,
            1.0f,
        };
        const UiQuad bottom_right_quad{
            Rect{0.75f, 0.75f, 0.25f, 0.25f},
            kBottomRightColor,
            1.0f,
        };

        // Part 1: single top-left quad (x in [0, 32), y in [0, 32) with row 0 = top).
        // Guards ui.vert's `1.0 - ny * 2.0` compensation for the negative-height viewport:
        // without it, ny in [0, 0.25] maps to clip.y in [-1, -0.5] and the quad lands in
        // the bottom-left corner around (8, 119) instead of the top-left around (8, 8).
        offscreen.device->clear(kClearColor);
        offscreen.device->begin_frame();
        offscreen.device->draw_ui({top_left_quad});
        offscreen.device->end_frame();

        {
            const std::vector<std::uint8_t> pixels = offscreen.readback->read_pixels();
            REQUIRE(pixels.size() == kPixelCount * 4u);

            INFO("frame 0 corners: top-left(8,8)=" << format_pixel(pixels, kWidth, 8, 8)
                 << " bottom-left(8,119)=" << format_pixel(pixels, kWidth, 8, 119)
                 << " top-right(119,8)=" << format_pixel(pixels, kWidth, 119, 8)
                 << " bottom-right(119,119)=" << format_pixel(pixels, kWidth, 119, 119));
            require_pixel_near(pixels, kWidth, 8, 8, expected_top_left, "frame 0 top-left (inside quad)");
            require_pixel_near(pixels, kWidth, 8, 119, expected_clear, "frame 0 bottom-left (outside quad)");
            require_pixel_near(pixels, kWidth, 119, 8, expected_clear, "frame 0 top-right (outside quad)");
            require_pixel_near(pixels, kWidth, 119, 119, expected_clear, "frame 0 bottom-right (outside quad)");
        }

        // Part 2: two separate draw_ui calls in one frame (composition check across
        // push-constant draws in the same pass), placing one quad top-left and one
        // quad bottom-right while the other two corners remain clear.
        offscreen.device->clear(kClearColor);
        offscreen.device->begin_frame();
        offscreen.device->draw_ui({top_left_quad});
        offscreen.device->draw_ui({bottom_right_quad});
        offscreen.device->end_frame();

        {
            const std::vector<std::uint8_t> pixels = offscreen.readback->read_pixels();
            REQUIRE(pixels.size() == kPixelCount * 4u);

            require_pixel_near(pixels, kWidth, 8, 8, expected_top_left, "frame 1 top-left (first draw_ui call)");
            require_pixel_near(pixels, kWidth, 119, 119, expected_bottom_right, "frame 1 bottom-right (second draw_ui call)");
            require_pixel_near(pixels, kWidth, 8, 119, expected_clear, "frame 1 bottom-left (outside quads)");
            require_pixel_near(pixels, kWidth, 119, 8, expected_clear, "frame 1 top-right (outside quads)");
        }
    }

    const std::vector<std::string> validation_messages = capture_sink->messages();
    mosaic::foundation::log::clear_sinks();
    mosaic::gpu_test::require_validation_observed_and_silent(validation_messages);
}
