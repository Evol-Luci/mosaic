#pragma once

#include "foundation/math/vec3.hpp"
#include "foundation/serialization/reference.hpp" // AssetID

namespace mosaic::engine::vfx
{
using mosaic::foundation::math::Vec3;

// A world-oriented, alpha-blended textured quad on a surface (scorch marks, blob
// shadows, signs). The entity's Transform (-> WorldTransform) places/orients/sizes it.
struct Decal
{
    mosaic::foundation::serialization::AssetID texture{};                 // nil = default white
    Vec3                                       color = Vec3{1, 1, 1};     // tint * texture
    float                                      alpha = 1.0f;              // opacity multiplier
};

template <class V>
void reflect(V& visitor, Decal& d)
{
    visitor.member("texture", d.texture);
    visitor.member("color", d.color);
    visitor.member("alpha", d.alpha);
}
} // namespace mosaic::engine::vfx
