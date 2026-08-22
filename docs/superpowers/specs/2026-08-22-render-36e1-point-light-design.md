# Render 36e-1 — Point Light (design)

**Issue:** [#46](https://github.com/Evol-Luci/mosaic/issues/46) (36e). **Date:** 2026-08-22.

## Summary

Add a **point light** to the forward renderer: a reflected `PointLight` component whose
world position comes from its entity's `WorldTransform`, shaded per-mesh in **object
space** with windowed inverse-square attenuation. This is the point-light half of the
36e issue.

The RenderGraph named in the 36e decomposition is **deferred** — see *Scope & the render
graph* below. This slice is therefore labelled **36e-1**, mirroring the 36d-1 / 36d-2
split.

## Scope & the render graph

The rendering decomposition slated a `RenderGraph` for 36e *"until ≥2 passes justify a
graph"* (direct submission behind the neutral `RenderDevice` interface until then). A
point light does **not** create a second pass — the renderer stays a single forward
pass — so the graph's own justification has not arrived. Building a one-pass graph now
would be speculative structure with nothing to order.

**Decision:** defer the RenderGraph to the next slice that genuinely forces ≥2 passes —
a shadow-map depth pre-pass, or the Studio's offscreen-viewport-to-texture in
[#37](https://github.com/Evol-Luci/mosaic/issues/37). 36e-1 ships the point light only.

Also out of scope (each re-filed to a future slice): **multiple point lights** (one
directional + one point, first-of-each, as today) and **world-space lighting** (the UBO
path — see *Known limitation*).

## Where this fits

The current renderer (36a–36d) is a single forward pass with **object-space, per-draw
shading**. `RenderSystem::render` finds the first `DirectionalLight`, transforms its
*direction* into each mesh's object space
(`transform_direction(inverse_affine(model), dir)`), and bakes shading inputs into a
112-byte push constant (`mvp | light_direction | light_color | base_color`); a
`static_assert(sizeof(PushConstants) == 112)` guards the layout. The vertex shader passes
object-space **normal** and **uv** to the fragment shader — but not object-space
position, which a positional light needs. Descriptor sets already exist (36d-2 texture).
`transform_point` and `inverse_affine` already exist in `foundation/math`.

The point light reuses this object-space machinery wholesale; the only genuinely new
plumbing is one extra vertex→fragment varying (object-space position) and two more
push-constant vectors.

## Components

### `engine/render/point_light.hpp`

```cpp
struct PointLight {
    Vec3  color{1, 1, 1};
    float intensity{1.0f};
    float range{10.0f};   // attenuation window; contribution fades to 0 at `range`
};
```

Reflected (`color`, `intensity`, `range`). **Position is not a field** — it is derived
from the light entity's `WorldTransform` translation, obtained as
`transform_point(wt->matrix, Vec3{0,0,0})`. This matches how `Camera` derives its view
from a `WorldTransform`: a point light is a *placed* object, so it can be moved and
parented via `Transform`. (This is a deliberate, minor inconsistency with
`DirectionalLight`, which stores `direction` as a plain field — a directional light has
no location.)

A light entity with no `WorldTransform` is skipped (no world position → no point light),
consistent with how meshes without a `WorldTransform` are skipped.

Registered in `engine/reflection/builtin_components.hpp` under the stable name
`"PointLight"`, so the app and the future editor share one component set.

## `DrawParams` and `RenderSystem`

`DrawParams` (`engine/render/render_device.hpp`) gains three fields, all expressed in the
**mesh's object space**, consistent with the existing `light_direction`:

```cpp
Vec3  point_position;  // point light position transformed into this mesh's object space
Vec3  point_color;     // light color * intensity (premultiplied; no separate field)
float point_range;     // 0 == no point light (contribution disabled)
```

`RenderSystem` gains `active_point_light(world)`: it returns the first `PointLight` whose
entity has a `WorldTransform`, together with that light's **world position**
(`transform_point(wt->matrix, {0,0,0})`), or a "none" result. Per drawn mesh, the system
computes:

```cpp
point_position = transform_point(inverse_affine(mesh_wt->matrix), light_world_pos);
point_color    = light.color * light.intensity;
point_range    = light.range;   // 0 when there is no active point light
```

This mirrors the existing directional line
(`transform_direction(inverse_affine(wt->matrix), light.direction)`). The directional
light path, camera path, material/texture path, and all fallbacks are unchanged.

## Shaders

**`cube.vert`** — add one output, object-space fragment position:

```glsl
layout(location = 2) out vec3 fragPos;
...
fragPos = inPosition;   // object space, same space as the normal and the point light
```

**`cube.frag`** — add a point-light term. Everything stays in object space (normal,
fragment position, and `point_position` all agree):

```glsl
vec3  toL   = pc.point_position.xyz - fragPos;
float d     = length(toL);
float win   = clamp(1.0 - pow(d / pc.point_range, 4.0), 0.0, 1.0);
float atten = (pc.point_range > 0.0) ? (win * win) / (d * d + 1.0) : 0.0;
float pl    = max(0.0, dot(n, toL / max(d, 1e-4))) * atten;

// existing directional term unchanged:
float lambert = max(0.0, dot(n, -normalize(pc.light_direction.xyz)));
outColor = vec4(albedo * (0.1 + lambert * pc.light_color.xyz)   // ambient + directional
              + albedo * pl * pc.point_color.xyz,                // + point
                1.0);
```

`pc.point_range == 0` disables the point term via the `atten` guard, so scenes with no
point light shade exactly as before.

## Vulkan backend

`modules/render_vulkan/vulkan_render_device.cpp`:

- `PushConstants` grows 112 → **144 bytes**: two added `float[4]` members —
  `point_position` (xyz = object-space position, **w = range**) and `point_color`
  (xyz = premultiplied color, w unused). No cross-field bit-packing; each `.w` carries a
  naturally-associated scalar. Update the `static_assert` to `== 144` and match the
  shader `Push` block.
- `draw()` copies the three new `DrawParams` fields into the struct.
- The pipeline layout already sizes its `VkPushConstantRange` from `sizeof(PushConstants)`,
  so it follows automatically.
- **Device-init budget check:** assert `sizeof(PushConstants) <=
  physicalDeviceProperties.limits.maxPushConstantsSize`. The Vulkan-guaranteed minimum is
  128 bytes; desktop GPUs (our GLFW target) expose 256, so 144 is fine in practice. The
  assert fails loud (log + throw) on a hypothetical 128-only device rather than letting
  validation catch it silently downstream.

## Known limitation (object-space distance)

Attenuation distance `d` is measured in **object space**. Under **non-uniform mesh
scale**, object-space distance differs from world-space distance, so the point light's
falloff and shape are warped. For uniformly-scaled meshes it matches world space up to
the uniform scale factor; for the **unit-scale demo cube it is exact**. This is the
inherent trade-off of the object-space shading model (chosen for minimalism and
consistency with the existing directional light). The fix is world-space lighting via a
per-frame uniform buffer — the deferred alternative, out of scope here. Documented, not a
bug.

## Demo

`app/main.cpp`: add a point-light entity so the effect is visible alongside the existing
directional light — a `Transform` at an offset (e.g. `{1.5, 1.0, 1.5}`), its
`WorldTransform`, and a `PointLight` (e.g. warm color, `range ≈ 6`). As the cube spins,
the point light's falloff reads across its faces. Static placement is sufficient; no
per-frame animation is added.

## Testing

`tests/unit/render/render_system_test.cpp`, tag `[render]`, using the existing
`FakeRenderDevice` seam (captures `DrawParams` per draw):

1. **Object-space point position** — a mesh with a non-axis-aligned world rotation and a
   point light at a known world position; assert `params.point_position` equals
   `transform_point(inverse_affine(mesh_world), light_world_pos)`, plus a guard that it
   differs from the raw world position (so the test fails if the object-space transform is
   ever skipped). Mirrors the existing directional-light assertion.
2. **Color/intensity/range passthrough** — assert `point_color == color * intensity`
   (component-wise) and `point_range == range`.
3. **No point light** — a scene with a mesh but no `PointLight`; assert
   `params.point_range == 0` (contribution disabled) while the directional/base paths
   still populate as before.

The shader attenuation math runs on the GPU and is not unit-tested (no device in the test
build); it is exercised by the windowed demo (`mosaic run --window`). The GUI window
cannot be screenshotted in the sandbox — verify functionally (exit 0, silent stderr) per
the existing render-slice practice.

## Non-goals

- RenderGraph / multi-pass structure (deferred to the first ≥2-pass slice).
- Multiple point lights (one directional + one point, first-of-each).
- World-space lighting / per-frame light UBO.
- Shadows, specular/PBR, spot lights.
- New `foundation/math` (existing `transform_point` / `inverse_affine` suffice).
