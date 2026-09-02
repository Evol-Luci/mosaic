#include <catch2/catch_test_macros.hpp>

#include "engine/physics/physics_components.hpp"
#include "engine/script/script_component.hpp"
#include "engine/world/world.hpp"
#include "studio/demo_physics.hpp"

using mosaic::engine::World;
using mosaic::engine::physics::RigidBody;
using mosaic::engine::script::ScriptComponent;
using mosaic::studio::set_demo_physics;

TEST_CASE("set_demo_physics swaps the demo object to physics-driven", "[studio]")
{
    World world;
    auto e = world.create_entity();
    world.add_component<ScriptComponent>(e, ScriptComponent{"src"});

    set_demo_physics(world, e, "src", true);
    REQUIRE(world.has_component<RigidBody>(e));
    REQUIRE_FALSE(world.has_component<ScriptComponent>(e));
}

TEST_CASE("set_demo_physics restores the demo object to script-driven", "[studio]")
{
    World world;
    auto e = world.create_entity();
    world.add_component<RigidBody>(e, RigidBody{true, 1.0f});

    set_demo_physics(world, e, "orbit-source", false);
    REQUIRE_FALSE(world.has_component<RigidBody>(e));
    REQUIRE(world.has_component<ScriptComponent>(e));
    REQUIRE(world.get_component<ScriptComponent>(e)->source == "orbit-source");
}
