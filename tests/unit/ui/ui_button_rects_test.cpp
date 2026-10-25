#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/ui/ui_button.hpp"
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
