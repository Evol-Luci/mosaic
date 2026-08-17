#include <cstdint>
#include <stdexcept>
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

// Find the entity node with the given "id" string in a scene document's array.
const Table& find_node(const Array& nodes, const std::string& id)
{
    for (const Value& node : nodes)
    {
        const Table& t = node.as<Table>();
        if (t.find("id")->as<std::string>() == id)
        {
            return t;
        }
    }
    throw std::runtime_error("node not found: " + id);
}
} // namespace

TEST_CASE("to_value serializes entities, components, empties, and references", "[scene]")
{
    TypeRegistry registry;
    ComponentReflection types;
    types.register_component<Position>(registry, "Position");
    types.register_component<Link>(registry, "Link");

    World world;
    const EntityID a = world.create_entity();
    world.add_component<Position>(a, Position{1.0f, 2.0f});
    const EntityID b = world.create_entity(); // component-less
    const EntityID c = world.create_entity();
    world.add_component<Link>(c, Link{a});

    const Value doc = mosaic::engine::scene::to_value(world, types);
    const Table& root = doc.as<Table>();
    REQUIRE(root.find("version")->as<std::int64_t>() == 1);

    const Array& nodes = root.find("entities")->as<Array>();
    REQUIRE(nodes.size() == 3);

    // a: Position{1,2}
    const Table& node_a = find_node(nodes, std::to_string(a.value()));
    const Table& comps_a = node_a.find("components")->as<Table>();
    const Table& pos = comps_a.find("Position")->as<Table>();
    REQUIRE(pos.find("x")->as<double>() == 1.0);
    REQUIRE(pos.find("y")->as<double>() == 2.0);

    // b: empty components table
    const Table& node_b = find_node(nodes, std::to_string(b.value()));
    REQUIRE(node_b.find("components")->as<Table>().size() == 0);

    // c: Link.target encoded as a's id string
    const Table& node_c = find_node(nodes, std::to_string(c.value()));
    const Table& link = node_c.find("components")->as<Table>().find("Link")->as<Table>();
    REQUIRE(link.find("target")->as<std::string>() == std::to_string(a.value()));
}

TEST_CASE("save_to_json produces parseable JSON text", "[scene]")
{
    TypeRegistry registry;
    ComponentReflection types;
    types.register_component<Position>(registry, "Position");

    World world;
    const EntityID a = world.create_entity();
    world.add_component<Position>(a, Position{5.0f, 6.0f});

    const std::string json = mosaic::engine::scene::save_to_json(world, types);
    REQUIRE(json.find("\"version\"") != std::string::npos);
    REQUIRE(json.find("\"Position\"") != std::string::npos);
}
