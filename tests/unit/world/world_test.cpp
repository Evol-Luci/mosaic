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

TEST_CASE("World::clear synchronously destroys every entity and its components", "[world]")
{
    World world;
    const EntityID a = world.create_entity();
    const EntityID b = world.create_entity();
    world.add_component<Position>(a, Position{1.0f, 1.0f});
    world.add_component<Position>(b, Position{2.0f, 2.0f});
    REQUIRE(world.entity_count() == 2);

    world.clear();

    REQUIRE(world.entity_count() == 0);
    REQUIRE_FALSE(world.is_valid(a));
    REQUIRE_FALSE(world.is_valid(b));
    int count = 0;
    world.for_each<Position>([&](EntityID, Position&) { ++count; });
    REQUIRE(count == 0);
}

TEST_CASE("World::clear takes effect immediately, unlike deferred destroy_entity", "[world]")
{
    World world;
    const EntityID e = world.create_entity();
    world.clear();
    REQUIRE(world.entity_count() == 0); // no tick() needed, unlike destroy_entity
    (void)e;
}

TEST_CASE("World::clear leaves no lingering effect from a not-yet-flushed destroy_entity",
          "[world]")
{
    // Generational ids mean a stale pending_destroy_ entry could never actually
    // collide with a later entity (is_valid() on the old id fails once its slot's
    // generation is bumped) -- this just confirms clear() drops it anyway (tidy
    // state, not a crash-prevention requirement) and a fresh entity ticks fine.
    World world;
    const EntityID e = world.create_entity();
    world.destroy_entity(e); // deferred, not yet flushed
    world.clear();

    const EntityID fresh = world.create_entity();
    world.tick(0.0f);
    REQUIRE(world.is_valid(fresh));
}
