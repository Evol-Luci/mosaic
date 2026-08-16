#include <catch2/catch_test_macros.hpp>

#include "engine/world/world.hpp"

using mosaic::engine::World;
using EntityID = mosaic::foundation::EntityID;

namespace
{
struct Position { float x, y; };
struct Velocity { float dx, dy; };
} // namespace

TEST_CASE("interleaved create/add/destroy/recycle across ticks", "[world]")
{
    World world;

    const EntityID a = world.create_entity();
    world.add_component<Position>(a, Position{1.0f, 0.0f});
    const EntityID b = world.create_entity();
    world.add_component<Velocity>(b, Velocity{0.0f, 1.0f});
    REQUIRE(world.entity_count() == 2);

    // Destroy a and commit the destruction.
    world.destroy_entity(a);
    world.tick(0.0f);
    REQUIRE_FALSE(world.is_valid(a));            // stale handle rejects
    REQUIRE(world.entity_count() == 1);
    REQUIRE_FALSE(world.has_component<Position>(a));
    REQUIRE(world.is_valid(b));                  // untouched entity survives
    REQUIRE(world.get_component<Velocity>(b)->dy == 1.0f);

    // Create a new entity — it recycles a's freed slot with a bumped generation.
    const EntityID c = world.create_entity();
    REQUIRE(c.index() == a.index());             // slot recycled
    REQUIRE(c.generation() != a.generation());   // generation advanced
    REQUIRE(world.is_valid(c));
    REQUIRE_FALSE(world.is_valid(a));            // old handle still stale post-recycle

    // c's components are its own; a's old Position did not leak in.
    REQUIRE_FALSE(world.has_component<Position>(c));
    REQUIRE(world.add_component<Position>(c, Position{9.0f, 9.0f}));
    REQUIRE(world.get_component<Position>(c)->x == 9.0f);
    REQUIRE_FALSE(world.has_component<Position>(a));
    REQUIRE(world.entity_count() == 2);
}
