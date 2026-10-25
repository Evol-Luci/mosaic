#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "engine/ui/ui_image.hpp"
#include "engine/ui/ui_layout_stats.hpp"
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
//
// THIS SCAN IS O(P) AND IT IS CALLED PER LAID-OUT ELEMENT, WHICH IS DELIBERATE AND MEASURED.
// U8d (2026-10-25) was the slice that was to replace it with a per-frame parent -> children
// index, and it measured instead of building one. The numbers, from
// tests/unit/ui/ui_layout_cost_test.cpp, which asserts them:
//
//   the shipped demo scene, one frame, all four resolve sites:  306 visits, 17 scans
//   a 256-element inventory grid, one pass:                   65,536 visits, 256 scans
//
// and that 65,536-visit pass costs ~0.7 ms in a RELEASE build (~17 ms in debug -- a 25x gap,
// so do not rule on a debug timing). An index would buy that back and would spend the thing
// this function's callers rely on: slot_for_child is pure and lazy, so the draw path and the
// hit path agree BY CONSTRUCTION rather than by sharing a cache whose invalidation someone
// has to get right. U8d also ran the naive memo as a mutation and found that it leaves a
// draw/hit AGREEMENT assertion green -- both paths read the same stale cache and agree on a
// wrong answer -- which is why the cost test checks every rect against an independently
// computed slot as well.
//
// THE TRIGGER TO BUILD THE INDEX, in the counters' own units rather than as a feeling: a real
// authored scene measuring more than ~200,000 `visits` or ~500 `scans` per frame
// (engine/ui/ui_layout_stats.hpp). Today's scene is ~650x under it. Add a case to
// ui_layout_cost_test.cpp to measure a candidate scene.
inline std::vector<mosaic::foundation::EntityID> stack_children(
    mosaic::engine::World& world, mosaic::foundation::EntityID parent)
{
    LayoutStats& stats = layout_stats();
    ++stats.scans;
    std::vector<std::pair<std::pair<int, std::uint32_t>, mosaic::foundation::EntityID>> keyed;
    // The allocation count is EXACT rather than modelled: `keyed` grows by push_back, so it
    // reallocates every time it outgrows its capacity, and a capacity change is the only
    // observable that says one happened. `out` below is reserved once, so it contributes
    // exactly one. See ui_layout_stats.hpp for why counts and not milliseconds.
    std::size_t capacity = keyed.capacity();
    world.for_each<UiParent>(
        [&](mosaic::foundation::EntityID e, UiParent& up)
        {
            ++stats.visits;
            // The WHOLE handle, not index(): EntityID is (generation << 32) | index and the
            // registry recycles indices, so comparing indices would let a recycled entity
            // inherit a destroyed one's children.
            if (up.parent != parent) { return; }
            keyed.push_back({{child_layout_order(world, e), e.index()}, e});
            if (keyed.capacity() != capacity)
            {
                capacity = keyed.capacity();
                ++stats.allocations;
            }
        });
    ++stats.sorts;
    std::sort(keyed.begin(), keyed.end(),
              [](const auto& l, const auto& r) { return l.first < r.first; });

    std::vector<mosaic::foundation::EntityID> out;
    // `out`'s allocations are OBSERVED, not assumed to be one. The reserve below makes it
    // exactly one today -- but a future edit that drops the reserve would turn it into
    // another geometric growth, and a counter hard-coded to 1 would report the regression as
    // no change at all. Same capacity-watch as `keyed` above, for the same reason.
    std::size_t out_capacity = out.capacity();
    out.reserve(keyed.size());
    if (out.capacity() != out_capacity)
    {
        out_capacity = out.capacity();
        ++stats.allocations;
    }
    for (const auto& k : keyed)
    {
        out.push_back(k.second);
        if (out.capacity() != out_capacity)
        {
            out_capacity = out.capacity();
            ++stats.allocations;
        }
    }
    return out;
}

// This child's slot within `parent_rect`, or nothing if its parent carries no layout
// container at all.
//
// THE ONE PLACE THE CONTAINER TYPE IS DISPATCHED. Nothing else in the engine switches on
// UiStack/UiGrid/UiFlow -- the draw path, the hit path and the HUD text path all reach
// layout only through resolve_parent_frame, which calls this. Adding a fourth container
// means one more lookup here and one more pure function in ui_stack.hpp, and nothing else.
//
// FIXED PRECEDENCE -- UiStack, then UiGrid, then UiFlow -- rather than whichever component
// happens to be found first. Authoring two containers on one entity is a mistake, but it has
// to be a DETERMINISTIC mistake: the same scene must lay out the same way whatever order its
// components were added in. Same discipline as child_layout_order above, and for the same
// reason.
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
    // THREE PROBES BEFORE THE SHORT-CIRCUIT, where U8a had one. That is the cost every
    // parented element in every scene pays before this function can say "no container here",
    // and counting it is what turned the backlog's "non-stack scenes pay nothing" from a
    // claim into a measured number. They are O(1) sparse-set lookups and they do not scan,
    // so the answer is "nearly nothing, and now three times nearly nothing" -- but the
    // counter is what makes a fourth container's cost visible instead of asserted.
    layout_stats().container_probes += 3;
    const UiStack* st = world.get_component<UiStack>(up->parent);
    const UiGrid*  gr = world.get_component<UiGrid>(up->parent);
    const UiFlow*  fl = world.get_component<UiFlow>(up->parent);
    if (st == nullptr && gr == nullptr && fl == nullptr) { return std::nullopt; }

    // The enumeration and the ordering are SHARED by all three containers, and that is the
    // point -- a grid and a flow are a different slot function over the same children, not a
    // different hierarchy.
    const std::vector<mosaic::foundation::EntityID> kids = stack_children(world, up->parent);
    for (std::size_t i = 0; i < kids.size(); ++i)
    {
        if (kids[i] == child)
        {
            if (st != nullptr)
            {
                return layout_slot(parent_rect, st->axis, st->spacing, st->padding, i,
                                   kids.size());
            }
            if (gr != nullptr)
            {
                // No child count passed, deliberately: a grid's cells are authored.
                return grid_slot(parent_rect, gr->cols, gr->rows, gr->spacing, gr->padding,
                                 i);
            }
            return flow_slot(parent_rect, fl->axis, fl->per_line, fl->spacing, fl->padding,
                             i, kids.size());
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
