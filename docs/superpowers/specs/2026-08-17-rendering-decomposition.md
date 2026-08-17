# Rendering (#36) — Decomposition

**Tracking issue:** [#36 Rendering (Vulkan) — v0.1 tracking issue](https://github.com/Evol-Luci/mosaic/issues/36)
**Status:** Accepted (decomposition; each sub-issue gets its own spec)
**Date:** 2026-08-17

This is a **decomposition**, not an implementation spec. It breaks the large #36
rendering tracking issue into five buildable sub-issues, fixes the cross-cutting
decisions they all inherit, and sets their order. Each sub-issue is then designed
and built through its own brainstorm → spec → plan → TDD cycle.

## Purpose

Rendering is the first *visible* subsystem — it turns the headless engine into
something on screen. It is too large for a single spec, so it is delivered as an
incremental sequence of slices, each producing a visibly-better frame while
honoring the architecture in [docs/design/rendering.md](../../design/rendering.md).

## Architectural spine (every slice honors this)

- **Backend-neutral interface.** Engine/game code targets a `RenderDevice` /
  `Renderer` interface and the high-level concepts `Mesh` / `Material` / `Texture`
  / `Shader` / `Camera` / `Light`. **No engine or game code touches Vulkan** — the
  [render boundary](../../architecture/boundaries.md#3-the-render-boundary).
- **Vulkan is the first backend**, implemented under `modules/render_vulkan/`. It
  ships first because it forces the abstraction to be honest, but the interface is
  backend-neutral from day one; a backend never defines the engine's rendering API.
- **The neutral interface location is established by the first slice** (36a) — an
  engine-facing render interface plus the `modules/render_vulkan/` implementation.
- **Reflected concepts.** `Material`, `Camera`, `Light`, and the mesh-renderer
  component map to reflected data (like every other component), so they are
  inspectable, serializable, and scriptable.

## Cross-cutting decisions (resolved; inherited by all sub-issues)

### Windowing: GLFW

The Vulkan backend uses **GLFW** (FetchContent) for the window and surface
(`glfwCreateWindowSurface` → `VkSurfaceKHR`): tiny, cross-platform, the de-facto
Vulkan choice, and its input handling serves the future editor. GLFW lives in the
backend/app tier — **never** in `engine/` or `foundation/`.

### Render graph: deferred to the last slice

[rendering.md](../../design/rendering.md) wants the render graph first-class
"early." In this decomposition, "early" means **as soon as it is load-bearing** —
i.e. once there are ≥2 real passes to order. The early slices (clear-screen, first
mesh draw, camera) use **direct command submission behind the neutral
`RenderDevice` interface**; the `RenderGraph` abstraction is introduced in **36e**,
migrating the existing draw into a graph pass when a second pass (e.g. a depth
prepass or a second light pass) justifies it. Rationale: the backend-neutral device
interface is the boundary that matters early; a graph with one pass is ceremony,
and building the graph API before real passes reveal its requirements risks
designing it wrong.

### Testability boundary (carried into each sub-issue)

GPU output cannot be unit-tested directly, so every slice **separates
headlessly-testable logic from the actual draw**:

- **Unit-tested (`tests/unit/`):** swapchain/config selection, view/projection
  matrix builders, transform→MVP, culling, resource/handle bookkeeping, render-graph
  ordering/resource-lifetime resolution.
- **Verified by a screenshot / smoke run:** the visible frame (mirrors the
  `mosaic run --ticks N` smoke check).
- The backend-neutral `RenderDevice` interface also admits a future **headless/null
  backend** for deterministic tests of the engine-side render logic.

## Sub-issues

| # | Slice | Scope (headline) | Depends on |
| --- | --- | --- | --- |
| [#42](https://github.com/Evol-Luci/mosaic/issues/42) | **36a** — Device + window + swapchain (clear-screen) | Neutral `RenderDevice` iface + `modules/render_vulkan`: instance, device, GLFW window+surface, swapchain, command submission, per-frame sync; clear-screen present from the app loop | app shell #35, loop #22 |
| [#43](https://github.com/Evol-Luci/mosaic/issues/43) | **36b** — Mesh draw at WorldTransform | Vertex/index upload, unlit pipeline+shader (SPIR-V), reflected `MeshRenderer`, a `RenderSystem` drawing by `WorldTransform`; fixed camera | 36a, transform #21 |
| [#44](https://github.com/Evol-Luci/mosaic/issues/44) | **36c** — Camera + view/projection | Reflected `Camera`; view/proj from `foundation/math` (column-major, RH, Y-up); MVP via UBO/push-constant | 36b, math #26 |
| [#45](https://github.com/Evol-Luci/mosaic/issues/45) | **36d** — Material + texture + directional light | Texture upload+sampling via asset registry #20, reflected `Material`, directional light, Lambert shading | 36c, assets #20 |
| [#46](https://github.com/Evol-Luci/mosaic/issues/46) | **36e** — Point light + minimal render graph | Point light; introduce `RenderGraph` (passes+resources+dependency resolution), migrating the draw into a graph pass | 36d |

## Order

Linear — each slice builds on the last:

```
#42 (36a clear-screen)
   -> #43 (36b mesh draw)
      -> #44 (36c camera)
         -> #45 (36d material + directional light)
            -> #46 (36e point light + render graph)
```

**Start with #42.** It is the only slice with no rendering predecessor and it lays
down the `RenderDevice` interface + `modules/render_vulkan` that every later slice
extends.

## Out of scope for the #36 v0.1 slice (future)

Shadows, post-processing, reflections/volumetrics, multiple backends
(D3D12/Metal/WebGPU — the interface anticipates them but only Vulkan ships now),
skinned meshes, and instancing. The v0.1 target is a lit, textured mesh drawn from
a camera under directional + point lights, expressed as a small render graph.

## Tier / dependency contract

- The neutral render interface is an engine-tier concept; the Vulkan backend and
  GLFW live under `modules/render_vulkan/` (a new module tier above `engine/`).
- `foundation/` and `engine/` gain **no** Vulkan/GLFW dependency — they see only the
  neutral interface.
- Rendering depends on existing work: `WorldTransform` + math (#21/#26), the asset
  registry (#20), the runtime loop (#22), and the app/CLI shell (#35, window/main).
