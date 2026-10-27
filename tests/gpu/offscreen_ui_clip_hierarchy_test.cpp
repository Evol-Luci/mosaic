#include "modules/render_vulkan/vulkan_render_device.hpp"

#include "engine/render/render_device.hpp"
#include "engine/ui/ui_clip_rect.hpp"
#include "engine/ui/ui_panel.hpp"
#include "engine/ui/ui_parent.hpp"
#include "engine/ui/ui_rect.hpp"
#include "engine/world/world.hpp"
#include "foundation/logging/log.hpp"
#include "foundation/math/vec3.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

std::array<std::uint8_t, 4> pixel_at(const std::vector<std::uint8_t>& pixels,
                                     std::uint32_t width, std::uint32_t x, std::uint32_t y)
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

void require_pixel_near(const std::vector<std::uint8_t>& pixels, std::uint32_t width,
                        std::uint32_t x, std::uint32_t y,
                        const std::array<std::uint8_t, 4>& expected, const char* label)
{
    const std::array<std::uint8_t, 4> actual = pixel_at(pixels, width, x, y);
    INFO(label << " at (" << x << ", " << y << ") actual=" << format_pixel(actual)
               << " expected=" << format_pixel(expected));
    REQUIRE(std::abs(static_cast<int>(actual[0]) - static_cast<int>(expected[0])) <= 1);
    REQUIRE(std::abs(static_cast<int>(actual[1]) - static_cast<int>(expected[1])) <= 1);
    REQUIRE(std::abs(static_cast<int>(actual[2]) - static_cast<int>(expected[2])) <= 1);
    REQUIRE(std::abs(static_cast<int>(actual[3]) - static_cast<int>(expected[3])) <= 1);
}

mosaic::engine::ui::UiPanel panel_at(float x, float y, float w, float h)
{
    mosaic::engine::ui::UiPanel p;
    p.anchor = static_cast<int>(mosaic::engine::ui::Anchor::TopLeft);
    p.pos    = {x, y};
    p.size   = {w, h};
    return p;
}

} // namespace

// The U9b gate, in pixels: a clip RESOLVED FROM A REAL HIERARCHY, through the real backend,
// for a NESTED case whose two candidate rules give different answers.
//
// What makes this test worth having beside U9a's: U9a hands the device a clip the test
// itself wrote down, so it proves the scissor works and nothing about where a clip comes
// from. Here the clip comes out of resolve_clip over a World, so the component, the
// parent-chain walk, the intersection and the scissor are one chain and a break anywhere in
// it shows up as a pixel.
//
// THE ARRANGEMENT, chosen so that "intersect every ancestor" and "use the innermost clip"
// disagree -- which they only do when an inner clip reaches OUTSIDE its ancestor:
//
//   outer  clips to x in [0.00, 0.50)   -> pixels   0..63
//   inner  is a child of outer authored pos {0.5, 0} size {1, 1} of outer's frame,
//          so it resolves to x in [0.25, 0.75)  -> pixels  32..95, HANGING OUT PAST outer
//   leaf   is a full-surface quad under inner
//
//   intersection:      x in [0.25, 0.50)  -> pixels 32..63   <- the correct clip
//   innermost only:    x in [0.25, 0.75)  -> pixels 32..95   <- the bug this guards
//
// Both clips are full height and every edge lands on a whole pixel at 128 wide (0.25 -> 32,
// 0.5 -> 64, 0.75 -> 96), so no assertion here depends on how a partially covered edge
// pixel rounds -- the same precaution U9a's test takes, and the reason the fractional-edge
// rounding policy is still deferred to U9d.
TEST_CASE("gpu offscreen a clip resolved from a nested hierarchy masks to the INTERSECTION",
          "[gpu][render][vulkan][clip]")
{
    using mosaic::engine::World;
    using mosaic::engine::render::Color;
    using mosaic::engine::render::UiQuad;
    using mosaic::engine::ui::Rect;
    using mosaic::engine::ui::resolve_clip;
    using mosaic::engine::ui::UiClipRect;
    using mosaic::engine::ui::UiPanel;
    using mosaic::engine::ui::UiParent;
    using mosaic::foundation::math::Vec3;
    using mosaic::render::vk::create_offscreen_vulkan_render_device;
    using mosaic::render::vk::OffscreenDevice;

    constexpr std::uint32_t kWidth = 128;
    constexpr std::uint32_t kHeight = 128;
    constexpr std::size_t kPixelCount =
        static_cast<std::size_t>(kWidth) * static_cast<std::size_t>(kHeight);

    // The orientation test's convention: clear and quad differ in every RGB channel.
    constexpr Color kClearColor{0.125f, 0.25f, 0.5f, 1.0f};
    constexpr Vec3 kQuadColor{0.75f, 0.5f, 0.125f};

    const std::array<std::uint8_t, 4> expected_clear =
        expected_srgb8(kClearColor.r, kClearColor.g, kClearColor.b, kClearColor.a);
    const std::array<std::uint8_t, 4> expected_quad =
        expected_srgb8(kQuadColor.x, kQuadColor.y, kQuadColor.z, 1.0f);
    REQUIRE(expected_clear == std::array<std::uint8_t, 4>{99, 137, 188, 255});
    REQUIRE(expected_quad == std::array<std::uint8_t, 4>{225, 188, 99, 255});

    // --- the hierarchy, and the clip it resolves to -----------------------------
    World      world;
    const auto outer = world.create_entity();
    world.add_component<UiPanel>(outer, panel_at(0.0f, 0.0f, 0.5f, 1.0f));
    world.add_component<UiClipRect>(outer, UiClipRect{});

    const auto inner = world.create_entity();
    world.add_component<UiPanel>(inner, panel_at(0.5f, 0.0f, 1.0f, 1.0f));
    world.add_component<UiParent>(inner, UiParent{outer});
    world.add_component<UiClipRect>(inner, UiClipRect{});

    const auto leaf = world.create_entity();
    world.add_component<UiPanel>(leaf, panel_at(0.0f, 0.0f, 1.0f, 1.0f));
    world.add_component<UiParent>(leaf, UiParent{inner});

    const std::optional<Rect> clip = resolve_clip(world, leaf);
    REQUIRE(clip.has_value());
    // Pinned in normalized space BEFORE any pixel is read, so a failure downstream can be
    // attributed to the scissor rather than to the arithmetic.
    REQUIRE(clip->x == 0.25f);
    REQUIRE(clip->w == 0.25f);

    {
        OffscreenDevice offscreen = create_offscreen_vulkan_render_device(kWidth, kHeight);
        REQUIRE(offscreen.device != nullptr);
        REQUIRE(offscreen.readback != nullptr);

        // A full-surface quad, so every masked pixel below is one the quad genuinely
        // covered -- "masked" cannot be satisfied by a quad that was never there.
        const UiQuad quad{Rect{0.0f, 0.0f, 1.0f, 1.0f}, kQuadColor, 1.0f};

        offscreen.device->clear(kClearColor);
        offscreen.device->begin_frame();
        offscreen.device->draw_ui({quad}, clip);
        offscreen.device->end_frame();
        const std::vector<std::uint8_t> pixels = offscreen.readback->read_pixels();
        REQUIRE(pixels.size() == kPixelCount * 4u);

        // Inside the intersection: the quad survives.
        require_pixel_near(pixels, kWidth, 48, 64, expected_quad,
                           "inside both clips, the quad survives");

        // Inside OUTER but outside INNER -- the half of the intersection an
        // outermost-only rule would wrongly keep.
        require_pixel_near(pixels, kWidth, 16, 64, expected_clear,
                           "inside outer but left of inner, must be clear");

        // Inside INNER but outside OUTER. THIS IS THE DECISIVE PIXEL: "use the innermost
        // clip" paints the quad here, because inner's own rect reaches to pixel 95.
        require_pixel_near(pixels, kWidth, 80, 64, expected_clear,
                           "inside inner but right of outer, must be clear -- the innermost-only bug");

        // Outside both.
        require_pixel_near(pixels, kWidth, 112, 64, expected_clear,
                           "outside both clips, must be clear");

        // And the whole surviving column range is the quad while everything else is clear,
        // so the three probes above are not three lucky pixels.
        std::size_t wrong = 0;
        std::size_t first_x = 0, first_y = 0;
        for (std::uint32_t y = 0; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0; x < kWidth; ++x)
            {
                const bool should_be_quad = (x >= 32u && x < 64u);
                const std::array<std::uint8_t, 4>& want =
                    should_be_quad ? expected_quad : expected_clear;
                const std::array<std::uint8_t, 4> got = pixel_at(pixels, kWidth, x, y);
                bool near = true;
                for (std::size_t c = 0; c < 4u; ++c)
                {
                    if (std::abs(static_cast<int>(got[c]) - static_cast<int>(want[c])) > 1)
                    {
                        near = false;
                    }
                }
                if (!near)
                {
                    if (wrong == 0) { first_x = x; first_y = y; }
                    ++wrong;
                }
            }
        }
        INFO("pixels disagreeing with the intersection mask: " << wrong
             << " (first at " << first_x << ", " << first_y << ")");
        REQUIRE(wrong == 0);
    }
}

// An empty intersection draws nothing at all -- and, just as importantly, does so without a
// validation error. The engine's draw passes skip a fully clipped element before reaching
// the device, so this exercises the case a direct device call still has to survive.
TEST_CASE("gpu offscreen an empty resolved clip leaves the surface untouched",
          "[gpu][render][vulkan][clip]")
{
    using mosaic::engine::World;
    using mosaic::engine::render::Color;
    using mosaic::engine::render::UiQuad;
    using mosaic::engine::ui::is_empty;
    using mosaic::engine::ui::Rect;
    using mosaic::engine::ui::resolve_clip;
    using mosaic::engine::ui::UiClipRect;
    using mosaic::engine::ui::UiPanel;
    using mosaic::engine::ui::UiParent;
    using mosaic::foundation::math::Vec3;
    using mosaic::render::vk::create_offscreen_vulkan_render_device;
    using mosaic::render::vk::OffscreenDevice;

    constexpr std::uint32_t kWidth = 64;
    constexpr std::uint32_t kHeight = 64;
    constexpr Color kClearColor{0.125f, 0.25f, 0.5f, 1.0f};
    constexpr Vec3 kQuadColor{0.75f, 0.5f, 0.125f};
    const std::array<std::uint8_t, 4> expected_clear =
        expected_srgb8(kClearColor.r, kClearColor.g, kClearColor.b, kClearColor.a);

    // Two disjoint nested clips: outer is the left half, inner is pushed fully past it.
    World      world;
    const auto outer = world.create_entity();
    world.add_component<UiPanel>(outer, panel_at(0.0f, 0.0f, 0.5f, 1.0f));
    world.add_component<UiClipRect>(outer, UiClipRect{});
    const auto inner = world.create_entity();
    world.add_component<UiPanel>(inner, panel_at(1.0f, 0.0f, 1.0f, 1.0f));
    world.add_component<UiParent>(inner, UiParent{outer});
    world.add_component<UiClipRect>(inner, UiClipRect{});
    const auto leaf = world.create_entity();
    world.add_component<UiPanel>(leaf, panel_at(0.0f, 0.0f, 1.0f, 1.0f));
    world.add_component<UiParent>(leaf, UiParent{inner});

    const std::optional<Rect> clip = resolve_clip(world, leaf);
    REQUIRE(clip.has_value());
    REQUIRE(is_empty(*clip));

    {
        OffscreenDevice offscreen = create_offscreen_vulkan_render_device(kWidth, kHeight);
        REQUIRE(offscreen.device != nullptr);

        offscreen.device->clear(kClearColor);
        offscreen.device->begin_frame();
        offscreen.device->draw_ui({UiQuad{Rect{0.0f, 0.0f, 1.0f, 1.0f}, kQuadColor, 1.0f}},
                                  clip);
        offscreen.device->end_frame();
        const std::vector<std::uint8_t> pixels = offscreen.readback->read_pixels();

        std::size_t non_clear = 0;
        for (std::uint32_t y = 0; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0; x < kWidth; ++x)
            {
                const std::array<std::uint8_t, 4> p = pixel_at(pixels, kWidth, x, y);
                for (std::size_t c = 0; c < 4u; ++c)
                {
                    if (std::abs(static_cast<int>(p[c]) -
                                 static_cast<int>(expected_clear[c])) > 1)
                    {
                        ++non_clear;
                        break;
                    }
                }
            }
        }
        INFO("non-clear pixels under an empty clip: " << non_clear);
        REQUIRE(non_clear == 0);
    }
}
