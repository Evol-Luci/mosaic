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

// Is this rect empty -- i.e. does it enclose no pixels at all?
//
// `<= 0` on EITHER axis, not `== 0`: intersect_rects below can produce a zero extent, and an
// authored UiPanel::size is an unclamped reflected float that a single inspector drag can
// take negative. Both mean "nothing survives", and the device agrees -- see
// RenderDevice::UiClip, whose contract says an empty clip draws nothing.
inline bool is_empty(const Rect& r)
{
    return r.w <= 0.0f || r.h <= 0.0f;
}

// The overlap of two rects: the pixels inside BOTH.
//
// THIS IS THE WHOLE OF CLIPPING'S ARITHMETIC, and it lives here, beside Rect and
// resolve_in, rather than beside the component that uses it -- the same split ui_stack.hpp
// and ui_layout.hpp already draw between pure slot arithmetic and the world-aware half.
// ui_clip_rect.hpp is the world-aware half.
//
// NOT resolve_in, AND THE DIFFERENCE IS THE POINT. resolve_in maps a child INTO a frame,
// which moves and rescales it; this returns a sub-rect of both inputs and moves nothing. A
// clip that relocated or resized what it clips would be the frame-narrowing bug U9a's device
// comment warns about.
//
// EMPTY EXTENTS ARE CLAMPED TO ZERO rather than left negative, which is what makes repeated
// intersection safe in any order: a disjoint pair would otherwise return a rect whose high
// edge is BELOW its low edge, and feeding that back in as `a` would hand max()/min() an
// inverted interval and could yield a non-empty result from an empty one. With the clamp,
// intersecting anything with an empty rect is empty, so a chain of ancestors can be folded
// left-to-right and an empty link stays empty to the end.
inline Rect intersect_rects(const Rect& a, const Rect& b)
{
    const float x0 = (a.x > b.x) ? a.x : b.x;
    const float y0 = (a.y > b.y) ? a.y : b.y;
    const float ax1 = a.x + a.w, bx1 = b.x + b.w;
    const float ay1 = a.y + a.h, by1 = b.y + b.h;
    const float x1 = (ax1 < bx1) ? ax1 : bx1;
    const float y1 = (ay1 < by1) ? ay1 : by1;
    const float w = x1 - x0;
    const float h = y1 - y0;
    return Rect{x0, y0, w > 0.0f ? w : 0.0f, h > 0.0f ? h : 0.0f};
}

// Does `outer` fully enclose `inner`?
inline bool contains_rect(const Rect& outer, const Rect& inner)
{
    return inner.x >= outer.x && inner.y >= outer.y &&
           (inner.x + inner.w) <= (outer.x + outer.w) &&
           (inner.y + inner.h) <= (outer.y + outer.h);
}

// What of `rect` survives `clip`: the whole of it when it is already inside, and the
// overlap otherwise.
//
// THE CONTAINMENT FAST PATH IS A CORRECTNESS PROPERTY, NOT AN OPTIMISATION, and it was
// added because a test caught its absence. intersect_rects recomputes an extent as
// `min(a.x+a.w, b.x+b.w) - max(a.x, b.x)`, and for a rect already inside the clip that is
// `(x + w) - x`, which in floating point is NOT always `w`: a contained button's rect came
// back differing from its unclipped rect in the last bits. One ULP hits nothing a user can
// see -- but "a clip changes NEITHER the position nor the size of what it clips" is the
// sentence that distinguishes a clip from resolve_parent_frame's frame narrowing
// (RenderDevice::UiClip says it in those words), and it has to be true exactly or it is
// a sentence that needs a footnote. Returning `rect` untouched makes it exact, and keeps
// the draw path and the hit path agreeing BY CONSTRUCTION for every element a clip does
// not actually trim.
inline Rect clip_rect(const Rect& rect, const Rect& clip)
{
    if (contains_rect(clip, rect)) { return rect; }
    return intersect_rects(rect, clip);
}

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
