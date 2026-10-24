#include "modules/render_vulkan/vulkan_render_device.hpp"

#include "engine/render/render_device.hpp"
#include "foundation/logging/log.hpp"
#include "foundation/math/mat4.hpp"
#include "foundation/math/vec3.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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

} // namespace

TEST_CASE("gpu offscreen draw_particles keeps two per-frame calls at distinct positions without aliasing",
          "[gpu][render][vulkan]")
{
    using mosaic::engine::render::Color;
    using mosaic::engine::render::ParticleDrawParams;
    using mosaic::engine::render::ParticleInstance;
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
    constexpr int kMinParticleChannelDelta = 64;

    // With identity view_proj and a negative-height viewport:
    //   x_fb = (x_ndc + 1) * 0.5 * 128
    //   y_fb = (1 - y_ndc) * 0.5 * 128
    // Particle A at (-0.5, +0.5, 0.5) with size 0.5 covers [16, 48] x [16, 48],
    // centered at P_A = (32, 32).
    // Particle B at (+0.5, -0.5, 0.5) with size 0.5 covers [80, 112] x [80, 112],
    // centered at P_B = (96, 96).
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

        const ParticleDrawParams params{
            Mat4::identity(),
            Vec3{1.0f, 0.0f, 0.0f},
            Vec3{0.0f, 1.0f, 0.0f},
        };

        const std::vector<ParticleInstance> batch_a{
            ParticleInstance{
                Vec3{-0.5f, 0.5f, 0.5f},
                0.5f,
                Vec3{1.0f, 0.25f, 0.125f},
                1.0f,
            },
        };
        const std::vector<ParticleInstance> batch_b{
            ParticleInstance{
                Vec3{0.5f, -0.5f, 0.5f},
                0.5f,
                Vec3{0.125f, 0.75f, 1.0f},
                1.0f,
            },
        };

        offscreen.device->clear(kClearColor);
        offscreen.device->begin_frame();
        offscreen.device->begin_hdr_scene_pass(kClearColor);
        offscreen.device->draw_particles(batch_a, params);
        offscreen.device->draw_particles(batch_b, params);
        offscreen.device->end_hdr_scene_pass();
        offscreen.device->draw_hdr_resolve(1.0f, 0.0f);
        offscreen.device->end_frame();

        const std::vector<std::uint8_t> pixels = offscreen.readback->read_pixels();
        REQUIRE(pixels.size() == kPixelCount * 4u);

        INFO("samples: P_A(32,32)=" << format_pixel(pixels, kWidth, kPAX, kPAY)
             << " P_B(96,96)=" << format_pixel(pixels, kWidth, kPBX, kPBY)
             << " P_control(32,96)=" << format_pixel(pixels, kWidth, kControlX, kControlY));
        require_pixel_differs_from_background(
            pixels, kWidth, kPAX, kPAY, kExpectedBackground, kMinParticleChannelDelta,
            "P_A (first draw_particles call)");
        require_pixel_differs_from_background(
            pixels, kWidth, kPBX, kPBY, kExpectedBackground, kMinParticleChannelDelta,
            "P_B (second draw_particles call)");
        require_pixel_near(
            pixels, kWidth, kControlX, kControlY, kExpectedBackground,
            "P_control (outside both particle quads)");
    }

    const std::vector<std::string> validation_messages = capture_sink->messages();
    mosaic::foundation::log::clear_sinks();
    REQUIRE(validation_messages.empty());
}
