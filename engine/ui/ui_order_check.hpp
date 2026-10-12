#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

#include "engine/ui/ui_button.hpp"
#include "engine/ui/ui_button_rects.hpp"
#include "engine/ui/ui_panel.hpp"
#include "engine/ui/ui_rect.hpp"
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

// Which way a pair of overlapping buttons disagrees.
enum class OrderConflictKind
{
    // Both axes are ordered and they rank the pair OPPOSITELY: the click provably goes to
    // the button drawn underneath. A definite bug.
    Inverted,
    // One axis is tied while the other is not, so the outcome falls to iteration order over
    // two different sparse sets. Not provably wrong in any one frame -- but destroying an
    // unrelated button can flip which of these receives the click, with nothing in the scene
    // having changed visually (see ui_hit.hpp's rule 2).
    Ambiguous
};

// Two overlapping buttons whose hit ranking disagrees with their visual ranking.
//
// `a` is always the entity with the lower EntityID value, and the four orders follow that
// assignment rather than iteration order -- otherwise the same conflict would key
// differently depending on which entity the outer loop reached first, and warn twice.
struct OrderConflict
{
    mosaic::foundation::EntityID a, b;
    OrderConflictKind            kind           = OrderConflictKind::Inverted;
    int                          a_button_order = 0;
    int                          a_panel_order  = 0;
    int                          b_button_order = 0;
    int                          b_panel_order  = 0;
};

// Do two normalized screen rects share any point?
//
// INCLUSIVE on both axes, matching topmost_hit, which tests containment with >= and <= and
// therefore treats a pointer exactly on a shared edge as inside BOTH rects. Two buttons that
// merely touch along an edge can genuinely misroute a click on that line, so touching counts
// as overlapping. Exclusive comparisons would make this check disagree with the hit test it
// exists to police.
inline bool rects_overlap(const Rect& p, const Rect& q)
{
    return p.x <= q.x + q.w && q.x <= p.x + p.w && p.y <= q.y + q.h && q.y <= p.y + p.h;
}

// Every pair of overlapping buttons whose UiButton::order ranking disagrees with their
// UiPanel::order ranking.
//
// THIS IS THE CORRECTNESS CHECK that find_order_disagreements above is a heuristic stand-in
// for. A lone button cannot misroute a click whatever its order says; the failure needs two
// buttons that overlap.
//
// Rects come from button_hit_candidates -- the SAME flatten UiSystem's press and hover paths
// use -- so a conflict reported here is one the hit test will actually honour.
//
// ONLY AUTHORED INTEGERS ARE COMPARED. The check never asks which entity comes first in a
// for_each, because that is precisely the fragile property it warns about: a verdict built on
// iteration order would change when an unrelated entity was destroyed.
//
// A button with no UiPanel on its OWN entity is skipped whole: under the ownership rule its
// visual rank is undefined, so there is nothing to compare. That is where a button overlaying
// a panel on a DIFFERENT entity falls -- still invisible here, still recorded in the backlog.
//
// O(n^2) in buttons, once per editor frame. Every scene in this repository has one button and
// 64 would be 2,016 pairs of four integer comparisons; a spatial index would be machinery
// defending a scene size nothing approaches.
inline std::vector<OrderConflict> find_order_conflicts(mosaic::engine::World& world)
{
    struct Ranked
    {
        mosaic::foundation::EntityID e;
        int                          button_order = 0;
        int                          panel_order  = 0;
        Rect                         rect;
    };

    std::vector<Ranked> ranked;
    for (const HitCandidate& c : button_hit_candidates(world))
    {
        const UiPanel* p = world.get_component<UiPanel>(c.e);
        if (p == nullptr)
        {
            continue; // no visual of its own -- nothing to rank it against
        }
        ranked.push_back(Ranked{c.e, c.order, p->order, c.rect});
    }

    std::vector<OrderConflict> out;
    for (std::size_t i = 0; i < ranked.size(); ++i)
    {
        for (std::size_t j = i + 1; j < ranked.size(); ++j)
        {
            const Ranked* lo = &ranked[i];
            const Ranked* hi = &ranked[j];
            if (!rects_overlap(lo->rect, hi->rect))
            {
                continue;
            }
            // Normalize so the finding's identity does not depend on iteration order.
            if (lo->e.value() > hi->e.value())
            {
                std::swap(lo, hi);
            }
            const int da = lo->button_order - hi->button_order;
            const int dp = lo->panel_order - hi->panel_order;
            if (da == 0 && dp == 0)
            {
                // Neither axis expresses a preference: the pair is symmetric, and which one
                // appears on top is as arbitrary as which one is clicked. Reporting it would
                // fire on every pair of overlapping default-constructed buttons.
                continue;
            }
            OrderConflictKind kind;
            if (da == 0 || dp == 0)
            {
                kind = OrderConflictKind::Ambiguous;
            }
            else if ((da > 0) != (dp > 0))
            {
                // Both non-zero here, so comparing SIGNS is exact. Comparing magnitudes would
                // miss -2 against 2 -- equal in magnitude, opposite in z, and the worst
                // disagreement there is to miss.
                kind = OrderConflictKind::Inverted;
            }
            else
            {
                continue; // same sign: the two axes agree
            }
            out.push_back(OrderConflict{lo->e, hi->e, kind, lo->button_order, lo->panel_order,
                                        hi->button_order, hi->panel_order});
        }
    }
    return out;
}

// The per-entity findings worth reporting once the pairwise check has had its say: those
// whose entity appears in no conflict.
//
// ONE LINE PER PROBLEM. Both checks have something true to say about a button whose own two
// orders disagree AND which conflicts with a neighbour, but the pairwise message is strictly
// the more useful: it carries all four orders plus the consequence ("the click goes to the
// one drawn underneath"). Emitting the per-entity line beside it says the same thing twice.
//
// The lint keeps its whole value, which is the case the pairwise check cannot speak to at
// all: a SINGLE button whose orders drifted apart, before any second button turns it into a
// real misroute.
//
// Matching on BOTH ends of a pair is load-bearing. `a` and `b` are assigned by entity value,
// not by which one an author edited, so checking only `a` would still double-report about
// half the time -- and which half would depend on entity ids, which is the worst kind of
// intermittent.
//
// Separated from both checks on purpose: each stays a pure, independent answer to its own
// question, and the PRECEDENCE between them -- a presentation decision -- lives here, where
// a reader looking for "why didn't I get the other warning" will find it.
//
// Found by a human working a verification checklist on 2026-10-12. Both diagnostics were
// individually correct, so no test noticed; only someone reading the Console saw two lines
// for one mistake.
inline std::vector<OrderDisagreement> without_entities_in_conflict(
    const std::vector<OrderDisagreement>& disagreements,
    const std::vector<OrderConflict>&     conflicts)
{
    std::vector<OrderDisagreement> kept;
    kept.reserve(disagreements.size());
    for (const OrderDisagreement& d : disagreements)
    {
        bool spoken_for = false;
        for (const OrderConflict& c : conflicts)
        {
            if (c.a == d.e || c.b == d.e)
            {
                spoken_for = true;
                break;
            }
        }
        if (!spoken_for)
        {
            kept.push_back(d);
        }
    }
    return kept;
}
} // namespace mosaic::engine::ui
