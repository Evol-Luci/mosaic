#include <catch2/catch_test_macros.hpp>

#include "engine/ecs/system.hpp"
#include "engine/world/world.hpp"

using mosaic::engine::World;
using mosaic::engine::ecs::System;
using EntityID = mosaic::foundation::EntityID;

namespace
{
struct Position { float x, y; };

// Destroys every entity holding a Position during its update — the classic
// "destroy while iterating" case that deferred destruction must make safe.
class Reaper final : public System
{
public:
    void update(World& world, float) override
    {
        world.for_each<Position>([&](EntityID id, Position&) { world.destroy_entity(id); });
    }
};
} // namespace

TEST_CASE("destroy_entity is deferred until the next tick commits it", "[world]")
{
    World world;
    const EntityID e = world.create_entity();
    world.add_component<Position>(e, Position{0.0f, 0.0f});

    world.destroy_entity(e);
    // Still present until the tick commits.
    REQUIRE(world.is_valid(e));
    REQUIRE(world.has_component<Position>(e));

    world.tick(0.0f);
    REQUIRE_FALSE(world.is_valid(e));
    REQUIRE(world.entity_count() == 0);
}

TEST_CASE("destroying during for_each is safe; commit removes all after systems run", "[world]")
{
    World world;
    const EntityID a = world.create_entity();
    const EntityID b = world.create_entity();
    world.add_component<Position>(a, Position{0.0f, 0.0f});
    world.add_component<Position>(b, Position{0.0f, 0.0f});
    world.add_system<Reaper>();

    world.tick(0.0f);
    REQUIRE(world.entity_count() == 0);
}

TEST_CASE("destroy_entity on an already-destroyed handle is a harmless no-op", "[world]")
{
    World world;
    const EntityID e = world.create_entity();
    world.destroy_entity(e);
    world.tick(0.0f);
    REQUIRE(world.entity_count() == 0);

    world.destroy_entity(e);     // stale handle
    world.tick(0.0f);            // must not throw or corrupt state
    REQUIRE(world.entity_count() == 0);
}
