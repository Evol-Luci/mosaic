#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/ui/ui_panel.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reference.hpp"
#include "foundation/serialization/reflect_value.hpp"

using Catch::Approx;
using mosaic::engine::ui::UiPanel;
using mosaic::foundation::math::Vec2;
using mosaic::foundation::math::Vec3;

TEST_CASE("UiPanel round-trips through reflection", "[ui][reflection]")
{
    UiPanel p;
    p.anchor = 2;                 // TopRight
    p.pos    = Vec2{0.03f, 0.04f};
    p.size   = Vec2{0.25f, 0.06f};
    p.color  = Vec3{0.1f, 0.2f, 0.3f};
    p.alpha  = 0.6f;
    p.order  = 5;
    p.visible = false;

    const auto value = mosaic::foundation::serialization::to_value(p);
    UiPanel back;
    mosaic::foundation::serialization::from_value(value, back);

    REQUIRE(back.anchor == 2);
    REQUIRE(back.pos.x == Approx(0.03f));
    REQUIRE(back.size.y == Approx(0.06f));
    REQUIRE(back.color.z == Approx(0.3f));
    REQUIRE(back.alpha == Approx(0.6f));
    REQUIRE(back.order == 5);
    // `visible` defaults to TRUE, so a round-trip that dropped the field entirely would
    // come back `true` and this assertion is the one that notices.
    REQUIRE(back.visible == false);
}

TEST_CASE("UiPanel visible defaults to true, so every pre-existing scene is unchanged",
          "[ui][reflection]")
{
    REQUIRE(UiPanel{}.visible == true);
    // A scene file written before this field existed carries no "visible" key at all, and
    // ReadVisitor leaves an absent member untouched -- so such a scene decodes to the
    // DEFAULT, which has to be `true` or every shipped scene's HUD would vanish. The
    // alpha-bearing fields are asserted alongside it so a reordered reflect() that happened
    // to shift `visible` onto another key could not pass this.
    mosaic::foundation::serialization::Table t;
    t["alpha"] = mosaic::foundation::serialization::Value(0.25);
    UiPanel back;
    mosaic::foundation::serialization::from_value(
        mosaic::foundation::serialization::Value(t), back);
    REQUIRE(back.alpha == Approx(0.25f));
    REQUIRE(back.visible == true);
}

TEST_CASE("UiPanel is a registered built-in component", "[ui][reflection]")
{
    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    REQUIRE(types.has("UiPanel"));
}
