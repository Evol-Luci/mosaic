#include "modules/render_vulkan/vulkan_render_device.hpp"
#include "modules/render_vulkan/vulkan_ui_draw_stats.hpp"

#include "engine/render/render_device.hpp"
#include "engine/ui/ui_rect.hpp"
#include "foundation/math/vec3.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

// THE RENDERER GATE, as a test instead of a sentence.
//
// What this replaces. Until 2026-10-30 the project's renderer gate was a command typed by
// hand and written down in `docs/project/landmines.md`:
//
//     timeout 12 env MOSAIC_VK_SYNC_VALIDATION=1 ./build/debug/app/mosaic editor
//
// with PASS defined as exit 124, three `[INFO]` lines and no VUID/ERROR/WARN. Measured
// twice (2026-10-27, re-measured 2026-10-30 on this branch) that run reaches ZERO UI
// draws, so its pass was indistinguishable from the process still starting up -- and a
// deliberately invalid scissor forced into `clip_to_pixel_rect` left it green, with zero
// validation output, at 12 s AND at 25 s. It was a gate that could not fail.
//
// Why this one can. It asserts the precondition the old gate assumed and never checked:
// that UI draws reached a command buffer at all. `ui_draws` is incremented inside
// `draw_ui`/`draw_ui_image`/`draw_ui_text` AFTER their early returns, so it counts
// recorded draws and not intentions. Every assertion below about scissors is reached only
// once that count has moved.
//
// Why not just a bigger timeout. 18 s of GUI startup to reach a first UI draw was the
// figure in the handoff; this branch did not reach one in 25 s. Waiting is not an
// assertion, and a gate that passes by being lucky with wall-clock time is the defect, not
// the timeout value.
//
// Why the scissor check is arithmetic and not the validation layer. The layer is compiled
// in only `#ifndef NDEBUG` and used only when it is installed, so an assertion resting on
// it passes for free where it is absent. `scissor_in_render_area` is four integer
// comparisons over the same rect vkCmdSetScissor would see, evaluated unconditionally.
// (Making the EXISTING `validation_messages.empty()` assertions in this directory
// non-vacuous is a separate, tracked piece of work; this test deliberately does not rely
// on them and does not touch them.)
TEST_CASE("gpu offscreen the UI gate reaches a real UI draw and every scissor it records is in the render area",
          "[gpu][render][vulkan][gate]")
{
    using mosaic::engine::render::Color;
    using mosaic::engine::render::UiQuad;
    using mosaic::engine::ui::Rect;
    using mosaic::foundation::math::Vec3;
    using mosaic::render::vk::create_offscreen_vulkan_render_device;
    using mosaic::render::vk::OffscreenDevice;
    using mosaic::render::vk::UiDrawStats;

    constexpr std::uint32_t kWidth = 96;
    constexpr std::uint32_t kHeight = 64;
    constexpr Color kClearColor{0.125f, 0.25f, 0.5f, 1.0f};
    constexpr Vec3 kQuadColor{0.75f, 0.5f, 0.125f};

    OffscreenDevice offscreen = create_offscreen_vulkan_render_device(kWidth, kHeight);
    REQUIRE(offscreen.device != nullptr);
    REQUIRE(offscreen.ui_stats != nullptr);

    // A fresh device has recorded nothing. Asserting this is not ceremony: if the counter
    // were wired to something that is non-zero from construction, every assertion below
    // would pass without a draw ever running, which is the exact failure mode this test
    // exists to close.
    {
        const UiDrawStats& before = offscreen.ui_stats->ui_draw_stats();
        INFO("a fresh device reports ui_draws=" << before.ui_draws
             << " clipped_ui_draws=" << before.clipped_ui_draws);
        REQUIRE(before.ui_draws == 0);
        REQUIRE(before.clipped_ui_draws == 0);
        REQUIRE(before.out_of_area_scissors == 0);
    }

    const UiQuad full{Rect{0.0f, 0.0f, 1.0f, 1.0f}, kQuadColor, 1.0f};

    // The clips span the cases `clip_to_pixel_rect` has to survive, in ONE frame, because
    // a single well-behaved clip cannot show that a bad one would be caught:
    //
    //   - an ordinary interior clip, every edge on a whole pixel;
    //   - one overhanging every side, which clamping must pull back to the full surface;
    //   - one starting inside and running off the far corner;
    //   - one entirely off the surface;
    //   - one with negative extent, which must collapse rather than wrap;
    //   - one at the exact far corner, where an off-by-one in either direction is
    //     out-of-area on one side and silently narrow on the other.
    const std::vector<Rect> clips{
        Rect{0.25f, 0.25f, 0.5f, 0.5f},
        Rect{-1.0f, -1.0f, 4.0f, 4.0f},
        Rect{0.5f, 0.5f, 10.0f, 10.0f},
        Rect{3.0f, 3.0f, 1.0f, 1.0f},
        Rect{0.5f, 0.5f, -1.0f, -1.0f},
        Rect{1.0f, 1.0f, 0.0f, 0.0f},
    };

    offscreen.device->clear(kClearColor);
    offscreen.device->begin_frame();
    for (const Rect& clip : clips) { offscreen.device->draw_ui({full}, clip); }
    // One unclipped draw as well, so `ui_draws` counting more than `clipped_ui_draws` is
    // asserted rather than assumed -- an unclipped call must record no scissor of its own.
    offscreen.device->draw_ui({full});
    offscreen.device->end_frame();

    const UiDrawStats& after = offscreen.ui_stats->ui_draw_stats();

    // --- the gate's first half: IT REACHED A UI FRAME --------------------------------
    // This is the assertion the old gate had no equivalent of. If the UI path never ran,
    // this fails here and the scissor assertions below are never credited.
    INFO("after one frame: ui_draws=" << after.ui_draws
         << " clipped_ui_draws=" << after.clipped_ui_draws
         << " out_of_area_scissors=" << after.out_of_area_scissors);
    REQUIRE(after.ui_draws == clips.size() + 1u);
    REQUIRE(after.clipped_ui_draws == clips.size());

    // --- the gate's second half: EVERY SCISSOR WAS LEGAL ------------------------------
    // The failure message names the offending rect, because "1 out-of-area scissor" sends
    // the next reader back to a debugger for the numbers.
    {
        const auto& r = after.first_out_of_area;
        INFO("first out-of-area scissor: offset=(" << r.x << ", " << r.y << ") extent=("
             << r.width << " x " << r.height << ") against a render area of "
             << after.first_out_of_area_dest_width << " x "
             << after.first_out_of_area_dest_height);
        REQUIRE(after.out_of_area_scissors == 0);
    }

    // The frame must also have survived: read_pixels after end_frame is what proves the
    // command buffer was submitted and executed rather than merely recorded, so a scissor
    // the driver rejected cannot hide behind a frame that never ran.
    const std::vector<std::uint8_t> pixels = offscreen.readback->read_pixels();
    REQUIRE(pixels.size() ==
            static_cast<std::size_t>(kWidth) * static_cast<std::size_t>(kHeight) * 4u);
}
