# Physics P4 — Raycasts / Queries (design)

**Decomposition:** [physics-decomposition](2026-08-24-physics-decomposition.md) (slice P4).
**Builds on:** P1 (`PhysicsBackend` + Box3D), P2 (`PhysicsSystem`), P3 (shapes).
**Design intent:** [design/physics.md](../../design/physics.md) — deterministic queries as a
public guarantee.
**Date:** 2026-08-24.

## Summary

Add a **closest-hit raycast** to the physics system: a neutral `PhysicsBackend::raycast`
returning a `RaycastResult`, implemented via Box3D's `b3World_CastRayClosest`, plus an
ECS-level `PhysicsSystem::raycast` that returns the hit **entity**. This is a pure engine
query capability — headless and deterministic, no visuals (viewport mouse-picking is a
separate, deferred editor feature).

## Scope

**In:**
- Neutral `RaycastResult` + `PhysicsBackend::raycast(origin, direction, max_distance)`.
- Box3D backend implementation (`b3World_CastRayClosest`; body handle stashed in body
  userData for hit → handle mapping).
- ECS-level `RaycastHit` + `PhysicsSystem::raycast` mapping the hit body to an `EntityID`.
- Headless deterministic tests (hit / miss / nearest-of-two, backend + ECS).

**Deferred (later / YAGNI):** all-hits / multi-hit queries, shape casts, overlap tests,
category/mask query filtering, and viewport mouse-picking (an editor feature).

## Architecture

### Neutral backend API (`engine/physics/physics_backend.hpp`)

```cpp
struct RaycastResult
{
    bool                           hit = false;
    BodyHandle                     body = 0;   // the body hit (0 when !hit)
    mosaic::foundation::math::Vec3 point{};     // world hit point
    mosaic::foundation::math::Vec3 normal{};    // world surface normal at the hit
    float                          distance = 0.0f; // distance from origin along the ray
};

// on class PhysicsBackend:
virtual RaycastResult raycast(const mosaic::foundation::math::Vec3& origin,
                              const mosaic::foundation::math::Vec3& direction,
                              float max_distance) const = 0;
```

`direction` is normalized inside the backend; the ray segment is `[origin, origin +
normalize(direction) * max_distance]`. **Pure virtual** — raycast is a real backend
capability. The one existing test double (`FakeBackend` in `physics_system_test.cpp`) gains a
one-line stub (`return RaycastResult{};`).

`RaycastResult` on a miss has `hit = false`; all other fields are unspecified (documented as
"valid only when `hit`").

### Box3D backend (`modules/physics_box3d/box3d_backend.cpp`)

- **Body-handle mapping via userData.** After `b3CreateBody`, stash the `BodyHandle` in the
  body's userData: `b3Body_SetUserData(id, reinterpret_cast<void*>(static_cast<std::uintptr_t>(handle)))`.
  (Set after the handle is known — `handle == bodies_.size()` post-`push_back`.)
- **`raycast`:**
  ```
  Vec3 d = normalize(direction);
  b3Pos   origin_b { origin.x, origin.y, origin.z };
  b3Vec3  translation { d.x * max_distance, d.y * max_distance, d.z * max_distance };
  b3RayResult r = b3World_CastRayClosest(world_, origin_b, translation, b3DefaultQueryFilter());
  ```
  Map: `out.hit = r.hit`; when hit — `out.point = {r.point.x,y,z}`, `out.normal =
  {r.normal.x,y,z}`, `out.distance = r.fraction * max_distance`, and
  `out.body = static_cast<BodyHandle>(reinterpret_cast<std::uintptr_t>(b3Body_GetUserData(b3Shape_GetBody(r.shapeId))))`.
  A zero-length or zero `max_distance` ray returns a miss (guard `d` normalization / `max_distance <= 0`).

Verified in the pinned headers: `b3RayResult b3World_CastRayClosest(b3WorldId, b3Pos, b3Vec3,
b3QueryFilter)`; `b3RayResult { b3ShapeId shapeId; b3Pos point; b3Vec3 normal; float fraction;
bool hit; ... }`; `b3QueryFilter b3DefaultQueryFilter(void)`; `b3BodyId
b3Shape_GetBody(b3ShapeId)`; `void b3Body_SetUserData(b3BodyId, void*)` / `void*
b3Body_GetUserData(b3BodyId)`.

### ECS surface (`engine/physics/physics_system.hpp`)

Game/engine code queries in **entities**, not body handles:

```cpp
struct RaycastHit
{
    bool                           hit = false;
    mosaic::foundation::EntityID   entity{}; // the entity hit (null when !hit)
    mosaic::foundation::math::Vec3 point{};
    mosaic::foundation::math::Vec3 normal{};
    float                          distance = 0.0f;
};

RaycastHit PhysicsSystem::raycast(const Vec3& origin, const Vec3& direction, float max_distance) const;
```

It calls `backend_->raycast(...)` and maps the returned `BodyHandle` → `EntityID` via a
**reverse map** (`std::unordered_map<BodyHandle, EntityID> handle_to_entity_`) maintained
alongside the existing `bodies_` (populated in the body-creation path). On a miss (or an
unmapped handle) it returns `{hit = false}`.

Two layered result types (backend `RaycastResult` with a `BodyHandle`; system `RaycastHit`
with an `EntityID`) are natural for the layer boundary — the backend cannot know `EntityID`s.

## Testability

Headless + deterministic — real assertions (the physics payoff).

- **Backend hit** (`tests/unit/physics/physics_raycast_test.cpp`): a static ground box (top
  y=0.5) at the origin; a ray from `(0, 5, 0)` pointing `(0, -1, 0)`, `max_distance = 10`;
  assert `hit`, `distance ≈ 4.5` (5 − 0.5), `point.y ≈ 0.5`, `normal.y > 0` (upward face),
  and `body` equals the ground's handle.
- **Backend miss:** a ray from `(100, 5, 0)` down (nothing there) → `hit == false`.
- **Backend nearest-of-two:** two **static** boxes at fixed heights (e.g. centers y=1 and
  y=3, half-extent 0.5); a downward ray from `(0,5,0)` returns the **upper** body (nearest) —
  `distance ≈ 1.5` (to the top box at y=3, top surface 3.5), not the lower one. Static +
  fixed positions keep the assertion exact (no settling).
- **ECS hit:** `PhysicsSystem::raycast` down onto a known entity returns `hit` with the
  correct `entity`; a miss returns `hit == false`.
- **Determinism:** an identical raycast on an identical world yields a bit-identical
  `distance` across two runs.

No demo / editor change (raycast is a headless query).

## Ground rules honored

- **Tier contract:** `RaycastResult`/`raycast` stay neutral in `engine/physics/`; `b3RayResult`
  and `b3World_CastRayClosest` stay in `modules/physics_box3d`. `b3*` never leaves the module.
- **Determinism:** Box3D queries are deterministic; closest-hit is well-defined (nearest),
  satisfying the "deterministic queries" guarantee for the single-hit case.
- No new third-party deps. Catch2 test names avoid a leading `--`.

## Acceptance

- [ ] Neutral `RaycastResult` + `PhysicsBackend::raycast`; the test fake stubs it.
- [ ] Box3D backend raycast (userData handle mapping) returns hit point/normal/distance/body.
- [ ] `PhysicsSystem::raycast` returns the hit `EntityID`.
- [ ] Backend + ECS hit/miss/nearest/determinism headless tests green.
- [ ] Full unit suite green.

## Depends on / relates

- **Depends on:** P1 (`PhysicsBackend`, `BodyHandle`), P2 (`PhysicsSystem`, `bodies_` map),
  P3 (shapes to hit). **Blocks:** none; P5 (contacts) is independent.
- **Relates:** viewport mouse-picking (deferred editor feature) will consume
  `PhysicsSystem::raycast`; scripting will expose it through the reflected API.
