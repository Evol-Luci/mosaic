#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/ui/ui_text.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reference.hpp"
#include "foundation/serialization/reflect_value.hpp"

using Catch::Approx;
using mosaic::engine::ui::UiText;
using mosaic::foundation::math::Vec2;
using mosaic::foundation::math::Vec3;
using mosaic::foundation::serialization::AssetID;

TEST_CASE("UiText round-trips through reflection", "[ui][reflection]")
{
    UiText t;
    t.font   = AssetID::from_string("00000000-0000-0000-0000-0000000000f7");
    t.text   = "Score: 10";
    t.anchor = 2; // TopRight
    t.pos    = Vec2{0.02f, 0.03f};
    t.height = 0.07f;
    t.color  = Vec3{0.9f, 0.8f, 0.1f};
    t.alpha  = 0.75f;
    t.order  = 3;

    const auto value = mosaic::foundation::serialization::to_value(t);
    UiText back;
    mosaic::foundation::serialization::from_value(value, back);

    REQUIRE(back.font == t.font);
    REQUIRE(back.text == "Score: 10");
    REQUIRE(back.anchor == 2);
    REQUIRE(back.pos.y == Approx(0.03f));
    REQUIRE(back.height == Approx(0.07f));
    REQUIRE(back.color.x == Approx(0.9f));
    REQUIRE(back.alpha == Approx(0.75f));
    REQUIRE(back.order == 3);
}

TEST_CASE("UiText is a registered built-in component", "[ui][reflection]")
{
    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    REQUIRE(types.has("UiText"));
}
