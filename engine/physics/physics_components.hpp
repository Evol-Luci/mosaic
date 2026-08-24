#pragma once

#include "foundation/math/vec3.hpp"

namespace mosaic::engine::physics
{
// A physics body attached to an entity. `dynamic` false = static (never moved by the sim).
struct RigidBody
{
    bool  dynamic = true;
    float density = 1.0f; // for dynamic bodies
};

// A box collider.
struct Collider
{
    mosaic::foundation::math::Vec3 half_extents{0.5f, 0.5f, 0.5f};
};

// A sphere collider.
struct SphereCollider
{
    float radius = 0.5f;
};

// A capsule collider (radius + half-length along local Y).
struct CapsuleCollider
{
    float radius = 0.5f;
    float half_height = 0.5f;
};

template <class V>
void reflect(V& visitor, RigidBody& b)
{
    visitor.member("dynamic", b.dynamic);
    visitor.member("density", b.density);
}

template <class V>
void reflect(V& visitor, Collider& c)
{
    visitor.member("half_extents", c.half_extents);
}

template <class V>
void reflect(V& visitor, SphereCollider& s)
{
    visitor.member("radius", s.radius);
}

template <class V>
void reflect(V& visitor, CapsuleCollider& c)
{
    visitor.member("radius", c.radius);
    visitor.member("half_height", c.half_height);
}
} // namespace mosaic::engine::physics
