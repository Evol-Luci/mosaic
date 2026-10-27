#include "modules/render_vulkan/vulkan_render_device.hpp"

#include "engine/render/render_device.hpp"
#include "engine/ui/ui_rect.hpp"
#include "foundation/logging/log.hpp"
#include "foundation/math/vec3.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
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
    return {linear_to_srgb8(r), linear_to_srgb8(g), linear_to_srgb8(b),
            linear_to_unorm8(a)};
}

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

std::array<std::uint8_t, 4> pixel_at(const std::vector<std::uint8_t>& pixels,
                                     std::uint32_t width,
                                     std::uint32_t x,
                                     std::uint32_t y)
{
    const std::size_t idx =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
         static_cast<std::size_t>(x)) * 4u;
    return {pixels[idx + 0u], pixels[idx + 1u], pixels[idx + 2u], pixels[idx + 3u]};
}

std::string format_pixel(const std::array<std::uint8_t, 4>& p)
{
    return "(" + std::to_string(p[0]) + ", " + std::to_string(p[1]) + ", " +
           std::to_string(p[2]) + ", " + std::to_string(p[3]) + ")";
}

void require_pixel_near(const std::vector<std::uint8_t>& pixels,
                        std::uint32_t width,
                        std::uint32_t x,
                        std::uint32_t y,
                        const std::array<std::uint8_t, 4>& expected,
                        const char* label)
{
    const std::array<std::uint8_t, 4> actual = pixel_at(pixels, width, x, y);
    INFO(label << " at (" << x << ", " << y << ") actual=" << format_pixel(actual)
               << " expected=" << format_pixel(expected));
    REQUIRE(std::abs(static_cast<int>(actual[0]) - static_cast<int>(expected[0])) <= 1);
    REQUIRE(std::abs(static_cast<int>(actual[1]) - static_cast<int>(expected[1])) <= 1);
    REQUIRE(std::abs(static_cast<int>(actual[2]) - static_cast<int>(expected[2])) <= 1);
    REQUIRE(std::abs(static_cast<int>(actual[3]) - static_cast<int>(expected[3])) <= 1);
}

} // namespace

// The U9a gate. Two halves, and BOTH are needed:
//
//   1. pixels OUTSIDE the clip are exactly the clear colour -- if they carry quad colour,
//      the clip did nothing at all;
//   2. pixels INSIDE the clip are identical to an unclipped draw of the same quad -- if
//      they differ, the clip moved or resized the quad instead of only masking it, which
//      is the frame-narrowing behaviour a clip must NOT have (ui_hierarchy.hpp's
//      resolve_parent_frame is the thing that relocates and resizes; a clip is not it).
//
// Half 2 is captured as an actual unclipped render rather than a recomputed expectation,
// so "identical to unclipped" is a measurement and not a restatement of the same formula.
TEST_CASE("gpu offscreen a clipped draw_ui masks pixels outside the clip and leaves the rest identical",
          "[gpu][render][vulkan]")
{
    using mosaic::engine::render::Color;
    using mosaic::engine::render::UiQuad;
    using mosaic::engine::ui::Rect;
    using mosaic::foundation::math::Vec3;
    using mosaic::render::vk::create_offscreen_vulkan_render_device;
    using mosaic::render::vk::OffscreenDevice;

    constexpr std::uint32_t kWidth = 128;
    constexpr std::uint32_t kHeight = 128;
    constexpr std::size_t kPixelCount =
        static_cast<std::size_t>(kWidth) * static_cast<std::size_t>(kHeight);

    // Clear and quad colours differ in every RGB channel, so clear and quad can never be
    // confused for one another (the orientation test's convention).
    constexpr Color kClearColor{0.125f, 0.25f, 0.5f, 1.0f};
    constexpr Vec3 kQuadColor{0.75f, 0.5f, 0.125f};
    constexpr Vec3 kSecondColor{0.25f, 0.75f, 0.875f};

    const std::array<std::uint8_t, 4> expected_clear =
        expected_srgb8(kClearColor.r, kClearColor.g, kClearColor.b, kClearColor.a);
    const std::array<std::uint8_t, 4> expected_quad =
        expected_srgb8(kQuadColor.x, kQuadColor.y, kQuadColor.z, 1.0f);
    const std::array<std::uint8_t, 4> expected_second =
        expected_srgb8(kSecondColor.x, kSecondColor.y, kSecondColor.z, 1.0f);

    // Pinned against the orientation test's own constants: a silent colour-management
    // change must not be absorbed by recomputing both sides of the comparison.
    REQUIRE(expected_clear == std::array<std::uint8_t, 4>{99, 137, 188, 255});
    REQUIRE(expected_quad == std::array<std::uint8_t, 4>{225, 188, 99, 255});
    REQUIRE(expected_second == std::array<std::uint8_t, 4>{137, 225, 240, 255});

    // A quad spanning the middle half horizontally and the full height, clipped to the
    // surface's TOP half. Every clip edge lands on a whole pixel at 128x128 (0.5 -> 64),
    // so the assertions below do not depend on how a partially covered edge pixel rounds.
    const Rect quad_rect{0.25f, 0.0f, 0.5f, 1.0f};   // x in [32,96), y in [0,128)
    const Rect top_half{0.0f, 0.0f, 1.0f, 0.5f};     // y in [0,64)
    const UiQuad quad{quad_rect, kQuadColor, 1.0f};

    // Inside the clip and inside the quad; outside the clip but still inside the quad.
    constexpr std::uint32_t kInsideX = 64, kInsideY = 32;
    constexpr std::uint32_t kOutsideX = 64, kOutsideY = 96;

    auto capture_sink = std::make_shared<ValidationCaptureSink>();
    mosaic::foundation::log::clear_sinks();
    mosaic::foundation::log::add_sink(capture_sink);

    {
        OffscreenDevice offscreen = create_offscreen_vulkan_render_device(kWidth, kHeight);
        REQUIRE(offscreen.device != nullptr);
        REQUIRE(offscreen.readback != nullptr);

        // --- reference: the SAME quad with no clip ---------------------------------
        offscreen.device->clear(kClearColor);
        offscreen.device->begin_frame();
        offscreen.device->draw_ui({quad});
        offscreen.device->end_frame();
        const std::vector<std::uint8_t> unclipped = offscreen.readback->read_pixels();
        REQUIRE(unclipped.size() == kPixelCount * 4u);

        // The reference must actually cover both probe points, or "masked" below would be
        // trivially satisfiable by a quad that was never drawn at all.
        require_pixel_near(unclipped, kWidth, kInsideX, kInsideY, expected_quad,
                           "unclipped reference, inside-clip probe");
        require_pixel_near(unclipped, kWidth, kOutsideX, kOutsideY, expected_quad,
                           "unclipped reference, outside-clip probe");

        // --- the clipped draw ------------------------------------------------------
        offscreen.device->clear(kClearColor);
        offscreen.device->begin_frame();
        offscreen.device->draw_ui({quad}, top_half);
        offscreen.device->end_frame();
        const std::vector<std::uint8_t> clipped = offscreen.readback->read_pixels();
        REQUIRE(clipped.size() == kPixelCount * 4u);

        // Half 1: outside the clip is exactly the clear colour. THIS is the assertion
        // that deleting the vkCmdSetScissor in set_ui_clip must break.
        require_pixel_near(clipped, kWidth, kOutsideX, kOutsideY, expected_clear,
                           "clipped: below the clip, inside the quad, must be clear");

        // Half 2: inside the clip is identical to the unclipped render, byte for byte
        // across the whole clipped region -- not merely at a probe point.
        {
            std::size_t differing = 0;
            std::size_t first_x = 0, first_y = 0;
            for (std::uint32_t y = 0; y < kHeight / 2u; ++y)
            {
                for (std::uint32_t x = 0; x < kWidth; ++x)
                {
                    const std::size_t idx =
                        (static_cast<std::size_t>(y) * kWidth + x) * 4u;
                    for (std::size_t c = 0; c < 4u; ++c)
                    {
                        if (clipped[idx + c] != unclipped[idx + c])
                        {
                            if (differing == 0) { first_x = x; first_y = y; }
                            ++differing;
                            break;
                        }
                    }
                }
            }
            INFO("pixels differing inside the clip: " << differing
                 << " (first at " << first_x << ", " << first_y << ")");
            REQUIRE(differing == 0);
        }

        // And the whole region below the clip is clear, so nothing of the quad survived
        // anywhere outside it.
        {
            std::size_t non_clear = 0;
            std::size_t first_x = 0, first_y = 0;
            for (std::uint32_t y = kHeight / 2u; y < kHeight; ++y)
            {
                for (std::uint32_t x = 0; x < kWidth; ++x)
                {
                    const std::array<std::uint8_t, 4> p = pixel_at(clipped, kWidth, x, y);
                    bool near_clear = true;
                    for (std::size_t c = 0; c < 4u; ++c)
                    {
                        if (std::abs(static_cast<int>(p[c]) -
                                     static_cast<int>(expected_clear[c])) > 1)
                        {
                            near_clear = false;
                        }
                    }
                    if (!near_clear)
                    {
                        if (non_clear == 0) { first_x = x; first_y = y; }
                        ++non_clear;
                    }
                }
            }
            INFO("non-clear pixels below the clip: " << non_clear
                 << " (first at " << first_x << ", " << first_y << ")");
            REQUIRE(non_clear == 0);
        }

        // --- two draws with DIFFERENT clips in one frame ---------------------------
        // The one-instance-fixture lesson: one clipped draw per frame cannot show that
        // the clip is per-CALL rather than per-frame. Quad A is clipped to the TOP half,
        // then quad B (a full-surface quad) to the BOTTOM-RIGHT quarter, and each must
        // appear in its own clip and nowhere else.
        //
        // What this does NOT catch, measured rather than assumed: deleting the restore
        // in restore_ui_clip leaves this block green. A clipped draw sets its own
        // scissor, which OVERWRITES a leaked one rather than intersecting with it, so a
        // clipped draw can never be the victim of a leak. Only a draw that passes no
        // clip -- and so records no scissor of its own -- can be, which is why the
        // trailing unclipped draw below is a separate case and not a flourish.
        const Rect bottom_right{0.5f, 0.5f, 0.5f, 0.5f};
        const UiQuad second{Rect{0.0f, 0.0f, 1.0f, 1.0f}, kSecondColor, 1.0f};

        offscreen.device->clear(kClearColor);
        offscreen.device->begin_frame();
        offscreen.device->draw_ui({quad}, top_half);
        offscreen.device->draw_ui({second}, bottom_right);
        offscreen.device->end_frame();
        const std::vector<std::uint8_t> two = offscreen.readback->read_pixels();
        REQUIRE(two.size() == kPixelCount * 4u);

        // B is present in its own quarter.
        require_pixel_near(two, kWidth, 96, 96, expected_second,
                           "two clips: B inside its own bottom-right clip");
        // A is still present in the top half where B's clip does not reach.
        require_pixel_near(two, kWidth, 64, 32, expected_quad,
                           "two clips: A inside the top-half clip");
        // B did not escape its clip into the bottom-LEFT quarter...
        require_pixel_near(two, kWidth, 32, 96, expected_clear,
                           "two clips: bottom-left is outside both clips");
        // ...nor into the top-left, which A's clip covers but A's quad does not.
        require_pixel_near(two, kWidth, 8, 32, expected_clear,
                           "two clips: top-left is inside A's clip but outside A's quad");

        // --- a third, UNCLIPPED draw after a clipped one --------------------------
        // The regression a per-call clip is supposed to make impossible, and the one an
        // unclipped call cannot defend itself against: passing nullopt records no scissor
        // of its own, so it inherits whatever the previous draw left set.
        //
        // This is the assertion that deleting restore_ui_clip's vkCmdSetScissor turns
        // red (confirmed by running that mutation): the leaked top-half clip suppresses
        // the bottom half of a draw that asked for no clip at all, and the pixel below
        // comes back at the clear colour instead of B's.
        offscreen.device->clear(kClearColor);
        offscreen.device->begin_frame();
        offscreen.device->draw_ui({quad}, top_half);
        offscreen.device->draw_ui({second});  // no clip: must cover the whole surface
        offscreen.device->end_frame();
        const std::vector<std::uint8_t> trailing = offscreen.readback->read_pixels();
        REQUIRE(trailing.size() == kPixelCount * 4u);

        require_pixel_near(trailing, kWidth, 64, 96, expected_second,
                           "unclipped draw after a clipped one covers the bottom half");
        require_pixel_near(trailing, kWidth, 64, 32, expected_second,
                           "unclipped draw after a clipped one covers the top half too");
    }

    const std::vector<std::string> validation_messages = capture_sink->messages();
    mosaic::foundation::log::clear_sinks();
    // A scissor outside the render area is a validation ERROR, not a wrong pixel, so this
    // is the half of the gate the pixel assertions above cannot express.
    INFO("validation messages: " << validation_messages.size());
    REQUIRE(validation_messages.empty());
}

// An out-of-range clip must produce a VALID scissor, not a validation error. The pixel
// assertions are secondary here; the point is that the offscreen device's validation
// capture stays empty while being handed rects no author should write but arithmetic can
// produce.
TEST_CASE("gpu offscreen an out-of-range UI clip is clamped rather than invalid",
          "[gpu][render][vulkan]")
{
    using mosaic::engine::render::Color;
    using mosaic::engine::render::UiQuad;
    using mosaic::engine::ui::Rect;
    using mosaic::foundation::math::Vec3;
    using mosaic::render::vk::create_offscreen_vulkan_render_device;
    using mosaic::render::vk::OffscreenDevice;

    constexpr std::uint32_t kWidth = 64;
    constexpr std::uint32_t kHeight = 64;
    constexpr Color kClearColor{0.125f, 0.25f, 0.5f, 1.0f};
    constexpr Vec3 kQuadColor{0.75f, 0.5f, 0.125f};

    const std::array<std::uint8_t, 4> expected_quad =
        expected_srgb8(kQuadColor.x, kQuadColor.y, kQuadColor.z, 1.0f);

    auto capture_sink = std::make_shared<ValidationCaptureSink>();
    mosaic::foundation::log::clear_sinks();
    mosaic::foundation::log::add_sink(capture_sink);

    {
        OffscreenDevice offscreen = create_offscreen_vulkan_render_device(kWidth, kHeight);
        REQUIRE(offscreen.device != nullptr);

        const UiQuad full{Rect{0.0f, 0.0f, 1.0f, 1.0f}, kQuadColor, 1.0f};

        offscreen.device->clear(kClearColor);
        offscreen.device->begin_frame();
        offscreen.device->draw_ui({full}, Rect{-1.0f, -1.0f, 4.0f, 4.0f});  // overhangs
        offscreen.device->draw_ui({full}, Rect{0.5f, 0.5f, 10.0f, 10.0f});  // runs off
        offscreen.device->draw_ui({full}, Rect{3.0f, 3.0f, 1.0f, 1.0f});    // fully off
        offscreen.device->draw_ui({full}, Rect{0.5f, 0.5f, -1.0f, -1.0f});  // negative
        offscreen.device->end_frame();

        const std::vector<std::uint8_t> pixels = offscreen.readback->read_pixels();
        REQUIRE(pixels.size() ==
                static_cast<std::size_t>(kWidth) * kHeight * 4u);

        // The first clip overhangs on every side, so after clamping it is the full
        // surface and the quad is drawn everywhere.
        require_pixel_near(pixels, kWidth, 4, 4, expected_quad,
                           "an overhanging clip still covers the surface");
        require_pixel_near(pixels, kWidth, 60, 60, expected_quad,
                           "an overhanging clip still covers the far corner");
    }

    const std::vector<std::string> validation_messages = capture_sink->messages();
    mosaic::foundation::log::clear_sinks();
    INFO("validation messages: " << validation_messages.size());
    REQUIRE(validation_messages.empty());
}
