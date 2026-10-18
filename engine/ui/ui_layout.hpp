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

// The product of every ANCESTOR panel's alpha, so that fading a container fades everything
// inside it. The element's own alpha is NOT included -- the render pass multiplies that in
// separately, which keeps the authored early-out (`alpha <= 0` means hidden) reading the
// authored value.
//
// A RENDER-TIME OVERRIDE THAT NEVER WRITES BACK, the discipline resolve_appearance states
// in render_system.hpp: "nothing writes to p.color, so the authored value survives and a
// scene saved while hovering carries no hover tint." A scene saved mid-fade must carry the
// authored alphas, not the product.
//
// An ancestor with no UiPanel contributes nothing rather than zero: UiParent may name any
// entity, resolve_parent_frame already treats that case as "no frame", and treating it as
// alpha 0 would hide children for a reason nobody authored. Same kMaxDepth as
// resolve_parent_frame, for the same reason -- a cycle in UiParent must terminate.
inline float inherited_alpha(mosaic::engine::World& world, mosaic::foundation::EntityID e)
{
    constexpr int                kMaxDepth = 16;
    float                        product   = 1.0f;
    mosaic::foundation::EntityID cur       = e;
    for (int depth = 0; depth < kMaxDepth; ++depth)
    {
        const UiParent* up = world.get_component<UiParent>(cur);
        if (up == nullptr || !world.is_valid(up->parent)) { break; }
        if (const UiPanel* pp = world.get_component<UiPanel>(up->parent))
        {
            product *= pp->alpha;
        }
        cur = up->parent;
    }
    // FLOORED, matching the discipline ui_stack.hpp states one file over. UiPanel::alpha is
    // an unbounded reflected float on an inspector DragFloat with no clamp anywhere in the
    // UI or the device path, so a negative container alpha is one drag away. The container
    // itself is hidden by the authored `alpha <= 0` early-out -- but without this floor it
    // would multiply each child to a NEGATIVE alpha, which reaches the blend equation as
    // 2*dst - src: an over-bright halo rather than a hidden child. Brighter than not fading
    // at all, from a value that asked for less.
    //
    // Only the negative side is floored. Alpha above 1 is the project's existing posture --
    // UiHoverStyle::alpha_scale 1.4 on an opaque panel already produces 1.4 today -- and
    // capping it here would invent a maximum nobody asked for.
    return product > 0.0f ? product : 0.0f;
}

} // namespace mosaic::engine::ui
