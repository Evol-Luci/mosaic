# Rendering 36b — Mesh Draw at WorldTransform — Design

**Issue:** [#43 — 36b: Mesh upload + single unlit draw at WorldTransform](https://github.com/Evol-Luci/mosaic/issues/43)
**Tracking:** [#36 Rendering](https://github.com/Evol-Luci/mosaic/issues/36) · **Decomposition:** [2026-08-17-rendering-decomposition.md](2026-08-17-rendering-decomposition.md) · **Builds on:** [36a clear-screen](2026-08-17-render-36a-clear-screen-design.md)
**Status:** Accepted
**Date:** 2026-08-18

## Purpose

Put the first geometry on screen: upload a mesh and draw it at its `WorldTransform`
under a fixed camera, driven by a `RenderSystem` inside the runtime loop. This is the
first slice that turns the clear-screen (36a) into an actual rendered object and
establishes the mesh/pipeline/draw path the rest of rendering builds on. Camera
(36c), materials/lighting (36d), and the render graph (36e) come later.

## Scope

- Neutral `RenderDevice` grows `create_mesh` (vertex+index upload → handle) and
  `draw(handle, mvp)`.
- A reflected `MeshRenderer` component and a `RenderSystem` that draws every entity
  holding `WorldTransform` (#21) + `MeshRenderer`.
- The Vulkan backend gains a depth buffer, a graphics pipeline, GLSL→SPIR-V shaders
  (compiled at build time with `glslc`), and mesh buffer upload/draw.
- A windowed demo: a spinning unlit cube, proving `WorldTransform` flows to the
  screen.

## Out of scope (later slices)

- A real `Camera` component / dynamic view-projection — **36c** (36b uses a fixed
  camera baked into the `RenderSystem`).
- Materials, textures, lighting — **36d** (36b is unlit vertex color).
- The render graph — **36e** (36b submits directly, as 36a does).
- Loading meshes through the asset registry — the cube is generated procedurally;
  asset-backed meshes come with textures in 36d.

## Architectural spine (inherited)

Backend-neutral: engine/app target the `RenderDevice` interface and never see Vulkan.
The Vulkan implementation lives in `modules/render_vulkan/`. GLFW/Vulkan appear only
in the backend module (+ the one app factory site). Render graph deferred.

## Components & interfaces

### `engine/render/` — neutral additions

```cpp
namespace mosaic::engine::render
{
using Vec3 = mosaic::foundation::math::Vec3;
using Mat4 = mosaic::foundation::math::Mat4;

struct Vertex { Vec3 position; Vec3 color; }; // unlit vertex color

using MeshHandle = std::uint32_t;             // opaque device mesh id; 0 == invalid

class RenderDevice
{
public:
    virtual ~RenderDevice() = default;
    virtual void begin_frame() = 0;
    virtual void clear(const Color&) = 0;   // sets the clear color for the frame
    virtual void end_frame() = 0;

    // Upload a mesh; returns an opaque handle (0 on empty input). The mesh lives
    // until the device is destroyed.
    virtual MeshHandle create_mesh(const std::vector<Vertex>& vertices,
                                   const std::vector<std::uint32_t>& indices) = 0;

    // Record one indexed draw of `mesh` transformed by `mvp`. Must be called
    // between begin_frame() and end_frame(). Unknown handle → no-op.
    virtual void draw(MeshHandle mesh, const Mat4& mvp) = 0;
};
} // namespace mosaic::engine::render
```

The device is **camera-agnostic**: it draws with whatever `mvp` it is handed. 36a's
`clear()` becomes "remember the clear color"; the actual color+depth clear happens in
`begin_frame`'s render-pass-begin.

### `engine/render/mesh_renderer.hpp` — reflected component

```cpp
namespace mosaic::engine::render
{
struct MeshRenderer { MeshHandle mesh = 0; };
// reflected: a single MeshHandle field (plain integer) — no backend type.
}
```

### `engine/render/render_system.hpp` — render-time traversal (NOT a tick System)

```cpp
class RenderSystem
{
public:
    RenderSystem();                              // installs a fixed perspective camera
    void render(World&, RenderDevice&) const;    // for_each<WorldTransform, MeshRenderer>
private:
    Mat4 view_;
    Mat4 projection_;
};
```

`render` iterates entities that hold **both** `WorldTransform` and `MeshRenderer`,
computes `mvp = projection_ * view_ * world.matrix`, and calls
`device.draw(mr.mesh, mvp)`. It is **not** a `World` tick system: the sim runs at a
fixed timestep in `World::tick`, but rendering runs once per frame in `on_frame`, and
the device frame is owned there. The app holds a `RenderSystem` and calls
`render(world, device)` between `clear()` and `end_frame()`.

For 36b the camera is fixed: a right-handed perspective looking down −Z, Y-up,
positioned to frame a unit cube a few units ahead. 36c replaces this with a `Camera`
component.

## Vulkan backend additions (`modules/render_vulkan/`)

All internal to `VulkanRenderDevice`; no interface leaks.

- **Depth buffer.** A `D32_SFLOAT` depth image + view, part of the recreatable
  swapchain group (rebuilt on resize). The render pass gains a depth attachment
  (`loadOp=CLEAR` to 1.0, `storeOp=DONT_CARE`, layout
  `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`); the subpass references it; a depth
  subpass-dependency stage is added.
- **Graphics pipeline** (built once at init): vertex input binding/attributes for
  `Vertex{position@loc0, color@loc1}`; a pipeline layout with a push-constant range
  (`VK_SHADER_STAGE_VERTEX_BIT`, `sizeof(Mat4)`); depth test + write enabled
  (`LESS`); back-face cull, CCW front face; triangle list; dynamic viewport+scissor
  (so resize needs no pipeline rebuild).
- **Shaders** — `shaders/cube.vert` (applies push-constant `mvp` to position, passes
  color) and `shaders/cube.frag` (outputs interpolated color). Compiled to SPIR-V at
  build time by `glslc`; the `.spv` are loaded at device init (throw on load
  failure).
- **Meshes** — `create_mesh` uploads vertices+indices into device-local buffers via a
  staging buffer + one-time command submit; stores `{vbuf, ibuf, index_count}` in a
  vector indexed by `MeshHandle`. `draw` binds the pipeline (once per frame), binds
  the mesh's vertex/index buffers, `vkCmdPushConstants(mvp)`, `vkCmdDrawIndexed`.
  Meshes are destroyed before the logical device in teardown.

**Frame flow:** `begin_frame` = acquire + begin cmd + set dynamic viewport/scissor +
begin render pass (clear color+depth) + bind pipeline; `RenderSystem::render` issues
`draw` calls; `end_frame` = end render pass + submit + present. Resize recreation now
also rebuilds the depth image.

## Demo (windowed mode)

In `main`'s `--window` path, after device creation: generate a cube
(`make_cube_mesh()` → 24 vertices with per-face colors, 36 indices),
`device->create_mesh(...)`, spawn an entity with `Transform` (a few units ahead of
the camera), `WorldTransform`, and `MeshRenderer{cube}`; register `TransformSystem`.
A small per-tick rotation advances the cube's `Transform.rotation` so the cube
**spins**, making the `WorldTransform` path visible. The `on_frame` hook becomes:
`poll → begin_frame → clear(bg) → render_system.render(world, device) → end_frame`.

## Error handling

- `create_mesh` with empty vertices or indices → returns handle `0` (invalid); the
  `RenderSystem` skips a `MeshRenderer{0}` (guarded), and `draw(0, ...)` is a no-op.
- `draw` with an unknown/out-of-range handle → no-op.
- Shader `.spv` load failure or any Vulkan setup failure → throw (caught by `main`,
  reported, exit non-zero), as in 36a.
- **glslc missing at configure time** → CMake hard-errors with a clear message
  (`glslc is required to build the Vulkan backend shaders`).

## Testing

Headless (no GPU), plus the smoke run — same boundary as 36a.

- **`RenderSystem` traversal (`[render]`)** — a **fake `RenderDevice`** records each
  `draw(handle, mvp)`. Assert:
  - one entity with `WorldTransform`+`MeshRenderer{h}` → exactly one recorded draw
    with `handle == h` and `mvp == projection * view * world.matrix` (matrix compared
    componentwise within tolerance);
  - entities missing `WorldTransform` **or** `MeshRenderer` are skipped;
  - a `MeshRenderer{0}` (invalid handle) is skipped;
  - N drawable entities → N draws.
- **Cube generator (`[render]`)** — `make_cube_mesh()` returns 24 vertices and 36
  indices; every index `< 24`; the 8 corner positions span the expected ±0.5 extent.
- **Fake device conformance** — the fake `RenderDevice` implements the full interface
  (compile-time proof the interface is sufficient for the engine-side render logic,
  and the seed of a future headless/null backend).
- **GPU parts** (pipeline, shaders, depth, buffer upload, actual draw) — verified by
  a warning-clean build + the `mosaic run --window` smoke: a spinning, depth-correct,
  vertex-colored cube; resizing keeps it rendering; closing exits 0.

## CMake

- `modules/render_vulkan/CMakeLists.txt`: `find_program(GLSLC glslc REQUIRED)` (hard
  error with a message if absent); `add_custom_command`s compiling
  `shaders/cube.vert` and `shaders/cube.frag` → `${CMAKE_CURRENT_BINARY_DIR}/*.spv`;
  a custom target the library depends on; the shader output dir is passed to the
  device (compile definition) so it loads the `.spv` at runtime.
- No new third-party dependency (glslc ships with the Vulkan SDK already required by
  36a).

## Acceptance criteria (from #43)

- [ ] A mesh (cube) draws at its `WorldTransform`; changing the transform moves/rotates
      it on screen next frame.
- [ ] The `RenderSystem` runs each frame inside the runtime loop alongside the
      `TransformSystem`.
- [ ] Transform→MVP traversal logic is unit-tested headlessly (fake device); cube
      generation is unit-tested.

## Tier / dependency contract

- The neutral `Vertex`/`MeshHandle`/`MeshRenderer`/`RenderSystem` live in
  `engine/render/` — no Vulkan/GLFW. `MeshHandle` is a plain integer.
- All Vulkan (pipeline, shaders, depth, buffers) stays in `modules/render_vulkan/`.
- `foundation`/`engine` gain no Vulkan/GLFW dependency. Depends on 36a (#42),
  transform + math (#21/#26), and the runtime loop (#22).
