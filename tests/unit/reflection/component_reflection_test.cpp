#include <cstdint>
#include <map>
#include <string>
#include <vector>

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

namespace
{
struct Inner
{
    int a;
};
template <class V> void reflect(V& visitor, Inner& i)
{
    visitor.member("a", i.a);
}

struct Complex
{
    Inner inner;
    std::vector<int> values;
    std::string label;
};
template <class V> void reflect(V& visitor, Complex& c)
{
    visitor.member("inner", c.inner);
    visitor.member("values", c.values);
    visitor.member("label", c.label);
}
} // namespace

TEST_CASE("round-trip: reflect -> to_value -> construct reproduces the component", "[reflection]")
{
    TypeRegistry registry;
    ComponentReflection reflector;
    reflector.register_component<Complex>(registry, "Complex");

    World world;
    const EntityID e = world.create_entity();

    Complex original{Inner{7}, {1, 2, 3}, "hp"};
    const Value value = to_value(original);
    REQUIRE(reflector.construct(world, e, "Complex", value));

    Complex* got = world.get_component<Complex>(e);
    REQUIRE(got != nullptr);
    REQUIRE(got->inner.a == 7);
    REQUIRE(got->values == std::vector<int>{1, 2, 3});
    REQUIRE(got->label == "hp");
}

TEST_CASE("ComponentReflection::save emits (entity, name, value) per component", "[reflection]")
{
    TypeRegistry registry;
    ComponentReflection reflector;
    reflector.register_component<Position>(registry, "Position");

    World world;
    const EntityID a = world.create_entity();
    const EntityID b = world.create_entity();
    world.add_component<Position>(a, Position{1.0f, 2.0f});
    world.add_component<Position>(b, Position{3.0f, 4.0f});

    std::map<std::uint64_t, std::string> names;
    int count = 0;
    reflector.save(world, [&](EntityID e, const std::string& name, const Value&)
    {
        names[e.value()] = name;
        ++count;
    });

    REQUIRE(count == 2);
    REQUIRE(names[a.value()] == "Position");
    REQUIRE(names[b.value()] == "Position");
}

TEST_CASE("ComponentReflection::has reports registered type names", "[reflection]")
{
    TypeRegistry registry;
    ComponentReflection reflector;
    reflector.register_component<Position>(registry, "Position");

    REQUIRE(reflector.has("Position"));
    REQUIRE_FALSE(reflector.has("Nope"));
}
