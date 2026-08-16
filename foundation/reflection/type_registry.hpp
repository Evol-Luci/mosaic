#pragma once

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "foundation/core/entity_id.hpp"
#include "foundation/reflection/reflect.hpp"
#include "foundation/reflection/type_id.hpp"
#include "foundation/serialization/reference.hpp"
#include "foundation/serialization/reflect_value.hpp"
#include "foundation/serialization/value.hpp"

namespace mosaic::foundation::reflection
{

// A reflected member: its name and a canonical, human-readable type label. The
// label mirrors the serialization encode() categories; it is descriptive (drives
// an inspector column / documentation), NOT a re-parseable type expression.
struct FieldInfo
{
    std::string name;
    std::string type;
};

struct TypeInfo
{
    TypeID id;                       // runtime handle (see type_id.hpp)
    std::string name;                // stable, persisted identity
    std::vector<FieldInfo> fields;   // in reflect() declaration order
};

// Canonical field-type label. Mirrors serialization::encode() so the label set
// stays in step with what actually round-trips.
template <class M>
std::string field_type_name()
{
    namespace ser = mosaic::foundation::serialization;
    using U = std::remove_cv_t<M>;
    if constexpr (std::is_same_v<U, ser::Value>)
    {
        return "value";
    }
    else if constexpr (std::is_same_v<U, bool>)
    {
        return "bool";
    }
    else if constexpr (std::is_same_v<U, mosaic::foundation::EntityID>)
    {
        return "EntityID";
    }
    else if constexpr (std::is_same_v<U, ser::AssetID>)
    {
        return "AssetID";
    }
    else if constexpr (ser::is_handle_v<U>)
    {
        return "Handle";
    }
    else if constexpr (std::is_integral_v<U> && std::is_signed_v<U>)
    {
        return "i" + std::to_string(sizeof(U) * 8);
    }
    else if constexpr (std::is_integral_v<U> && std::is_unsigned_v<U>)
    {
        return "u" + std::to_string(sizeof(U) * 8);
    }
    else if constexpr (std::is_floating_point_v<U>)
    {
        return sizeof(U) == 4 ? "f32" : "f64";
    }
    else if constexpr (std::is_same_v<U, std::string>)
    {
        return "string";
    }
    else if constexpr (ser::is_std_vector_v<U>)
    {
        return "array<" + field_type_name<typename U::value_type>() + ">";
    }
    else if constexpr (Reflectable<U>)
    {
        return "object";
    }
    else
    {
        return "unknown";
    }
}

// A runtime registry of reflected types, keyed by stable name and by TypeID. Owned
// instance, no global. Register once per type; the returned TypeInfo has a stable
// address for the registry's lifetime.
class TypeRegistry
{
public:
    // Records TypeInfo{ type_id<T>(), name, fields-of-T }. Fields are enumerated by
    // running reflect() over a default-constructed T (T must be default-
    // constructible and reflected). Idempotent for the same T already registered
    // under `name`; throws std::logic_error on a re-register under a different name
    // or a name collision with a different type.
    template <class T>
    const TypeInfo& register_type(std::string name)
    {
        const TypeID id = type_id<T>();

        if (auto it = by_id_.find(id); it != by_id_.end())
        {
            const TypeInfo& existing = *it->second;
            if (existing.name != name)
            {
                throw std::logic_error(
                    "TypeRegistry: type already registered under a different name");
            }
            return existing;
        }
        if (by_name_.find(name) != by_name_.end())
        {
            throw std::logic_error(
                "TypeRegistry: name '" + name + "' already registered for a different type");
        }

        auto info = std::make_unique<TypeInfo>();
        info->id = id;
        info->name = name;

        FieldEnumerator enumerator{info->fields};
        T probe{};
        reflect(enumerator, probe);

        const TypeInfo& ref = *info;
        by_name_.emplace(info->name, info.get());
        by_id_.emplace(id, std::move(info));
        return ref;
    }

    const TypeInfo* find(std::string_view name) const
    {
        auto it = by_name_.find(std::string(name));
        return it == by_name_.end() ? nullptr : it->second;
    }

    const TypeInfo* find(TypeID id) const
    {
        auto it = by_id_.find(id);
        return it == by_id_.end() ? nullptr : it->second.get();
    }

private:
    // Visitor that records each member's name + type label into a field list.
    struct FieldEnumerator
    {
        std::vector<FieldInfo>& fields;
        template <class M> void member(std::string_view name, M&)
        {
            fields.push_back(FieldInfo{std::string(name), field_type_name<M>()});
        }
    };

    // by_id_ owns the TypeInfo (stable heap address); by_name_ points into it.
    std::unordered_map<std::string, TypeInfo*> by_name_;
    std::unordered_map<TypeID, std::unique_ptr<TypeInfo>> by_id_;
};

} // namespace mosaic::foundation::reflection
