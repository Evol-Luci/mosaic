#pragma once

#include "foundation/core/entity_id.hpp"
#include "foundation/math/mat4.hpp"
#include "foundation/math/quat.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::engine::transform
{

// Authored local transform. Reflected, so it serializes and constructs generically
// (#18/#19); `parent` (nil = root) remaps through the scene resolver like any
// EntityID.
struct Transform
{
    mosaic::foundation::math::Vec3 position;
    mosaic::foundation::math::Quat rotation;
    mosaic::foundation::math::Vec3 scale{1.0f, 1.0f, 1.0f};
    mosaic::foundation::EntityID parent;
};

template <class V>
void reflect(V& visitor, Transform& t)
{
    visitor.member("position", t.position);
    visitor.member("rotation", t.rotation);
    visitor.member("scale", t.scale);
    visitor.member("parent", t.parent);
}

// Derived world transform, written by TransformSystem. Not reflected (never
// serialized — recomputed each tick).
struct WorldTransform
{
    mosaic::foundation::math::Mat4 matrix;
};

} // namespace mosaic::engine::transform
