# Rendering — Shading-Data UBO Migration (design)

**Backlog item:** "Shading-data UBO migration", 🔴 Large tier,
`docs/project/deferred-work-backlog.md` (Rendering section). No tracked GitHub
issue yet. **Date:** 2026-09-27.

## Summary

Move per-frame lighting data off push constants into a real uniform buffer, and
move the shading math from **object space to world space**. Per-draw push
constants shrink from **256 bytes (the hard ceiling) to 208**, restoring
headroom; the light data is written **once per frame** into a small UBO instead
of being re-transformed into every mesh's object space and re-pushed per draw.

This is the forcing function both the shadows v1 and spot-lights specs
predicted in writing. It was picked as its own slice, ahead of the Blinn-Phong
specular work that triggered it, so that a substantial shading refactor stays
reviewable on its own rather than being bundled with a new feature.

Confirmed with the user before writing this:
- **Do the migration as its own slice first**, with specular following on a
  clean foundation — rather than squeezing specular into the last 4 padding
  floats (which would have cost authorable specular strength and spent every
  remaining byte for a partial feature).
- **World-space shading + a per-frame light UBO**, rather than keeping
  object-space shading and adding a per-draw dynamic UBO ring buffer. The
  world-space route needs *less* Vulkan machinery, not more, and removes the
  per-mesh `inverse_affine` work rather than perpetuating it.

## Where this fits

The renderer today (36a–37d, shadows v1, spot lights) does object-space,
per-draw shading: `RenderSystem::render` transforms the directional light's
direction, the point light's position, and the spot light's position+direction
into **each mesh's object space**, then bakes all of it into a 256-byte push
constant per draw. That design was deliberate early on — it meant the backend
needed no model matrix — but it is why the push constant is full: nearly every
field is per-draw *only because it was transformed per-mesh*.

In world space, none of that light data varies per draw. It becomes per-frame
data written once, and the per-draw payload collapses to the matrices and
material color that genuinely do vary.

## What moves where

**New per-frame `FrameLighting`** (`engine/render/render_device.hpp`),
backend-neutral, all in **world space**:

```cpp
struct FrameLighting
{
    mosaic::foundation::math::Vec3 light_direction;  // world space, normalized
    mosaic::foundation::math::Vec3 light_color;
    mosaic::foundation::math::Vec3 point_position;   // world space
    mosaic::foundation::math::Vec3 point_color;      // premultiplied by intensity
    float                          point_range = 0.0f; // 0 == no point light
    mosaic::foundation::math::Vec3 spot_position;    // world space
    mosaic::foundation::math::Vec3 spot_direction;   // world space, normalized
    mosaic::foundation::math::Vec3 spot_color;       // premultiplied by intensity
    float                          spot_range = 0.0f;  // 0 == no spot light
    float                          spot_cos_inner = 1.0f;
    float                          spot_cos_outer = 1.0f;
    mosaic::foundation::math::Mat4 light_view_proj;  // for shadow-map sampling
};
```

**`DrawParams` shrinks** to only what genuinely varies per draw:

```cpp
struct DrawParams
{
    mosaic::foundation::math::Mat4 mvp;
    mosaic::foundation::math::Mat4 model;         // object -> world
    mosaic::foundation::math::Mat4 normal_matrix; // transpose(inverse_affine(model)); upper 3x3 used
    mosaic::foundation::math::Vec3 base_color;
};
```

`light_mvp` disappears from the per-draw payload entirely — the vertex shader
now computes the light-space position as `light_view_proj (UBO) * worldPos`,
which alone saves 64 bytes.

**Push-constant budget:** `mat4 mvp` (64) + `mat4 model` (64) + `mat4
normal_matrix` (64) + `vec4 base_color` (16) = **208 bytes**, down from 256,
leaving 48 bytes of headroom.

A `mat3` normal matrix would be 48 bytes rather than 64 under std140 (three
columns each padded to `vec4`), saving a further 16 — **deliberately not done**:
std140 `mat3` column padding is a well-known silent-corruption footgun, and 16
bytes is cheap insurance when 48 bytes of headroom already comfortably covers
what specular needs next (see below). Passing a full `mat4` keeps the layout
boring.

**Headroom check against the work this unblocks:** Blinn-Phong specular needs
an object-space eye position (3 floats) — except in world space the camera
position is *per-frame*, so it goes in the UBO, not push constants. Its only
per-draw additions are shininess and specular strength (2 floats, 8 bytes,
which fit in existing padding). The 48 bytes of new headroom is ample.

## Normal matrix

World-space shading requires normals transformed by the inverse-transpose of
the model matrix, not the model matrix itself, or non-uniformly-scaled meshes
shade incorrectly. `RenderSystem` computes it CPU-side per draw as
`transpose(inverse_affine(model))`, reusing the existing `Mat4::inverse_affine`.

`foundation/math/mat4.hpp` gains **`Mat4::transpose()`** — a new, general,
trivially unit-testable primitive, following the pattern where each rendering
slice adds exactly the math it needs (36b added `perspective`/`look_at`, 36c
`inverse_affine`, 36d-1 `transform_direction`, shadows v1 `orthographic`).

Doing normals properly here is what makes the non-uniform-scale story coherent:
this migration already fixes attenuation distance under non-uniform scale (see
*Intentional behavior change*), and fixing that while leaving normals wrong
would be a half-fix.

## `RenderDevice` interface changes

One new hook, default no-op, matching the established pattern (every prior
rendering addition — the viewport hooks, the shadow hooks — did the same):

```cpp
// Per-frame, world-space lighting shared by every draw this frame. Called once
// before the frame's mesh draws; the backend uploads it to a uniform buffer the
// shading pipeline reads. Default no-op: a backend without a UBO path simply
// renders unlit rather than failing.
virtual void set_frame_lighting(const FrameLighting& /*lighting*/) {}
```

`RenderSystem::render` calls it once, before the mesh loop, built from the same
`active_light`/`active_point_light`/`active_spot_light` accessors that exist
today (their world-space values are used directly now instead of being
per-mesh-transformed). The per-mesh loop then only computes `mvp`, `model`,
`normal_matrix`, and `base_color`.

`RenderSystem::render_shadows` is **unchanged** — the depth-only shadow pass has
its own 64-byte `ShadowPushConstants` and its own pipeline, neither of which is
touched by this migration. Both `render_shadows` and the `FrameLighting`'s
`light_view_proj` derive from the same `directional_light_view_projection(light)`
helper, so the shadow map and its sampling stay in agreement exactly as today.

## Vulkan backend

This introduces the **first uniform buffer in the codebase** (existing
host-visible mapped buffers are vertex/index/staging only; all descriptor sets
so far are combined image samplers).

- One host-visible, host-coherent, **persistently-mapped** uniform buffer **per
  frame-in-flight** (`kFramesInFlight = 2`). Per-frame-in-flight is what makes
  the write safe: writing frame N's lighting cannot race a still-executing frame
  N-1 that reads its own buffer.
- A new descriptor set layout, pool, and one set per frame-in-flight, bound as
  **set 2** (set 0 is the per-material texture sampler, set 1 the shadow map).
  The main pipeline layout grows from two set layouts to three.
- `set_frame_lighting` is a plain CPU `memcpy` into the current frame's mapped
  buffer — no command buffer, no render-pass restriction, so it is safe wherever
  in the frame `RenderSystem` calls it.
- Set 2 is bound in `draw()` alongside sets 0 and 1, matching the existing code
  shape rather than introducing a separate bind point (the per-draw rebind of an
  unchanging set is negligible and keeps the change local).
- The struct written to the UBO uses explicit `vec4`-aligned padding matching
  the shader's std140 block, with a `static_assert` on its size — the same
  guard-the-layout discipline the push-constant path already uses.

## Shading

`cube.vert` outputs **world-space** position and normal (`model * pos`,
`normal_matrix * normal`) plus the light-space position computed from the UBO's
`light_view_proj`. `cube.frag` does all three light types' math in world space,
reading the UBO — the falloff formulas themselves (windowed inverse-square
distance attenuation, the spot cone `smoothstep`, the shadow lookup and bias)
are **carried over unchanged**; only the space they operate in changes.

## Intentional behavior change

Point- and spot-light attenuation distance is currently measured in **object
space**, so non-uniform mesh scale warps it — a limitation the point-light slice
(36e-1) documented explicitly at the time. In world space the distance is
correct. Normals likewise become correct under non-uniform scale.

Output is **identical for uniformly-scaled objects**, which is what the demo and
test scenes use — so this is a latent-bug fix, not a visible change to existing
content.

## Testing & verification

**The main risk is test churn, and it is worth naming plainly.** Every existing
`RenderSystem` lighting test asserts *object-space* values and must be rewritten
to world space:

- `"RenderSystem produces lit DrawParams from Material + DirectionalLight"`
- `"RenderSystem::render_shadows records the shadow pass with the right
  light_mvp, matching render()'s own light_mvp"` (the `DrawParams::light_mvp`
  half of this assertion goes away with the field; the shadow-pass half stays)
- `"RenderSystem uses default base color and light when none present"`
- `"RenderSystem transforms point light into mesh object space"`
- `"RenderSystem transforms spot light position and direction into mesh object
  space"`
- `"RenderSystem disables spot light when none present"`
- `"RenderSystem disables point light when none present"`

Rewriting tests to match new behavior is exactly how a genuine regression hides.
Three mitigations, all required:

1. **The new assertions are simpler and independently derivable** — world-space
   expected values are the raw light values as authored, with no transform to
   re-derive. A wrong implementation has nowhere to hide behind matching
   arithmetic on both sides.
2. **Keep the deliberately-rotated mesh setups.** The existing tests place the
   mesh at a 90°-about-Y rotation specifically so an object-space transform is
   observable. Those setups stay, now asserting the light values are
   *unchanged* by that rotation — so a stray leftover transform still fails the
   test.
3. **New coverage for the split itself:** that `set_frame_lighting` is called
   exactly once per `render()` with the right world-space values, that the
   per-draw `normal_matrix` is the inverse-transpose of a non-uniformly-scaled
   model (a case the current suite has no coverage for at all), and
   `Mat4::transpose` unit tests.

**Not unit-testable, Vulkan/GPU-specific:** the UBO upload, descriptor binding,
and the shaders — verified functionally via `mosaic run --window` and `mosaic
editor` (exit 0, clean stderr, no Vulkan validation errors), the established
convention for this sandbox. As with every prior rendering slice, confirming it
*looks* right needs real hardware outside this sandbox.

## Non-goals

Recorded in `docs/project/deferred-work-backlog.md`'s Rendering section:

- **Blinn-Phong specular** — the feature that forced this migration, deliberately
  following it as its own slice rather than being bundled in.
- **Real multi-light support** — this slice changes *where* the existing
  single-light-per-type data lives, not *how many* lights there are. The
  "first found" limitation for each of the three light types is unchanged.
- **Moving `view_proj` per-frame and dropping `mvp` from push constants** — a
  further available reduction (would take the per-draw payload to ~144 bytes)
  but unnecessary: this slice's job is the lighting data, and 48 bytes of
  headroom already covers what is queued behind it.
- **Full PBR**, **spot/point-light shadows**, **RenderGraph** — all unchanged,
  still tracked separately.
