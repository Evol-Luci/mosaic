#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/ui/ui_button.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reference.hpp"
#include "foundation/serialization/reflect_value.hpp"

using Catch::Approx;
using mosaic::engine::ui::UiButton;
using mosaic::foundation::math::Vec2;

TEST_CASE("UiButton round-trips through reflection", "[ui][reflection]")
{
    UiButton b;
    b.anchor = 2; // TopRight
    b.pos    = Vec2{0.02f, 0.03f};
    b.size   = Vec2{0.25f, 0.06f};
    b.event  = "hud_click";

    const auto value = mosaic::foundation::serialization::to_value(b);
    UiButton back;
    mosaic::foundation::serialization::from_value(value, back);

    REQUIRE(back.anchor == 2);
    REQUIRE(back.pos.x == Approx(0.02f));
    REQUIRE(back.size.y == Approx(0.06f));
    REQUIRE(back.event == "hud_click");
}

TEST_CASE("UiButton is a registered built-in component", "[ui][reflection]")
{
    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    REQUIRE(types.has("UiButton"));
}
