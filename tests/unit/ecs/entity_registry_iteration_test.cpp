#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/ecs/entity_registry.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"

using mosaic::engine::World;
using mosaic::engine::ecs::EntityRegistry;
using mosaic::foundation::EntityID;

TEST_CASE("EntityRegistry::for_each visits every live entity", "[entity_registry]")
{
    EntityRegistry registry;
    const EntityID a = registry.create();
    const EntityID b = registry.create();
    const EntityID c = registry.create();
    registry.destroy(b);

    std::vector<EntityID> seen;
    registry.for_each([&](EntityID e) { seen.push_back(e); });

    REQUIRE(seen.size() == 2);
    REQUIRE(seen[0] == a);
    REQUIRE(seen[1] == c);
}

TEST_CASE("EntityRegistry::for_each on an empty registry visits nothing", "[entity_registry]")
{
    EntityRegistry registry;
    int calls = 0;
    registry.for_each([&](EntityID) { ++calls; });
    REQUIRE(calls == 0);
}

TEST_CASE("World::for_each_entity visits live entities including component-less ones", "[world]")
{
    World world;
    const EntityID a = world.create_entity();
    const EntityID b = world.create_entity(); // no components

    std::vector<EntityID> seen;
    world.for_each_entity([&](EntityID e) { seen.push_back(e); });

    REQUIRE(seen.size() == 2);
    REQUIRE(seen[0] == a);
    REQUIRE(seen[1] == b);
}
