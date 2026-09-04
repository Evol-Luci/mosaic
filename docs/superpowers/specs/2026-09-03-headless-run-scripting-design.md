# Headless `mosaic run` Scripting — Design

## Problem

`mosaic run` only ticks scripts and physics when launched with `--window`.
In `app/main.cpp`, the headless world gets **only** `TransformSystem`
(line 126); the call to `setup_runtime` — which adds `ScriptSystem` +
`PhysicsSystem` (+ a `RenderSystem`) — lives **inside** the
`if (options.window)` block. So `mosaic run <scene>` with no window advances
transforms only: a scripted or physics-driven scene is inert headlessly.

This blocks headless gameplay, CI simulation, and server/tooling use of the
runtime — and it undercuts the rest of the scripting bucket (play-from-script,
`.py`-as-asset, worker thread), which all want a headless run to verify
against.

## Goal

`mosaic run <scene>` (no `--window`) runs the same simulation systems as the
windowed run — `ScriptSystem` and `PhysicsSystem` tick — just without
rendering. Windowed behaviour is unchanged.

## Approach

`setup_runtime` is already **device-free** by design (its own comment:
"Needs no device (GPU uploads happen at render time)") and its script-wiring
is already unit-tested at world level (`tests/unit/app/script_runtime_test.cpp`).
The only bug is that `main.cpp` calls it conditionally.

**Hoist `setup_runtime` (and the project-asset load) out of the
`if (options.window)` block** so both modes wire the simulation systems. The
returned `render_system` is used only by the window path's `on_frame` render
hook; headless simply holds it unused (a cheap `shared_ptr`).

### `app/main.cpp` change (structural)

```
// after populate_world(...), BEFORE the window branch:
auto render_system = setup_runtime(world, asset_registry, types,
    [] { return mosaic::script::python::create_python_script_engine(); }).render_system;
if (options.assets_dir) { load_project_assets(asset_registry, *options.assets_dir); ... }

if (options.window) {
    ... glfw/vulkan init ...
    deps.on_frame       = [ ... render_system ... ];   // unchanged body
    deps.stop_requested = [glfw_window] { ... };        // unchanged
} else {
    deps.stop_requested = [] { return g_stop.load(); }; // unchanged
}
```

Nothing else moves. The system tick order is identical to today's windowed
path (`TransformSystem` added at line 126, then Script → Physics → Render from
`setup_runtime`).

## Testing

**New integration test** `tests/unit/app/headless_run_test.cpp` — exercises the
exact headless pipeline (`setup_runtime` + `FramePump.run`, no window, no
`on_frame`) end to end:

- Build a `World`, add `TransformSystem`, create an entity with a `Transform`
  and a `ScriptComponent` whose `on_update(dt)` increments `position.x` by `dt`.
- `setup_runtime(world, registry, types, python-factory)`.
- Run through `FramePump` with a `StepClock` and `options.ticks = N`
  (mirrors `frame_pump_test.cpp`'s harness), headless `PumpDeps` (no `on_frame`).
- Assert the entity's `position.x` advanced by the simulated time and
  `summary.stopped_by == "ticks"` — i.e. scripts really ticked under a headless
  run, not just a bare `world.tick` loop.

Existing `script_runtime_test.cpp` (world-level `setup_runtime` wiring) and
`frame_pump_test.cpp` (pump semantics) remain green.

**Manual acceptance (self-verifiable, no GUI):** run the real binary headless
on a scene with a logging script — `mosaic run <scene.json> --ticks 3` — and
confirm the script's per-tick log output appears and the process exits 0.

## Out of scope (YAGNI)

- Factoring `main()`'s run body into a testable `run_app` (deferred; would help
  later slices but isn't needed for this fix — the integration test covers the
  composition).
- Headless rendering / offscreen capture.
- Any change to the windowed path's behaviour.
