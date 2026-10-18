#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "engine/ui/ui_image.hpp"
#include "engine/ui/ui_panel.hpp"
#include "engine/ui/ui_parent.hpp"
#include "engine/ui/ui_rect.hpp"
#include "engine/ui/ui_stack.hpp"
#include "engine/ui/ui_text.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"

namespace mosaic::engine::ui
{

// The `order` a layout sorts this child by.
//
// FIXED PRECEDENCE -- UiPanel, then UiImage, then UiText -- rather than whichever component
// happens to be found first, or the same scene would lay out differently depending on the
// order its components were added.
//
// UiPanel first because its `order` is the only one the renderer actually sorts by.
// ui_order_check.hpp records the rest: UiImage::order and UiText::order are reflected,
// documented, and ignored by the draw path. Sorting by the number that already means
// something keeps ONE concept rather than inventing a second one for layout.
inline int child_layout_order(mosaic::engine::World& world, mosaic::foundation::EntityID e)
{
    if (const UiPanel* p = world.get_component<UiPanel>(e)) { return p->order; }
    if (const UiImage* i = world.get_component<UiImage>(e)) { return i->order; }
    if (const UiText*  t = world.get_component<UiText>(e))  { return t->order; }
    return 0;
}

// Every child of `parent`, in layout order.
//
// ORDER COMES FROM AN AUTHORED KEY, NEVER FROM ITERATION, and this is the sharpest
// correctness point in the layout slice. ComponentRegistry::for_each visits "in dense
// order" over a sparse set, and removal swaps the last element into the hole -- so
// deriving layout order from iteration would make destroying ANY entity that merely
// happens to carry UiParent (in a different panel, or in no panel at all) silently
// reshuffle this stack. Draw-order reshuffling is a flicker; layout reshuffling moves
// elements to different places on screen.
//
// The entity index is a DETERMINISTIC TIEBREAK, not a feature: equal `order` values come
// back in a reproducible sequence rather than a storage-dependent one. Nothing should
// author equal orders and rely on the result.
//
// Hidden children still count. A child with alpha 0 is skipped by the draw pass but still
// occupies a slot, so fading one element does not reflow the rest -- and layout does not
// come to depend on a render-time property.
inline std::vector<mosaic::foundation::EntityID> stack_children(
    mosaic::engine::World& world, mosaic::foundation::EntityID parent)
{
    std::vector<std::pair<std::pair<int, std::uint32_t>, mosaic::foundation::EntityID>> keyed;
    world.for_each<UiParent>(
        [&](mosaic::foundation::EntityID e, UiParent& up)
        {
            // The WHOLE handle, not index(): EntityID is (generation << 32) | index and the
            // registry recycles indices, so comparing indices would let a recycled entity
            // inherit a destroyed one's children.
            if (up.parent != parent) { return; }
            keyed.push_back({{child_layout_order(world, e), e.index()}, e});
        });
    std::sort(keyed.begin(), keyed.end(),
              [](const auto& l, const auto& r) { return l.first < r.first; });

    std::vector<mosaic::foundation::EntityID> out;
    out.reserve(keyed.size());
    for (const auto& k : keyed) { out.push_back(k.second); }
    return out;
}

// This child's slot within `parent_rect`, or nothing if its parent carries no UiStack.
//
// TAKES THE PARENT'S RECT rather than resolving it, for two reasons. It breaks what would
// otherwise be a circular include -- ui_hierarchy.hpp includes this header, so this header
// must not include that one. And it keeps the recursion in ONE place, inside
// resolve_parent_frame's existing kMaxDepth guard, rather than opening a second unbounded
// path up the parent chain.
inline std::optional<Rect> slot_for_child(mosaic::engine::World& world,
                                          mosaic::foundation::EntityID child,
                                          const Rect& parent_rect)
{
    const UiParent* up = world.get_component<UiParent>(child);
    if (up == nullptr || !world.is_valid(up->parent)) { return std::nullopt; }
    const UiStack* st = world.get_component<UiStack>(up->parent);
    if (st == nullptr) { return std::nullopt; }

    const std::vector<mosaic::foundation::EntityID> kids = stack_children(world, up->parent);
    for (std::size_t i = 0; i < kids.size(); ++i)
    {
        if (kids[i] == child)
        {
            return layout_slot(parent_rect, st->axis, st->spacing, st->padding, i,
                               kids.size());
        }
    }
    // Unreachable in practice -- `child` carries the UiParent that stack_children matched
    // on -- but returning nothing rather than asserting means a future caller passing an
    // unrelated entity gets today's behaviour instead of a crash.
    return std::nullopt;
}

} // namespace mosaic::engine::ui
