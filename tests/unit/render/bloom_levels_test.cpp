#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "modules/render_vulkan/vulkan_bloom_levels.hpp"

using mosaic::render::vk::bloom_level_count;
using mosaic::render::vk::kBloomMaxLevels;

TEST_CASE("bloom_level_count clamps at six on a 1080p swapchain", "[render][bloom]")
{
    // The spec's worked example: mip 0 is half the swapchain, so 960x540, and halving
    // reaches 30x16 at level 6. Beyond six each step costs a render pass and contributes
    // almost nothing, so the clamp is a budget and not a limit of the math.
    REQUIRE(bloom_level_count(1920, 1080) == 6);
    REQUIRE(kBloomMaxLevels == 6);
}

TEST_CASE("bloom_level_count never returns zero for a drawable swapchain", "[render][bloom]")
{
    // A window this small gives a 1-level chain: prefilter only, with the downsample and
    // upsample loops running N-1 = 0 times. Both of those counters are unsigned in the
    // backend, so a zero here is the difference between "no blur" and "loop 4294967295
    // times".
    REQUIRE(bloom_level_count(1, 1) == 1);
    REQUIRE(bloom_level_count(2, 2) == 1);
    REQUIRE(bloom_level_count(32, 24) == 1);
}

TEST_CASE("bloom_level_count stops before a mip falls under eight pixels", "[render][bloom]")
{
    // 64x64 -> mip0 32x32, mip1 16x16, mip2 8x8; 4x4 would be next and is refused.
    // A 4x4 mip blurred by a 4-tap kernel is one colour, which is a pass spent to learn
    // the average of the image.
    REQUIRE(bloom_level_count(64, 64) == 3);
    // The SMALLER dimension decides, so a letterboxed window does not get extra levels
    // from its long edge: 1024x64 -> mip0 512x32, mip1 256x16, mip2 128x8, stop.
    REQUIRE(bloom_level_count(1024, 64) == 3);
}

TEST_CASE("bloom_level_count returns zero for a zero extent", "[render][bloom]")
{
    // A minimised window. The backend skips creating the chain entirely rather than
    // creating a degenerate one, which matches what create_hdr_target already does.
    REQUIRE(bloom_level_count(0, 1080) == 0);
    REQUIRE(bloom_level_count(1920, 0) == 0);
    REQUIRE(bloom_level_count(0, 0) == 0);
}
