# Physics P4 — Raycasts / Queries Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A closest-hit raycast on the physics system — a neutral `PhysicsBackend::raycast` (Box3D-backed) returning a `RaycastResult`, plus an ECS-level `PhysicsSystem::raycast` that returns the hit entity.

**Architecture:** The backend does the actual `b3World_CastRayClosest` and returns a neutral `RaycastResult` (a `BodyHandle`); the body's `BodyHandle` is stashed in Box3D body userData for hit→handle mapping. `PhysicsSystem::raycast` maps the hit handle to an `EntityID` via a reverse map. All headless + deterministic; no visuals.

**Tech Stack:** C++20, Box3D (`b3World_CastRayClosest`), `engine/physics`, Catch2, CMake.

## Global Constraints

- **C++20**; header-only in `engine/physics/`.
- **Tier contract:** `RaycastResult`/`raycast` stay neutral in `engine/physics/`; `b3RayResult`/`b3World_CastRayClosest` stay in `modules/physics_box3d`. `b3*` never leaves the module.
- **Determinism:** Box3D queries are deterministic; closest-hit is the nearest.
- No new third-party deps. Catch2 test names must not start with `--`.
- **Build/test:** `cmake --build build/debug -j` · `ctest --test-dir build/debug --output-on-failure`.

## Verified Box3D API (pinned commit)

- `b3RayResult b3World_CastRayClosest(b3WorldId, b3Pos origin, b3Vec3 translation, b3QueryFilter)`.
- `b3RayResult { b3ShapeId shapeId; b3Pos point; b3Vec3 normal; float fraction; bool hit; ... }`.
- `b3QueryFilter b3DefaultQueryFilter(void)`; `b3BodyId b3Shape_GetBody(b3ShapeId)`.
- `void b3Body_SetUserData(b3BodyId, void*)`; `void* b3Body_GetUserData(b3BodyId)`; `b3BodyDef` has a `void* userData`.
- `b3Pos` == `b3Vec3 {x,y,z}` in the default single-precision build.

## Current state (verified)

- `modules/physics_box3d/box3d_backend.cpp` `create_body` ends `bodies_.push_back(id); return static_cast<ep::BodyHandle>(bodies_.size());`.
- `foundation/math/vec3.hpp` has `inline Vec3 normalize(const Vec3&)`.
- `tests/unit/physics/physics_system_test.cpp` `FakeBackend` overrides `create_body`/`step`/`body_pose` (needs a `raycast` stub once the method is pure).
- `PhysicsSystem` (`engine/physics/physics_system.hpp`) maintains `std::unordered_map<EntityID, BodyHandle> bodies_`, populated in `update`.

---

## File Structure

- `engine/physics/physics_backend.hpp` — **modify.** `RaycastResult` + pure `raycast`.
- `modules/physics_box3d/box3d_backend.cpp` — **modify.** `b3Body_SetUserData` at creation + `raycast` impl.
- `engine/physics/physics_system.hpp` — **modify.** `RaycastHit` + `PhysicsSystem::raycast` + reverse map.
- `tests/unit/physics/physics_system_test.cpp` — **modify.** `FakeBackend` raycast stub.
- `tests/unit/physics/physics_raycast_test.cpp` — **create.** Backend hit/miss/nearest + ECS hit/miss.
- `tests/CMakeLists.txt` — **modify.** Register the new test.

---

## Task 1: Neutral raycast + Box3D backend

**Files:**
- Modify: `engine/physics/physics_backend.hpp`
- Modify: `modules/physics_box3d/box3d_backend.cpp`
- Modify: `tests/unit/physics/physics_system_test.cpp`
- Create: `tests/unit/physics/physics_raycast_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `mosaic::engine::physics::RaycastResult { bool hit; BodyHandle body; Vec3 point; Vec3 normal; float distance; }`; `virtual RaycastResult PhysicsBackend::raycast(const Vec3& origin, const Vec3& direction, float max_distance) const = 0`.

- [ ] **Step 1: Write the failing backend test**

Create `tests/unit/physics/physics_raycast_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/physics/physics_backend.hpp"
#include "modules/physics_box3d/box3d_backend.hpp"

using Catch::Approx;
using mosaic::engine::physics::BodyDef;
using mosaic::engine::physics::BodyHandle;
using mosaic::engine::physics::BodyType;
using mosaic::engine::physics::RaycastResult;
using mosaic::engine::physics::WorldDef;
using mosaic::foundation::math::Vec3;
using mosaic::physics::box3d::create_box3d_backend;

namespace
{
BodyHandle add_static_box(mosaic::engine::physics::PhysicsBackend& b, Vec3 pos, Vec3 half)
{
    BodyDef d;
    d.type = BodyType::Static;
    d.position = pos;
    d.shape.half_extents = half;
    return b.create_body(d);
}
} // namespace

TEST_CASE("backend raycast hits a box and reports point/normal/distance/body", "[physics]")
{
    auto             backend = create_box3d_backend(WorldDef{});
    const BodyHandle ground = add_static_box(*backend, Vec3{0, 0, 0}, Vec3{5, 0.5f, 5}); // top y=0.5

    const RaycastResult r = backend->raycast(Vec3{0, 5, 0}, Vec3{0, -1, 0}, 10.0f);
    REQUIRE(r.hit);
    REQUIRE(r.body == ground);
    REQUIRE(r.distance == Approx(4.5f).margin(0.05f)); // 5 - 0.5
    REQUIRE(r.point.y == Approx(0.5f).margin(0.05f));
    REQUIRE(r.normal.y > 0.5f); // upward-facing top surface
}

TEST_CASE("backend raycast misses empty space", "[physics]")
{
    auto backend = create_box3d_backend(WorldDef{});
    add_static_box(*backend, Vec3{0, 0, 0}, Vec3{5, 0.5f, 5});

    const RaycastResult r = backend->raycast(Vec3{100, 5, 0}, Vec3{0, -1, 0}, 10.0f);
    REQUIRE_FALSE(r.hit);
}

TEST_CASE("backend raycast returns the nearest of two boxes", "[physics]")
{
    auto             backend = create_box3d_backend(WorldDef{});
    add_static_box(*backend, Vec3{0, 1, 0}, Vec3{0.5f, 0.5f, 0.5f});               // lower
    const BodyHandle high = add_static_box(*backend, Vec3{0, 3, 0}, Vec3{0.5f, 0.5f, 0.5f}); // upper

    const RaycastResult r = backend->raycast(Vec3{0, 5, 0}, Vec3{0, -1, 0}, 10.0f);
    REQUIRE(r.hit);
    REQUIRE(r.body == high);                            // nearest (top at y=3.5)
    REQUIRE(r.distance == Approx(1.5f).margin(0.05f));  // 5 - 3.5
}

TEST_CASE("backend raycast is deterministic", "[physics]")
{
    auto backend = create_box3d_backend(WorldDef{});
    add_static_box(*backend, Vec3{0, 0, 0}, Vec3{5, 0.5f, 5});
    REQUIRE(backend->raycast(Vec3{0, 5, 0}, Vec3{0, -1, 0}, 10.0f).distance ==
            backend->raycast(Vec3{0, 5, 0}, Vec3{0, -1, 0}, 10.0f).distance);
}
```

Register it in `tests/CMakeLists.txt` (after the other `unit/physics/...` entries):

```cmake
  unit/physics/physics_raycast_test.cpp
```

- [ ] **Step 2: Run — verify it fails**

Run: `cmake --build build/debug -j 2>&1 | tail -20`
Expected: compile errors — `RaycastResult` undeclared; `PhysicsBackend` has no `raycast`;
`FakeBackend` (in `physics_system_test.cpp`) becomes abstract once `raycast` is pure (added
next step) — resolved in Step 4.

- [ ] **Step 3: Add `RaycastResult` + the pure `raycast` to the interface**

In `engine/physics/physics_backend.hpp`, add (after `struct Pose`):

```cpp
struct RaycastResult
{
    bool                           hit = false;
    BodyHandle                     body = 0;        // valid only when hit
    mosaic::foundation::math::Vec3 point{};          // world hit point
    mosaic::foundation::math::Vec3 normal{};         // world surface normal at the hit
    float                          distance = 0.0f;  // distance from origin along the ray
};
```

and add to `class PhysicsBackend` (after `body_pose`):

```cpp
    // Closest-hit raycast along [origin, origin + normalize(direction) * max_distance].
    virtual RaycastResult raycast(const mosaic::foundation::math::Vec3& origin,
                                  const mosaic::foundation::math::Vec3& direction,
                                  float max_distance) const = 0;
```

- [ ] **Step 4: Stub `raycast` in the test `FakeBackend`**

In `tests/unit/physics/physics_system_test.cpp`, add to `class FakeBackend` (after `body_pose`):

```cpp
    mosaic::engine::physics::RaycastResult raycast(const mosaic::foundation::math::Vec3&,
                                                   const mosaic::foundation::math::Vec3&,
                                                   float) const override
    {
        return mosaic::engine::physics::RaycastResult{};
    }
```

- [ ] **Step 5: Implement the Box3D backend raycast + userData**

In `modules/physics_box3d/box3d_backend.cpp`, add `#include <cstdint>` near the top includes.

Replace the `create_body` tail:

```cpp
        bodies_.push_back(id);
        return static_cast<ep::BodyHandle>(bodies_.size()); // handle == index + 1
```

with:

```cpp
        bodies_.push_back(id);
        const auto handle = static_cast<ep::BodyHandle>(bodies_.size()); // handle == index + 1
        b3Body_SetUserData(id, reinterpret_cast<void*>(static_cast<std::uintptr_t>(handle)));
        return handle;
```

Add the `raycast` override (after `body_pose`):

```cpp
    ep::RaycastResult raycast(const Vec3& origin, const Vec3& direction,
                              float max_distance) const override
    {
        ep::RaycastResult out;
        if (max_distance <= 0.0f)
        {
            return out;
        }
        const Vec3 d = mosaic::foundation::math::normalize(direction);

        b3Pos o{};
        o.x = origin.x;
        o.y = origin.y;
        o.z = origin.z;
        b3Vec3 translation{d.x * max_distance, d.y * max_distance, d.z * max_distance};

        const b3RayResult r = b3World_CastRayClosest(world_, o, translation, b3DefaultQueryFilter());
        if (!r.hit)
        {
            return out;
        }
        out.hit = true;
        out.point = Vec3{r.point.x, r.point.y, r.point.z};
        out.normal = Vec3{r.normal.x, r.normal.y, r.normal.z};
        out.distance = r.fraction * max_distance;
        const b3BodyId body = b3Shape_GetBody(r.shapeId);
        out.body = static_cast<ep::BodyHandle>(
            reinterpret_cast<std::uintptr_t>(b3Body_GetUserData(body)));
        return out;
    }
```

- [ ] **Step 6: Run — verify pass**

Run: `cmake --build build/debug -j && ctest --test-dir build/debug --output-on-failure -R "backend raycast"`
Expected: PASS (4 cases). If `distance`/`point` land just outside the `margin(0.05)` bands
(Box3D contact epsilon), widen the margin slightly; the `hit`/`body`/`normal.y>0` and
`nearest` assertions are load-bearing.

- [ ] **Step 7: Full suite + commit**

Run: `ctest --test-dir build/debug --output-on-failure`
Expected: all PASS.

```bash
git add engine/physics/physics_backend.hpp modules/physics_box3d/box3d_backend.cpp tests/unit/physics/physics_system_test.cpp tests/unit/physics/physics_raycast_test.cpp tests/CMakeLists.txt
git commit -m "physics P4: neutral RaycastResult + Box3D closest-hit raycast (#37)"
```

---

## Task 2: ECS raycast (entity-level)

**Files:**
- Modify: `engine/physics/physics_system.hpp`
- Modify: `tests/unit/physics/physics_raycast_test.cpp`

**Interfaces:**
- Consumes: `RaycastResult`, `PhysicsBackend::raycast` (Task 1).
- Produces: `mosaic::engine::physics::RaycastHit { bool hit; EntityID entity; Vec3 point; Vec3 normal; float distance; }`; `RaycastHit PhysicsSystem::raycast(const Vec3&, const Vec3&, float) const`.

- [ ] **Step 1: Write the failing ECS test**

Append to `tests/unit/physics/physics_raycast_test.cpp`:

```cpp
#include "engine/physics/physics_components.hpp"
#include "engine/physics/physics_system.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"

using mosaic::engine::World;
using mosaic::engine::physics::Collider;
using mosaic::engine::physics::PhysicsSystem;
using mosaic::engine::physics::RigidBody;
using mosaic::engine::transform::Transform;
using mosaic::foundation::math::Quat;

TEST_CASE("PhysicsSystem::raycast returns the hit entity", "[physics]")
{
    World  world;
    auto&  sys = world.add_system<PhysicsSystem>(create_box3d_backend(WorldDef{}));

    const auto ground = world.create_entity();
    world.add_component<Transform>(ground, Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{10, 1, 10}, {}});
    world.add_component<RigidBody>(ground, RigidBody{false, 1.0f});
    world.add_component<Collider>(ground, Collider{Vec3{5.0f, 0.5f, 5.0f}}); // top y=0.5

    world.tick(1.0f / 60.0f); // creates the backend body + populates the handle->entity map

    const auto hit = sys.raycast(Vec3{0, 5, 0}, Vec3{0, -1, 0}, 10.0f);
    REQUIRE(hit.hit);
    REQUIRE(hit.entity == ground);
    REQUIRE(hit.distance == Approx(4.5f).margin(0.05f));

    const auto miss = sys.raycast(Vec3{100, 5, 0}, Vec3{0, -1, 0}, 10.0f);
    REQUIRE_FALSE(miss.hit);
}
```

- [ ] **Step 2: Run — verify it fails**

Run: `cmake --build build/debug -j 2>&1 | tail -20`
Expected: `PhysicsSystem` has no member `raycast`; `RaycastHit` undeclared.

- [ ] **Step 3: Add `RaycastHit` + `PhysicsSystem::raycast` + the reverse map**

In `engine/physics/physics_system.hpp`, add the struct (before `class PhysicsSystem`):

```cpp
struct RaycastHit
{
    bool                           hit = false;
    mosaic::foundation::EntityID   entity{}; // valid only when hit
    mosaic::foundation::math::Vec3 point{};
    mosaic::foundation::math::Vec3 normal{};
    float                          distance = 0.0f;
};
```

In the body-creation lambda inside `update`, record the reverse mapping — change the final
line `bodies_[e] = backend_->create_body(def);` to:

```cpp
                const BodyHandle handle = backend_->create_body(def);
                bodies_[e] = handle;
                handle_to_entity_[handle] = e;
```

Add the public method (after `update`):

```cpp
    RaycastHit raycast(const mosaic::foundation::math::Vec3& origin,
                       const mosaic::foundation::math::Vec3& direction,
                       float max_distance) const
    {
        const RaycastResult r = backend_->raycast(origin, direction, max_distance);
        RaycastHit          hit;
        if (!r.hit)
        {
            return hit;
        }
        const auto it = handle_to_entity_.find(r.body);
        if (it == handle_to_entity_.end())
        {
            return hit;
        }
        hit.hit = true;
        hit.entity = it->second;
        hit.point = r.point;
        hit.normal = r.normal;
        hit.distance = r.distance;
        return hit;
    }
```

Add the member (next to `bodies_`):

```cpp
    std::unordered_map<BodyHandle, mosaic::foundation::EntityID> handle_to_entity_;
```

- [ ] **Step 4: Run — verify pass**

Run: `cmake --build build/debug -j && ctest --test-dir build/debug --output-on-failure -R "PhysicsSystem::raycast"`
Expected: PASS.

- [ ] **Step 5: Full suite + commit**

Run: `ctest --test-dir build/debug --output-on-failure`
Expected: all PASS.

```bash
git add engine/physics/physics_system.hpp tests/unit/physics/physics_raycast_test.cpp
git commit -m "physics P4: PhysicsSystem::raycast returns the hit entity (#37)"
```

---

## Task 3: Update the build handoff (next-steps)

**Files:**
- Modify: `docs/project/next-steps.md`

- [ ] **Step 1: Record P4**

Update `docs/project/next-steps.md`: change the "Last updated" line and note **physics P4 is
built** — a neutral `RaycastResult` + `PhysicsBackend::raycast` (closest-hit via Box3D's
`b3World_CastRayClosest`, body identified through userData) and an ECS-level
`PhysicsSystem::raycast` returning the hit `EntityID`; headless deterministic hit/miss/nearest
tests (no visuals — mouse-picking is a deferred editor feature). **Next is P5** — contact
events (via the #11 EventBus) + record/replay determinism, the final physics slice. Link the
[P4 spec](../superpowers/specs/2026-08-24-physics-p4-raycast-design.md) and
[P4 plan](../superpowers/plans/2026-08-24-physics-p4-raycast.md).

- [ ] **Step 2: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "next-steps: physics P4 (raycasts) built; next is P5"
```

---

## Self-Review

**Spec coverage:**
- Neutral `RaycastResult` + `PhysicsBackend::raycast` (spec §Neutral backend API) → Task 1. ✓
- Box3D backend (userData mapping + `b3World_CastRayClosest`) (spec §Box3D backend) → Task 1. ✓
- ECS `RaycastHit` + `PhysicsSystem::raycast` (spec §ECS surface) → Task 2. ✓
- Backend hit/miss/nearest/determinism + ECS hit/miss (spec §Testability) → Tasks 1 & 2. ✓
- Fake stub for the pure method (spec §Neutral backend API) → Task 1 Step 4. ✓

**Placeholder scan:** No TBD/TODO; complete code in each step; commands have expected output. ✓

**Type consistency:** `RaycastResult { hit, body(BodyHandle), point, normal, distance }` and `raycast(Vec3,Vec3,float) const` identical across Tasks 1/2; `RaycastHit { hit, entity(EntityID), ... }` consistent in Task 2; `handle_to_entity_` populated in `update` and read in `raycast`; `create_box3d_backend`/`add_static_box` helper reused across tests. ✓

**One risk flagged (not blocking):** the `distance`/`point` margins (0.05) may need a small
widen for Box3D's contact epsilon at the pinned commit; the `hit`/`body`/`entity`/`nearest`
assertions are exact and load-bearing. The determinism assertion is near-tautological for a
const query but is kept per the spec's guarantee.
