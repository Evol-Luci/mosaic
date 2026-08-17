#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include "engine/reflection/component_reflection.hpp"
#include "engine/world/world.hpp"
#include "foundation/serialization/json_backend.hpp"
#include "foundation/serialization/reference.hpp"
#include "foundation/serialization/value.hpp"

namespace mosaic::engine::scene
{

using Value = mosaic::foundation::serialization::Value;

// Serialize every live entity and its registered components to a scene Value:
// { version: 1, entities: [ { id, components: { TypeName: {fields...} } } ] }.
// Component-less entities appear with an empty components table.
inline Value to_value(World& world, const reflection::ComponentReflection& types)
{
    namespace ser = mosaic::foundation::serialization;

    std::unordered_map<std::uint64_t, ser::Table> components_by_entity;
    types.save(world, [&](World::EntityID e, const std::string& name, const Value& value)
    {
        components_by_entity[e.value()][name] = value;
    });

    ser::Array entities;
    world.for_each_entity([&](World::EntityID e)
    {
        ser::Table node;
        node["id"] = Value(std::to_string(e.value()));
        auto it = components_by_entity.find(e.value());
        node["components"] = Value(it != components_by_entity.end() ? it->second : ser::Table{});
        entities.push_back(Value(std::move(node)));
    });

    ser::Table root;
    root["version"] = Value(std::int64_t{1});
    root["entities"] = Value(std::move(entities));
    return Value(std::move(root));
}

// JSON-string convenience over to_value (via the #10 JSON backend).
inline std::string save_to_json(World& world, const reflection::ComponentReflection& types)
{
    return mosaic::foundation::serialization::to_json(to_value(world, types));
}

// Remaps serialized intra-scene EntityID references to the handles created on
// load. A reference to an entity not in this scene resolves to null EntityID(0).
class SceneReferenceResolver final : public mosaic::foundation::serialization::ReferenceResolver
{
public:
    explicit SceneReferenceResolver(
        const std::unordered_map<std::uint64_t, World::EntityID>& remap)
        : remap_(remap)
    {
    }

    mosaic::foundation::EntityID resolve_entity(
        mosaic::foundation::EntityID serialized) const override
    {
        auto it = remap_.find(serialized.value());
        return it == remap_.end() ? mosaic::foundation::EntityID(0) : it->second;
    }

private:
    const std::unordered_map<std::uint64_t, World::EntityID>& remap_;
};

// Rebuild entities + components into `world` from a scene Value. `world` is
// expected empty for a faithful round-trip. Throws SerializationError on a
// malformed document or an unknown component type name.
inline void from_value(const Value& scene, World& world,
                       const reflection::ComponentReflection& types)
{
    namespace ser = mosaic::foundation::serialization;

    if (!scene.is<ser::Table>())
    {
        throw ser::SerializationError("Scene: root must be a table");
    }
    const ser::Table& root = scene.as<ser::Table>();

    const Value* version = root.find("version");
    if (version == nullptr || !version->is<std::int64_t>() || version->as<std::int64_t>() != 1)
    {
        throw ser::SerializationError("Scene: missing or unsupported version");
    }

    const Value* entities = root.find("entities");
    if (entities == nullptr || !entities->is<ser::Array>())
    {
        throw ser::SerializationError("Scene: 'entities' must be an array");
    }
    const ser::Array& entity_nodes = entities->as<ser::Array>();

    // Pass 1: create entities, build the serialized-id -> new-handle remap.
    std::unordered_map<std::uint64_t, World::EntityID> remap;
    remap.reserve(entity_nodes.size());
    for (const Value& node_value : entity_nodes)
    {
        if (!node_value.is<ser::Table>())
        {
            throw ser::SerializationError("Scene: entity node must be a table");
        }
        const ser::Table& node = node_value.as<ser::Table>();
        const Value* id = node.find("id");
        if (id == nullptr || !id->is<std::string>())
        {
            throw ser::SerializationError("Scene: entity 'id' must be a string");
        }
        std::uint64_t raw = 0;
        try
        {
            raw = std::stoull(id->as<std::string>());
        }
        catch (const std::exception& e)
        {
            throw ser::SerializationError(std::string("Scene: invalid entity id: ") + e.what());
        }
        remap[raw] = world.create_entity();
    }

    const SceneReferenceResolver resolver(remap);

    // Pass 2: construct each entity's components, remapping references.
    for (const Value& node_value : entity_nodes)
    {
        const ser::Table& node = node_value.as<ser::Table>();
        const std::uint64_t raw = std::stoull(node.find("id")->as<std::string>());
        const World::EntityID entity = remap[raw];

        const Value* components = node.find("components");
        if (components == nullptr)
        {
            continue;
        }
        if (!components->is<ser::Table>())
        {
            throw ser::SerializationError("Scene: 'components' must be a table");
        }
        for (const auto& [type_name, field_value] : components->as<ser::Table>())
        {
            if (!types.has(type_name))
            {
                throw ser::SerializationError("Scene: unknown component type '" + type_name + "'");
            }
            types.construct(world, entity, type_name, field_value, resolver);
        }
    }
}

// JSON-string convenience over from_value (via the #10 JSON backend).
inline void load_from_json(const std::string& json, World& world,
                           const reflection::ComponentReflection& types)
{
    from_value(mosaic::foundation::serialization::from_json(json), world, types);
}

} // namespace mosaic::engine::scene
