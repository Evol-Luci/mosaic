#pragma once

#include <cstddef>

#include "engine/ui/ui_rect.hpp"

namespace mosaic::engine::ui
{

// A one-axis auto-layout container, authored on the PARENT entity.
//
// Its children's frames become evenly divided slots of the parent's rect, which is what
// makes each child's own anchor/pos/size keep working unchanged: layout narrows the frame
// and the existing arithmetic runs inside it. A child's authored `pos` therefore offsets
// it FROM its slot rather than being ignored -- the UI decomposition reserved that
// question for the human, and narrowing the frame turns out to answer it, because
// resolve_in already treats `pos` as an offset within the frame it is given.
//
// `spacing` and `padding` are fractions of the parent's frame, like `pos` and `size`, not
// absolute screen units -- so a stack nests correctly and the arithmetic never refers to
// the screen.
//
// A STACK NEEDS A UiPanel ON THE SAME ENTITY. resolve_parent_frame returns the whole SCREEN
// when a parent has no UiPanel, so a UiStack alone lays nothing out -- there is no rect to
// divide. A layout container must therefore carry a panel even when it should show no chrome
// of its own.
//
// THE AUTHORED WAY TO WRITE A LAYOUT-ONLY CONTAINER:
//
//     UiPanel{ ..., .alpha = 1.0f, .visible = false }
//
// Both halves are load-bearing, and each one is the answer to a thing that does not work:
//
//   .visible = false   hides THIS panel's own quad and nothing else. It does not inherit
//                      down the chain and it does not reach any other component, so the
//                      children render normally. It also does NOT make resolve_parent_frame
//                      treat the panel as absent, which is what makes the layout survive --
//                      see UiPanel::visible, property 1.
//   .alpha = 1.0f      because inherited_alpha multiplies ANCESTOR alphas down, and 1 is
//                      that product's identity. Author anything less and the children fade
//                      with the container they cannot see.
//
// WHAT DOES NOT WORK, AND WHY IT IS WRITTEN DOWN: UiPanel{alpha = 0} is the obvious reach,
// and it hides the whole subtree, because a container's alpha multiplies down to its
// children. This comment previously offered two workarounds for that -- a fully transparent
// `color`, or an alpha low enough to look invisible but above zero -- and named two possible
// real fixes, the `visible` bool "or an opt-out on alpha propagation". The opt-out was
// CONSIDERED AND DECLINED by the human verification pass of 2026-10-20 ("fading a container
// should fade what is inside it"; recorded in app/run/demo_scene.hpp beside the "HP" label
// and in docs/record/2026-10.md), so do not reach for it. `visible` shipped in U8b and is the
// whole answer; the two workarounds are gone with the problem.
struct UiStack
{
    // 0 = vertical (children stack downward), 1 = horizontal. An int rather than an enum,
    // matching `anchor` on all four UI components: the reflection layer and the scene
    // format already carry it that way, and a scene file can hold any int.
    int   axis    = 0;
    float spacing = 0.0f; // gap BETWEEN slots, as a fraction of the frame's main axis
    float padding = 0.0f; // inset on all four sides, as a fraction of the frame
};

template <class V>
void reflect(V& visitor, UiStack& s)
{
    visitor.member("axis", s.axis);
    visitor.member("spacing", s.spacing);
    visitor.member("padding", s.padding);
}

// The slot for child `index` of `count`, within `frame`.
//
// PURE ARITHMETIC, no world: this is where every edge case lives, and keeping it free of
// the ECS is what makes those cases cheap to pin. The world-aware half -- who the children
// are, and in what order -- is ui_layout.hpp.
//
// Returns a zero-extent rect at the frame's origin for `count == 0` or an out-of-range
// index. NEVER returns a negative extent: resolve_in MULTIPLIES by w/h, so a negative one
// would turn every descendant rect into garbage -- inverted, off-screen, hit-testing
// against nothing -- rather than into nothing at all.
inline Rect layout_slot(const Rect& frame, int axis, float spacing, float padding,
                        std::size_t index, std::size_t count)
{
    if (count == 0 || index >= count)
    {
        return Rect{frame.x, frame.y, 0.0f, 0.0f};
    }

    // Floored rather than trusted. Both are reflected floats on unbounded inspector
    // DragFloats, and a negative padding would OUTSET the slot past its own frame -- a
    // child escaping its container, which reads as an engine bug rather than as a typo.
    const float pad      = padding > 0.0f ? padding : 0.0f;
    const float gap_frac = spacing > 0.0f ? spacing : 0.0f;

    const float ix = pad * frame.w;
    const float iy = pad * frame.h;
    float       inner_w = frame.w - 2.0f * ix;
    float       inner_h = frame.h - 2.0f * iy;
    if (inner_w < 0.0f) { inner_w = 0.0f; }
    if (inner_h < 0.0f) { inner_h = 0.0f; }
    const Rect inner{frame.x + ix, frame.y + iy, inner_w, inner_h};

    // Anything that is not 1 is vertical, so a scene file holding a stray int gets a real
    // column rather than an invisible UI -- the same posture to_anchor takes.
    const bool  horizontal = (axis == 1);
    const float extent     = horizontal ? inner.w : inner.h;

    // count - 1 gaps: spacing sits BETWEEN slots, never at the ends. Counted at the ends
    // too, the stack would float inside its frame and look like a padding bug.
    const float gap  = gap_frac * extent;
    const float gaps = gap * static_cast<float>(count - 1);
    float       slot = (extent - gaps) / static_cast<float>(count);
    if (slot < 0.0f) { slot = 0.0f; }

    const float off = (slot + gap) * static_cast<float>(index);
    if (horizontal)
    {
        return Rect{inner.x + off, inner.y, slot, inner.h};
    }
    return Rect{inner.x, inner.y + off, inner.w, slot};
}

} // namespace mosaic::engine::ui
