#include <algorithm>
#include <catch2/catch_test_macros.hpp>

#include "engine/ecs/component_type_id.hpp"
#include "engine/ecs/system.hpp"

using mosaic::engine::ecs::ComponentAccess;
using mosaic::engine::ecs::ComponentTypeID;
using mosaic::engine::ecs::System;
using mosaic::engine::ecs::component_type_id;

namespace
{
struct Position { float x, y; };
struct Velocity { float dx, dy; };

// A system that reads Position and writes Velocity.
class MovementSystem final : public System
{
public:
    MovementSystem()
    {
        access_.reads<Position>().writes<Velocity>();
    }
};

bool contains(const std::vector<ComponentTypeID>& set, ComponentTypeID id)
{
    return std::find(set.begin(), set.end(), id) != set.end();
}
} // namespace

TEST_CASE("a system reports exactly the component access it declared", "[system]")
{
    MovementSystem system;
    const ComponentAccess& access = system.access();

    REQUIRE(contains(access.read_set(), component_type_id<Position>()));
    REQUIRE_FALSE(contains(access.read_set(), component_type_id<Velocity>()));

    REQUIRE(contains(access.write_set(), component_type_id<Velocity>()));
    REQUIRE_FALSE(contains(access.write_set(), component_type_id<Position>()));
}
