# Scripting (Python) — Decomposition

**Roadmap pillar:** [v0.1 Scripting](../../project/roadmap.md) — "Python embedding, reflection
bindings, command access." Normative intent: [design/scripting.md](../../design/scripting.md).
**Status:** Accepted (decomposition; each sub-slice gets its own spec).
**Date:** 2026-08-30.

This is a **decomposition**, not an implementation spec. It breaks the Scripting pillar — the last
v0.1 vertical-slice pillar — into buildable sub-slices, fixes the cross-cutting decisions they
inherit, and sets their order, mirroring the
[physics](2026-08-24-physics-decomposition.md) and
[tools](2026-08-29-tools-workflow-decomposition.md) decompositions. Each sub-slice is then
designed and built through its own brainstorm → spec → plan → TDD cycle.

## Purpose

Give Mosaic a **Python** scripting layer so a user (or an agent) can write a script that
manipulates entities/components and presses Play — the workflow the manifesto and roadmap
promise ("drag into scene → add physics → attach Python script → Play → Build"). Per
[design/scripting.md](../../design/scripting.md), **bindings are generated from
[reflection](../../design/reflection.md)** — scripts see the same properties/components/commands
the inspector and (future) automation API see; there is no separate scripting data model.

## The cross-cutting decision: runtime & tier wall

- **Runtime: real CPython, embedded** (not a minimal Python subset). The design doc's rationale —
  the enormous ecosystem, Blender-shared muscle memory, agent-friendliness — means genuine
  CPython. Feasibility was confirmed on this machine: `find_package(Python3 COMPONENTS
  Development)` resolves with a shared `libpython`, `Python.h` is present, and `python3-config
  --embed` links.
- **Binding layer: pybind11 (FetchContent).** Header-only, the industry standard for embedding
  CPython and exposing C++, and consistent with how the project already vendors third-party libs
  (Catch2, toml++, box3d, imgui via FetchContent; Vulkan/Threads via `find_package`). It keeps the
  reflection-driven *generic* bridge small — a real reliability win. (Alternatives considered:
  raw CPython C-API — 3–5× the code, manual refcounting; pocketpy — not real CPython. Both
  rejected.)
- **Tier wall (like Box3D/Vulkan).** The engine depends only on a **neutral `ScriptEngine`
  interface** (`engine/script/`); the CPython + pybind11 implementation lives in
  **`modules/script_python`** and never leaks Python types above it. So Python is a swappable
  backend, and the engine/foundation tiers stay Python-free.
- **New build requirements (introduced by this pillar):** a Python dev install (libpython
  headers + shared lib, found via `find_package(Python3 Development)`) and pybind11 (FetchContent).
  This is the one place the pillar necessarily adds external dependencies — scripting cannot exist
  without a Python runtime. Landed and reviewable in **S1** (the natural veto point).

## Scope (v0.1)

**In:** embed CPython behind a neutral `ScriptEngine`; expose the reflected component/entity API to
Python (read/write properties, query/create/destroy entities); command/Play access; a
`ScriptComponent` that runs a script's `on_update` each tick; a Studio way to run a script against
the live world.

**Deferred (post-v0.1 / separate work):** additional language bindings (JS/Lua/WASM — the "one
reflected API, many languages" is honored by generating from reflection, but only Python ships);
hot-reload of scripts; a Python debugger/profiler; pip/package management for script plugins;
sandboxing/permissions enforcement for untrusted scripts (the `required_permissions` hook exists
in #11 but enforcement is later); calling engine events from Python beyond the basic surface.

## Architectural spine (every slice honors this)

- **Tier contract** ([ARCHITECTURE.md](../../architecture/ARCHITECTURE.md)): `foundation/` and
  `engine/` never include Python. The neutral `ScriptEngine`/`ScriptValue` interface lives in
  `engine/script/`; the CPython/pybind11 backend is `modules/script_python`, injected like the
  physics backend (`add_system`/constructor injection).
- **Bindings derive from reflection (#18).** The Python↔C++ bridge is **generic**: it walks the
  reflected field surface (`FieldVisitor` / `to_value`/`from_value`) rather than hand-writing a
  binding per component. Adding a new reflected component makes it scriptable for free.
- **Serialization is #10.** Values cross the boundary through the `Value` DOM where practical, so
  the bridge reuses the existing to/from-value machinery.
- **Determinism / headless-testability.** Every slice through S4 is headlessly unit-testable
  (run a script string, assert C++ state; run a scripted tick, assert deterministic mutation).
  Only S5's editor console leans on the standing functional-launch check.

## Sub-slice breakdown & order

1. **S1 — Python embedding + smoke test.** A neutral `ScriptEngine` interface (`init`, `run_string`,
   error/stdout capture) + `modules/script_python` (CPython via `find_package`, pybind11 via
   FetchContent) implementing it. Headless test: run `"result = 6*7"` / a script that sets a value,
   read it back; a syntax error surfaces as a captured error, not a crash. *Lands the runtime +
   the dependency additions — the veto point.*
2. **S2 — Reflection → Python: component properties.** Expose a reflected component to Python so a
   script reads/sets its properties by name, via the #18 bridge (generic). E.g. a script sets a
   `Transform`'s position; C++ reads the change. Headless round-trip test.
3. **S3 — World + commands.** A script queries live entities, creates/destroys them, and invokes a
   command / toggles Play (#11 command surface). Headless test: a script creates N entities and
   fires a command; C++ observes both.
4. **S4 — `ScriptComponent` (the core payoff).** A reflected `ScriptComponent` referencing a Python
   script/callable; a `ScriptSystem` runs each scripted entity's `on_update(dt)` per `World::tick`.
   "Attach a Python script, press Play." Headless deterministic test: a scripted entity moves a set
   amount per tick, bit-identically across runs.
5. **S5 — Studio script console.** A Studio panel/menu that runs a script string against the live
   editor `World` (reusing S1–S3), so you can script the running scene and watch the viewport
   update. The visible demo; trimmable if v0.1 is tight.

**Suggested order:** S1 → S2 → S3 → S4 → S5 (linear). First buildable is **S1**. S1–S4 complete the
roadmap's Scripting pillar functionally; S5 is the visible cap.

## Cross-cutting decisions (fixed here)

- **CPython + pybind11**, walled behind a neutral `ScriptEngine` in `modules/script_python`.
- **Bindings are generic + reflection-derived** (#18), not per-component hand-written.
- **Scripting/CLI/editor glue** stays in `engine/script` (neutral) + `modules/script_python`
  (backend) + `app`/`studio` (entry/console); `foundation`/`engine` stay Python-free.
- Each sub-slice gets its own brainstorm → spec → plan → TDD cycle.

## Acceptance (tracking-level; each sub-issue refines its own)

- [ ] CPython embeds behind a neutral `ScriptEngine`; a script string runs + errors are captured
      (S1).
- [ ] A script reads/sets a reflected component's properties, generically (S2).
- [ ] A script queries/creates entities and invokes a command / Play (S3).
- [ ] A `ScriptComponent`'s `on_update` runs each tick, deterministically (S4).
- [ ] The Studio can run a script against the live world (S5).

## Depends on / relates

- **Depends on:** #18 reflection (the binding surface), #11 commands/events (Play/commands), the
  runtime `World`, #10 serialization (`Value` bridge), the app/CLI shell (#35), the Studio (#37).
  **Completes** the v0.1 vertical slice (foundation → rendering → physics → tools → **scripting**).
- **Relates:** the deferred automation/agent API (`design/automation-and-agents.md`) targets the
  same reflected surface; additional language bindings (JS/Lua) reuse this reflection-derived
  bridge.
