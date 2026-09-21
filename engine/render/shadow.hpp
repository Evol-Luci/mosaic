#pragma once

#include <cmath>

#include "engine/render/directional_light.hpp"
#include "foundation/math/mat4.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::engine::render
{

// Fixed-size shadow frustum, v1: a box generously sized for the current demo/test
// scenes, centered on the origin. Not scene/camera-fitted -- see the design doc's
// Non-goals (docs/superpowers/specs/2026-09-21-render-shadows-v1-design.md) and
// docs/project/deferred-work-backlog.md's Rendering section for the deferred
// scene-fitted/cascaded follow-up.
constexpr float kShadowFrustumHalfExtent = 15.0f;
constexpr float kShadowNearZ = 0.1f;
constexpr float kShadowFarZ = 2.0f * kShadowFrustumHalfExtent + kShadowNearZ;

// The light's combined view-projection matrix for the fixed v1 shadow frustum.
// `light_position` is pulled back along -direction from the origin by the
// frustum's own half-extent (plus the near-plane offset), so the fixed volume
// always sits entirely in front of the near plane -- a deterministic function of
// direction alone, never scene-dependent.
inline mosaic::foundation::math::Mat4
directional_light_view_projection(const DirectionalLight& light)
{
    using mosaic::foundation::math::Mat4;
    using mosaic::foundation::math::Vec3;
    using mosaic::foundation::math::normalize;

    const Vec3 dir = normalize(light.direction);
    const Vec3 light_position = Vec3{0.0f, 0.0f, 0.0f} -
                                dir * (kShadowFrustumHalfExtent + kShadowNearZ);
    // An up vector not parallel to `dir` -- fall back to world +X if `dir` is
    // (near-)vertical, mirroring the degenerate-up handling every other look_at
    // call site in this codebase already assumes callers avoid.
    Vec3 up{0.0f, 1.0f, 0.0f};
    if (std::abs(mosaic::foundation::math::dot(dir, up)) > 0.999f)
    {
        up = Vec3{1.0f, 0.0f, 0.0f};
    }
    const Mat4 view = Mat4::look_at(light_position, light_position + dir, up);
    const Mat4 proj = Mat4::orthographic(
        -kShadowFrustumHalfExtent, kShadowFrustumHalfExtent,
        -kShadowFrustumHalfExtent, kShadowFrustumHalfExtent,
        kShadowNearZ, kShadowFarZ);
    return proj * view;
}

} // namespace mosaic::engine::render
