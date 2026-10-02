# Physics — Emit Contact Events Outside the Backend Lock (design)

**Date:** 2026-10-01
**Status:** approved (design), pending implementation plan
**Branch:** `physics-contact-emit-unlocked`, cut from `main` at `609493a`

## Intent

Stop `PhysicsSystem` calling out to user code while holding
`backend_mutex_`.

#209 closed the last place a gameplay script could race the physics
simulation, by serializing `update()`, `raycast()`, `apply_impulse()` and
`has_body()` behind one mutex. Its own review then found the cost: step 5
of `update()` emits `ContactEvent` on the **synchronous** event bus, and
`EventBus::emit` dispatches sync handlers on the emitting stack. So every
contact handler in the program runs *inside* the lock.

Two consequences, one latent and one already live.

**Latent — self-deadlock.** A handler that calls `raycast`, `has_body` or
`apply_impulse` deadlocks on a plain `std::mutex`. Nothing does today.
The first gameplay handler that reacts to a collision by querying physics
— "when the ball hits the wall, raycast to pick a bounce target" — will,
and it will present as a hang with no diagnostic.

**Already live — lock ordering.** Every lock a sync handler takes is now
ordered *under* `backend_mutex_`. That already includes the global logger
mutex, because the demo's contact handler calls `log::info` once per
contact (`app/run/demo_scene.hpp`). Nothing inverts that ordering today,
so nothing hangs today. But it is an invariant that nothing states, tests,
or enforces, and the set of locks reachable from a handler grows every
time someone writes a handler.

**Success criteria**

- A synchronous `ContactEvent` handler can call back into
  `PhysicsSystem` and the tick completes.
- The mutual-exclusion property #209 established is unchanged: no two
  backend calls in flight at once.
- Contacts are still delivered, exactly once, in the same order.
- The test fails **in seconds** without the fix, not at a ctest timeout.

## A correction to the finding as originally recorded

#209's backlog entry, and this author's summary of it, said the fix
"changes when subscribers observe a contact relative to the pose
write-back". **That is wrong, and it is worth stating rather than quietly
not doing.**

Reading `update()`: the pose write-back is step 4 and contact emission is
step 5, already the last statement in the function. Poses are written
before any handler runs, today and after this change. That ordering does
not move, which makes this slice smaller and safer than advertised — and
removes the behaviour decision the entry claimed it needed.

What the change actually alters is listed under **What this opens** below.

## Decisions

1. **An inner braced block, not a manual unlock.** Steps 1-4 and the
   contact mapping go inside a block holding the `lock_guard`; the emit
   loop runs after it closes. Confirmed with the user. Two alternatives
   were rejected. A `std::unique_lock` with an explicit `unlock()` before
   the emit is the smallest diff and needs no reindentation, but the
   locked region then ends at a statement in the middle of a function with
   nothing marking it, so a later edit that adds a call-out above that
   line silently reintroduces this exact bug. Extracting a private
   `step_simulation(...)` helper gives the strongest contract — the lock
   lives in a function that provably contains no `emit` — but it adds a
   function and a signature for one caller and splits `update()`'s five
   numbered steps across two places. The braced block is self-enforcing
   through lexical scope: you cannot write a call-out inside it without
   seeing the braces, and you cannot accidentally be outside it.

   This follows #209's own reasoning rather than contradicting it. #209
   chose to hold the lock across the *whole* of `update()` on the grounds
   that "four narrow critical sections inside one function is the shape
   that grows a bug later". The fix here is not four sections; it is one
   section with a lexically delimited end.

2. **A watchdog thread in the test, not a ctest `TIMEOUT`.** Confirmed
   with the user. The pre-fix failure mode is a hang, and this repo sets
   no ctest timeout, so the default 1500s applies — a deadlocking test
   would stall CI for 25 minutes before failing, and could not be probed
   by running the test binary directly, which is how every mutation in
   this arc has been verified. A watchdog arms before `world.tick()`,
   sets an atomic flag if the tick has not returned within **3000 ms**, and
   the test asserts on that flag. The failure then reads as "the subscriber
   callback deadlocked" in seconds. A ctest `TIMEOUT` property was also
   considered as useful general infrastructure; it is recorded as a
   non-goal rather than folded in, because the watchdog makes it
   unnecessary *here* and a global knob wants its own justification.

## Design

In `engine/physics/physics_system.hpp`, `update()` becomes:

```
std::vector<ContactEvent> contacts;
{
    const std::lock_guard<std::mutex> lock(backend_mutex_);
    // 1. lazily create bodies
    // 2. push external Transform edits in
    // 3. step
    // 4. write poses back
    // 5a. map BodyContact -> ContactEvent into `contacts` (no emit)
}
// 5b. Outside the lock: a handler may call back into PhysicsSystem.
for (const ContactEvent& e : contacts)
{
    world.events().emit<ContactEvent>(e);
}
```

- `contacts` is a plain local. Contact counts are small (zero on most
  ticks), and a reused member buffer would add state to save an
  allocation nobody has measured.
- The handle→entity mapping stays inside the lock, because it reads
  `handle_to_entity_`. Only the `emit` moves out. This is the whole point:
  the lock still covers every access to `backend_` and to the three maps.
- Contacts that map to an unknown handle are still dropped during
  collection, exactly as now.
- One file changes. No interface changes, no `PhysicsBackend` change, no
  Box3D change.

## What this opens

Stated here deliberately, so a future reader knows it was a choice.

**A worker-thread `raycast()` can now interleave between `step()` and the
handlers**, where previously it waited until every handler had finished.
This is benign on inspection — a raycast does not mutate the backend, and
poses are already written by step 4, so a worker raycast between the step
and the handlers sees the same world a handler sees. But it is a real
change in what the worker can observe, and the mutual-exclusion test from
#209 remains the guard that the two never overlap *inside* the backend.

**A handler can now reach `PhysicsSystem`'s read methods**, which is the
goal. It cannot reach a mutator: `update()` is the only method that writes
`bodies_`/`handle_to_entity_`/`synced_pose_`, and `World::tick` runs
systems sequentially, so no handler can re-enter `update()` in a sane
program.

## Testing

A new `tests/unit/physics/physics_contact_reentry_test.cpp`.

1. **A sync `ContactEvent` handler calling `raycast()` and `has_body()`
   completes the tick.** The load-bearing case. A watchdog thread arms
   before `world.tick()` and sets an atomic `timed_out` if the tick has
   not returned within **3000 ms**; the test asserts the handler ran, its
   raycast returned, and `timed_out` is false.
2. **A sync handler calling `apply_impulse()` completes the tick.** The
   write path, same shape — and the one most likely to be written by a
   real gameplay author.
3. **Contacts still arrive, exactly once.** The no-regression guard
   specific to collect-then-emit: assert the handler fired exactly as
   many times as there were mappable contacts, so a refactor that drops
   or duplicates events fails here rather than in a game.

Plus: the existing `[physics]` contact tests pass untouched, and #209's
overlap detector still passes — the latter is what proves this change did
not loosen the mutual exclusion it was built to guarantee.

**The fix must be shown to be load-bearing.** Move the `emit` back inside
the braced block and require cases 1 and 2 to fail on the watchdog, in
seconds. A test for a deadlock that has never been watched to deadlock is
indistinguishable from one that cannot.

## Risks

**The watchdog introduces a timing constant into the suite.** 3000 ms is
arbitrary: too short and a loaded CI runner produces a false failure, too
long and the probe is slow. It is bounded by being a *ceiling* on a tick
that normally takes microseconds, so the margin is four orders of
magnitude — but it is a wall-clock assumption, and the first flake it
produces should raise the constant, not delete the test.

**A watchdog that observes a real deadlock cannot clean up after itself.**
If the tick genuinely hangs, the test thread stays blocked forever; the
watchdog can record the failure but the process will not exit normally.
That is acceptable for a mutation probe run deliberately, and it is the
pre-fix state rather than something this change introduces — but it means
the probe run ends in a killed process, which should be expected rather
than treated as a surprise.

**Collect-then-emit copies each contact once more than before.** A
`std::vector<ContactEvent>` per tick, of a struct holding two `Vec3`s, two
floats, two ids and an enum. Irrelevant at realistic contact counts, and
noted only so it is not mistaken for an oversight.

## Non-goals

To be recorded in `docs/project/deferred-work-backlog.md`:

- **A ctest `TIMEOUT` property for the suite.** Genuinely useful — this
  repo has none, so any future hang costs 25 minutes of CI — but the
  watchdog makes it unnecessary for this slice, and a global knob wants
  its own justification and its own value.
- **Auditing other engine systems for call-outs made under a lock.**
  `PhysicsSystem` is the only system holding a mutex today, so there is
  nothing else to audit yet; the general rule ("never call user code
  while holding an internal lock") has no enforcement mechanism and
  inventing one now would be speculative.
- **A general thread-safety contract for `ScriptHost` hooks.** Already
  recorded by #209; unchanged by this slice.
- **The missing body-destruction path.** Recorded 2026-10-01 while
  scoping this work: `create_body` has no counterpart, so a destroyed
  entity leaks its Box3D body and leaves stale map entries. Adjacent —
  releasing the lock makes "a handler destroys a colliding entity" a
  reachable shape for the first time — but it needs a new
  `PhysicsBackend` pure virtual and a decision about how `PhysicsSystem`
  learns an entity died, so it is its own slice.
