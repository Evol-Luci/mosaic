# Next Steps — Build Handoff

This is the live "you are here" for a worker picking up Mosaic. It points at the
next buildable work and the decisions that constrain it. When the work below is
done, update this file to point at what comes next.

**Last updated:** 2026-08-16 (reflection system #18 built)

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

## What to build next

The runtime `World` and the reflection system are in place. The remaining
**Engine** / early **Phase 1** units of the
**[v0.1 vertical slice](roadmap.md#the-v01-vertical-slice--the-first-mountain)**
are now the frontier (acceptance criteria on each issue):

1. **Scene load/save** — [#19](https://github.com/Evol-Luci/mosaic/issues/19).
   Serialize/deserialize an entity+component graph via #10, loading into a `World`.
   Now unblocked: it calls `ComponentReflection::construct` (#18) to materialize
   each component from its serialized type name.
2. **Asset registry** — [#20](https://github.com/Evol-Luci/mosaic/issues/20).
   Runtime `AssetID` → resource resolution + caching, wired into the #10
   `ReferenceResolver` hook. Importer/cooker are future work.
3. **Transform system** — [#21](https://github.com/Evol-Luci/mosaic/issues/21).
   The first concrete `System` subclass to exercise `World::tick`. Buildable in
   parallel with the asset registry; flags that `foundation/` has no math types yet.
4. **Runtime loop** — [#22](https://github.com/Evol-Luci/mosaic/issues/22). The
   outer fixed-timestep driver that calls `World::tick(dt)` each frame.

Suggested first target: **Scene load/save** (#19) — the reflection system that
gated it is now built, and Scene is the next piece of the vertical slice that the
inspector and editor both build on.

## Suggested sequence

```
#9 ECS core ✓ ──┬── #10 Serialization ✓
                └── #11 Command/event system ✓
                        │
                        └── World runtime ✓ ──┬── #18 reflection ✓ ── #19 Scene load/save
                                              ├── #20 asset registry
                                              ├── #21 transform system
                                              └── #22 runtime loop
```

#9, #10, #11, the World runtime, and #18 reflection are done. #19–#22 are filed and
unblocked; #19 now depends only on already-built work (#18), the rest depend only on
the World runtime.

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
- If a decision in RFC 0001 turns out to be wrong under implementation, don't
  silently deviate — open an issue proposing an amendment and link the RFC. (See
  the redo/announce interpretation note under *Where we are* — a resolved
  ambiguity, not a deviation, but worth revisiting at the World/UI layer.)

## After the reflection system

With entities, serialization, commands/events, the runtime `World`, and the
reflection system in place, the on-ramp to **Phase 1 — First World** is Scene
load/save (#19, now unblocked), plus the asset registry (#20), transform system
(#21), and runtime loop (#22). Reassess this file as each lands.
