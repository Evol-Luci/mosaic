#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reference.hpp"
#include "foundation/serialization/reflect_value.hpp"
#include "foundation/serialization/value.hpp"

namespace mosaic::engine::reflection
{

// Bridges a foundation TypeRegistry to generic component construction into a
// World. Holds one attach thunk per registered component type, keyed by name.
// The thunk is captured where T is known (register_component); construct() callers
// (e.g. Scene load) name only a string, never a concrete C++ type.
class ComponentReflection
{
public:
    using Value = mosaic::foundation::serialization::Value;
    using ReferenceResolver = mosaic::foundation::serialization::ReferenceResolver;
    using EntityID = World::EntityID;

    // Registers T's metadata into `registry` AND an attach thunk here, sharing
    // type_id<T>() and `name`.
    template <class T>
    void register_component(mosaic::foundation::reflection::TypeRegistry& registry, std::string name)
    {
        registry.register_type<T>(name);
        attachers_[name] =
            [](World& world, EntityID e, const Value& value, const ReferenceResolver& resolver)
        {
            T component{};
            mosaic::foundation::serialization::from_value(value, component, resolver);
            return world.add_component<T>(e, std::move(component));
        };
        savers_[std::move(name)] =
            [](World& world, const std::string& type_name, const SaveSink& sink)
        {
            world.for_each<T>([&](EntityID e, T& component)
            {
                sink(e, type_name, mosaic::foundation::serialization::to_value(component));
            });
        };
    }

    using SaveSink = std::function<void(EntityID, const std::string& type_name, const Value&)>;

    // For every registered component type, emit (entity, name, to_value(component))
    // for each entity that holds it.
    void save(World& world, const SaveSink& sink) const
    {
        for (const auto& [name, saver] : savers_)
        {
            saver(world, name, sink);
        }
    }

    // True iff `name` is a registered component type.
    bool has(std::string_view name) const
    {
        return attachers_.find(std::string(name)) != attachers_.end();
    }

    // Look up by name, deserialize `value` into a fresh component via the templated
    // from_value, and world.add_component<T>(e, ...). Returns false if `name` is not
    // registered, or whatever add_component returns (false for an invalid entity).
    bool construct(World& world, EntityID e, std::string_view name, const Value& value,
                   const ReferenceResolver& resolver =
                       mosaic::foundation::serialization::default_resolver()) const
    {
        auto it = attachers_.find(std::string(name));
        if (it == attachers_.end())
        {
            return false;
        }
        return it->second(world, e, value, resolver);
    }

private:
    using Attacher = std::function<bool(World&, EntityID, const Value&, const ReferenceResolver&)>;
    std::unordered_map<std::string, Attacher> attachers_;

    using Saver = std::function<void(World&, const std::string&, const SaveSink&)>;
    // std::map (not unordered): iterating in sorted type-name order gives
    // deterministic per-entity component ordering in saved scenes.
    std::map<std::string, Saver> savers_;
};

} // namespace mosaic::engine::reflection
