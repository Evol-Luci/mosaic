#include <cstdint>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/component_reflection.hpp"
#include "engine/scene/scene.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/value.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::foundation::serialization::Array;
using mosaic::foundation::serialization::SerializationError;
using mosaic::foundation::serialization::Table;
using mosaic::foundation::serialization::Value;
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

struct Link
{
    EntityID target;
};
template <class V> void reflect(V& visitor, Link& l)
{
    visitor.member("target", l.target);
}

ComponentReflection make_types(TypeRegistry& registry)
{
    ComponentReflection types;
    types.register_component<Position>(registry, "Position");
    types.register_component<Link>(registry, "Link");
    return types;
}
} // namespace

TEST_CASE("round-trip preserves the entity/component graph, including empties", "[scene]")
{
    TypeRegistry registry;
    ComponentReflection types = make_types(registry);

    World src;
    const EntityID a = src.create_entity();
    src.add_component<Position>(a, Position{1.0f, 2.0f});
    src.create_entity(); // component-less
    const EntityID c = src.create_entity();
    src.add_component<Position>(c, Position{3.0f, 4.0f});
    src.add_component<Link>(c, Link{a});

    const Value doc = mosaic::engine::scene::to_value(src, types);

    World dst;
    mosaic::engine::scene::from_value(doc, dst, types);

    REQUIRE(dst.entity_count() == 3);
    int positions = 0;
    dst.for_each<Position>([&](EntityID, Position&) { ++positions; });
    REQUIRE(positions == 2);
    int links = 0;
    dst.for_each<Link>([&](EntityID, Link&) { ++links; });
    REQUIRE(links == 1);
}

TEST_CASE("intra-scene EntityID references remap to the loaded handles", "[scene]")
{
    TypeRegistry registry;
    ComponentReflection types = make_types(registry);

    // Bump a's generation so its handle differs from a fresh sequential alloc,
    // making the remap non-trivial.
    World src;
    const EntityID scratch = src.create_entity();
    src.destroy_entity(scratch);
    src.tick(0.0f); // slot recycled next
    const EntityID a = src.create_entity(); // slot 0, generation 2
    src.add_component<Position>(a, Position{7.0f, 0.0f});
    const EntityID c = src.create_entity();
    src.add_component<Link>(c, Link{a});
    REQUIRE(a.generation() == 2u);

    const Value doc = mosaic::engine::scene::to_value(src, types);

    World dst;
    mosaic::engine::scene::from_value(doc, dst, types);

    EntityID loaded_target{};
    dst.for_each<Link>([&](EntityID, Link& l) { loaded_target = l.target; });

    REQUIRE(dst.is_valid(loaded_target)); // remapped to a live handle
    REQUIRE(loaded_target != a);          // not the stale source handle
    const Position* p = dst.get_component<Position>(loaded_target);
    REQUIRE(p != nullptr);                // points at the entity that was `a`
    REQUIRE(p->x == 7.0f);
}

TEST_CASE("a reference to an entity outside the scene resolves to null", "[scene]")
{
    TypeRegistry registry;
    ComponentReflection types = make_types(registry);

    // Hand-build a scene: one entity "1" with Link.target = "999" (absent).
    Table link;
    link["target"] = Value(std::string("999"));
    Table comps;
    comps["Link"] = Value(std::move(link));
    Table node;
    node["id"] = Value(std::string("1"));
    node["components"] = Value(std::move(comps));
    Array entities;
    entities.push_back(Value(std::move(node)));
    Table root;
    root["version"] = Value(std::int64_t{1});
    root["entities"] = Value(std::move(entities));

    World dst;
    mosaic::engine::scene::from_value(Value(std::move(root)), dst, types);

    EntityID loaded_target{EntityID{1u, 1u}}; // sentinel non-null
    dst.for_each<Link>([&](EntityID, Link& l) { loaded_target = l.target; });
    REQUIRE(loaded_target == EntityID(0));
}

TEST_CASE("an unknown component type name aborts the load", "[scene]")
{
    TypeRegistry registry;
    ComponentReflection types = make_types(registry);

    Table ghost;
    ghost["x"] = Value(1.0);
    Table comps;
    comps["Ghost"] = Value(std::move(ghost));
    Table node;
    node["id"] = Value(std::string("1"));
    node["components"] = Value(std::move(comps));
    Array entities;
    entities.push_back(Value(std::move(node)));
    Table root;
    root["version"] = Value(std::int64_t{1});
    root["entities"] = Value(std::move(entities));

    World dst;
    REQUIRE_THROWS_AS(mosaic::engine::scene::from_value(Value(std::move(root)), dst, types),
                      SerializationError);
}

TEST_CASE("malformed JSON aborts the load", "[scene]")
{
    TypeRegistry registry;
    ComponentReflection types = make_types(registry);
    World dst;
    REQUIRE_THROWS_AS(mosaic::engine::scene::load_from_json("{ not json", dst, types),
                      SerializationError);
}

TEST_CASE("save_to_json / load_from_json round-trips the graph", "[scene]")
{
    TypeRegistry registry;
    ComponentReflection types = make_types(registry);

    World src;
    const EntityID a = src.create_entity();
    src.add_component<Position>(a, Position{1.0f, 2.0f});
    const EntityID c = src.create_entity();
    src.add_component<Link>(c, Link{a});

    const std::string json = mosaic::engine::scene::save_to_json(src, types);

    World dst;
    mosaic::engine::scene::load_from_json(json, dst, types);
    REQUIRE(dst.entity_count() == 2);
    int links = 0;
    dst.for_each<Link>([&](EntityID, Link&) { ++links; });
    REQUIRE(links == 1);
}
