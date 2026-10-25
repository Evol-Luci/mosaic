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
    // Non-default on purpose: a field left at its default round-trips whether or not it
    // is serialised at all, so a default would assert nothing.
    im.sub_rect_pos  = Vec2{32.0f, 64.0f};
    im.sub_rect_size = Vec2{16.0f, 8.0f};
    im.fit_mode      = 2; // Fill
    // Non-default: `visible` defaults to true, so a round-trip that dropped the field
    // would come back true and assert nothing.
    im.visible       = false;

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
    REQUIRE(back.sub_rect_pos.x == Approx(32.0f));
    REQUIRE(back.sub_rect_pos.y == Approx(64.0f));
    REQUIRE(back.sub_rect_size.x == Approx(16.0f));
    REQUIRE(back.sub_rect_size.y == Approx(8.0f));
    REQUIRE(back.fit_mode == 2);
    REQUIRE(back.visible == false);
}

TEST_CASE("UiImage is a registered built-in component", "[ui][reflection]")
{
    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    REQUIRE(types.has("UiImage"));
}
