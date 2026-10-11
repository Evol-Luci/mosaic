#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/ui/ui_button.hpp"
#include "engine/ui/ui_image.hpp"
#include "engine/ui/ui_order_check.hpp"
#include "engine/ui/ui_panel.hpp"
#include "engine/ui/ui_text.hpp"
#include "engine/world/world.hpp"

using mosaic::engine::World;
using mosaic::engine::ui::find_order_disagreements;
using mosaic::engine::ui::UiButton;
using mosaic::engine::ui::UiImage;
using mosaic::engine::ui::UiPanel;
using mosaic::engine::ui::UiText;

namespace
{
// Adds a button and a panel to one entity with the given orders -- the shape every case here
// is a variation of.
mosaic::foundation::EntityID button_with_panel(World& world, int button_order, int panel_order)
{
    const auto e = world.create_entity();
    UiButton   b;
    b.order = button_order;
    world.add_component<UiButton>(e, b);
    UiPanel p;
    p.order = panel_order;
    world.add_component<UiPanel>(e, p);
    return e;
}
} // namespace

TEST_CASE("a button and a panel with different orders on one entity is reported", "[ui][order]")
{
    // The authoring slip this lint exists for: the panel's order was edited to 2 and the
    // button's was left at 5.
    World      world;
    const auto e = button_with_panel(world, 5, 2);

    const auto found = find_order_disagreements(world);

    REQUIRE(found.size() == 1);
    REQUIRE(found[0].e == e);
    // Both orders are carried, and they are NOT interchangeable: a finding that swapped them
    // would tell a user to change the wrong number.
    REQUIRE(found[0].button_order == 5);
    REQUIRE(found[0].panel_order == 2);
}

TEST_CASE("matching orders are not reported", "[ui][order]")
{
    // The state every scene in the repository is in today, including the shipped demo. This
    // must stay silent, or the lint is noise and a user learns to ignore it.
    World world;
    button_with_panel(world, 3, 3);

    REQUIRE(find_order_disagreements(world).empty());
}

TEST_CASE("a button with no panel on its entity is not reported", "[ui][order]")
{
    // A bare hit region is legitimate -- and it is also the shape of the blind spot: a
    // button overlaying a panel on a DIFFERENT entity looks exactly like this. Silent by
    // design, not by accident.
    World      world;
    const auto e = world.create_entity();
    UiButton   b;
    b.order = 6;
    world.add_component<UiButton>(e, b);

    REQUIRE(find_order_disagreements(world).empty());
}

TEST_CASE("a panel with no button is not reported", "[ui][order]")
{
    // Almost every UI entity in a scene is this. Only buttons are compared, because only
    // UiButton is hit-testable.
    World      world;
    const auto e = world.create_entity();
    UiPanel    p;
    p.order = 1;
    world.add_component<UiPanel>(e, p);

    REQUIRE(find_order_disagreements(world).empty());
}

TEST_CASE("a disagreeing image or text on the entity is not reported", "[ui][order]")
{
    // THE REGRESSION GUARD FOR THE BUG THIS SLICE SHIPPED AND THEN REMOVED. An earlier
    // version compared the button against UiImage and UiText too, and fired on the shipped
    // demo's camera entity.
    //
    // It is not a disagreement, because the three HUD passes in render_system.hpp are fixed
    // and sequential -- panels, then images over panels, then text over both -- and ONLY the
    // panel pass sorts. UiImage::order and UiText::order are ignored by the renderer
    // entirely, so a button's order cannot disagree with a number that orders nothing.
    //
    // This is also the demo's exact shape: a panel that agrees, alongside an image and a text
    // that do not.
    World      world;
    const auto e = button_with_panel(world, 0, 0);
    UiImage    i;
    i.order = 4;
    world.add_component<UiImage>(e, i);
    UiText t;
    t.order = 1;
    world.add_component<UiText>(e, t);

    REQUIRE(find_order_disagreements(world).empty());
}

TEST_CASE("an agreeing entity is not reported alongside a disagreeing one", "[ui][order]")
{
    // Isolation: the finding names the entity that actually disagreed. With one entity in
    // the world an implementation that reported the wrong id still passes the first case.
    World world;
    button_with_panel(world, 1, 1);
    const auto bad = button_with_panel(world, 1, 2);

    const auto found = find_order_disagreements(world);

    REQUIRE(found.size() == 1);
    REQUIRE(found[0].e == bad);
}

TEST_CASE("a negative button order disagreeing with zero is reported", "[ui][order]")
{
    // `order` is signed precisely so a scene can author -1 to push a button behind its
    // neighbours (see ui_button.hpp).
    World world;
    button_with_panel(world, -1, 0);

    const auto found = find_order_disagreements(world);

    REQUIRE(found.size() == 1);
    REQUIRE(found[0].button_order == -1);
    REQUIRE(found[0].panel_order == 0);
}

TEST_CASE("orders that differ only in sign are reported", "[ui][order]")
{
    // -2 against 2: EQUAL in magnitude, opposite in z. This is the case a comparison written
    // on magnitudes gets wrong, and it is the worst possible disagreement to miss -- the
    // button is as far in FRONT of its panel as the panel is behind it.
    //
    // Found by a mutation probe: swapping the comparison for one on std::abs left the suite
    // green, because the other negative case (-1 against 0) has unequal magnitudes and so is
    // reported either way.
    World world;
    button_with_panel(world, -2, 2);

    const auto found = find_order_disagreements(world);

    REQUIRE(found.size() == 1);
    REQUIRE(found[0].button_order == -2);
    REQUIRE(found[0].panel_order == 2);
}
