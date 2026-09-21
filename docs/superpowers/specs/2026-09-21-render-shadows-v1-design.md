# Rendering — Shadows v1 (design)

**Backlog item:** "Rendering: shadows", 🔴 Large tier,
`docs/project/deferred-work-backlog.md`. No tracked GitHub issue yet. **Date:**
2026-09-21.

## Summary

Add real-time shadowing for the renderer's single directional light: a depth-only
shadow map rendered from the light's point of view each frame, sampled during the
existing forward shading pass to attenuate the Lambert term. Scope is deliberately
narrow — **directional-light-only, hard-edged (no PCF), a fixed-size orthographic
frustum** — proving the pipeline (light-space transform, depth pre-pass, shadow
sampling with bias) before any of the fancier variants. Every simplification named
below has a corresponding entry in `docs/project/deferred-work-backlog.md`'s
Rendering section, added during scoping — see that file for the full list and
rationale; this spec doesn't repeat it.

Confirmed with the user before writing this: extend **direct submission** (the
36e-1/37d pattern) rather than build the `RenderGraph`, directional-only (not
point-light) shadows, hard-edged (not PCF) filtering, and a fixed-size ortho box
(not scene/camera-fitted).

## Where this fits

The renderer (36a–37d) is a single forward pass with object-space, per-draw
shading: `RenderSystem::render` finds the first `DirectionalLight` and first
`PointLight`, transforms their direction/position into each mesh's **object
space**, and bakes shading inputs into a 144-byte push constant (`DrawParams` in
`engine/render/render_device.hpp`) — chosen specifically so the backend needs no
model matrix. `mosaic editor` already runs **two** direct-submission passes (scene
→ offscreen, then ImGui → swapchain, from 37d); `mosaic run --window` runs one
(scene → swapchain directly). Both entry points share one `RenderDevice`.

Shadows add a **new pass before the existing scene pass** in both entry points:
render scene depth from the light's view into a dedicated shadow map, then the
existing scene pass samples it. This brings `mosaic editor` to **three** passes
and `mosaic run --window` to **two** — see *The render-graph question* below.

## The render-graph question (re-confirmed, not reopened)

37d named "a shadow-map depth pre-pass" as the kind of slice that would justify
building the `RenderGraph` for real. This is that slice, and the question was
re-asked rather than assumed. **Decision (confirmed with the user): extend direct
submission again**, the same way 37d extended 36e-1's single pass to two. The
shadow pass has a **fixed, always-present position in the ordering** (always
first, always exactly one) — it doesn't introduce the *dynamic or reorderable*
pass structure that would actually strain hand-written ordering. The three passes
(shadow → offscreen scene → swapchain, editor; shadow → scene, `run --window`)
are recorded into one command buffer and submitted once, same as 37d. Inter-pass
hazards (shadow depth image becoming shader-readable before the scene pass reads
it; scene color image becoming shader-readable before the UI pass reads it, in
the editor) are handled the same declarative way 37d established: pass
`finalLayout`s plus subpass dependencies, not hand-authored `vkCmdPipelineBarrier`
calls.

This is now the closest the project has come to the graph's own named trigger.
`docs/project/deferred-work-backlog.md` records that the next time a *dynamic*
pass need appears (conditional passes, a variable-length chain), that's the
actual bar — not "one more fixed pass," which direct submission keeps handling
fine.

## Data model

**No new component.** Shadow casting is implicit whenever a `DirectionalLight`
exists in the world, mirroring `RenderSystem::active_light`'s existing "first
found, else a default" pattern — there is exactly one shadow-casting light in v1,
matching the existing one-directional-light model exactly. A per-light
enable/disable toggle is unnecessary scope for a renderer that only supports one
directional light at all today.

## Foundation math: `Mat4::orthographic`

The light's projection is orthographic (parallel rays, no perspective divide) —
`foundation/math/mat4.hpp` gains `Mat4::orthographic(left, right, bottom, top,
near_z, far_z)`, a new primitive alongside the existing `perspective`/`look_at`,
following the same pattern every prior rendering slice used to add exactly the
math primitive it needed (36b added `perspective`, 36c added `inverse_affine`,
36d-1 added `transform_direction`). Fully unit-testable in isolation like every
other `foundation/math` function — no Vulkan/GPU dependency.

## The shadow frustum (fixed-size, v1)

A fixed-size orthographic volume, centered on the origin, generously sized for
the project's current demo/test scenes (exact half-extent is an implementation
constant, tuned during the plan/implementation phase against the real demo
scene — not hardcoded to one number picked here blind). The light's view matrix
is `Mat4::look_at(light_position, light_position + direction, up)`, where
`light_position` is chosen far enough back along `-direction` from the origin to
keep the whole fixed volume in front of the near plane — a deterministic
function of `DirectionalLight::direction` and the frustum's own half-extent, not
scene-dependent.

**Known limitation, not built now:** this does not adapt to scene size or
position — a scene bigger than the fixed box, or not centered near the origin,
will have incorrectly clipped or missing shadows. Scene/camera-fitted (and
eventually cascaded) frustums are a named, deferred follow-up (see the backlog).

## `RenderDevice` interface changes

Following the established pattern (new virtual hooks, default no-op, only the
Vulkan backend implements them — e.g. 37d's four viewport hooks):

```cpp
// Shadow pass: render depth-only geometry from the light's point of view into a
// dedicated shadow map. Default no-op backends (or any backend not yet supporting
// shadows) simply skip the shadow map -- draw_shadow calls become inert, and the
// main pass's shadow sampling degrades to "fully lit" (see Shading below).
virtual void begin_shadow_pass(const Mat4& light_view_proj) {}
virtual void draw_shadow(MeshHandle mesh, const Mat4& light_mvp) {}
virtual void end_shadow_pass() {}
```

`RenderSystem::render` gains a shadow pass loop, run once before the existing
main-pass loop: for each `MeshRenderer` entity with a `WorldTransform`, call
`draw_shadow(mesh, light_view_proj * wt->matrix)`. This mirrors the existing main
loop's mesh iteration exactly (same mesh-cache lookup, same nil/missing-mesh
skip), just without material/texture lookups — the shadow pass only needs
position.

`DrawParams` (used by the existing main-pass `draw` call) gains one field:

```cpp
Mat4 light_mvp; // light_view_proj * model, for shadow-map sampling in the fragment shader
```

## Push-constant size (a real, called-out risk)

`DrawParams` is currently 144 bytes (grown once already, at the point-light
slice, behind a `static_assert` against the device's `maxPushConstantsSize`).
Adding one `Mat4` (64 bytes) for `light_mvp` brings it to 208 bytes. Vulkan
guarantees only 128 bytes minimum; the existing 144-byte push constant already
relies on more than that guarantee, confirmed to work via the existing device
assert on this project's target hardware. 208 bytes is a further step past the
guaranteed minimum.

**Decision:** grow it anyway, guarded by the same `static_assert`-style device
check the point-light slice already established, consistent with this project's
own precedent rather than introducing new machinery (a per-draw UBO/dynamic
buffer) to avoid a risk that hasn't materialized yet. **If the assert ever fails
on real target hardware** (verified only by running there, not by more reading),
the fallback is a small per-draw dynamic uniform buffer for `light_mvp` instead
of a push-constant field — noted here as the concrete next step if needed, not
built speculatively now.

## Shading

The main fragment shader gains: sample the shadow map at the fragment's
light-clip-space position (computed in the vertex shader from `light_mvp` and
the object-space vertex position, interpolated to the fragment stage — the same
technique the existing `mvp`-to-`gl_Position` transform already uses, just a
second transform run in parallel), compare its depth against the shadow map's
stored depth, and apply a **constant + slope-scaled bias** (to avoid shadow-acne
self-shadowing — a standard, small, tunable pair of constants, not a novel
technique) before multiplying the existing directional Lambert term by 0 or 1
(hard shadow, v1). The point light's contribution is unaffected — it does not
cast shadows in v1. A fragment whose light-clip-space position falls outside the
shadow map's `[0,1]` UV range (outside the fixed frustum) is treated as **not
shadowed** (falls back to fully lit) rather than erroring — consistent with the
project's existing "missing data degrades gracefully" convention (e.g. a missing
mesh/texture already skips rather than throws).

## Vulkan backend

One new depth-only render pass (a single depth attachment, no color, no fragment
shader stage needed), a fixed-resolution depth image + image view + sampler
(resolution is an implementation constant, not user-configurable in v1 — a
configurable-resolution knob is a named, deferred follow-up), and one new
graphics pipeline (position-only vertex input is sufficient, but reusing the
existing full `Vertex` layout is simpler and avoids a second vertex buffer
binding — the unused normal/uv attributes cost nothing at this scale). The main
pass's existing descriptor set(s) gain one more binding: the shadow map + a
comparison-capable sampler, so the fragment shader can sample it directly.

## Testing & verification

- **Unit-testable:** `Mat4::orthographic` (frustum math, like every other
  `foundation/math` primitive), and the light-space matrix construction
  (`light_view_proj` from a `DirectionalLight` + the fixed frustum constants) —
  pure math, no Vulkan dependency, following `tests/unit/math/`'s existing
  pattern.
- **Not unit-testable, Vulkan/GPU-specific:** the actual shadow pass, sampling,
  and bias — verified functionally via `mosaic run --window` / `mosaic editor`
  (exit 0, clean stderr), the established convention for this sandbox, which has
  no GPU-backed Vulkan ICD and cannot screenshot the GUI window. Visual
  correctness (does the shadow actually look right) needs the user's own
  hardware to confirm — named explicitly here rather than silently assumed.

## Non-goals (v1)

All of the following are recorded in `docs/project/deferred-work-backlog.md`'s
Rendering section (added during scoping, before this spec was written):

- PCF / soft shadow filtering (v1 is hard-edged, single-sample).
- Point-light shadows (cubemap/omnidirectional — a different technique, roughly
  doubling scope).
- Scene/camera-fitted and eventually cascaded shadow frustums (v1 is a fixed-size
  box centered on the origin).
- Configurable shadow map resolution (v1 is a fixed implementation constant).
- The `RenderGraph` (still deferred; see *The render-graph question* above).
- A general per-frame light UBO / multi-light support (unrelated to shadows
  specifically — the existing one-directional-plus-one-point-light model is
  unchanged by this work).
