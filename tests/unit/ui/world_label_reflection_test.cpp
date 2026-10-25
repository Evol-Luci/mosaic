#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/ui/world_label.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reference.hpp"
#include "foundation/serialization/reflect_value.hpp"

using Catch::Approx;
using mosaic::engine::ui::WorldLabel;
using mosaic::foundation::math::Vec3;
using mosaic::foundation::serialization::AssetID;

TEST_CASE("WorldLabel round-trips through reflection", "[ui][reflection]")
{
    WorldLabel l;
    l.font   = AssetID::from_string("00000000-0000-0000-0000-0000000000e5");
    l.text   = "Orbiter";
    l.size   = 0.5f;
    l.color  = Vec3{0.2f, 0.9f, 0.3f};
    l.alpha  = 0.8f;
    l.offset = Vec3{0.0f, 1.5f, 0.0f};
    // Non-default: `visible` defaults to true, so a round-trip that dropped the field
    // would come back true and assert nothing.
    l.visible = false;

    const auto value = mosaic::foundation::serialization::to_value(l);
    WorldLabel back;
    mosaic::foundation::serialization::from_value(value, back);

    REQUIRE(back.font == l.font);
    REQUIRE(back.text == "Orbiter");
    REQUIRE(back.size == Approx(0.5f));
    REQUIRE(back.color.y == Approx(0.9f));
    REQUIRE(back.alpha == Approx(0.8f));
    REQUIRE(back.offset.y == Approx(1.5f));
    REQUIRE(back.visible == false);
}

TEST_CASE("WorldLabel is a registered built-in component", "[ui][reflection]")
{
    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    REQUIRE(types.has("WorldLabel"));
}
