#pragma once

#include "engine/ui/ui_layout.hpp"
#include "engine/ui/ui_layout_stats.hpp"
#include "engine/ui/ui_panel.hpp"
#include "engine/ui/ui_parent.hpp"
#include "engine/ui/ui_rect.hpp"
#include "engine/ui/ui_scroll.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"

namespace mosaic::engine::ui
{
// The frame a child positions within: its parent UiPanel's resolved rect (recursive),
// or the whole screen {0,0,1,1} if no UiParent / invalid parent / parent has no UiPanel.
inline Rect resolve_parent_frame(mosaic::engine::World& world,
                                 mosaic::foundation::EntityID e, int depth = 0)
{
    constexpr int kMaxDepth = 16;
    const Rect kScreen{0.0f, 0.0f, 1.0f, 1.0f};
    // Counted INCLUDING the recursive entries, because the cost model the backlog states is
    // O(E x d x P) and `d` is exactly the recursion depth: a per-call counter is the only one
    // that can tell those two factors apart. See ui_layout_stats.hpp.
    ++layout_stats().resolves;
    if (depth >= kMaxDepth) { return kScreen; }
    UiParent* up = world.get_component<UiParent>(e);
    if (up == nullptr || !world.is_valid(up->parent)) { return kScreen; }
    UiPanel* pp = world.get_component<UiPanel>(up->parent);
    if (pp == nullptr) { return kScreen; }
    const Rect parent_frame = resolve_parent_frame(world, up->parent, depth + 1);
    const Rect parent_rect =
        resolve_in(parent_frame, to_anchor(pp->anchor), pp->pos, pp->size);
    // LAYOUT NARROWS THE FRAME. If the parent carries a UiStack, this child's frame is its
    // slot rather than the parent's whole rect -- and everything downstream runs unchanged,
    // which is the point. resolve_in already treats `pos` as an offset within the frame and
    // `size` as a fraction of it, so a child under a stack keeps its anchor, offsets from
    // its slot, and sizes relative to it with no new rule anywhere.
    //
    // THE HOOK IS HERE AND NOT IN resolve_ui_rect, deliberately. Two call paths reach the
    // hierarchy: panels, images and button hit-tests go through resolve_ui_rect, but TEXT
    // calls this function directly (the HUD text pass in render_system.hpp) and hands the
    // frame to layout_text. Hooking the wrapper would have laid out panels and images and
    // silently left every label where it was, with no existing test failing.
    Rect frame = parent_rect;
    if (const std::optional<Rect> slot = slot_for_child(world, e, parent_rect))
    {
        frame = *slot;
    }
    // A SCROLL TRANSLATES THE FRAME AND TOUCHES NOTHING ELSE -- not x, not w, not h, and not
    // the parent's own rect, which was already resolved above and is what the clip is taken
    // from. That is why a scrolled child's width and height come out BIT-identical rather
    // than merely close: resolve_in derives them from numbers this line never wrote.
    //
    // Measured against `parent_rect.h`, NOT `frame.h`, and the difference is load-bearing
    // when a container carries both a UiStack and a UiScroll: `offset` is defined in
    // container-heights, so one notch must move a child by the same distance whether the
    // container divides its rect into three slots or none. Against `frame.h` the step would
    // silently shrink with the slot count.
    //
    // SUBTRACTED, so a positive offset moves the content UP and the viewer sees further
    // down -- matching the wheel sign PointerState::wheel documents (positive = away from
    // the user = look further up the document), which apply_wheel_scroll negates once.
    //
    // Applied AFTER the slot rather than before: for a rigid translation the two are
    // equivalent (translating a rect translates every slot it divides into by the same
    // amount), and doing it here keeps the one subtraction visibly the only thing a scroll
    // does, instead of hiding it upstream of arithmetic that would then have to be re-read
    // to confirm it only translated.
    frame.y -= scroll_shift_for_children(world, up->parent, parent_rect.h);
    return frame;
}

// This element's screen rect, resolved within its parent frame.
inline Rect resolve_ui_rect(mosaic::engine::World& world, mosaic::foundation::EntityID e,
                            Anchor anchor, Vec2 pos, Vec2 size)
{
    return resolve_in(resolve_parent_frame(world, e), anchor, pos, size);
}
} // namespace mosaic::engine::ui
