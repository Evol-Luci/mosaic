# Physics — Apply Impulse (design)

**Date:** 2026-09-29
**Status:** approved (design), pending implementation plan
**Branch:** `physics-apply-impulse`, cut from `main` at `2d934e2`

## Intent

Give the engine its first way to impart momentum to a simulated body,
then use it to make demo objects pushable on click.

Nothing in Mosaic can currently make an object move under its own
momentum. `PhysicsBackend`'s only write path into the simulation is
`set_body_transform`, which teleports a body **and resets its velocity**,
and `BodyDef` carries no initial velocity. Objects can be positioned but
never pushed. This was discovered while trying to get the demo sphere
rolling so a specular highlight could be seen sweeping across it — but
the gap is not a demo problem. It blocks every gameplay verb that
involves motion: throwing, knockback, recoil, jumping, launching.

**Success criteria**

- A dynamic body can be pushed from C++ by an impulse applied at a world
  point.
- A sphere pushed off-center **rolls** — it gains angular velocity, not
  just linear velocity.
- Clicking a demo object in `mosaic run --window` visibly nudges it.
- A body that has settled and fallen asleep still responds to a click.
- Clicking the HUD does not also push the object behind it.

## Decisions

Confirmed with the user before design, not assumed:

1. **One method, world point required.**
   `apply_impulse(entity, impulse, world_point)` — a single call with a
   single meaning, which always produces correct torque. Two
   alternatives were rejected:
   - *A pair of methods* (`at_point` + `to_center`), mirroring Box3D's
     own API. More expressive, but it hands callers an easy way to pick
     the variant that makes objects slide unnaturally — the exact
     failure this slice exists to prevent.
   - *An optional point* defaulting to the center of mass. Convenient,
     but an optional argument that silently changes the *physical
     behavior* is a trap in an API meant to be read by future gameplay
     code.

   A center-of-mass push remains expressible by passing the body's
   center, so nothing is lost.

2. **Click pushes along the camera ray.** The impulse direction is the
   ray from the camera through the clicked point. It reads as poking the
   object, and because the click point on a sphere is off-center, it
   naturally produces both translation and spin. Two alternatives were
   rejected: pushing along the inward *surface normal* aims at a
   sphere's center and so produces almost pure translation with no roll,
   defeating the purpose; *horizontal-only* (the camera ray flattened to
   the ground plane) is the most controllable for a rolling demo but
   feels less like direct manipulation and is a special case that would
   not generalize.

## The engine primitive

### `PhysicsBackend` (engine/physics/physics_backend.hpp)

```cpp
// Apply an instantaneous linear impulse (N·s) to a dynamic body at a world-space
// point. Applying it away from the center of mass induces torque -- this is what
// makes a struck sphere roll rather than slide, and is why the point is required
// rather than optional. No-op for a static body.
virtual void apply_impulse(BodyHandle body,
                           const mosaic::foundation::math::Vec3& impulse,
                           const mosaic::foundation::math::Vec3& world_point) = 0;
```

A pure virtual, matching every other method on this interface. Only one
other implementation exists — the fake in
`tests/unit/physics/physics_system_test.cpp` — so a pure virtual costs
one small update and keeps the interface honest, rather than letting a
backend silently do nothing.

### `Box3DBackend` (modules/physics_box3d/box3d_backend.cpp)

```cpp
b3Body_ApplyLinearImpulse(id, b3Vec3{impulse.x, impulse.y, impulse.z},
                          b3Pos{point.x, point.y, point.z}, /*wake=*/true);
```

Two details that are easy to get wrong:

- **`wake` must be true.** Box3D lets settled bodies sleep. The demo
  objects fall and settle within about two seconds of launch, so every
  click a user would realistically try lands on a sleeping body. With
  `wake=false` the call succeeds, reports nothing, and the object does
  not move — a silent no-op that looks exactly like a broken feature.
- **`impulse` is `b3Vec3` (float), `point` is `b3Pos` (double).** The
  same float/double split the contact-manifold work already documented
  in `docs/superpowers/specs/2026-09-03-physics-contact-manifolds-design.md`.
  The conversion is explicit at the call site.

The existing handle convention holds: `bodies_[handle - 1]` is the
`b3BodyId`, and `handle == 0` is invalid. Guard exactly as
`set_body_transform` already does.

### `PhysicsSystem` (engine/physics/physics_system.hpp)

```cpp
// Push a dynamic body. `world_point` is where the impulse is applied -- off-center
// to induce spin. A no-op if the entity has no simulated body (never created, or
// has no RigidBody), matching this system's existing "missing data degrades, does
// not throw" contract.
void apply_impulse(mosaic::foundation::EntityID entity,
                   const mosaic::foundation::math::Vec3& impulse,
                   const mosaic::foundation::math::Vec3& world_point);
```

Callers think in entities, not `BodyHandle`s; the system already owns the
`bodies_` map. It looks up the handle and forwards, or returns silently.

**Interaction with the Transform sync, checked rather than assumed:** the
system's `update()` teleports a body whenever its `Transform` no longer
matches the pose last written to it. An impulse changes velocity, not the
`Transform`, so it does not trip that check. On the next `update()` the
body's new pose is read back and written to `Transform`, and
`synced_pose_` is refreshed alongside — the normal path. No special
ordering is required, and `apply_impulse` may be called from outside
`update()`.

## Click-to-nudge

Lives in a new `app/run/click_nudge.hpp`, called from the run loop in
`app/main.cpp`. The `app` tier already owns the GLFW pointer provider and
is the tier permitted to depend on `studio`. `engine` is the wrong home —
it cannot depend on `studio`, where the picker lives — and scripting is
not an option because scripts can neither raycast nor push a body (both
tracked as deferred).

Flow, on each frame:

1. **Press edge only.** Act when `p.down && !was_down`, mirroring
   `UiSystem`'s existing pattern, so holding the button does not fire an
   impulse every frame.
2. **Skip clicks the UI claimed** (see below).
3. **Normalized pointer → NDC.** `PointerState` is `[0,1]` with a
   top-left origin; `pick_entity` wants `[-1,1]` with +y up:
   ```cpp
   const float ndc_x = p.x * 2.0f - 1.0f;
   const float ndc_y = 1.0f - p.y * 2.0f;   // Y flip
   ```
   Dropping that flip yields a picker that is mirrored vertically — it
   still "works", just on the wrong object, which is why it is written
   out here.
4. **Pick.** `studio::pick_entity(world, aspect, ndc_x, ndc_y)` returns a
   `RaycastHit{hit, entity, point, normal, distance}`. Bail if `!hit`.
5. **Push.** Direction is the camera ray through the hit point;
   `impulse = normalize(hit.point - camera_position) * kNudgeStrength`,
   applied at `hit.point`.

`kNudgeStrength` is a named constant. For scale: the demo sphere has
radius 0.5 and density 1, so mass ≈ 0.52 kg, and an impulse of 1.0 N·s
gives roughly 1.9 m/s — a firm but not comical shove. The value is tuned
by eye during implementation and documented where it is defined.

### UI arbitration

`UiSystem` already records whether a press landed on a `UiButton`, in its
private `pressed_`, but exposes nothing. Without that information a click
on the demo HUD panel would fall through and also poke whatever sits
behind it.

Add one public accessor:

```cpp
// True when the most recent press landed on a UiButton, so world-space click
// handling (picking, nudging) can decline a click the UI already claimed.
bool press_consumed() const { return pressed_ != mosaic::foundation::EntityID{}; }
```

This is input arbitration the UI layer should own regardless. The
alternative — duplicating `UiSystem::hit()`'s rect test inside `app` —
would put the same logic in two places and drift.

This requires `UiSystem::update()` to have run before the nudge in the
same frame. The run loop ticks systems and then calls the nudge, so the
ordering holds; the plan states it as a precondition at the call site.

## Testing

Against the fake backend in `tests/unit/physics/physics_system_test.cpp`:

1. `apply_impulse` forwards to the backend with the correct `BodyHandle`,
   impulse and point for a known entity.
2. It is a silent no-op for an entity with no `RigidBody`.
3. It is a silent no-op for an unknown/never-created entity.

Against the **real Box3D backend** — this is the test that matters:

4. A dynamic sphere given an impulse at a point offset from its center,
   then stepped, must show **both** a changed position and a changed
   rotation. Position alone passes even if someone swaps in
   `b3Body_ApplyLinearImpulseToCenter`; the rotation assertion is what
   pins *rolling* rather than sliding, which is the decision this whole
   slice turns on.
5. A sphere that has been stepped long enough to fall asleep still moves
   when struck — the `wake=true` guarantee. Without this, the most
   common real-world case (clicking a settled object) is untested.

For the click logic, `app/run/click_nudge.hpp` exposes the decision as a
pure function so it can be tested without a window:

6. The press edge fires once per press, not once per frame held.
7. NDC conversion maps a known normalized point to the expected NDC,
   including the Y flip.
8. A press the UI claimed produces no impulse.

## Risks

**The silent-no-op family.** Every likely failure here is quiet rather
than loud: a sleeping body that never wakes, a static body that ignores
the push, an entity with no physics body, a ray that misses. None throws;
all look identical to "the feature is broken." Mitigation is that each
one is a named test above rather than a thing to notice by hand.

**The rolling-vs-sliding distinction is invisible in most tests.** A
position assertion passes for both. Test 4 is the only guard, and it is
called out here so it does not get quietly relaxed into "the object
moved."

## Non-goals

All recorded in `docs/project/deferred-work-backlog.md` (Physics pillar)
with rationale:

- **Angular impulse / torque API** — the linear-at-a-point call already
  induces spin; a direct torque verb has no caller yet.
- **Script (Python) bindings** for impulses and raycasts — needs its own
  binding-surface design; depends on this API existing first.
- **Continuous forces** as distinct from instantaneous impulses —
  different semantics, no caller today.
- **Drag-to-throw** — needs press/drag/release tracking nothing else
  wants yet.
- **`BodyDef::rotation`** — static bodies cannot be created tilted;
  independent of this work and unnecessary once impulses exist.
