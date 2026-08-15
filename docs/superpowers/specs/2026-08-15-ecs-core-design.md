# ECS Core — Design Spec (#9)

**Date:** 2026-08-15
**Issue:** [#9 — Implement ECS core: EntityID + generational entity storage](https://github.com/Evol-Luci/mosaic/issues/9)
**Implements:** [RFC 0001 — Core Data Model](../../rfcs/0001-core-data-model.md) (Accepted), §Identity types, §Component contract, §System contract.
**Status:** Approved — ready for implementation planning.

## Purpose

Build the entity/component core of Mosaic and, because this is the first code in
the repository, stand up the C++ toolchain it sits on. Everything else in the
foundation (serialization #10, commands/events #11) depends on entities and
components existing first.

## Toolchain bootstrap

This issue lands the build system, not just ECS code.

- **Build:** CMake + `CMakePresets.json` (already committed to in
  [repo-layout.md](../../project/repo-layout.md)). Debug and Release presets.
- **Language:** C++20.
- **Tests:** Catch2, pulled via CMake `FetchContent` — no external package manager,
  zero-install for contributors. `ctest` wiring; a first green `ctest` run is part
  of "done."
- **Targets:** `mosaic_foundation` and `mosaic_engine`, with include boundaries set
  so `engine` sees `foundation` but not vice versa. (CI enforcement of the full
  dependency-direction contract is out of scope here — see Non-goals.)

## Scope

In scope:

- `EntityID` value type.
- `EntityRegistry` — slot table with free-list recycling and generational validity.
- Sparse-set component storage (`SparseSet`, `ComponentStorage<T>`, type-erased base).
- A minimal per-type identity shim (`ComponentTypeID`).
- A declaration-only `System` contract (read/write component sets).
- Unit tests covering every acceptance criterion.

## Non-goals (YAGNI / later issues)

- **Field-level reflection.** #10 (serialization) needs it; #9 needs only per-type
  identity. A minimal `ComponentTypeID` shim covers #9; the real reflection system
  replaces/extends it later.
- **Scheduling / threading.** The job system is a later concern. Systems *declare*
  access now; nothing executes them.
- **Archetypes / queries.** Explicitly out of scope; sparse-set is the chosen
  representation.
- **Serialization.** Issue #10.
- **CI dependency-direction enforcement.** Target-level include boundaries are set;
  CI wiring of the full "forbidden edges" contract is a separate concern.

## Layer split

Honors the enforced `foundation → engine` direction from
[repo-layout.md](../../project/repo-layout.md).

| Unit | Location | Depends on |
|---|---|---|
| `EntityID` | `foundation/core/entity_id.hpp` | nothing |
| `SparseSet`, `ComponentStorage<T>`, `IComponentStorage` | `engine/ecs/` | `foundation/core` |
| `EntityRegistry` | `engine/ecs/` | `foundation/core` |
| `System` contract, `ComponentTypeID` | `engine/ecs/` | `foundation/core` |
| Unit tests | `tests/unit/ecs/` | both |

`EntityID` lives in `foundation/` because it is a dependency-free generational
handle primitive that #10 will reference without pulling in the engine.

## Component designs

### `EntityID` (foundation/core)

- Strong type wrapping `uint64_t`; layout **`(generation << 32) | index`** — index
  in the low 32 bits, generation in the high 32, per RFC §Identity types.
- **Generations start at 1.** `EntityID(0)` (index 0, generation 0) is therefore the
  reserved null/invalid handle and can never collide with a live entity.
- Interface: `index()`, `generation()`, `explicit operator bool`, `operator==`,
  `std::hash` specialization. No behavior beyond identity.

### `EntityRegistry` (engine/ecs)

State: `std::vector<uint32_t> generations_` (one entry per slot) and
`std::vector<uint32_t> free_list_`.

- `create() -> EntityID` — pop a free slot or grow the table; return
  `EntityID{generation, index}`.
- `destroy(EntityID) -> bool` — validate; bump the slot generation. If the bump
  would exceed `UINT32_MAX`, **retire** the slot (do not return it to the free-list)
  so a generational check can never alias; otherwise push the slot to the free-list.
- `is_valid(EntityID) -> bool` — `id && generations_[id.index()] == id.generation()`.
  Stale handles reject here and on every component operation.
- **Test seam:** a narrow internal hook (e.g. `force_generation(index, value)` on a
  testing surface) lets the forced-overflow harness drive a slot onto the
  retirement path without four billion real allocations.

### Component storage — sparse set (engine/ecs)

- `SparseSet`: `sparse` (slot index → dense position) + `dense` (packed `EntityID`s)
  + a parallel `T` array. O(1) `add` / `get` / `remove` (swap-and-pop); dense
  iteration.
- `ComponentStorage<T>` wraps a typed `SparseSet`. A type-erased `IComponentStorage`
  base lets `EntityRegistry`/the component manager hold heterogeneous storages and
  erase all of an entity's components on `destroy`.
- Keyed by **`ComponentTypeID`** — the minimal reflection shim: a compile-time
  monotonic id per component type (`template<class T> ComponentTypeID id()` via a
  counter). Type identity only, **not** field reflection.
- API: `add<T>(EntityID, T)`, `get<T>(EntityID) -> T*` (null if absent or stale),
  `has<T>(EntityID) -> bool`, `remove<T>(EntityID) -> bool`. Every op validates the
  handle against the registry first.

### System contract (engine/ecs)

- A `System` base declaring `reads()` / `writes()` as sets of `ComponentTypeID`,
  built through a typed helper (e.g. `access.reads<Position>().writes<Velocity>()`).
- **Declaration only** — no scheduler, no execution wiring. Systems hold no
  per-entity state (RFC §System contract).

## Testing (tests/unit/ecs, Catch2)

One-to-one with the issue's acceptance criteria:

- Create / destroy / recycle entities; recycled slot has a bumped generation.
- A stale handle (old generation) is rejected by `is_valid` and by component access.
- **Forced generation-exhaustion retires the slot** — driven via the test seam;
  the retired slot is never handed back out.
- Add / get / remove components by `EntityID`; `get` on a removed or stale handle
  returns null.
- A `System` reports exactly the read/write `ComponentTypeID` sets it declared.

## Acceptance criteria (from #9)

- [ ] Create/destroy/recycle entities; stale handles reject cleanly.
- [ ] Generation-exhaustion path retires the slot (unit-tested with a forced-overflow harness).
- [ ] Add/get/remove components by `EntityID`.
- [ ] Systems declare read/write component sets.
- [ ] Unit tests in `tests/unit/`.
- [ ] Toolchain: CMake + CMakePresets + Catch2 via FetchContent; `ctest` green.

## Risks / open items

- **Reflection shim drift.** The `ComponentTypeID` counter is a placeholder for
  reflection-derived `TypeID`s. When the reflection system lands, this must be
  reconciled so ids stay stable. Flagged, not solved here.
- If any RFC 0001 decision proves wrong under implementation, do not silently
  deviate — open an amendment issue and link the RFC (per next-steps.md ground
  rules).
