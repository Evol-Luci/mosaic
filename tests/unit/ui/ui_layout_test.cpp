#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/ui/ui_image.hpp"
#include "engine/ui/ui_layout.hpp"
#include "engine/ui/ui_panel.hpp"
#include "engine/ui/ui_parent.hpp"
#include "engine/ui/ui_stack.hpp"
#include "engine/ui/ui_text.hpp"
#include "engine/world/world.hpp"

using Catch::Approx;
using mosaic::engine::World;
using mosaic::engine::ui::child_layout_order;
using mosaic::engine::ui::stack_children;
using mosaic::engine::ui::UiPanel;
using mosaic::engine::ui::UiParent;
using mosaic::engine::ui::UiStack;

namespace
{
// A parent carrying a panel (so it has a rect) and a stack.
World::EntityID make_stack_parent(World& world, int axis = 0)
{
    const auto e = world.create_entity();
    world.add_component<UiPanel>(e, UiPanel{});
    world.add_component<UiStack>(e, UiStack{axis, 0.0f, 0.0f});
    return e;
}

// A child panel with an authored draw order.
World::EntityID make_child(World& world, World::EntityID parent, int order)
{
    const auto e = world.create_entity();
    UiPanel p;
    p.order = order;
    world.add_component<UiPanel>(e, p);
    world.add_component<UiParent>(e, UiParent{parent});
    return e;
}
} // namespace

TEST_CASE("stack_children finds only the named parent's children", "[ui][layout]")
{
    World world;
    const auto a  = make_stack_parent(world);
    const auto b  = make_stack_parent(world);
    const auto a1 = make_child(world, a, 0);
    make_child(world, b, 0);

    const auto kids = stack_children(world, a);
    REQUIRE(kids.size() == 1);
    REQUIRE(kids[0] == a1);
}

TEST_CASE("stack_children orders by authored order, not insertion", "[ui][layout]")
{
    // Inserted 30, 10, 20 and expected back 10, 20, 30. Insertion order is what `for_each`
    // would give, so this fails if the sort is dropped.
    World world;
    const auto p   = make_stack_parent(world);
    const auto c30 = make_child(world, p, 30);
    const auto c10 = make_child(world, p, 10);
    const auto c20 = make_child(world, p, 20);

    const auto kids = stack_children(world, p);
    REQUIRE(kids.size() == 3);
    REQUIRE(kids[0] == c10);
    REQUIRE(kids[1] == c20);
    REQUIRE(kids[2] == c30);
}

TEST_CASE("destroying an unrelated entity does not reorder a stack", "[ui][layout]")
{
    // THE TEST THIS TASK EXISTS FOR. ComponentRegistry::for_each visits "in dense order"
    // over a sparse set: removal swaps the last element into the hole. So destroying an
    // entity that merely HAPPENS to carry UiParent -- in a different panel, or in no panel
    // at all -- would reshuffle this stack if the order came from iteration.
    //
    // The failure is invisible to any test that builds a world and reads it once. It needs
    // a destroy BETWEEN two enumerations, which is why it is written this way.
    World world;
    const auto p            = make_stack_parent(world);
    const auto c1           = make_child(world, p, 10);
    // The stranger is inserted in the MIDDLE of this stack's children, deliberately. A
    // sparse set removes by swapping the LAST dense element into the hole, so destroying
    // the last-inserted entity merely pops and moves nothing -- a version of this test
    // that created the stranger last passed even with the sort deleted, which made its
    // own comment false. Verified by probe: with the stranger here and the sort removed,
    // iteration yields c1, c3, c2.
    const auto other_parent = make_stack_parent(world);
    const auto stranger     = make_child(world, other_parent, 0);
    const auto c2           = make_child(world, p, 20);
    const auto c3           = make_child(world, p, 30);

    const auto before = stack_children(world, p);
    world.destroy_entity(stranger);
    world.tick(0.0f); // destroy_entity is deferred; the tick applies it
    const auto after = stack_children(world, p);

    REQUIRE(before.size() == 3);
    REQUIRE(after.size() == 3);
    REQUIRE(after[0] == c1);
    REQUIRE(after[1] == c2);
    REQUIRE(after[2] == c3);
}

TEST_CASE("equal orders give a stable, reproducible sequence", "[ui][layout]")
{
    // Ties are LEGAL and often harmless, so they are not an error. What they must not be
    // is storage-dependent: two children with the same order must come back in the same
    // sequence every time, which the entity index provides as a deterministic tiebreak.
    // It is a tiebreak, not a feature -- nothing should author equal orders and rely on it.
    World world;
    const auto p = make_stack_parent(world);
    make_child(world, p, 5);
    make_child(world, p, 5);
    make_child(world, p, 5);

    const auto first  = stack_children(world, p);
    const auto second = stack_children(world, p);
    REQUIRE(first.size() == 3);
    REQUIRE(first == second);
}

TEST_CASE("a hidden child still occupies a slot", "[ui][layout]")
{
    // A deliberate choice rather than an oversight. A child with alpha 0 is skipped by the
    // draw pass but still COUNTS here, so hiding one element does not reflow the others.
    // The alternative -- counting only visible children -- would make a fade cause the
    // rest of the stack to jump, and would make layout depend on a render-time property.
    World world;
    const auto p = make_stack_parent(world);
    make_child(world, p, 10);
    const auto hidden = world.create_entity();
    UiPanel invisible;
    invisible.order = 20;
    invisible.alpha = 0.0f;
    world.add_component<UiPanel>(hidden, invisible);
    world.add_component<UiParent>(hidden, UiParent{p});
    make_child(world, p, 30);

    REQUIRE(stack_children(world, p).size() == 3);
}

TEST_CASE("child_layout_order prefers UiPanel, then UiImage, then UiText", "[ui][layout]")
{
    // A child may carry any of the three components that have an `order`, and the
    // precedence has to be FIXED rather than whichever is found first, or the same scene
    // would lay out differently depending on the order its components were added.
    //
    // UiPanel first because it is the only `order` the renderer actually sorts by -- see
    // ui_order_check.hpp, which records that UiImage::order and UiText::order are
    // reflected, documented and ignored by the draw path. Sorting by the number that
    // already means something keeps one concept rather than inventing a second.
    World world;
    const auto p = make_stack_parent(world);

    const auto panel_and_text = world.create_entity();
    UiPanel    pp;
    pp.order = 7;
    world.add_component<UiPanel>(panel_and_text, pp);
    mosaic::engine::ui::UiText t;
    t.order = 99;
    world.add_component<mosaic::engine::ui::UiText>(panel_and_text, t);
    world.add_component<UiParent>(panel_and_text, UiParent{p});
    REQUIRE(child_layout_order(world, panel_and_text) == 7);

    const auto text_only = world.create_entity();
    mosaic::engine::ui::UiText t2;
    t2.order = 42;
    world.add_component<mosaic::engine::ui::UiText>(text_only, t2);
    world.add_component<UiParent>(text_only, UiParent{p});
    REQUIRE(child_layout_order(world, text_only) == 42);

    const auto bare = world.create_entity();
    world.add_component<UiParent>(bare, UiParent{p});
    REQUIRE(child_layout_order(world, bare) == 0);
}

TEST_CASE("a stale parent handle captures nobody", "[ui][layout]")
{
    // EntityID is (generation << 32) | index, and the registry recycles indices. If sibling
    // matching compared index() rather than the whole handle, a recycled entity would
    // inherit the destroyed one's children -- a stack that silently adopts strangers.
    //
    // operator== compares all 64 bits, so this is free as long as comparison is used. The
    // test exists because using index() is the plausible mistake, not because the current
    // code makes it.
    World world;
    const auto doomed = make_stack_parent(world);
    make_child(world, doomed, 10);
    make_child(world, doomed, 20);
    world.destroy_entity(doomed);
    world.tick(0.0f);

    const auto recycled = make_stack_parent(world);
    INFO("recycled index " << recycled.index() << " vs doomed index " << doomed.index());
    REQUIRE(stack_children(world, recycled).empty());
}
