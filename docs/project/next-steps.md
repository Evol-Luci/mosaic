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

**Serialization ([#10](https://github.com/Evol-Luci/mosaic/issues/10)) is built and
merged.** Reflection-driven, lossless cross-format projection:

- `foundation/serialization/` — `Value` DOM, `AssetID`/`Handle<T>`/`ReferenceResolver`,
  reflection bridge (`to_value`/`from_value`), JSON backend (nlohmann), TOML backend
  (toml++), and error handling via `SerializationError`. `from_json`/`from_toml`
  guard against adversarial deeply-nested input with a configurable
  nesting-depth cap (default 128) that throws `SerializationError` rather than
  overflowing the stack — the non-blocking hardening follow-up from the #10 review. ✓
- `tests/unit/serialization/` — unit tests for every tag: `[value]`, `[reference]`,
  `[reflect]`, `[refenc]`, `[json]`, `[toml]`, `[crossfmt]` — 66 tests, all green.

The next code is the **command / event system**.

## What to build next

One implementation issue remains in **Phase 0 — Foundation** as part of the
**[v0.1 vertical slice](roadmap.md#the-v01-vertical-slice--the-first-mountain)**.

### 1. Command / event system — [#11](https://github.com/Evol-Luci/mosaic/issues/11)  ← start here

`foundation/commands/` + `foundation/events/`. Unblocked — #9 provides the
entities it mutates and #10 provides the serialization it round-trips.

- Commands: `id, parameters, required_permissions, execute(), undo(), serialize()`;
  the undoable transaction log.
- Events: synchronous in-tick by default; **double-buffered per-frame drain** for
  deferred subscribers (FIFO, one tick after emit). Deferred events emitted by a
  command enqueue on *execute*, not on undo/redo.

## Suggested sequence

```
#9 ECS core ✓ ──┬── #10 Serialization ✓
                └── #11 Command/event system  ← start here
```

#10 is done. #11 is the next buildable unit — it depends only on #9, which is
already in place.

## Ground rules for the worker

- Honor the tier/dependency contract in
  [ARCHITECTURE.md](../architecture/ARCHITECTURE.md): `foundation/` knows nothing
  above it; `engine/` depends only on `foundation/`.
- Each issue lists acceptance criteria and expects unit tests under `tests/unit/`.
- Reflection metadata is a soft dependency for #10; #9 shipped a minimal
  `ComponentTypeID` shim (type identity only). Reconcile it when the full
  reflection system lands. Flag it if it becomes a blocker.
- ECS handoff for #10 / the future World layer: own the `on_destroy(e)` →
  `EntityRegistry::destroy(e)` sequencing, and add an interleaved multi-entity
  create/add/destroy/recycle integration test — #9's per-unit tests don't cover
  that cross-cutting cycle.
- If a decision in RFC 0001 turns out to be wrong under implementation, don't
  silently deviate — open an issue proposing an amendment and link the RFC.

## After these three

With entities, serialization, and commands/events in place, the next foundation
pieces (asset registry, World/Scene runtime, transform system, the runtime loop)
become buildable — the on-ramp to **Phase 1 — First World**. Reassess and update
this file when #10–#11 close.
