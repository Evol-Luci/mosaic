#pragma once

#include <cstdint>

#include "foundation/math/vec3.hpp"

namespace mosaic::engine::vfx
{
using mosaic::foundation::math::Vec3;

// A fading tail left behind a moving entity. Reflected + serialized; the live position
// history lives in TrailSystem, not here.
struct TrailRenderer
{
    Vec3  color           = Vec3{1.0f, 1.0f, 1.0f}; // trail color
    float size            = 0.1f;                    // billboard edge length at the head (newest)
    int   max_points      = 32;                      // history length (ring-buffer cap)
    float sample_interval = 0.03f;                   // seconds between position samples
};

template <class V>
void reflect(V& visitor, TrailRenderer& t)
{
    visitor.member("color", t.color);
    visitor.member("size", t.size);
    visitor.member("max_points", t.max_points);
    visitor.member("sample_interval", t.sample_interval);
}
} // namespace mosaic::engine::vfx
