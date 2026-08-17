# Next Steps — Build Handoff

This is the live "you are here" for a worker picking up Mosaic. It points at the
next buildable work and the decisions that constrain it. When the work below is
done, update this file to point at what comes next.

**Last updated:** 2026-08-16 (runtime loop #22 built — v0.1 engine core complete)

## Where we are

- Docs, architecture, and the licensing/CLA groundwork are in place.
- **[RFC 0001: Core Data Model](../rfcs/0001-core-data-model.md) is Accepted.** The
  four questions that gated the foundation are settled (see the RFC's *Resolved
  decisions*), so the data-model portion of **Phase 0 — Foundation** is unblocked
  and ready to implement.

**ECS core ([#9](https://github.com/Evol-Luci/mosaic/issues/9)) is built and
merged.** The C++ toolchain is stood up (CMake + CMakePresets, C++20, Catch2 via
FetchContent), and the first foundation code is in place:

- `foundation/core/entity_id.hpp` — `EntityID`, the **32 index / 32 generation**
  handle; `EntityID(0)` null; slot **retired** (not recycled) on generation exhaustion.
- `engine/ecs/` — `EntityRegistry` (slot table, free-list recycle, stale-handle
  detection, retirement), sparse-set component storage (`ComponentRegistry` +
  a minimal `ComponentTypeID` shim), and the declaration-only `System` contract.
- `tests/unit/ecs/` — unit tests, all green.

**Serialization ([#10](https://github.com/Evol-Luci/mosaic/issues/10)) is built and
merged.** Reflection-driven, lossless cross-format projection; normative stance in
[RFC 0001 *Serialization stance*](../rfcs/0001-core-data-model.md#serialization-stance):

- `foundation/serialization/` — `Value` DOM, `AssetID`/`Handle<T>`/`ReferenceResolver`,
  reflection bridge (`to_value`/`from_value`), JSON backend (nlohmann), TOML backend
  (toml++), and error handling via `SerializationError`. `from_json`/`from_toml`
  guard against adversarial deeply-nested input with a configurable
  nesting-depth cap (default 128) that throws `SerializationError` rather than
  overflowing the stack — the non-blocking hardening follow-up from the #10 review. ✓
- `tests/unit/serialization/` — unit tests for every tag: `[value]`, `[reference]`,
  `[reflect]`, `[refenc]`, `[json]`, `[toml]`, `[crossfmt]` — 66 tests, all green.

**Command / event system ([#11](https://github.com/Evol-Luci/mosaic/issues/11)) is
built and merged.** The last Phase 0 foundation primitive; normative semantics live
in [design/command-event-system.md](../design/command-event-system.md), and
`required_permissions` ties into
[design/automation-and-agents.md](../design/automation-and-agents.md):

- `foundation/commands/` — `Command` (the `id, parameters, required_permissions,
  execute(), undo(), serialize()` contract, with a default `serialize()` that
  projects `{ id, parameters, required_permissions }` through the serialization
  `Value` DOM) and `CommandStack`, the undoable transaction log (execute pushes +
  clears redo; undo inverts LIFO; redo re-executes).
- `foundation/events/` — `EventBus`: synchronous subscribers fire in-tick before
  `emit` returns; deferred subscribers are served from a **double-buffered queue**
  drained once per tick, FIFO in global emit order across all event types, never
  mid-tick. A re-emit from inside a drain defers to the next tick.
- `tests/unit/events/` + `tests/unit/commands/` — `[events]`, `[commands]` unit
  tests plus a command/event integration test (`[commands][events]`): a deferred
  event emitted by a command announces on the next drain, and **undo announces
  nothing** — the "enqueue on execute, not undo" acceptance. 80 tests, all green.
  - *Interpretation note for the World layer:* the RFC says a deferred event
    enqueues "on execute, not when undone/redone." `CommandStack::redo()` re-runs
    the command's `execute()`, so a redo **does** re-announce — events live in the
    command body, not the log, and the UI must observe a redo. This resolves the
    RFC's ambiguity in the natural direction (redo == re-execute); flag it if the
    World/UI layer needs redo to be silent.

The three Phase 0 foundation implementation issues (#9, #10, #11) are now complete.

**The runtime `World` (Engine layer) is built and merged.** The first Engine unit
after the foundation primitives, designed and planned in
[specs/2026-08-16-engine-world-runtime-design.md](../superpowers/specs/2026-08-16-engine-world-runtime-design.md)
and [plans/2026-08-16-engine-world-runtime.md](../superpowers/plans/2026-08-16-engine-world-runtime.md):

- `engine/world/` — `World`: owns the `EntityRegistry`, `ComponentRegistry`, and a
  per-world `EventBus`; delegates entity/component ops; runs registered systems.
  `tick(dt)` does exactly three things in order: **drain deferred events** (the
  #11 fixed sync point), **run systems** in registration order, **commit deferred
  destructions** (`on_destroy → destroy`). `destroy_entity` is deferred so systems
  never have entities vanish mid-`for_each`.
- `engine/ecs/` additions — `SparseSet::each` / `ComponentRegistry::for_each<T>`
  single-component iteration, and `System::update(World&, dt)` (default no-op).
- `tests/unit/world/` + `tests/unit/ecs/` — `[world]` cases plus the interleaved
  create/add/destroy/recycle integration test the #9 handoff asked for. Full suite
  95 tests, all green.

Deliberately **out of scope** (their own future specs): Scene load/save, concrete
systems (e.g. transform), and the outer runtime loop. `World` exposes `tick(dt)`;
nothing calls it in a loop yet.

**Reflection system ([#18](https://github.com/Evol-Luci/mosaic/issues/18)) is built
and merged.** The gap `World` deliberately left open — reflection-driven *generic*
component construction — is now closed. Designed and planned in
[specs/2026-08-16-reflection-system-design.md](../superpowers/specs/2026-08-16-reflection-system-design.md)
and [plans/2026-08-16-reflection-system.md](../superpowers/plans/2026-08-16-reflection-system.md);
normative intent in [design/reflection.md](../design/reflection.md):

- `foundation/reflection/` — `type_id.hpp` (`TypeID` + `type_id<T>()`, the stable
  within-run type handle) and `type_registry.hpp` (`TypeRegistry`: register a
  reflected type by stable name, look it up by name **or** `TypeID`, enumerate its
  fields via the existing `reflect()` visitor). Pure foundation — no `World`.
- `engine/reflection/component_reflection.hpp` — `ComponentReflection`: the
  engine-layer bridge. `register_component<T>(registry, "Name")` records metadata
  **and** an attach thunk; `construct(world, e, name, value)` deserializes a
  `Value` into a fresh `T` (via the #10 `from_value`) and attaches it to an entity
  — **no concrete C++ type at the call site**. This is the path Scene load/save
  will call.
- The #9 `ComponentTypeID` shim is now a **re-export** of
  `foundation::reflection::TypeID`: one identity source, no drift. `ComponentRegistry`
  is unchanged and now shares its per-type integer with `TypeInfo::id`.
- `tests/unit/reflection/` — `[reflection]` cases: register/lookup by name & id,
  field enumeration, generic construct-and-attach into a `World`, the
  reflect → `to_value` → construct round-trip (nested + vector fields), and
  unknown-name / invalid-entity rejection. Full suite 107 tests, all green.
  - *Durability rule for future workers:* the **persisted** identity of a type is
    its **name** (a string) — Scene files, replication, any on-disk form key on it.
    `TypeID` is a runtime-only handle and may vary in assignment order across runs;
    never persist it.

**Scene load/save ([#19](https://github.com/Evol-Luci/mosaic/issues/19)) is built
and merged.** The first persistence path — a `World`'s entity+component graph to
JSON and back. Designed and planned in
[specs/2026-08-16-scene-load-save-design.md](../superpowers/specs/2026-08-16-scene-load-save-design.md)
and [plans/2026-08-16-scene-load-save.md](../superpowers/plans/2026-08-16-scene-load-save.md):

- `engine/scene/scene.hpp` — `to_value`/`from_value` (World ↔ scene `Value`) and
  `save_to_json`/`load_from_json` (via the #10 backend). Header-only, in-memory
  only; filesystem I/O is a caller / asset-registry concern.
- `engine/ecs/` + `engine/world/` — `EntityRegistry::for_each` /
  `World::for_each_entity` live-entity enumeration (also feeds the future editor
  hierarchy), so component-less entities round-trip faithfully.
- `engine/reflection/` — `ComponentReflection` gained the **save** direction
  (`save`/`has`) symmetric to `construct`; per-entity component order is
  deterministic (sorted type name).
- **Load is two-pass:** create every entity + build a serialized-id → new-handle
  remap, then generically construct components through a `SceneReferenceResolver`
  so intra-scene `EntityID` references remap to the loaded handles. An unknown
  component type name throws `SerializationError`; a reference outside the scene
  resolves to null. `AssetID`/`Handle` ids round-trip now; *resolving* them to
  live resources is the asset registry (#20).
- `tests/unit/scene/` — `[scene]` cases: populated round-trip (empties preserved),
  non-trivial reference remap (bumped generation), broken-ref → null, unknown-type
  throw, malformed-JSON throw, JSON convenience round-trip. Full suite 120 tests,
  all green.

**Asset registry ([#20](https://github.com/Evol-Luci/mosaic/issues/20)) is built
and merged.** The runtime-side registry — resolve/cache `AssetID` → loaded
resources, validate references on deserialize. Designed and planned in
[specs/2026-08-16-asset-registry-design.md](../superpowers/specs/2026-08-16-asset-registry-design.md)
and [plans/2026-08-16-asset-registry.md](../superpowers/plans/2026-08-16-asset-registry.md):

- `engine/assets/asset_metadata.hpp` — `AssetMetadata` (uuid, type, version, source,
  dependencies, open-ended metadata/import_settings), reflected, round-tripping
  through TOML via #10.
- `engine/assets/asset_registry.hpp` — `AssetRegistry`: `register_asset(meta, loader)`
  (lazy) / `register_resource<T>` (eager), `load<T>(id)` resolves + caches
  (loader invoked once), `metadata`/`contains`. Resources are type-erased
  (`shared_ptr<void>` + `load<T>` cast) — no `Resource` base class, since no concrete
  resource types exist until rendering. Missing id → null/nil, non-throwing.
- `AssetReferenceResolver` (same header) — the #10 `ReferenceResolver` hook: on
  deserialize a registered `AssetID`/`Handle` passes through, an unknown one becomes
  a nil handle.
- `foundation/serialization/reference.hpp` gained `std::hash<AssetID>` (mirrors
  `EntityID`), the natural cross-reference key.
- `tests/unit/assets/` — `[assets]` cases: hash-as-map-key, metadata TOML round-trip,
  register/cached-load (loader-once), eager resource, unknown → null, Handle
  validate/nil on deserialize. Full suite 127 tests, all green.
  - **Deferred (small follow-up):** wiring `AssetReferenceResolver` *into* Scene's
    load path (a combined entity-remap + asset-validation resolver), and any real
    loader/resource type (rendering), importer, and cooker.

**Foundation math + transform system ([#21](https://github.com/Evol-Luci/mosaic/issues/21),
[#26](https://github.com/Evol-Luci/mosaic/issues/26)) is built and merged.** The
first concrete `System`, and the math layer the rest of the slice inherits. Designed
and planned in
[specs/2026-08-16-transform-system-design.md](../superpowers/specs/2026-08-16-transform-system-design.md)
and [plans/2026-08-16-transform-system.md](../superpowers/plans/2026-08-16-transform-system.md):

- `foundation/math/{vec3,quat,mat4}.hpp` — minimal, hand-rolled (no GLM), header-only.
  **Conventions now fixed for rendering/physics:** column-major storage, column
  vectors, right-handed, Y-up, radians; `world = parent * local`, `local = T*R*S`.
  `Vec3`/`Quat` are reflected so transforms serialize.
- `engine/transform/transform.hpp` — reflected `Transform` (authored local
  position/rotation/scale + optional `parent` EntityID, nil = root) and the derived
  `WorldTransform{ Mat4 }` (system-written, not serialized).
- `engine/transform/transform_system.hpp` — `TransformSystem`, the first concrete
  `System`. `update` walks each entity's parent chain (`world(e) = world(parent) *
  local(e)`) and writes its `WorldTransform`; a depth guard bounds malformed cycles.
- `tests/unit/math/` + `tests/unit/transform/` — `[math]` (Vec3/Quat/Mat4 ops, TRS,
  transform_point) and `[transform]` (single→world, parent compose, rotated parent,
  moving a parent updates children next tick, reflection round-trip). Full suite 142
  tests, all green.
  - *Perf follow-up:* `update` recomputes each entity by re-walking ancestors —
    memoize per tick or process parent-first when scenes grow.

**Runtime loop ([#22](https://github.com/Evol-Luci/mosaic/issues/22)) is built and
merged.** The outer driver that finally *runs* the engine. Designed and planned in
[specs/2026-08-16-runtime-loop-design.md](../superpowers/specs/2026-08-16-runtime-loop-design.md)
and [plans/2026-08-16-runtime-loop.md](../superpowers/plans/2026-08-16-runtime-loop.md):

- `engine/runtime/runtime_loop.hpp` — `RuntimeLoop`: a deterministic fixed-timestep
  accumulator. `advance(world, real_dt)` banks externally-supplied elapsed time and
  dispatches whole `World::tick(fixed_dt)` calls; `step` is manual single-step;
  `start`/`stop` pause/resume; a `max_steps_per_advance` cap guards the spiral of
  death. Owns pacing **only** — the per-tick order stays in `World::tick`.
- The real `std::chrono` measure-and-sleep frame pump is deliberately **not** here —
  it belongs to the app/CLI (`mosaic run`) that owns the process main loop; the loop
  consumes an injected `real_dt` so it stays deterministic and testable.
- `tests/unit/runtime/` — `[runtime]` cases: fixed dispatch + remainder carry,
  start/stop/single-step, determinism (same schedule → equal tick count),
  spiral-of-death cap, and a populated-`World` + `TransformSystem` run stable over
  1000 ticks. Full suite 149 tests, all green.

**The headless engine core of the v0.1 vertical slice is now complete** (#9 ECS,
#10 serialization, #11 commands/events, the runtime `World`, #18 reflection, #19
Scene, #20 assets, #21 transform + #26 math, #22 runtime loop).

## What to build next

The engine core runs, but it is still **headless** — there is nothing to look at yet
and no process `main`. Per the
[v0.1 scope](roadmap.md#the-v01-vertical-slice--the-first-mountain), the frontier now
splits three ways (each its own future issue/spec):

1. **The app / CLI shell** — a `main` (`mosaic run`) that owns the real-time
   `std::chrono` frame pump and drives a `RuntimeLoop`. The smallest step to actually
   *running* a world outside a test, and the first thing that produces observable
   output (even if only logs).
2. **First *visible* subsystem — rendering (Vulkan) or the Studio shell.** The first
   milestone that turns the engine from a test suite into something on screen. Large;
   will want its own decomposition.
3. **Remaining foundation — logging, then jobs.** Logging is the nearest (every
   subsystem wants it); jobs (a scheduler for `World::tick`) comes when
   single-threaded ticking is a bottleneck.

### Foundation still owed for v0.1 (not yet filed)

Two Foundation pieces from the [v0.1 scope](roadmap.md#the-v01-vertical-slice--the-first-mountain)
remain **unbuilt and unfiled** (math is now done, #26):

- **Logging** — a structured logging facility.
- **Jobs** — the job/task system `World::tick` will eventually schedule systems on
  (today systems run single-threaded in registration order, by design).

File issues for them as they come into play.

## Suggested sequence

```
#9 ECS core ✓ ──┬── #10 Serialization ✓
                └── #11 Command/event system ✓
                        │
                        └── World runtime ✓ ──┬── #18 reflection ✓ ── #19 Scene load/save ✓
                                              ├── #20 asset registry ✓
                                              ├── #21 transform system ✓ (+ foundation math #26)
                                              └── #22 runtime loop ✓
```

**The v0.1 headless engine core is complete.** The next frontier is the app/CLI
shell, the first visible subsystem (rendering / studio), and the remaining
foundation pieces (logging, jobs) — see *What to build next*.

#9, #10, #11, the World runtime, #18 reflection, #19 Scene load/save, #20 asset
registry, #21 transform system (+ foundation math #26), and #22 runtime loop are all
done. **The v0.1 headless engine core is complete** — the frontier moves off this
tree to the app/CLI shell, the first visible subsystem, and the remaining foundation
pieces (see *What to build next*).

## Ground rules for the worker

- Honor the tier/dependency contract in
  [ARCHITECTURE.md](../architecture/ARCHITECTURE.md): `foundation/` knows nothing
  above it; `engine/` depends only on `foundation/`.
- Each issue lists acceptance criteria and expects unit tests under `tests/unit/`.
- ~~Reflection metadata is a soft dependency for #10; #9 shipped a minimal
  `ComponentTypeID` shim (type identity only). The reflection system reconciles
  it — and the `World` needs its *generic* construction path before Scene
  load/save is buildable.~~ **Done by #18:** `ComponentTypeID` is now a re-export
  of `foundation::reflection::TypeID` (one identity source), and
  `engine/reflection/component_reflection.hpp` supplies the generic construction
  path Scene load/save calls.
- ~~ECS handoff: own the `on_destroy(e)` → `EntityRegistry::destroy(e)` sequencing
  and add an interleaved create/add/destroy/recycle integration test.~~ **Done by
  the `World`:** `tick` commits deferred destructions in that order, and
  `tests/unit/world/world_integration_test.cpp` covers the interleaved cycle.
- Event-drain handoff: the `World::tick` owns the once-per-tick
  `EventBus::drain_deferred()` at the tick's start. The still-open piece is the
  **runtime loop** that calls `World::tick(dt)` each frame — there is no scheduler
  yet, by design.
- Known tech debt ([#25](https://github.com/Evol-Luci/mosaic/issues/25), post-v0.1):
  `type_id<T>()` shares one global counter across all reflected types, so
  `ComponentRegistry::storages_` is sized by the max id of *any* reflected type
  (null gaps for non-components). Accepted, correctness-safe for v0.1; give
  components a dense id space later without reintroducing the #9 identity drift.
- If a decision in RFC 0001 turns out to be wrong under implementation, don't
  silently deviate — open an issue proposing an amendment and link the RFC. (See
  the redo/announce interpretation note under *Where we are* — a resolved
  ambiguity, not a deviation, but worth revisiting at the World/UI layer.)

## After the engine core

The **v0.1 headless engine core is complete**: entities, serialization,
commands/events, the runtime `World`, reflection, Scene load/save, the asset
registry, foundation math, the transform system, and the runtime loop. Everything
composes into a world that can be populated, serialized, reloaded, and ticked
deterministically — but nothing *runs it* outside a test, and nothing is *visible*.

The three on-ramps from here (each its own future spec):
- **App / CLI shell** (`mosaic run`) — a process `main` owning the real-time frame
  pump that drives `RuntimeLoop`. Smallest path to running a world for real.
- **First visible subsystem** — Vulkan rendering or the Studio shell. The milestone
  that makes the engine something you can look at.
- **Remaining foundation** — logging, then jobs.

Reassess this file as each lands.
