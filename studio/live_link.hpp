#pragma once

#include <optional>
#include <string>
#include <vector>

#include "engine/transform/transform.hpp"
#include "engine/world/name.hpp"
#include "engine/world/world.hpp"
#include "foundation/math/quat.hpp"
#include "foundation/math/vec3.hpp"
#include "foundation/serialization/json_backend.hpp"
#include "foundation/serialization/value.hpp"

namespace mosaic::studio
{
struct LiveLinkUpdate
{
    std::string                    name;
    mosaic::foundation::math::Vec3 position;
    mosaic::foundation::math::Quat rotation;
    mosaic::foundation::math::Vec3 scale;
};

namespace detail
{
namespace ser = mosaic::foundation::serialization;

// A JSON number may come back as int64, uint64, or double depending on whether the
// source literal had a fractional part. Normalizes any of the three to float.
inline std::optional<float> as_float(const ser::Value& v)
{
    if (v.is<double>())        { return static_cast<float>(v.as<double>()); }
    if (v.is<std::int64_t>())  { return static_cast<float>(v.as<std::int64_t>()); }
    if (v.is<std::uint64_t>()) { return static_cast<float>(v.as<std::uint64_t>()); }
    return std::nullopt;
}

inline std::optional<mosaic::foundation::math::Vec3> as_vec3(const ser::Value* v)
{
    if (v == nullptr || !v->is<ser::Array>()) { return std::nullopt; }
    const ser::Array& a = v->as<ser::Array>();
    if (a.size() != 3) { return std::nullopt; }
    const auto x = as_float(a[0]);
    const auto y = as_float(a[1]);
    const auto z = as_float(a[2]);
    if (!x || !y || !z) { return std::nullopt; }
    return mosaic::foundation::math::Vec3{*x, *y, *z};
}

inline std::optional<mosaic::foundation::math::Quat> as_quat(const ser::Value* v)
{
    if (v == nullptr || !v->is<ser::Array>()) { return std::nullopt; }
    const ser::Array& a = v->as<ser::Array>();
    if (a.size() != 4) { return std::nullopt; }
    const auto x = as_float(a[0]);
    const auto y = as_float(a[1]);
    const auto z = as_float(a[2]);
    const auto w = as_float(a[3]);
    if (!x || !y || !z || !w) { return std::nullopt; }
    return mosaic::foundation::math::Quat{*x, *y, *z, *w};
}
} // namespace detail

// Parses one newline-delimited JSON line into a LiveLinkUpdate. Returns nullopt on
// malformed JSON, a non-object root, or a missing/wrong-typed field — never throws.
inline std::optional<LiveLinkUpdate> parse_live_link_message(const std::string& line)
{
    namespace ser = mosaic::foundation::serialization;
    ser::Value root;
    try
    {
        root = ser::from_json(line);
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
    if (!root.is<ser::Table>()) { return std::nullopt; }
    const ser::Table& t = root.as<ser::Table>();

    const ser::Value* name_v = t.find("name");
    if (name_v == nullptr || !name_v->is<std::string>()) { return std::nullopt; }

    const auto position = detail::as_vec3(t.find("position"));
    const auto rotation = detail::as_quat(t.find("rotation"));
    const auto scale = detail::as_vec3(t.find("scale"));
    if (!position || !rotation || !scale) { return std::nullopt; }

    LiveLinkUpdate update;
    update.name = name_v->as<std::string>();
    update.position = *position;
    update.rotation = *rotation;
    update.scale = *scale;
    return update;
}

// Finds the entity whose Name matches update.name and overwrites its Transform in
// place. Returns false if zero or more-than-one entity matches (ambiguous or
// unknown name) and touches nothing in that case.
inline bool apply_live_link_update(mosaic::engine::World& world, const LiveLinkUpdate& update)
{
    std::vector<mosaic::engine::World::EntityID> matches;
    world.for_each<mosaic::engine::Name>(
        [&](mosaic::engine::World::EntityID e, mosaic::engine::Name& n)
        {
            if (n.value == update.name) { matches.push_back(e); }
        });
    if (matches.size() != 1) { return false; }

    mosaic::engine::transform::Transform* t =
        world.get_component<mosaic::engine::transform::Transform>(matches[0]);
    if (t == nullptr) { return false; }

    t->position = update.position;
    t->rotation = update.rotation;
    t->scale = update.scale;
    return true;
}
} // namespace mosaic::studio
