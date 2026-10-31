#include "modules/render_vulkan/vulkan_ui_draw_stats.hpp"

namespace mosaic::render::vk
{

bool scissor_in_render_area(const ClipPixelRect& rect,
                            std::uint32_t dest_width,
                            std::uint32_t dest_height)
{
    if (rect.x < 0 || rect.y < 0) { return false; }
    // 64-bit on purpose: see the header. int32 offset + uint32 extent can wrap.
    const std::int64_t right =
        static_cast<std::int64_t>(rect.x) + static_cast<std::int64_t>(rect.width);
    const std::int64_t bottom =
        static_cast<std::int64_t>(rect.y) + static_cast<std::int64_t>(rect.height);
    return right <= static_cast<std::int64_t>(dest_width) &&
           bottom <= static_cast<std::int64_t>(dest_height);
}

void record_ui_clip_scissor(UiDrawStats& stats,
                            const ClipPixelRect& rect,
                            std::uint32_t dest_width,
                            std::uint32_t dest_height)
{
    ++stats.clipped_ui_draws;
    if (scissor_in_render_area(rect, dest_width, dest_height)) { return; }
    if (stats.out_of_area_scissors == 0)
    {
        stats.first_out_of_area = rect;
        stats.first_out_of_area_dest_width = dest_width;
        stats.first_out_of_area_dest_height = dest_height;
    }
    ++stats.out_of_area_scissors;
}

} // namespace mosaic::render::vk
