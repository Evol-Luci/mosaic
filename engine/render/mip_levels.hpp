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
// A zero dimension has no levels; callers treat that as "nothing to upload".
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

} // namespace mosaic::engine::render
