#pragma once

#include "foundation/math/vec2.hpp"

namespace mosaic::engine::ui
{
using mosaic::foundation::math::Vec2;

// Which screen reference point an element is anchored to. Origin is top-left,
// x right, y down — matching normalized screen space [0,1].
enum class Anchor
{
    TopLeft = 0, Top, TopRight,
    Left,        Center, Right,
    BottomLeft,  Bottom, BottomRight
};

// Clamp an int to an Anchor, falling back to TopLeft out of range.
//
// Lives HERE, beside the enum, because three callers need it and a scene file can hold any
// int: `anchor` is a reflected int on UiPanel, UiImage, UiText and UiButton.
//
// It existed three times until 2026-10-12 -- to_anchor in render_system.hpp,
// hierarchy_anchor in ui_hierarchy.hpp and button_anchor in ui_button_rects.hpp -- each with
// its own `a <= 8` bound. That was a correctness hazard rather than mere duplication: adding
// a tenth Anchor value meant updating three independent bounds, and missing one would make
// hit-testing and rendering disagree about where a button is. The upper bound is written
// against BottomRight rather than a literal 8, so extending the enum needs no edit here at
// all.
inline Anchor to_anchor(int a)
{
    return (a >= 0 && a <= static_cast<int>(Anchor::BottomRight)) ? static_cast<Anchor>(a)
                                                                  : Anchor::TopLeft;
}

// A resolved rectangle in normalized screen space: origin top-left, x/y/w/h in [0,1].
struct Rect
{
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
};

namespace detail
{
// One axis: reference band 0 = Left/Top (offset in from the low edge),
// 1 = Center (centered, pos nudges), 2 = Right/Bottom (offset in from the high edge).
inline float resolve_axis(int band, float pos, float size)
{
    if (band == 0) { return pos; }                     // flush to low edge, offset inward
    if (band == 2) { return 1.0f - size - pos; }       // flush to high edge, offset inward
    return 0.5f - size * 0.5f + pos;                   // centered
}
} // namespace detail

// Resolve an anchored element to a normalized screen Rect.
//   size : element size as a fraction of the screen (w = size.x, h = size.y).
//   pos  : normalized offset FROM the anchor point toward the screen interior
//          (pos {0,0} places the element flush against its anchor).
inline Rect resolve_rect(Anchor anchor, Vec2 pos, Vec2 size)
{
    const int a = static_cast<int>(anchor); // 0..8, row-major: col = horizontal band, row = vertical band
    const int col = a % 3;                  // 0 = Left, 1 = Center, 2 = Right
    const int row = a / 3;                  // 0 = Top,  1 = Center, 2 = Bottom
    Rect r;
    r.x = detail::resolve_axis(col, pos.x, size.x);
    r.y = detail::resolve_axis(row, pos.y, size.y);
    r.w = size.x;
    r.h = size.y;
    return r;
}

// Resolve an element within an arbitrary frame rect (normalized, top-left origin).
// pos/size are fractions of `frame`; anchor picks the frame's corner/edge.
// resolve_rect(a,p,s) == resolve_in(Rect{0,0,1,1}, a, p, s).
inline Rect resolve_in(const Rect& frame, Anchor anchor, Vec2 pos, Vec2 size)
{
    const Rect local = resolve_rect(anchor, pos, size);
    return Rect{frame.x + local.x * frame.w,
                frame.y + local.y * frame.h,
                local.w * frame.w,
                local.h * frame.h};
}
} // namespace mosaic::engine::ui
