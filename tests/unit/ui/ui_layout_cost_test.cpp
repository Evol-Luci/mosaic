// U8d's committed measurement. THIS FILE IS THE BEFORE/AFTER ARTIFACT, not a scratch script
// somebody ran once and pasted.
//
// WHY COUNTS AND NOT MILLISECONDS. The backlog's case for a `parent -> children` cache is
// stated entirely in counts -- "a 30-child stack in a world of 100 UiParent entities is
// roughly 12,000 visits, 120 sorts and 240 heap allocations per frame; a 200-child stack with
// 500 parents is ~400,000 visits and ~4,000 allocations per frame". Every one of those is
// deterministic and exactly countable. Wall time on this machine is not: there is no
// /usr/bin/time and no bc here, so the usual shell timing idiom prints a confident, wrong
// `0.00s`, and a debug build's run-to-run spread is wider than the effect being argued about.
// So the counts are the assertions and timing is corroboration reported elsewhere.
//
// WHAT MAKES IT A BEFORE/AFTER. These are exact equalities on the algorithm's shape, not
// thresholds. Anything that memoises the scan, changes the probe order, or hoists the
// enumeration out of the per-element path moves a number here and fails loudly with both
// values printed -- which is exactly what a before/after has to do to be worth committing.
// A threshold ("fewer than X visits") would have passed a cache that quietly did nothing.
//
// HOW TO RE-RUN: ctest --preset debug -R "layout cost" (the preset carries jobs: 8; do not
// add -j). Each case is a handful of milliseconds, far under the 10.0 s per-case budget in
// tools/ci/check_ctest_run.py, so it lives in the default suite rather than outside it.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/run/demo_scene.hpp"
#include "engine/ui/ui_button.hpp"
#include "engine/ui/ui_button_rects.hpp"
#include "engine/ui/ui_hierarchy.hpp"
#include "engine/ui/ui_layout.hpp"
#include "engine/ui/ui_layout_stats.hpp"
#include "engine/ui/ui_image.hpp"
#include "engine/ui/ui_panel.hpp"
#include "engine/ui/ui_parent.hpp"
#include "engine/ui/ui_stack.hpp"
#include "engine/ui/ui_text.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"

using Catch::Approx;
using mosaic::engine::World;
using mosaic::engine::ui::button_hit_candidates;
using mosaic::engine::ui::LayoutStats;
using mosaic::engine::ui::Rect;
using mosaic::engine::ui::take_layout_stats;
using mosaic::engine::ui::UiButton;
using mosaic::engine::ui::UiGrid;
using mosaic::engine::ui::UiPanel;
using mosaic::engine::ui::UiParent;
using mosaic::engine::ui::UiStack;

namespace
{

// The same two helpers ui_grid_flow_test.cpp uses, so the cost case and the correctness case
// measure the SAME shape. Duplicated rather than shared because a header shared between two
// test files is a third place for the fixture to drift from the thing it models.
World::EntityID make_container(World& world)
{
    const auto e = world.create_entity();
    UiPanel    p;
    p.anchor = 0;
    p.pos    = {0.0f, 0.0f};
    p.size   = {1.0f, 1.0f};
    world.add_component<UiPanel>(e, p);
    return e;
}

World::EntityID make_child(World& world, World::EntityID parent, int order)
{
    const auto e = world.create_entity();
    UiPanel    p;
    p.order = order;
    world.add_component<UiPanel>(e, p);
    world.add_component<UiParent>(e, UiParent{parent});
    return e;
}

// How many times a std::vector of stack_children's key type reallocates while being filled
// with `n` push_backs from empty.
//
// MEASURED, NOT ASSUMED. The backlog's estimate models two allocations per scan -- one vector
// in, one out. The real number is higher because `keyed` is filled with push_back and grows
// geometrically, and the growth factor is a standard-library implementation detail (libstdc++
// doubles; libc++ does too, MSVC uses 1.5). Hard-coding "9 reallocations for 256" would pin
// this test to one standard library and fail on another for a reason that is not a
// regression. Replaying the same push_back sequence against the same type asks the library
// what it actually does.
std::size_t growth_steps(std::size_t n)
{
    using Keyed = std::pair<std::pair<int, std::uint32_t>, mosaic::foundation::EntityID>;
    std::vector<Keyed> probe;
    std::size_t        capacity = probe.capacity();
    std::size_t        steps    = 0;
    for (std::size_t i = 0; i < n; ++i)
    {
        probe.push_back(Keyed{});
        if (probe.capacity() != capacity)
        {
            capacity = probe.capacity();
            ++steps;
        }
    }
    return steps;
}

// Resolve every child the way the HUD PANEL PASS does -- one resolve per element, which is
// what one frame of a panel-only grid costs. render_system.hpp's panel pass is
// for_each<UiPanel> + resolve_ui_rect, and resolve_ui_rect is resolve_parent_frame plus pure
// arithmetic, so this is the frame's layout cost with the device removed.
void resolve_like_panel_pass(World& world, const std::vector<World::EntityID>& kids)
{
    for (const auto k : kids)
    {
        const UiPanel* p = world.get_component<UiPanel>(k);
        REQUIRE(p != nullptr);
        (void)mosaic::engine::ui::resolve_ui_rect(
            world, k, mosaic::engine::ui::to_anchor(p->anchor), p->pos, p->size);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// The scan's cost, exactly, at three sizes.
// ---------------------------------------------------------------------------

TEST_CASE("the layout cost of one frame of a grid is quadratic in its child count",
          "[ui][layout][cost]")
{
    // THE MEASUREMENT U8d WAS ORDERED TO TAKE, as an assertion rather than a paste.
    //
    // One container, N children, each child resolved once -- which is exactly what one frame
    // of render_system.hpp's panel pass does to a grid whose children carry only a UiPanel.
    // Every number below is forced by the code and none of them is a threshold:
    //
    //   resolves         2N  -- the child, then its parent (which has no UiParent, so the
    //                           recursion stops there). This is the `d` in O(E x d x P), and
    //                           it is 2, not the 4-5 the backlog's review estimated. That
    //                           estimate was counting DRAW PASSES, not resolves per element.
    //   scans             N  -- one stack_children per laid-out element. THE CACHE WOULD
    //                           MAKE THIS 1.
    //   visits          N*N  -- each scan is a full for_each over every UiParent in the
    //                           world, and in this world every UiParent is a child of this
    //                           one container. This is the quadratic term and the entire
    //                           reason the item exists.
    //   sorts             N  -- one per scan, of N elements each.
    //   allocations  N*(g+1) -- g reallocations growing `keyed` plus one reserved `out`. The
    //                           backlog modelled 2 per scan; the measured number is 6 at
    //                           N=16 and 10 at N=256, so the estimate UNDERSTATED
    //                           allocations by 3-5x.
    //   probes           3N  -- slot_for_child's three container lookups, one set per
    //                           resolve that has a parent. U8a's was one.
    SECTION("sixteen children")
    {
        World      world;
        const auto p = make_container(world);
        world.add_component<UiGrid>(
            p, UiGrid{.cols = 4, .rows = 4, .spacing = 0.0f, .padding = 0.0f});
        std::vector<World::EntityID> kids;
        for (int i = 0; i < 16; ++i) { kids.push_back(make_child(world, p, i)); }

        (void)take_layout_stats();
        resolve_like_panel_pass(world, kids);
        const LayoutStats s = take_layout_stats();

        INFO("visits=" << s.visits << " scans=" << s.scans << " sorts=" << s.sorts
                       << " allocations=" << s.allocations << " resolves=" << s.resolves
                       << " probes=" << s.container_probes);
        REQUIRE(s.resolves == 32);
        REQUIRE(s.scans == 16);
        REQUIRE(s.visits == 256);
        REQUIRE(s.sorts == 16);
        REQUIRE(s.allocations == 16 * (growth_steps(16) + 1));
        REQUIRE(s.container_probes == 48);
    }

    SECTION("sixty-four children")
    {
        World      world;
        const auto p = make_container(world);
        world.add_component<UiGrid>(
            p, UiGrid{.cols = 8, .rows = 8, .spacing = 0.0f, .padding = 0.0f});
        std::vector<World::EntityID> kids;
        for (int i = 0; i < 64; ++i) { kids.push_back(make_child(world, p, i)); }

        (void)take_layout_stats();
        resolve_like_panel_pass(world, kids);
        const LayoutStats s = take_layout_stats();

        INFO("visits=" << s.visits << " scans=" << s.scans << " allocations=" << s.allocations);
        REQUIRE(s.resolves == 128);
        REQUIRE(s.scans == 64);
        // 4x the children for 16x the visits. The ratio is the finding, not the absolute
        // number: 256 -> 4096 is the quadratic, stated as two measurements rather than as a
        // complexity class.
        REQUIRE(s.visits == 4096);
        REQUIRE(s.sorts == 64);
        REQUIRE(s.allocations == 64 * (growth_steps(64) + 1));
        REQUIRE(s.container_probes == 192);
    }

    SECTION("two hundred and fifty-six children")
    {
        // U8c'S INVENTORY GRID, the size the backlog named as the trigger ("a UI of a few
        // hundred elements"). 65,536 component visits and 2,560 heap allocations for ONE
        // frame of ONE pass.
        World      world;
        const auto p = make_container(world);
        world.add_component<UiGrid>(
            p, UiGrid{.cols = 16, .rows = 16, .spacing = 0.0f, .padding = 0.0f});
        std::vector<World::EntityID> kids;
        for (int i = 0; i < 256; ++i) { kids.push_back(make_child(world, p, i)); }

        (void)take_layout_stats();
        resolve_like_panel_pass(world, kids);
        const LayoutStats s = take_layout_stats();

        INFO("visits=" << s.visits << " scans=" << s.scans << " allocations=" << s.allocations);
        REQUIRE(s.resolves == 512);
        REQUIRE(s.scans == 256);
        REQUIRE(s.visits == 65536);
        REQUIRE(s.sorts == 256);
        REQUIRE(s.allocations == 256 * (growth_steps(256) + 1));
        REQUIRE(s.container_probes == 768);
    }
}

TEST_CASE("the scan visits every UiParent in the world, not only the container's children",
          "[ui][layout][cost]")
{
    // THE `P` IN O(E x d x P), ISOLATED. The backlog's figures multiply the laid-out element
    // count by the world's TOTAL UiParent count -- "a 30-child stack in a world of 100
    // UiParent entities" -- and this is the case that shows those are genuinely independent
    // factors. Eight laid-out children plus 92 unrelated parented entities elsewhere in the
    // world: the child count does not change, the visit count quadruples.
    //
    // This is the assertion that would catch a cache keyed on the wrong thing. A cache built
    // per CONTAINER still pays P once; a cache built per WORLD pays it once in total.
    World      world;
    const auto p = make_container(world);
    world.add_component<UiGrid>(p,
                                UiGrid{.cols = 4, .rows = 2, .spacing = 0.0f, .padding = 0.0f});
    std::vector<World::EntityID> kids;
    for (int i = 0; i < 8; ++i) { kids.push_back(make_child(world, p, i)); }

    (void)take_layout_stats();
    resolve_like_panel_pass(world, kids);
    const LayoutStats bare = take_layout_stats();
    REQUIRE(bare.visits == 64); // 8 scans x 8 UiParent entities

    // 92 more parented entities with nothing to do with this container, so the world holds
    // 100 UiParent entities in total -- the backlog's own denominator.
    const auto elsewhere = make_container(world);
    for (int i = 0; i < 92; ++i) { (void)make_child(world, elsewhere, i); }

    (void)take_layout_stats();
    resolve_like_panel_pass(world, kids);
    const LayoutStats crowded = take_layout_stats();

    INFO("bare visits=" << bare.visits << " crowded visits=" << crowded.visits);
    REQUIRE(crowded.scans == bare.scans);      // same number of laid-out elements
    REQUIRE(crowded.visits == 8 * 100);              // but 12.5x the visits
    REQUIRE(crowded.visits == bare.visits * 100 / 8); // stated as the ratio, 12.5x
}

// ---------------------------------------------------------------------------
// The premise the backlog states about scenes with no container.
// ---------------------------------------------------------------------------

TEST_CASE("a parented element with no container scans nothing but still probes three times",
          "[ui][layout][cost]")
{
    // "NON-STACK SCENES PAY NOTHING -- the UiStack lookup short-circuits before the scan."
    // That claim is in the backlog and it was written when the lookup was ONE get_component.
    // U8c made it three (UiStack, UiGrid, UiFlow). Both halves are checked here rather than
    // believed:
    //
    //   - The scan really is short-circuited: zero scans, zero visits, zero allocations,
    //     however many parented elements there are. So the O(E x P) term is genuinely not
    //     paid, and that half of the claim HOLDS.
    //   - "Nothing" is not literally nothing, and it grew by 3x. Every parented element in
    //     every scene pays three O(1) sparse-set probes per resolve where U8a paid one. They
    //     do not scan and they do not allocate, so the claim's SPIRIT holds -- but the
    //     constant is now proportional to the number of container types, which is the thing
    //     a fourth container would make worse and nobody would notice.
    World      world;
    const auto p = make_container(world); // a panel, deliberately NO UiStack/UiGrid/UiFlow
    std::vector<World::EntityID> kids;
    for (int i = 0; i < 64; ++i) { kids.push_back(make_child(world, p, i)); }

    (void)take_layout_stats();
    resolve_like_panel_pass(world, kids);
    const LayoutStats s = take_layout_stats();

    INFO("scans=" << s.scans << " visits=" << s.visits << " probes=" << s.container_probes);
    REQUIRE(s.scans == 0);
    REQUIRE(s.visits == 0);
    REQUIRE(s.sorts == 0);
    REQUIRE(s.allocations == 0);
    // Three per resolve-with-a-parent, and the 64 parent resolves have no UiParent of their
    // own so they contribute none: 64 x 3, not 128 x 3.
    REQUIRE(s.container_probes == 192);
    REQUIRE(s.resolves == 128);
}

// ---------------------------------------------------------------------------
// The multiplicity the backlog puts at "4-5 times a frame".
// ---------------------------------------------------------------------------

TEST_CASE("an element is resolved once per visual component it carries, not four times",
          "[ui][layout][cost]")
{
    // THE PREMISE CHECK. The whole-branch review's figure is "the resolve happens in the
    // panel pass, the image pass, the text pass AND button_hit_candidates, so 4-5 times a
    // frame". Counting the call sites says something narrower: there are exactly four in
    // production (render_system.hpp's panel, image and text passes, and
    // ui_button_rects.hpp's flatten), but each one iterates ITS OWN component, so an element
    // is resolved once per such component it actually carries. 4-5 is the worst case for an
    // element carrying all four, not the per-element rate.
    //
    // That matters to the cache decision in the obvious direction: it divides the frame cost
    // by about four for the realistic inventory-grid shape, where a slot carries a panel and
    // maybe a button.
    World      world;
    const auto p = make_container(world);
    world.add_component<UiGrid>(p,
                                UiGrid{.cols = 2, .rows = 1, .spacing = 0.0f, .padding = 0.0f});
    const auto panel_only = make_child(world, p, 0);

    // A second child that is ALSO a button, so the hit path reaches it too.
    const auto clickable = make_child(world, p, 1);
    UiButton   b;
    world.add_component<UiButton>(clickable, b);

    // One frame's panel pass: two elements, one resolve each.
    (void)take_layout_stats();
    resolve_like_panel_pass(world, {panel_only, clickable});
    const LayoutStats draw = take_layout_stats();
    REQUIRE(draw.scans == 2); // not 8

    // One frame's hit path: button_hit_candidates iterates UiButton, so it reaches ONE of the
    // two, not both.
    (void)take_layout_stats();
    const auto cands = button_hit_candidates(world);
    const LayoutStats hit = take_layout_stats();

    INFO("draw scans=" << draw.scans << " hit scans=" << hit.scans);
    REQUIRE(cands.size() == 1);
    REQUIRE(hit.scans == 1);
    // So the frame's total for this pair is 3 scans across both paths, not 2 x 4 = 8.
    REQUIRE(draw.scans + hit.scans == 3);
}

// ---------------------------------------------------------------------------
// The property a cache would endanger. THIS IS THE GATE'S SECOND HALF.
// ---------------------------------------------------------------------------

TEST_CASE("the draw path and the hit path resolve a grid child to the same rect",
          "[ui][layout][cost]")
{
    // WHY THIS IS THE ASSERTION AND NOT THE FRAME TIME. slot_for_child is pure and lazy, so
    // the panel pass and button_hit_candidates agree BY CONSTRUCTION: both call
    // resolve_parent_frame on the live world and neither can read a stale enumeration. A
    // memoised parent -> children index replaces that guarantee with an invalidation
    // question, and the first thing an invalidation bug does is make a click land somewhere
    // the panel is not drawn.
    //
    // So this case asserts the guarantee itself, against the live world, THREE TIMES: on a
    // settled world, after a child is destroyed, and after a child is created. The two
    // mutations are the "created or destroyed mid-frame" cases the slice was asked about --
    // with no cache they cost nothing to pass, which is exactly the point: this is the
    // trip-wire that fires the day a cache is added whose generation the two paths can
    // disagree about.
    //
    // AGREEMENT ALONE IS NOT ENOUGH, AND THAT IS A MEASURED FINDING, NOT A PRECAUTION. U8d
    // ran the obvious cache as a mutation -- a function-local memo in slot_for_child,
    // populated once and never invalidated -- and the agreement half of this case stayed
    // GREEN. Of course it did: both paths read the SAME cache, so a stale enumeration makes
    // them agree on a wrong answer rather than disagree. Agreement is necessary and not
    // sufficient, and a slice that had asserted only agreement would have shipped that cache
    // believing it was covered.
    //
    // So every rect is ALSO checked against an expected value computed WITHOUT the layout
    // path: the expected order is re-derived here from the authored keys, and the expected
    // slot from grid_slot directly. The duplication is deliberate -- a trip-wire whose job
    // is to catch a stale enumeration must not ask the enumeration what the answer is. That
    // is what makes this case red under the memo mutation where the agreement half was not.
    World      world;
    const auto p = make_container(world);
    world.add_component<UiGrid>(p,
                                UiGrid{.cols = 4, .rows = 4, .spacing = 0.0f, .padding = 0.0f});
    std::vector<World::EntityID> kids;
    // The panel and the button are authored with the SAME anchor, pos and size, so the two
    // paths' rects are comparable at all: the hit path resolves the button's rect and the
    // draw path the panel's, and the only thing they share is the FRAME that
    // resolve_parent_frame hands both. Identical geometry is what turns "the frames agree"
    // into an assertion on the rects.
    for (int i = 0; i < 16; ++i)
    {
        const auto k = make_child(world, p, i);
        UiPanel*   pan = world.get_component<UiPanel>(k);
        REQUIRE(pan != nullptr);
        pan->anchor = 0;
        pan->pos    = {0.25f, 0.25f};
        pan->size   = {0.5f, 0.5f};
        UiButton b;
        b.anchor = 0;
        b.pos    = {0.25f, 0.25f};
        b.size   = {0.5f, 0.5f};
        world.add_component<UiButton>(k, b);
        kids.push_back(k);
    }

    // The container's own rect, which its children's slots divide. Authored to fill the
    // frame exactly, so it IS the frame and the expected slots are plain grid_slot values.
    const Rect container_rect{0.0f, 0.0f, 1.0f, 1.0f};

    // THE EXPECTED ORDER, RE-DERIVED FROM THE AUTHORED KEYS and deliberately not from
    // stack_children: a check on a possibly-stale enumeration must not be built out of that
    // enumeration. Same rule the layout path follows -- (UiPanel::order, entity index), the
    // index as a deterministic tiebreak -- stated once here so a cache that reorders
    // children has something independent to be wrong against.
    const auto expected_order = [&]()
    {
        std::vector<std::pair<std::pair<int, std::uint32_t>, World::EntityID>> keyed;
        world.for_each<UiParent>(
            [&](mosaic::foundation::EntityID e, UiParent& up)
            {
                if (up.parent != p) { return; }
                const UiPanel* pan = world.get_component<UiPanel>(e);
                keyed.push_back({{pan != nullptr ? pan->order : 0, e.index()}, e});
            });
        std::sort(keyed.begin(), keyed.end(),
                  [](const auto& l, const auto& r) { return l.first < r.first; });
        std::vector<World::EntityID> out;
        for (const auto& k : keyed) { out.push_back(k.second); }
        return out;
    };

    // The draw path's rect for a child, computed the way the panel pass computes it, versus
    // the hit path's rect for the same entity out of the shared flatten -- and both against
    // the independently computed truth.
    const auto agree = [&](const char* when)
    {
        const std::vector<World::EntityID> order = expected_order();
        const auto                         cands = button_hit_candidates(world);
        for (const auto& c : cands)
        {
            const UiPanel* pan = world.get_component<UiPanel>(c.e);
            REQUIRE(pan != nullptr);
            const Rect drawn = mosaic::engine::ui::resolve_ui_rect(
                world, c.e, mosaic::engine::ui::to_anchor(pan->anchor), pan->pos, pan->size);
            INFO(when << ": entity " << c.e.value());
            // 1. The two paths agree with each other.
            REQUIRE(c.rect.x == Approx(drawn.x));
            REQUIRE(c.rect.y == Approx(drawn.y));
            REQUIRE(c.rect.w == Approx(drawn.w));
            REQUIRE(c.rect.h == Approx(drawn.h));
            // 2. And they agree with the truth. Without this, a cache both paths share makes
            // them agree on a stale slot and nothing above notices.
            const auto at = std::find(order.begin(), order.end(), c.e);
            REQUIRE(at != order.end());
            const std::size_t i = static_cast<std::size_t>(at - order.begin());
            const Rect expected_slot =
                mosaic::engine::ui::grid_slot(container_rect, 4, 4, 0.0f, 0.0f, i);
            const Rect want = mosaic::engine::ui::resolve_in(
                expected_slot, mosaic::engine::ui::to_anchor(pan->anchor), pan->pos,
                pan->size);
            INFO(when << ": entity " << c.e.value() << " expected at index " << i);
            REQUIRE(drawn.x == Approx(want.x));
            REQUIRE(drawn.y == Approx(want.y));
            REQUIRE(drawn.w == Approx(want.w));
            REQUIRE(drawn.h == Approx(want.h));
        }
        return cands.size();
    };

    REQUIRE(agree("settled") == 16);

    // DESTROYED. clear()-free immediate destruction: destroy_entity is deferred to tick(),
    // so the tick is what makes the hole real and the sparse set swap the last element into
    // it -- the exact move that makes an iteration-derived order unsafe.
    world.destroy_entity(kids[5]);
    world.tick(0.0f);
    REQUIRE(!world.is_valid(kids[5]));
    REQUIRE(agree("after a destroy") == 15);

    // CREATED. A new child appears with an order that places it in the middle of the
    // existing ones, so every later child's slot shifts -- both paths must shift together.
    const auto late = make_child(world, p, 5);
    UiPanel*   lp = world.get_component<UiPanel>(late);
    REQUIRE(lp != nullptr);
    lp->anchor = 0;
    lp->pos    = {0.25f, 0.25f};
    lp->size   = {0.5f, 0.5f};
    UiButton lb;
    lb.anchor = 0;
    lb.pos    = {0.25f, 0.25f};
    lb.size   = {0.5f, 0.5f};
    world.add_component<UiButton>(late, lb);
    REQUIRE(agree("after a create") == 16);
}

TEST_CASE("the layout counters move when the scan is made to do more work",
          "[ui][layout][cost]")
{
    // THE INSTRUMENT'S OWN TEST, and it is here because U8d may legitimately ship a
    // measurement and no cache -- in which case the counters ARE the deliverable, and a
    // counter nobody has seen move is not evidence of anything.
    //
    // It makes the scan artificially worse WITHOUT touching the layout code, by adding
    // unrelated UiParent entities to the world (the `P` factor) and by resolving more
    // elements (the `E` factor), and asserts each counter responds to the right factor:
    //
    //   - adding parents raises `visits` and leaves `scans` alone
    //   - resolving more elements raises `scans` AND `visits`
    //
    // A counter wired to the wrong place -- say, incremented once per stack_children call
    // instead of once per visited entity -- fails the first half. A counter that is never
    // reset fails the second.
    World      world;
    const auto p = make_container(world);
    world.add_component<UiStack>(
        p, UiStack{.axis = 1, .spacing = 0.0f, .padding = 0.0f});
    std::vector<World::EntityID> kids;
    for (int i = 0; i < 4; ++i) { kids.push_back(make_child(world, p, i)); }

    (void)take_layout_stats();
    resolve_like_panel_pass(world, kids);
    const LayoutStats base = take_layout_stats();
    REQUIRE(base.scans == 4);
    REQUIRE(base.visits == 16);

    // P x 4, E unchanged.
    const auto elsewhere = make_container(world);
    for (int i = 0; i < 12; ++i) { (void)make_child(world, elsewhere, i); }
    (void)take_layout_stats();
    resolve_like_panel_pass(world, kids);
    const LayoutStats more_parents = take_layout_stats();
    INFO("scans " << base.scans << " -> " << more_parents.scans << ", visits " << base.visits
                  << " -> " << more_parents.visits);
    REQUIRE(more_parents.scans == base.scans); // E did not change
    REQUIRE(more_parents.visits == 64);        // 4 x 16, up from 4 x 4

    // E x 2, P unchanged: resolve the same elements twice, as a frame with two draw passes
    // over them would.
    (void)take_layout_stats();
    resolve_like_panel_pass(world, kids);
    resolve_like_panel_pass(world, kids);
    const LayoutStats twice = take_layout_stats();
    REQUIRE(twice.scans == 2 * more_parents.scans);
    REQUIRE(twice.visits == 2 * more_parents.visits);

    // And the snapshot really zeroes: an immediately following take with no work between
    // must be empty, or every number above is cumulative and meaningless.
    const LayoutStats empty = take_layout_stats();
    REQUIRE(empty.scans == 0);
    REQUIRE(empty.visits == 0);
    REQUIRE(empty.resolves == 0);
}

// ---------------------------------------------------------------------------
// The shipped scene, so the synthetic number has a real one beside it.
// ---------------------------------------------------------------------------

TEST_CASE("the whole shipped demo scene costs a three-figure layout bill per frame",
          "[ui][layout][cost]")
{
    // WHY A REAL-FRAME NUMBER MATTERS TO THE DECISION. The synthetic 256-child grid says what
    // an inventory grid would cost; this says what the engine actually pays today, which is
    // the other half of "is the cost real". The demo scene is the largest authored UI in the
    // repository -- U8b's stack plus U8c's 3x2 grid and 5-item flow.
    //
    // It simulates ONE FRAME of all four production resolve sites, each over the component it
    // actually iterates (see render_system.hpp's three HUD passes and
    // ui_button_rects.hpp's flatten), which is the only honest way to count the
    // multiplicity: an element is resolved once per such component it carries.
    //
    // AN UPPER BOUND, stated rather than implied. The real image and text passes also
    // early-out when a texture handle or a font atlas is unavailable, and neither can resolve
    // here with no device and no asset registry -- so the true frame resolves at most this
    // many times and possibly fewer. An upper bound is the right side to be wrong on for a
    // "do we need a cache" question.
    //
    // THE ASSERTIONS ARE BOUNDS, not equalities, and that is deliberate: this case must not
    // become the fifth test that fails whenever someone adds a HUD element to the demo. The
    // 2000-visit ceiling is ~30x today's measured value, so it catches an order-of-magnitude
    // change in the scene's layout cost and nothing smaller. The exact figures live in
    // docs/record/2026-10.md, where a number that moves is a note rather than a red test.
    mosaic::engine::World world;
    mosaic::app::build_default_scene(world);

    (void)take_layout_stats();
    world.for_each<UiPanel>(
        [&](mosaic::foundation::EntityID e, UiPanel& p)
        {
            if (!p.visible || p.alpha <= 0.0f) { return; }
            (void)mosaic::engine::ui::resolve_ui_rect(
                world, e, mosaic::engine::ui::to_anchor(p.anchor), p.pos, p.size);
        });
    world.for_each<mosaic::engine::ui::UiImage>(
        [&](mosaic::foundation::EntityID e, mosaic::engine::ui::UiImage& im)
        {
            if (!im.visible || im.alpha <= 0.0f) { return; }
            (void)mosaic::engine::ui::resolve_ui_rect(
                world, e, mosaic::engine::ui::to_anchor(im.anchor), im.pos, im.size);
        });
    world.for_each<mosaic::engine::ui::UiText>(
        [&](mosaic::foundation::EntityID e, mosaic::engine::ui::UiText& t)
        {
            if (!t.visible || t.alpha <= 0.0f || t.text.empty()) { return; }
            (void)mosaic::engine::ui::resolve_parent_frame(world, e);
        });
    (void)button_hit_candidates(world);
    const LayoutStats frame = take_layout_stats();

    INFO("demo frame: resolves=" << frame.resolves << " scans=" << frame.scans << " visits="
                                 << frame.visits << " sorts=" << frame.sorts
                                 << " allocations=" << frame.allocations
                                 << " probes=" << frame.container_probes);
    // Non-trivial, so a scene that silently lost its parented UI does not pass this vacuously.
    REQUIRE(frame.scans > 8);
    // And three orders of magnitude below the synthetic grid's 65,536. THIS is the sentence
    // the cache decision rests on: the shipped scene's whole-frame layout bill is a few
    // hundred component visits, which is not a cost worth trading a correctness guarantee for.
    REQUIRE(frame.visits < 2000);
    REQUIRE(frame.allocations < 400);
}
