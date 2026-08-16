#include "foundation/serialization/toml_backend.hpp"

#include <cstdint>
#include <sstream>
#include <string>

#include <toml++/toml.h>

#include "foundation/serialization/reference.hpp"

namespace mosaic::foundation::serialization
{
namespace
{
toml::table to_toml_table(const Table& t);

toml::array to_toml_array(const Array& a); // fwd

void insert_toml(toml::table& tbl, const std::string& key, const Value& v)
{
    if (v.is_null()) return; // TOML has no null; omit the key
    if (v.is<bool>()) { tbl.insert(key, v.as<bool>()); return; }
    if (v.is<std::int64_t>()) { tbl.insert(key, v.as<std::int64_t>()); return; }
    if (v.is<std::uint64_t>()) { tbl.insert(key, static_cast<std::int64_t>(v.as<std::uint64_t>())); return; }
    if (v.is<double>()) { tbl.insert(key, v.as<double>()); return; }
    if (v.is<std::string>()) { tbl.insert(key, v.as<std::string>()); return; }
    if (v.is<Array>()) { tbl.insert(key, to_toml_array(v.as<Array>())); return; }
    tbl.insert(key, to_toml_table(v.as<Table>()));
}

void push_toml(toml::array& arr, const Value& v)
{
    if (v.is<bool>()) { arr.push_back(v.as<bool>()); return; }
    if (v.is<std::int64_t>()) { arr.push_back(v.as<std::int64_t>()); return; }
    if (v.is<std::uint64_t>()) { arr.push_back(static_cast<std::int64_t>(v.as<std::uint64_t>())); return; }
    if (v.is<double>()) { arr.push_back(v.as<double>()); return; }
    if (v.is<std::string>()) { arr.push_back(v.as<std::string>()); return; }
    if (v.is<Array>()) { arr.push_back(to_toml_array(v.as<Array>())); return; }
    if (v.is<Table>()) { arr.push_back(to_toml_table(v.as<Table>())); return; }
    // Null in an array is omitted; acceptance schemas never produce it.
}

toml::array to_toml_array(const Array& a)
{
    toml::array out;
    for (const Value& v : a)
    {
        push_toml(out, v);
    }
    return out;
}

toml::table to_toml_table(const Table& t)
{
    toml::table out;
    for (const auto& entry : t)
    {
        insert_toml(out, entry.first, entry.second);
    }
    return out;
}

Value node_to_value(const toml::node& n);

Value table_to_value(const toml::table& t)
{
    Table out;
    for (auto&& [key, value] : t)
    {
        out[std::string(key.str())] = node_to_value(value);
    }
    return Value(std::move(out));
}

Value array_to_value(const toml::array& a)
{
    Array out;
    a.for_each([&](const toml::node& element) { out.push_back(node_to_value(element)); });
    return Value(std::move(out));
}

Value node_to_value(const toml::node& n)
{
    switch (n.type())
    {
        case toml::node_type::boolean: return Value(n.value<bool>().value());
        case toml::node_type::integer: return Value(n.value<std::int64_t>().value());
        case toml::node_type::floating_point: return Value(n.value<double>().value());
        case toml::node_type::string: return Value(n.value<std::string>().value());
        case toml::node_type::array: return array_to_value(*n.as_array());
        case toml::node_type::table: return table_to_value(*n.as_table());
        default: return Value(); // date/time node types are unused by these schemas
    }
}
} // namespace

std::string to_toml(const Value& value)
{
    if (!value.is<Table>())
    {
        throw SerializationError("to_toml: top-level value must be a table");
    }
    std::ostringstream os;
    os << to_toml_table(value.as<Table>());
    return os.str();
}

Value from_toml(const std::string& text)
{
    try
    {
        return table_to_value(toml::parse(text));
    }
    catch (const toml::parse_error& e)
    {
        throw SerializationError(std::string("TOML parse error: ") + e.description().data());
    }
}

} // namespace mosaic::foundation::serialization
