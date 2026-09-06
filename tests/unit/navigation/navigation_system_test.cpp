#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/navigation/nav_agent.hpp"
#include "engine/navigation/nav_grid.hpp"
#include "engine/navigation/navigation_system.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"

using Catch::Approx;
using mosaic::engine::World;
using mosaic::engine::navigation::NavAgent;
using mosaic::engine::navigation::NavGrid;
using mosaic::engine::navigation::NavigationSystem;
using mosaic::engine::transform::Transform;

TEST_CASE("NavigationSystem steers an agent to its goal on open ground", "[navigation]")
{
    NavGrid grid(6, 3); // declared before `world` so it outlives the system's reference
    World world;
    world.add_system<NavigationSystem>(grid);

    const auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});               // starts at cell (0,0)
    world.add_component<NavAgent>(e, NavAgent{4, 0, 10.0f, true}); // goal cell (4,0)

    for (int i = 0; i < 30; ++i) { world.tick(0.1f); }
    auto* tf = world.get_component<Transform>(e);
    REQUIRE(tf->position.x == Approx(4.0).margin(0.01));
    REQUIRE(tf->position.z == Approx(0.0).margin(0.01));
}

TEST_CASE("NavigationSystem routes an agent around an obstacle", "[navigation]")
{
    NavGrid grid(5, 5);
    for (int y = 0; y < 4; ++y) { grid.block(2, y); } // wall on column x=2, gap at (2,4)
    World world;
    world.add_system<NavigationSystem>(grid);

    const auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    world.add_component<NavAgent>(e, NavAgent{4, 0, 10.0f, true});

    for (int i = 0; i < 100; ++i) { world.tick(0.1f); }
    auto* tf = world.get_component<Transform>(e);
    REQUIRE(tf->position.x == Approx(4.0).margin(0.01)); // reached the goal despite the wall
    REQUIRE(tf->position.z == Approx(0.0).margin(0.01));
}

TEST_CASE("an inactive NavAgent does not move", "[navigation]")
{
    NavGrid grid(6, 3);
    World world;
    world.add_system<NavigationSystem>(grid);
    const auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    world.add_component<NavAgent>(e, NavAgent{4, 0, 10.0f, false}); // inactive
    for (int i = 0; i < 10; ++i) { world.tick(0.1f); }
    REQUIRE(world.get_component<Transform>(e)->position.x == Approx(0.0));
}
