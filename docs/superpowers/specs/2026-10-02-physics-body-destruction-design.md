# Physics — Destroying a Body When Its Entity Dies (design)

**Date:** 2026-10-02
**Status:** approved (design), pending implementation plan
**Branch:** `physics-body-destruction`, cut from `main` at `45537b0`

## Intent

Make destroying an entity destroy its physics body.

`PhysicsBackend` has no counterpart to `create_body`, and
`PhysicsSystem`'s three maps — `bodies_`, `handle_to_entity_`,
`synced_pose_` — are insert-only. So a destroyed entity's Box3D body
keeps stepping forever.

**This is a gameplay bug, not a leak.** The body keeps colliding:
destroy a crate and it still blocks the player, with no mesh to explain
why. Unbounded map growth over a long session is the lesser half of it.

`update()` tolerates the situation only by accident — step 2 and step 4
both `continue` when `get_component` returns null, and step 5a drops
contacts whose handle is unmapped. That accidental tolerance is exactly
why this has stayed invisible since the physics pillar shipped.

There is a second hole in the same place: **`World::clear()` bypasses the
deferred destroy queue entirely and runs no systems**, so no system ever
gets a chance to react to a scene reload.

**Success criteria**

- A destroyed entity's body stops colliding, demonstrated positionally
  rather than by the absence of a crash.
- All three maps shrink; `has_body()` reports false afterwards.
- Every backend is forced to answer "how do I free a body?" rather than
  inheriting a default that silently leaks.

## Decisions

1. **`PhysicsSystem` polls in `update()`; no new ECS machinery.**
   Confirmed with the user. A reap pass at the top of `update()` asks, for
   each entity in `bodies_`, whether its `RigidBody` component still
   exists. Two alternatives were rejected. An `EntityDestroyed` event on
   the bus is more precise in principle but lands next tick anyway through
   deferred delivery, so the latency is identical; it adds an event type
   and a new responsibility to `World`; `clear()` would still need
   separate handling because it runs no systems and drains nothing; a
   synchronous emit from `tick()`'s step 3 would reintroduce the
   call-out-under-a-lock question #211 just closed; and it would not cover
   `RigidBody` removal at all. A per-type teardown hook in
   `ComponentRegistry` is the most precise — the body would die the
   instant the component does — and would be reusable by any future system
   holding external resources, but `on_destroy` today just erases from
   every storage with no hook mechanism, so it means new machinery in
   `engine/ecs` for a bug that does not warrant that blast radius.

2. **The predicate is "`RigidBody` is gone", not "the entity is
   invalid".** One check covers three cases: `destroy_entity` committing
   (`on_destroy` erases every component), `clear()` (the same teardown),
   and someone removing `RigidBody` from a living entity — which should
   also stop simulation. `is_valid()` would miss the third. `EntityID` is
   generational (`generation << 32 | index`), so a recycled slot yields a
   different id and cannot alias a stale key.

3. **`destroy_body` is a pure virtual, not a defaulted no-op.** Confirmed
   with the user. It matches `PhysicsBackend`'s existing shape — all seven
   current methods are pure — and a destroy counterpart to `create_body`
   is precisely the method where a wrong default means a silent,
   unbounded leak of a body that keeps colliding. `RenderDevice`'s
   default-no-op hooks are the opposite case: there, a backend that does
   not implement a hook correctly does nothing visible. Cost is three
   edits: the real backend and the two test fakes in
   `physics_system_test.cpp` and `physics_concurrency_test.cpp`. That
   friction is the point.

## The reap

At the top of `update()`, inside the existing lock, before step 1:

```cpp
for (auto it = bodies_.begin(); it != bodies_.end();)
{
    if (world.get_component<RigidBody>(it->first) != nullptr) { ++it; continue; }
    backend_->destroy_body(it->second);
    handle_to_entity_.erase(it->second);
    synced_pose_.erase(it->first);
    it = bodies_.erase(it);
}
```

It runs first so nothing downstream iterates stale maps. It also closes a
loose end for free: `last_contacts_` can still hold a reaped body's handle
from the previous `step()`, and step 5a already drops contacts whose
handle is unmapped — so erasing `handle_to_entity_` makes that correct by
construction rather than by luck.

**One extra tick is the floor, not a shortcoming of this approach.**
`destroy_entity` commits in `tick()`'s step 3, *after* every system has
run, so the earliest any mechanism can observe the destruction is the next
tick's `update()`. The body is therefore stepped once more after its
entity is gone. The event-based alternative has exactly the same latency.

## The backend, and what makes this more than two lines

`PhysicsBackend` gains:

```cpp
// Destroy a body previously returned by create_body. The handle is invalid
// afterwards. A no-op for an unknown or already-destroyed handle, matching how
// the other handle-taking methods degrade. Required rather than defaulted: a
// no-op default would silently leak a body that keeps simulating and colliding.
virtual void destroy_body(BodyHandle body) = 0;
```

The complication is in `modules/physics_box3d/box3d_backend.cpp`.
`bodies_` is a `std::vector<b3BodyId>` with `handle == index + 1`, and it
is insert-only. **Erasing a slot would shift every later handle and
invalidate it**, so a destroyed slot must be *tombstoned* with
`b3_nullBodyId`.

That has a consequence worth stating plainly, because it is the bulk of
the diff: the three existing handle-taking accessors —
`set_body_transform`, `apply_impulse`, `body_pose` — today guard only the
range (`body == 0 || body > bodies_.size()`) and must now also reject a
tombstone. Rather than repeat the check four times, all four route through
one private helper:

```cpp
// nullptr for an out-of-range or already-destroyed handle.
const b3BodyId* live_body(ep::BodyHandle h) const;
```

`b3Body_IsValid` is the guard. Its source was read rather than assumed: it
returns false when `index1 < 1`, so it is safe on `b3_nullBodyId` and
needs no special-casing. `b3DestroyBody(b3BodyId)` likewise exists and was
confirmed present before this design relied on it.

This is a targeted improvement to the file the work lives in, not
unrelated refactoring — but it is why the change is not two lines.

**The handle↔id mapping is safe across destruction.** The forward
direction is `bodies_[handle - 1]`. The reverse, used by `raycast`, reads
the handle out of Box3D's per-body *user data*, set at creation — it is
not derived from the `b3BodyId`'s index. So a recycled `b3BodyId` gets
fresh user data and cannot be mistaken for the destroyed body that
previously held that index.

## Testing

**The load-bearing test is the gameplay one, against the real Box3D
backend.** Ground, plus two boxes stacked. Let them settle, destroy the
lower entity, keep ticking. The upper box must fall and come to rest on
the ground. With the bug it stays propped on a ghost. That is a clean
positional assertion, not an absence of a crash — and this project has
twice now paid for treating "it did not crash" as evidence.

Then:

- `has_body()` is false after the reap, and all three maps have shrunk.
- Removing `RigidBody` from a *living* entity also reaps its body — the
  case `is_valid()` would have missed.
- `World::clear()` followed by a tick reaps every body without crashing,
  covering the second hole.
- A fake backend records `destroy_body` calls, so the count can be
  asserted as exactly one per destroyed entity rather than "at least
  one" — a double-destroy is as much a bug as none.
- The existing `[physics]` suite passes untouched, including #209's
  overlap detector and #211's contact-delivery guards.

**The fix must be shown to be load-bearing, and the probe is pointed.**
Delete the `backend_->destroy_body(...)` call while keeping the map
erasure. The `has_body`, map-shrink, and `clear()` tests all still pass —
only the stacked-boxes test fails. That asymmetry is the argument for
writing the gameplay test at all, and it should be recorded as a measured
result rather than asserted.

## Risks

**`bodies_` never shrinks.** Tombstones accumulate for the lifetime of
the backend, so a session that spawns and destroys many bodies grows the
vector without bound even though the Box3D bodies themselves are freed.
This is strictly better than today (where the *bodies* leak, not just
8-byte slots) but it is a leak of a different size, not its absence.
Handle reuse is recorded as a non-goal rather than left implied.

**The reap depends on `RigidBody` being erased, not on the entity slot.**
If a future `ComponentRegistry` change stopped erasing components on
destroy — or erased them lazily — the predicate would silently stop
firing and the bug would return with no test failing, because every test
here destroys entities through the normal path. The coupling is real and
worth knowing; pinning it would mean asserting on `on_destroy`'s
behaviour from the physics tests, which trades a clear test for a
cross-layer one.

**A body is stepped once after its entity is gone.** Stated under The
reap above. Any observer reading contacts in that window sees a contact
for an entity that no longer exists; step 5a drops it because the handle
is still mapped at that point. Harmless today, and it is the floor for
every mechanism considered.

## Non-goals

To be recorded in `docs/project/deferred-work-backlog.md`:

- **Handle reuse / a free list for `bodies_`.** Tombstones mean the
  vector only grows. A free list would reuse slots but reintroduces the
  ABA question the generational `EntityID` solves on the ECS side, and
  there is no measured pressure.
- **Reaping when a collider component is removed but `RigidBody`
  stays.** The body persists with the shape it was created with. Arguably
  it should be recreated with the new shape, which is a shape-change
  feature rather than a destruction one.
- **A `PhysicsSystem` destructor that frees every body.** Unnecessary:
  the backend owns the Box3D world, and destroying the world frees
  everything in it.
- **Same-tick destruction.** Impossible given that `destroy_entity`
  commits after systems run; see The reap.
- **Changing a body's shape or type in place.** Today the only way is
  destroy-and-recreate, which this slice makes possible for the first
  time but does not expose.
