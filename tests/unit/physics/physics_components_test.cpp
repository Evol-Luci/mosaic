#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/physics/physics_components.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reflect_value.hpp"

using Catch::Approx;
using mosaic::engine::physics::Collider;
using mosaic::engine::physics::RigidBody;

TEST_CASE("physics component defaults", "[physics]")
{
    RigidBody rb;
    REQUIRE(rb.dynamic == true);
    REQUIRE(rb.density == Approx(1.0f));

    Collider col;
    REQUIRE(col.half_extents.x == Approx(0.5f));
}

TEST_CASE("RigidBody and Collider round-trip through reflection", "[physics]")
{
    RigidBody rb{false, 2.5f};
    RigidBody rb_out;
    mosaic::foundation::serialization::from_value(
        mosaic::foundation::serialization::to_value(rb), rb_out);
    REQUIRE(rb_out.dynamic == false);
    REQUIRE(rb_out.density == Approx(2.5f));

    Collider col{mosaic::foundation::math::Vec3{2.0f, 3.0f, 4.0f}};
    Collider col_out;
    mosaic::foundation::serialization::from_value(
        mosaic::foundation::serialization::to_value(col), col_out);
    REQUIRE(col_out.half_extents.y == Approx(3.0f));
}

TEST_CASE("physics components are registered as builtins", "[physics]")
{
    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    REQUIRE(types.has("RigidBody"));
    REQUIRE(types.has("Collider"));
}
