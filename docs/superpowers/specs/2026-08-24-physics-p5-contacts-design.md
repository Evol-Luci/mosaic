# Physics P5 — Contact Events + Determinism (design)

**Decomposition:** [physics-decomposition](2026-08-24-physics-decomposition.md) (slice P5, final).
**Builds on:** P1–P4 (backend, system, shapes, raycast), #11 EventBus, #33 logging, 37e console.
**Design intent:** [design/physics.md](../../design/physics.md) — determinism + record/replay as
public guarantees.
**Date:** 2026-08-24.

## Summary

The final physics slice: **contact events** (begin/end touch) surfaced through the #11
`EventBus` as a neutral `ContactEvent` carrying the two entities, a **contact-sequence
determinism** test (the record/replay guarantee, applied to the event stream), and a small
**Console payoff** — the app logs contacts so they show in the 37e Console panel when the demo
bodies land. This closes the physics pillar for v0.1.

## Scope

**In:**
- Box3D backend: enable contact events on shapes; after each step, collect begin/end touch
  events and expose them neutrally as `BodyContact`.
- Engine: a public `ContactEvent { ContactPhase; EntityID a; EntityID b; }` emitted on the
  `World`'s `EventBus` by `PhysicsSystem` each tick.
- Console payoff: the app subscribes to `ContactEvent` and logs it (#33 → 37e Console).
- Headless tests: begin fires on landing, end fires on separation, and the contact sequence
  is deterministic across runs.

**Deferred (later / YAGNI):** full record/replay-to-disk (Box3D's recorder — a larger future
feature; the determinism *guarantee* is satisfied by reproducible event sequences), contact
manifolds/points/normals/impulses (P5 carries only the pair + phase), and hit/impact events.

## Architecture

### Backend contact retrieval (`engine/physics/physics_backend.hpp` + Box3D)

Neutral types:

```cpp
enum class ContactPhase { Begin, End };

struct BodyContact
{
    ContactPhase phase;
    BodyHandle   a;
    BodyHandle   b;
};

// on class PhysicsBackend (pure; the one test fake stubs it -> {}):
virtual std::vector<BodyContact> contact_events() const = 0; // touch events from the last step
```

Box3D (`modules/physics_box3d/box3d_backend.cpp`):
- **Enable events on shapes.** In `create_body`, set `sd.enableContactEvents = true` before
  creating the shape (on all shapes; ignored for static bodies, per Box3D). This is required —
  touch events are off by default.
- **Collect after step.** `step()` calls `b3World_Step`, then `b3World_GetContactEvents(world_)`
  and builds `last_contacts_`: for each of `beginCount` begin events and `endCount` end events,
  map `shapeIdA`/`shapeIdB` → body (`b3Shape_GetBody`) → `BodyHandle` (the userData stashed in
  P4) → a `BodyContact{phase, a, b}`. The Box3D event arrays are valid only until the next step,
  so mapping to neutral copies inside `step()` is what keeps them usable.
- `contact_events()` returns `last_contacts_` (by value).

Verified in the pinned headers: `b3ContactEvents b3World_GetContactEvents(b3WorldId)` with
`{ b3ContactBeginTouchEvent* beginEvents; b3ContactEndTouchEvent* endEvents; int beginCount;
int endCount; ... }`; each touch event has `b3ShapeId shapeIdA, shapeIdB`; `b3ShapeDef` has a
`bool enableContactEvents`.

### Engine event on the EventBus (`engine/physics/physics_system.hpp`)

```cpp
struct ContactEvent  // the public, subscribable physics event
{
    ContactPhase                 phase;
    mosaic::foundation::EntityID a;
    mosaic::foundation::EntityID b;
};
```

`PhysicsSystem::update` gains a final step (after step + write-back): drain
`backend_->contact_events()`, map each `BodyContact`'s `a`/`b` handles → `EntityID` via the
existing `handle_to_entity_` reverse map (P4), and `world.events().emit<ContactEvent>({phase,
ea, eb})`. A contact whose handle is unmapped is skipped. Synchronous subscribers fire in-tick;
deferred subscribers observe it on the next drain (standard #11 semantics).

Two layered types (backend `BodyContact`/handles → engine `ContactEvent`/entities), consistent
with raycast (`RaycastResult` → `RaycastHit`).

### Console payoff (`app/run/demo_scene.hpp`)

`build_demo_scene` (shared by `mosaic editor` and `mosaic run --window`) subscribes on
`world.events()` to `ContactEvent` and logs each one via #33:

```cpp
world.events().subscribe<physics::ContactEvent>(
    [](const physics::ContactEvent& c) {
        mosaic::foundation::log::info("physics", "contact {}: entity {} <-> entity {}",
            c.phase == physics::ContactPhase::Begin ? "begin" : "end", c.a.value(), c.b.value());
    });
```

In the editor the line lands in the 37e **Console panel** (its `ConsoleSink`); under `mosaic
run` it lands on stderr (the CLI `ConsoleSink`). The subscription lives with the `World`. Noise
is bounded — the demo bodies produce a short burst of begins as they land, then rest.

## Testability

Headless + deterministic (the physics payoff).

- **Begin fires on landing** (`tests/unit/physics/physics_contacts_test.cpp`): subscribe to
  `ContactEvent`; drop a dynamic box onto a static ground; tick until it lands; assert at least
  one `Begin` event whose `{a, b}` is the box+ground pair (order-insensitive).
- **End fires on separation:** after landing, teleport the box far up/away (external Transform
  edit, as the editor does); tick; assert an `End` for that pair.
- **Contact-sequence determinism (record/replay):** run a scenario (box drops onto ground +
  a second box lands on the first) twice, recording `(tick, phase, a, b)` for every emitted
  `ContactEvent`; assert the two recordings are **identical**.
- **Functional:** `mosaic editor`/`mosaic run --window` exit 0 with 0 validation errors; the
  Console shows `contact begin: …` lines as the demo bodies land (maintainer confirms).

Contact-event firing rule risk: if Box3D only fires touch events when *both* shapes enable them
(and a static shape can't), the box↔ground begin may not fire — the begin test then uses two
**dynamic** boxes (both enabled) instead. The plan's test-first step will surface this; the
determinism + dynamic-pair coverage hold regardless.

## Ground rules honored

- **Tier contract:** `BodyContact`/`ContactEvent`/`contact_events` stay neutral in
  `engine/physics/`; `b3ContactEvents`/`b3World_GetContactEvents` stay in `modules/physics_box3d`.
- **Determinism:** unchanged stepping; the contact sequence is asserted reproducible.
- **Reflected-property / events:** contacts flow through the existing #11 `EventBus`, the same
  channel commands/UI already use — no physics-specific event system.
- No new third-party deps. Catch2 test names avoid a leading `--`.

## Acceptance

- [ ] Backend enables contact events + returns `BodyContact`s from the last step.
- [ ] `PhysicsSystem` emits `ContactEvent` (entities) on the `EventBus` each tick.
- [ ] Begin/end + contact-sequence-determinism headless tests green.
- [ ] The demo logs contacts; `mosaic editor` Console shows them; exit 0, 0 validation errors.
- [ ] Full unit suite green. **Physics pillar complete for v0.1.**

## Depends on / relates

- **Depends on:** P1–P4, #11 `EventBus` (`World::events()`), #33 logging + 37e console (payoff).
- **Relates:** scripting will subscribe to `ContactEvent` through the reflected API; full
  record/replay-to-disk is a deferred, larger determinism feature.
