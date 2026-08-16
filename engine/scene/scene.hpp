#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>

#include "engine/reflection/component_reflection.hpp"
#include "engine/world/world.hpp"
#include "foundation/serialization/json_backend.hpp"
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

} // namespace mosaic::engine::scene
