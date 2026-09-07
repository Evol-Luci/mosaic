# VFX V3 — quad decals — Design

## Goal (Phase 4, VFX pillar — slice 3)

Add **decals**: a `Decal` component renders a **world-oriented, alpha-blended
textured quad** on a surface (scorch marks, blob shadows, signs). Pragmatic v1 that
reuses the engine's existing texture-descriptor infrastructure and the `WorldTransform`
placement meshes already use. Projected-onto-arbitrary-geometry decals (depth
reprojection) are a documented follow-up.

Same GPU-verification caveat as ribbons/particles: geometry/wiring verified
headlessly + build + `mosaic run --window` exit 0; the **visual is the maintainer's
hardware check**.

## Design

### `engine/vfx/decal.hpp` — reflected component

```cpp
struct Decal
{
    mosaic::foundation::serialization::AssetID texture{};        // decal image (nil = nothing drawn)
    Vec3                                       color = {1,1,1};  // tint multiplied with the texture
    float                                      alpha = 1.0f;     // overall opacity multiplier
};
```

Reflected + registered as `"Decal"` in `register_builtin_components` (serializes,
shows in the Inspector). The entity's `Transform` (→ derived `WorldTransform`) places,
orients, and sizes the quad — a ground decal lies flat via its rotation, its footprint
set by scale. No size field on the component (Transform.scale is the size), matching how
meshes are sized.

### `engine/render/render_device.hpp` — new seam

```cpp
struct DecalInstance
{
    mosaic::foundation::math::Mat4 model;         // world transform of the unit quad
    TextureHandle                  texture = 0;   // resolved decal texture (0 = default white)
    mosaic::foundation::math::Vec3 color;         // tint
    float                          alpha = 1.0f;
};
struct DecalDrawParams { mosaic::foundation::math::Mat4 view_proj; };
```

`RenderDevice` gains a default-no-op `draw_decals(const std::vector<DecalInstance>&,
const DecalDrawParams&)` (like `draw_particles`/`draw_ribbons`).

### `engine/render/render_system.hpp` — decal pass

After the billboard/ribbon pass, iterate `Decal` entities that also have a
`WorldTransform`; resolve each `Decal.texture` to a `TextureHandle` through the same
`AssetRegistry` + `TextureCache` path used for `Material.texture` (nil/unresolved →
the default white texture, handle 0); build a `DecalInstance{ world.matrix, handle,
color, alpha }`; and if any, call `device.draw_decals(instances, DecalDrawParams{
projection * view })`. Decals are alpha-blended, so they draw after opaque meshes.

### `modules/render_vulkan` — textured, alpha-blended quad pipeline

- `shaders/decal.vert`: a shader-generated unit quad (6 corners in local `[-0.5,0.5]`
  on the local XY plane) transformed by a pushed `mvp = view_proj * model`; passes a
  `[0,1]` UV and the tint color + alpha.
- `shaders/decal.frag`: `vec4 t = texture(sampler, uv); outColor = vec4(t.rgb *
  tint, t.a * alpha);` (alpha from the texture × the decal alpha).
- Push constant `DecalPush { float mvp[16]; float color[4]; }` (color.w = alpha),
  80 bytes.
- Pipeline layout reuses the existing `texture_set_layout_` (set 0 = the sampler), so
  each decal binds its texture's existing per-`Texture` `VkDescriptorSet`
  (`textures_[handle-1].set`), exactly like the mesh `draw()` path. Alpha blending +
  depth-test-on / depth-write-off, `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST`, two variants
  (swapchain + viewport), modeled on the particle pipeline.
- `draw_decals` impl: per decal, bind `textures_[tex_index].set`, push
  `{ view_proj*model, color, alpha }`, `vkCmdDraw(6, 1, 0, 0)`. (Per-decal draw — decal
  counts are small; no instancing needed for v1.)
- `decal.vert`/`decal.frag` added to the glslc `foreach` in the module CMake; pipelines
  destroyed in teardown.

### Wiring & demo

- `register_builtin_components` — register `"Decal"`.
- `setup_runtime` — no new system (decals are pure render state; the pass lives in
  `RenderSystem`). No wiring beyond the render pass.
- Demo (`build_default_scene`): a small decal entity on the ground — a `Transform`
  positioned just above `y = 0`, rotated flat, scaled to a small footprint, with a
  `Decal{ texture = kStripesTextureId }`. This **adds one entity**, so the four
  default-scene count assertions (`transforms 10 → 11` in `demo_scene_test`,
  `populate_world_test`, `bundle_test`, `scene_save_roundtrip_test`) are updated.

## Testing (headless)

`tests/unit/vfx/decal_test.cpp` (`[vfx]`) + render extension.
1. **Reflection round-trip** — `Decal` fields survive `to_value` → `from_value`.
2. **Builtin registration** — `types.has("Decal")`.
3. **Render emits a decal instance** (`[render]`, `render_system_test.cpp`) — an entity
   with a `Transform` (→ `WorldTransform` via `TransformSystem`) + a `Decal` yields one
   `draw_decals` call with the entity's world matrix, tint color, and alpha (via a
   `FakeRenderDevice` `decal_draws` recorder). A `Decal` with a nil texture still emits
   an instance (texture handle 0 = default white).
4. **Default-scene counts** — the four count tests are updated to `11` and stay green.
5. **Build + smoke** — glslc compiles the decal shaders; `mosaic run --window --ticks N`
   exits 0, no new validation errors.

## Out of scope / follow-ups

- **Projected decals** (reproject onto scene geometry via the depth buffer) — the
  "real" decal technique; a much larger deferred-style pass.
- Decal texture atlases; normal-map / emissive decals; per-decal blend modes.
- Sorting / z-fighting controls beyond depth-write-off + a small surface offset.
- Instanced decal submission (v1 draws one quad per decal).
