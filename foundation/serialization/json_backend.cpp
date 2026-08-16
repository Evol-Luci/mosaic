#include "foundation/serialization/json_backend.hpp"

#include <cstdint>
#include <limits>

#include <nlohmann/json.hpp>

#include "foundation/serialization/reference.hpp"

namespace mosaic::foundation::serialization
{
namespace
{
using nlohmann::json;

json to_json_node(const Value& v)
{
    if (v.is_null()) return json(nullptr);
    if (v.is<bool>()) return json(v.as<bool>());
    if (v.is<std::int64_t>()) return json(v.as<std::int64_t>());
    if (v.is<std::uint64_t>()) return json(v.as<std::uint64_t>());
    if (v.is<double>()) return json(v.as<double>());
    if (v.is<std::string>()) return json(v.as<std::string>());
    if (v.is<Array>())
    {
        json out = json::array();
        for (const Value& e : v.as<Array>())
        {
            out.push_back(to_json_node(e));
        }
        return out;
    }
    json out = json::object();
    for (const auto& entry : v.as<Table>())
    {
        out[entry.first] = to_json_node(entry.second);
    }
    return out;
}

Value from_json_node(const json& j)
{
    if (j.is_null()) return Value();
    if (j.is_boolean()) return Value(j.get<bool>());
    if (j.is_number_unsigned())
    {
        std::uint64_t u = j.get<std::uint64_t>();
        if (u <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        {
            return Value(static_cast<std::int64_t>(u)); // canonicalize small unsigned to Int64
        }
        return Value(u);
    }
    if (j.is_number_integer()) return Value(j.get<std::int64_t>());
    if (j.is_number_float()) return Value(j.get<double>());
    if (j.is_string()) return Value(j.get<std::string>());
    if (j.is_array())
    {
        Array out;
        for (const auto& e : j)
        {
            out.push_back(from_json_node(e));
        }
        return Value(std::move(out));
    }
    Table out;
    for (auto it = j.begin(); it != j.end(); ++it)
    {
        out[it.key()] = from_json_node(it.value());
    }
    return Value(std::move(out));
}
} // namespace

std::string to_json(const Value& value)
{
    return to_json_node(value).dump(2);
}

Value from_json(const std::string& text)
{
    try
    {
        return from_json_node(json::parse(text));
    }
    catch (const json::exception& e)
    {
        throw SerializationError(std::string("JSON parse error: ") + e.what());
    }
}

} // namespace mosaic::foundation::serialization
