# Physics Body Destruction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make destroying an entity destroy its physics body, so a destroyed object stops colliding instead of remaining as an invisible obstacle.

**Architecture:** `PhysicsBackend` gains a pure-virtual `destroy_body`. The Box3D backend tombstones the destroyed slot with `b3_nullBodyId` (handles are vector indices, so erasing would shift them) and routes all four handle-taking methods through one `live_body()` guard. `PhysicsSystem::update()` gains a reap pass at the top, inside the existing lock, that destroys the body of any entry in `bodies_` whose `RigidBody` component has vanished.

**Tech Stack:** C++20, Box3D (`b3DestroyBody`, `b3Body_IsValid`, `b3_nullBodyId`), Catch2, CMake presets.

**Spec:** `docs/superpowers/specs/2026-10-02-physics-body-destruction-design.md`

## Global Constraints

- **`destroy_body` is a pure virtual**, not a defaulted no-op. All three implementations must be updated: `modules/physics_box3d/box3d_backend.cpp`, and the two test fakes in `tests/unit/physics/physics_system_test.cpp` (`FakeBackend`) and `tests/unit/physics/physics_concurrency_test.cpp` (`OverlapDetectingBackend`).
- **Never erase from the Box3D backend's `bodies_` vector.** `handle == index + 1`, so erasing shifts and invalidates every later handle. A destroyed slot is tombstoned with `b3_nullBodyId`.
- **`b3Body_IsValid` is the liveness guard.** Its source returns false when `index1 < 1`, so it is safe on `b3_nullBodyId` and needs no special-casing. Do not reach into `b3BodyId`'s fields.
- **The reap predicate is `world.get_component<RigidBody>(e) == nullptr`**, not `world.is_valid(e)`. The component check also covers removing `RigidBody` from a living entity, which `is_valid` would miss.
- **The reap goes at the top of `update()`, inside the existing `lock_guard` block**, before step 1. It touches `backend_` and all three maps.
- `destroy_body` on an unknown or already-destroyed handle is a **silent no-op**, matching how the other handle-taking methods already degrade.
- Do not change `create_body`, `step`, `contact_events`, or `raycast`'s signatures or behaviour. Do not change `ContactEvent`, `BodyDef`, or any other value type.
- Do not widen or move `PhysicsSystem`'s lock. The emit still happens outside the braced block (#211) and the reap still happens inside it.
- Build: `cmake --build --preset debug -j 20`. Test binary: `./build/debug/tests/mosaic_unit_tests`. Gate: `ctest --preset debug --output-on-failure` — **no `-E` exclusions**; they were removed on 2026-09-30 and must not come back.
- **Run only one build at a time** — concurrent `cmake --build` in this tree corrupts archives.
- Never `git add -A`: `docs/project/advancement-plans-and-evaluation.md` is modified and must never be staged. Stage explicit paths only.
- **Never `git commit --amend` without first checking `git log -1`** — a commit rejected by the pre-commit hook leaves HEAD on the previous commit, and amending then rewrites that one.
- The pre-commit hook regenerates `docs/index.html` and `docs/reference/*.md`. If it rejects a commit saying generated docs were stale, `git add` the regenerated files it names and commit again.
- Catch2 `TEST_CASE` names: no commas inside the name, no leading `--`, no unmatched `[`.

## Review Focus

Failure modes the spec implies that no task's tests would otherwise exercise. Each gets a test in the task that owns the code.

1. **Destroying the same handle twice must be a safe no-op.** The reap erases its map entries so it cannot fire twice for one entity, but a caller holding a stale handle can, and `b3DestroyBody` on a freed id is undefined. The tombstone plus `live_body` is what makes this safe, and nothing else would catch it. → Task 1.
2. **A `raycast` must never report a destroyed body.** The handle lives in Box3D's per-body user data, so a stale read would return a plausible-looking handle for a body that no longer exists — far worse than a miss. → Task 1.
3. **The erase-while-iterating must not drop or skip a surviving body.** `it = bodies_.erase(it)` is correct, but a plain `++it` after erase, or erasing inside a range-for, silently loses live bodies — and every other test here uses one body, so none would notice. → Task 2.
4. **A contact recorded in the previous step, naming a reaped handle, must be dropped rather than emitted with a garbage entity.** Step 5a's unmapped-handle drop covers it only because the reap erases `handle_to_entity_` first; reverse the order and a `ContactEvent` carries a destroyed entity. → Task 2.
5. **`has_body` and `apply_impulse` on a reaped entity's id must be inert.** A gameplay script can hold an entity id across a destroy; this is the path #207's bindings expose to user code. → Task 2.

---

### Task 1: `destroy_body` on the backend, with tombstoning

**Files:**
- Modify: `engine/physics/physics_backend.hpp` (add the pure virtual)
- Modify: `modules/physics_box3d/box3d_backend.cpp` (`live_body`, `destroy_body`, and the three existing accessors)
- Modify: `tests/unit/physics/physics_system_test.cpp:34-70` (`FakeBackend` — add the override and record calls)
- Modify: `tests/unit/physics/physics_concurrency_test.cpp:66-90` (`OverlapDetectingBackend` — add the override)
- Create: `tests/unit/physics/physics_destroy_body_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces: `virtual void destroy_body(BodyHandle body) = 0;` on `PhysicsBackend`. `FakeBackend` gains a public `std::vector<BodyHandle> destroyed;` recording every call, which Task 2's tests assert against.

- [ ] **Step 1: Write the failing backend-level tests**

Create `tests/unit/physics/physics_destroy_body_test.cpp`:

```cpp
// Backend-level destruction. The gameplay consequence -- a destroyed entity stops
// blocking things -- is tested at the PhysicsSystem level in physics_system_test.cpp;
// these pin the backend contract the system relies on.

#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "engine/physics/physics_backend.hpp"
#include "modules/physics_box3d/box3d_backend.hpp"

using mosaic::engine::physics::BodyDef;
using mosaic::engine::physics::BodyHandle;
using mosaic::engine::physics::BodyType;
using mosaic::engine::physics::WorldDef;
using mosaic::foundation::math::Vec3;
using mosaic::physics::box3d::create_box3d_backend;

namespace
{
// A static floor spanning the origin, and a dynamic box resting above it.
BodyHandle make_ground(mosaic::engine::physics::PhysicsBackend& backend)
{
    BodyDef ground;
    ground.type = BodyType::Static;
    ground.position = Vec3{0, 0, 0};
    ground.shape.half_extents = Vec3{5, 0.5f, 5};
    return backend.create_body(ground);
}

BodyHandle make_box(mosaic::engine::physics::PhysicsBackend& backend, float y)
{
    BodyDef box;
    box.type = BodyType::Dynamic;
    box.position = Vec3{0, y, 0};
    box.shape.half_extents = Vec3{0.5f, 0.5f, 0.5f};
    return backend.create_body(box);
}
} // namespace

TEST_CASE("destroy_body makes the other handle-taking methods inert", "[physics]")
{
    auto backend = create_box3d_backend(WorldDef{});
    const BodyHandle box = make_box(*backend, 3.0f);

    backend->step(1.0f / 60.0f, 4);
    REQUIRE(backend->body_pose(box).position.y < 3.0f); // it was simulating

    backend->destroy_body(box);

    // A destroyed handle reads as the default pose rather than indexing a freed body.
    REQUIRE(backend->body_pose(box).position.y == 0.0f);
    // And the mutating calls do nothing instead of corrupting the world.
    REQUIRE_NOTHROW(backend->set_body_transform(box, Vec3{9, 9, 9}, mosaic::foundation::math::Quat{}));
    REQUIRE_NOTHROW(backend->apply_impulse(box, Vec3{100, 0, 0}, Vec3{0, 0, 0}));
    REQUIRE_NOTHROW(backend->step(1.0f / 60.0f, 4));
    REQUIRE(backend->body_pose(box).position.y == 0.0f);
}

TEST_CASE("destroy_body is a no-op for an unknown or already-destroyed handle", "[physics]")
{
    // Review Focus 1: the reap erases its map entries so it cannot fire twice for one
    // entity, but a caller holding a stale handle can -- and b3DestroyBody on a freed id
    // is undefined. The tombstone plus the liveness guard is what makes this safe, and
    // nothing else in the suite would catch its absence.
    auto backend = create_box3d_backend(WorldDef{});
    const BodyHandle box = make_box(*backend, 3.0f);

    REQUIRE_NOTHROW(backend->destroy_body(0));                 // the reserved invalid handle
    REQUIRE_NOTHROW(backend->destroy_body(box + 99));          // never issued
    REQUIRE_NOTHROW(backend->destroy_body(box));
    REQUIRE_NOTHROW(backend->destroy_body(box));               // again: must not double-free
    REQUIRE_NOTHROW(backend->step(1.0f / 60.0f, 4));
}

TEST_CASE("a destroyed body no longer stops a raycast", "[physics]")
{
    // Review Focus 2: the handle lives in Box3D's per-body user data, so a stale read
    // would hand back a plausible-looking handle for a body that no longer exists --
    // worse than a miss, because a caller would map it to an entity.
    auto backend = create_box3d_backend(WorldDef{});
    const BodyHandle ground = make_ground(*backend);
    const BodyHandle box = make_box(*backend, 2.0f);

    for (int i = 0; i < 120; ++i) { backend->step(1.0f / 60.0f, 4); } // box settles on ground

    const auto before = backend->raycast(Vec3{0, 10, 0}, Vec3{0, -1, 0}, 20.0f);
    REQUIRE(before.hit);
    REQUIRE(before.body == box); // the box is what the ray meets first

    backend->destroy_body(box);
    backend->step(1.0f / 60.0f, 4);

    const auto after = backend->raycast(Vec3{0, 10, 0}, Vec3{0, -1, 0}, 20.0f);
    REQUIRE(after.hit);
    REQUIRE(after.body == ground); // now the ground, never the destroyed box
}

TEST_CASE("destroying one body leaves the handles of other bodies valid", "[physics]")
{
    // Handles are vector indices (handle == index + 1), so erasing a slot would shift
    // every later handle. Tombstoning is what keeps `high` addressable after `low` dies.
    auto backend = create_box3d_backend(WorldDef{});
    const BodyHandle low = make_box(*backend, 2.0f);
    const BodyHandle high = make_box(*backend, 6.0f);
    REQUIRE(low != high);

    backend->step(1.0f / 60.0f, 4);
    const float high_y_before = backend->body_pose(high).position.y;

    backend->destroy_body(low);
    for (int i = 0; i < 30; ++i) { backend->step(1.0f / 60.0f, 4); }

    // `high` still resolves to its own body and is still falling.
    REQUIRE(backend->body_pose(high).position.y < high_y_before);
    REQUIRE(backend->body_pose(low).position.y == 0.0f); // and `low` is gone
}
```

Register it in `tests/CMakeLists.txt` immediately after the `unit/physics/physics_backend_test.cpp` line:

```
  unit/physics/physics_destroy_body_test.cpp
```

- [ ] **Step 2: Run the build to verify it fails**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20 2>&1 | grep -E "error" | head -5
```

Expected: a COMPILE error, not a test failure — something like `'class mosaic::engine::physics::PhysicsBackend' has no member named 'destroy_body'`. That is the red state for a new interface method. Record the exact message.

- [ ] **Step 3: Add the pure virtual to the interface**

In `engine/physics/physics_backend.hpp`, inside `class PhysicsBackend`, immediately after the `create_body` declaration:

```cpp
    // Destroy a body previously returned by create_body. The handle is invalid afterwards.
    // A no-op for an unknown or already-destroyed handle, matching how the other
    // handle-taking methods degrade. Required rather than defaulted: a no-op default would
    // silently leak a body that keeps simulating and keeps colliding -- an invisible
    // obstacle with no mesh to explain it.
    virtual void destroy_body(BodyHandle body) = 0;
```

- [ ] **Step 4: Make both test fakes implement it**

In `tests/unit/physics/physics_system_test.cpp`, inside `class FakeBackend`, add a recorder beside the existing `impulses` vector:

```cpp
    std::vector<BodyHandle> destroyed;
```

and the override, immediately after `create_body`:

```cpp
    void destroy_body(BodyHandle body) override { destroyed.push_back(body); }
```

In `tests/unit/physics/physics_concurrency_test.cpp`, inside `class OverlapDetectingBackend`, add the override immediately after `create_body`. It must enter the `Guard` like every other method, because the overlap detector's whole claim is that no two backend calls are ever in flight at once:

```cpp
    void destroy_body(BodyHandle) override { Guard g(this); }
```

- [ ] **Step 5: Implement it in the Box3D backend**

In `modules/physics_box3d/box3d_backend.cpp`, add a private helper beside the existing `handle_of`:

```cpp
    // The b3BodyId for a handle, or nullptr when the handle is out of range or names a
    // body that has been destroyed. Every handle-taking method goes through this. A
    // destroyed slot is tombstoned with b3_nullBodyId rather than erased, because
    // handle == index + 1 and erasing would shift every later handle.
    const b3BodyId* live_body(ep::BodyHandle body) const
    {
        if (body == 0 || body > bodies_.size())
        {
            return nullptr;
        }
        const b3BodyId& id = bodies_[body - 1];
        // b3Body_IsValid returns false when index1 < 1, so this is safe on b3_nullBodyId.
        return b3Body_IsValid(id) ? &id : nullptr;
    }
```

Add the public override, immediately after `create_body`:

```cpp
    void destroy_body(ep::BodyHandle body) override
    {
        const b3BodyId* slot = live_body(body);
        if (slot == nullptr)
        {
            return;
        }
        const b3BodyId id = *slot;         // copy before tombstoning the slot
        bodies_[body - 1] = b3_nullBodyId; // handles are indices; never erase
        b3DestroyBody(id);
    }
```

Then route the three existing accessors through the guard. Replace `set_body_transform`'s opening:

```cpp
        if (body == 0 || body > bodies_.size())
        {
            return;
        }
        const b3BodyId id = bodies_[body - 1];
```

with:

```cpp
        const b3BodyId* slot = live_body(body);
        if (slot == nullptr)
        {
            return;
        }
        const b3BodyId id = *slot;
```

Replace `apply_impulse`'s opening with exactly the same five lines (it has the identical range check today).

Replace `body_pose`'s opening:

```cpp
        if (body == 0 || body > bodies_.size())
        {
            return ep::Pose{};
        }
        const auto   p = b3Body_GetPosition(bodies_[body - 1]); // b3Pos == b3Vec3 {x,y,z}
        const b3Quat q = b3Body_GetRotation(bodies_[body - 1]); // b3Quat {b3Vec3 v; float s;}
```

with:

```cpp
        const b3BodyId* slot = live_body(body);
        if (slot == nullptr)
        {
            return ep::Pose{};
        }
        const auto   p = b3Body_GetPosition(*slot); // b3Pos == b3Vec3 {x,y,z}
        const b3Quat q = b3Body_GetRotation(*slot); // b3Quat {b3Vec3 v; float s;}
```

Leave `create_body`, `step`, `contact_events` and `raycast` alone.

- [ ] **Step 6: Run the tests to verify they pass**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
./build/debug/tests/mosaic_unit_tests "[physics]"
```

Expected: PASS, including the four new cases and every pre-existing `[physics]` case — notably #209's overlap detector, which now sees a seventh method through its `Guard`.

- [ ] **Step 7: Prove the tombstone is load-bearing**

Temporarily make `live_body` ignore liveness, so a destroyed slot still resolves:

```cpp
        return &id;   // MUTATION: was b3Body_IsValid(id) ? &id : nullptr;
```

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
./build/debug/tests/mosaic_unit_tests "[physics]" 2>&1 | tail -5
```

Expected: FAIL. The inert-methods case and the no-op-for-destroyed-handle case both depend on the guard; the second may crash rather than fail cleanly, since it double-frees. Either is the red state — record which happened.

Then restore the guard, rebuild, and confirm `[physics]` is green again.

- [ ] **Step 8: Commit**

```bash
git add engine/physics/physics_backend.hpp modules/physics_box3d/box3d_backend.cpp tests/unit/physics/physics_destroy_body_test.cpp tests/unit/physics/physics_system_test.cpp tests/unit/physics/physics_concurrency_test.cpp tests/CMakeLists.txt
git commit -m "feat(physics): let a backend destroy a body"
```

---

### Task 2: Reap bodies whose entity is gone

**Files:**
- Modify: `engine/physics/physics_system.hpp` (`update()`, the reap at the top of the locked block)
- Modify: `tests/unit/physics/physics_system_test.cpp` (append tests)

**Interfaces:**
- Consumes: `PhysicsBackend::destroy_body(BodyHandle)` and `FakeBackend::destroyed` from Task 1.
- Produces: nothing consumed by a later task.

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/physics/physics_system_test.cpp`. The file already includes `modules/physics_box3d/box3d_backend.hpp` and has `using` declarations for `World`, `Collider`, `PhysicsSystem`, `RigidBody`, `Transform`, `Quat`, `Vec3`, `BodyHandle` and `Approx` — reuse them, do not redeclare.

```cpp
TEST_CASE("PhysicsSystem destroys the body of a destroyed entity", "[physics]")
{
    World world;
    auto  backend = std::make_unique<FakeBackend>();
    auto* fake = backend.get();
    world.add_system<PhysicsSystem>(std::move(backend));

    const auto box = world.create_entity();
    world.add_component<Transform>(box, Transform{Vec3{0, 3, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(box, RigidBody{true, 1.0f});
    world.add_component<Collider>(box, Collider{Vec3{0.5f, 0.5f, 0.5f}});

    world.tick(1.0f / 60.0f);
    auto* physics = world.get_system<PhysicsSystem>();
    REQUIRE(physics != nullptr);
    REQUIRE(physics->has_body(box));
    REQUIRE(fake->destroyed.empty());

    world.destroy_entity(box); // deferred: commits at the END of this tick
    world.tick(1.0f / 60.0f);  // commits the destroy, reap has not run yet
    world.tick(1.0f / 60.0f);  // this tick's reap sees the vanished RigidBody

    // Exactly one: a double destroy is as much a bug as none, and the maps must stop it
    // firing again on later ticks.
    REQUIRE(fake->destroyed.size() == 1);
    world.tick(1.0f / 60.0f);
    REQUIRE(fake->destroyed.size() == 1);

    // Review Focus 5: a script can hold an entity id across a destroy. Both of these are
    // reachable from the Python bindings shipped in #207.
    REQUIRE_FALSE(physics->has_body(box));
    REQUIRE_NOTHROW(physics->apply_impulse(box, Vec3{10, 0, 0}, Vec3{0, 0, 0}));
}

TEST_CASE("PhysicsSystem reaps a body when RigidBody is removed from a living entity", "[physics]")
{
    // The reap predicate is "RigidBody is gone", not "the entity is invalid" -- which is
    // what makes this case work at all. world.is_valid() would still be true here.
    World world;
    auto  backend = std::make_unique<FakeBackend>();
    auto* fake = backend.get();
    world.add_system<PhysicsSystem>(std::move(backend));

    const auto box = world.create_entity();
    world.add_component<Transform>(box, Transform{Vec3{0, 3, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(box, RigidBody{true, 1.0f});
    world.add_component<Collider>(box, Collider{Vec3{0.5f, 0.5f, 0.5f}});
    world.tick(1.0f / 60.0f);

    auto* physics = world.get_system<PhysicsSystem>();
    REQUIRE(physics->has_body(box));

    REQUIRE(world.remove_component<RigidBody>(box));
    world.tick(1.0f / 60.0f);

    REQUIRE(world.is_valid(box)); // the entity is still alive -- only the component went
    REQUIRE(fake->destroyed.size() == 1);
    REQUIRE_FALSE(physics->has_body(box));
}

TEST_CASE("PhysicsSystem reaps every body after World clear", "[physics]")
{
    // clear() bypasses the deferred destroy queue and runs no systems, so the reap is the
    // only thing that can notice. Without it, a scene reload leaks every body in the
    // outgoing scene.
    World world;
    auto  backend = std::make_unique<FakeBackend>();
    auto* fake = backend.get();
    world.add_system<PhysicsSystem>(std::move(backend));

    for (int i = 0; i < 3; ++i)
    {
        const auto e = world.create_entity();
        world.add_component<Transform>(
            e, Transform{Vec3{static_cast<float>(i), 3, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
        world.add_component<RigidBody>(e, RigidBody{true, 1.0f});
        world.add_component<Collider>(e, Collider{Vec3{0.5f, 0.5f, 0.5f}});
    }
    world.tick(1.0f / 60.0f);
    REQUIRE(fake->created.size() == 3);

    world.clear();
    REQUIRE_NOTHROW(world.tick(1.0f / 60.0f));

    REQUIRE(fake->destroyed.size() == 3);
}

TEST_CASE("PhysicsSystem keeps surviving bodies when one is reaped", "[physics]")
{
    // Review Focus 3: `it = bodies_.erase(it)` is correct, but a plain ++it after erase --
    // or erasing inside a range-for -- silently loses live bodies. Every other case here
    // uses one body, so none of them would notice.
    World world;
    auto  backend = std::make_unique<FakeBackend>();
    auto* fake = backend.get();
    world.add_system<PhysicsSystem>(std::move(backend));

    std::vector<mosaic::foundation::EntityID> boxes;
    for (int i = 0; i < 5; ++i)
    {
        const auto e = world.create_entity();
        world.add_component<Transform>(
            e, Transform{Vec3{static_cast<float>(i), 3, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
        world.add_component<RigidBody>(e, RigidBody{true, 1.0f});
        world.add_component<Collider>(e, Collider{Vec3{0.5f, 0.5f, 0.5f}});
        boxes.push_back(e);
    }
    world.tick(1.0f / 60.0f);

    auto* physics = world.get_system<PhysicsSystem>();
    for (const auto e : boxes) { REQUIRE(physics->has_body(e)); }

    // Destroy one from the middle, so a broken iterator would strand a neighbour.
    world.destroy_entity(boxes[2]);
    world.tick(1.0f / 60.0f);
    world.tick(1.0f / 60.0f);

    REQUIRE(fake->destroyed.size() == 1);
    REQUIRE(fake->destroyed[0] != 0);
    for (std::size_t i = 0; i < boxes.size(); ++i)
    {
        if (i == 2) { REQUIRE_FALSE(physics->has_body(boxes[i])); }
        else { REQUIRE(physics->has_body(boxes[i])); }
    }
}

TEST_CASE("PhysicsSystem emits no ContactEvent naming a reaped entity", "[physics]")
{
    // Review Focus 4: step 5a drops contacts whose handle is unmapped, which covers this
    // ONLY because the reap erases handle_to_entity_ first. Reverse that order and a
    // ContactEvent carries an entity that no longer exists.
    World world;
    world.add_system<PhysicsSystem>(
        mosaic::physics::box3d::create_box3d_backend(mosaic::engine::physics::WorldDef{}));

    const auto ground = world.create_entity();
    world.add_component<Transform>(ground,
                                   Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{10, 1, 10}, {}});
    world.add_component<RigidBody>(ground, RigidBody{false, 1.0f});
    world.add_component<Collider>(ground, Collider{Vec3{5.0f, 0.5f, 5.0f}});

    const auto box = world.create_entity();
    world.add_component<Transform>(box, Transform{Vec3{0, 2, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(box, RigidBody{true, 1.0f});
    world.add_component<Collider>(box, Collider{Vec3{0.5f, 0.5f, 0.5f}});

    // Subscribe BEFORE settling, so the first phase proves the channel actually delivers
    // box-naming contacts. Without that, "no contact named the box" after the destroy would
    // be satisfied by a subscriber that never fires at all -- and whether Box3D emits an
    // end-touch event for a destroyed body is not something this test should assume.
    bool named_the_box = false;
    world.events().subscribe<mosaic::engine::physics::ContactEvent>(
        [&](const mosaic::engine::physics::ContactEvent& c)
        {
            if (c.a == box || c.b == box) { named_the_box = true; }
        });

    for (int i = 0; i < 120; ++i) { world.tick(1.0f / 60.0f); } // settle, generating contacts
    REQUIRE(named_the_box); // the channel works; now it must go quiet for the right reason

    named_the_box = false;
    world.destroy_entity(box);
    for (int i = 0; i < 10; ++i) { world.tick(1.0f / 60.0f); }

    REQUIRE_FALSE(named_the_box);
}

TEST_CASE("a destroyed entity stops blocking the things resting on it", "[physics]")
{
    // THE load-bearing case, and the only one that fails if destroy_body is never called
    // while the maps are still erased. Everything else above would pass with the body left
    // alive in the backend -- this is the user-visible bug: destroy a crate and it still
    // holds up whatever was stacked on it, with no mesh to explain why.
    World world;
    world.add_system<PhysicsSystem>(
        mosaic::physics::box3d::create_box3d_backend(mosaic::engine::physics::WorldDef{}));

    const auto ground = world.create_entity();
    world.add_component<Transform>(ground,
                                   Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{10, 1, 10}, {}});
    world.add_component<RigidBody>(ground, RigidBody{false, 1.0f});
    world.add_component<Collider>(ground, Collider{Vec3{5.0f, 0.5f, 5.0f}});

    const auto lower = world.create_entity();
    world.add_component<Transform>(lower, Transform{Vec3{0, 1, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(lower, RigidBody{true, 1.0f});
    world.add_component<Collider>(lower, Collider{Vec3{0.5f, 0.5f, 0.5f}});

    const auto upper = world.create_entity();
    world.add_component<Transform>(upper, Transform{Vec3{0, 2.1f, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(upper, RigidBody{true, 1.0f});
    world.add_component<Collider>(upper, Collider{Vec3{0.5f, 0.5f, 0.5f}});

    for (int i = 0; i < 180; ++i) { world.tick(1.0f / 60.0f); } // both settle into a stack

    const float stacked_y = world.get_component<Transform>(upper)->position.y;
    REQUIRE(stacked_y > 1.4f); // the upper box really is resting on the lower one

    world.destroy_entity(lower);
    for (int i = 0; i < 240; ++i) { world.tick(1.0f / 60.0f); }

    // With the lower body destroyed, the upper box falls to the ground: its collider's
    // half-extent is 0.5 and the ground's top face is at y = 0.5, so it rests near y = 1.0.
    const float fallen_y = world.get_component<Transform>(upper)->position.y;
    REQUIRE(fallen_y < stacked_y - 0.5f);
    REQUIRE(fallen_y == Approx(1.0f).margin(0.2f));
}
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
./build/debug/tests/mosaic_unit_tests "PhysicsSystem destroys the body of a destroyed entity"
./build/debug/tests/mosaic_unit_tests "a destroyed entity stops blocking the things resting on it"
```

Expected: both FAIL. The first on `fake->destroyed.size() == 1` with expansion `0 == 1`, because nothing calls `destroy_body` yet. The second on the fallen-position assertions, because the destroyed body is still holding the upper box up — that failure IS the bug, observed. Run the other four new cases too and record each outcome.

- [ ] **Step 3: Add the reap**

In `engine/physics/physics_system.hpp`, inside `update()`'s braced lock block, as the very first thing — before the `// 1. Lazily create ...` comment:

```cpp
            // 0. Reap bodies whose entity is gone. destroy_entity() is deferred to the end
            // of tick() and on_destroy() erases every component, so an entry whose
            // RigidBody has vanished belongs to a destroyed entity -- or to a living one
            // someone removed RigidBody from, which should also stop simulating. Either way
            // the body must go: left alone it keeps stepping and keeps colliding, an
            // invisible obstacle with no mesh to explain it. handle_to_entity_ is erased
            // here too, which is what lets step 5a drop a contact recorded for this body by
            // the previous step.
            for (auto it = bodies_.begin(); it != bodies_.end();)
            {
                if (world.get_component<RigidBody>(it->first) != nullptr)
                {
                    ++it;
                    continue;
                }
                backend_->destroy_body(it->second);
                handle_to_entity_.erase(it->second);
                synced_pose_.erase(it->first);
                it = bodies_.erase(it);
            }
```

Change nothing else. The lock, the emit's position outside the block, and steps 1 through 5b all stay exactly as they are.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
./build/debug/tests/mosaic_unit_tests "[physics]"
```

Expected: PASS, including all six new cases, Task 1's four, #209's overlap detector and #211's contact-delivery guards.

- [ ] **Step 5: Prove the gameplay test is the one that matters**

Delete only the backend call from the reap, keeping every map erasure:

```cpp
                // backend_->destroy_body(it->second);   // MUTATION
```

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
./build/debug/tests/mosaic_unit_tests "PhysicsSystem destroys the body of a destroyed entity" 2>&1 | tail -3
./build/debug/tests/mosaic_unit_tests "a destroyed entity stops blocking the things resting on it" 2>&1 | tail -3
./build/debug/tests/mosaic_unit_tests "PhysicsSystem reaps every body after World clear" 2>&1 | tail -3
```

Expected: the `destroyed`-counting cases FAIL (they assert the call happened), and the gameplay case FAILS because the ghost body still holds the upper box up. Record both.

Restore `destroy_body`, then run a second mutation: comment out the whole reap loop, so nothing is ever reaped.

Do NOT reach for inverting the predicate instead — `== nullptr` would skip the dead entries and fall through to destroy every *live* body, which is a different and much louder bug, not "nothing reaped".

Expected: every one of the six new cases fails. Restore the loop, rebuild, and confirm `[physics]` is green.

Record the asymmetry: under the first mutation the gameplay case is the ONLY one that fails for the right reason — the `destroyed`-counting cases fail merely because the call is absent, while the gameplay case fails because a ghost body is still holding the upper box up. That is the difference between "maps cleaned but body left alive" and a correct reap, and it is the whole argument for writing a positional test instead of a call-counting one.

- [ ] **Step 6: Run the full gate**

```bash
cmake --build --preset debug -j 20
```

Wait for it to exit, then:

```bash
ctest --preset debug --output-on-failure
```

Expected: PASS, with **no `-E` exclusions**.

- [ ] **Step 7: Verify both entry points**

```bash
timeout 40 ./build/debug/app/mosaic run --window --duration 10
timeout 40 ./build/debug/app/mosaic run --window --script-thread --duration 10
```

Expected for both: exit 0, **zero Vulkan validation errors**, no Python errors. The demo scene never destroys an entity, so these confirm the reap costs nothing on the common path rather than exercising it.

- [ ] **Step 8: Commit**

```bash
git add engine/physics/physics_system.hpp tests/unit/physics/physics_system_test.cpp
git commit -m "fix(physics): destroy a body when its entity is destroyed"
```
