# Physics contact manifolds — richer contact info (design)

**Phase:** post-v0.1 physics follow-up (deferred from P5).
**Builds on:** P5 (`ContactEvent`/`BodyContact` begin/end touch events), the neutral `PhysicsBackend`
wall + the Box3D backend, `PhysicsSystem`.
**Date:** 2026-09-03.

> Built autonomously (the user authorized unattended roadmap work). Headless-only slice — fully
> verifiable without a visual step.

## Summary

Today a physics contact carries only `{phase, a, b}` — *that* two bodies touched, not *where* or
*how hard*. This adds the **manifold detail** to a Begin contact: the world **contact point**, the
world **normal**, the **separation** (penetration depth), and the **normal impulse**. It surfaces
through the neutral `BodyContact` and the public `ContactEvent`, so gameplay can react to *where* a
hit landed and *how hard* (sound, damage, particles, decals) — the payoff P5 deferred. Fully
deterministic and headless-testable.

## Scope

**In:**
- Extend the neutral `BodyContact` (`engine/physics/physics_backend.hpp`) with `point`, `normal`,
  `separation`, `normal_impulse` — populated for **Begin** contacts, zero for **End** (an End event
  has no manifold).
- The Box3D backend reads the manifold for each begin-touch via `b3Contact_GetData(contactId)` and
  fills those fields (world point derived from the manifold anchor + the body's world center of
  mass). `b3*` types never cross the wall.
- Extend the public `ContactEvent` (`engine/physics/physics_system.hpp`) with the same four fields;
  `PhysicsSystem` passes them through when it maps a `BodyContact` to entities.

**Deferred (documented):**
- **Hit events** (Box3D's `b3ContactHitEvent`: point/normal/approach-speed for above-threshold
  collisions) — a second, threshold-gated channel; the manifold path here covers every contact
  (resting or fast), so hit events are a later refinement.
- **Multiple manifold points** and **friction/rolling impulses** — this slice exposes the *first*
  (deepest/primary) manifold point + the normal impulse; per-point arrays and tangential impulses
  are a follow-up.

## Architecture

### 1. Neutral `BodyContact` (`engine/physics/physics_backend.hpp`)

```cpp
struct BodyContact
{
    ContactPhase phase;
    BodyHandle   a;
    BodyHandle   b;
    // Manifold detail — populated for Begin contacts; zero for End (no manifold).
    mosaic::foundation::math::Vec3 point{};   // world contact point (primary manifold point)
    mosaic::foundation::math::Vec3 normal{};  // world contact normal (from a's shape toward b's)
    float                          separation = 0.0f;      // <0 = penetrating
    float                          normal_impulse = 0.0f;  // impulse along the normal (final sub-step)
};
```
(`Vec3` is already included via the neighboring `RaycastResult`, which uses `point`/`normal` too.)

### 2. Box3D backend (`modules/physics_box3d/box3d_backend.cpp`)

In `step()`'s begin-event loop, after resolving the two body handles, fetch the manifold and fill the
new fields (End loop is unchanged — those stay zero). Box3D specifics (verified against the pinned
version): `b3ContactBeginTouchEvent` has `{shapeIdA, shapeIdB, contactId}`; `b3Contact_GetData(id)`
returns `b3ContactData{ ... const b3Manifold* manifolds; int manifoldCount; }`; a `b3Manifold` has
`b3ManifoldPoint points[]`, `int pointCount`, `b3Vec3 normal`; a `b3ManifoldPoint` has
`b3Vec3 anchorA` (contact point **relative to body A's world center of mass**), `float separation`,
`float normalImpulse`; `b3Body_GetWorldCenter(bodyId)` → `b3Pos{double x,y,z}` (the world COM).

```cpp
const b3ContactBeginTouchEvent& be = ev.beginEvents[i];
ep::BodyContact c{ep::ContactPhase::Begin, handle_of(b3Shape_GetBody(be.shapeIdA)),
                  handle_of(b3Shape_GetBody(be.shapeIdB))};
if (b3Contact_IsValid(be.contactId))
{
    const b3ContactData cd = b3Contact_GetData(be.contactId);
    if (cd.manifoldCount > 0 && cd.manifolds[0].pointCount > 0)
    {
        const b3Manifold&      m = cd.manifolds[0];
        const b3ManifoldPoint& p = m.points[0];
        const b3Pos            ca = b3Body_GetWorldCenter(b3Shape_GetBody(be.shapeIdA));
        c.point  = Vec3{static_cast<float>(ca.x) + p.anchorA.x,
                        static_cast<float>(ca.y) + p.anchorA.y,
                        static_cast<float>(ca.z) + p.anchorA.z};
        c.normal = Vec3{m.normal.x, m.normal.y, m.normal.z};
        c.separation     = p.separation;
        c.normal_impulse = p.normalImpulse;
    }
}
last_contacts_.push_back(c);
```
`b3Contact_IsValid` guards a stale contactId; a manifold with no points leaves the fields zero
(harmless). No shape-def change is needed — `enableContactEvents` is already on; hit events are not
used.

### 3. Public `ContactEvent` + `PhysicsSystem` (`engine/physics/physics_system.hpp`)

```cpp
struct ContactEvent
{
    ContactPhase                 phase;
    mosaic::foundation::EntityID a;
    mosaic::foundation::EntityID b;
    mosaic::foundation::math::Vec3 point{};
    mosaic::foundation::math::Vec3 normal{};
    float                          separation = 0.0f;
    float                          normal_impulse = 0.0f;
};
```
The emit becomes a passthrough of the new fields:
```cpp
world.events().emit<ContactEvent>(
    ContactEvent{c.phase, ia->second, ib->second, c.point, c.normal, c.separation, c.normal_impulse});
```

## Data flow

```
b3World_Step -> begin-touch event (contactId)
  -> b3Contact_GetData(contactId).manifolds[0]  ->  normal, points[0].{anchorA,separation,normalImpulse}
  -> world point = b3Body_GetWorldCenter(bodyA) + anchorA
  -> BodyContact{ ..., point, normal, separation, normal_impulse }
PhysicsSystem: map handles -> entities -> emit ContactEvent{ ..., point, normal, separation, normal_impulse }
```

## Error handling

- A stale/invalid `contactId` (`!b3Contact_IsValid`) or a manifold with `pointCount == 0` → the four
  fields stay zero; the contact is still reported (phase/a/b intact). Never throws.
- End contacts carry zeroed manifold fields by construction (no manifold exists at separation).
- `b3*` types never leak above `modules/physics_box3d` — the neutral `BodyContact`/`ContactEvent` use
  only `Vec3`/`float`.

## Testability

Headless, deterministic:
- **Backend** (`tests/unit/physics/physics_contacts_test.cpp`, extend): the existing "box lands on the
  ground" begin-touch test also asserts, on the Begin contact, that the **normal is vertical + unit
  length** (`std::abs(normal.y) > 0.9f`, `length(normal)` ≈ 1), the **separation is small**
  (`std::abs(separation) < 0.5f`), and the **point is finite and near the interface** (its `y` within
  a generous band around the ground top). Exact componentwise values are avoided (the A/B ordering —
  hence the normal's sign — is a Box3D internal), keeping the test robust; determinism is already
  covered by P5's sequence test.
- **ECS** (`tests/unit/physics/physics_contacts_ecs_test.cpp` or extend the P2/P5 ECS test): a
  `World` with a dynamic box over a static ground + a `PhysicsSystem`; subscribe to `ContactEvent`;
  tick until a Begin arrives; assert its `normal` is vertical/unit and `a`/`b` are the two entities —
  proving the manifold data propagates to the entity-level event.
- Full suite green.

## Ground rules honored

- **Tier wall:** all `b3*` calls stay in `modules/physics_box3d`; the neutral `PhysicsBackend` /
  `BodyContact` / `ContactEvent` remain Box3D-free (`Vec3`/`float` only) — same wall as the rest of
  physics.
- **Determinism:** the manifold is a deterministic function of the (deterministic) step; no new
  randomness. No new third-party deps (Box3D already vendored). Catch2 names avoid a leading `--`.

## Acceptance

- [ ] `BodyContact` + `ContactEvent` carry `point`/`normal`/`separation`/`normal_impulse`; populated
      for Begin, zero for End.
- [ ] The Box3D backend fills them from the manifold (`b3Contact_GetData`); `b3*` stays in the module.
- [ ] A backend test asserts a real vertical/unit normal + small separation on box-lands-ground; an
      ECS test asserts the data reaches the entity-level `ContactEvent`. Full suite green.

## Depends on / relates

- **Depends on:** P5 (contact events), the neutral `PhysicsBackend`, `PhysicsSystem`. **Relates:** the
  deferred hit-event channel, viewport mouse-picking (`raycast`), and any gameplay that reacts to
  contacts (audio, damage, VFX) — all consume this richer `ContactEvent`.
