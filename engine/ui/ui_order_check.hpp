#pragma once

#include <vector>

#include "engine/ui/ui_button.hpp"
#include "engine/ui/ui_panel.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"

namespace mosaic::engine::ui
{
// A UiButton whose hit-test `order` differs from the draw `order` of the UiPanel on its own
// entity.
//
// Both numbers are carried rather than just a flag, so a diagnostic can name the two values
// an author has to reconcile.
struct OrderDisagreement
{
    mosaic::foundation::EntityID e;
    int                          button_order = 0;
    int                          panel_order  = 0;
};

// Every button/panel `order` disagreement in the world, in `for_each<UiButton>` order.
//
// THIS IS A HEURISTIC LINT, NOT A CORRECTNESS CHECK, and the difference is worth stating
// plainly because the backlog item that asked for it was framed as the latter.
//
// The genuine failure needs TWO overlapping buttons: if A's `UiButton::order` ranks above
// B's while A's `UiPanel::order` ranks below B's, the click goes to A and the user sees B on
// top. That is a PAIRWISE RELATIVE property, so with a single button in a scene no `order`
// value can misroute anything and this function's finding is not a bug by itself. What it
// catches is the authoring slip the relative bug grows from -- editing a panel's order and
// forgetting its button -- which is cheap to detect and cheap to fix. The real pairwise
// check needs rect-overlap resolution and is recorded in the backlog.
//
// WHY ONLY UiPanel, when a button's entity may also carry a UiImage or a UiText: the three
// HUD passes in render_system.hpp are FIXED AND SEQUENTIAL -- panels, then images over
// panels, then text over both -- and only the panel pass sorts. `UiImage::order` and
// `UiText::order` are reflected, documented, and ignored by the renderer, so comparing a
// button's order against either is comparing numbers that participate in no ordering at all.
// An earlier version of this check did compare all three and fired on the shipped demo,
// whose camera entity carries UiPanel{0}, UiImage{0} and UiText{1} -- a finding no button
// order could have silenced, because those three do not agree with each other either.
//
// OWNERSHIP IS ENTITY IDENTITY, the rule the hover slice stated: a button owns the UI
// components on its OWN entity. So this does not walk `UiParent`, unlike active_hover_style,
// which walks up because a style INHERITS down a chain. An order does not inherit: a child's
// order is its own position in its own pass.
//
// DELIBERATE BLIND SPOT: a button overlaying a panel on a DIFFERENT entity is invisible here.
// Detecting that needs the same rect-overlap analysis as the pairwise check. Recorded as a
// non-goal in the backlog rather than left implicit.
inline std::vector<OrderDisagreement> find_order_disagreements(mosaic::engine::World& world)
{
    std::vector<OrderDisagreement> found;
    world.for_each<UiButton>(
        [&](mosaic::foundation::EntityID e, UiButton& b)
        {
            const UiPanel* p = world.get_component<UiPanel>(e);
            if (p == nullptr)
            {
                return; // a bare hit region is legitimate
            }
            if (p->order != b.order)
            {
                found.push_back(OrderDisagreement{e, b.order, p->order});
            }
        });
    return found;
}
} // namespace mosaic::engine::ui
