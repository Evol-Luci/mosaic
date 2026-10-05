#pragma once
#include <cstdint>

namespace mosaic::engine::render
{

// How many mip levels a texture of this size has, counting the base level.
//
// floor(log2(max(w, h))) + 1: each level halves both dimensions, rounding down but
// never below 1, and the chain ends at the 1x1 level. A 64x64 texture has 7 levels
// (64, 32, 16, 8, 4, 2, 1); a 100x1 texture has 7 as well, because the chain is driven
// by the LARGER dimension and the smaller one simply stops shrinking once it reaches 1.
//
// Lives in engine/ rather than inside the Vulkan backend so the arithmetic can be
// tested without a GPU, which is the only part of mip generation that CI can check.
// A zero dimension has no levels. create_texture turns that into a 0 handle before
// asking, so it is unreachable there -- but the value is defined rather than left to a
// caller's assumption, because mipLevels = 0 is a Vulkan VU violation.
constexpr std::uint32_t mip_levels_for(std::uint32_t width, std::uint32_t height)
{
    if (width == 0 || height == 0)
    {
        return 0;
    }
    std::uint32_t largest = (width > height) ? width : height;
    std::uint32_t levels = 1;
    while (largest > 1)
    {
        largest /= 2;
        ++levels;
    }
    return levels;
}


// The extent of one dimension at `level`, matching Vulkan's own level sizing:
// max(1, base >> level). Every level floors at 1 independently, which is what makes a
// non-square chain correct -- a 100x1 texture's height stays 1 for all 7 levels while
// its width keeps halving.
//
// This lives beside mip_levels_for ON PURPOSE. The backend's blit ladder depends on the
// two AGREEING: the chain must land on exactly 1x1 at level mip_levels_for - 1 and never
// on 0. When the loop restated the halving itself, dropping its floor-at-1 guard -- a
// one-token edit that reads as a simplification -- produced a zero-extent blit region on
// any non-square texture, with both the arithmetic test and the source contract still
// green. One tested source instead of two independent restatements. Review 2026-10-05.
constexpr std::uint32_t mip_extent_for(std::uint32_t base, std::uint32_t level)
{
    std::uint32_t e = base;
    for (std::uint32_t i = 0; i < level; ++i)
    {
        e = (e > 1) ? e / 2 : 1;
    }
    return (e > 0) ? e : 1;
}

} // namespace mosaic::engine::render
