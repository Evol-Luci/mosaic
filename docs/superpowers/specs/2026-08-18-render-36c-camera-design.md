# Rendering 36c — Camera + View/Projection — Design

**Issue:** [#44 — 36c: Camera + view/projection](https://github.com/Evol-Luci/mosaic/issues/44)
**Tracking:** [#36 Rendering](https://github.com/Evol-Luci/mosaic/issues/36) · **Decomposition:** [2026-08-17-rendering-decomposition.md](2026-08-17-rendering-decomposition.md) · **Builds on:** [36b mesh draw](2026-08-18-render-36b-mesh-draw-design.md)
**Status:** Accepted
**Date:** 2026-08-18

## Purpose

Replace the `RenderSystem`'s hardcoded camera (36b) with a real, reflected `Camera`
component placed in the world via a `Transform`. The scene renders from the active
camera entity; moving/rotating that entity changes the view. This is the last piece
before materials/lighting (36d).

## Scope

- A reflected `Camera` component (lens: fov / near / far), registered in
  `builtin_components` (serializable, unlike `MeshRenderer`).
- The camera is an **entity with a `Transform` + a `Camera`**; its view is the inverse
  of its `WorldTransform` (idiomatic ECS — integrates with the transform hierarchy).
- `foundation/math` gains `inverse_affine` (invert a TRS/affine matrix).
- The `RenderSystem` computes `view`/`projection` per frame from the active camera.
- The windowed demo spawns a camera entity and renders the spinning cube through it.

## Out of scope (later)

- A "primary camera" flag / multi-camera viewports (36c uses the first-found camera).
- Camera controllers / input-driven movement (a camera is just a `Transform`, so this
  is trivial to add later; the demo camera is static).
- Orthographic projection (perspective only for now).
- Materials, textures, lighting (36d); the render graph (36e).

## Architectural spine (inherited)

Backend-neutral: `Camera` and the `RenderSystem` live in `engine/render` (Vulkan-free);
`inverse_affine` is `foundation/math`. The Vulkan backend is unchanged. The MVP is
still a push constant; the neutral `RenderDevice::draw(handle, mvp)` interface does not
change.

## Components & interfaces

### `engine/render/camera.hpp` — reflected component

```cpp
namespace mosaic::engine::render
{
// The camera "lens". Placement comes from the entity's Transform (view = inverse of
// its WorldTransform). Pure data — reflected and serializable (unlike MeshRenderer,
// whose value is a runtime device handle).
struct Camera
{
    float fov_y_radians = 1.0472f; // 60 degrees
    float near_z = 0.1f;
    float far_z = 100.0f;
};

template <class V>
void reflect(V& visitor, Camera& c)
{
    visitor.member("fov_y_radians", c.fov_y_radians);
    visitor.member("near_z", c.near_z);
    visitor.member("far_z", c.far_z);
}
} // namespace mosaic::engine::render
```

Registered in `engine/reflection/builtin_components.hpp`:
`types.register_component<render::Camera>(registry, "Camera");` (fills the existing
`// future: Camera` slot).

### `foundation/math` — `inverse_affine`

```cpp
// Inverse of an affine transform (bottom row assumed (0,0,0,1)): invert the upper-left
// 3x3 linear part, then translation' = -inv3x3 * translation. If the 3x3 is ~singular
// (|det| < epsilon), returns identity (a camera never has zero scale). Column-major.
static Mat4 Mat4::inverse_affine(const Mat4& m);
```

Cheaper and more stable than a general 4x4 inverse, and correct for any TRS matrix
(rotation + translation + scale). Reusable (parenting, physics).

### `engine/render/render_system.hpp` — camera-driven

The `RenderSystem` no longer stores fixed matrices. Per `render()` call:

1. Find the **active camera**: the first entity (deterministic iteration order) that has
   both a `Camera` and a `WorldTransform`.
2. If found: `view = Mat4::inverse_affine(cameraWorldTransform.matrix)`;
   `projection = Mat4::perspective(cam.fov_y_radians, aspect_, cam.near_z, cam.far_z)`.
3. If **no** active camera: use a documented default view/projection (a fallback
   camera) so meshes still render rather than showing a black screen.
4. Draw each entity with `WorldTransform` + `MeshRenderer{mesh != 0}` as
   `projection * view * meshWorld` (the existing 36b traversal).

`set_aspect(float aspect)` now stores `aspect_` (used to build the projection); it no
longer holds a prebuilt projection. `view()`/`projection()` accessors are dropped or
recomputed on demand — the matrices are now per-frame, per-camera (a test can call a
small helper or reconstruct expected values from the camera + `inverse_affine`).

## Data flow

```
camera entity: Transform (placement) --TransformSystem--> WorldTransform
RenderSystem.render(world, device):
  active camera = first entity with Camera + WorldTransform (else default)
  view       = inverse_affine(cameraWorldTransform)
  projection = perspective(camera.fov_y, aspect_, camera.near, camera.far)
  for each (WorldTransform wt, MeshRenderer mr, mr.mesh != 0):
     device.draw(mr.mesh, projection * view * wt.matrix)
```

## Error handling / edge cases

- **No active camera** → default fallback view/projection (documented); meshes still
  render.
- **Multiple cameras** → first-found is active (deterministic). A primary-camera flag
  is future scope.
- **Degenerate camera Transform** (zero scale → non-invertible 3x3) → `inverse_affine`
  guards `|det| < epsilon` and returns identity; a real camera never has zero scale.
- `aspect_` defaults to `16.0/9.0` until the app calls `set_aspect`.

## Testing

Headless (fake `RenderDevice`) + the smoke run.

- **`inverse_affine` (`[math]`)** — for a known TRS `m`, `inverse_affine(m) * m ≈
  identity`; a pure translation inverts to the negated translation; a pure rotation
  inverts to its transpose.
- **`RenderSystem` renders from the camera (`[render]`)** — a camera entity
  (`Transform` at `(0,0,4)`, identity rotation, + `Camera`) and a mesh entity
  (`WorldTransform` + `MeshRenderer{h}`): assert the recorded draw's `mvp ==
  perspective(cam) * inverse_affine(cameraWorld) * meshWorld` componentwise, and that
  the view equals `translation(0,0,-4)` for that placement.
- **No-camera fallback (`[render]`)** — with no camera entity present, meshes still
  draw (the default), and the draw count is correct.
- **Active-camera selection (`[render]`)** — with two camera entities, the first-found
  lens/placement is used (change one camera's fov and assert the mvp reflects the
  first).
- **`Camera` reflection round-trip (`[reflection]`)** — register via
  `register_builtin_components`, serialize a `Camera` and load it back; fov/near/far
  preserved (proves it is serializable, unlike `MeshRenderer`).
- **GPU** unchanged from 36b; the windowed smoke confirms the camera-driven cube still
  renders (and the demo camera at `(0,0,4)` frames the cube).

## Demo (windowed mode)

The app spawns a **camera entity**: `Transform{position = (0,0,4)}` (identity rotation
→ looks down −Z at the cube at the origin) + `Camera{}`. The cube entity + `SpinSystem`
stay. The `RenderSystem` now derives its view from this camera instead of a built-in
one. (Camera placement/rotation is just a `Transform`, so an angled or moving camera is
a trivial follow-up.)

## Acceptance criteria (from #44)

- [ ] The scene renders from a `Camera` entity; moving/rotating that entity changes the
      view.
- [ ] `Camera` is reflected and registered in `builtin_components` (serializable).
- [ ] `inverse_affine` and the camera-driven traversal (fake device) are unit-tested
      headlessly; the windowed smoke shows the cube framed by the camera.

## Tier / dependency contract

- `Camera` + the `RenderSystem` change stay in `engine/render` (Vulkan/GLFW-free);
  `inverse_affine` is `foundation/math`. No Vulkan interface change. No new third-party
  dependency. Depends on 36b (#43), transform/math (#21/#26), reflection (#18).
