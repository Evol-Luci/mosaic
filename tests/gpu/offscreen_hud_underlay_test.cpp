#include "modules/render_vulkan/vulkan_render_device.hpp"

#include "engine/render/image.hpp"
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
#include <memory>
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

TEST_CASE("gpu offscreen draw_ui_image composites the HUD underlay beneath the checker",
          "[gpu][render][vulkan]")
{
    using mosaic::engine::render::Color;
    using mosaic::engine::render::Image;
    using mosaic::engine::render::ImageFormat;
    using mosaic::engine::render::TextureHandle;
    using mosaic::engine::render::UiImageQuad;
    using mosaic::engine::ui::Rect;
    using mosaic::foundation::math::Vec3;
    using mosaic::render::vk::OffscreenDevice;
    using mosaic::render::vk::create_offscreen_vulkan_render_device;

    constexpr std::uint32_t kWidth = 128;
    constexpr std::uint32_t kHeight = 128;
    constexpr std::size_t kPixelCount =
        static_cast<std::size_t>(kWidth) * static_cast<std::size_t>(kHeight);

    // Clear, underlay, and checker colours differ in every RGB channel so no two can
    // be confused by a single-channel mistake.
    constexpr Color kClearColor{0.125f, 0.25f, 0.5f, 1.0f};
    constexpr Vec3 kUnderlayColor{0.75f, 0.5f, 0.125f};
    constexpr Vec3 kCheckerColor{0.25f, 0.75f, 0.875f};

    const std::array<std::uint8_t, 4> expected_clear =
        expected_srgb8(kClearColor.r, kClearColor.g, kClearColor.b, kClearColor.a);
    const std::array<std::uint8_t, 4> expected_underlay =
        expected_srgb8(kUnderlayColor.x, kUnderlayColor.y, kUnderlayColor.z, 1.0f);
    const std::array<std::uint8_t, 4> expected_checker =
        expected_srgb8(kCheckerColor.x, kCheckerColor.y, kCheckerColor.z, 1.0f);

    REQUIRE(expected_clear == std::array<std::uint8_t, 4>{99, 137, 188, 255});
    REQUIRE(expected_underlay == std::array<std::uint8_t, 4>{225, 188, 99, 255});
    REQUIRE(expected_checker == std::array<std::uint8_t, 4>{137, 225, 240, 255});

    auto capture_sink = mosaic::gpu_test::install_validation_capture();

    {
        OffscreenDevice offscreen = create_offscreen_vulkan_render_device(kWidth, kHeight);
        REQUIRE(offscreen.device != nullptr);
        REQUIRE(offscreen.readback != nullptr);

        Image solid_white;
        solid_white.width = 4;
        solid_white.height = 4;
        solid_white.format = ImageFormat::Rgba8;
        solid_white.pixels.assign(4u * 4u * 4u, 255u);

        const TextureHandle texture = offscreen.device->create_texture(solid_white);
        REQUIRE(texture != 0u);

        // Underlay covers [16, 80) x [16, 80); checker covers [48, 112) x [48, 112).
        // They overlap on [48, 80) x [48, 80), leaving [16, 48) x [16, 48) as
        // underlay-only and [0, 16) x [0, 16) outside both.
        const UiImageQuad underlay_quad{
            Rect{0.125f, 0.125f, 0.5f, 0.5f},
            0.0f,
            0.0f,
            1.0f,
            1.0f,
            kUnderlayColor,
            1.0f,
        };
        const UiImageQuad checker_quad{
            Rect{0.375f, 0.375f, 0.5f, 0.5f},
            0.0f,
            0.0f,
            1.0f,
            1.0f,
            kCheckerColor,
            1.0f,
        };

        // Submitted in the order RenderSystem emits for order = -1 (underlay) followed
        // by order = 0 (checker): underlay first, checker second.
        offscreen.device->clear(kClearColor);
        offscreen.device->begin_frame();
        offscreen.device->draw_ui_image({underlay_quad}, texture);
        offscreen.device->draw_ui_image({checker_quad}, texture);
        offscreen.device->end_frame();

        const std::vector<std::uint8_t> pixels = offscreen.readback->read_pixels();
        REQUIRE(pixels.size() == kPixelCount * 4u);

        INFO("samples: overlap(64,64)=" << format_pixel(pixels, kWidth, 64, 64)
             << " underlay-only(32,32)=" << format_pixel(pixels, kWidth, 32, 32)
             << " outside(8,8)=" << format_pixel(pixels, kWidth, 8, 8));

        // 1. Overlap region: must be the checker's colour, not the underlay's.
        require_pixel_near(pixels, kWidth, 64, 64, expected_checker,
                           "overlap region (checker over underlay)");
        // 2. Underlay-only region: must be the underlay's colour.
        require_pixel_near(pixels, kWidth, 32, 32, expected_underlay,
                           "underlay-only region");
        // 3. Outside both quads: must remain the clear colour.
        require_pixel_near(pixels, kWidth, 8, 8, expected_clear,
                           "outside both quads (clear)");
    }

    const std::vector<std::string> validation_messages = capture_sink->messages();
    mosaic::foundation::log::clear_sinks();
    mosaic::gpu_test::require_validation_observed_and_silent(validation_messages);
}
