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
using mosaic::engine::ui::button_anchor;
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

TEST_CASE("button_anchor clamps an out-of-range anchor to TopLeft", "[ui][buttonrects]")
{
    REQUIRE(button_anchor(0) == Anchor::TopLeft);
    REQUIRE(button_anchor(8) == Anchor::BottomRight);
    REQUIRE(button_anchor(9) == Anchor::TopLeft);
    REQUIRE(button_anchor(-1) == Anchor::TopLeft);
}

TEST_CASE("button_hit_candidates is empty with no buttons", "[ui][buttonrects]")
{
    World world;
    world.create_entity();
    REQUIRE(button_hit_candidates(world).empty());
}
