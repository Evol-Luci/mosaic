# Engine `World` runtime — design

**Status:** Approved (brainstorming) — pending implementation plan
**Date:** 2026-08-16
**Layer:** Engine (part of the [v0.1 vertical slice](../../project/roadmap.md#the-v01-vertical-slice--the-first-mountain))
**Depends on:** #9 ECS core, #11 command/event system (`foundation/events`)
**Related:** [RFC 0001](../../rfcs/0001-core-data-model.md), [data-model](../../architecture/data-model.md), [next-steps](../../project/next-steps.md)

## Summary

Introduce the runtime **World**: the engine-layer container that owns entities,
components, and systems, runs a per-tick update, and drains the deferred event
channel from #11. This is the first Engine-layer unit after the three Phase 0
foundation primitives (#9 ECS, #10 serialization, #11 commands/events) and the
container the ECS core was built for.

## Motivation

The ECS core (#9) ships an `EntityRegistry` and a `ComponentRegistry`, but nothing
composes them into a runnable whole, drives systems, or owns the destroy
sequencing (`on_destroy → destroy`). The `System` base is declaration-only — it
has no execution entry point — and neither `SparseSet` nor `ComponentRegistry`
exposes iteration, so "systems process components each tick" is not yet
expressible. The World fills exactly that gap and gives every downstream Engine
piece (transform system, asset registry, the outer runtime loop) a container to
attach to.

## Scope

**In scope:** the runtime `World` container, a system execution model, per-tick
event draining, and deferred entity destruction with correct sequencing.

**Explicitly out of scope** (each its own future spec):

- **Scene load/save.** Requires reflection-driven *generic* component
  construction (materializing a component from a serialized type name without the
  C++ type at the call site). `ComponentRegistry` is template-keyed today; that
  generic path does not exist yet. This is the seam the World spec deliberately
  stops at.
- **The reflection system** (type registry, generic component construction).
- **Concrete systems** such as the transform system.
- **The outer runtime loop.** World exposes `tick(dt)`; an editor/CLI/game loop
  calls it. There is no scheduler in the engine yet by design.

## Design decisions

Three decisions were settled during brainstorming:

1. **Scope: World runtime only.** Defer Scene + reflection to their own specs,
   because Scene loading depends on generic reflection-driven component
   instantiation that is currently only a `ComponentTypeID` shim.
2. **System execution: `update(World&, dt)` + single-component `for_each`.** Add
   a virtual `update` entry point and expose single-component iteration. A system
   needing two component types iterates one and `get_component<U>()`s the other.
   Multi-component join *views* are deferred until a system actually needs one.
3. **Ownership: World owns the `EventBus`, not the `CommandStack`.** The World
   must drain deferred events each tick and is the natural owner of the per-world
   announcement channel. The `CommandStack` (undo/redo) is an editor/tooling
   concern a shipped game runtime does not need; it stays outside the World.
   Commands mutate the World through its public API and emit through
   `world.events()`.

## Architecture

### Boundaries

The World depends only on `foundation/` (`EntityID`, `events::EventBus`) and
`engine/ecs/`. No upward dependencies — the tier contract in
[ARCHITECTURE.md](../../architecture/ARCHITECTURE.md) is preserved.

### Files

| File | Change |
|------|--------|
| `engine/world/world.hpp` | New. The `World` class, namespace `mosaic::engine`. |
| `engine/world/world.cpp` | New. `tick` and destroy-commit logic. |
| `engine/ecs/system.hpp` | Add `virtual void update(World&, float dt) {}` (default no-op so declaration-only systems still compile); forward-declare `mosaic::engine::World`. |
| `engine/ecs/sparse_set.hpp` | Add dense iteration: `template<class Fn> void each(Fn&&)` over `(EntityID, T&)`. |
| `engine/ecs/component_registry.hpp` | Add `template<class T, class Fn> void for_each(Fn&&)` and expose `each` on `ComponentStorage<T>`. |
| `engine/CMakeLists.txt` | Add `world/world.cpp` to `mosaic_engine`. |
| `tests/CMakeLists.txt` | Add the new `tests/unit/world/` sources. |

### World API surface

```cpp
namespace mosaic::engine {

class World {
public:
    using EntityID = mosaic::foundation::EntityID;

    // Entities
    EntityID create_entity();               // nameless; a "name" is a future component
    void     destroy_entity(EntityID);      // deferred — committed at end of tick
    bool     is_valid(EntityID) const;
    std::size_t entity_count() const;

    // Components (delegate to ComponentRegistry, entity-validity checked there)
    template<class T> bool add_component(EntityID, T);
    template<class T> T*   get_component(EntityID);
    template<class T> bool has_component(EntityID) const;
    template<class T> bool remove_component(EntityID);
    template<class T, class Fn> void for_each(Fn&&);   // visits (EntityID, T&)

    // Systems — owned, run in registration order
    template<class S, class... A> S& add_system(A&&...);

    // Events — World owns the bus and drains it each tick
    events::EventBus& events();

    // Runtime
    void tick(float dt);

private:
    ecs::EntityRegistry     entities_;
    ecs::ComponentRegistry  components_{entities_};
    events::EventBus        events_;
    std::vector<std::unique_ptr<ecs::System>> systems_;
    std::vector<EntityID>   pending_destroy_;
};

} // namespace mosaic::engine
```

`create_entity()` is nameless in the core — an entity name is just a future
`Name` component, not a World concern.

### Tick semantics

`tick(dt)` performs exactly three steps, in order:

1. **Drain deferred events** — `events_.drain_deferred()` at the tick's start,
   the RFC #11 fixed sync point. Events emitted during *this* tick surface at the
   *next* tick's drain (never mid-tick).
2. **Run systems** — `system->update(*this, dt)` for each system in registration
   order. Single-threaded; scheduling/threading are deferred to a future job
   system.
3. **Commit destructions** — flush `pending_destroy_`: for each id that is still
   valid, call `components_.on_destroy(id)` **then** `entities_.destroy(id)` (the
   handoff-mandated ordering: components first, slot retirement/recycle second).
   Clear the queue.

### Destroy sequencing

`destroy_entity(e)` **enqueues**; the actual destruction is committed at the end
of the tick (step 3). Deferring destruction means a system iterating a component
set via `for_each` never has entities vanish mid-iteration.

Constraint for v0.1: structurally mutating a component set *during* its own
`for_each` (adding/removing that same component type) is unsupported — it may
reallocate the dense array under the iterator. Systems must defer such changes
(`destroy_entity`, or mutate after iterating). This is single-threaded, so the
constraint is a documented usage rule, not a locking concern.

Double-queuing the same entity across a tick is harmless: the validity check in
step 3 skips ids already destroyed (or recycled) so a stale queued handle is a
no-op.

## Testing (TDD, `tests/unit/world/`)

All via Catch2, tag `[world]` (integration cases also tagged `[world][events]`):

- **Entity lifecycle:** create/destroy/validity and correct delegation to the
  registries; `entity_count` tracks live entities.
- **Component access:** `add`/`get`/`has`/`remove` round-trip; operations on
  invalid handles reject.
- **`for_each<T>`** visits exactly the live entities holding `T`, and no others.
- **System execution:** systems run in registration order each tick; `update`
  receives the passed `dt`; a declaration-only system (no `update` override) is a
  no-op.
- **Event draining through World:** a deferred event emitted during a tick is
  delivered at the *next* tick's drain, never within the emitting tick.
- **Destroy sequencing:** `destroy_entity` is deferred; the entity and its
  components remain until `tick` commits, then are gone.
- **Interleaved integration test** (handoff-requested): a multi-entity
  create / add-component / destroy / recycle cycle across several ticks,
  asserting stale handles reject after their slot recycles and that recycled
  slots behave correctly — the cross-cutting cycle #9's per-unit tests do not
  cover.

## Open questions / follow-ups

- **Built-in lifecycle events** (e.g. `EntityDestroyed`): deliberately *not*
  emitted by the core World in v0.1 — producers announce their own changes. Revisit
  when a consumer needs them.
- **Multi-component views/queries:** deferred until a concrete system needs a
  join; the single-component `for_each` is the v0.1 primitive.
- **Scene + reflection:** the next specs after this one; the World's generic
  component construction gap is where they plug in.
