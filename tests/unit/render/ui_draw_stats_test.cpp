#include "modules/render_vulkan/vulkan_ui_draw_stats.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>

// The predicate the renderer gate now rests on.
//
// `scissor_in_render_area` is what the GPU gate
// (tests/gpu/offscreen_ui_frame_reached_test.cpp) asks about every scissor the device
// records. That gate needs a VkDevice and is opt-in locally, so the arithmetic itself is
// pinned here, where it is free: a predicate that answered "in area" for everything would
// make the gate green again without touching a single line of it.
//
// The cases are the four VUIDs vkCmdSetScissor enforces, one per rule, plus the two that
// have bitten in practice -- a zero extent (LEGAL, and relied on by a fully-clipped
// subtree) and an int32/uint32 sum that wraps.

using mosaic::render::vk::ClipPixelRect;
using mosaic::render::vk::record_ui_clip_scissor;
using mosaic::render::vk::scissor_in_render_area;
using mosaic::render::vk::UiDrawStats;

TEST_CASE("a scissor covering the whole render area is in area", "[render][ui]")
{
    REQUIRE(scissor_in_render_area(ClipPixelRect{0, 0, 128u, 64u}, 128, 64));
}

TEST_CASE("an interior scissor is in area", "[render][ui]")
{
    REQUIRE(scissor_in_render_area(ClipPixelRect{32, 16, 32u, 16u}, 128, 64));
}

TEST_CASE("a scissor flush against the far corner is in area", "[render][ui]")
{
    // The off-by-one boundary: offset + extent == the render area is legal, one more is
    // not. Both directions are asserted so a <= silently becoming < is caught too.
    REQUIRE(scissor_in_render_area(ClipPixelRect{120, 60, 8u, 4u}, 128, 64));
    REQUIRE_FALSE(scissor_in_render_area(ClipPixelRect{120, 60, 9u, 4u}, 128, 64));
    REQUIRE_FALSE(scissor_in_render_area(ClipPixelRect{120, 60, 8u, 5u}, 128, 64));
}

TEST_CASE("a zero-extent scissor is in area and not degenerate", "[render][ui]")
{
    // A zero extent draws nothing, which is what a fully-clipped-away subtree should
    // render. Treating it as invalid would turn correct behaviour into a gate failure.
    REQUIRE(scissor_in_render_area(ClipPixelRect{0, 0, 0u, 0u}, 128, 64));
    REQUIRE(scissor_in_render_area(ClipPixelRect{128, 64, 0u, 0u}, 128, 64));
}

TEST_CASE("a negative scissor offset is out of area on either axis", "[render][ui]")
{
    // VUID-vkCmdSetScissor-x-00595. Each axis separately: a check that only looked at x
    // would pass the y case.
    REQUIRE_FALSE(scissor_in_render_area(ClipPixelRect{-1, 0, 8u, 8u}, 128, 64));
    REQUIRE_FALSE(scissor_in_render_area(ClipPixelRect{0, -1, 8u, 8u}, 128, 64));
    REQUIRE_FALSE(scissor_in_render_area(ClipPixelRect{-4, -4, 128u, 64u}, 128, 64));
}

TEST_CASE("a scissor running past the render area is out of area on either axis",
          "[render][ui]")
{
    REQUIRE_FALSE(scissor_in_render_area(ClipPixelRect{0, 0, 129u, 64u}, 128, 64));
    REQUIRE_FALSE(scissor_in_render_area(ClipPixelRect{0, 0, 128u, 65u}, 128, 64));
    REQUIRE_FALSE(scissor_in_render_area(ClipPixelRect{64, 32, 128u, 64u}, 128, 64));
}

TEST_CASE("a scissor whose offset plus extent would wrap is out of area", "[render][ui]")
{
    // The reason the sums are taken in 64-bit. In int32 arithmetic
    // 2147483647 + 2 is negative, which would read as comfortably inside the area.
    constexpr std::int32_t kMaxOffset = std::numeric_limits<std::int32_t>::max();
    REQUIRE_FALSE(scissor_in_render_area(ClipPixelRect{kMaxOffset, 0, 2u, 0u}, 128, 64));
    REQUIRE_FALSE(scissor_in_render_area(ClipPixelRect{0, kMaxOffset, 0u, 2u}, 128, 64));
    REQUIRE_FALSE(scissor_in_render_area(
        ClipPixelRect{0, 0, std::numeric_limits<std::uint32_t>::max(), 0u}, 128, 64));
}

TEST_CASE("recording a clipped scissor counts it and remembers the first offender",
          "[render][ui]")
{
    UiDrawStats stats{};

    record_ui_clip_scissor(stats, ClipPixelRect{0, 0, 128u, 64u}, 128, 64);
    REQUIRE(stats.clipped_ui_draws == 1);
    REQUIRE(stats.out_of_area_scissors == 0);
    // A legal scissor must not be counted as the first offender, or the failure message
    // for a later real one would name the wrong rect.
    REQUIRE(stats.first_out_of_area_dest_width == 0u);

    record_ui_clip_scissor(stats, ClipPixelRect{-7, 0, 8u, 8u}, 128, 64);
    REQUIRE(stats.clipped_ui_draws == 2);
    REQUIRE(stats.out_of_area_scissors == 1);
    REQUIRE(stats.first_out_of_area.x == -7);
    REQUIRE(stats.first_out_of_area_dest_width == 128u);
    REQUIRE(stats.first_out_of_area_dest_height == 64u);

    // A SECOND offender must not overwrite the first: the earliest one is the one closest
    // to the cause.
    record_ui_clip_scissor(stats, ClipPixelRect{0, -9, 8u, 8u}, 128, 64);
    REQUIRE(stats.out_of_area_scissors == 2);
    REQUIRE(stats.first_out_of_area.x == -7);
    REQUIRE(stats.first_out_of_area.y == 0);

    // Nothing here touches the reached-a-draw counter: it is incremented by the draw
    // paths, not by recording a scissor.
    REQUIRE(stats.ui_draws == 0);
}
