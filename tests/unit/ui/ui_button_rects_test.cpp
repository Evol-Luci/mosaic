#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/ui/ui_button.hpp"
#include "engine/ui/ui_clip_rect.hpp"
#include "engine/ui/ui_hit.hpp"
#include "engine/ui/pointer.hpp"
#include "engine/ui/ui_button_rects.hpp"
#include "engine/ui/ui_panel.hpp"
#include "engine/ui/ui_parent.hpp"
#include "engine/world/world.hpp"

using Catch::Approx;
using mosaic::engine::World;
using mosaic::engine::ui::Anchor;
using mosaic::engine::ui::button_hit_candidates;
using mosaic::engine::ui::UiButton;
using mosaic::engine::ui::UiPanel;
using mosaic::engine::ui::UiParent;

TEST_CASE("button_hit_candidates carries each button's entity order and rect", "[ui][buttonrects]")
{
    // The shape UiSystem's press and hover paths both consume. If this drifts from what
    // UiSystem used to build, every hit test in the engine moves with it.
    World      world;
    const auto e = world.create_entity();
    UiButton   b;
    b.anchor = static_cast<int>(Anchor::TopLeft);
    b.pos    = {0.25f, 0.5f};
    b.size   = {0.1f, 0.2f};
    b.order  = 4;
    world.add_component<UiButton>(e, b);

    const auto cands = button_hit_candidates(world);

    REQUIRE(cands.size() == 1);
    REQUIRE(cands[0].e == e);
    REQUIRE(cands[0].order == 4);
    REQUIRE(cands[0].rect.x == Approx(0.25f));
    REQUIRE(cands[0].rect.y == Approx(0.5f));
    REQUIRE(cands[0].rect.w == Approx(0.1f));
    REQUIRE(cands[0].rect.h == Approx(0.2f));
}

TEST_CASE("button_hit_candidates resolves a nested button inside its parent frame",
          "[ui][buttonrects]")
{
    // Proves the flatten goes through resolve_ui_rect rather than resolve_rect: a child of
    // a half-screen panel occupies half the space its own numbers describe.
    World      world;
    const auto parent = world.create_entity();
    UiPanel    p;
    p.anchor = static_cast<int>(Anchor::TopLeft);
    p.pos    = {0.0f, 0.0f};
    p.size   = {0.5f, 0.5f};
    world.add_component<UiPanel>(parent, p);

    const auto child = world.create_entity();
    UiButton   b;
    b.anchor = static_cast<int>(Anchor::TopLeft);
    b.pos    = {0.2f, 0.2f};
    b.size   = {0.4f, 0.4f};
    world.add_component<UiButton>(child, b);
    world.add_component<UiParent>(child, UiParent{parent});

    const auto cands = button_hit_candidates(world);

    REQUIRE(cands.size() == 1);
    REQUIRE(cands[0].rect.x == Approx(0.1f)); // 0.2 * 0.5
    REQUIRE(cands[0].rect.w == Approx(0.2f)); // 0.4 * 0.5
}

TEST_CASE("button_hit_candidates is empty with no buttons", "[ui][buttonrects]")
{
    World world;
    world.create_entity();
    REQUIRE(button_hit_candidates(world).empty());
}

TEST_CASE("button_hit_candidates skips a UiButton with visible false", "[ui][buttonrects]")
{
    // GATE OBSERVATION 2. `visible = false` on a UiButton means NON-INTERACTIVE, and this
    // is the single place that has to enforce it: button_hit_candidates is the one flatten
    // the press path, the hover path AND the order-conflict diagnostic all consume, so a
    // button absent from here cannot be clicked, hovered or dragged by any of them.
    //
    // Two buttons rather than one, so a passing run has to show the filter is SELECTIVE
    // rather than that the function returned nothing.
    World      world;
    const auto hidden = world.create_entity();
    UiButton   h;
    h.pos     = {0.0f, 0.0f};
    h.size    = {0.2f, 0.2f};
    h.visible = false;
    world.add_component<UiButton>(hidden, h);

    const auto shown = world.create_entity();
    UiButton   s;
    s.pos  = {0.5f, 0.5f};
    s.size = {0.2f, 0.2f};
    world.add_component<UiButton>(shown, s);

    const auto cands = button_hit_candidates(world);
    REQUIRE(cands.size() == 1);
    REQUIRE(cands[0].e == shown);
}

TEST_CASE("an invisible UiPanel leaves its entity's UiButton hit-testable", "[ui][buttonrects]")
{
    // GATE OBSERVATION 3, and THE ASSERTION THAT FAILS IF SOMEONE COUPLES THE TWO FIELDS.
    //
    // It is tempting to infer: "the panel is hidden, so the button on the same entity must
    // be dead." That would build the feature on a convention rather than on an invariant.
    // app/run/demo_scene.hpp piles the unrelated "MOSAIC" title and the checker UiImage onto
    // the `camera` entity purely for convenience, so "one entity is one widget" is a habit
    // this repository happens to keep, not something the data enforces -- and an inference
    // across components would break the first time someone authored against it.
    //
    // Hiding a button AND its visual therefore means setting two fields. That is the honest
    // cost, and this case is what holds the engine to it.
    World      world;
    const auto e = world.create_entity();
    UiPanel    p;
    p.pos     = {0.1f, 0.1f};
    p.size    = {0.3f, 0.3f};
    p.visible = false;
    world.add_component<UiPanel>(e, p);
    UiButton b;
    b.anchor  = static_cast<int>(Anchor::TopLeft);
    b.pos     = {0.1f, 0.1f};
    b.size    = {0.3f, 0.3f};
    b.visible = true;
    world.add_component<UiButton>(e, b);

    const auto cands = button_hit_candidates(world);
    REQUIRE(cands.size() == 1);
    REQUIRE(cands[0].e == e);
    // The rect too, not merely the count: an implementation that kept the candidate but
    // zeroed its rect would hit-test nothing and still satisfy a count-only assertion.
    REQUIRE(cands[0].rect.x == Approx(0.1f));
    REQUIRE(cands[0].rect.y == Approx(0.1f));
    REQUIRE(cands[0].rect.w == Approx(0.3f));
    REQUIRE(cands[0].rect.h == Approx(0.3f));
}

TEST_CASE("an invisible UiButton does not disable the UiPanel on its entity",
          "[ui][buttonrects]")
{
    // The other direction of the same no-inference rule, which the gate does not name but
    // which a coupling would break just as badly: turning a hit region off must not blank
    // the visual it sits under. Asserted on the authored data, because the panel's own
    // `visible` is what the draw pass reads.
    World      world;
    const auto e = world.create_entity();
    UiPanel    p;
    world.add_component<UiPanel>(e, p);
    UiButton   b;
    b.visible = false;
    world.add_component<UiButton>(e, b);

    REQUIRE(button_hit_candidates(world).empty());
    const UiPanel* back = world.get_component<UiPanel>(e);
    REQUIRE(back != nullptr);
    REQUIRE(back->visible == true);
}

// ---------------------------------------------------------------------------
// U9b: an ancestor's UiClipRect reaches the hit test, in the same slice that it reaches the
// draw. These cases are the HIT half of the slice's evidence and they can regress
// independently of the draw half -- which is the whole reason the two halves ship together:
// a clip that hid a button's pixels but left it clickable is the U8b bug class, a dead-
// looking control that still answers, and that one was found by a human looking at a
// screenshot rather than by a test.
// ---------------------------------------------------------------------------

TEST_CASE("a button wholly outside its clip produces NO hit candidate", "[ui][buttonrects][clip]")
{
    // ABSENT, not merely smaller. The distinction is the one this file already makes for
    // `visible`: a candidate that exists can win topmost_hit and swallow a press that
    // belongs to whatever is beneath it.
    World      world;
    const auto parent = world.create_entity();
    UiPanel    p;
    p.anchor = static_cast<int>(Anchor::TopLeft);
    p.pos    = {0.0f, 0.0f};
    p.size   = {0.5f, 1.0f};        // the left half of the screen
    world.add_component<UiPanel>(parent, p);
    world.add_component<mosaic::engine::ui::UiClipRect>(parent,
                                                        mosaic::engine::ui::UiClipRect{});

    const auto child = world.create_entity();
    UiButton   b;
    b.anchor = static_cast<int>(Anchor::TopLeft);
    b.pos    = {1.2f, 0.0f};        // 1.2 of a half-screen frame -> x starts at 0.6
    b.size   = {0.2f, 0.2f};        // entirely right of the clip's x = 0.5 edge
    world.add_component<UiButton>(child, b);
    world.add_component<UiParent>(child, UiParent{parent});

    // The rect it WOULD have had, so "absent" is not confused with "never resolved".
    const auto unclipped =
        mosaic::engine::ui::resolve_ui_rect(world, child, Anchor::TopLeft, b.pos, b.size);
    REQUIRE(unclipped.x == Approx(0.6f));
    REQUIRE(unclipped.w == Approx(0.1f));

    REQUIRE(button_hit_candidates(world).empty());
}

TEST_CASE("a button partly outside its clip reports only the surviving rect",
          "[ui][buttonrects][clip]")
{
    World      world;
    const auto parent = world.create_entity();
    UiPanel    p;
    p.anchor = static_cast<int>(Anchor::TopLeft);
    p.pos    = {0.0f, 0.0f};
    p.size   = {0.5f, 1.0f};        // clip: x in [0.0, 0.5)
    world.add_component<UiPanel>(parent, p);
    world.add_component<mosaic::engine::ui::UiClipRect>(parent,
                                                        mosaic::engine::ui::UiClipRect{});

    const auto child = world.create_entity();
    UiButton   b;
    b.anchor = static_cast<int>(Anchor::TopLeft);
    b.pos    = {0.8f, 0.0f};        // -> x from 0.4
    b.size   = {0.8f, 0.5f};        // -> w 0.4, so x in [0.4, 0.8): half of it is outside
    world.add_component<UiButton>(child, b);
    world.add_component<UiParent>(child, UiParent{parent});

    const auto cands = button_hit_candidates(world);
    REQUIRE(cands.size() == 1);
    REQUIRE(cands[0].rect.x == Approx(0.4f));
    REQUIRE(cands[0].rect.w == Approx(0.1f));  // trimmed at the clip edge, not 0.4
    REQUIRE(cands[0].rect.h == Approx(0.5f));  // the unclipped axis is untouched
}

TEST_CASE("a button fully inside its clip keeps exactly its unclipped rect",
          "[ui][buttonrects][clip]")
{
    // The no-op direction. A clip must change NEITHER position nor size of what survives
    // it, so a contained button's rect has to come back bit-identical -- otherwise every
    // button in a clipped region would shift by whatever the intersection rounded to.
    World      world;
    const auto parent = world.create_entity();
    UiPanel    p;
    p.anchor = static_cast<int>(Anchor::TopLeft);
    p.pos    = {0.1f, 0.1f};
    p.size   = {0.8f, 0.8f};
    world.add_component<UiPanel>(parent, p);
    world.add_component<mosaic::engine::ui::UiClipRect>(parent,
                                                        mosaic::engine::ui::UiClipRect{});

    const auto child = world.create_entity();
    UiButton   b;
    b.anchor = static_cast<int>(Anchor::TopLeft);
    b.pos    = {0.25f, 0.25f};
    b.size   = {0.25f, 0.25f};
    world.add_component<UiButton>(child, b);
    world.add_component<UiParent>(child, UiParent{parent});

    const auto expected =
        mosaic::engine::ui::resolve_ui_rect(world, child, Anchor::TopLeft, b.pos, b.size);
    const auto cands = button_hit_candidates(world);
    REQUIRE(cands.size() == 1);
    REQUIRE(cands[0].rect.x == expected.x);
    REQUIRE(cands[0].rect.y == expected.y);
    REQUIRE(cands[0].rect.w == expected.w);
    REQUIRE(cands[0].rect.h == expected.h);
}

TEST_CASE("the hit test intersects NESTED clips rather than taking the innermost",
          "[ui][buttonrects][clip]")
{
    // The hit half of U9b's decisive case, asserted here and not only through resolve_clip,
    // because the draw path and the hit path are separate call sites and can regress
    // independently.
    //
    // outer clips to the left half, x in [0.0, 0.5). inner is a child of outer authored to
    // reach past it: pos {0.5,0} size {1,1} of outer's frame -> x in [0.25, 0.75). The
    // intersection is x in [0.25, 0.5).
    //
    // The button sits at x in [0.5, 0.625) -- INSIDE inner's own rect but OUTSIDE the
    // intersection. "Use the innermost clip" keeps it as a candidate; intersecting drops it.
    World      world;
    const auto outer = world.create_entity();
    UiPanel    op;
    op.anchor = static_cast<int>(Anchor::TopLeft);
    op.pos    = {0.0f, 0.0f};
    op.size   = {0.5f, 1.0f};
    world.add_component<UiPanel>(outer, op);
    world.add_component<mosaic::engine::ui::UiClipRect>(outer,
                                                         mosaic::engine::ui::UiClipRect{});

    const auto inner = world.create_entity();
    UiPanel    ip;
    ip.anchor = static_cast<int>(Anchor::TopLeft);
    ip.pos    = {0.5f, 0.0f};
    ip.size   = {1.0f, 1.0f};
    world.add_component<UiPanel>(inner, ip);
    world.add_component<UiParent>(inner, UiParent{outer});
    world.add_component<mosaic::engine::ui::UiClipRect>(inner,
                                                         mosaic::engine::ui::UiClipRect{});

    const auto child = world.create_entity();
    UiButton   b;
    b.anchor = static_cast<int>(Anchor::TopLeft);
    b.pos    = {0.5f, 0.0f};        // inner's frame is x in [0.25,0.75) w 0.5 -> x = 0.5
    b.size   = {0.25f, 0.5f};       // -> w 0.125, so x in [0.5, 0.625)
    world.add_component<UiButton>(child, b);
    world.add_component<UiParent>(child, UiParent{inner});

    // The button is where the comment says, so the drop below is for the stated reason.
    const auto unclipped =
        mosaic::engine::ui::resolve_ui_rect(world, child, Anchor::TopLeft, b.pos, b.size);
    REQUIRE(unclipped.x == Approx(0.5f));
    REQUIRE(unclipped.w == Approx(0.125f));
    // ...and it IS inside inner's own rect, so the innermost-only rule would keep it.
    REQUIRE(unclipped.x >= 0.25f);
    REQUIRE(unclipped.x + unclipped.w <= 0.75f);

    REQUIRE(button_hit_candidates(world).empty());
}

TEST_CASE("a clipped-away button lets the press reach what is beneath it",
          "[ui][buttonrects][clip]")
{
    // ABSENT, not silent, demonstrated through the thing that consumes the list. The
    // clipped-away button has the HIGHER order, so if it were still a candidate it would win
    // topmost_hit and swallow the press. Emitting no candidate is what makes the click pass
    // through.
    World      world;

    const auto under = world.create_entity();
    UiButton   u;
    u.anchor = static_cast<int>(Anchor::TopLeft);
    u.pos    = {0.6f, 0.0f};
    u.size   = {0.2f, 0.2f};
    u.order  = 0;
    world.add_component<UiButton>(under, u);

    const auto parent = world.create_entity();
    UiPanel    p;
    p.anchor = static_cast<int>(Anchor::TopLeft);
    p.pos    = {0.0f, 0.0f};
    p.size   = {0.5f, 1.0f};        // clip: the left half
    world.add_component<UiPanel>(parent, p);
    world.add_component<mosaic::engine::ui::UiClipRect>(parent,
                                                        mosaic::engine::ui::UiClipRect{});

    const auto over = world.create_entity();
    UiButton   o;
    o.anchor = static_cast<int>(Anchor::TopLeft);
    o.pos    = {1.3f, 0.0f};        // 1.3 of a half-screen frame -> x = 0.65
    o.size   = {0.2f, 0.4f};        // -> w 0.1, so x in [0.65, 0.75): over `under`
    o.order  = 5;                   // and ON TOP of it
    world.add_component<UiButton>(over, o);
    world.add_component<UiParent>(over, UiParent{parent});

    const auto cands = button_hit_candidates(world);
    REQUIRE(cands.size() == 1);
    REQUIRE(cands[0].e == under);

    mosaic::engine::ui::PointerState pointer;
    pointer.inside = true;
    pointer.x      = 0.7f;
    pointer.y      = 0.1f;
    REQUIRE(mosaic::engine::ui::topmost_hit(cands, pointer) == under);
}

TEST_CASE("an unclipped button is untouched by a clip elsewhere in the world",
          "[ui][buttonrects][clip]")
{
    // The containment claim: UiClipRect clips its SUBTREE, so a button that is not a
    // descendant of it must come back exactly as before. Without this, a single authored
    // clip region could quietly crop the whole HUD.
    World      world;
    const auto clipper = world.create_entity();
    UiPanel    p;
    p.anchor = static_cast<int>(Anchor::TopLeft);
    p.pos    = {0.0f, 0.0f};
    p.size   = {0.1f, 0.1f};
    world.add_component<UiPanel>(clipper, p);
    world.add_component<mosaic::engine::ui::UiClipRect>(clipper,
                                                         mosaic::engine::ui::UiClipRect{});

    const auto loose = world.create_entity();
    UiButton   b;
    b.anchor = static_cast<int>(Anchor::TopLeft);
    b.pos    = {0.8f, 0.8f};
    b.size   = {0.1f, 0.1f};
    world.add_component<UiButton>(loose, b);

    const auto cands = button_hit_candidates(world);
    REQUIRE(cands.size() == 1);
    REQUIRE(cands[0].rect.x == Approx(0.8f));
    REQUIRE(cands[0].rect.w == Approx(0.1f));
}
