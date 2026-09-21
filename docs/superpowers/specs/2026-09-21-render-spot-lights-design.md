# Rendering — Spot Lights (design)

**Backlog item:** "Specular/PBR, spot lights" (decomposed 2026-09-21), 🔴 Large
tier, `docs/project/deferred-work-backlog.md`. No tracked GitHub issue yet.
**Date:** 2026-09-21.

## Summary

Add a `SpotLight` component: a third simultaneous "first found" light type
alongside the existing `DirectionalLight` and `PointLight`, reusing the point
light's positional (`WorldTransform`-derived) machinery and windowed
inverse-square distance falloff, with an added cone falloff (smooth inner/outer
angle, not a hard cutoff). No new render pass, no new `RenderDevice` hooks —
this extends the existing single forward pass's `DrawParams`/push-constant
path exactly the way the point-light slice (36e-1) did.

Confirmed with the user before writing this: decomposed "Specular/PBR, spot
lights" into spot lights first (this slice), with Blinn-Phong specular, full
PBR, and real multi-light/per-frame-UBO support all named as separate,
deferred follow-ups (see `docs/project/deferred-work-backlog.md`'s Rendering
section). Also confirmed: push constants grow a third time (208 → 256 bytes,
the documented desktop-GPU ceiling, guarded by the existing device-limit
assert) rather than introducing a UBO now; spot lights do not cast shadows in
v1 (directional-only shadows, per the shadows v1 spec, stay as-is); the cone
edge is smooth (`smoothstep`), not hard-cut — a plain technical choice, not
worth its own question, since it costs nothing extra in the shader.

## Where this fits

The renderer (36a–37d, plus shadows v1) is a single forward pass with
object-space, per-draw shading. `RenderSystem::render` already finds the first
`DirectionalLight` and first `PointLight` (`active_light`/`active_point_light`,
each "first found, else absent/default") and transforms their
direction/position into each mesh's **object space** before baking them into a
208-byte push constant (`DrawParams`, `engine/render/render_device.hpp`). A
spot light is a positional light exactly like a point light (world position
derived from `WorldTransform`, not a stored field) plus a direction (also
derived from `WorldTransform`, via the rotation) and a cone. This slice adds a
third "first found" light of this shape, following the identical pattern.

## `SpotLight` component

`engine/render/spot_light.hpp` (new):

```cpp
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
// edge) -- the light is at full intensity inside `inner_angle`, smoothly fades
// to zero at `outer_angle`, and `inner_angle` must be <= `outer_angle` (not
// enforced here; a caller passing inner > outer gets an inverted, likely
// visually-wrong falloff, not a crash -- consistent with this project's
// "missing/malformed data degrades gracefully, doesn't throw" convention for
// render components).
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
```

Registered in `engine/reflection/builtin_components.hpp` under the stable name
`"SpotLight"`, alongside `PointLight`/`DirectionalLight`, so the app and Studio
share one component set (the same registration every prior light type used).

A light entity with no `WorldTransform` is skipped, consistent with every
other light/mesh component's existing convention in `RenderSystem`.

## `RenderSystem` — `active_spot_light`

A `SpotLightState` + `active_spot_light(world)`, structurally identical to the
existing `PointLightState`/`active_point_light` (first `SpotLight` whose entity
has a `WorldTransform`; absent if none):

```cpp
struct SpotLightState
{
    bool present = false;
    mosaic::foundation::math::Vec3 world_pos{};
    mosaic::foundation::math::Vec3 direction{}; // world space, normalized
    mosaic::foundation::math::Vec3 color{};     // premultiplied by intensity
    float range = 0.0f;
    float cos_inner = 1.0f; // cos(inner_angle), precomputed once here (not per-mesh/per-frag)
    float cos_outer = 1.0f; // cos(outer_angle)
};
```

`direction` comes from `wt->matrix.forward()` (the existing `Mat4::forward()`
method, already used elsewhere for exactly this "local -Z axis in world space"
extraction — no new math primitive needed). `cos_inner`/`cos_outer` are
precomputed with `std::cos` once per frame here, not recomputed per-fragment in
the shader — the shader only ever needs the cosines for its `smoothstep`
comparison, never the angles themselves.

## `DrawParams` and the object-space transform

Three new fields on `DrawParams` (`engine/render/render_device.hpp`), appended
after the existing `light_mvp` field (matching this project's established
"append, don't reorder" convention every prior lighting-field addition used):

```cpp
mosaic::foundation::math::Vec3 spot_position;  // object space
mosaic::foundation::math::Vec3 spot_direction; // object space, normalized
mosaic::foundation::math::Vec3 spot_color;     // premultiplied by intensity
float spot_range = 0.0f;   // 0 == no spot light (disabled), same convention as point_range
float spot_cos_inner = 1.0f;
float spot_cos_outer = 1.0f;
```

In `RenderSystem::render`'s main per-mesh loop, alongside the existing
point-light object-space transform block:

```cpp
Vec3 spot_position{0.0f, 0.0f, 0.0f};
Vec3 spot_direction{0.0f, 0.0f, -1.0f};
Vec3 spot_color{0.0f, 0.0f, 0.0f};
float spot_range = 0.0f;
float spot_cos_inner = 1.0f;
float spot_cos_outer = 1.0f;
if (spot.present)
{
    const Mat4 inv = Mat4::inverse_affine(wt->matrix);
    spot_position = mosaic::foundation::math::transform_point(inv, spot.world_pos);
    spot_direction = mosaic::foundation::math::normalize(
        mosaic::foundation::math::transform_direction(inv, spot.direction));
    spot_color = spot.color;
    spot_range = spot.range;
    spot_cos_inner = spot.cos_inner;
    spot_cos_outer = spot.cos_outer;
}
```

(`inv` here is computed once and reused for both the point-light and spot-light
transforms in the real implementation, since both derive from the same
`Mat4::inverse_affine(wt->matrix)` — the plan phase should fold this into a
single shared local rather than compute `inverse_affine` twice per mesh; noted
here as an implementation detail, not a behavior change.)

## Push-constant size (grown a third time — the documented ceiling, no margin left)

`PushConstants` (`modules/render_vulkan/vulkan_render_device.cpp`) is
currently 208 bytes (after shadows v1's growth). Three new `vec4`s for the
spot light (position+range, direction+cos_outer, color+cos_inner — packing
each `Vec3`+`float` pair into one `vec4` exactly like point light's
`point_position`/`point_color` already do) add 48 bytes, bringing the total to
**256 bytes** — the figure this codebase's own existing comment already cites
as "what desktop GPUs offer" for the guaranteed-beyond-Vulkan's-128-byte-
minimum budget. This is the third growth of the same struct (112 → 144 → 208 →
256), each guarded by the same `static_assert`/`vkGetPhysicalDeviceProperties`
device-limit check established at the point-light slice — unchanged here,
since the check compares against `sizeof(PushConstants)` generically and needs
no code change beyond the struct growing.

**Explicitly named, not glossed over:** 256 bytes leaves zero margin for
anything added after this slice on the push-constant path. The deferred PBR
material work and any further multi-light support are the natural forcing
function for a real per-frame/per-object UBO migration — not attempted here,
per the decomposition decision, but the next thing that touches this shading
path should not reach for a fourth push-constant growth without first
revisiting that question.

## Shading

The fragment shader (`modules/render_vulkan/shaders/cube.frag`) gains a spot
contribution, computed the same windowed inverse-square distance falloff the
point light already uses, multiplied by a smooth cone term:

```glsl
// Spot light (object space): same windowed inverse-square falloff as the point
// light, additionally scaled by a smooth cone edge (smoothstep between the
// outer and inner cosines -- full intensity inside the inner cone, smoothly
// fading to zero at the outer cone, never a hard/aliased cutoff).
vec3  toSpotL  = pc.spot_position.xyz - fragPos;
float spotD    = length(toSpotL);
float spotRange = pc.spot_position.w;
float spotAtten = 0.0;
if (spotRange > 0.0)
{
    float win = clamp(1.0 - pow(spotD / spotRange, 4.0), 0.0, 1.0);
    spotAtten = (win * win) / (spotD * spotD + 1.0);
}
float cosAngle = dot(normalize(-toSpotL), pc.spot_direction.xyz);
float coneAtten = smoothstep(pc.spot_direction.w, pc.spot_color.w, cosAngle); // (cos_outer, cos_inner)
float spotL = max(0.0, dot(n, toSpotL / max(spotD, 1e-4))) * spotAtten * coneAtten;
```

added into the existing `color` accumulation alongside `albedo * pl *
pc.point_color.xyz`, as `+ albedo * spotL * pc.spot_color.xyz`. `n` and
`fragPos` are the same object-space normal/position varyings the directional
and point-light terms already use — no new vertex-shader output needed (unlike
shadows v1, which needed a new `fragLightSpacePos` varying). `-toSpotL`
normalized gives the direction from the light to the fragment (pointing
outward from the light, matching `spot_direction`'s own "light's forward"
convention), so `cosAngle` is the cosine of the angle between the light's
forward axis and the direction toward this fragment — 1.0 dead-center in the
cone, falling toward -1.0 at the cone's back.

## Testing & verification

- **Unit-testable:** `active_spot_light`'s "first found, else absent" logic and
  the object-space position/direction transform, via `RenderSystem` +
  `FakeRenderDevice` — the same pattern
  `tests/unit/render/render_system_test.cpp`'s existing
  `"RenderSystem transforms point light into mesh object space"` test already
  established; this slice adds the spot-light analog (position AND direction
  both transform correctly, cone cosines pass through unchanged, `spot_range
  == 0` correctly signals "no spot light" the same way `point_range == 0`
  already does).
- **Not unit-testable, Vulkan/GPU-specific:** the actual cone-falloff shading
  and the push-constant size/device-limit check — verified functionally via
  `mosaic run --window`/`mosaic editor` (exit 0, clean stderr), the established
  convention for this sandbox (no GPU-backed Vulkan ICD, no GUI screenshot
  capability).

## Non-goals (this slice)

Recorded in `docs/project/deferred-work-backlog.md`'s Rendering section
(added during scoping, before this spec was written):

- Blinn-Phong specular highlights (a separate, future slice — this slice adds
  a light *type*, not a new shading *term* on top of the existing diffuse-only
  model).
- Full PBR material model (metallic/roughness, Cook-Torrance BRDF) — its own,
  larger, separately-decomposed future work.
- Spot light shadows (directional-only shadows stay as-is, per the shadows v1
  spec's own explicit scope).
- Real multi-light support / a per-frame light UBO (spot lights inherit the
  same "first found" single-instance-per-type limitation directional and
  point lights already have — not fixed by this slice).
- A hard-cutoff cone edge (deliberately not chosen — smooth falloff costs
  nothing extra and avoids visible aliasing at the cone boundary).
