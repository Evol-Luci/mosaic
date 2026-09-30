# Scripting — Buffered Off-Thread Physics Writes (design)

**Date:** 2026-09-30
**Status:** approved (design), pending implementation plan
**Branch:** `script-buffered-physics-writes`, cut from `main` at `c2cc75c`

## Intent

Turn a guard into a fix.

`apply_impulse` shipped in #207 with a deliberate hole: on the
`--script-thread` path the hook is simply **not wired**, so the call
returns `False` and does nothing. That was the right call at the time —
the alternative was undefined behaviour, because `on_update` runs on a
worker thread while the main thread may be inside `PhysicsSystem`'s step,
and the binding wrote straight into the live Box3D body. But it leaves a
script that works single-threaded silently doing nothing under a shipped
CLI flag, which is its own kind of broken.

This slice buffers impulses the way the threaded path already buffers
component writes, so the same script works in both modes.

**Success criteria**

- A script that pushes a body works under `--script-thread`, with the
  one-tick lag that path already has for every other write.
- No script code touches the live physics world from the worker thread.
- `apply_impulse`'s return value means the same thing in both modes.
- The `!script_thread` guard in `setup_runtime` is **removed**, not
  merely bypassed.

## What the existing machinery already gives us

`ScriptSystem::update_threaded` already runs this cycle each tick
(`engine/script/script_system.hpp:165-190`):

1. `collect()` the **previous** tick's `SnapshotContext::Write`s and apply
   them on the main thread via `types_.set_value(...)`.
2. Snapshot every scripted entity's components into the job.
3. `dispatch()` the job; the worker runs `on_update` against the snapshot.

Impulses ride the same road. Nothing new is invented — a second channel is
added to a lane that already exists.

## Decisions

Confirmed with the user before design:

1. **The return value stays honest, answered from the snapshot.** Step 2
   snapshots *every* component on each scripted entity
   (`types_.components_on`), and body creation is determined by exactly
   those: a `RigidBody` plus a collider. So the worker answers
   `True`/`False` from the snapshot rather than guessing. It is one tick
   stale — stale in precisely the way everything else on this path is.

   Two alternatives were rejected. *Always `True` ("queued")* would make
   the return value mean something different depending on a CLI flag, so a
   script that checks the result to decide whether to retry would behave
   differently in the two modes. *Always `False`* reads as failure, so a
   guarding script would stop pushing entirely and the feature would look
   broken under `--script-thread` even though it worked.

2. **`raycast` stays as it is, out of scope.** Buffering fixes *writes*;
   a raycast is a read that needs its answer inside the call and cannot be
   deferred. The honest options for it — return a miss on the threaded
   path, or answer rays against snapshotted collider geometry — are a
   behaviour regression and a slice of its own respectively. Its existing
   backlog entry stands.

3. **A second channel on the collected result**, rather than reusing the
   write channel or giving physics a queue. Two alternatives were
   rejected: modelling an impulse as a `Write` with a magic component name
   (`comp == "__impulse"`) smuggles a non-component through the component
   channel and forces `set_value` to special-case it — the same instinct
   as hiding a material parameter inside a matrix, which this project
   deliberately undid one slice earlier; and a mutex-protected queue owned
   by `PhysicsSystem` puts locking in the physics hot path and teaches
   physics about scripts, with no second producer to justify it.

## Design

### The buffered impulse

```cpp
// engine/script/script_worker.hpp (or script_context.hpp, beside Write)
struct DeferredImpulse
{
    std::uint64_t                  entity;
    mosaic::foundation::math::Vec3 impulse;
    mosaic::foundation::math::Vec3 point;
};
```

### `ScriptWorker` records instead of applying

The worker binds **its own** `apply_impulse` into the engines it owns,
replacing whatever the host carried. That callback:

- appends a `DeferredImpulse` to a per-job buffer, and
- returns whether the **current job's snapshot** shows that entity with a
  `RigidBody` and a collider component (`Collider`, `SphereCollider` or
  `CapsuleCollider` — the three `shape_for` accepts).

Only the worker thread touches that buffer while a job runs; `collect()`
moves it out under the mutex the worker already holds. No new
synchronization primitive is introduced.

### `collect()` returns both channels

```cpp
struct WorkerResult
{
    std::vector<SnapshotContext::Write> writes;
    std::vector<DeferredImpulse>        impulses;
};
WorkerResult collect();
```

This changes `collect()`'s signature, which has one caller
(`script_system.hpp:169`) and one test file.

### `ScriptSystem` replays on the main thread

In `update_threaded`, after applying the writes, replay each impulse
**through `host_.apply_impulse`** — the neutral callback `ScriptSystem`
already holds (`script_system.hpp:241`). This matters: `ScriptSystem`
still never learns what physics is, so the neutrality rule that shaped the
whole binding holds unchanged.

Ordering falls out for free. `ScriptSystem` is registered before
`PhysicsSystem`, so a replayed impulse lands before that tick's step, with
the same one-tick lag as every other threaded script write.

### The guard comes out

`setup_runtime` returns to wiring `apply_impulse` unconditionally:

```cpp
if (!host.apply_impulse) { host.apply_impulse = make_physics_apply_impulse(world); }
```

The `&& !script_thread` clause and its explanatory comment are deleted,
and the backlog entry describing the guard is marked shipped. The worker
now intercepts the call before it can reach the live world, so the
condition the guard existed for no longer holds.

## Testing

1. **The one that matters:** a `--script-thread` run where a script pushes
   a body and the body **moves**. This is what is silently broken today,
   and no other test observes it. Build a world, register
   `ScriptSystem` with `script_thread = true`, tick enough for the
   one-tick lag, assert the Transform changed.
2. The worker's `apply_impulse` does **not** touch the world: bind a host
   whose `apply_impulse` sets a flag, run a job, and assert the flag is
   still unset after the job completes but before `collect()` is replayed.
3. `collect()` returns the recorded impulses with the right entity,
   impulse and point.
4. Snapshot-based validity: an entity with a `ScriptComponent` but no
   `RigidBody` gets `False` from the worker's binding, and no impulse is
   buffered for it.
5. An entity with a `RigidBody` but no collider also gets `False` — body
   creation needs both, so checking only `RigidBody` would over-report.
6. The single-threaded path is unchanged: the existing
   `script_runtime_test.cpp` end-to-end push still passes untouched.
7. `setup_runtime` wires `apply_impulse` even when `script_thread` is
   true — the guard's removal, asserted rather than assumed.

## Risks

**The one-tick lag is now load-bearing for physics, not just for
component writes.** A script that pushes every frame under
`--script-thread` will see its impulses applied a tick late, which for
accumulating pushes means the *timing* differs slightly between modes even
though the behaviour is now correct in both. That is inherent to the
threaded path's contract and is documented rather than hidden.

**`collect()`'s signature change touches the worker's own tests.** They
are the only other caller, but a sloppy migration could quietly drop the
writes channel while the impulse channel passes — so the existing write
tests must be confirmed still meaningful after the change, not merely
still compiling.

## Non-goals

To be recorded in `docs/project/deferred-work-backlog.md`:

- **`raycast`'s off-thread read** — unchanged by this slice, existing
  entry stands.
- **Other physics verbs from scripts** (set velocity, teleport, overlap
  queries) — unchanged.
- **Deferred host calls as a general mechanism** — only `apply_impulse`
  is buffered. If a third deferrable verb appears, generalising the
  channel is a better change than adding a third parallel vector, but two
  is not yet a pattern.
- **Removing the one-tick lag** — it is the threaded path's defining
  contract, not a defect.
