#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/ui/ui_image.hpp"
#include "engine/ui/ui_hierarchy.hpp"
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

TEST_CASE("a child of a stack resolves within its slot", "[ui][layout]")
{
    // The hook, end to end. A full-screen parent with a vertical stack of two children:
    // the second child's frame is the bottom half.
    World world;
    const auto p = world.create_entity();
    UiPanel parent_panel;
    parent_panel.anchor = 0;            // TopLeft
    parent_panel.pos    = {0.0f, 0.0f};
    parent_panel.size   = {1.0f, 1.0f}; // the whole screen
    world.add_component<UiPanel>(p, parent_panel);
    world.add_component<UiStack>(p, UiStack{0, 0.0f, 0.0f});

    make_child(world, p, 10);
    const auto second = make_child(world, p, 20);

    const auto frame = mosaic::engine::ui::resolve_parent_frame(world, second);
    REQUIRE(frame.x == Approx(0.0f));
    REQUIRE(frame.y == Approx(0.5f));
    REQUIRE(frame.w == Approx(1.0f));
    REQUIRE(frame.h == Approx(0.5f));
}

TEST_CASE("a parent without a UiStack resolves exactly as before", "[ui][layout]")
{
    // THE REGRESSION GUARD every existing scene depends on. With no stack the frame must
    // be the parent's whole rect, identical to the pre-layout behaviour.
    World world;
    const auto p = world.create_entity();
    UiPanel parent_panel;
    parent_panel.pos  = {0.1f, 0.2f};
    parent_panel.size = {0.5f, 0.4f};
    world.add_component<UiPanel>(p, parent_panel);
    // deliberately NO UiStack
    const auto child = make_child(world, p, 0);

    const auto frame  = mosaic::engine::ui::resolve_parent_frame(world, child);
    const auto expect = mosaic::engine::ui::resolve_in(
        mosaic::engine::ui::Rect{0.0f, 0.0f, 1.0f, 1.0f},
        mosaic::engine::ui::to_anchor(parent_panel.anchor), parent_panel.pos,
        parent_panel.size);
    REQUIRE(frame.x == Approx(expect.x));
    REQUIRE(frame.y == Approx(expect.y));
    REQUIRE(frame.w == Approx(expect.w));
    REQUIRE(frame.h == Approx(expect.h));
}

TEST_CASE("a UiStack on an entity with no UiPanel lays nothing out", "[ui][layout]")
{
    // resolve_parent_frame already returns the whole SCREEN when the parent has no
    // UiPanel -- there is no rect to divide. Dividing the screen instead would silently
    // promote a child into a screen-wide layout, which looks like the parent was ignored
    // and is worse than doing nothing.
    World world;
    const auto p = world.create_entity();
    world.add_component<UiStack>(p, UiStack{0, 0.0f, 0.0f}); // no UiPanel
    const auto child = make_child(world, p, 0);

    const auto frame = mosaic::engine::ui::resolve_parent_frame(world, child);
    REQUIRE(frame.x == Approx(0.0f));
    REQUIRE(frame.y == Approx(0.0f));
    REQUIRE(frame.w == Approx(1.0f));
    REQUIRE(frame.h == Approx(1.0f));
}

TEST_CASE("the authored pos offsets a child from its slot", "[ui][layout]")
{
    // The decision the UI decomposition reserved for the human, and it needs no new rule:
    // narrowing the FRAME means resolve_in already treats pos as an offset within it.
    World world;
    const auto p = world.create_entity();
    UiPanel parent_panel;
    parent_panel.size = {1.0f, 1.0f};
    world.add_component<UiPanel>(p, parent_panel);
    world.add_component<UiStack>(p, UiStack{0, 0.0f, 0.0f});

    const auto on_slot = make_child(world, p, 10);
    const auto nudged  = make_child(world, p, 20);
    UiPanel*   np      = world.get_component<UiPanel>(nudged);
    np->pos  = {0.25f, 0.0f};
    np->size = {0.5f, 1.0f};

    const auto a = mosaic::engine::ui::resolve_ui_rect(
        world, on_slot, mosaic::engine::ui::to_anchor(0),
        mosaic::foundation::math::Vec2{0.0f, 0.0f},
        mosaic::foundation::math::Vec2{1.0f, 1.0f});
    REQUIRE(a.y == Approx(0.0f));
    REQUIRE(a.h == Approx(0.5f));

    const auto b = mosaic::engine::ui::resolve_ui_rect(
        world, nudged, mosaic::engine::ui::to_anchor(np->anchor), np->pos, np->size);
    // Slot is the bottom half; pos.x of 0.25 of the slot's width shifts it right.
    REQUIRE(b.x == Approx(0.25f));
    REQUIRE(b.y == Approx(0.5f));
    REQUIRE(b.w == Approx(0.5f));
}

TEST_CASE("text is laid out by the same hook", "[ui][layout]")
{
    // The case an implementer who hooked resolve_ui_rect would have shipped broken, with
    // no existing test failing: render_system.hpp calls resolve_parent_frame DIRECTLY for
    // UiText and hands the frame to layout_text. Panels would move and labels would not.
    World world;
    const auto p = world.create_entity();
    UiPanel parent_panel;
    parent_panel.size = {1.0f, 1.0f};
    world.add_component<UiPanel>(p, parent_panel);
    world.add_component<UiStack>(p, UiStack{0, 0.0f, 0.0f});

    make_child(world, p, 10);
    const auto label = world.create_entity();
    mosaic::engine::ui::UiText t;
    t.order = 20;
    world.add_component<mosaic::engine::ui::UiText>(label, t);
    world.add_component<UiParent>(label, UiParent{p});

    // Exactly the call the text pass makes.
    const auto frame = mosaic::engine::ui::resolve_parent_frame(world, label);
    REQUIRE(frame.y == Approx(0.5f));
    REQUIRE(frame.h == Approx(0.5f));
}

TEST_CASE("stacks nest", "[ui][layout]")
{
    // The slot computation sits INSIDE resolve_parent_frame's existing recursion, so the
    // kMaxDepth guard covers it; a slot computed alongside the recursion would re-enter
    // unbounded.
    //
    // Outer vertical stack of two, inner stack filling the second slot (bottom half) with
    // two children: the inner second child is the bottom quarter of the screen.
    World world;
    const auto outer = world.create_entity();
    UiPanel op;
    op.size = {1.0f, 1.0f};
    world.add_component<UiPanel>(outer, op);
    world.add_component<UiStack>(outer, UiStack{0, 0.0f, 0.0f});

    make_child(world, outer, 10);
    const auto inner = make_child(world, outer, 20);
    UiPanel*   ip    = world.get_component<UiPanel>(inner);
    ip->pos  = {0.0f, 0.0f};
    ip->size = {1.0f, 1.0f}; // fills its slot
    world.add_component<UiStack>(inner, UiStack{0, 0.0f, 0.0f});

    make_child(world, inner, 10);
    const auto deep = make_child(world, inner, 20);

    const auto frame = mosaic::engine::ui::resolve_parent_frame(world, deep);
    REQUIRE(frame.y == Approx(0.75f));
    REQUIRE(frame.h == Approx(0.25f));
}

TEST_CASE("inherited_alpha multiplies down the ancestor chain", "[ui][layout]")
{
    // Fading a container fades everything in it. Three deep, 0.5 * 0.5 = 0.25 for the
    // grandchild -- the ANCESTORS' product, not including the element's own alpha, which
    // the render pass multiplies in separately.
    World world;
    const auto a = world.create_entity();
    UiPanel pa; pa.alpha = 0.5f;
    world.add_component<UiPanel>(a, pa);

    const auto b = world.create_entity();
    UiPanel pb; pb.alpha = 0.5f;
    world.add_component<UiPanel>(b, pb);
    world.add_component<UiParent>(b, UiParent{a});

    const auto c = world.create_entity();
    UiPanel pc; pc.alpha = 1.0f;
    world.add_component<UiPanel>(c, pc);
    world.add_component<UiParent>(c, UiParent{b});

    REQUIRE(mosaic::engine::ui::inherited_alpha(world, a) == Approx(1.0f));
    REQUIRE(mosaic::engine::ui::inherited_alpha(world, b) == Approx(0.5f));
    REQUIRE(mosaic::engine::ui::inherited_alpha(world, c) == Approx(0.25f));
}

TEST_CASE("inherited_alpha is 1 with no parent", "[ui][layout]")
{
    // The state of every element in every scene authored before this feature -- so this is
    // the regression guard for inherited alpha, and 1.0 is the only value that leaves them
    // unchanged.
    World world;
    const auto e = world.create_entity();
    UiPanel p; p.alpha = 0.3f;
    world.add_component<UiPanel>(e, p);
    REQUIRE(mosaic::engine::ui::inherited_alpha(world, e) == Approx(1.0f));
}

TEST_CASE("inherited_alpha skips a parent with no UiPanel", "[ui][layout]")
{
    // UiParent may name any entity. An ancestor without a UiPanel has no alpha to
    // contribute, and resolve_parent_frame already treats that case as "no frame" -- so
    // treating it as alpha 0 would hide children for a reason nobody authored.
    World world;
    const auto plain = world.create_entity(); // no UiPanel
    const auto child = world.create_entity();
    UiPanel p; p.alpha = 1.0f;
    world.add_component<UiPanel>(child, p);
    world.add_component<UiParent>(child, UiParent{plain});
    REQUIRE(mosaic::engine::ui::inherited_alpha(world, child) == Approx(1.0f));
}

TEST_CASE("inherited_alpha does not write to the authored alphas", "[ui][layout]")
{
    // The discipline resolve_appearance states: "nothing writes to p.color, so the
    // authored value survives and a scene saved while hovering carries no hover tint." A
    // scene saved while a container is mid-fade must carry the authored alphas, not the
    // product.
    World world;
    const auto a = world.create_entity();
    UiPanel pa; pa.alpha = 0.5f;
    world.add_component<UiPanel>(a, pa);
    const auto b = world.create_entity();
    UiPanel pb; pb.alpha = 0.5f;
    world.add_component<UiPanel>(b, pb);
    world.add_component<UiParent>(b, UiParent{a});

    (void)mosaic::engine::ui::inherited_alpha(world, b);
    REQUIRE(world.get_component<UiPanel>(a)->alpha == Approx(0.5f));
    REQUIRE(world.get_component<UiPanel>(b)->alpha == Approx(0.5f));
}

TEST_CASE("a negative container alpha cannot make a child's alpha negative", "[ui][layout]")
{
    // UiPanel::alpha is an unbounded reflected float on an inspector DragFloat with no
    // clamp anywhere in the UI or the device path, so a negative is one drag away. The
    // container itself is hidden by the authored `alpha <= 0` early-out -- but it would
    // multiply each child to a NEGATIVE alpha, which reaches the blend equation as
    // 2*dst - src: an over-bright halo rather than a hidden child. Brighter than not
    // fading at all, from a value that asked for less.
    //
    // ui_stack.hpp states the matching discipline one file over ("NEVER returns a negative
    // extent ... rather than into nothing at all"); this is the alpha half of it.
    World world;
    const auto container = world.create_entity();
    UiPanel bad;
    bad.alpha = -0.5f;
    world.add_component<UiPanel>(container, bad);

    const auto child = world.create_entity();
    UiPanel c;
    c.alpha = 1.0f;
    world.add_component<UiPanel>(child, c);
    world.add_component<UiParent>(child, UiParent{container});

    REQUIRE(mosaic::engine::ui::inherited_alpha(world, child) >= 0.0f);
    REQUIRE(mosaic::engine::ui::inherited_alpha(world, child) == Approx(0.0f));
}

TEST_CASE("a child anchors within its slot for every anchor band", "[ui][layout]")
{
    // THE SPEC CLAIMED THIS WAS TESTED AND IT WAS NOT -- every other layout case here uses
    // a TopLeft child, so Center, Right and Bottom bands were asserted nowhere. The
    // behaviour is correct (resolve_in is an affine map of the unit square onto whatever
    // frame it is handed, and resolve_axis never refers to the screen), but this is
    // exactly the per-band arithmetic the project has had triplicated and divergent
    // before, so the gate should hold it.
    //
    // The property: for every band, resolving inside a slot equals resolving inside the
    // same rect passed directly as a frame. That is what "layout narrows the frame" MEANS,
    // and it is band-independent by construction.
    World world;
    const auto p = world.create_entity();
    UiPanel parent_panel;
    parent_panel.size = {1.0f, 1.0f};
    world.add_component<UiPanel>(p, parent_panel);
    world.add_component<UiStack>(p, UiStack{0, 0.0f, 0.0f});

    make_child(world, p, 10);
    const auto second = make_child(world, p, 20);

    const auto slot = mosaic::engine::ui::resolve_parent_frame(world, second);
    const mosaic::foundation::math::Vec2 pos{0.1f, 0.1f};
    const mosaic::foundation::math::Vec2 size{0.2f, 0.3f};

    for (int a = 0; a <= static_cast<int>(mosaic::engine::ui::Anchor::BottomRight); ++a)
    {
        const auto anchor = mosaic::engine::ui::to_anchor(a);
        const auto via_layout = mosaic::engine::ui::resolve_ui_rect(world, second, anchor,
                                                                    pos, size);
        const auto direct = mosaic::engine::ui::resolve_in(slot, anchor, pos, size);
        INFO("anchor band " << a);
        REQUIRE(via_layout.x == Approx(direct.x));
        REQUIRE(via_layout.y == Approx(direct.y));
        REQUIRE(via_layout.w == Approx(direct.w));
        REQUIRE(via_layout.h == Approx(direct.h));
        // And it stays inside its slot, which is the containment claim.
        REQUIRE(via_layout.x >= Approx(slot.x));
        REQUIRE(via_layout.y >= Approx(slot.y));
        REQUIRE(via_layout.x + via_layout.w <= Approx(slot.x + slot.w));
        REQUIRE(via_layout.y + via_layout.h <= Approx(slot.y + slot.h));
    }
}

// ---------------------------------------------------------------------------
// U8b: `visible` affects DRAWING ONLY. These three cases pin the properties the
// draw-path change must NOT acquire, so they live beside layout rather than beside the
// renderer.
// ---------------------------------------------------------------------------

TEST_CASE("an invisible container still narrows its children's frames", "[ui][layout]")
{
    // THE SHARP TRAP OF THIS SLICE, and the reason a layout-only container is possible at
    // all. `visible = false` must NOT make resolve_parent_frame treat the panel as ABSENT:
    // that function returns the whole SCREEN {0,0,1,1} for a parent with no UiPanel, so an
    // invisible container treated as absent would resolve every child against the screen
    // instead of against its slot. That is the exact opposite of what the flag is for, it
    // would silently break U8a's shipped demo stack, and NOTHING ELSE IN THE SUITE WOULD
    // NOTICE -- the children would still be drawn, just in the wrong places.
    //
    // Asserted as EQUALITY WITH THE VISIBLE CASE rather than against hand-computed numbers,
    // so it cannot drift if layout_slot's arithmetic ever changes: the claim is "identical",
    // and that is what is written down.
    World      world;
    const auto make = [](World& w, bool container_visible)
    {
        const auto p = w.create_entity();
        UiPanel    parent_panel;
        parent_panel.anchor  = 0;
        parent_panel.pos     = {0.1f, 0.2f};
        parent_panel.size    = {0.6f, 0.8f};
        parent_panel.visible = container_visible;
        w.add_component<UiPanel>(p, parent_panel);
        w.add_component<UiStack>(p, UiStack{0, 0.05f, 0.02f});
        std::vector<World::EntityID> kids;
        for (int i = 0; i < 3; ++i) { kids.push_back(make_child(w, p, 10 * i)); }
        return kids;
    };

    World                        shown_world;
    const std::vector<World::EntityID> shown  = make(shown_world, true);
    World                        hidden_world;
    const std::vector<World::EntityID> hidden = make(hidden_world, false);

    REQUIRE(shown.size() == 3);
    REQUIRE(hidden.size() == 3);
    for (std::size_t i = 0; i < 3; ++i)
    {
        const auto a = mosaic::engine::ui::resolve_parent_frame(shown_world, shown[i]);
        const auto b = mosaic::engine::ui::resolve_parent_frame(hidden_world, hidden[i]);
        // Guards against the degenerate pass where BOTH collapsed to the screen rect: a
        // real slot of a 0.6x0.8 parent split three ways is never the full screen.
        REQUIRE(a.h < 0.5f);
        REQUIRE(b.x == Approx(a.x));
        REQUIRE(b.y == Approx(a.y));
        REQUIRE(b.w == Approx(a.w));
        REQUIRE(b.h == Approx(a.h));
    }
}

TEST_CASE("visible does not join the inherited-alpha product", "[ui][layout]")
{
    // Decision 4 of the slice, and the thing that makes a layout-only container usable: if
    // `visible = false` contributed 0 to the product, every child of the container would be
    // hidden too -- which is precisely the alpha-0 trap the flag exists to escape.
    //
    // So the AUTHORED WAY to write a layout-only container is `alpha = 1, visible = false`:
    // alpha 1 is the product's identity, and `visible` hides only this one component.
    World      world;
    const auto container = world.create_entity();
    UiPanel    outer;
    outer.alpha   = 1.0f;
    outer.visible = false;
    world.add_component<UiPanel>(container, outer);

    const auto child = make_child(world, container, 0);
    REQUIRE(mosaic::engine::ui::inherited_alpha(world, child) == Approx(1.0f));

    // And the other direction: an invisible container with an authored alpha below 1 STILL
    // fades its children, because the two fields are independent.
    UiPanel* mut = world.get_component<UiPanel>(container);
    REQUIRE(mut != nullptr);
    mut->alpha = 0.5f;
    REQUIRE(mosaic::engine::ui::inherited_alpha(world, child) == Approx(0.5f));
}

TEST_CASE("an invisible child still occupies its slot", "[ui][layout]")
{
    // The companion to "a hidden child still occupies a slot" above, which made the same
    // claim for alpha. Layout must not come to depend on a render-time property in EITHER
    // spelling, or hiding one element would reflow all the others.
    World      world;
    const auto p = make_stack_parent(world);
    const auto a = make_child(world, p, 10);
    const auto b = make_child(world, p, 20);
    UiPanel*   pa = world.get_component<UiPanel>(a);
    REQUIRE(pa != nullptr);
    pa->visible = false;

    REQUIRE(stack_children(world, p).size() == 2);
    // make_stack_parent gives the container a DEFAULT UiPanel, whose rect is {0,0,0.2,0.1}
    // -- so two vertical slots are 0.05 tall and the second starts at y = 0.05. Expressed
    // against the visible sibling's slot as well, so the numbers cannot be read as magic.
    const auto fa = mosaic::engine::ui::resolve_parent_frame(world, a);
    const auto fb = mosaic::engine::ui::resolve_parent_frame(world, b);
    REQUIRE(fa.y == Approx(0.0f));
    REQUIRE(fb.y == Approx(0.05f)); // still the SECOND of two slots, not promoted to the first
    REQUIRE(fb.h == Approx(0.05f));
    REQUIRE(fb.h == Approx(fa.h));  // and the two slots are still equal
}
