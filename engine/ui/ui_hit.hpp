#pragma once

#include <vector>

#include "engine/ui/pointer.hpp"
#include "engine/ui/ui_rect.hpp"
#include "foundation/core/entity_id.hpp"

namespace mosaic::engine::ui
{
// One hit-testable region, flattened out of the world so the tie-break needs no World,
// no component storage and no pointer provider to test.
struct HitCandidate
{
    mosaic::foundation::EntityID e;
    int                          order = 0;
    Rect                         rect;
};

// Which candidate is under the pointer, resolving overlaps by z-order. Nil EntityID when
// nothing is hit.
//
// TWO rules, and the second is the one that was broken:
//
//   1. Higher `order` wins. `order` is a signed int because a scene file can author -1 to
//      push a button behind its neighbours.
//   2. Among EQUAL orders, the candidate later in `candidates` wins. This matches the
//      panel pass, which stable-sorts ascending and draws in that order, so the entity
//      drawn last is on top -- but only in the COMMON CASE, not by construction. The two
//      iterate different sparse sets (`UiButton`'s and `UiPanel`'s) with independent
//      insertion histories, and removal is swap-and-pop, so destroying any unrelated
//      button can reorder an equal-order run. Treat this as a DETERMINISTIC tie-break that
//      usually agrees with the draw order; author distinct `order` values when it matters.
//
// Before this function, UiSystem took the FIRST button under the pointer while the
// renderer drew the LAST one on top -- opposite ends of the same list -- so a click on two
// overlapping equal-order buttons went to the one the user could not see.
//
// Containment is filtered BEFORE ordering, not after. Sorting first and then testing the
// single winner would return nothing whenever a high-order button sits elsewhere on screen
// while a lower-order one is genuinely under the pointer.
//
// Edges are inclusive on both sides, matching the comparison this replaces. A consequence
// worth stating rather than discovering: a zero-size rect is hit at exactly its corner.
inline mosaic::foundation::EntityID topmost_hit(const std::vector<HitCandidate>& candidates,
                                                const PointerState& pointer)
{
    // No pointer means nothing is hit, whatever the coordinates say. Checked here rather
    // than in each caller so press and hover cannot disagree about it, and because a
    // provider's "no pointer" state still carries coordinates -- usually (0,0) or a value
    // clamped to the surface edge, both of which land inside real buttons.
    if (!pointer.inside)
    {
        return mosaic::foundation::EntityID{};
    }
    const HitCandidate* best = nullptr;
    for (const HitCandidate& c : candidates)
    {
        const bool inside = pointer.x >= c.rect.x && pointer.x <= c.rect.x + c.rect.w
                            && pointer.y >= c.rect.y && pointer.y <= c.rect.y + c.rect.h;
        if (!inside)
        {
            continue;
        }
        // `>=` rather than `>` is rule 2: a later candidate of EQUAL order replaces the
        // earlier one, which is what "later in iteration is on top" means. With `>` the
        // first of an equal-order run would win, which is precisely today's bug.
        if (best == nullptr || c.order >= best->order)
        {
            best = &c;
        }
    }
    return best != nullptr ? best->e : mosaic::foundation::EntityID{};
}
} // namespace mosaic::engine::ui
