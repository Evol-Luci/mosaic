# Studio shell (#37) — Decomposition

**Tracking issue:** [#37 Studio shell — v0.1 tracking issue](https://github.com/Evol-Luci/mosaic/issues/37)
**Status:** Accepted (decomposition; each sub-issue gets its own spec)
**Date:** 2026-08-22

This is a **decomposition**, not an implementation spec. It breaks the large #37
Studio tracking issue into buildable sub-issues, fixes the cross-cutting decisions
they all inherit, and sets their order. Each sub-issue is then designed and built
through its own brainstorm → spec → plan → TDD cycle.

## Purpose

The Studio is the editor front-end — the first thing that makes the engine
*operable* by a person, and where **reflection (#18) becomes user-visible** as a
property inspector. It sits on top of the now-complete v0.1 rendering subsystem
(36a–36e-1) and the headless engine core (World #, reflection #18, scene #19,
transform #21, runtime loop #22).

It is too large for a single spec, so it is delivered as an incremental sequence of
slices, each producing a visibly-more-capable editor.

## Scope (v0.1)

**In scope — the "lean core + console":**
- Editor window + dockable panel shell.
- Scene hierarchy (the entity tree).
- Reflection-driven inspector (edits a selected entity's components live; `Transform`
  is the acceptance target).
- Viewport hosting the renderer's output.
- Play/stop driving the `RuntimeLoop` (#22).
- Console panel surfacing the #33 logging output.

**Deferred (post-v0.1, their own future tracking work):** asset browser, and the
Blender live-link preview. Both are substantial enough to stand alone and are not on
the roadmap's hard acceptance path for the Studio.

## Architectural spine (every slice honors this)

### Tier & location: a `studio/` app-tier module

The Studio is application/tooling code, not engine code. It lives in a new top-level
**`studio/`** tier as a `mosaic_studio` library (all logic behind test seams),
wired into the existing CLI as the **`mosaic editor`** subcommand — mirroring the
#35 app/CLI structure (a `mosaic_app` library plus a thin `mosaic` executable; the
CLI was deliberately built to host `create`/`editor`/`build` subcommands).

- `studio/` depends on `engine/` and the **neutral render interface**
  (`engine/render/render_device.hpp`); it may use Dear ImGui.
- **`engine/` and `foundation/` never depend on `studio/` or ImGui.** The tier
  contract from [ARCHITECTURE.md](../../architecture/ARCHITECTURE.md) is unchanged.

### GUI framework: Dear ImGui (docking branch)

Immediate-mode, with official `imgui_impl_glfw` + `imgui_impl_vulkan` backends that
drop into the existing GLFW window and Vulkan device; the **docking branch** provides
the dockable-panel shell. A small FetchContent dependency, consistent with the
project's existing minimal deps (GLFW, Catch2, nlohmann, toml++). It is the de-facto
standard for engine tooling.

### The render boundary holds

[boundaries.md §3 — the render boundary](../../architecture/boundaries.md) says no
engine/game code touches Vulkan. The Studio preserves this:

- **ImGui *core*** (`ImGui::Begin`/`End`/widgets) is an ordinary C++ UI library. The
  studio tier calling it is **not** "touching Vulkan."
- **Only `modules/render_vulkan`** links the ImGui *backend* impls
  (`imgui_impl_glfw`, `imgui_impl_vulkan`) — the pieces that actually touch Vulkan.
- The neutral **`RenderDevice` interface grows UI/offscreen hooks** — begin/end a UI
  frame, create a panel-sized offscreen render target, render the scene into it, and
  expose it as an ImGui-samplable image handle. The Vulkan backend implements them;
  the Studio drives panels through ImGui + these neutral hooks. A future backend
  provides its own equivalents.

The exact shape of those hooks is settled by the slices that need them (37a for the
UI frame; 37d for the offscreen target).

### Reflected-property API is the one editing path

The inspector edits components through the reflection layer (#18), not through
component-specific editors. This is the strategic payoff of #18 and the same path the
future automation/agent API ([automation-and-agents.md](../../design/automation-and-agents.md))
will use — humans and agents share one reflected-property surface.

**Known design point for 37c:** today's reflection metadata (`TypeRegistry` /
`TypeInfo` / `FieldInfo`) captures a field's **name and a human-readable type label**,
not a typed get/set accessor into a live instance. The inspector can therefore edit
via the existing **`Value` round-trip** (`ComponentReflection` save direction /
`to_value` → edit the `Value` DOM → `construct` back onto the entity), disambiguating
widget types with the `FieldInfo` type label. If per-field editing needs finer control,
37c may extend reflection with typed field accessors. That choice is 37c's to make.

### Testability boundary (carried into each sub-issue)

GPU/ImGui output cannot be unit-tested directly, so every slice **separates
headlessly-testable logic from the actual draw**, exactly as the rendering slices did:

- **Unit-tested (no window):** the selection model, the hierarchy model over
  `World::for_each_entity`, the inspector's field/edit model, and the play/stop
  control state.
- **Verified functionally:** the ImGui/GPU window runs (`mosaic editor` launches,
  exits 0, silent stderr). The GUI window cannot be screenshotted in the sandbox
  (per the standing render-slice practice), so functional launch is the acceptance
  for the draw path.

## Offscreen viewport & the deferred render graph

The viewport panel forces **two passes**: (1) the scene renders to an **offscreen
color+depth target** sized to the panel, then (2) ImGui renders the UI into the
swapchain, sampling that target into the viewport panel. This is precisely the
"**≥2 real passes**" condition under which the 36e decomposition deferred the
`RenderGraph` (a one-pass graph was ceremony; build it when passes reveal its
requirements).

**Therefore the RenderGraph deferral comes due in 37d (viewport).** The decomposition
fixes only the outcome — the scene composites into an ImGui-sampled offscreen target.
Whether 37d formalizes a `RenderGraph` to order the two passes or uses minimal direct
two-pass submission is settled by 37d's own brainstorm, informed by the real passes in
front of it.

## Sub-issue breakdown & order

1. **37a — Editor shell.** `mosaic editor` opens an ImGui docking window: the
   `studio/` tier + `mosaic_studio` library, `mosaic editor` CLI subcommand, ImGui
   (docking) wired into the backend (`imgui_impl_glfw`/`imgui_impl_vulkan`) and into
   the frame loop, a dockspace with empty placeholder panels and a menu bar. Foundation
   for every other slice. Establishes the neutral UI-frame hooks on `RenderDevice`.
2. **37b — Scene hierarchy.** An entity-tree panel built on `World::for_each_entity`
   (#19), plus a headlessly-testable **selection model** (which entity is selected;
   consumed by the inspector).
3. **37c — Reflection-driven inspector.** Enumerate the selected entity's components
   (`ComponentReflection` / `TypeRegistry`), render a widget per field, write edits
   back. Edits a `Transform`'s fields live — the roadmap's acceptance target and the
   #18 payoff. Owns the reflection-editing-path decision noted above.
4. **37d — Viewport.** Embed the renderer's output in a dock panel via a panel-sized
   offscreen render target. Where the render-graph-vs-direct-submission decision lands.
5. **37e — Play/stop + console.** A toolbar play/stop that drives `RuntimeLoop`
   start/stop (#22), and a console panel that surfaces #33 logging via a custom `Sink`.

**Recommended order:** 37a → 37b → 37c → 37d → 37e. This lands the reflection payoff
(37c) early and defers the hardest work (37d's offscreen target + render graph). The
trade-off: the 3D scene does not reappear *inside the editor* until 37d — the earlier
slices show the hierarchy/inspector against a running (but not yet embedded) world. The
alternative, building 37d right after 37a for immediate in-editor visuals, front-loads
the render-graph work; we accept the payoff-early order instead.

Each sub-issue gets its own brainstorm → spec → plan → TDD cycle.

## Acceptance (tracking-level; each sub-issue refines its own)

- [ ] `mosaic editor` opens a dockable editor window (37a).
- [ ] An entity hierarchy panel with selection (37b).
- [ ] A reflection-driven inspector that edits a selected `Transform` live (37c).
- [ ] The renderer's output embedded in a viewport panel (37d).
- [ ] Play/stop drives the world via `RuntimeLoop`; a console shows log output (37e).

## Depends on / relates

- **Depends on:** the rendering subsystem (viewport) — v0.1-complete through 36e-1;
  #18 reflection (inspector), #19 scene + `for_each_entity` (hierarchy), #21 transform
  (the acceptance component), #22 runtime loop (play/stop), #33 logging (console).
- **Relates:** the automation/agent API shares the reflected-property surface the
  inspector exposes.
