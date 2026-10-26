#include "modules/render_vulkan/vulkan_ui_clip.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

// The pixel conversion behind draw_ui's clip parameter.
//
// These exist because the GPU pixel test that proves the clip WORKS is opt-in locally and
// runs on lavapipe in CI, and because the failure mode this arithmetic owns -- a scissor
// reaching outside the render area -- is a Vulkan validation error rather than a wrong
// pixel. A validation error is invisible to the whole unit suite (nothing here creates a
// VkDevice) and shows up only in the renderer gate, by which point it is a manual read of
// a log. So the clamp is asserted directly, on the integers, where it is cheap.

using mosaic::engine::ui::Rect;
using mosaic::render::vk::clip_to_pixel_rect;

TEST_CASE("a clip covering the whole surface maps to the full extent", "[render][ui]")
{
    const auto r = clip_to_pixel_rect(Rect{0.0f, 0.0f, 1.0f, 1.0f}, 128, 64);
    REQUIRE(r.x == 0);
    REQUIRE(r.y == 0);
    REQUIRE(r.width == 128u);
    REQUIRE(r.height == 64u);
}

TEST_CASE("a clip's two axes scale against their own dimension", "[render][ui]")
{
    // A non-square destination is the case a single shared divisor gets wrong, and the
    // editor's viewport panel is never square.
    const auto r = clip_to_pixel_rect(Rect{0.5f, 0.25f, 0.25f, 0.5f}, 128, 64);
    REQUIRE(r.x == 64);
    REQUIRE(r.y == 16);
    REQUIRE(r.width == 32u);
    REQUIRE(r.height == 32u);
}

TEST_CASE("a clip hanging off the low edge is TRIMMED, not slid inward", "[render][ui]")
{
    // The mutation this guards: clamping the offset while keeping the computed extent.
    // That would return x = 0, width = 128 here -- the full surface, i.e. no clip at all
    // -- instead of the right-hand half the author asked to keep.
    const auto r = clip_to_pixel_rect(Rect{-0.5f, -0.5f, 1.0f, 1.0f}, 128, 64);
    REQUIRE(r.x == 0);
    REQUIRE(r.y == 0);
    REQUIRE(r.width == 64u);
    REQUIRE(r.height == 32u);
}

TEST_CASE("a clip hanging off the high edge stays inside the render area", "[render][ui]")
{
    // offset + extent must not exceed the render area: outside it is a validation error,
    // not a clamped draw.
    const auto r = clip_to_pixel_rect(Rect{0.75f, 0.75f, 1.0f, 1.0f}, 128, 64);
    REQUIRE(r.x == 96);
    REQUIRE(r.y == 48);
    REQUIRE(static_cast<std::int64_t>(r.x) + r.width == 128);
    REQUIRE(static_cast<std::int64_t>(r.y) + r.height == 64);
}

TEST_CASE("an empty clip collapses to a zero extent", "[render][ui]")
{
    const auto zero = clip_to_pixel_rect(Rect{0.5f, 0.5f, 0.0f, 0.0f}, 128, 64);
    REQUIRE(zero.width == 0u);
    REQUIRE(zero.height == 0u);

    // A fully-clipped-away subtree resolves to an empty intersection, which must draw
    // nothing rather than wrap to a huge unsigned extent.
    const auto negative = clip_to_pixel_rect(Rect{0.5f, 0.5f, -0.25f, -0.25f}, 128, 64);
    REQUIRE(negative.width == 0u);
    REQUIRE(negative.height == 0u);
}

TEST_CASE("a clip entirely outside the surface draws nothing", "[render][ui]")
{
    const auto past_high = clip_to_pixel_rect(Rect{2.0f, 2.0f, 1.0f, 1.0f}, 128, 64);
    REQUIRE(past_high.width == 0u);
    REQUIRE(past_high.height == 0u);

    const auto past_low = clip_to_pixel_rect(Rect{-3.0f, -3.0f, 1.0f, 1.0f}, 128, 64);
    REQUIRE(past_low.x == 0);
    REQUIRE(past_low.y == 0);
    REQUIRE(past_low.width == 0u);
    REQUIRE(past_low.height == 0u);
}

TEST_CASE("a non-finite clip edge yields a valid scissor", "[render][ui]")
{
    // NaN compares false against every bound, so a clamp alone does not contain it and
    // std::lround of it is undefined -- it would reach vkCmdSetScissor as a garbage
    // int32 offset. Authored float rects run through enough arithmetic to produce one.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    const auto with_nan = clip_to_pixel_rect(Rect{nan, nan, 1.0f, 1.0f}, 128, 64);
    REQUIRE(with_nan.x >= 0);
    REQUIRE(with_nan.y >= 0);
    REQUIRE(static_cast<std::int64_t>(with_nan.x) + with_nan.width <= 128);
    REQUIRE(static_cast<std::int64_t>(with_nan.y) + with_nan.height <= 64);

    const auto with_inf = clip_to_pixel_rect(Rect{0.0f, 0.0f, inf, inf}, 128, 64);
    REQUIRE(with_inf.x >= 0);
    REQUIRE(with_inf.y >= 0);
    REQUIRE(static_cast<std::int64_t>(with_inf.x) + with_inf.width <= 128);
    REQUIRE(static_cast<std::int64_t>(with_inf.y) + with_inf.height <= 64);
}

TEST_CASE("a zero-sized destination yields a zero scissor", "[render][ui]")
{
    // A minimised window, which create_hdr_target already skips on.
    const auto r = clip_to_pixel_rect(Rect{0.0f, 0.0f, 1.0f, 1.0f}, 0, 0);
    REQUIRE(r.x == 0);
    REQUIRE(r.y == 0);
    REQUIRE(r.width == 0u);
    REQUIRE(r.height == 0u);
}
