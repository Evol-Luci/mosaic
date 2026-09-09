#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/ui/ui_image.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reference.hpp"
#include "foundation/serialization/reflect_value.hpp"

using Catch::Approx;
using mosaic::engine::ui::UiImage;
using mosaic::foundation::math::Vec2;
using mosaic::foundation::math::Vec3;
using mosaic::foundation::serialization::AssetID;

TEST_CASE("UiImage round-trips through reflection", "[ui][reflection]")
{
    UiImage im;
    im.texture = AssetID::from_string("00000000-0000-0000-0000-0000000000e3");
    im.anchor  = 2; // TopRight
    im.pos     = Vec2{0.02f, 0.03f};
    im.size    = Vec2{0.12f, 0.12f};
    im.tint    = Vec3{0.8f, 0.9f, 1.0f};
    im.alpha   = 0.85f;
    im.order   = 4;

    const auto value = mosaic::foundation::serialization::to_value(im);
    UiImage back;
    mosaic::foundation::serialization::from_value(value, back);

    REQUIRE(back.texture == im.texture);
    REQUIRE(back.anchor == 2);
    REQUIRE(back.pos.x == Approx(0.02f));
    REQUIRE(back.size.y == Approx(0.12f));
    REQUIRE(back.tint.z == Approx(1.0f));
    REQUIRE(back.alpha == Approx(0.85f));
    REQUIRE(back.order == 4);
}

TEST_CASE("UiImage is a registered built-in component", "[ui][reflection]")
{
    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    REQUIRE(types.has("UiImage"));
}
