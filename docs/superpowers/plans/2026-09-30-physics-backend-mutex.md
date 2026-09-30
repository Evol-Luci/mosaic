# Physics Backend Mutex Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop a gameplay script's `raycast()` racing the physics simulation, by serializing every `PhysicsSystem` entry point that touches shared state.

**Architecture:** One `mutable std::mutex` on `PhysicsSystem`, taken by `update()`, `raycast()`, `apply_impulse()` and `has_body()`. It protects two things: the seven `backend_` call sites, and the `bodies_`/`handle_to_entity_` maps that `update()` writes while the other three read them.

**Tech Stack:** C++20, `std::mutex`/`std::lock_guard`/`std::atomic`, Box3D, embedded CPython via pybind11, Catch2, CMake presets.

**Spec:** `docs/superpowers/specs/2026-09-30-physics-backend-mutex-design.md`

## Global Constraints

- **Every method that touches shared mutable state takes the lock** — `update()`, `raycast()`, `apply_impulse()`, `has_body()`. Guarding only `step()` and `raycast()` is the fix that looks right and is not.
- `has_body()` takes the lock even though it never touches `backend_`: it reads `bodies_`, which `update()` writes. A concurrent read of an `unordered_map` being written is a data race in its own right.
- `apply_impulse()` is **not** already safe by being main-thread-only. It runs from `ScriptSystem::update_threaded`'s replay, which is *outside* `PhysicsSystem::update()`, exactly when the worker may be mid-`raycast()`.
- Use a plain `std::mutex`, **not** `std::recursive_mutex`. `update()` never calls the other three, and every main-thread caller runs outside `update()`. A recursive mutex here would paper over a nesting problem that does not exist.
- `update()` holds the lock across its **whole body**, including ECS traversal. Longer than strictly necessary, and deliberately so — four narrow critical sections inside one function is the shape that grows a bug later.
- `raycast()` is `const`, so the member is `mutable`. Do not change `raycast()`'s signature.
- Do not change any `PhysicsBackend` interface or any Box3D call.
- Build: `cmake --build --preset debug -j 20`. Test binary: `./build/debug/tests/mosaic_unit_tests`. Gate: `ctest --preset debug --output-on-failure` — **no `-E` exclusions**; they were removed on 2026-09-30 and must not come back.
- **Run only one build at a time** — concurrent `cmake --build` in this tree corrupts archives.
- Never `git add -A`: `docs/project/advancement-plans-and-evaluation.md` is modified and must never be staged.
- **Never `git commit --amend` without first checking `git log -1`** — a commit rejected by the pre-commit hook leaves HEAD on the previous commit, and amending then rewrites that one.
- Catch2 `TEST_CASE` names: no commas inside the name, no leading `--`, no unmatched `[`.

## Review Focus

Input classes the spec implies that no task's tests would otherwise exercise. Each gets a test in the task that owns the code.

1. **A worker raycast landing while `update()` is mid-body-creation**, not just mid-step. `create_body` and the `bodies_` insert are the earliest part of `update()`; a lock that only covered stepping would leave them exposed. → Task 2.
2. **`has_body()` called concurrently with `update()` inserting into `bodies_`.** The map read is the race, not a backend call — so a reviewer scanning only for `backend_->` would not see it. → Task 2.
3. **A raycast on the main thread while the worker holds nothing** — i.e. the ordinary editor-picker path must not deadlock or regress now that `raycast()` takes a lock. → Task 1.
4. **Re-entrancy:** any path where a locked method calls another locked method would self-deadlock with a plain `std::mutex`. The claim is that none exists; it needs pinning, not assuming. → Task 1.
5. **The single-threaded path is unchanged** — no behaviour difference, no measurable stall, for runs that never start a worker. → Task 1.

---

### Task 1: Take the lock in all four entry points

Adds the mutex and the four `lock_guard`s. Testable on its own: the existing physics suite must pass untouched, which is what proves no deadlock and no single-threaded regression.

**Files:**
- Modify: `engine/physics/physics_system.hpp`
- Test: `tests/unit/physics/physics_system_test.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `PhysicsSystem` is safe to call `raycast()`, `apply_impulse()` and `has_body()` on from another thread while `update()` runs on the main thread.

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/physics/physics_system_test.cpp`:

```cpp
TEST_CASE("PhysicsSystem::raycast is callable from another thread while update runs", "[physics]")
{
    // Review Focus 3 and 5: the ordinary main-thread raycast path must still work, and
    // a second thread hammering raycast while the main thread ticks must neither
    // deadlock nor crash. This is the smoke test; the overlap DETECTOR is Task 2.
    World world;
    world.add_system<PhysicsSystem>(
        mosaic::physics::box3d::create_box3d_backend(mosaic::engine::physics::WorldDef{}));

    auto ground = world.create_entity();
    world.add_component<Transform>(ground,
                                   Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{10, 1, 10}, {}});
    world.add_component<RigidBody>(ground, RigidBody{false, 1.0f});
    world.add_component<Collider>(ground, Collider{Vec3{5.0f, 0.5f, 5.0f}});
    world.tick(1.0f / 60.0f);

    auto* physics = world.get_system<PhysicsSystem>();
    REQUIRE(physics != nullptr);

    std::atomic<bool> stop{false};
    std::atomic<int>  hits{0};
    std::thread reader(
        [&]
        {
            while (!stop.load())
            {
                if (physics->raycast(Vec3{0, 5, 0}, Vec3{0, -1, 0}, 10.0f).hit) { ++hits; }
            }
        });

    for (int i = 0; i < 200; ++i) { world.tick(1.0f / 60.0f); }
    stop.store(true);
    reader.join();

    REQUIRE(hits.load() > 0); // the ray really was hitting the ground, not just not-crashing
}

TEST_CASE("PhysicsSystem locked methods do not re-enter one another", "[physics]")
{
    // Review Focus 4: a plain std::mutex self-deadlocks on re-entry. The claim is that
    // update() never calls raycast/apply_impulse/has_body and vice versa. If that ever
    // stops being true this test hangs rather than failing quietly, which is the point.
    World world;
    world.add_system<PhysicsSystem>(
        mosaic::physics::box3d::create_box3d_backend(mosaic::engine::physics::WorldDef{}));

    auto ball = world.create_entity();
    world.add_component<Transform>(ball,
                                   Transform{Vec3{0, 5, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(ball, RigidBody{true, 1.0f});
    world.add_component<SphereCollider>(ball, SphereCollider{0.5f});
    world.tick(1.0f / 60.0f);

    auto* physics = world.get_system<PhysicsSystem>();
    REQUIRE(physics != nullptr);

    // Each of these takes the lock; calling them back to back on one thread proves the
    // lock is released between calls.
    REQUIRE(physics->has_body(ball));
    REQUIRE_NOTHROW(physics->apply_impulse(ball, Vec3{1, 0, 0}, Vec3{0, 4.5f, 0}));
    REQUIRE_NOTHROW(physics->raycast(Vec3{0, 10, 0}, Vec3{0, -1, 0}, 20.0f));
    REQUIRE_NOTHROW(world.tick(1.0f / 60.0f));
    REQUIRE(physics->has_body(ball));
}
```

Add `#include <atomic>` and `#include <thread>` to that test file's includes if absent.

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
./build/debug/tests/mosaic_unit_tests "PhysicsSystem::raycast is callable from another thread while update runs"
```

Expected: this is a race, so it may CRASH, hang, or pass by luck rather than failing cleanly. Any of those is the "before" state. Run it three times and record what happens each time — that record is the evidence the mutex changed something. Do not proceed on a single lucky pass.

- [ ] **Step 3: Add the mutex**

In `engine/physics/physics_system.hpp`, add `#include <mutex>` to the include block (it currently has `<memory>` and `<unordered_map>`; keep them alphabetical).

In the `private:` section, beside the existing members, add:

```cpp
    // Serializes every entry point that touches backend_ or the handle maps. Scripts
    // can run on a worker thread (--script-thread), and a raycast from there would
    // otherwise run concurrently with the main thread's update() -- reading a Box3D
    // world mid-step, and reading bodies_/handle_to_entity_ mid-insert. Held across
    // the whole of update() rather than around individual calls: four narrow critical
    // sections inside one function is the shape that grows a bug later.
    mutable std::mutex backend_mutex_;
```

- [ ] **Step 4: Take the lock in all four entry points**

In `update(World& world, float dt)`, as the very first statement of the body:

```cpp
        const std::lock_guard<std::mutex> lock(backend_mutex_);
```

In `raycast(...)`, as the very first statement:

```cpp
        const std::lock_guard<std::mutex> lock(backend_mutex_);
```

In `has_body(...)`, as the very first statement:

```cpp
        const std::lock_guard<std::mutex> lock(backend_mutex_);
```

In `apply_impulse(...)`, as the very first statement:

```cpp
        const std::lock_guard<std::mutex> lock(backend_mutex_);
```

Do not add a lock anywhere else. `shape_for` and `pose_matches` are static helpers that touch no member state.

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
./build/debug/tests/mosaic_unit_tests "[physics]"
```

Expected: PASS, including every pre-existing `[physics]` case.

Then run the concurrent test ten times to check it is stable, not lucky:

```bash
for i in $(seq 1 10); do ./build/debug/tests/mosaic_unit_tests "PhysicsSystem::raycast is callable from another thread while update runs" 2>&1 | tail -1; done
```

Expected: ten passes. If any run hangs, a locked method is re-entering another — check that no lock was added inside a helper called from `update()`.

- [ ] **Step 6: Commit**

```bash
git add engine/physics/physics_system.hpp tests/unit/physics/physics_system_test.cpp
git commit -m "fix(physics): serialize backend access behind a mutex"
```

---

### Task 2: Prove mutual exclusion with an overlap detector

A passing concurrent test shows no crash. It does not show the lock works. This task tests the actual property.

**Files:**
- Test: `tests/unit/physics/physics_concurrency_test.cpp` (create)
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: the locking added in Task 1.
- Produces: nothing consumed by a later task.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/physics/physics_concurrency_test.cpp`:

```cpp
// Tests the property, not the symptom.
//
// "Ran a threaded script, saw no crash" is not evidence of mutual exclusion -- it is
// the reasoning that let a real race hide behind a CI exclusion in this project for
// nine days. This backend records whether two of its methods were ever in flight at
// once, and sleeps inside each one to widen the window.

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/physics/physics_backend.hpp"
#include "engine/physics/physics_components.hpp"
#include "engine/physics/physics_system.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"

using mosaic::engine::World;
using mosaic::engine::physics::BodyDef;
using mosaic::engine::physics::BodyHandle;
using mosaic::engine::physics::Collider;
using mosaic::engine::physics::PhysicsBackend;
using mosaic::engine::physics::PhysicsSystem;
using mosaic::engine::physics::Pose;
using mosaic::engine::physics::RigidBody;
using mosaic::engine::transform::Transform;
using mosaic::foundation::math::Quat;
using mosaic::foundation::math::Vec3;

namespace
{
// Every method enters through Guard, which trips overlap_seen if a second call is
// already inside, and sleeps to make a genuine overlap overwhelmingly likely to be
// observed rather than missed.
class OverlapDetectingBackend : public PhysicsBackend
{
public:
    std::atomic<int>  depth{0};
    std::atomic<bool> overlap_seen{false};

    struct Guard
    {
        OverlapDetectingBackend* b;
        explicit Guard(OverlapDetectingBackend* be) : b(be)
        {
            if (b->depth.fetch_add(1) > 0) { b->overlap_seen.store(true); }
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        ~Guard()
        {
            if (b->depth.fetch_sub(1) > 1) { b->overlap_seen.store(true); }
        }
    };

    BodyHandle create_body(const BodyDef&) override
    {
        Guard g(this);
        return ++next_;
    }
    void step(float, int) override { Guard g(this); }
    Pose body_pose(BodyHandle) const override
    {
        Guard g(const_cast<OverlapDetectingBackend*>(this));
        return Pose{};
    }
    void set_body_transform(BodyHandle, const Vec3&, const Quat&) override { Guard g(this); }
    void apply_impulse(BodyHandle, const Vec3&, const Vec3&) override { Guard g(this); }
    mosaic::engine::physics::RaycastResult raycast(const Vec3&, const Vec3&,
                                                   float) const override
    {
        Guard g(const_cast<OverlapDetectingBackend*>(this));
        return mosaic::engine::physics::RaycastResult{};
    }
    std::vector<mosaic::engine::physics::BodyContact> contact_events() const override
    {
        Guard g(const_cast<OverlapDetectingBackend*>(this));
        return {};
    }

private:
    BodyHandle next_ = 0;
};
} // namespace

TEST_CASE("PhysicsSystem never has two backend calls in flight at once", "[physics]")
{
    World world;
    auto  backend = std::make_unique<OverlapDetectingBackend>();
    auto* probe   = backend.get();
    world.add_system<PhysicsSystem>(std::move(backend));

    // Several bodies, so update() spends real time inside create_body and the
    // bodies_ insert -- Review Focus 1: the earliest part of update(), not just step().
    for (int i = 0; i < 8; ++i)
    {
        auto e = world.create_entity();
        world.add_component<Transform>(
            e, Transform{Vec3{static_cast<float>(i), 2, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
        world.add_component<RigidBody>(e, RigidBody{true, 1.0f});
        world.add_component<Collider>(e, Collider{Vec3{0.5f, 0.5f, 0.5f}});
    }

    auto* physics = world.get_system<PhysicsSystem>();
    REQUIRE(physics != nullptr);

    std::atomic<bool> stop{false};
    // Review Focus 2: has_body() reads bodies_ while update() inserts into it. That is a
    // map race, not a backend call, so it is invisible to anyone grepping for backend_->.
    std::thread reader(
        [&]
        {
            while (!stop.load())
            {
                physics->raycast(Vec3{0, 5, 0}, Vec3{0, -1, 0}, 10.0f);
                physics->has_body(mosaic::foundation::EntityID{});
            }
        });

    for (int i = 0; i < 150; ++i) { world.tick(1.0f / 60.0f); }
    stop.store(true);
    reader.join();

    REQUIRE_FALSE(probe->overlap_seen.load());
}
```

Register it in `tests/CMakeLists.txt` immediately after the `unit/physics/physics_system_test.cpp` line:

```
  unit/physics/physics_concurrency_test.cpp
```

- [ ] **Step 2: Run the test to verify it passes with the mutex**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
./build/debug/tests/mosaic_unit_tests "PhysicsSystem never has two backend calls in flight at once"
```

Expected: PASS. (Task 1 already added the lock, so this is the "after" state.)

- [ ] **Step 3: Prove the detector can fail**

This is the step that gives the test its value. A concurrency test that has never failed is indistinguishable from one that cannot.

Temporarily comment out the `lock_guard` line in `PhysicsSystem::raycast()` only — leave the other three in place:

```cpp
        // const std::lock_guard<std::mutex> lock(backend_mutex_);   // MUTATION
```

Rebuild and run the test five times:

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
for i in $(seq 1 5); do ./build/debug/tests/mosaic_unit_tests "PhysicsSystem never has two backend calls in flight at once" 2>&1 | tail -1; done
```

Expected: FAIL on most or all runs, on `REQUIRE_FALSE(probe->overlap_seen.load())`.

If it passes all five, the detector is not detecting: the sleep window may be too short, or the reader thread may not be overlapping `update()` at all. Do not proceed — a green test here would be worthless. Raise the sleep to 1000 microseconds and retry; if it still passes, report that rather than accepting it.

Then restore the line, rebuild, and confirm the test passes again.

- [ ] **Step 4: Run the full gate**

```bash
cmake --build --preset debug -j 20
```

Wait for it to exit, then:

```bash
ctest --preset debug --output-on-failure
```

Expected: PASS, with **no `-E` exclusions**. Those were removed on 2026-09-30 after a real race was fixed; do not reintroduce them to make a run green.

- [ ] **Step 5: Verify both entry points**

```bash
timeout 40 ./build/debug/app/mosaic run --window --duration 10
timeout 40 ./build/debug/app/mosaic run --window --script-thread --duration 10
```

Expected for both: exit 0, **zero Vulkan validation errors**, no Python errors. The second exercises the path this whole slice is about — a script raycasting from the worker thread against a locked `PhysicsSystem`.

- [ ] **Step 6: Commit**

```bash
git add tests/unit/physics/physics_concurrency_test.cpp tests/CMakeLists.txt
git commit -m "test(physics): detect overlapping backend calls directly"
```
