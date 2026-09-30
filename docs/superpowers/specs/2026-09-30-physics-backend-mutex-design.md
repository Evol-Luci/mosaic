# Physics — Backend Mutex for Off-Thread Reads (design)

**Date:** 2026-09-30
**Status:** approved (design), pending implementation plan
**Branch:** `physics-backend-mutex`, cut from `main` at `22d7add`

## Intent

Close the last place a gameplay script can race the physics simulation.

The previous slice buffered script *writes* so the worker thread never
touches the live Box3D world. Reads were left alone and explicitly
deferred: a script's `raycast()` still calls
`b3World_CastRayClosest(world_, …)` on the very `b3WorldId` the main
thread is stepping. A read racing a step can return torn or
mid-integration results — a ray that reports a hit at a position no body
ever occupied.

That is the whole remaining gap. Writes are serialized; reads are not.

**Success criteria**

- No script-visible physics call touches backend state concurrently with
  another.
- `raycast()` keeps working on both paths — no behaviour regression for
  threaded scripts, which is what makes this different from simply
  refusing the call.
- The mutual-exclusion property is tested directly, not inferred from an
  absence of crashes.

## Decisions

1. **A mutex, held for the duration of the physics update.** Confirmed
   with the user. A worker raycast blocks until the main thread's physics
   update finishes — bounded, and a fraction of a ~16 ms tick. Two
   alternatives were rejected: *snapshot-served geometry* preserves full
   thread independence but requires reimplementing ray-vs-shape outside
   Box3D and keeping two intersection implementations in agreement
   forever, whose failure mode is silent divergence from the real world;
   and a *try-lock returning a miss* never blocks and never races, but
   makes `raycast` non-deterministic in a new way — returning a miss for a
   ray that would have hit is arguably worse than either waiting or
   racing.

2. **Every method that touches shared mutable state takes the lock**, not
   just the two that first come to mind. This is the correction that
   matters, and it is why "guard `step()` and `raycast()`" would be a fix
   that looks right and is not.

## What the lock actually protects

Two distinct things, which is easy to miss:

**The backend.** Seven call sites in `engine/physics/physics_system.hpp`:
`create_body` (:83), `set_body_transform` (:101), `step` (:107),
`body_pose` (:124), `contact_events` (:131) — all inside `update()` — plus
`raycast` (:149) and `apply_impulse` (:189).

**The handle maps.** `bodies_` and `handle_to_entity_` are *written* by
`update()` (a body created for a newly-seen entity) and *read* by
`raycast()` (handle → entity), `apply_impulse()` (entity → handle) and
`has_body()`. So `has_body()` needs the lock even though it never touches
`backend_` at all: concurrent read of an `unordered_map` being written is
a data race in its own right.

**Why `apply_impulse()` is not already safe.** It is called on the main
thread — but from `ScriptSystem::update_threaded`'s replay, which runs
*outside* `PhysicsSystem::update()`. The worker can be executing a job,
and therefore a `raycast`, at that exact moment. Main-thread-only is not
the same as serialized.

## Design

- `mutable std::mutex backend_mutex_;` on `PhysicsSystem`.
- `std::lock_guard` at the top of `update()`, `raycast()`,
  `apply_impulse()` and `has_body()`.
- `update()` holds it across its whole body. That is longer than strictly
  necessary — it also covers ECS traversal — but it is the version that is
  obviously correct, and the alternative (four narrow critical sections
  inside one function) is the shape that grows a bug later.
- A plain `std::mutex`, not recursive: `update()` never calls the other
  three, and every main-thread caller (`studio::pick_entity`, the
  click-nudge system, the impulse replay) runs outside `update()`. A
  recursive mutex here would be papering over a nesting problem that does
  not exist.
- `raycast()` is `const`, hence `mutable` on the member — matching the
  existing signature rather than changing it.

## Testing

The weak version of this test asserts nothing useful: run a threaded
script that raycasts, observe no crash, declare victory. Absence of a
symptom is not evidence of mutual exclusion — and this project spent nine
days with a real race hidden behind exactly that reasoning.

**So the test detects overlap directly.** A fake backend where every
method:

1. increments an atomic depth counter on entry,
2. records `overlap_seen = true` if the counter ever exceeds 1,
3. sleeps briefly to widen the window,
4. decrements on exit.

Driven by a `ScriptSystem` with `threaded = true` whose script calls
`raycast()` every tick, over a few hundred ticks, then asserting
`overlap_seen` is false.

That tests the actual property — *two backend calls were never in flight
at once* — rather than the absence of a consequence.

**The test must be shown to fail without the mutex.** Remove the lock,
watch `overlap_seen` trip, restore it. A concurrency test that has never
failed is indistinguishable from one that cannot.

Plus:

- `raycast()` still returns correct hits on the threaded path — the
  no-regression guard, since the rejected alternative was to make it miss.
- The single-threaded path is unchanged: existing raycast tests pass
  untouched.
- **`has_body()`'s lock is the one guard on this slice with no failing mutation
  behind it**, and this is stated rather than glossed. The overlap detector
  cannot see it: `has_body()` makes no backend call, so it never enters the
  detector's `Guard` and never increments the depth counter. Removing its
  `lock_guard` leaves the detector green. Catching the `bodies_` map race
  directly would need ThreadSanitizer, which this slice does not add.
  The lock stays regardless: it is free, it is correct by construction, and
  it is the right shape for the next hook. But in a slice whose whole thesis
  is that an unfalsifiable concurrency test is worthless, an unfalsified
  guard is worth naming as such.
- **A note on how live the `bodies_` hazard actually is.** `ScriptWorker`
  rebinds `apply_impulse` with its own recording version, so
  `make_physics_apply_impulse` -- the only non-test caller of `has_body()` --
  never runs on the worker thread. The worker reaches only `raycast()`, which
  reads `handle_to_entity_`. So `bodies_` has no live concurrent
  writer/reader pair today, and the rationale above is stronger than the
  current code warrants. It is a guard against the next hook, not against a
  bug in flight.

## Risks

**A lock in the physics path is a permanent cost for a rare hazard.** The
contention is real but bounded: one worker raycast per script per tick
against one physics update. If a profile ever shows it, the answer is
snapshot-served geometry, not a finer-grained lock — recorded as a
non-goal rather than left as an implicit next step.

**The overlap detector is probabilistic.** It widens the race window
artificially, but a passing run does not prove the absence of a race under
different timing. It proves the lock is present and working for the
interleavings it exercised. That limit is stated here rather than implied
by a green tick.

**Holding the lock across ECS traversal in `update()`** means a worker
raycast waits on work unrelated to the backend. Accepted deliberately for
obviousness; noted so a future reader knows it was a choice, not an
oversight.

## Non-goals

To be recorded in `docs/project/deferred-work-backlog.md`:

- **Making other engine systems thread-safe.** This slice closes the
  physics boundary only, because that is the one scripts reach.
- **Reader-writer or lock-free refinement.** No evidence of contention
  yet; adding it now would be speculative.
- **Snapshot-served ray geometry.** The long-term answer if blocking ever
  shows up in a profile, and the only approach that removes the
  dependency entirely.
- **Auditing other `ScriptHost` hooks for thread safety.** `set_playing`
  and `request_stop` were verified during the previous slice's review to
  write only an atomic, so they are safe today — but nothing enforces that
  a future hook will be.
