#include <catch2/catch_test_macros.hpp>

#include "engine/commands/set_component_command.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reflect_value.hpp"
#include "foundation/serialization/value.hpp"

using mosaic::engine::World;
using mosaic::engine::commands::SetComponentCommand;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::transform::Transform;
using mosaic::foundation::math::Vec3;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::foundation::serialization::to_value;
using mosaic::foundation::serialization::Value;

TEST_CASE("SetComponentCommand executes then undoes to the prior value", "[commands]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    Transform t{}; t.position = Vec3{1, 2, 3};
    world.add_component<Transform>(e, t);

    Transform desired{}; desired.position = Vec3{9, 2, 3};
    SetComponentCommand cmd(world, types, e, "Transform", to_value(desired));
    cmd.execute();
    REQUIRE(cmd.valid());
    REQUIRE(world.get_component<Transform>(e)->position.x == 9.0f);
    cmd.undo();
    REQUIRE(world.get_component<Transform>(e)->position.x == 1.0f); // prior restored
}

TEST_CASE("SetComponentCommand undo of an add exactly removes the component", "[commands]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity(); // no Transform yet

    SetComponentCommand cmd(world, types, e, "Transform", to_value(Transform{}));
    cmd.execute();
    REQUIRE(cmd.valid());
    REQUIRE(world.has_component<Transform>(e)); // added
    cmd.undo();
    REQUIRE_FALSE(world.has_component<Transform>(e)); // removed, not left at default
}

TEST_CASE("SetComponentCommand is invalid for an unregistered component", "[commands]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();

    SetComponentCommand cmd(world, types, e, "Nope", Value{});
    cmd.execute();
    REQUIRE_FALSE(cmd.valid()); // set_value on an unregistered name -> false
}
