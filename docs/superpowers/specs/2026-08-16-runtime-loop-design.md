# Runtime loop (#22) — design

**Status:** Approved (brainstorming) — pending implementation plan
**Date:** 2026-08-16
**Layer:** Engine — the last unit of the [v0.1 vertical slice](../../project/roadmap.md#the-v01-vertical-slice--the-first-mountain) engine core
**Depends on:** #9 (`World::tick`)
**Related:** [#22](https://github.com/Evol-Luci/mosaic/issues/22), [next-steps](../../project/next-steps.md)

## Summary

The outer driver that turns elapsed real time into fixed simulation ticks — a
deterministic fixed-timestep accumulator that calls `World::tick(fixed_dt)` the right
number of times per frame, with start/stop/single-step controls. It owns **pacing
only**; the per-tick order (drain deferred events → run systems → commit
destructions) already lives in `World::tick`. This is the still-open piece the World
runtime deliberately left, and it completes the headless engine core.

## Motivation

`World::tick(dt)` advances the world one step, but nothing calls it in a loop —
"press Play" has no pacing. The runtime loop supplies exactly that: it decouples the
deterministic simulation step from wall-clock and frame rate, so the same input plus
the same dt schedule always yields the same tick count and state. It is the seam the
World runtime explicitly deferred to a later scheduler.

## Scope

**In scope:**
- A fixed-timestep accumulator that dispatches `World::tick(fixed_dt)`.
- Start / stop / single-step controls.
- A spiral-of-death guard (cap catch-up ticks per advance).

**Explicitly out of scope** (future work):
- **The real-time `std::chrono` frame pump** (measure wall-clock dt, sleep to a
  target rate). That belongs to the app / `main`, which does not exist yet. The
  loop consumes an externally supplied `real_dt` — exactly the seam this layer was
  left to fill.
- **Render-interpolation alpha** from the leftover accumulator — no renderer yet.
- **Threaded / job-scheduled ticks** — single-threaded by design (systems run in
  registration order inside `World::tick`).

## Design decisions

Settled during brainstorming:

1. **Deterministic `advance(world, real_dt)` core; no wall-clock inside.** The caller
   feeds elapsed real time each frame; the loop dispatches the fixed ticks. This
   keeps the loop fully deterministic and testable (feed a dt schedule, assert the
   tick count) and leaves the `std::chrono` pump to the app.
2. **Ticks a `World&` directly** (`world.tick(fixed_dt)`), matching the issue. Tests
   use a `World` with a counting `System` to verify tick count and the dt passed.
3. **`stop()` is a pause that ignores fed time** — while stopped, `advance` is a
   no-op and does not bank `real_dt`, so resuming does not flood a backlog. **`step()`**
   is a manual single tick independent of running state / accumulator.
4. **Spiral-of-death guard:** `max_steps_per_advance` caps catch-up ticks; if the cap
   is hit with time still pending, the excess accumulated time is dropped.

## Architecture

### Boundaries

`engine/runtime` depends only on `engine/world` (`World`). Header-only. No upward
dependency; tier contract holds.

### Files

| File | Change |
|------|--------|
| `engine/runtime/runtime_loop.hpp` | New. `RuntimeLoop`. Namespace `mosaic::engine::runtime`. Header-only. |
| `tests/unit/runtime/runtime_loop_test.cpp` | New. `[runtime]` tests. |
| `tests/CMakeLists.txt` | Register the new test source. |

No `CMakeLists.txt` library changes: header-only; tests link existing libs.

### API

```cpp
namespace mosaic::engine::runtime {

class RuntimeLoop
{
public:
    // fixed_dt: the deterministic sim step (seconds, > 0).
    // max_steps_per_advance: catch-up cap per advance (spiral-of-death guard).
    explicit RuntimeLoop(float fixed_dt, int max_steps_per_advance = 8);

    void start();                 // running = true
    void stop();                  // running = false (pause)
    bool is_running() const;

    // Feed elapsed real time; dispatch as many world.tick(fixed_dt) as fit.
    // Returns the number of ticks run. No-op returning 0 while stopped. Remainder
    // carries to the next advance. Negative real_dt is clamped to 0.
    int advance(World& world, float real_dt);

    // One fixed tick regardless of running state / accumulator. Returns 1.
    int step(World& world);

    float fixed_dt() const;
    float accumulator() const;         // leftover real time not yet ticked
    std::uint64_t tick_count() const;  // total ticks driven
};

} // namespace
```

### `advance` algorithm

```
if (!running_) return 0;                       // paused: fed time ignored, not banked
accumulator_ += (real_dt > 0 ? real_dt : 0);   // clamp negatives
steps = 0;
while (accumulator_ >= fixed_dt_ && steps < max_steps_)
{
    world.tick(fixed_dt_);
    accumulator_ -= fixed_dt_;
    ++tick_count_;
    ++steps;
}
if (steps == max_steps_ && accumulator_ >= fixed_dt_)  // hit cap with time pending
{
    accumulator_ = 0.0f;                       // drop backlog (spiral-of-death guard)
}
return steps;
```

`step` runs `world.tick(fixed_dt_)` once, increments `tick_count_`, returns 1 — it
does not touch `running_` or `accumulator_`.

### Data flow

```
app frame ──real_dt──▶ RuntimeLoop::advance ──(N×)──▶ World::tick(fixed_dt)
                                                         └─ drain events → systems → commit
```

The app measures wall-clock dt and calls `advance` each frame; the loop translates
that into whole fixed ticks. Determinism: identical `(fixed_dt, real_dt schedule)`
⇒ identical tick count ⇒ identical World state.

## Error handling

- **Negative `real_dt`** → clamped to 0 (defensive; a backwards clock never rewinds
  the sim).
- **`fixed_dt <= 0`** → a programming error; documented precondition (constructor
  assumes `fixed_dt > 0`). Not guarded per call.
- **Sustained overload** (each `advance` exceeds `max_steps_per_advance` of work) →
  excess time dropped; the sim runs slower than real time but never spirals.

## Testing (TDD, `tests/unit/runtime/`, tag `[runtime]`)

A test-local `System` records its update count and the last dt it received; a
`World` registers it so ticks are observable.

- **Fixed dispatch:** `fixed_dt = 0.01`, running; `advance(world, 0.05)` runs 5
  ticks; the counting system saw 5 updates, each with dt `0.01`.
- **Remainder carries:** `advance(world, 0.005)` → 0 ticks; a following
  `advance(world, 0.006)` → 1 tick (accumulated `0.011`).
- **stop / start:** after `stop()`, `advance` returns 0 and the accumulator is
  unchanged (fed time ignored); after `start()`, ticking resumes.
- **single-step:** `step(world)` runs exactly one tick and increments `tick_count`,
  regardless of running state or accumulator.
- **determinism:** two loops with the same `fixed_dt` fed the same `real_dt`
  schedule end with equal `tick_count`.
- **spiral-of-death cap:** a single huge `advance` runs at most
  `max_steps_per_advance` ticks.
- **populated-World stability:** a `World` with several entities and a
  `TransformSystem`, advanced over many ticks, stays consistent — entity count
  stable and `WorldTransform`s computed (acceptance: "headless run of a populated
  World over many ticks is stable").

Full suite stays green.

## Open questions / follow-ups

- **Real-time pump:** the `std::chrono` measure-and-sleep frame driver arrives with
  the app/CLI (`mosaic run`) that owns the process main loop.
- **Interpolation alpha:** expose `accumulator() / fixed_dt()` as a render lerp
  factor once a renderer exists.
- **Variable / semi-fixed timestep** and per-system tick rates — revisit if a
  subsystem needs them; the fixed step is the v0.1 primitive.
</content>
