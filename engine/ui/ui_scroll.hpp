#pragma once

#include <algorithm>

// Deliberately minimal: this header is included BY ui_hierarchy.hpp, so anything it pulls in
// that reaches back into the hierarchy is an include cycle. It needs the world to look the
// component up and nothing else -- no Rect, no UiPanel, no UiClipRect.
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"

namespace mosaic::engine::ui
{

// A scrollable container, authored on the PARENT entity beside a UiClipRect.
//
// A SCROLL IS A RIGID VERTICAL TRANSLATION OF THE SUBTREE AND NOTHING ELSE. It shifts the
// frame its children position within; it does not move, resize or reshape the container, and
// it does not touch the clip. That is the slice's conservation law and
// `scroll_shift_for_children` below is the only thing that implements it -- the shift is
// subtracted from the frame's `y` and from nothing else, so `resolve_in` derives every
// child's x, w and h from numbers a scroll never touched.
//
// IT NEEDS A UiClipRect ON THE SAME ENTITY TO BE USEFUL, and deliberately does not imply
// one. Scrolling without clipping draws the off-region content over its neighbours, which
// looks like a bug and is: the two are separate components because `UiClipRect` is also
// useful alone (a static mask), and because implying one component from another is the kind
// of hidden coupling `UiStack`'s "a stack needs a UiPanel" comment already warns about.
// Authoring a scroll without a clip is a scene bug, not an engine one.
//
// BOTH FIELDS ARE MULTIPLES OF THE CONTAINER'S OWN HEIGHT, which is the single
// interpretation that survives a resize and a nested container with no second rule anywhere:
//   * `content = 1.8` means "my content is 1.8 container-heights tall".
//   * `offset  = 0.4` means "scrolled down by 0.4 of a container-height".
// Normalized screen units were the alternative and are rejected: the same authored number
// would mean different distances depending on where the container sits in the hierarchy,
// which is the mistake `UiStack::spacing` already avoids by being a frame-fraction.
//
// `content` IS AUTHORED, NOT MEASURED, and that is a deferral with a reason rather than an
// omission. Measuring it inside the layout path is a dependency cycle, not merely work: a
// child's rect needs its frame, the frame needs the scroll bound, the bound needs the
// content extent, and the extent needs the children's rects. Breaking it means a second
// parent-chain walk resolving every sibling at offset 0 plus font metrics for any text
// child, neither of which the layout path has where the frame is computed. `UiGrid` took the
// same posture for its cell counts. Backlogged.
struct UiScroll
{
    float offset  = 0.0f; // container-heights scrolled down; clamped to [0, content - 1]
    float content = 1.0f; // content extent in container-heights; <= 1 means nothing scrolls
};

template <typename V>
void reflect(V& visitor, UiScroll& s)
{
    visitor.member("offset", s.offset);
    visitor.member("content", s.content);
}

// How far this region can be scrolled, in container-heights.
//
// `content <= 1` yields 0: content that fits does not scroll, and the region is inert rather
// than being a special case anywhere else. A non-finite or negative `content` also yields 0,
// via the same comparison -- `!(x > 1)` is false for NaN, which is the floored-input
// discipline ui_stack.hpp and clamp01 already use, rather than trusting a reflected float an
// inspector drag or a hand-edited scene can set to anything.
inline float scroll_max_offset(const UiScroll& s)
{
    if (!(s.content > 1.0f)) { return 0.0f; }
    return s.content - 1.0f;
}

// The region's effective offset: the authored value held inside [0, max_offset].
//
// CLAMPED ON READ, not only where the wheel writes it. `offset` is a reflected float, so a
// scene file or an unbounded inspector drag can put any value in it without the wheel ever
// running -- and an out-of-range offset must not translate the subtree somewhere no scroll
// could reach. The wheel path clamps on write too, so the inspector shows the truth rather
// than a value the renderer silently ignores.
inline float scroll_offset_of(const UiScroll& s)
{
    const float max_offset = scroll_max_offset(s);
    if (!(s.offset > 0.0f)) { return 0.0f; } // also catches NaN
    return std::min(s.offset, max_offset);
}

// The normalized-Y distance this parent's children shift, given the parent's own resolved
// height. Positive means the content moves UP, i.e. the viewer is looking further down.
//
// Returns 0 for an entity with no UiScroll, which is what keeps every scene that does not
// use the feature on exactly its pre-U9d arithmetic: `resolve_parent_frame` subtracts this
// unconditionally, and subtracting a literal 0.0f from a float is the identity, so an
// unscrolled frame is bit-identical rather than merely close.
inline float scroll_shift_for_children(mosaic::engine::World& world,
                                       mosaic::foundation::EntityID parent,
                                       float container_h)
{
    const UiScroll* s = world.get_component<UiScroll>(parent);
    if (s == nullptr) { return 0.0f; }
    return scroll_offset_of(*s) * container_h;
}

} // namespace mosaic::engine::ui
