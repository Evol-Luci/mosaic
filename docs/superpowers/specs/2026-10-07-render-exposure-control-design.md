# Rendering — Exposure Control (design)

**Date:** 2026-10-07
**Status:** approved design (knob location chosen by the user), not yet implemented
**Backlog item:** "Exposure control" (`docs/project/deferred-work-backlog.md`, Rendering),
deferred 2026-10-04 during the tone-mapping design, listed 🟢 in the effort/impact tiers.

## Intent

Tone mapping ships with a hard-coded curve and no way to set the exposure feeding it, so a
scene authored brighter or darker than the curve expects has no recourse but re-authoring
every light. This adds one knob.

The backlog deferred it because "adding the knob now would mean deciding where it lives (a
`Camera` member? the lighting UBO? a push constant?) for no current caller." **The decision
is: a field on the `Camera` component.** Exposure is a camera property physically and in
every DCC tool; a `Camera` field serialises with the scene, is per-camera rather than
per-process, and appears in the reflection inspector with no new code.

## What scoping measured, and the one trap

**`camera_position.w` is already taken.** The backlog entry says exposure is "one `float`
through the existing lighting UBO (which already carries `camera_position`)", implying there
is a free padding slot. There is not: slice 4c put the IBL-enabled flag in `camera_position.w`,
and `vulkan_render_device.cpp` carries an explicit warning that "a future field that takes
this slot silently disables image-based lighting." This slice is that future field. It does
not take the slot.

**`light_direction.w`, `light_color.w` and `point_color.w` are documented unused** and could
hold it. Rejected: packing a camera property into an unrelated light vector's padding is
exactly the move whose cost `camera_position.w` is currently demonstrating. Instead the UBO
**grows by one `vec4`**, 192 → 208 bytes, appended **after `light_view_proj`**.

Appended at the end rather than grouped next to `camera_position`, where it belongs
logically, for one reason: `static_assert(sizeof(FrameLightingUbo) == 192)` checks the
**total size only, never the field offsets**. Inserting mid-struct would shift
`light_view_proj` on one side of the C++/GLSL boundary and the assertion could not see a
mismatch — it would show up as a wrong shadow matrix. Appending cannot shift anything.

**The skybox does not bind the UBO.** `skybox.frag` takes `set = 0` for the environment
cubemap and gets everything else through push constants, so it cannot read `fl.exposure`.
It gets `float exposure` added to `SkyboxPush` instead (72 → 76 bytes).

## Which shaders apply it, and why not the others

Exposure applies **exactly where tone mapping applies**: `cube.frag` and `skybox.frag`,
the two shaders that carry `tonemap_aces`. That is not a coincidence to be tidied up later
— exposure is the scale of the value entering the curve, so a shader with no curve has
nothing to scale.

The four unlit scene shaders (`particle`, `ribbon`, `decal`, `world_text`) are deliberately
**not** exposed, for the same reason slice 1 recorded for not tone mapping them: they emit
authored colour that cannot exceed 1.0, and scaling it would change what the author asked
for. A test already pins that they are not tone mapped; this slice does not disturb it.
Recorded in the backlog as a known consequence rather than an oversight — a bright scene
exposed down will make particles read relatively *brighter* against it.

**Linear multiplier, not EV stops.** `color * exposure`, with `exposure = 1.0` neutral. EV
stops (`color * pow(2, ev)`) are the photographer's unit and are better for a UI slider,
but they put a `pow` in two fragment shaders and make the neutral value 0.0, which reads
as "no exposure" rather than "unchanged" to anyone inspecting a component. The conversion
belongs in a UI affordance if one is ever built; recorded in the backlog.

## The change

- `engine/render/camera.hpp`: `float exposure = 1.0f;` plus its `reflect()` line.
- `engine/render/render_device.hpp`: `float exposure = 1.0f;` on `FrameLighting`.
- `engine/render/render_system.hpp`: `find_active_camera` gains a `float& exposure`
  out-parameter, so the existing single traversal carries it; the no-camera fallback
  leaves it at 1.0.
- `modules/render_vulkan/vulkan_render_device.cpp`: `FrameLightingUbo` gains
  `float exposure[4]` (xyz unused) at the end, size assertion 192 → 208; `SkyboxPush`
  gains `float exposure`, 72 → 76.
- `cube.frag`: the `FrameLighting` block gains the trailing `vec4 exposure`, and the final
  write becomes `outColor = vec4(tonemap_aces(color * fl.exposure.x), 1.0);`
- `skybox.frag`: `pc.exposure` in its push block, and
  `outColor = vec4(tonemap_aces(radiance * pc.exposure), 1.0);`

**`exposure = 1.0` must render byte-identically to today.** That is the migration story:
nothing authored moves, the committed scene's `Camera` gains a field whose default is
neutral, and a scene file written before this slice loads with exposure 1.0.

## Testing

1. **`Camera` round-trips `exposure`** — extending `camera_reflection_test.cpp`'s existing
   case, which uses aggregate initialisation and so keeps compiling with a fourth defaulted
   field.
2. **The active camera's exposure reaches `FrameLighting`** — a `render_system_test` case
   with a non-default exposure on the camera, asserting the value the fake device received.
3. **No camera leaves exposure at 1.0** — the fallback path, which otherwise would ship
   whatever the uninitialised out-param held.
4. **`exposure` multiplies the colour BEFORE the tone map, in both shaders.** This is the
   sharp one, and the naive form of it is worthless: asserting that `exposure` *appears*
   is satisfied by `tonemap_aces(color) * fl.exposure.x`, which applies the scale to an
   already-clamped `[0,1]` value — it would darken but never brighten, and every token
   search stays green. The assertion pins the full expression, whitespace-normalised, in
   each shader.
5. **The two shaders use the same neutral value.** A contract case pinning that neither
   shader has a hard-coded exposure constant, so the skybox and the lit geometry cannot
   drift apart in brightness — the failure that would read as "the sky doesn't match the
   scene" rather than as a bug.
6. **The UBO size assertions** are the existing mechanism and are updated, not deleted.

**Mutation probes**, derived from the *code* and not from the assertion list — the lesson
the multi-scatter slice's review produced: `exposure` moved outside the tone map in each
shader; the multiply deleted in each shader; `exposure[4]` inserted mid-struct instead of
appended; the fallback left uninitialised; `reflect()`'s line dropped.

## Non-goals

- **Auto-exposure / eye adaptation.** Needs a luminance reduction over the framebuffer,
  which needs the offscreen HDR target that is still deferred. Recorded.
- **EV-stop authoring units and a UI slider.** Recorded above and in the backlog.
- **Exposure on the four unlit shaders.** Recorded above and in the backlog.
- **Per-camera exposure actually varying between two cameras in one scene.** The renderer
  resolves one "first found" camera, so this is bounded by the existing single-camera
  limitation rather than by anything here.
