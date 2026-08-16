#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "foundation/reflection/reflect.hpp"
#include "foundation/serialization/reference.hpp"
#include "foundation/core/entity_id.hpp"
#include "foundation/serialization/value.hpp"

namespace mosaic::foundation::serialization
{

// --- type traits ---------------------------------------------------------
template <class>
inline constexpr bool dependent_false = false;

template <class>
inline constexpr bool is_std_vector_v = false;
template <class E, class A>
inline constexpr bool is_std_vector_v<std::vector<E, A>> = true;

template <class>
inline constexpr bool is_handle_v = false;
template <class R>
inline constexpr bool is_handle_v<Handle<R>> = true;

// --- forward declarations -----------------------------------------------
template <class T> Value to_value(T& obj);
template <class T> void from_value(const Value& node, T& obj, const ReferenceResolver& resolver);

// --- encode: reflected field value -> Value -----------------------------
template <class M>
Value encode(M& value)
{
    using U = std::remove_cv_t<M>;
    if constexpr (std::is_same_v<U, Value>)
    {
        return value; // open-ended pass-through (e.g. metadata/import_settings)
    }
    else if constexpr (std::is_same_v<U, bool>)
    {
        return Value(value);
    }
    else if constexpr (std::is_same_v<U, mosaic::foundation::EntityID>)
    {
        return Value(std::to_string(value.value()));
    }
    else if constexpr (std::is_same_v<U, AssetID>)
    {
        return Value(value.to_string());
    }
    else if constexpr (is_handle_v<U>)
    {
        return Value(value.asset_id().to_string());
    }
    else if constexpr (std::is_integral_v<U> && std::is_signed_v<U>)
    {
        return Value(static_cast<std::int64_t>(value));
    }
    else if constexpr (std::is_integral_v<U> && std::is_unsigned_v<U>)
    {
        return Value(static_cast<std::uint64_t>(value));
    }
    else if constexpr (std::is_floating_point_v<U>)
    {
        return Value(static_cast<double>(value));
    }
    else if constexpr (std::is_same_v<U, std::string>)
    {
        return Value(value);
    }
    else if constexpr (is_std_vector_v<U>)
    {
        Array arr;
        arr.reserve(value.size());
        for (auto& element : value)
        {
            arr.push_back(encode(element));
        }
        return Value(std::move(arr));
    }
    else if constexpr (reflection::Reflectable<U>)
    {
        return to_value(value);
    }
    else
    {
        static_assert(dependent_false<U>, "encode: unsupported field type");
    }
}

// --- decode: Value -> reflected field value -----------------------------
template <class M>
void decode(const Value& node, M& out, const ReferenceResolver& resolver)
{
    using U = std::remove_cv_t<M>;
    if constexpr (std::is_same_v<U, Value>)
    {
        out = node;
    }
    else if constexpr (std::is_same_v<U, bool>)
    {
        if (!node.is<bool>()) throw SerializationError("decode: expected bool");
        out = node.as<bool>();
    }
    else if constexpr (std::is_same_v<U, mosaic::foundation::EntityID>)
    {
        if (!node.is<std::string>()) throw SerializationError("decode: expected EntityID string");
        std::uint64_t raw = 0;
        try
        {
            raw = std::stoull(node.as<std::string>());
        }
        catch (const std::exception& e)
        {
            throw SerializationError(std::string("decode: invalid EntityID string: ") + e.what());
        }
        out = resolver.resolve_entity(mosaic::foundation::EntityID(raw));
    }
    else if constexpr (std::is_same_v<U, AssetID>)
    {
        if (!node.is<std::string>()) throw SerializationError("decode: expected AssetID string");
        out = resolver.resolve_asset(AssetID::from_string(node.as<std::string>()));
    }
    else if constexpr (is_handle_v<U>)
    {
        if (!node.is<std::string>()) throw SerializationError("decode: expected Handle AssetID string");
        AssetID id = resolver.resolve_asset(AssetID::from_string(node.as<std::string>()));
        out = U(id);
    }
    else if constexpr (std::is_integral_v<U> && std::is_signed_v<U>)
    {
        if (!node.is<std::int64_t>()) throw SerializationError("decode: expected signed integer");
        out = static_cast<U>(node.as<std::int64_t>());
    }
    else if constexpr (std::is_integral_v<U> && std::is_unsigned_v<U>)
    {
        if (!node.is<std::uint64_t>()) throw SerializationError("decode: expected unsigned integer");
        out = static_cast<U>(node.as<std::uint64_t>());
    }
    else if constexpr (std::is_floating_point_v<U>)
    {
        if (!node.is<double>()) throw SerializationError("decode: expected floating-point");
        out = static_cast<U>(node.as<double>());
    }
    else if constexpr (std::is_same_v<U, std::string>)
    {
        if (!node.is<std::string>()) throw SerializationError("decode: expected string");
        out = node.as<std::string>();
    }
    else if constexpr (is_std_vector_v<U>)
    {
        if (!node.is<Array>()) throw SerializationError("decode: expected array");
        const Array& arr = node.as<Array>();
        out.clear();
        out.resize(arr.size());
        for (std::size_t i = 0; i < arr.size(); ++i)
        {
            decode(arr[i], out[i], resolver);
        }
    }
    else if constexpr (reflection::Reflectable<U>)
    {
        from_value(node, out, resolver);
    }
    else
    {
        static_assert(dependent_false<U>, "decode: unsupported field type");
    }
}

// --- visitors ------------------------------------------------------------
struct WriteVisitor
{
    Table table;
    template <class M> void member(std::string_view name, M& value)
    {
        table[std::string(name)] = encode(value);
    }
};

struct ReadVisitor
{
    const Table& table;
    const ReferenceResolver& resolver;
    template <class M> void member(std::string_view name, M& value)
    {
        const Value* found = table.find(std::string(name));
        if (found != nullptr)
        {
            decode(*found, value, resolver);
        }
    }
};

// --- public entry points -------------------------------------------------
template <class T>
Value to_value(T& obj)
{
    WriteVisitor visitor;
    reflect(visitor, obj);
    return Value(std::move(visitor.table));
}

// Overload for temporaries / rvalues: materialize and delegate to the
// lvalue overload so callers can write to_value(MyType{...}) without a
// named local.
template <class T>
Value to_value(T&& obj)
{
    return to_value(obj);
}

inline const ReferenceResolver& default_resolver()
{
    static const ReferenceResolver resolver;
    return resolver;
}

template <class T>
void from_value(const Value& node, T& obj, const ReferenceResolver& resolver)
{
    if (!node.is<Table>()) throw SerializationError("from_value: expected table");
    ReadVisitor visitor{node.as<Table>(), resolver};
    reflect(visitor, obj);
}

template <class T>
void from_value(const Value& node, T& obj)
{
    from_value(node, obj, default_resolver());
}

} // namespace mosaic::foundation::serialization
