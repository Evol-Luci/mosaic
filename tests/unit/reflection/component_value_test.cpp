#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reflect_value.hpp"
#include "foundation/serialization/value.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::transform::Transform;
using mosaic::foundation::math::Vec3;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::foundation::serialization::to_value;
using mosaic::foundation::serialization::Value;

namespace
{
ComponentReflection make_types(TypeRegistry& r) { ComponentReflection t; register_builtin_components(r, t); return t; }
} // namespace

TEST_CASE("component_value returns the live component as a Value", "[reflection]")
{
    TypeRegistry reg; ComponentReflection types = make_types(reg);
    World world;
    auto e = world.create_entity();
    Transform t{}; t.position = Vec3{1, 2, 3};
    world.add_component<Transform>(e, t);

    auto v = types.component_value(world, e, "Transform");
    REQUIRE(v.has_value());
    REQUIRE(v->is<mosaic::foundation::serialization::Table>()); // a component serializes to a table
    // Unknown / absent -> nullopt.
    REQUIRE_FALSE(types.component_value(world, e, "Nope").has_value());
    auto e2 = world.create_entity();
    REQUIRE_FALSE(types.component_value(world, e2, "Transform").has_value()); // e2 has no Transform
}

TEST_CASE("set_value updates an existing component in place", "[reflection]")
{
    TypeRegistry reg; ComponentReflection types = make_types(reg);
    World world;
    auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});

    Transform desired{}; desired.position = Vec3{7, 8, 9};
    REQUIRE(types.set_value(world, e, "Transform", to_value(desired)));
    REQUIRE(world.get_component<Transform>(e)->position.x == 7.0f);
    REQUIRE(world.get_component<Transform>(e)->position.z == 9.0f);
}

TEST_CASE("set_value adds the component when absent, and rejects unknown/bad input", "[reflection]")
{
    TypeRegistry reg; ComponentReflection types = make_types(reg);
    World world;
    auto e = world.create_entity(); // no Transform yet

    Transform desired{}; desired.position = Vec3{4, 0, 0};
    REQUIRE(types.set_value(world, e, "Transform", to_value(desired))); // adds it
    REQUIRE(world.has_component<Transform>(e));
    REQUIRE(world.get_component<Transform>(e)->position.x == 4.0f);

    REQUIRE_FALSE(types.set_value(world, e, "NotAType", to_value(desired))); // unregistered -> false

    // bad value shape -> from_value throws SerializationError -> caught -> false, never throws
    REQUIRE_FALSE(types.set_value(world, e, "Transform",
                                  Value{static_cast<std::int64_t>(42)}));
    REQUIRE(world.get_component<Transform>(e)->position.x == 4.0f); // component unchanged after bad input
}

TEST_CASE("remove_value removes a component; false when absent or unregistered", "[reflection]")
{
    TypeRegistry reg; ComponentReflection types = make_types(reg);
    World world;
    auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});

    REQUIRE(types.remove_value(world, e, "Transform"));
    REQUIRE_FALSE(world.has_component<Transform>(e));
    REQUIRE_FALSE(types.remove_value(world, e, "Transform")); // already gone
    REQUIRE_FALSE(types.remove_value(world, e, "Nope"));      // unregistered
}
