#include "modules/render_vulkan/vulkan_ui_clip.hpp"

#include <algorithm>
#include <cmath>

namespace mosaic::render::vk
{

namespace
{

// Round one normalized edge to a pixel index clamped to [0, limit].
//
// std::lround on a non-finite float is undefined, and a NaN compares false against every
// bound, so NaN is folded to 0 explicitly before any comparison rather than being left to
// the clamp. An authored rect reaching here through a chain of float arithmetic is the
// plausible source, and a NaN that slipped through would become a garbage int32 offset --
// a validation error rather than a wrong-looking pixel.
std::int64_t edge_to_pixel(float normalized, std::uint32_t limit)
{
    if (!std::isfinite(normalized)) { return 0; }
    const double scaled = static_cast<double>(normalized) * static_cast<double>(limit);
    const double clamped = std::clamp(scaled, 0.0, static_cast<double>(limit));
    return static_cast<std::int64_t>(std::llround(clamped));
}

} // namespace

ClipPixelRect clip_to_pixel_rect(const mosaic::engine::ui::Rect& clip,
                                 std::uint32_t dest_width,
                                 std::uint32_t dest_height)
{
    // Both edges are clamped INDEPENDENTLY to the render area before the extent is taken
    // as their difference. Computing the extent first and clamping it afterwards is the
    // tempting shortcut and it is wrong: a clip starting at -0.5 with w = 1.0 would keep
    // its full width while its offset snapped to 0, sliding the surviving pixels right
    // instead of trimming the part that fell off the left edge.
    const std::int64_t x0 = edge_to_pixel(clip.x, dest_width);
    const std::int64_t y0 = edge_to_pixel(clip.y, dest_height);
    const std::int64_t x1 = edge_to_pixel(clip.x + clip.w, dest_width);
    const std::int64_t y1 = edge_to_pixel(clip.y + clip.h, dest_height);

    ClipPixelRect out{};
    out.x = static_cast<std::int32_t>(x0);
    out.y = static_cast<std::int32_t>(y0);
    // max(0, ...) rather than an abs or a swap: a negative w means an empty rect, not a
    // rect running the other way. Nothing should be drawn for one.
    out.width = static_cast<std::uint32_t>(std::max<std::int64_t>(0, x1 - x0));
    out.height = static_cast<std::uint32_t>(std::max<std::int64_t>(0, y1 - y0));
    return out;
}

} // namespace mosaic::render::vk
