#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/component_reflection.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reflect_value.hpp"
#include "foundation/serialization/value.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::foundation::serialization::Value;
using mosaic::foundation::serialization::to_value;
using EntityID = World::EntityID;

namespace
{
struct Position
{
    float x;
    float y;
};
template <class V> void reflect(V& visitor, Position& p)
{
    visitor.member("x", p.x);
    visitor.member("y", p.y);
}
} // namespace

TEST_CASE("construct attaches a component with no concrete type at the call site", "[reflection]")
{
    TypeRegistry registry;
    ComponentReflection reflector;
    reflector.register_component<Position>(registry, "Position");

    World world;
    const EntityID e = world.create_entity();

    const Value value = to_value(Position{3.0f, 4.0f});
    REQUIRE(reflector.construct(world, e, "Position", value));

    Position* p = world.get_component<Position>(e);
    REQUIRE(p != nullptr);
    REQUIRE(p->x == 3.0f);
    REQUIRE(p->y == 4.0f);
}

TEST_CASE("construct with an unregistered name returns false and attaches nothing", "[reflection]")
{
    TypeRegistry registry;
    ComponentReflection reflector;
    reflector.register_component<Position>(registry, "Position");

    World world;
    const EntityID e = world.create_entity();
    const Value value = to_value(Position{1.0f, 1.0f});

    REQUIRE_FALSE(reflector.construct(world, e, "Unknown", value));
    REQUIRE(world.get_component<Position>(e) == nullptr);
}

TEST_CASE("construct onto an invalid entity returns false", "[reflection]")
{
    TypeRegistry registry;
    ComponentReflection reflector;
    reflector.register_component<Position>(registry, "Position");

    World world;
    const EntityID e = world.create_entity();
    world.destroy_entity(e);
    world.tick(0.0f); // commit destruction -> e is now invalid

    const Value value = to_value(Position{1.0f, 1.0f});
    REQUIRE_FALSE(reflector.construct(world, e, "Position", value));
}
