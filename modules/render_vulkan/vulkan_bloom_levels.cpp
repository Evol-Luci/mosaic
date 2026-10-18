#include "modules/render_vulkan/vulkan_bloom_levels.hpp"

namespace mosaic::render::vk
{

std::uint32_t bloom_level_count(std::uint32_t width, std::uint32_t height)
{
    if (width == 0 || height == 0)
    {
        return 0;
    }

    // Mip 0 is half resolution, floored at 1: a 1-pixel-wide window still gets a chain,
    // because the alternative is a special case in every caller.
    std::uint32_t w = width / 2 > 0 ? width / 2 : 1;
    std::uint32_t h = height / 2 > 0 ? height / 2 : 1;

    std::uint32_t levels = 1;
    while (levels < kBloomMaxLevels)
    {
        const std::uint32_t nw = w / 2;
        const std::uint32_t nh = h / 2;
        // The SMALLER dimension decides, so a letterboxed window does not earn extra
        // levels from its long edge.
        if (nw < kBloomMinExtent || nh < kBloomMinExtent)
        {
            break;
        }
        w = nw;
        h = nh;
        ++levels;
    }
    return levels;
}

} // namespace mosaic::render::vk
