#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/vfx/decal.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reference.hpp"
#include "foundation/serialization/reflect_value.hpp"

using Catch::Approx;
using mosaic::engine::vfx::Decal;
using mosaic::foundation::math::Vec3;

TEST_CASE("Decal round-trips through reflection", "[vfx]")
{
    Decal d;
    d.texture = mosaic::foundation::serialization::AssetID::from_string(
        "00000000-0000-0000-0000-0000000000d1");
    d.color = Vec3{0.8f, 0.2f, 0.1f};
    d.alpha = 0.6f;

    const auto value = mosaic::foundation::serialization::to_value(d);
    Decal back;
    mosaic::foundation::serialization::from_value(value, back);

    REQUIRE(back.texture == d.texture);
    REQUIRE(back.color.x == Approx(0.8f));
    REQUIRE(back.alpha == Approx(0.6f));
}

TEST_CASE("Decal is a registered built-in component", "[vfx]")
{
    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    REQUIRE(types.has("Decal"));
}
