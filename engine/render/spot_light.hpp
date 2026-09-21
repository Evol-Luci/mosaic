#pragma once
#include "foundation/math/vec3.hpp"
namespace mosaic::engine::render
{
// A positional, directional light with a cone. World position and direction are
// NOT stored here -- position is derived from the light entity's WorldTransform
// translation (like PointLight), direction from its rotation (WorldTransform's
// local -Z axis in world space, via Mat4::forward() -- the same convention
// Camera's view derivation already uses). `color` is premultiplied by
// `intensity` at shading time; `range` is the same windowed-attenuation
// distance falloff PointLight already uses. `inner_angle`/`outer_angle` are
// cone HALF-angles in radians (angle from the light's forward axis to the cone
// edge) -- full intensity inside `inner_angle`, smoothly fading to zero at
// `outer_angle`. `inner_angle` should be <= `outer_angle`; this is not
// enforced here -- a caller passing inner > outer gets an inverted, likely
// visually-wrong falloff, not a crash, consistent with this project's
// "missing/malformed data degrades gracefully, doesn't throw" convention for
// render components.
struct SpotLight
{
    mosaic::foundation::math::Vec3 color{1.0f, 1.0f, 1.0f};
    float                          intensity{1.0f};
    float                          range{10.0f};
    float                          inner_angle{0.4f};  // radians (~23 degrees)
    float                          outer_angle{0.6f};  // radians (~34 degrees)
};
template <class V> void reflect(V& visitor, SpotLight& l)
{
    visitor.member("color", l.color);
    visitor.member("intensity", l.intensity);
    visitor.member("range", l.range);
    visitor.member("inner_angle", l.inner_angle);
    visitor.member("outer_angle", l.outer_angle);
}
} // namespace mosaic::engine::render
