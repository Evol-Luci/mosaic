#include <catch2/catch_test_macros.hpp>

#include "engine/world/world.hpp"

using mosaic::engine::World;
using EntityID = mosaic::foundation::EntityID;

namespace
{
struct Position { float x, y; };
} // namespace

TEST_CASE("World creates valid entities and tracks the live count", "[world]")
{
    World world;
    REQUIRE(world.entity_count() == 0);

    const EntityID e = world.create_entity();
    REQUIRE(world.is_valid(e));
    REQUIRE(world.entity_count() == 1);
}

TEST_CASE("World delegates component add/get/has/remove", "[world]")
{
    World world;
    const EntityID e = world.create_entity();

    REQUIRE(world.add_component<Position>(e, Position{1.0f, 2.0f}));
    REQUIRE(world.has_component<Position>(e));
    REQUIRE(world.get_component<Position>(e) != nullptr);
    REQUIRE(world.get_component<Position>(e)->x == 1.0f);

    REQUIRE(world.remove_component<Position>(e));
    REQUIRE_FALSE(world.has_component<Position>(e));
    REQUIRE(world.get_component<Position>(e) == nullptr);
}

TEST_CASE("World::for_each visits entities holding the component", "[world]")
{
    World world;
    const EntityID a = world.create_entity();
    const EntityID b = world.create_entity();
    world.add_component<Position>(a, Position{1.0f, 1.0f});
    world.add_component<Position>(b, Position{2.0f, 2.0f});

    int count = 0;
    world.for_each<Position>([&](EntityID, Position&) { ++count; });
    REQUIRE(count == 2);
}
