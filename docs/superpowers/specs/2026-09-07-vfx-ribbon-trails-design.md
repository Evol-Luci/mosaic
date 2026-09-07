# VFX V2 — ribbon trails (connected geometry) — Design

## Goal (Phase 4, VFX pillar — slice 2)

Upgrade trails from the V1 billboard-chain to a **connected camera-facing ribbon**:
one continuous tapered strip instead of discrete sprites. The `TrailRenderer`
component and `TrailSystem` (point history) are unchanged; only the **rendering**
changes, backed by a new alpha-blended Vulkan triangle pipeline.

**Approach:** the engine **expands the ribbon to triangles on the CPU** (camera-facing
math stays testable) and submits them through a new neutral `draw_ribbons` device
seam; the Vulkan side is a trivial world-space triangle pipeline (no billboard/instance
math), modeled on the existing particle pipeline. Verified headlessly (geometry +
`FakeRenderDevice` call) + build + `mosaic run --window` smoke; the **actual ribbon
visual is the maintainer's check on hardware** (GPU output can't be seen in the
sandbox — same caveat as the particle-rendering slice).

Ribbon **replaces** the billboard-chain rendering for trails; particles keep using
`draw_particles`.

## Design

### `engine/render/render_device.hpp` — new seam (beside `ParticleInstance`)

```cpp
// One vertex of a ribbon strip (pre-expanded, world-space). alpha is the taper.
struct RibbonVertex { Vec3 position; Vec3 color; float alpha = 1.0f; };
struct RibbonDrawParams { Mat4 view_proj; }; // projection * view (no model matrix)
```

`RenderDevice` gains a default-no-op virtual (like `draw_particles`):
```cpp
virtual void draw_ribbons(const std::vector<RibbonVertex>& /*vertices*/,
                          const RibbonDrawParams& /*params*/) {}
```

### `engine/vfx/ribbon.hpp` — neutral geometry builder (headless-testable)

```cpp
// Append a camera-facing ribbon (triangle LIST, 6 verts per segment) for `points`
// (oldest -> newest) to `out`. `width` is the head (newest) width; width and alpha
// taper linearly from ~0 at the oldest end to full at the newest (frac = (i+1)/n).
// Fewer than 2 points appends nothing. `camera_pos` orients each segment to face the
// viewer: side = normalize(cross(segment_dir, dir_to_camera)) (with a stable fallback
// when the segment points near the camera).
void build_ribbon(const std::vector<Vec3>& points, const Vec3& camera_pos, float width,
                  const Vec3& color, std::vector<RibbonVertex>& out);
```

Per segment `s` from `points[i]` to `points[i+1]`: `seg = normalize(p[i+1]-p[i])`,
`to_cam = normalize(camera_pos - p[i])`, `side = cross(seg, to_cam)`; if
`|side| ~ 0` fall back to `cross(seg, {0,1,0})`, then `{1,0,0}`; normalize. Half-width
at a point `j` is `0.5 * width * (j+1)/n`, alpha is `(j+1)/n`. Emit the quad
`(p[i]±side*half_i, p[i+1]±side*half_{i+1})` as two triangles (6 `RibbonVertex`, color
constant, alpha per end). Includes `render_device.hpp` for `RibbonVertex`.

### `engine/render/render_system.hpp` — ribbon pass (replaces trail billboards)

Revert the V1 trail sub-loop out of the particle billboard batch (particles →
`draw_particles` only, as before V1's trail change). Add a **separate trail ribbon
pass**: compute the camera world position from `Mat4::inverse_affine(view)`'s
translation, then for each `TrailRenderer` entity call `build_ribbon(*points_for(e),
cam_pos, tr.size, tr.color, ribbon)`; if the accumulated `ribbon` is non-empty,
`device.draw_ribbons(ribbon, RibbonDrawParams{ projection * view })`.

### `modules/render_vulkan` — the ribbon pipeline

Modeled on the particle pipeline:
- `shaders/ribbon.vert`: `gl_Position = view_proj * vec4(inPos,1)`; pass `inColor`,
  `inAlpha` through. `shaders/ribbon.frag`: `outColor = vec4(fragColor, fragAlpha)`.
  Added to the glslc `foreach` list in `modules/render_vulkan/CMakeLists.txt`.
- A `RibbonPush { float view_proj[16]; }` (64 bytes) push constant.
- Per-vertex vertex input: binding stride `sizeof(RibbonVertex)`; attrs loc0 `vec3`
  position (offset 0), loc1 `vec3` color (offset 12), loc2 `float` alpha (offset 24);
  `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST`; alpha blending identical to the particle
  pipeline. Two pipelines (swapchain `ribbon_pipeline_` + viewport `vp_ribbon_pipeline_`)
  like particles.
- `draw_ribbons` impl: grow/map/upload a per-frame vertex buffer (mirror the particle
  instance-buffer upload), bind the pipeline (swapchain vs viewport), push `view_proj`,
  bind the vertex buffer, `vkCmdDraw(vertexCount, 1, 0, 0)`. Destroy the new pipelines
  in teardown.

## Testing

### Headless (`[vfx]`, `tests/unit/vfx/ribbon_test.cpp`)
1. **Two points → one quad** — `build_ribbon` on 2 points appends exactly 6 vertices;
   `< 2` points appends nothing.
2. **Taper** — the newest-end vertices have `alpha == 1`; the oldest-end alpha is
   `< 1` (== `1/n`); newest half-width > oldest half-width.
3. **Camera-facing** — with points along +X and the camera above, `side` is along ±Z:
   `dot(offset, segment_dir) ≈ 0` and `dot(offset, dir_to_camera) ≈ 0` for the emitted
   vertices (offset from the centerline).
4. **Multiple trails concatenate** — two calls append into the same vector additively.

### Headless render (`[render]`, extend `render_system_test.cpp`)
5. `FakeRenderDevice` gains a `draw_ribbons` override recording `ribbon_draws`. Replace
   the V1 "draws a trail as tapered billboards" test with: a `TrailSystem` + a
   `TrailRenderer` ticked to `n` points yields **one `draw_ribbons` call** with
   `6*(n-1)` vertices (and no trail contribution to `particle_draws`).

### Build + smoke
- The module builds (glslc compiles the ribbon shaders); full suite green;
  `mosaic run --window --ticks 90` exits 0 with no new validation errors. **The ribbon
  visual (a smooth tapered tail behind the falling sphere) is the maintainer's
  hardware check.**

## Out of scope / follow-ups

- Per-trail **texture** + UVs along the ribbon (needs a UV attribute + sampler).
- Width- and color-**over-length curves** (reuse the particle over-life easing).
- Corner smoothing / miter joins (V2 uses per-segment sides; sharp turns may pinch).
- Soft/additive blend modes; depth-write behavior tuning.
- Decals and screen/post-processing (separate VFX slices).
