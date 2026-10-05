// The only part of mip generation CI can check without a GPU.
//
// Getting the level count wrong is not a crash: too few leaves the tail of the chain
// never written, so minified samples read undefined memory; too many fails image
// creation outright. Neither is something a shader contract or a draw-call assertion
// can see, so the arithmetic is pulled out of the backend and tested directly.

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "engine/render/mip_levels.hpp"

using mosaic::engine::render::mip_levels_for;

TEST_CASE("mip_levels_for counts the base level plus every halving", "[render]")
{
    // Powers of two: the chain is exactly log2 + 1.
    REQUIRE(mip_levels_for(1, 1) == 1);
    REQUIRE(mip_levels_for(2, 2) == 2);
    REQUIRE(mip_levels_for(64, 64) == 7);   // 64 32 16 8 4 2 1
    REQUIRE(mip_levels_for(1024, 1024) == 11);
}

TEST_CASE("mip_levels_for is driven by the larger dimension", "[render]")
{
    // Non-square and non-power-of-two are the cases a naive implementation gets wrong.
    // The smaller dimension stops shrinking at 1 while the larger keeps halving, so the
    // chain length follows the larger one -- using the smaller would truncate it and
    // leave levels unwritten.
    REQUIRE(mip_levels_for(64, 1) == 7);
    REQUIRE(mip_levels_for(1, 64) == 7);
    REQUIRE(mip_levels_for(100, 1) == 7);   // floor(log2(100)) + 1
    REQUIRE(mip_levels_for(3, 3) == 2);     // floor(log2(3)) + 1
    REQUIRE(mip_levels_for(5, 3) == 3);     // driven by 5, not 3
    REQUIRE(mip_levels_for(640, 480) == 10);
}

TEST_CASE("mip_levels_for reports no levels for an empty image", "[render]")
{
    // create_texture already returns 0 for an empty image; this keeps the arithmetic
    // from being the thing that decides, and makes a zero dimension unambiguous rather
    // than producing a one-level chain over nothing.
    REQUIRE(mip_levels_for(0, 0) == 0);
    REQUIRE(mip_levels_for(0, 16) == 0);
    REQUIRE(mip_levels_for(16, 0) == 0);
}
