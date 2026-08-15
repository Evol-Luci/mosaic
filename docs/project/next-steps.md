# Next Steps — Build Handoff

This is the live "you are here" for a worker picking up Mosaic. It points at the
next buildable work and the decisions that constrain it. When the work below is
done, update this file to point at what comes next.

**Last updated:** 2026-08-15

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

The next code is **serialization against the now-real component model**.

## What to build next

Two implementation issues remain, both part of **Phase 0 — Foundation** and the
**[v0.1 vertical slice](roadmap.md#the-v01-vertical-slice--the-first-mountain)**.

### 1. Serialization — [#10](https://github.com/Evol-Luci/mosaic/issues/10)  ← start here

`foundation/serialization/`. Unblocked now that #9 provides real components to
round-trip. Normative stance: [RFC 0001](../rfcs/0001-core-data-model.md)
*Serialization stance*.

- Reflection-driven, serialize **by field** — no hand-written serializers.
- **TOML** (toml++) for config/asset metadata; **JSON** for nested scenes.
- Both are lossless projections of the same schema; cross-format round-trip is an
  identity on the model.
- `References`/`Handle<T>` serialize as `AssetID`/`EntityID` and re-resolve at load.

### 2. Command / event system — [#11](https://github.com/Evol-Luci/mosaic/issues/11)

`foundation/commands/` + `foundation/events/`. Can proceed in parallel with #10 —
#9 already provides the entities it mutates. Normative semantics live in
[command-event-system](../design/command-event-system.md); `required_permissions`
ties into [automation-and-agents](../design/automation-and-agents.md).

- Commands: `id, parameters, required_permissions, execute(), undo(), serialize()`;
  the undoable transaction log.
- Events: synchronous in-tick by default; **double-buffered per-frame drain** for
  deferred subscribers (FIFO, one tick after emit). Deferred events emitted by a
  command enqueue on *execute*, not on undo/redo.

## Suggested sequence

```
#9 ECS core ✓ ──┬── #10 Serialization  ← start here
                └── #11 Command/event system
```

#10 and #11 can go in either order (or in parallel). #10 has the harder dependency
on the component representation, so if working solo, do #10 next while the ECS
shape is fresh, then #11.

## Ground rules for the worker

- Honor the tier/dependency contract in
  [ARCHITECTURE.md](../architecture/ARCHITECTURE.md): `foundation/` knows nothing
  above it; `engine/` depends only on `foundation/`.
- Each issue lists acceptance criteria and expects unit tests under `tests/unit/`.
- Reflection metadata is a soft dependency for #10; #9 shipped a minimal
  `ComponentTypeID` shim (type identity only). Reconcile it when the full
  reflection system ([design/reflection.md](../design/reflection.md)) lands.
  Flag it if it becomes a blocker.
- ECS handoff for #10 / the future World layer: own the `on_destroy(e)` →
  `EntityRegistry::destroy(e)` sequencing, and add an interleaved multi-entity
  create/add/destroy/recycle integration test — #9's per-unit tests don't cover
  that cross-cutting cycle.
- If a decision in RFC 0001 turns out to be wrong under implementation, don't
  silently deviate — open an issue proposing an amendment and link the RFC.

## After these two

With entities (#9, done), serialization, and commands/events in place, the whole
foundation trio is complete and the next pieces (asset registry, World/Scene
runtime, transform system, the runtime loop) become buildable — the on-ramp to
**Phase 1 — First World**. Reassess and update this file when #10–#11 close.
