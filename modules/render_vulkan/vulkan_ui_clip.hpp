#pragma once

#include <cstdint>

#include "engine/ui/ui_rect.hpp"

namespace mosaic::render::vk
{

// A scissor rect in destination pixels: the offset is signed because VkRect2D's is, and
// the extent unsigned for the same reason. Spelled in plain integers rather than
// VkRect2D so this arithmetic needs neither vulkan.h nor a VkDevice to test -- the same
// reason vulkan_bloom_levels.hpp and vulkan_swapchain_support.hpp exist.
struct ClipPixelRect
{
    std::int32_t  x = 0;
    std::int32_t  y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// Convert a normalized clip rect (top-left origin, x/y/w/h nominally in [0,1], the same
// space as UiQuad::rect) to destination pixels, CLAMPED to the destination's render area.
//
// The clamp is load-bearing, not defensive. vkCmdSetScissor requires a non-negative
// offset and an offset+extent that stays inside the render area; a scissor reaching
// outside it is a validation error, and that is precisely the failure class the unit
// suite cannot see (it never creates a VkDevice) -- it shows up only in the renderer
// gate. Meanwhile the clip arriving here is built from AUTHORED float rects, which can
// legitimately be negative or exceed 1 before any intersection has bounded them. So the
// invalid input is expected, and the guarantee is that no input produces an invalid
// scissor.
//
// Degenerate rects collapse to a zero extent rather than wrapping: a zero-extent scissor
// is legal and draws nothing, which is exactly what a fully-clipped-away subtree should
// render. Returning a zero extent for an empty clip is relied on by that case, so it is
// not merely a safe fallback either.
//
// Edges are rounded to the nearest pixel (not floored/ceiled outward) so the clip
// boundary lands where the quad's own rasterized edge lands -- both resolve against
// pixel centres. A caller wanting an exact boundary should place it on a whole pixel.
ClipPixelRect clip_to_pixel_rect(const mosaic::engine::ui::Rect& clip,
                                 std::uint32_t dest_width,
                                 std::uint32_t dest_height);

} // namespace mosaic::render::vk
