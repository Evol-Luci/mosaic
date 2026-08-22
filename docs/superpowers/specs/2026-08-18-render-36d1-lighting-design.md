# Rendering 36d-1 — Directional Lighting — Design

**Issue:** [#45 — 36d: Material + texture + directional light](https://github.com/Evol-Luci/mosaic/issues/45) (this is the **first of two sub-slices**; 36d-2 adds texture + descriptor sets + asset-registry resolution)
**Tracking:** [#36 Rendering](https://github.com/Evol-Luci/mosaic/issues/36) · **Decomposition:** [2026-08-17-rendering-decomposition.md](2026-08-17-rendering-decomposition.md) · **Builds on:** [36c camera](2026-08-18-render-36c-camera-design.md)
**Status:** Accepted
**Date:** 2026-08-18

## Purpose

Light the cube. Add vertex normals, a `Material` (base color) and a
`DirectionalLight` component, and Lambert shading — so surfaces facing the light are
bright and those facing away are dim. Deliberately **no textures and no descriptor
sets** (those are 36d-2): all per-draw data fits in push constants via object-space
lighting.

## Scope

- `Vertex` gains a `normal` (drops `color`); the cube generator emits per-face normals.
- A reflected `Material { Vec3 base_color }` and `DirectionalLight { Vec3 direction; Vec3 color }`, registered in `builtin_components` (serializable).
- The neutral `RenderDevice::draw` takes a `DrawParams` block (mvp + light + base color).
- The `RenderSystem` finds the active `DirectionalLight` and each mesh's `Material`,
  transforms the light into object space, and issues lit draws.
- Lambert + ambient shading in the fragment shader; no descriptor sets.
- A small `foundation/math::transform_direction`.

## Out of scope (36d-2 and later)

- Textures, UVs, samplers, descriptor sets, and asset-registry texture resolution — **36d-2**.
- Point/spot lights, multiple lights, shadows — later (36e adds a point light).
- Specular/PBR — later.

## Architectural spine (inherited)

Backend-neutral: components + the `RenderSystem` live in `engine/render` (Vulkan-free);
`transform_direction` is `foundation/math`. The Vulkan backend gains normals + a bigger
push constant + a lighting shader — **but no descriptor sets** (that machinery is 36d-2).

## Components & interfaces

### `engine/render/material.hpp` — reflected component

```cpp
namespace mosaic::engine::render
{
struct Material
{
    mosaic::foundation::math::Vec3 base_color{0.8f, 0.8f, 0.8f};
    // 36d-2 adds: AssetID texture;
};
template <class V> void reflect(V& v, Material& m) { v.member("base_color", m.base_color); }
}
```

### `engine/render/directional_light.hpp` — reflected component

```cpp
namespace mosaic::engine::render
{
// A directional light. `direction` is the direction the light travels (world space);
// shading uses -direction as the vector toward the light.
struct DirectionalLight
{
    mosaic::foundation::math::Vec3 direction{0.0f, -1.0f, 0.0f};
    mosaic::foundation::math::Vec3 color{1.0f, 1.0f, 1.0f};
};
template <class V> void reflect(V& v, DirectionalLight& l)
{
    v.member("direction", l.direction);
    v.member("color", l.color);
}
}
```

Both registered in `register_builtin_components` (`"Material"`, `"DirectionalLight"`).

### `Vertex` (in `engine/render/render_device.hpp`)

```cpp
struct Vertex
{
    mosaic::foundation::math::Vec3 position;
    mosaic::foundation::math::Vec3 normal;   // was `color`
};
```

### Neutral `draw` grows a parameter block

```cpp
struct DrawParams
{
    mosaic::foundation::math::Mat4 mvp;
    mosaic::foundation::math::Vec3 light_direction; // in the mesh's OBJECT space
    mosaic::foundation::math::Vec3 light_color;
    mosaic::foundation::math::Vec3 base_color;
};

virtual void draw(MeshHandle mesh, const DrawParams& params) = 0;
```

`light_direction` is documented as **object-space** (the `RenderSystem` transforms the
world light per mesh), so the backend needs no model/normal matrix and all per-draw data
fits in a push constant.

### `foundation/math::transform_direction`

```cpp
// Apply m's upper-left 3x3 (linear part) to a direction, ignoring translation.
inline Vec3 transform_direction(const Mat4& m, const Vec3& v);
// = (m[0]v.x + m[4]v.y + m[8]v.z, m[1]v.x + m[5]v.y + m[9]v.z, m[2]v.x + m[6]v.y + m[10]v.z)
```

### `RenderSystem` change

Per `render()`:
1. Active camera → `view`/`projection` (unchanged from 36c).
2. Active `DirectionalLight` = first entity with a `DirectionalLight` (else a default
   light: direction `(-0.5,-1,-0.3)`, white).
3. For each mesh entity (`WorldTransform` + `MeshRenderer{mesh!=0}`):
   - `base_color` = its `Material.base_color` (default `{0.8,0.8,0.8}` if no `Material`).
   - `mvp = projection * view * world`.
   - `light_dir_object = normalize(transform_direction(Mat4::inverse_affine(world), light.direction))`.
   - `device.draw(mesh, DrawParams{mvp, light_dir_object, light.color, base_color})`.

## Data flow

```
DirectionalLight (world dir) --+
Material (base_color)         --+--> RenderSystem.render
mesh WorldTransform ----------+     per mesh:
                                     mvp = proj*view*world
                                     light_obj = normalize(transform_direction(inverse_affine(world), light.dir))
                                     draw(mesh, {mvp, light_obj, light.color, base_color})
Vertex{position, normal} --pipeline--> vert: gl_Position = mvp*pos; pass object-space normal
                                       frag: lambert = max(0, dot(N, -light_obj));
                                             out = base_color * (ambient + lambert*light_color)
```

## Vulkan backend (no descriptor sets)

- **Vertex input**: add the normal attribute (`loc1`, `R32G32B32_SFLOAT`,
  `offsetof(Vertex, normal)`); `loc0` position unchanged. Stride is still `sizeof(Vertex)`.
- **Push-constant range** grows to the packed `DrawParams`: `mat4 mvp` (64) + three
  `vec4`s (light_direction, light_color, base_color; each `Vec3` padded to 16) = **112
  bytes**, within the guaranteed 128-byte minimum. Stage `VERTEX | FRAGMENT`.
- **Shaders**: `cube.vert` — `gl_Position = pc.mvp * vec4(inPosition,1)`, `outNormal =
  inNormal`. `cube.frag` — `float lambert = max(0.0, dot(normalize(inNormal),
  -normalize(pc.light_direction))); outColor = vec4(pc.base_color * (ambient + lambert *
  pc.light_color), 1.0);` with `ambient = 0.1`.
- `draw()` copies the four fields into the padded push-constant struct and
  `vkCmdPushConstants` + `vkCmdDrawIndexed`. Depth/pipeline otherwise unchanged.

## Error handling / edge cases

- **No `DirectionalLight`** → a default directional light (so the scene is still lit).
- **No `Material` on a mesh** → default `base_color {0.8,0.8,0.8}`.
- **Degenerate mesh transform** (zero scale) → `inverse_affine` returns identity (36c
  guard); `normalize` guards a zero-length light direction (returns it unchanged).

## Testing

Headless (fake `RenderDevice` recording `DrawParams`) + the smoke run.

- **`transform_direction` (`[math]`)** — a 90°-about-Y rotation maps `(0,0,-1) → (-1,0,0)`
  (within tolerance); a pure translation leaves a direction unchanged; the translation
  column is ignored.
- **`RenderSystem` lit draw (`[render]`)** — mesh + camera + `DirectionalLight` +
  `Material`: assert `DrawParams.mvp == proj*view*world`, `base_color` == the Material's,
  `light_color` == the light's, and `light_direction` == the object-space transform of the
  world light for a known (rotated) mesh world matrix.
- **Fallbacks (`[render]`)** — no `Material` → default base color in `DrawParams`; no
  `DirectionalLight` → the default light's color/direction.
- **`Material` / `DirectionalLight` reflection round-trip (`[reflection]`)** — registered,
  serialized, reloaded; fields preserved.
- **Cube normals (`[render]`)** — `make_cube_mesh` yields 24 vertices whose normals are
  unit-length and per-face outward (e.g. the +X face's four normals are `(1,0,0)`).
- **GPU** — build + `mosaic run --window`: the cube is shaded by the directional light
  (faces toward it bright, away dim but not black); spinning sweeps the shading across
  faces.

## Demo (windowed mode)

Add a `DirectionalLight` entity (`direction {-0.5,-1,-0.3}`, white) and a
`Material{base_color}` on the cube entity (the per-vertex color is gone). Camera +
spinning cube unchanged. The cube now renders lit.

## Acceptance criteria (this sub-slice of #45)

- [ ] A mesh renders shaded by a directional light (Lambert + ambient); rotating it
      changes which faces are lit.
- [ ] `Material` and `DirectionalLight` are reflected, registered, and serializable.
- [ ] `transform_direction`, the object-space light transform, and the lit-draw traversal
      (fake device) are unit-tested headlessly; the windowed smoke shows the shaded cube.
- [ ] No descriptor sets, no texture, no asset-registry use (deferred to 36d-2).

## Tier / dependency contract

- `Material`/`DirectionalLight` + the `RenderSystem`/`Vertex`/`DrawParams` changes stay
  in `engine/render` (Vulkan/GLFW-free); `transform_direction` is `foundation/math`. The
  Vulkan backend gains normals + a larger push constant + a lighting shader but no
  descriptor sets. No new third-party dependency. Depends on 36c (#44), transform/math
  (#21/#26), reflection (#18).
