#pragma once

#include <algorithm>
#include <cstdint>

#include "engine/ui/ui_rect.hpp"
#include "foundation/math/vec2.hpp"

namespace mosaic::engine::ui
{
using mosaic::foundation::math::Vec2;

// How a UiImage reconciles its texture's aspect ratio with its authored rect.
//
// Stretch is the default and is what shipped before this existed, so a
// default-constructed UiImage renders exactly as it always did.
enum class ImageFitMode
{
    Stretch = 0, // fill the rect, distorting if the aspects differ
    Fit     = 1, // whole image visible, rect shrunk and centred (letterbox)
    Fill    = 2, // rect fully covered, texture cropped (and cropped WITHIN the sub-rect)
};

// The rect to draw and the UVs to sample. The four UV floats are spelled exactly as
// UiGlyphQuad and TextQuad spell them -- foundation/math has no Vec4, and this slice is
// not the place to add a type the reflection layer has never seen.
struct ResolvedImageQuad
{
    Rect  rect;
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
};

// Does an authored sub-rect start past the far edge of its texture?
//
// The mistake this spots is a mistyped `sub_rect_pos`. Past the far edge BOTH halves of
// resolve_image_quad's clamp collapse onto that edge -- x0 clamps to tw, then
// x1 = clamp(x0 + size, x0, tw) is tw as well -- so the sprite is drawn from a degenerate
// one-texel line. That behaviour is defensible; its silence is not, which is why this is a
// predicate for a diagnostic rather than a behaviour change. Choosing between "treat as the
// whole texture" and "skip the draw" is a design call nobody has needed to make yet.
//
// A NEGATIVE position is deliberately NOT a finding. The lower clamp pulls it back on, so it
// samples a shifted-but-valid region instead of collapsing -- a different mistake, and one
// with visible output to notice.
//
// The gates mirror resolve_image_quad's own, so this cannot report a case that function
// treats as the whole-texture default: a zero-size sub-rect (what every existing scene
// carries) and a missing texture are both silent.
//
// Pure, and takes dimensions rather than an asset, so it stays testable with no registry and
// no device. The caller that holds the texture does the lookup -- see app/run/editor.cpp.
inline bool sub_rect_off_texture(std::uint32_t texture_w, std::uint32_t texture_h,
                                 Vec2 sub_rect_pos_px, Vec2 sub_rect_size_px)
{
    if (texture_w == 0 || texture_h == 0)
    {
        return false;
    }
    if (!(sub_rect_size_px.x > 0.0f) || !(sub_rect_size_px.y > 0.0f))
    {
        return false;
    }
    return sub_rect_pos_px.x >= static_cast<float>(texture_w)
           || sub_rect_pos_px.y >= static_cast<float>(texture_h);
}

// Resolve a UiImage's authored rect and pixel sub-rect into a quad to draw.
//
// `sub_rect_size_px` of zero on either axis means the whole texture, which is what a
// default-constructed UiImage carries.
//
// BOTH aspects matter. `authored_rect` is normalized screen space, which is not square: a
// rect of size {0.2, 0.2} is twice as wide as tall in PIXELS on a 2:1 display. So
// preserving the image's aspect means reconciling the texture's w/h against the rect's
// (w * screen_aspect) / h. Using only the texture aspect is the obvious mistake and is
// wrong on every non-square window, which is all of them.
//
// Total by construction: every divisor is guarded, so no input produces NaN. The caller's
// invariant (a non-zero texture handle implies non-zero dimensions) makes the degenerate
// paths unreachable in practice, but a NaN here would poison every sampler read and every
// emitted rect silently, so it is cheaper to be safe than to rely on the invariant.
inline ResolvedImageQuad resolve_image_quad(std::uint32_t texture_w, std::uint32_t texture_h,
                                            const Rect& authored_rect, float screen_aspect,
                                            Vec2 sub_rect_pos_px, Vec2 sub_rect_size_px,
                                            int fit_mode)
{
    ResolvedImageQuad out;
    out.rect = authored_rect;

    const float tw = static_cast<float>(texture_w);
    const float th = static_cast<float>(texture_h);

    // --- the sub-rect, in UV space -------------------------------------------------
    // Clamped on BOTH halves. Clamping only the size still lets a position past the edge
    // sample outside the texture, and in an atlas that is a neighbour's art -- which reads
    // as corrupt rather than as a bug.
    if (tw > 0.0f && th > 0.0f && sub_rect_size_px.x > 0.0f && sub_rect_size_px.y > 0.0f)
    {
        const float x0 = std::clamp(sub_rect_pos_px.x, 0.0f, tw);
        const float y0 = std::clamp(sub_rect_pos_px.y, 0.0f, th);
        const float x1 = std::clamp(x0 + sub_rect_size_px.x, x0, tw);
        const float y1 = std::clamp(y0 + sub_rect_size_px.y, y0, th);
        out.u0 = x0 / tw;
        out.v0 = y0 / th;
        out.u1 = x1 / tw;
        out.v1 = y1 / th;
    }
    // else: leave the full 0..1 range -- the whole-texture default.

    const ImageFitMode mode = (fit_mode == static_cast<int>(ImageFitMode::Fit))
                                  ? ImageFitMode::Fit
                                  : (fit_mode == static_cast<int>(ImageFitMode::Fill))
                                        ? ImageFitMode::Fill
                                        : ImageFitMode::Stretch; // also catches out of range
    if (mode == ImageFitMode::Stretch)
    {
        return out;
    }

    // --- the aspects ---------------------------------------------------------------
    // The IMAGE's aspect is the sub-rect's in pixels, not the whole texture's: with a
    // sub-rect in play, "the image" is that region.
    const float src_w_px = (out.u1 - out.u0) * tw;
    const float src_h_px = (out.v1 - out.v0) * th;
    if (!(src_w_px > 0.0f) || !(src_h_px > 0.0f) || !(screen_aspect > 0.0f)
        || !(authored_rect.w > 0.0f) || !(authored_rect.h > 0.0f))
    {
        return out; // degenerate: nothing meaningful to preserve, and no divide by zero
    }
    const float image_aspect = src_w_px / src_h_px;
    // The rect's aspect in PIXELS, which is where screen_aspect enters.
    const float rect_aspect = (authored_rect.w * screen_aspect) / authored_rect.h;

    if (mode == ImageFitMode::Fit)
    {
        // Shrink one axis of the GEOMETRY so the drawn rect matches the image's aspect,
        // then centre it inside the authored rect. UVs untouched.
        if (rect_aspect > image_aspect)
        {
            const float w = authored_rect.w * (image_aspect / rect_aspect);
            out.rect.x = authored_rect.x + (authored_rect.w - w) * 0.5f;
            out.rect.w = w;
        }
        else
        {
            const float h = authored_rect.h * (rect_aspect / image_aspect);
            out.rect.y = authored_rect.y + (authored_rect.h - h) * 0.5f;
            out.rect.h = h;
        }
        return out;
    }

    // Fill: GEOMETRY untouched, UVs cropped -- and cropped WITHIN the sub-rect, which is
    // what keeps an atlas sprite from bleeding into its neighbour.
    if (rect_aspect > image_aspect)
    {
        // Rect is relatively wider: use the full width, crop height, centred.
        const float keep = image_aspect / rect_aspect; // fraction of the sub-rect's height
        const float mid = (out.v0 + out.v1) * 0.5f;
        const float half = (out.v1 - out.v0) * keep * 0.5f;
        out.v0 = mid - half;
        out.v1 = mid + half;
    }
    else
    {
        const float keep = rect_aspect / image_aspect; // fraction of the sub-rect's width
        const float mid = (out.u0 + out.u1) * 0.5f;
        const float half = (out.u1 - out.u0) * keep * 0.5f;
        out.u0 = mid - half;
        out.u1 = mid + half;
    }
    return out;
}
} // namespace mosaic::engine::ui
