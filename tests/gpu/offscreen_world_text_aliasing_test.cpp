#include "modules/render_vulkan/vulkan_render_device.hpp"

#include "engine/render/image.hpp"
#include "engine/render/render_device.hpp"
#include "foundation/logging/log.hpp"
#include "foundation/math/mat4.hpp"
#include "foundation/math/vec3.hpp"

#include <algorithm>
#include <array>
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

class ValidationCaptureSink final : public mosaic::foundation::log::Sink
{
public:
    void write(const mosaic::foundation::log::Record& record) noexcept override
    {
        console_.write(record);
        if (record.category == "vulkan" &&
            (record.level == mosaic::foundation::log::Level::Warn ||
             record.level == mosaic::foundation::log::Level::Error))
        {
            std::lock_guard<std::mutex> lock(mutex_);
            messages_.emplace_back(record.message);
        }
    }

    std::vector<std::string> messages() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return messages_;
    }

private:
    mosaic::foundation::log::ConsoleSink console_;
    mutable std::mutex mutex_;
    std::vector<std::string> messages_;
};

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

void require_pixel_differs_from_background(
    const std::vector<std::uint8_t>& pixels,
    std::uint32_t width,
    std::uint32_t x,
    std::uint32_t y,
    const std::array<std::uint8_t, 4>& background,
    int min_channel_delta,
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
    const int dr = std::abs(static_cast<int>(r) - static_cast<int>(background[0]));
    const int dg = std::abs(static_cast<int>(g) - static_cast<int>(background[1]));
    const int db = std::abs(static_cast<int>(b) - static_cast<int>(background[2]));
    const int max_rgb_delta = std::max({dr, dg, db});
    INFO(label << " at (" << x << ", " << y << ")"
               << " actual=(" << static_cast<unsigned>(r) << ", "
               << static_cast<unsigned>(g) << ", "
               << static_cast<unsigned>(b) << ", "
               << static_cast<unsigned>(a) << ")"
               << " background=(" << static_cast<unsigned>(background[0]) << ", "
               << static_cast<unsigned>(background[1]) << ", "
               << static_cast<unsigned>(background[2]) << ", "
               << static_cast<unsigned>(background[3]) << ")"
               << " max_rgb_delta=" << max_rgb_delta
               << " min_channel_delta=" << min_channel_delta);
    REQUIRE(max_rgb_delta > min_channel_delta);
}

std::vector<mosaic::engine::render::WorldGlyphVertex> make_world_glyph_quad(
    float x0,
    float y0,
    float x1,
    float y1,
    float z,
    const mosaic::foundation::math::Vec3& color,
    float alpha)
{
    using mosaic::engine::render::WorldGlyphVertex;
    using mosaic::foundation::math::Vec3;
    // VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST: two triangles (6 pre-expanded vertices).
    return {
        WorldGlyphVertex{Vec3{x0, y0, z}, 0.0f, 0.0f, color, alpha},
        WorldGlyphVertex{Vec3{x1, y0, z}, 1.0f, 0.0f, color, alpha},
        WorldGlyphVertex{Vec3{x1, y1, z}, 1.0f, 1.0f, color, alpha},
        WorldGlyphVertex{Vec3{x0, y0, z}, 0.0f, 0.0f, color, alpha},
        WorldGlyphVertex{Vec3{x1, y1, z}, 1.0f, 1.0f, color, alpha},
        WorldGlyphVertex{Vec3{x0, y1, z}, 0.0f, 1.0f, color, alpha},
    };
}

} // namespace

TEST_CASE("gpu offscreen draw_world_text keeps two per-frame calls at distinct positions without aliasing",
          "[gpu][render][vulkan]")
{
    using mosaic::engine::render::Color;
    using mosaic::engine::render::Image;
    using mosaic::engine::render::ImageFormat;
    using mosaic::engine::render::TextureHandle;
    using mosaic::engine::render::WorldGlyphVertex;
    using mosaic::foundation::math::Mat4;
    using mosaic::foundation::math::Vec3;
    using mosaic::render::vk::OffscreenDevice;
    using mosaic::render::vk::create_offscreen_vulkan_render_device;

    constexpr std::uint32_t kWidth = 128;
    constexpr std::uint32_t kHeight = 128;
    constexpr std::size_t kPixelCount =
        static_cast<std::size_t>(kWidth) * static_cast<std::size_t>(kHeight);

    constexpr Color kClearColor{0.0f, 0.0f, 0.0f, 1.0f};
    constexpr std::array<std::uint8_t, 4> kExpectedBackground{0, 0, 0, 255};
    constexpr int kMinWorldTextChannelDelta = 64;

    // With identity view_proj and a negative-height viewport:
    //   x_fb = (x_ndc + 1) * 0.5 * 128
    //   y_fb = (1 - y_ndc) * 0.5 * 128
    // World-text quad A spanning [-0.75, -0.25] x [+0.25, +0.75] at z = 0.5 covers
    // [16, 48] x [16, 48], centered at P_A = (32, 32).
    // World-text quad B spanning [+0.25, +0.75] x [-0.75, -0.25] at z = 0.5 covers
    // [80, 112] x [80, 112], centered at P_B = (96, 96).
    // Control point P_control = (32, 96) sits in the bottom-left quadrant outside both.
    constexpr std::uint32_t kPAX = 32;
    constexpr std::uint32_t kPAY = 32;
    constexpr std::uint32_t kPBX = 96;
    constexpr std::uint32_t kPBY = 96;
    constexpr std::uint32_t kControlX = 32;
    constexpr std::uint32_t kControlY = 96;

    auto capture_sink = std::make_shared<ValidationCaptureSink>();
    mosaic::foundation::log::clear_sinks();
    mosaic::foundation::log::add_sink(capture_sink);

    {
        OffscreenDevice offscreen = create_offscreen_vulkan_render_device(kWidth, kHeight);
        REQUIRE(offscreen.device != nullptr);
        REQUIRE(offscreen.readback != nullptr);

        // draw_world_text early-returns when textures_.empty(); a solid-white atlas
        // makes coverage = 1.0 everywhere so UV sampling is irrelevant and the test
        // isolates per-call vertex-buffer chunking.
        Image solid_white;
        solid_white.width = 4;
        solid_white.height = 4;
        solid_white.format = ImageFormat::Rgba8;
        solid_white.pixels.assign(4u * 4u * 4u, 255u);

        const TextureHandle atlas = offscreen.device->create_texture(solid_white);
        REQUIRE(atlas != 0u);

        const std::vector<WorldGlyphVertex> batch_a = make_world_glyph_quad(
            -0.75f, 0.25f, -0.25f, 0.75f, 0.5f, Vec3{1.0f, 0.25f, 0.125f}, 1.0f);
        const std::vector<WorldGlyphVertex> batch_b = make_world_glyph_quad(
            0.25f, -0.75f, 0.75f, -0.25f, 0.5f, Vec3{0.125f, 0.75f, 1.0f}, 1.0f);

        offscreen.device->clear(kClearColor);
        offscreen.device->begin_frame();
        offscreen.device->begin_hdr_scene_pass(kClearColor);
        offscreen.device->draw_world_text(batch_a, Mat4::identity(), atlas);
        offscreen.device->draw_world_text(batch_b, Mat4::identity(), atlas);
        offscreen.device->end_hdr_scene_pass();
        offscreen.device->draw_hdr_resolve(1.0f, 0.0f);
        offscreen.device->end_frame();

        const std::vector<std::uint8_t> pixels = offscreen.readback->read_pixels();
        REQUIRE(pixels.size() == kPixelCount * 4u);

        INFO("samples: P_A(32,32)=" << format_pixel(pixels, kWidth, kPAX, kPAY)
             << " P_B(96,96)=" << format_pixel(pixels, kWidth, kPBX, kPBY)
             << " P_control(32,96)=" << format_pixel(pixels, kWidth, kControlX, kControlY));
        require_pixel_differs_from_background(
            pixels, kWidth, kPAX, kPAY, kExpectedBackground, kMinWorldTextChannelDelta,
            "P_A (first draw_world_text call)");
        require_pixel_differs_from_background(
            pixels, kWidth, kPBX, kPBY, kExpectedBackground, kMinWorldTextChannelDelta,
            "P_B (second draw_world_text call)");
        require_pixel_near(
            pixels, kWidth, kControlX, kControlY, kExpectedBackground,
            "P_control (outside both world-text quads)");
    }

    const std::vector<std::string> validation_messages = capture_sink->messages();
    mosaic::foundation::log::clear_sinks();
    REQUIRE(validation_messages.empty());
}
