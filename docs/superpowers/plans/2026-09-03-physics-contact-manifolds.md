# Physics Contact Manifolds Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add manifold detail — world contact **point**, **normal**, **separation**, **normal impulse** — to a Begin physics contact, surfaced through the neutral `BodyContact` and the public `ContactEvent`, so gameplay can react to where/how hard a hit landed.

**Architecture:** Extend the neutral `BodyContact` (4 new fields). The Box3D backend reads each begin-touch's manifold via `b3Contact_GetData(contactId)` (world point = the manifold anchor + the body's world center of mass); `b3*` stays inside the module. Extend the public `ContactEvent` with the same fields and pass them through in `PhysicsSystem`. Fully headless + deterministic.

**Tech Stack:** C++20, CMake, Catch2, Box3D (already vendored). Reuses P5 (`ContactEvent`/`BodyContact`), the neutral `PhysicsBackend`, `PhysicsSystem`.

**Spec:** `docs/superpowers/specs/2026-09-03-physics-contact-manifolds-design.md`

## Global Constraints

- **Tier wall:** all `b3*` calls stay in `modules/physics_box3d`; the neutral `PhysicsBackend`/`BodyContact` and the public `ContactEvent` use only `Vec3`/`float` — no Box3D types cross the wall.
- **Begin-only manifold:** the 4 new fields are populated for **Begin** contacts; **End** contacts leave them zero (no manifold exists at separation).
- **Never throw:** a stale `contactId` (`!b3Contact_IsValid`) or an empty manifold (`pointCount == 0`) leaves the fields zero; the contact is still reported.
- **Determinism preserved:** no new randomness; the manifold is a deterministic function of the step.
- **Build:** `cmake --build build/debug -j`; **Test:** `ctest --test-dir build/debug`. Catch2 names not starting with `--`. Namespaces: `mosaic::engine::physics`, `mosaic::physics::box3d`, `mosaic::foundation::math`, `mosaic::foundation`.
- **Box3D API facts (verified against the pinned version):** `b3ContactBeginTouchEvent { b3ShapeId shapeIdA, shapeIdB; b3ContactId contactId; }`. `bool b3Contact_IsValid(b3ContactId)`. `b3ContactData b3Contact_GetData(b3ContactId)` where `b3ContactData { ...; const b3Manifold* manifolds; int manifoldCount; }`. `b3Manifold { b3ManifoldPoint points[]; int pointCount; b3Vec3 normal; ... }`. `b3ManifoldPoint { b3Vec3 anchorA; ...; float separation; ...; float normalImpulse; ... }` (`anchorA` = contact point **relative to body A's world center of mass**, world-space). `b3BodyId b3Shape_GetBody(b3ShapeId)`. `b3Pos b3Body_GetWorldCenter(b3BodyId)` where `b3Pos { double x, y, z; }` (world COM). `b3Vec3 { float x, y, z; }`. The backend already aliases `Vec3 = mosaic::foundation::math::Vec3` and has `handle_of(b3BodyId) -> BodyHandle`. `enableContactEvents` is already set; no shape-def change is needed.

---

### Task 1: Manifold detail in the neutral `BodyContact` + the Box3D backend

**Files:**
- Modify: `engine/physics/physics_backend.hpp` (`BodyContact` fields)
- Modify: `modules/physics_box3d/box3d_backend.cpp` (`step()` begin-event loop)
- Modify: `tests/unit/physics/physics_contacts_test.cpp` (backend test assertions)

**Interfaces:**
- Produces: `BodyContact` gains `Vec3 point; Vec3 normal; float separation; float normal_impulse;` (populated for Begin, zero for End).

- [ ] **Step 1: Extend the backend test's Begin assertions in `tests/unit/physics/physics_contacts_test.cpp`**

Add `#include <cmath>` near the top of the file (with the other includes). In the FIRST test ("backend reports a begin-touch contact when a box lands on the ground"), change the collection loop to capture the Begin contact, and add manifold assertions after `REQUIRE(begin_seen);`. Replace:
```cpp
    bool begin_seen = false;
    for (int i = 0; i < 180; ++i)
    {
        backend->step(1.0f / 60.0f, 4);
        for (const auto& c : backend->contact_events())
        {
            if (c.phase == ContactPhase::Begin && is_pair(c.a, c.b, gh, bh))
            {
                begin_seen = true;
            }
        }
    }
    REQUIRE(begin_seen);
}
```
with:
```cpp
    bool begin_seen = false;
    mosaic::engine::physics::BodyContact hit{};
    for (int i = 0; i < 180; ++i)
    {
        backend->step(1.0f / 60.0f, 4);
        for (const auto& c : backend->contact_events())
        {
            if (c.phase == ContactPhase::Begin && is_pair(c.a, c.b, gh, bh))
            {
                begin_seen = true;
                hit = c;
            }
        }
    }
    REQUIRE(begin_seen);

    // Manifold detail: a box landing flat on the ground has a vertical, unit-length normal, is
    // barely separated, and a finite contact point. (The A/B ordering — hence the normal's sign —
    // is a Box3D internal, so we assert |normal.y|, not its sign.)
    const float nlen = std::sqrt(hit.normal.x * hit.normal.x + hit.normal.y * hit.normal.y +
                                 hit.normal.z * hit.normal.z);
    REQUIRE(nlen > 0.99f);
    REQUIRE(nlen < 1.01f);
    REQUIRE(std::abs(hit.normal.y) > 0.9f);
    REQUIRE(std::abs(hit.separation) < 0.5f);
    REQUIRE(std::isfinite(hit.point.x));
    REQUIRE(std::isfinite(hit.point.y));
    REQUIRE(std::isfinite(hit.point.z));
}
```

- [ ] **Step 2: Build → confirm it fails**
```bash
cmake --build build/debug -j 2>&1 | tail -5
```
Expected: compile error — `BodyContact` has no member `point`/`normal`/`separation`.

- [ ] **Step 3: Extend `BodyContact` in `engine/physics/physics_backend.hpp`**

The struct currently is:
```cpp
struct BodyContact
{
    ContactPhase phase;
    BodyHandle   a;
    BodyHandle   b;
};
```
Replace with:
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
(`Vec3` is already in scope in this header — `RaycastResult` above uses it.)

- [ ] **Step 4: Fill the manifold in the Box3D backend `step()` (`modules/physics_box3d/box3d_backend.cpp`)**

Replace the begin-event loop:
```cpp
        for (int i = 0; i < ev.beginCount; ++i)
        {
            last_contacts_.push_back(ep::BodyContact{
                ep::ContactPhase::Begin, handle_of(b3Shape_GetBody(ev.beginEvents[i].shapeIdA)),
                handle_of(b3Shape_GetBody(ev.beginEvents[i].shapeIdB))});
        }
```
with:
```cpp
        for (int i = 0; i < ev.beginCount; ++i)
        {
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
        }
```
(The End-event loop is unchanged — those contacts keep zeroed manifold fields.)

- [ ] **Step 5: Build + test**
```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "backend reports a begin-touch" --output-on-failure
```
Expected: PASS. Then the full suite once (`ctest --test-dir build/debug 2>&1 | tail -3`) — 0 failures.

- [ ] **Step 6: Commit**
```bash
git add engine/physics/physics_backend.hpp modules/physics_box3d/box3d_backend.cpp tests/unit/physics/physics_contacts_test.cpp
git commit -m "feat(physics): manifold detail (point/normal/separation/impulse) on Begin contacts"
```

---

### Task 2: Propagate manifold detail to the entity-level `ContactEvent`

**Files:**
- Modify: `engine/physics/physics_system.hpp` (`ContactEvent` fields + emit passthrough)
- Modify: `tests/unit/physics/physics_contacts_test.cpp` (ECS test assertion)

**Interfaces:**
- Consumes: `BodyContact` (Task 1) with `point`/`normal`/`separation`/`normal_impulse`.
- Produces: `ContactEvent` gains the same four fields; `PhysicsSystem` fills them from the `BodyContact`.

- [ ] **Step 1: Extend the ECS test's Begin assertion in `tests/unit/physics/physics_contacts_test.cpp`**

In the SECOND test ("PhysicsSystem emits a ContactEvent when a box lands on the ground"), change the begin-detection loop to capture the event and assert its normal. Replace:
```cpp
    bool begin_seen = false;
    for (const auto& c : events)
    {
        if (c.phase == ContactPhase::Begin && is_entity_pair(c, box, ground))
        {
            begin_seen = true;
        }
    }
    REQUIRE(begin_seen);
```
with:
```cpp
    bool begin_seen = false;
    ContactEvent begin_evt{};
    for (const auto& c : events)
    {
        if (c.phase == ContactPhase::Begin && is_entity_pair(c, box, ground))
        {
            begin_seen = true;
            begin_evt = c;
        }
    }
    REQUIRE(begin_seen);
    // The manifold normal propagated to the entity-level event (vertical for a box on the ground).
    REQUIRE(std::abs(begin_evt.normal.y) > 0.9f);
```
(`<cmath>` was added to this file in Task 1.)

- [ ] **Step 2: Build → confirm it fails**
```bash
cmake --build build/debug -j 2>&1 | tail -5
```
Expected: compile error — `ContactEvent` has no member `normal`.

- [ ] **Step 3: Extend `ContactEvent` + the emit in `engine/physics/physics_system.hpp`**

The struct currently is:
```cpp
struct ContactEvent
{
    ContactPhase                 phase;
    mosaic::foundation::EntityID a;
    mosaic::foundation::EntityID b;
};
```
Replace with:
```cpp
struct ContactEvent
{
    ContactPhase                 phase;
    mosaic::foundation::EntityID a;
    mosaic::foundation::EntityID b;
    mosaic::foundation::math::Vec3 point{};   // world contact point (Begin only; zero for End)
    mosaic::foundation::math::Vec3 normal{};  // world contact normal
    float                          separation = 0.0f;
    float                          normal_impulse = 0.0f;
};
```
Then change the emit (in the contact-events loop) from:
```cpp
            world.events().emit<ContactEvent>(ContactEvent{c.phase, ia->second, ib->second});
```
to:
```cpp
            world.events().emit<ContactEvent>(ContactEvent{c.phase, ia->second, ib->second, c.point,
                                                           c.normal, c.separation, c.normal_impulse});
```
(`Vec3` is in scope in this header — it includes the transform/math headers via `physics_backend.hpp`/`world.hpp`. If the build reports `Vec3` is undeclared, add `#include "foundation/math/vec3.hpp"`.)

- [ ] **Step 4: Build + test**
```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "PhysicsSystem emits a ContactEvent" --output-on-failure
```
Expected: PASS. Then the full suite once (`ctest --test-dir build/debug 2>&1 | tail -3`) — 0 failures.

- [ ] **Step 5: Commit**
```bash
git add engine/physics/physics_system.hpp tests/unit/physics/physics_contacts_test.cpp
git commit -m "feat(physics): ContactEvent carries manifold detail (point/normal/separation/impulse)"
```

---

## Notes for the executor

- **`docs/project/next-steps.md`** — after Task 2, mark the physics **contact manifolds** follow-up done (Begin contacts now carry point/normal/separation/impulse through `ContactEvent`); the deferred **hit-event** channel + multi-point/friction impulses remain. Update the running test count (confirm the actual `ctest` total). Commit on this branch (rides in the PR).
- **Finishing:** use `superpowers:finishing-a-development-branch`; base branch is `main`.
- All changes are headless; no `--window`/GUI step. Determinism is already covered by P5's sequence test — these tests assert the *shape* of the manifold data (vertical unit normal, small separation), not exact values, to stay robust across Box3D updates.
