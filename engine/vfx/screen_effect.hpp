#pragma once

#include "foundation/math/vec3.hpp"

namespace mosaic::engine::vfx
{
using mosaic::foundation::math::Vec3;

// Drives a fullscreen overlay: fade/flash/grade (tint + tint_strength) and vignette.
// Defaults = no visible effect. A normal component, so scripts can animate tint_strength.
struct ScreenEffect
{
    Vec3  tint          = Vec3{0, 0, 0}; // color the screen blends toward
    float tint_strength = 0.0f;          // 0 = none, 1 = fully tint
    float vignette      = 0.0f;          // 0 = none, 1 = strong edge darkening
};

template <class V>
void reflect(V& visitor, ScreenEffect& s)
{
    visitor.member("tint", s.tint);
    visitor.member("tint_strength", s.tint_strength);
    visitor.member("vignette", s.vignette);
}
} // namespace mosaic::engine::vfx
