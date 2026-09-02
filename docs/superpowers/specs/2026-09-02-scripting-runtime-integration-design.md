# Scripting runtime integration — wire ScriptSystem + scripted demo + physics toggle (design)

**Builds on:** the Scripting pillar S1–S5 (all merged): S4 `ScriptComponent`/`ScriptSystem`, S5 the
Studio console + `bind_world(const ComponentReflection&)`. Also #37 Studio, the runtime loop, physics.
**Date:** 2026-09-02.

> Closes a v0.1 vertical-slice gap: `ScriptSystem` (built + unit-tested in S4) was never wired into
> the runtime, so "attach a Python script → press **Play** → **Build** a standalone that plays" did
> not actually work end-to-end. This wires it in and ships a scripted demo object to prove it.

## Summary

Wire the existing `ScriptSystem` into `setup_runtime` — the one scaffolding function called by both
the editor (`mosaic editor`) and the windowed runtime (`mosaic run --window`, which is what a built
bundle launches). Add a small scripted demo object (an orbiting cube driven by a `ScriptComponent`)
to the default scene so there is something to watch move. Add an editor-only checkbox that toggles
that object between **script-driven** (orbits) and **physics-driven** (falls). The result is the
honest v0.1 slice, live: `create → move (Inspector) → physics → script → Play → Build → run`.

## Scope

**In:**
- `ScriptSystem` added inside `setup_runtime`, **before** the `PhysicsSystem`, so scripts tick in the
  editor's Play loop and in the built standalone. `setup_runtime`'s signature grows to accept the
  `ComponentReflection` + an engine factory.
- A scripted demo object in `build_default_scene` (reflected `ScriptComponent`, so it serializes and
  survives `save → mosaic build → mosaic run`).
- An editor **"Demo: physics"** checkbox that swaps the demo object between its `ScriptComponent` and
  a dynamic `RigidBody`+`Collider`. The swap logic is a pure, headlessly-testable function; the
  checkbox is thin glue.

**Deferred (recorded for the roadmap):**
- A dedicated slider / color-picker / Euler-rotation **controls panel** (a Phase-4-ish editor-UX
  enhancement beyond v0.1's "inspector" scope). The Inspector already edits position/rotation/scale/
  color as drag-fields; bounded sliders are a nicety, not a v0.1 requirement.
- Scripting in the **headless** `mosaic run` (no `--window`): that path does not call `setup_runtime`
  and has no render/physics; it stays a minimal determinism-tick path. The bundle uses `--window`, so
  the product slice is covered. (If headless scripting is wanted later, add `ScriptSystem` there too.)
- `PlayState`/Play-toggle from a script (needs a neutral host-callback — unchanged from S5).

## Architecture

### 1. `ScriptSystem` in `setup_runtime` (`app/run/demo_scene.hpp`)

`setup_runtime` is the shared "runtime scaffolding for ANY scene" (its own comment). It is called by
`app/main.cpp:188` (windowed `mosaic run` — the bundle's path) and `app/run/editor.cpp:120` (editor).
Change:

```cpp
// was: inline RuntimeScene setup_runtime(World& world, AssetRegistry& registry)
inline RuntimeScene setup_runtime(
    mosaic::engine::World& world,
    mosaic::engine::assets::AssetRegistry& registry,
    const mosaic::engine::reflection::ComponentReflection& reflection,
    mosaic::engine::script::ScriptSystem::EngineFactory script_factory)
{
    // Gameplay scripts run BEFORE physics so a dynamic body's write-back wins for physics objects,
    // and a script-only object (no RigidBody) is left untouched by physics.
    world.add_system<mosaic::engine::script::ScriptSystem>(reflection, std::move(script_factory));
    world.add_system<physics::PhysicsSystem>( /* ...existing box3d backend... */ );
    // ...existing texture registration + RenderSystem unchanged...
}
```

`demo_scene.hpp` gains `#include "engine/script/script_system.hpp"` (+ `script_component.hpp`) — both
**Python-free neutral** headers. The CPython backend enters only through the injected `script_factory`
(`std::function`), supplied by the callers — the same tier seam as the S5 console. **Callers:**
- `app/main.cpp` (`#include "modules/script_python/python_script_engine.hpp"`; `types` in scope):
  `setup_runtime(world, asset_registry, types, [] { return mosaic::script::python::create_python_script_engine(); });`
- `app/run/editor.cpp` (already includes it; `reflection` in scope): same call with `reflection`.

**Tick order (verified):** In the windowed run, `main.cpp` adds `TransformSystem` first, then
`setup_runtime` adds `[ScriptSystem, PhysicsSystem]`. In the editor, `setup_runtime` adds
`[ScriptSystem, PhysicsSystem]` and `TransformSystem` runs separately each frame. Either way
`ScriptSystem` precedes `PhysicsSystem`.

### 2. Scripted demo object (`build_default_scene` in `app/run/demo_scene.hpp`)

A new visible entity — a bright cube, **no `RigidBody`** (the script owns its Transform):

```cpp
const auto orbiter = world.create_entity();
world.add_component<transform::Transform>(orbiter,
    transform::Transform{Vec3{3, 2, 0}, Quat::identity(), Vec3{0.5f, 0.5f, 0.5f}, {}});
world.add_component<render::MeshRenderer>(orbiter, render::MeshRenderer{render::kCubeMeshId});
world.add_component<render::Material>(orbiter, render::Material{Vec3{0.2f, 0.9f, 0.3f}, {}}); // green
world.add_component<mosaic::engine::script::ScriptComponent>(orbiter,
    mosaic::engine::script::ScriptComponent{kOrbitScript});
```

with the orbit script as a `constexpr` string:

```python
import math
_t = 0.0
def on_update(dt):
    global _t
    _t += dt
    tr = get_component(entity, 'Transform')
    tr['position']['x'] = 3.0 * math.cos(_t)
    tr['position']['z'] = 3.0 * math.sin(_t)
    tr['position']['y'] = 2.0
    set_component(entity, 'Transform', tr)
```

`import math` works (real CPython). Deterministic (`cos`/`sin` of a fixed-`dt` accumulator). Because
`ScriptComponent` is reflected (#18/S4), the object round-trips through `save_to_json` → `mosaic
build` → `mosaic run`, so the standalone plays it.

### 3. Physics toggle (editor-only) — `studio/`

**Why a swap, not a layer:** a `ScriptComponent` and a dynamic `RigidBody` cannot both drive one
Transform — the script's per-tick write is seen by `PhysicsSystem` step-2 as an external edit and
teleports the body, so they fight. The toggle therefore **switches** the object between the two.

- **Pure logic** (`studio/demo_physics.hpp`, no ImGui, testable): a free function
  ```cpp
  // Switch the demo entity between script-driven and physics-driven. Idempotent per target state.
  void set_demo_physics(mosaic::engine::World& world, mosaic::foundation::EntityID e,
                        const std::string& script_source, bool physics);
  ```
  `physics==true`: `remove_component<ScriptComponent>`; `add_component<RigidBody>({true,1.0f})`;
  `add_component<Collider>({Vec3{0.5,0.5,0.5}})`. `physics==false`: remove `RigidBody`+`Collider`;
  `add_component<ScriptComponent>({script_source})`.
- **StudioApp state:** the demo object is identified as *the (first) entity that has a
  `ScriptComponent`*; on first sight StudioApp caches `std::optional<EntityID> demo_entity_` +
  `std::string demo_script_` + `bool demo_physics_ = false`. A `char`-free checkbox in the toolbar
  (beside Play/Stop) calls `set_demo_physics(world, *demo_entity_, demo_script_, demo_physics_)` when
  toggled. The checkbox is disabled with a tooltip if no scripted demo object is found (e.g. a scene
  saved in physics mode — see below).
- **Editor-only; persists via save.** There is no toggle in a headless bundle; the object plays
  whatever components the scene carries. The default scene ships in **script mode** (orbiting). If the
  user saves while in physics mode, the saved scene has the `RigidBody` (no `ScriptComponent`) and the
  bundle plays a falling cube — a valid state; the editor simply can't re-derive the script source on
  reload (the toggle greys out). Documented, acceptable for the demo.

`studio/` may include `engine/physics/physics_components.hpp` + `engine/script/script_component.hpp`
(it links `mosaic_engine`) — no new dependency, no Python.

## Data flow

```
mosaic run --window <bundle>  ->  setup_runtime(world, reg, types, py_factory)
   adds [ScriptSystem, PhysicsSystem];  RuntimeLoop drives world.tick(dt)
   -> ScriptSystem: orbiter.on_update(dt) sets Transform;  PhysicsSystem: falling boxes
editor: same setup_runtime; Play starts the loop -> orbiter orbits, boxes fall
editor toolbar "Demo: physics" ON -> set_demo_physics(..., true) -> cube now falls under physics
```

## Error handling

- Unchanged script-error behavior (S4): a throwing/`on_update`-less `ScriptComponent` is non-fatal —
  `ScriptSystem` ignores the failed per-tick result; the tick and other objects proceed.
- The toggle operates only when a demo entity is cached; otherwise the checkbox is disabled (no crash).
- A null/absent engine factory would make `ScriptSystem` create no engines (S4 behavior) — but both
  callers pass a real factory; the app links `mosaic_script_python` (since S5).

## Testability

- **Wiring test** (`tests/unit/app/script_runtime_test.cpp`, links `mosaic_app` + `mosaic_script_python`,
  both already linked): build a `World` + `TypeRegistry` + `ComponentReflection`
  (`register_builtin_components`); add an entity with a `Transform` + a **linear-mover**
  `ScriptComponent` (`position.x += dt`); call `setup_runtime(world, registry, types, [] { return
  create_python_script_engine(); })`; `world.tick(0.25f)` four times; assert `position.x == 1.0f`
  (exact) — proving `setup_runtime` actually adds `ScriptSystem` and it ticks in the runtime.
- **Default-scene test:** `build_default_scene` produces exactly one entity with a `ScriptComponent`
  (the orbiter); update any existing test that asserts the default scene's entity count/contents
  (adding the orbiter bumps the count) — the same kind of fix as the S5 panel-count test.
- **Toggle logic test** (`tests/unit/studio/demo_physics_test.cpp`): `set_demo_physics(world, e, src,
  true)` → `e` has `RigidBody` and no `ScriptComponent`; `set_demo_physics(..., false)` → the reverse,
  and the restored `ScriptComponent.source == src`.
- Full suite green. **Visual acceptance (user):** `mosaic editor` → Play (cube orbits + boxes fall) →
  toggle physics (cube falls) → optionally `mosaic build` a bundle and run it (cube orbits on its own).
  The `mosaic run --window` smoke path stays intact.

## Ground rules honored

- **Tier wall:** `demo_scene.hpp`/`studio` use the neutral `ScriptSystem`/`ScriptComponent` +
  reflected physics components + a `std::function` factory — no pybind11. CPython enters only via the
  app-supplied factory, exactly like every other backend.
- **Reflection-derived / #10:** the demo object is ordinary reflected components; the script mutates
  through the generic S2/S3 bridge; the `ScriptComponent` serializes as a string and round-trips.
- **Determinism:** single-threaded on the tick thread; the wiring test uses a linear mover for exact
  assertion. No new third-party deps. Catch2 names avoid a leading `--`. EntityID crosses as `uint64`.

## Acceptance

- [ ] `setup_runtime` adds `ScriptSystem` (before physics); a scene with a `ScriptComponent` ticks in
      the runtime — proven by a headless wiring test.
- [ ] `build_default_scene` ships an orbiting scripted demo cube that serializes and survives
      build→run; default-scene tests updated; full suite green.
- [ ] An editor "Demo: physics" checkbox swaps the demo object between script- and physics-driven
      (pure swap function unit-tested); editor Play shows the orbit; the toggle shows the fall.
- [ ] The v0.1 slice is honestly end-to-end: script attached → Play runs it → a built standalone
      plays it. (User visual acceptance.)

## Depends on / relates

- **Depends on:** S4 (`ScriptComponent`/`ScriptSystem`), S5 (`bind_world` const, app links
  `mosaic_script_python`), the runtime loop, `PhysicsSystem`, `build_default_scene`, the T4 bundle.
  **Completes** the v0.1 vertical slice's scripting workflow end-to-end.
- **Relates:** the deferred controls-panel roadmap item; headless-run scripting; Play-from-script.
