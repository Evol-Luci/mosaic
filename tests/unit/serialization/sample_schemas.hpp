#pragma once

#include <string>
#include <vector>

#include "foundation/core/entity_id.hpp"
#include "foundation/serialization/value.hpp"

// Sample reflected types shared by the serialization tests. Each type opts into
// reflection with a free `reflect(visitor, value)` found via ADL (they live in
// the same namespace as the type). More schemas are appended by later tasks.
namespace mosaic_samples
{

struct Vec3
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    friend bool operator==(const Vec3&, const Vec3&) = default;
};
template <class V> void reflect(V& v, Vec3& t)
{
    v.member("x", t.x);
    v.member("y", t.y);
    v.member("z", t.z);
}

struct Quat
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 1.0;
    friend bool operator==(const Quat&, const Quat&) = default;
};
template <class V> void reflect(V& v, Quat& t)
{
    v.member("x", t.x);
    v.member("y", t.y);
    v.member("z", t.z);
    v.member("w", t.w);
}

struct Transform
{
    Vec3 position;
    Quat rotation;
    Vec3 scale;
    friend bool operator==(const Transform&, const Transform&) = default;
};
template <class V> void reflect(V& v, Transform& t)
{
    v.member("position", t.position);
    v.member("rotation", t.rotation);
    v.member("scale", t.scale);
}

struct Name
{
    std::string value;
    friend bool operator==(const Name&, const Name&) = default;
};
template <class V> void reflect(V& v, Name& t)
{
    v.member("value", t.value);
}


struct SceneEntity
{
    mosaic::foundation::EntityID id;
    mosaic::foundation::serialization::Value components; // Table: type name -> component Value
    friend bool operator==(const SceneEntity&, const SceneEntity&) = default;
};
template <class V> void reflect(V& v, SceneEntity& e)
{
    v.member("id", e.id);
    v.member("components", e.components);
}

struct Scene
{
    std::vector<SceneEntity> entities;
    friend bool operator==(const Scene&, const Scene&) = default;
};
template <class V> void reflect(V& v, Scene& s)
{
    v.member("entities", s.entities);
}

} // namespace mosaic_samples
