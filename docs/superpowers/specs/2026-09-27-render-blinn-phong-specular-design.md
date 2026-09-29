# Rendering — Blinn-Phong Specular (design)

**Date:** 2026-09-27
**Status:** approved (design), pending implementation plan
**Predecessors:** shadows v1 (PR #200), spot lights (PR #201), shading-data
UBO migration (PR #202)

## Intent

Add a view-dependent specular highlight to the existing forward shading
path, so surfaces stop reading as uniformly matte. This is the last
obvious "toy renderer" gap before a real PBR material model, and the
smallest remaining rendering slice: the UBO migration already paid for
the headroom this needs.

**Success criteria**

- Every material can be authored from matte to glossy, persisted in a
  scene file, and edited through the existing reflection/MCP surface.
- Highlights track the camera, and move when the camera moves.
- The three existing diffuse terms and the shadow term are unchanged.
  A matte material (`specular_strength = 0`) renders byte-identically
  to today.
- It stays a small slice: no new descriptor set, no new buffer, no new
  binding, no new render pass.

## Decisions

Confirmed with the user before design, not assumed:

1. **All three lights get specular** — directional, point, and spot.
   Each reuses the attenuation, cone, and shadow factors already
   computed for its own diffuse term. Directional-only was considered
   and rejected: it leaves point and spot lights visibly inconsistent
   with the sun, and adding them later reopens this same shader and
   re-reviews this same math.
2. **Both parameters are authorable on `Material`** — `shininess` and
   `specular_strength`, reflected and serialized. Shininess-only and
   hardcoded-constants were both considered and rejected: "how tight"
   and "how bright" are genuinely different knobs, and a material
   system whose materials cannot differ from each other defeats itself.
3. **Scalars ride in push constants; the camera position rides in the
   frame UBO.** Two alternatives were rejected:
   - *A second UBO / storage buffer for material data.* Anticipates
     PBR, but we have 40 bytes of push-constant headroom, exactly one
     material struct, and no knowledge yet of PBR's actual data shape.
     This is the RenderGraph mistake in miniature — building the
     general mechanism before the need is real. PBR can migrate later,
     knowing more than we do now.
   - *Packing into existing padding* (`base_color.w` plus an unused
     cell of `normal_matrix`). Grows nothing, but hiding a material
     parameter inside a matrix is precisely the practice that made the
     UBO migration necessary one slice ago.

## Data layout

The layout is the risk in this slice (see **Risks**), so it is written
out explicitly rather than left implicit in the code.

### `PushConstants` — 208 → 216 bytes

Push-constant blocks use std430, so the two trailing floats pack tight
after the `vec4` with no padding.

| Field | Offset | Size |
|---|---|---|
| `mvp` (`mat4`) | 0 | 64 |
| `model` (`mat4`) | 64 | 64 |
| `normal_matrix` (`mat4`) | 128 | 64 |
| `base_color` (`vec4`, `.w` unused) | 192 | 16 |
| `shininess` (`float`) | 208 | 4 |
| `specular_strength` (`float`) | 212 | 4 |

`static_assert(sizeof(PushConstants) == 216)`. The existing runtime
`maxPushConstantsSize` check needs no change — it already compares
against `sizeof(PushConstants)`. 216 is comfortably under the 256-byte
desktop floor; 40 bytes remain.

### `FrameLightingUbo` — 176 → 192 bytes

`camera_position` is inserted **before** `light_view_proj`, keeping the
`vec4` block contiguous. This shifts `light_view_proj` from offset 112
to 128 — both GLSL blocks and the C++ struct must move together.

| Field | Offset | Size |
|---|---|---|
| `light_direction` (`vec4`) | 0 | 16 |
| `light_color` (`vec4`) | 16 | 16 |
| `point_position` (`vec4`, `.w` = range) | 32 | 16 |
| `point_color` (`vec4`) | 48 | 16 |
| `spot_position` (`vec4`, `.w` = range) | 64 | 16 |
| `spot_direction` (`vec4`, `.w` = cos outer) | 80 | 16 |
| `spot_color` (`vec4`, `.w` = cos inner) | 96 | 16 |
| **`camera_position` (`vec4`, `.w` unused)** | **112** | **16** |
| `light_view_proj` (`mat4`) | 128 | 64 |

`static_assert(sizeof(FrameLightingUbo) == 192)`.

### Engine-side structs

```cpp
// engine/render/material.hpp
struct Material
{
    Vec3    base_color{0.8f, 0.8f, 0.8f};
    AssetID texture{};               // nil = untextured
    float   shininess = 32.0f;       // Phong exponent; clamped to >= 1 when shading
    float   specular_strength = 0.5f; // 0 = matte
};
```

New fields go **after** `texture` so the existing aggregate
initialization `Material mat{Vec3{...}}` in
`tests/unit/reflection/lighting_reflection_test.cpp` keeps compiling.
`reflect()` gains a `v.member(...)` line per field, which is what makes
them serialize and appear in the MCP component surface and the
generated `docs/reference/components.md`.

`FrameLighting` gains `Vec3 camera_position;` (world space), placed
before `light_view_proj` to mirror the UBO order. `DrawParams` gains
`float shininess = 32.0f; float specular_strength = 0.5f;` after
`base_color` — defaulted, so the two `DrawParams{}` uses in the test
suite are unaffected.

## Shading math

```glsl
// Blinn-Phong specular for one light. L points from the fragment
// TOWARD the light and is unit length.
float blinn_spec(vec3 n, vec3 v, vec3 L, float shininess, float strength)
{
    vec3  hv = v + L;
    // v == -L (viewer exactly opposite the light) makes hv zero-length and
    // normalize() produce NaN, which would propagate into the output color
    // as an undiagnosable black or white pixel. Fall back to the normal.
    vec3  h  = (dot(hv, hv) > 1e-8) ? normalize(hv) : n;
    // pow(x, 0.0) is 1 everywhere (and pow(0,0) is undefined), so a zero
    // exponent would wash the whole surface; clamp rather than trusting input.
    float e  = max(shininess, 1.0);
    return pow(max(dot(n, h), 0.0), e) * strength * step(0.0, dot(n, L));
}
```

The view vector is computed once: `vec3 v = normalize(fl.camera_position.xyz - fragPos);`

`shadow_factor()` is currently called inline inside the `lambert`
expression. It moves to a local `float shadow = shadow_factor();`
computed once and used by both the directional diffuse and the
directional specular — the same value, not a second call.

Per-light `L`:

- directional: `L = -normalize(fl.light_direction.xyz)` (the stored
  direction points *from* the light)
- point: `L = toL / max(d, 1e-4)` — the existing expression
- spot: `L = toSpotL / max(spotD, 1e-4)` — the existing expression

Final composition:

```glsl
vec3 color = albedo * (0.1 + lambert * fl.light_color.xyz)
           + albedo * pl     * fl.point_color.xyz
           + albedo * spotL  * fl.spot_color.xyz
           + spec_dir  * fl.light_color.xyz
           + spec_pt   * fl.point_color.xyz
           + spec_spot * fl.spot_color.xyz;
```

where `spec_dir = blinn_spec(n, v, -l, ...) * shadow`,
`spec_pt = blinn_spec(n, v, Lp, ...) * atten`, and
`spec_spot = blinn_spec(n, v, Ls, ...) * spotAtten * coneAtten`.

Four properties of this composition are deliberate, each a known trap:

- **Specular is multiplied by light color, never by `albedo`.** A red
  plastic ball gets a white highlight under a white light. Tinting
  specular by albedo is the most common Blinn-Phong error and makes
  every surface read as painted metal.
- **The `step(0.0, dot(n, L))` gate.** Without it the half-vector
  produces highlights on geometry facing away from the light, which
  reads as light leaking through the object.
- **Each light's specular is gated by that light's own factors.**
  Directional specular is multiplied by `shadow` — shadowed ground
  with a sun glint on it is a glaring artifact. Point and spot
  specular carry their windowed attenuation, and spot carries its cone
  `smoothstep`.
- **Ambient stays `0.1 * albedo`.** Specular from ambient is
  image-based lighting; it belongs to PBR (tracked in the backlog).

## RenderSystem changes

`Mat4::inverse_affine(view)` yields the camera's world matrix; its
translation is the camera position:

```cpp
const Mat4 cam_world = Mat4::inverse_affine(view);
frame_lighting.camera_position = Vec3{cam_world.m[12], cam_world.m[13], cam_world.m[14]};
```

This works identically for the fallback camera path, where the eye is
`{0, 1.5, 4}` per `fallback_view()`.

**Targeted cleanup, in scope:** the particle pass already computes
`const Mat4 cam_world = Mat4::inverse_affine(view);` inside its own
block. `cam_world` is now needed unconditionally for lighting, so it is
hoisted above the `FrameLighting` construction and the particle pass
reuses it. This removes a duplicate computation rather than adding a
second one. No other behavior changes.

Per-draw, the two material parameters are read alongside `base_color`,
using the same defaults as the no-`Material` path:

```cpp
Vec3  base_color{0.8f, 0.8f, 0.8f};
float shininess = 32.0f;
float specular_strength = 0.5f;
if (const Material* mat = world.get_component<Material>(e)) { /* ...all three... */ }
```

## Testing

The `RenderSystem` tests capture `DrawParams` and `FrameLighting`
through a fake device, so everything except the shader arithmetic is
directly assertable.

1. `Material`'s `shininess`/`specular_strength` reach `DrawParams`.
2. An entity with **no** `Material` draws with the documented defaults
   (32.0, 0.5) — the same fallback as `base_color`.
3. `FrameLighting::camera_position` equals the camera entity's world
   position.
4. `camera_position` is correct on the **fallback** camera path (no
   `Camera` component) — golden value `{0, 1.5, 4}`. This is the case
   that gets missed, because the fallback only supplies a view matrix
   and the position has to be recovered from it.
5. `Material` round-trips both new fields through scene
   serialization — extend the existing case in
   `tests/unit/reflection/lighting_reflection_test.cpp` rather than
   adding a parallel one.

The shader math is not unit-testable in this harness. It is verified
the way the last three slices were: both real entry points
(`mosaic run --window`, `mosaic editor`) running clean with **zero
Vulkan validation errors**. For a change that alters a push-constant
layout and a descriptor-bound UBO layout, validation silence is the
signal that matters.

## Risks

**The layout change is the risk.** `PushConstants` grows a fourth time,
and `FrameLightingUbo` grows for the first time — with
`light_view_proj` *moving*, which is worse than merely appending. A
mismatch between the C++ struct and the GLSL blocks does not crash; it
renders subtly wrong, and the two most likely symptoms (a black screen
from a garbage `light_view_proj`, or highlights in the wrong place) are
easy to misattribute to the new shading math instead of the layout.

Mitigations:

- The `static_assert`s catch C++-side drift but **cannot see the
  shaders**. The two GLSL blocks must stay byte-identical to each
  other and to the table above; they are checked by hand against the
  table, which is why the table exists.
- `light_view_proj` moving means **shadows are the canary**. If the
  offsets are wrong, shadows break visibly and immediately — verify
  shadows still render before trusting any specular observation.
- Verify a `specular_strength = 0` material is unchanged from today
  before evaluating the new highlights.

## Non-goals

All are recorded in `docs/project/deferred-work-backlog.md` (Rendering
section) with their rationale:

- Specular/gloss **texture maps** — v1 is scalar-only.
- **Fresnel / energy conservation** — folded into full PBR;
  `specular_strength` is an authored fudge factor, not an `F0`.
- **Per-light specular color** distinct from diffuse color.
- **Ambient/environment specular (IBL)**.
- A **perceptual `0..1` gloss remap.** `shininess` ships as a raw Phong
  exponent, which is the traditional parameterization but a poor
  authoring knob — it is perceptually non-linear (8→16 is a large
  change, 200→208 is invisible). Deferred deliberately: the remap
  decision belongs with PBR's roughness parameter, and shipping both
  would leave two competing gloss scales in the material system.
- **Multiple simultaneous lights per type** — unchanged "first found"
  behavior, tracked separately.
