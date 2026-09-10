#pragma once

#include <optional>
#include <string>
#include <unordered_map>

#include "engine/net/net_replicated.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/world/world.hpp"
#include "foundation/serialization/json_backend.hpp"
#include "foundation/serialization/value.hpp"

namespace mosaic::engine::net
{
namespace ser = mosaic::foundation::serialization;
using Value = ser::Value;

// Server: JSON snapshot of every NetReplicated entity's reflected components
// (the marker excluded — its net id is the key). Shape:
//   { "entities": [ { "net": <u>, "components": { "<Name>": <Value>, ... } }, ... ] }
inline std::string snapshot(World& world,
                            const mosaic::engine::reflection::ComponentReflection& types)
{
    const std::vector<std::string> names = types.registered_names();

    ser::Array entities;
    world.for_each<NetReplicated>([&](World::EntityID e, NetReplicated& nr)
    {
        ser::Table components;
        for (const std::string& name : names)
        {
            if (name == "NetReplicated")
            {
                continue; // the marker is the key, never replicated
            }
            std::optional<Value> value = types.component_value(world, e, name);
            if (value.has_value())
            {
                components[name] = *value;
            }
        }

        ser::Table node;
        node["net"] = Value(static_cast<std::uint64_t>(nr.id));
        node["components"] = Value(std::move(components));
        entities.push_back(Value(std::move(node)));
    });

    ser::Table root;
    root["entities"] = Value(std::move(entities));
    return ser::to_json(Value(std::move(root)));
}

// Client: stable NetId -> local EntityID map (create-on-first-sight).
struct NetIdMap
{
    std::unordered_map<NetId, World::EntityID> map;
};

// Read a "net" field as a NetId, accepting either an unsigned or signed integer
// (a JSON round-trip may narrow an unsigned to signed). Returns false if absent
// or not an integer.
inline bool read_net_id(const ser::Table& node, NetId& out)
{
    const Value* net = node.find("net");
    if (net == nullptr)
    {
        return false;
    }
    if (net->is<std::uint64_t>())
    {
        out = static_cast<NetId>(net->as<std::uint64_t>());
        return true;
    }
    if (net->is<std::int64_t>())
    {
        out = static_cast<NetId>(net->as<std::int64_t>());
        return true;
    }
    return false;
}

// Client: apply a snapshot — for each entity node, find-or-create its local
// entity via the map, then set_value() each component (upsert). Returns false on
// malformed JSON/shape; never throws.
inline bool apply_snapshot(const std::string& blob, World& world,
                           const mosaic::engine::reflection::ComponentReflection& types,
                           NetIdMap& ids)
{
    Value root;
    try
    {
        root = ser::from_json(blob);
    }
    catch (...)
    {
        return false; // malformed JSON throws SerializationError
    }

    if (!root.is<ser::Table>())
    {
        return false;
    }
    const ser::Table& root_table = root.as<ser::Table>();

    const Value* entities = root_table.find("entities");
    if (entities == nullptr || !entities->is<ser::Array>())
    {
        return false;
    }

    for (const Value& node_value : entities->as<ser::Array>())
    {
        if (!node_value.is<ser::Table>())
        {
            return false;
        }
        const ser::Table& node = node_value.as<ser::Table>();

        NetId net = 0;
        if (!read_net_id(node, net))
        {
            return false;
        }

        World::EntityID local;
        auto it = ids.map.find(net);
        if (it != ids.map.end())
        {
            local = it->second;
        }
        else
        {
            local = world.create_entity();
            ids.map.emplace(net, local);
        }

        const Value* components = node.find("components");
        if (components == nullptr || !components->is<ser::Table>())
        {
            continue; // no components to apply
        }
        for (const auto& [name, value] : components->as<ser::Table>())
        {
            types.set_value(world, local, name, value); // upsert (add or update)
        }
    }

    return true;
}
} // namespace mosaic::engine::net
