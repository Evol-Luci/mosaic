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

// A box collider (P2 is box-only; more shapes in P3).
struct Collider
{
    mosaic::foundation::math::Vec3 half_extents{0.5f, 0.5f, 0.5f};
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
} // namespace mosaic::engine::physics
