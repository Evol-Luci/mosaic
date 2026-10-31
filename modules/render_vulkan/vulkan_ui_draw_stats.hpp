#pragma once

#include <cstdint>

#include "modules/render_vulkan/vulkan_ui_clip.hpp"

namespace mosaic::render::vk
{

// EVIDENCE THAT UI DRAWS REACHED A COMMAND BUFFER, and that every scissor they recorded
// was legal. This type exists because of a gate that could not fail.
//
// The project's renderer gate -- `timeout 12 env MOSAIC_VK_SYNC_VALIDATION=1
// ./build/debug/app/mosaic editor`, PASS = exit 124 -- was read for weeks as covering the
// UI path. It does not: measured 2026-10-27 and re-measured 2026-10-30, that 12 s run
// reaches ZERO UI draws, so a deliberately invalid scissor (a negative offset, or an
// extent running past the render area) left it green with zero validation output. A gate
// whose pass is indistinguishable from a startup timeout asserts nothing -- and raising
// the timeout does not fix it, because WHETHER the run gets there is then the variable:
// the first UI draw lands somewhere between 12 s and 25 s on this machine.
//
// The counters below are what makes "it reached a UI frame" assertable instead of
// inferred. A test that asserts only on pixels cannot tell "the clip was correct" from
// "no UI draw ran at all" except by also pinning an unclipped reference; a test that
// asserts only `validation_messages.empty()` cannot tell "no errors" from "the layer was
// never on". `ui_draws > 0` is the missing precondition for both.
//
// `out_of_area_scissors` is deliberately checked HERE, in plain integer arithmetic, and
// not delegated to the validation layer. The layer is enabled only `#ifndef NDEBUG` and
// only when it is installed, so an assertion resting on it is free to pass where it is
// absent -- exactly the vacuousness this type was added to remove. One scissor's legality
// is four comparisons; borrowing someone else's optional dependency for them is a worse
// trade than doing them.
struct UiDrawStats
{
    // draw_ui / draw_ui_image / draw_ui_text calls that got past their early returns and
    // actually recorded draw commands. This is the "reached a UI frame" counter.
    std::uint64_t ui_draws = 0;
    // Of those, the ones that carried a clip and so recorded a vkCmdSetScissor.
    std::uint64_t clipped_ui_draws = 0;
    // Scissors that vkCmdSetScissor would reject. Any non-zero value is a defect.
    std::uint64_t out_of_area_scissors = 0;

    // The first offender, kept so a failing assertion can name the numbers rather than
    // only the count. Meaningless while out_of_area_scissors == 0.
    ClipPixelRect first_out_of_area{};
    std::uint32_t first_out_of_area_dest_width = 0;
    std::uint32_t first_out_of_area_dest_height = 0;
};

// Whether `rect` is a scissor vkCmdSetScissor will accept for a render area of
// dest_width x dest_height.
//
// The Vulkan rules, and all four are load-bearing: offset.x and offset.y must be >= 0
// (VUID-vkCmdSetScissor-x-00595), and offset + extent must not run past the render area
// (VUID-vkCmdSetScissor-offset-00596/00597). A zero extent is LEGAL and in-area -- it
// draws nothing, which is what a fully-clipped-away subtree should render -- so it must
// not be treated as degenerate here.
//
// The sums are taken in 64-bit because offset is int32 and extent is uint32: an
// `x + width` computed in int32 can overflow to a negative number and read as in-area,
// which is the one way a bad scissor could slip past this predicate.
bool scissor_in_render_area(const ClipPixelRect& rect,
                            std::uint32_t dest_width,
                            std::uint32_t dest_height);

// Count one clipped UI draw's scissor into `stats`, remembering the first illegal one.
void record_ui_clip_scissor(UiDrawStats& stats,
                            const ClipPixelRect& rect,
                            std::uint32_t dest_width,
                            std::uint32_t dest_height);

} // namespace mosaic::render::vk
