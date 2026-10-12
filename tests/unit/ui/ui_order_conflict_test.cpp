#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/ui/ui_button.hpp"
#include "engine/ui/ui_order_check.hpp"
#include "engine/ui/ui_panel.hpp"
#include "engine/ui/ui_parent.hpp"
#include "engine/world/world.hpp"

using mosaic::engine::World;
using mosaic::engine::ui::Anchor;
using mosaic::engine::ui::find_order_conflicts;
using mosaic::engine::ui::OrderConflictKind;
using mosaic::engine::ui::UiButton;
using mosaic::engine::ui::UiPanel;
using mosaic::engine::ui::UiParent;
using mosaic::foundation::EntityID;

namespace
{
// A button with a panel on its own entity -- the ownership rule -- at an explicit rect.
EntityID add_button(World& world, float x, float y, float w, float h, int button_order,
                    int panel_order)
{
    const auto e = world.create_entity();
    UiButton   b;
    b.anchor = static_cast<int>(Anchor::TopLeft);
    b.pos    = {x, y};
    b.size   = {w, h};
    b.order  = button_order;
    world.add_component<UiButton>(e, b);
    UiPanel p;
    p.anchor = static_cast<int>(Anchor::TopLeft);
    p.pos    = {x, y};
    p.size   = {w, h};
    p.order  = panel_order;
    world.add_component<UiPanel>(e, p);
    return e;
}
} // namespace

TEST_CASE("two overlapping buttons ranked oppositely are an inverted conflict", "[ui][conflict]")
{
    // THE BUG. A's button order wins the click (5 > 2) while B's panel is drawn on top
    // (7 > 1), so the click lands on the button the user cannot see.
    World      world;
    const auto a = add_button(world, 0.1f, 0.1f, 0.3f, 0.3f, 5, 1);
    const auto b = add_button(world, 0.2f, 0.2f, 0.3f, 0.3f, 2, 7);

    const auto found = find_order_conflicts(world);

    REQUIRE(found.size() == 1);
    REQUIRE(found[0].kind == OrderConflictKind::Inverted);
    REQUIRE(found[0].a == a);
    REQUIRE(found[0].b == b);
    // All four orders travel with the finding, matched to their own entity -- a message
    // that transposed them would tell a user to change the wrong number.
    REQUIRE(found[0].a_button_order == 5);
    REQUIRE(found[0].a_panel_order == 1);
    REQUIRE(found[0].b_button_order == 2);
    REQUIRE(found[0].b_panel_order == 7);
}

TEST_CASE("two overlapping buttons ranked the same way are silent", "[ui][conflict]")
{
    // The state a correctly authored scene is in: the button that receives the click is also
    // the one drawn on top.
    World world;
    add_button(world, 0.1f, 0.1f, 0.3f, 0.3f, 5, 7);
    add_button(world, 0.2f, 0.2f, 0.3f, 0.3f, 2, 1);

    REQUIRE(find_order_conflicts(world).empty());
}

TEST_CASE("non-overlapping buttons are silent however their orders rank", "[ui][conflict]")
{
    // Overlap is the gate. Two buttons at opposite corners cannot misroute a click between
    // them no matter what their orders say, and reporting them would make the diagnostic fire
    // on most scenes.
    World world;
    add_button(world, 0.0f, 0.0f, 0.2f, 0.2f, 5, 1);
    add_button(world, 0.7f, 0.7f, 0.2f, 0.2f, 2, 7);

    REQUIRE(find_order_conflicts(world).empty());
}

TEST_CASE("buttons touching along an edge are treated as overlapping", "[ui][conflict]")
{
    // Pins the INCLUSIVE comparison. The first ends at x = 0.4; the second starts there. A
    // pointer at exactly x = 0.4 is inside both, because topmost_hit tests containment with
    // >= and <=, so the misroute is reachable on that line.
    World world;
    add_button(world, 0.2f, 0.2f, 0.2f, 0.2f, 5, 1);
    add_button(world, 0.4f, 0.2f, 0.2f, 0.2f, 2, 7);

    const auto found = find_order_conflicts(world);

    REQUIRE(found.size() == 1);
    REQUIRE(found[0].kind == OrderConflictKind::Inverted);
}

TEST_CASE("tied button orders with differing panel orders are ambiguous", "[ui][conflict]")
{
    // Equal button orders means the hit tie-break falls to iteration order over the UiButton
    // sparse set, while the panels are explicitly ranked. Destroying an unrelated button can
    // flip the click without the scene looking any different.
    World world;
    add_button(world, 0.1f, 0.1f, 0.3f, 0.3f, 3, 1);
    add_button(world, 0.2f, 0.2f, 0.3f, 0.3f, 3, 8);

    const auto found = find_order_conflicts(world);

    REQUIRE(found.size() == 1);
    REQUIRE(found[0].kind == OrderConflictKind::Ambiguous);
}

TEST_CASE("tied panel orders with differing button orders are ambiguous", "[ui][conflict]")
{
    // The mirror image: the click is decided but what the user sees on top is not.
    World world;
    add_button(world, 0.1f, 0.1f, 0.3f, 0.3f, 1, 4);
    add_button(world, 0.2f, 0.2f, 0.3f, 0.3f, 9, 4);

    const auto found = find_order_conflicts(world);

    REQUIRE(found.size() == 1);
    REQUIRE(found[0].kind == OrderConflictKind::Ambiguous);
}

TEST_CASE("two fully tied buttons are not reported", "[ui][conflict]")
{
    // Deliberately silent: the author expressed no preference on either axis, so there is no
    // disagreement to fix. Two default-constructed overlapping buttons are this case, which
    // is why reporting it would be noise rather than a finding.
    World world;
    add_button(world, 0.1f, 0.1f, 0.3f, 0.3f, 0, 0);
    add_button(world, 0.2f, 0.2f, 0.3f, 0.3f, 0, 0);

    REQUIRE(find_order_conflicts(world).empty());
}

TEST_CASE("a button with no panel of its own is skipped entirely", "[ui][conflict]")
{
    // Under the ownership rule its visual rank is undefined, so it must be skipped WHOLE
    // rather than compared as if its panel order were zero -- which would invent a conflict
    // against the neighbour that does have one. This is also the shape of the recorded blind
    // spot: a button overlaying a panel on a DIFFERENT entity looks exactly like this.
    World      world;
    const auto bare = world.create_entity();
    UiButton   b;
    b.anchor = static_cast<int>(Anchor::TopLeft);
    b.pos    = {0.1f, 0.1f};
    b.size   = {0.3f, 0.3f};
    b.order  = 9; // would invert against the neighbour below if it were ranked
    world.add_component<UiButton>(bare, b);

    add_button(world, 0.2f, 0.2f, 0.3f, 0.3f, 1, 8);

    REQUIRE(find_order_conflicts(world).empty());
}

TEST_CASE("three mutually overlapping conflicting buttons yield three findings", "[ui][conflict]")
{
    // Every PAIR is reported, not one finding per entity: orders 1/3, 2/2, 3/1 invert against
    // each other in all three combinations. A loop that broke out after the first conflict
    // for an entity would return two.
    World world;
    add_button(world, 0.1f, 0.1f, 0.4f, 0.4f, 1, 3);
    add_button(world, 0.15f, 0.15f, 0.4f, 0.4f, 2, 2);
    add_button(world, 0.2f, 0.2f, 0.4f, 0.4f, 3, 1);

    REQUIRE(find_order_conflicts(world).size() == 3);
}

TEST_CASE("the finding names the lower entity first whatever the creation order", "[ui][conflict]")
{
    // Identity must not depend on which entity the outer loop reached first, or the same
    // conflict keys two ways and warns twice. Built in BOTH creation orders; the finding must
    // come out the same way round each time.
    World      first;
    const auto early = add_button(first, 0.1f, 0.1f, 0.3f, 0.3f, 5, 1);
    const auto late  = add_button(first, 0.2f, 0.2f, 0.3f, 0.3f, 2, 7);
    const auto fa    = find_order_conflicts(first);
    REQUIRE(fa.size() == 1);
    REQUIRE(fa[0].a.value() < fa[0].b.value());
    REQUIRE(fa[0].a == early);
    REQUIRE(fa[0].b == late);
    // The orders follow the entities, not the loop: `a` is the one with button order 5.
    REQUIRE(fa[0].a_button_order == 5);

    // Same geometry, same orders, opposite creation order.
    World second;
    add_button(second, 0.2f, 0.2f, 0.3f, 0.3f, 2, 7);
    add_button(second, 0.1f, 0.1f, 0.3f, 0.3f, 5, 1);
    const auto fb = find_order_conflicts(second);
    REQUIRE(fb.size() == 1);
    REQUIRE(fb[0].a.value() < fb[0].b.value());
    // The earlier-created entity is now the one with button order 2, and `a` follows the
    // ENTITY, so the orders swap with it rather than staying put.
    REQUIRE(fb[0].a_button_order == 2);
    REQUIRE(fb[0].b_button_order == 5);
}

TEST_CASE("orders differing only in sign are an inverted conflict", "[ui][conflict]")
{
    // The exact gap a mutation probe found in #240: -2 against 2 is EQUAL in magnitude and
    // opposite in z, so a comparison written on std::abs passes every other case here and
    // misses the worst disagreement there is.
    World world;
    add_button(world, 0.1f, 0.1f, 0.3f, 0.3f, -2, 2);
    add_button(world, 0.2f, 0.2f, 0.3f, 0.3f, 2, -2);

    const auto found = find_order_conflicts(world);

    REQUIRE(found.size() == 1);
    REQUIRE(found[0].kind == OrderConflictKind::Inverted);
}

TEST_CASE("two zero-size buttons at the same point conflict", "[ui][conflict]")
{
    // ui_hit.hpp states that a zero-size rect is hit at exactly its corner, so a pointer on
    // that point is inside both and the misroute is genuinely reachable. Degenerate geometry,
    // real consequence -- and an overlap test written with `<` would drop it.
    World world;
    add_button(world, 0.5f, 0.5f, 0.0f, 0.0f, 4, 1);
    add_button(world, 0.5f, 0.5f, 0.0f, 0.0f, 1, 4);

    REQUIRE(find_order_conflicts(world).size() == 1);
}

TEST_CASE("a UiParent cycle does not hang the conflict check", "[ui][conflict]")
{
    // A scene file can author a cycle. resolve_parent_frame depth-caps at 16, and the check
    // inherits that only because it resolves rects through the same path -- so this asserts
    // termination, not a particular rect. If this test ever hangs rather than fails, the cap
    // has been lost somewhere up the chain.
    World      world;
    const auto x = add_button(world, 0.1f, 0.1f, 0.3f, 0.3f, 5, 1);
    const auto y = add_button(world, 0.2f, 0.2f, 0.3f, 0.3f, 2, 7);
    world.add_component<UiParent>(x, UiParent{y});
    world.add_component<UiParent>(y, UiParent{x});

    REQUIRE_NOTHROW(find_order_conflicts(world));
}

TEST_CASE("a stale UiParent target falls back to the screen frame", "[ui][conflict]")
{
    // The parent is destroyed, so resolve_parent_frame returns the whole screen and the
    // button resolves at its own numbers. Must not crash, and must still rank: a check that
    // skipped entities with a dangling parent would go quiet on exactly the scenes most
    // likely to be mid-edit.
    World      world;
    const auto ghost = world.create_entity();
    const auto x     = add_button(world, 0.1f, 0.1f, 0.3f, 0.3f, 5, 1);
    world.add_component<UiParent>(x, UiParent{ghost});
    world.destroy_entity(ghost);
    world.tick(0.0f); // commit the deferred destruction

    add_button(world, 0.2f, 0.2f, 0.3f, 0.3f, 2, 7);

    const auto found = find_order_conflicts(world);
    REQUIRE(found.size() == 1);
    REQUIRE(found[0].kind == OrderConflictKind::Inverted);
}

TEST_CASE("buttons overlapping in x but not y are silent", "[ui][conflict]")
{
    // Both occupy x 0.1 to 0.4; one sits at the top, the other near the bottom. They share
    // no point, so there is nothing to misroute.
    //
    // Found by a mutation probe: dropping the Y comparison from rects_overlap left the suite
    // green, because the other non-overlapping case separates its buttons on BOTH axes and
    // stays non-overlapping with either half of the test removed. One axis at a time is the
    // distinguishing input.
    World world;
    add_button(world, 0.1f, 0.0f, 0.3f, 0.2f, 5, 1);
    add_button(world, 0.1f, 0.6f, 0.3f, 0.2f, 2, 7);

    REQUIRE(find_order_conflicts(world).empty());
}

TEST_CASE("buttons overlapping in y but not x are silent", "[ui][conflict]")
{
    // The mirror of the case above, so dropping the X comparison is caught too. Symmetry in
    // the code deserves symmetry in the tests; without this, half the overlap test is
    // unguarded.
    World world;
    add_button(world, 0.0f, 0.1f, 0.2f, 0.3f, 5, 1);
    add_button(world, 0.6f, 0.1f, 0.2f, 0.3f, 2, 7);

    REQUIRE(find_order_conflicts(world).empty());
}

TEST_CASE("normalization holds when iteration order is reversed by a destroy", "[ui][conflict]")
{
    // THE CASE THAT MAKES THE NORMALIZATION LOAD-BEARING, and the one the other ordering test
    // cannot reach: when entities are simply created in sequence, the UiButton sparse set
    // iterates them in ascending order and the swap never fires.
    //
    // Destroying the FIRST button makes the set's swap-and-pop move the LAST element into its
    // slot, so iteration then yields the later-created entity first -- measured, not assumed.
    // This is exactly the production scenario ui_hit.hpp's rule 2 warns about: "destroying any
    // unrelated button can reorder an equal-order run".
    //
    // Found by a mutation probe: deleting the swap left the suite green, because every other
    // case here happens to iterate in ascending order already.
    World      world;
    const auto doomed = world.create_entity();
    world.add_component<UiButton>(doomed, UiButton{});

    const auto lower  = add_button(world, 0.1f, 0.1f, 0.3f, 0.3f, 5, 1);
    const auto higher = add_button(world, 0.2f, 0.2f, 0.3f, 0.3f, 2, 7);
    REQUIRE(lower.value() < higher.value());

    world.destroy_entity(doomed);
    world.tick(0.0f); // commit, and reorder the set

    const auto found = find_order_conflicts(world);

    REQUIRE(found.size() == 1);
    // `a` is the lower entity even though the loop reached `higher` first.
    REQUIRE(found[0].a == lower);
    REQUIRE(found[0].b == higher);
    // And the orders travelled with their own entities rather than staying put.
    REQUIRE(found[0].a_button_order == 5);
    REQUIRE(found[0].a_panel_order == 1);
    REQUIRE(found[0].b_button_order == 2);
    REQUIRE(found[0].b_panel_order == 7);
}

TEST_CASE("a per-entity finding is dropped when its entity is in a conflict", "[ui][conflict]")
{
    // ONE LINE PER PROBLEM. The per-entity lint and the pairwise check both have something
    // to say about a button whose own orders disagree AND which conflicts with a neighbour,
    // and the pairwise message is strictly the more useful of the two: it already carries
    // all four orders plus the consequence. Reporting both says the same thing twice.
    //
    // Found by a human working the verification checklist -- the editor emitted two warnings
    // for one authoring mistake, which no test noticed because each diagnostic was correct
    // on its own.
    using mosaic::engine::ui::OrderConflict;
    using mosaic::engine::ui::OrderDisagreement;
    using mosaic::engine::ui::without_entities_in_conflict;

    const EntityID in_conflict{10, 1};
    const EntityID alone{20, 1};

    const std::vector<OrderDisagreement> disagreements{
        OrderDisagreement{in_conflict, 5, 0},
        OrderDisagreement{alone, 3, 1},
    };
    const std::vector<OrderConflict> conflicts{
        OrderConflict{EntityID{9, 1}, in_conflict, OrderConflictKind::Inverted, 0, 0, 5, -1},
    };

    const auto kept = without_entities_in_conflict(disagreements, conflicts);

    REQUIRE(kept.size() == 1);
    REQUIRE(kept[0].e == alone); // the one the pairwise check says nothing about
}

TEST_CASE("the suppression matches an entity in either position of a pair", "[ui][conflict]")
{
    // `a` and `b` are assigned by entity value, not by which one the author edited, so a
    // suppression that only checked `a` would still double-report half the time -- and which
    // half would depend on entity ids, which is the worst kind of intermittent.
    using mosaic::engine::ui::OrderConflict;
    using mosaic::engine::ui::OrderDisagreement;
    using mosaic::engine::ui::without_entities_in_conflict;

    const EntityID low{4, 1};
    const EntityID high{8, 1};
    const std::vector<OrderConflict> conflicts{
        OrderConflict{low, high, OrderConflictKind::Inverted, 0, 0, 5, -1},
    };

    REQUIRE(without_entities_in_conflict({OrderDisagreement{low, 1, 2}}, conflicts).empty());
    REQUIRE(without_entities_in_conflict({OrderDisagreement{high, 1, 2}}, conflicts).empty());
}

TEST_CASE("with no conflicts every per-entity finding survives", "[ui][conflict]")
{
    // The common case, and the lint's whole reason to exist: a single button whose orders
    // drifted apart, before any second button makes it a real misroute.
    using mosaic::engine::ui::OrderDisagreement;
    using mosaic::engine::ui::without_entities_in_conflict;

    const std::vector<OrderDisagreement> disagreements{
        OrderDisagreement{EntityID{1, 1}, 5, 0},
        OrderDisagreement{EntityID{2, 1}, 3, 1},
    };

    REQUIRE(without_entities_in_conflict(disagreements, {}).size() == 2);
    REQUIRE(without_entities_in_conflict({}, {}).empty());
}
