#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/vfx/screen_effect.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reflect_value.hpp"

using Catch::Approx;
using mosaic::engine::vfx::ScreenEffect;
using mosaic::foundation::math::Vec3;

TEST_CASE("ScreenEffect round-trips through reflection", "[vfx]")
{
    ScreenEffect s;
    s.tint          = Vec3{1.0f, 0.2f, 0.1f};
    s.tint_strength = 0.7f;
    s.vignette      = 0.3f;

    const auto value = mosaic::foundation::serialization::to_value(s);
    ScreenEffect back;
    mosaic::foundation::serialization::from_value(value, back);

    REQUIRE(back.tint.x == Approx(1.0f));
    REQUIRE(back.tint_strength == Approx(0.7f));
    REQUIRE(back.vignette == Approx(0.3f));
}

TEST_CASE("ScreenEffect is a registered built-in component", "[vfx]")
{
    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    REQUIRE(types.has("ScreenEffect"));
}
