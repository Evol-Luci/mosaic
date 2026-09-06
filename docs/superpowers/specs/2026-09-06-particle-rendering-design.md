# Particle Rendering — Instanced Billboards (Design)

**Date:** 2026-09-06
**Status:** Draft (awaiting review)
**Depends on:** the Particles subsystem (`engine/particles/`, done 2026-09-06 —
[spec](2026-09-06-particles-design.md)); the rendering pillar (#36).

## Motivation

The Particles subsystem simulates transient particle pools per emitter but nothing
draws them — `ParticleSystem::particles_for(EntityID)` was added explicitly as the
"future renderer" seam. This slice is the **first `particles_for` consumer**: it makes
particles visible as camera-facing, alpha-blended billboard sprites, closing the loop
on the particles work and giving the first visible Phase-4 payoff.

## Approach

`RenderSystem::render()` gains a **particle pass** that runs after the mesh pass. It
reads each emitter's live particles through `particles_for`, builds a flat instance
buffer (world position, size, color, per-particle alpha), and issues a single neutral
`RenderDevice::draw_particles(...)` call. The Vulkan backend expands each instance into
a camera-facing quad through a dedicated, alpha-blended pipeline.

The neutral wall is preserved: the engine tier gains particle *draw types* but no
Vulkan; all GPU work stays walled in `modules/render_vulkan`. The engine-tier gather
logic is fully unit-testable against a fake device; the Vulkan pipeline is verified
functionally (validation-clean) since the sandbox cannot screenshot the GUI window.

### Alternative considered (deferred)

Drawing each particle as a small lit cube through the existing `draw()` path needs no
new pipeline and reuses a proven render path, but yields opaque cubes (no alpha fade),
O(particles) draw calls, and no camera-facing billboards. The instanced-billboard path
was chosen for a proper, scalable, good-looking result; the cube approach is not kept.

## Components

### 1. Appearance fields — `engine/particles/particle_emitter.hpp`

Add two reflected, serialized members to `ParticleEmitter`:

- `Vec3 color = {1.0f, 1.0f, 1.0f}` — base particle color.
- `float size = 0.1f` — world-space billboard edge length.

Both are added to `reflect()` so they round-trip through the scene file. Alpha fade is
**derived at render time** (`alpha = 1 - age/lifetime`, clamped to `[0,1]`), not stored,
so the `Particle` struct (`engine/particles/particle.hpp`) is unchanged.

### 2. Neutral render API — `engine/render/render_device.hpp`

```cpp
// One billboarded particle. world_position/size are world-space; alpha is the
// derived fade (1 at spawn -> 0 at end of life).
struct ParticleInstance
{
    mosaic::foundation::math::Vec3 world_position;
    float                          size = 0.0f;
    mosaic::foundation::math::Vec3 color;
    float                          alpha = 1.0f;
};

// Camera data for billboarding. view_proj = projection * view (no model matrix;
// each instance is positioned in the vertex shader). camera_right/up are the
// world-space camera basis vectors the billboard is spanned along.
struct ParticleDrawParams
{
    mosaic::foundation::math::Mat4 view_proj;
    mosaic::foundation::math::Vec3 camera_right;
    mosaic::foundation::math::Vec3 camera_up;
};
```

New virtual on `RenderDevice`, defaulting to a no-op (consistent with the existing
`init_ui`/viewport hooks — headless devices and unrelated fakes need not implement it):

```cpp
virtual void draw_particles(const std::vector<ParticleInstance>& /*instances*/,
                            const ParticleDrawParams& /*params*/) {}
```

### 3. `RenderSystem` — `engine/render/render_system.hpp`

After the existing `for_each<MeshRenderer>` mesh loop, add a particle pass:

1. `auto* psys = world.get_system<particles::ParticleSystem>();` — if null, skip the
   pass entirely (a scene with no particles, or a headless run with no particle system).
2. Derive the camera basis in world space from `cam_world = Mat4::inverse_affine(view)`:
   `camera_right = normalize(column 0 xyz)`, `camera_up = normalize(column 1 xyz)`
   (column-major storage: column `c`, row `r` at `m[c*4 + r]`).
3. Build a single `std::vector<ParticleInstance>` by iterating `for_each<ParticleEmitter>`:
   - `const auto* pool = psys->particles_for(e);` skip if null or empty.
   - For each `Particle p`: append `{p.position, em.size, em.color,
     clamp(1 - p.age/p.lifetime, 0, 1)}` (guard `lifetime <= 0` → alpha 1).
4. If the instance vector is non-empty, call
   `device.draw_particles(instances, {projection * view, camera_right, camera_up})`.

Requires adding `#include "engine/particles/particle_system.hpp"` (same engine tier).
Particles across all emitters are **batched into one `draw_particles` call**; per-instance
color/size carry the per-emitter appearance.

**Ordering contract:** particles draw last in the pass. Mesh `draw()` does not rebind the
pipeline per call (only `ensure_swapchain_pass`/`begin_viewport_pass` bind it once), so a
particle draw that binds the particle pipeline must not be followed by a mesh draw in the
same pass. `RenderSystem` guarantees this by drawing all meshes before any particles.

### 4. Vulkan backend — `modules/render_vulkan/`

**Shaders** (`shaders/particle.vert`, `shaders/particle.frag`, new):

- `particle.vert`: a `push_constant` block `{ mat4 view_proj; vec4 camera_right;
  vec4 camera_up; }`. Instance-rate inputs: `world_position` (vec3), `size` (float),
  `color` (vec3), `alpha` (float). A `const vec2 corners[6]` array indexed by
  `gl_VertexIndex` gives the quad corners in `[-0.5, 0.5]`; the world position is
  `world_position + corner.x*size*camera_right.xyz + corner.y*size*camera_up.xyz`;
  `gl_Position = view_proj * vec4(worldPos, 1.0)`. Outputs color, alpha, and the corner
  as a `[0,1]` uv for the round falloff.
- `particle.frag`: soft round sprite — `float d = length(uv - 0.5) * 2.0;`
  `float falloff = smoothstep(1.0, 0.0, d);` `outColor = vec4(color, alpha * falloff);`.

**Pipelines:** two new pipelines mirroring the existing dual-pipeline pattern —
`particle_pipeline_` (compatible with `render_pass_`, the swapchain pass) and
`vp_particle_pipeline_` (compatible with `vp_render_pass_`, the offscreen editor
viewport pass). Both:

- Alpha blending enabled: `srcColorBlendFactor = SRC_ALPHA`,
  `dstColorBlendFactor = ONE_MINUS_SRC_ALPHA`, add; alpha channel `ONE`/`ZERO`.
- Depth test **on**, depth write **off** (particles respect scene depth but do not
  occlude one another via the depth buffer).
- `cullMode = NONE` (billboards face the camera; winding is irrelevant).
- Topology `TRIANGLE_LIST`; dynamic viewport/scissor (as the scene pipeline).
- Own pipeline layout `particle_pipeline_layout_` with **no descriptor sets** and a
  single push-constant range (`sizeof(ParticlePush)`, vertex stage).
- One instance-rate vertex binding describing the four instanced attributes.

**Instance buffers:** one host-visible buffer per frame-in-flight
(`particle_instance_buf_[kFramesInFlight]` + memory + tracked capacity). `draw_particles`
grows the current frame's buffer if the instance count exceeds capacity (destroy +
`create_buffer` with `VERTEX_BUFFER` usage, `HOST_VISIBLE | HOST_COHERENT`), maps and
memcpy's the instances.

**`draw_particles(instances, params)`:**
1. If not in a viewport pass, `ensure_swapchain_pass()`.
2. If `frame_skipped_` or `instances.empty()`, return.
3. Upload instances to the current frame's instance buffer.
4. Bind the pipeline matching the active pass (`viewport_pass_open_ ?
   vp_particle_pipeline_ : particle_pipeline_`).
5. Push constants (`view_proj`, `camera_right`, `camera_up`).
6. Bind the instance buffer as vertex binding 0; `vkCmdDraw(6, instanceCount, 0, 0)`.

**Cleanup:** destroy both particle pipelines, the particle pipeline layout, and the
per-frame instance buffers/memory in the existing teardown path.

**Build:** add `particle.vert` and `particle.frag` to the `glslc` `foreach` list in
`modules/render_vulkan/CMakeLists.txt` (they compile to `${_shader_dir}/*.spv` and are
loaded via `MOSAIC_SHADER_DIR`, exactly like `cube.*`).

### 5. Demo — `app/run/demo_scene.hpp`

Add a fountain emitter entity to `build_default_scene`: a `Transform` at a visible
position, a `ParticleEmitter{ emitting=true, rate≈50, initial_velocity=up, spread>0,
acceleration=gravity, lifetime≈2, color=orange, size≈0.15 }`. No `MeshRenderer` (the
emitter itself is invisible; only its particles draw). This exercises the full GPU path
in `mosaic run --window` and the editor, and serializes with the scene.

## Error handling

- No `ParticleSystem` in the world → particle pass skipped (no crash).
- `particles_for` returns null, or an emitter has no live particles → that emitter
  contributes no instances.
- Empty instance set → no `draw_particles` call (Vulkan also early-returns on empty).
- `lifetime <= 0` → alpha defaults to 1 (avoid divide-by-zero).
- Vulkan: `frame_skipped_` / pass-not-open guarded exactly as `draw()`; the instance
  buffer grows to fit any instance count (bounded upstream by each emitter's
  `max_particles`).

## Testing

### Headless (engine tier) — `tests/unit/render/render_system_test.cpp`

Extend `FakeRenderDevice` with a recording `draw_particles` override
(`std::vector<ParticleDrawCall>` capturing instances + params). Cases:

1. **One emitter, N live particles** → exactly one `draw_particles` call with N
   instances at the particles' world positions, carrying the emitter's `color`/`size`.
2. **Alpha fade** → an aged particle has smaller alpha than a fresh one; alpha is
   clamped to `[0,1]` and monotonic in age.
3. **Two emitters, different colors** → instances batched into one call, each carrying
   its own emitter color.
4. **No `ParticleSystem`** → no `draw_particles` call, no crash.
5. **No live particles** → no `draw_particles` call.
6. **Camera basis** → for a known camera transform, `camera_right`/`camera_up` are the
   expected unit vectors (perpendicular; match the camera's world basis).

### Reflection — `tests/unit/particles/`

Round-trip a `ParticleEmitter` with non-default `color`/`size` through
`to_value`/`from_value` (extend the existing emitter reflection coverage).

### Vulkan (functional) — manual / acceptance

`mosaic run --window --ticks N` exits 0 with **0 validation errors** (the debug
messenger routes validation to the log; per the project's screenshot constraint the
window itself is not captured). The demo emitter guarantees the billboard pipeline is
actually recorded and submitted.

## Files touched

- `engine/particles/particle_emitter.hpp` — `color`, `size` + `reflect`.
- `engine/render/render_device.hpp` — `ParticleInstance`, `ParticleDrawParams`,
  `draw_particles` (default no-op).
- `engine/render/render_system.hpp` — particle pass; include `particle_system.hpp`.
- `modules/render_vulkan/vulkan_render_device.cpp` — particle pipelines, instance
  buffers, `draw_particles`, cleanup.
- `modules/render_vulkan/shaders/particle.vert`, `particle.frag` — new.
- `modules/render_vulkan/CMakeLists.txt` — compile the new shaders.
- `app/run/demo_scene.hpp` — demo fountain emitter.
- `tests/unit/render/render_system_test.cpp` — fake `draw_particles` + 6 cases.
- `tests/unit/particles/` — reflection round-trip for `color`/`size`.

## Out of scope / follow-ups

- Color/size-over-life curves (constant per emitter for now; alpha fade is linear).
- Textured/atlas sprites, additive blend mode selection, soft-particle depth fade.
- Sub-frame spawn interpolation, script-driven emission (separate particles follow-ups).
- Sorting particles back-to-front (acceptable for the current soft-additive look with
  depth-write off; revisit if ordering artifacts appear).
