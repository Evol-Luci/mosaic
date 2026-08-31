# Scripting S4 — ScriptComponent + ScriptSystem (design)

**Decomposition:** [scripting-decomposition](2026-08-30-scripting-decomposition.md) (slice S4 — the core payoff).
**Builds on:** S1 (`ScriptEngine`), S2 (`bind_world`/`set_global`/`get_component`/`set_component`),
S3 (entity/world script surface), #18 reflection, the ECS `System`/`World::tick`.
**Date:** 2026-08-30.

> Built semi-autonomously (user approved the scripting cadence at PR #77: build → PR each slice).

## Summary

"Attach a Python script, press Play." A reflected **`ScriptComponent`** holds a Python source string
defining `on_update(dt)`; a **`ScriptSystem`** runs each scripted entity's `on_update(dt)` once per
`World::tick`, deterministically. The script mutates its entity through the S2/S3 surface
(`get_component`/`set_component`, with the entity id available as the `entity` global). This is the
slice that turns scripting from "run a string" into "scripted behavior that ticks with the world" —
the roadmap's promised workflow.

## Scope

**In:**
- **`ScriptComponent`** (`engine/script/script_component.hpp`): a reflected component
  `{ std::string source; }` — the Python source that defines `on_update(dt)`. Reflected (serializes
  via #10, shows read-only in the inspector via `field_readonly`), registered in
  `register_builtin_components` as `"ScriptComponent"`.
- **`ScriptSystem`** (`engine/script/script_system.hpp`): an `ecs::System` that, per tick, runs each
  scripted entity's `on_update(dt)`. Python-free — it drives the **neutral** `ScriptEngine` and takes
  an injected engine factory (`std::function<std::unique_ptr<ScriptEngine>()>`), so the CPython
  backend is supplied by the caller (app/test), not linked into the engine tier.

**Deferred:**
- **Script assets / file references** — v0.1 inlines the source in the component. Loading a
  `ScriptComponent` from a `.py` asset (via the T5 asset system) is a post-v0.1 refinement.
- **Lifecycle hooks beyond `on_update`** (`on_start`/`on_destroy`/event handlers) — v0.1 ships the
  per-tick `on_update(dt)` entry point only. (`on_start` is a natural next add: run once on first
  tick; noted, not built.)
- **Hot-reload UI**, a Studio "attach script" panel, and pressing Play in the editor — that is S5
  (the Studio console/integration). S4 is the headless engine mechanism S5 drives.
- **Undo of per-tick script mutations** — per-tick simulation must NOT flood the undo stack, so
  `ScriptSystem` binds the world only (S2 direct `set_value` path), NOT a `CommandStack`. Undoable
  scripting (S3) is for interactive/console use, not the tick loop.

## Architecture

### 1. `ScriptComponent` (`engine/script/script_component.hpp`) — reflected, Python-free

```cpp
namespace mosaic::engine::script
{
// A Python behavior attached to an entity. `source` is a script that defines `on_update(dt)`;
// ScriptSystem runs it each tick. Reflected, so it serializes with the scene and appears in the
// inspector (read-only string for v0.1). No Python types here — just the source text.
struct ScriptComponent
{
    std::string source;
};

template <class V>
void reflect(V& visitor, ScriptComponent& c)
{
    visitor.member("source", c.source);
}
} // namespace mosaic::engine::script
```

Registered in `engine/reflection/builtin_components.hpp`:
`types.register_component<mosaic::engine::script::ScriptComponent>(registry, "ScriptComponent");`
(`builtin_components.hpp` gains `#include "engine/script/script_component.hpp"` — still Python-free.)

### 2. `ScriptSystem` (`engine/script/script_system.hpp`) — engine tier, Python-free

An `ecs::System` driving the neutral `ScriptEngine`. Holds a **per-entity engine cache** — runtime,
non-authoritative state (the authoritative source lives in the component); rebuilt on demand. This
is the render-system-caches-GPU-resources pattern, not game state in a system.

```cpp
namespace mosaic::engine::script
{
class ScriptSystem : public ecs::System
{
public:
    using EngineFactory = std::function<std::unique_ptr<ScriptEngine>()>;

    // `types` and `factory` are non-owning/kept by the caller. The factory yields a fresh
    // ScriptEngine (its own namespace) per scripted entity.
    ScriptSystem(reflection::ComponentReflection& types, EngineFactory factory);

    // Per tick: run on_update(dt) for every entity with a ScriptComponent.
    void update(World& world, float dt) override;

private:
    struct Entry { std::unique_ptr<ScriptEngine> engine; std::string source; };
    reflection::ComponentReflection& types_;
    EngineFactory factory_;
    std::unordered_map<std::uint64_t, Entry> engines_; // keyed by EntityID.value()
};
} // namespace mosaic::engine::script
```

`update(world, dt)`:
1. `world.for_each<ScriptComponent>([&](EntityID e, ScriptComponent& sc){ ... })`:
   - **Ensure/compile:** look up `engines_[e.value()]`. If absent, or its cached `source` differs
     from `sc.source` (source edited → recompile, resetting script state): `entry.engine =
     factory_();`, `entry.source = sc.source;`, `entry.engine->bind_world(world, types_);`,
     `entry.engine->set_global("entity", Value(uint64(e.value())));`, then
     `entry.engine->run_string(sc.source)` to define `on_update` + run any top-level setup.
   - **Tick:** `entry.engine->set_global("dt", Value(double(dt)));`
     `entry.engine->run_string("on_update(dt)");`. The result is captured, never thrown
     (non-fatal — a per-tick script error skips that entity's mutation, doesn't crash the tick or
     stop other entities). `entity` is set once at compile (constant per engine); `dt` each tick.
2. **Prune:** drop cache entries for entities not visited this tick (destroyed or de-scripted), so
   the cache tracks the live scripted set.

The `access_` set (declared in the ctor) is left permissive/empty for v0.1: a script may touch any
reflected component through the generic bridge, so a precise static read/write set isn't knowable —
documented; the scheduler that would use `access_` doesn't exist yet.

### 3. Wiring

The app/editor registers it with the real backend factory:
```cpp
world.add_system<mosaic::engine::script::ScriptSystem>(
    types, [] { return mosaic::script::python::create_python_script_engine(); });
```
`add_system` constructs in place; the factory (a neutral `std::function`) is where the Python
dependency enters — the engine tier never names pybind11. (v0.1 headless S4 is fully testable; the
editor wiring + Play button is S5.)

## Data flow

```
// entity e has Transform{position.x=0} + ScriptComponent{ "def on_update(dt):\n
//   t=get_component(entity,'Transform'); t['position']['x']+=dt; set_component(entity,'Transform',t)" }
world.add_system<ScriptSystem>(types, py_factory);
world.tick(0.25f);  // ScriptSystem: compile e's script (bind_world, entity=e), on_update(0.25) -> x=0.25
world.tick(0.25f);  // cached engine: on_update(0.25) -> x=0.50
// ... deterministic: after 4 ticks of 0.25, x == 1.0, bit-identical across runs.
```

## Error handling

- A script with no `on_update`, or whose `on_update` raises → `run_string("on_update(dt)")` returns
  `ScriptResult{ ok=false }`; the system ignores it (that entity's mutation just doesn't happen).
  The tick and other entities proceed. Never throws into the tick loop.
- A syntax error in `source` → the compile `run_string(source)` fails; `on_update` is undefined, so
  the per-tick call is a captured `NameError`. Non-fatal, same as above.
- Editing `source` between ticks → detected by the cached-source compare → engine recreated (script
  state reset). Removing the `ScriptComponent` or destroying the entity → the cache entry is pruned.
- Per-tick mutations use the S2 direct path (no `CommandStack` bound), so they do not touch undo.

## Testability

Headless (`tests/unit/script/script_system_test.cpp`, links the CPython backend for the factory):

- **Deterministic per-tick mutation:** an entity with `Transform` + a `ScriptComponent` whose
  `on_update` adds `dt` to `position.x`; add a `ScriptSystem(types, create_python_script_engine)`;
  `world.tick(0.25f)` four times → `position.x == 1.0f` exactly (0.25 is binary-exact). Run the whole
  setup twice → the two final values are bit-identical (`==`).
- **Script state persists across ticks:** an `on_update` that increments a script-global counter and
  writes it into a component field proves the per-entity engine (and its namespace) is cached, not
  recreated each tick.
- **Recompile on source change:** change the component's `source` between ticks → the new behavior
  takes effect (engine recreated).
- **Errors are non-fatal:** an entity whose `on_update` raises does not crash `tick`, and a second
  scripted entity still updates in the same tick.
- **`ScriptComponent` reflection** (in `tests/unit/reflection/` or the scene test): registered in
  `register_builtin_components`; `component_value`/`set_value` round-trip its `source` string.

## Ground rules honored

- **Tier wall:** `engine/script` (`ScriptComponent`, `ScriptSystem`) stays Python-free — it uses the
  neutral `ScriptEngine` + a `std::function` factory; all pybind11 stays in `modules/script_python`.
  `builtin_components.hpp` gains a Python-free include.
- **Reflection-derived (#18):** `ScriptComponent` is an ordinary reflected component; the script
  mutates entities through the generic S2/S3 bridge — no per-component binding.
- **Serialization is #10:** `ScriptComponent.source` serializes as a string; scenes with scripts
  save/load and construct generically.
- **Determinism:** single-threaded, run on the tick thread; a pure-arithmetic `on_update` is
  bit-reproducible. No new third-party deps. Catch2 names avoid a leading `--`. EntityID crosses as
  `uint64`.

## Acceptance

- [ ] `ScriptComponent` reflected + registered; its `source` round-trips through reflection.
- [ ] `ScriptSystem` runs `on_update(dt)` per tick for each scripted entity, driving the neutral
      `ScriptEngine` via an injected factory (engine tier Python-free).
- [ ] A scripted entity moves a set amount per tick, deterministically and bit-identically across
      runs; script state persists across ticks; source edits recompile.
- [ ] A throwing/`on_update`-less script is non-fatal (tick + other entities proceed). Full suite green.

## Depends on / relates

- **Depends on:** S1–S3 (`ScriptEngine`, `bind_world`/`set_global`, entity/world script surface),
  #18 reflection, the ECS `System`/`World::tick`. **Blocks:** S5 (the Studio attaches a
  `ScriptComponent` and presses Play — driving this system against the editor world).
- **Relates:** deferred `on_start`/event hooks, script assets, and hot-reload all extend this
  component/system pair.
