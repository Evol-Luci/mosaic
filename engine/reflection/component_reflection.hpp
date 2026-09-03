#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine/reflection/field_visitor.hpp"
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
        presence_[name] = [](World& world, EntityID e) { return world.has_component<T>(e); };
        inspectors_[name] = [](World& world, EntityID e, FieldVisitor& visitor)
        {
            if (T* component = world.get_component<T>(e))
            {
                FieldVisitorAdapter adapter{visitor};
                reflect(adapter, *component);
            }
        };
        valuers_[name] = [](World& world, EntityID e) -> std::optional<Value>
        {
            if (T* c = world.get_component<T>(e))
            {
                return mosaic::foundation::serialization::to_value(*c);
            }
            return std::nullopt;
        };
        setters_[name] = [](World& world, EntityID e, const Value& value,
                            const ReferenceResolver& resolver) -> bool
        {
            T c{};
            try
            {
                mosaic::foundation::serialization::from_value(value, c, resolver);
            }
            catch (const mosaic::foundation::serialization::SerializationError&)
            {
                return false;
            }
            if (T* existing = world.get_component<T>(e))
            {
                *existing = std::move(c);
                return true;
            }
            return world.add_component<T>(e, std::move(c));
        };
        removers_[name] = [](World& world, EntityID e) -> bool
        {
            return world.remove_component<T>(e);
        };
        adders_[name] = [](World& world, EntityID e) -> bool
        {
            if (world.has_component<T>(e)) { return false; } // never replace
            return world.add_component<T>(e, T{});           // default-constructed
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

    // Registered component type names the entity holds, in sorted order (presence_ is a map).
    std::vector<std::string> components_on(World& world, EntityID e) const
    {
        std::vector<std::string> names;
        for (const auto& [name, present] : presence_)
        {
            if (present(world, e))
            {
                names.push_back(name);
            }
        }
        return names;
    }

    // Live component `name` on `e` as a Value (to_value), or nullopt if unregistered/absent.
    std::optional<Value> component_value(World& world, EntityID e, std::string_view name) const
    {
        auto it = valuers_.find(std::string(name));
        return it == valuers_.end() ? std::nullopt : it->second(world, e);
    }

    // Deserialize `value` and apply to `e`'s component `name` (update in place if present, else
    // add). False if unregistered, invalid entity, or deserialization fails. Never throws.
    bool set_value(World& world, EntityID e, std::string_view name, const Value& value,
                   const ReferenceResolver& resolver =
                       mosaic::foundation::serialization::default_resolver()) const
    {
        auto it = setters_.find(std::string(name));
        return it == setters_.end() ? false : it->second(world, e, value, resolver);
    }

    // Remove component `name` from `e`. False if unregistered or the entity had no such component.
    bool remove_value(World& world, EntityID e, std::string_view name) const
    {
        auto it = removers_.find(std::string(name));
        return it == removers_.end() ? false : it->second(world, e);
    }

    // All registered component type names, sorted (for an "Add Component" menu).
    std::vector<std::string> registered_names() const
    {
        std::vector<std::string> names;
        names.reserve(presence_.size());
        for (const auto& entry : presence_) { names.push_back(entry.first); }
        return names;
    }

    // Attach a default-constructed component `name` to `e`. False if unregistered or already present.
    bool add_default(World& world, EntityID e, std::string_view name) const
    {
        auto it = adders_.find(std::string(name));
        return it == adders_.end() ? false : it->second(world, e);
    }

    // Run `visitor` over the live component `name` on entity `e` (no-op if unregistered
    // or the entity lacks it).
    void inspect(World& world, EntityID e, std::string_view name, FieldVisitor& visitor) const
    {
        auto it = inspectors_.find(std::string(name));
        if (it != inspectors_.end())
        {
            it->second(world, e, visitor);
        }
    }

private:
    using Attacher = std::function<bool(World&, EntityID, const Value&, const ReferenceResolver&)>;
    std::unordered_map<std::string, Attacher> attachers_;

    using Saver = std::function<void(World&, const std::string&, const SaveSink&)>;
    // std::map (not unordered): iterating in sorted type-name order gives
    // deterministic per-entity component ordering in saved scenes.
    std::map<std::string, Saver> savers_;

    using Presence = std::function<bool(World&, EntityID)>;
    // std::map so components_on returns sorted (deterministic) names.
    std::map<std::string, Presence> presence_;

    using Inspector = std::function<void(World&, EntityID, FieldVisitor&)>;
    std::unordered_map<std::string, Inspector> inspectors_;

    using Valuer = std::function<std::optional<Value>(World&, EntityID)>;
    std::unordered_map<std::string, Valuer> valuers_;
    using Setter = std::function<bool(World&, EntityID, const Value&, const ReferenceResolver&)>;
    std::unordered_map<std::string, Setter> setters_;
    using Remover = std::function<bool(World&, EntityID)>;
    std::unordered_map<std::string, Remover> removers_;
    using Adder = std::function<bool(World&, EntityID)>;
    std::unordered_map<std::string, Adder> adders_;
};

} // namespace mosaic::engine::reflection
