# Physics — Emit Contact Events Outside the Backend Lock Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop `PhysicsSystem::update()` calling out to user code while holding `backend_mutex_`, so a synchronous `ContactEvent` handler can call back into `PhysicsSystem` without deadlocking.

**Architecture:** Wrap steps 1–4 of `update()` plus the handle→entity contact mapping in an inner braced block holding the `lock_guard`, collecting mapped `ContactEvent`s into a local vector. The block closes, releasing the lock. Then emit. Nothing else moves: every access to `backend_` and to the three maps stays inside the lock.

**Tech Stack:** C++20, `std::mutex`/`std::lock_guard`/`std::atomic`/`std::thread`, Box3D, Catch2, CMake presets.

**Spec:** `docs/superpowers/specs/2026-10-01-physics-contact-emit-unlocked-design.md`

## Global Constraints

- **Only the `emit` moves out of the lock.** The handle→entity mapping reads `handle_to_entity_` and stays inside it. Every access to `backend_`, `bodies_`, `handle_to_entity_` and `synced_pose_` remains under the lock.
- **Use an inner braced block, not `std::unique_lock` + `unlock()`, and not an extracted helper.** The lock's extent must be delimited lexically so a later edit cannot add a call-out inside it without seeing the braces. This was decided against both alternatives; do not "simplify" it back.
- `contacts` is a plain local `std::vector<ContactEvent>`, not a reused member. Contact counts are small and no allocation has been measured.
- Contacts whose handle is not in `handle_to_entity_` are still dropped during collection, exactly as now.
- Do not change `ContactEvent`, any `PhysicsBackend` interface, or any Box3D call. Do not change `raycast()`'s signature or the mutex's type (plain `std::mutex`, not recursive).
- The pose write-back (step 4) already precedes emission (step 5). **This ordering does not change.** Any claim that it does is wrong — see the spec's correction section.
- Watchdog timeout is **3000 ms**, as a named constant. Do not inline the number at multiple sites.
- Build: `cmake --build --preset debug -j 20`. Test binary: `./build/debug/tests/mosaic_unit_tests`. Gate: `ctest --preset debug --output-on-failure` — **no `-E` exclusions**; they were removed on 2026-09-30 and must not come back.
- **Run only one build at a time** — concurrent `cmake --build` in this tree corrupts archives.
- Never `git add -A`: `docs/project/advancement-plans-and-evaluation.md` is modified and must never be staged. Stage explicit paths only.
- **Never `git commit --amend` without first checking `git log -1`** — a commit rejected by the pre-commit hook leaves HEAD on the previous commit, and amending then rewrites that one.
- The pre-commit hook regenerates `docs/index.html` and `docs/reference/*.md`. If it rejects a commit saying generated docs were stale, `git add` the regenerated files it names and commit again.
- Catch2 `TEST_CASE` names: no commas inside the name, no leading `--`, no unmatched `[`.

## Review Focus

Failure modes the spec implies that no task's tests would otherwise exercise. Each gets a test in the task that owns the code.

1. **A contact emitted twice is invisible to every existing test.** `physics_contacts_test.cpp`'s determinism case asserts `record_contacts() == record_contacts()` — an emit-twice bug duplicates identically in both runs and passes. The presence cases scan for `begin_seen`/`end_seen`, never a count. Collect-then-emit is exactly the kind of refactor that drops or duplicates, and today nothing would catch it. → Task 1.
2. **A tick with no contacts must not fire the handler at all.** The empty-vector path is the common case (most ticks have zero contacts) and the one a collect-then-emit bug would most easily turn into a spurious emission of a stale buffer. → Task 1.
3. **Contact order must survive the buffer.** A reversed or re-sorted collection would still satisfy run-to-run determinism and still contain every event. Begin must still precede End for the same pair. → Task 1.
4. **A handler's `raycast()` must return a real hit, not merely return.** Proving "it did not deadlock" is weaker than proving the call worked: poses are written by step 4, so a ray cast down at a resting body must actually hit it. A handler that gets a miss is a handler observing a broken world. → Task 2.
5. **A handler's `apply_impulse()` must actually move the body.** The write path. "It returned" is not the property; the impulse reaching the simulation is. → Task 2.

---

### Task 1: Pin contact delivery before touching the emit

A characterization guard, not red-green TDD: these tests pass against the **current** code. That is the point — they must exist and be proven load-bearing *before* Task 2 refactors the code they protect. Say so plainly rather than dressing it as a failing test.

**Files:**
- Test: `tests/unit/physics/physics_contacts_test.cpp` (append)

**Interfaces:**
- Consumes: nothing.
- Produces: the regression guard Task 2 relies on. No symbols.

- [ ] **Step 1: Write the characterization tests**

Append to `tests/unit/physics/physics_contacts_test.cpp`. The file already has `using` declarations for `World`, `Collider`, `ContactEvent`, `PhysicsSystem`, `RigidBody`, `Transform`, `EntityID`, `Quat`, `Vec3`, `ContactPhase`, `WorldDef`, `create_box3d_backend`, and an `is_entity_pair` helper — reuse them, do not redeclare.

```cpp
TEST_CASE("PhysicsSystem emits each contact exactly once", "[physics]")
{
    // Review Focus 1: the determinism case above compares two runs to each other, so an
    // emit-twice bug duplicates identically in both and passes it. The presence cases scan
    // for begin_seen, never a count. Nothing in this suite counts, so nothing here would
    // notice collect-then-emit delivering a contact twice.
    World world;
    world.add_system<PhysicsSystem>(create_box3d_backend(WorldDef{}));

    int begin_count = 0;
    const auto ground = world.create_entity();
    const auto box = world.create_entity();

    world.events().subscribe<ContactEvent>(
        [&](const ContactEvent& c)
        {
            if (c.phase == ContactPhase::Begin && is_entity_pair(c, box, ground)) { ++begin_count; }
        });

    world.add_component<Transform>(ground,
                                   Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{10, 1, 10}, {}});
    world.add_component<RigidBody>(ground, RigidBody{false, 1.0f});
    world.add_component<Collider>(ground, Collider{Vec3{5.0f, 0.5f, 5.0f}});

    world.add_component<Transform>(box, Transform{Vec3{0, 3, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(box, RigidBody{true, 1.0f});
    world.add_component<Collider>(box, Collider{Vec3{0.5f, 0.5f, 0.5f}});

    for (int i = 0; i < 180; ++i) { world.tick(1.0f / 60.0f); }

    // Exactly one: the box lands once and stays put. Two means duplication; zero means the
    // scenario stopped producing contacts and the test would be vacuous.
    REQUIRE(begin_count == 1);
}

TEST_CASE("PhysicsSystem fires no ContactEvent on a tick with no contacts", "[physics]")
{
    // Review Focus 2: the empty-collection path is the common case -- most ticks have zero
    // contacts -- and the one a collect-then-emit bug would most easily turn into a
    // re-emission of a stale buffer.
    World world;
    world.add_system<PhysicsSystem>(create_box3d_backend(WorldDef{}));

    int emitted = 0;
    world.events().subscribe<ContactEvent>([&](const ContactEvent&) { ++emitted; });

    // One dynamic box falling through empty space: nothing to touch.
    const auto box = world.create_entity();
    world.add_component<Transform>(box, Transform{Vec3{0, 50, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(box, RigidBody{true, 1.0f});
    world.add_component<Collider>(box, Collider{Vec3{0.5f, 0.5f, 0.5f}});

    for (int i = 0; i < 30; ++i) { world.tick(1.0f / 60.0f); }

    REQUIRE(emitted == 0);
    REQUIRE(world.get_component<Transform>(box)->position.y < 50.0f); // it really did simulate
}

TEST_CASE("PhysicsSystem preserves contact order across the collection buffer", "[physics]")
{
    // Review Focus 3: a reversed or re-sorted buffer still satisfies run-to-run determinism
    // and still contains every event, so the determinism case cannot see it. Begin must
    // still arrive before End for the same pair.
    World world;
    world.add_system<PhysicsSystem>(create_box3d_backend(WorldDef{}));

    const auto ground = world.create_entity();
    const auto box = world.create_entity();

    std::vector<ContactPhase> phases;
    world.events().subscribe<ContactEvent>(
        [&](const ContactEvent& c)
        {
            if (is_entity_pair(c, box, ground)) { phases.push_back(c.phase); }
        });

    world.add_component<Transform>(ground,
                                   Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{10, 1, 10}, {}});
    world.add_component<RigidBody>(ground, RigidBody{false, 1.0f});
    world.add_component<Collider>(ground, Collider{Vec3{5.0f, 0.5f, 5.0f}});

    world.add_component<Transform>(box, Transform{Vec3{0, 3, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(box, RigidBody{true, 1.0f});
    world.add_component<Collider>(box, Collider{Vec3{0.5f, 0.5f, 0.5f}});

    for (int i = 0; i < 180; ++i) { world.tick(1.0f / 60.0f); } // lands -> Begin

    world.get_component<Transform>(box)->position = Vec3{0, 50, 0};
    for (int i = 0; i < 30; ++i) { world.tick(1.0f / 60.0f); }  // separates -> End

    REQUIRE(phases.size() >= 2);
    REQUIRE(phases.front() == ContactPhase::Begin);
    REQUIRE(phases.back() == ContactPhase::End);
}
```

- [ ] **Step 2: Run them against the current, unmodified code**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
./build/debug/tests/mosaic_unit_tests "PhysicsSystem emits each contact exactly once"
./build/debug/tests/mosaic_unit_tests "PhysicsSystem fires no ContactEvent on a tick with no contacts"
./build/debug/tests/mosaic_unit_tests "PhysicsSystem preserves contact order across the collection buffer"
```

Expected: all three PASS. They are characterization tests — they describe behaviour that already holds. A failure here means one of the scenarios does not do what this plan claims (e.g. the box bounces and lands twice), and the test must be corrected before Task 2 relies on it.

- [ ] **Step 3: Prove the exactly-once test can fail**

A guard that has never failed is not a guard. Temporarily duplicate the emission in `engine/physics/physics_system.hpp`'s step 5, inside the existing loop:

```cpp
            world.events().emit<ContactEvent>(ContactEvent{c.phase, ia->second, ib->second, c.point,
                                                           c.normal, c.separation, c.normal_impulse});
            world.events().emit<ContactEvent>(ContactEvent{c.phase, ia->second, ib->second, c.point,
                                                           c.normal, c.separation, c.normal_impulse});  // MUTATION
```

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
./build/debug/tests/mosaic_unit_tests "PhysicsSystem emits each contact exactly once"
./build/debug/tests/mosaic_unit_tests "the contact-event stream is deterministic across runs"
```

Expected: the exactly-once case FAILS with `begin_count == 2`. The determinism case **PASSES** — which is the whole justification for adding these tests, so record both results.

Then remove the duplicated line, rebuild, and confirm the exactly-once case passes again. Verify with `git diff engine/physics/physics_system.hpp` that the file is byte-identical to HEAD before continuing.

- [ ] **Step 4: Commit**

```bash
git add tests/unit/physics/physics_contacts_test.cpp
git commit -m "test(physics): pin contact delivery to exactly once, in order"
```

---

### Task 2: Move the emit outside the lock

**Files:**
- Modify: `engine/physics/physics_system.hpp` (`update()`, currently lines 54–140)
- Create: `tests/unit/physics/physics_contact_reentry_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1's delivery guard (no symbols; it is the regression net for this change).
- Produces: a `ContactEvent` handler may call `PhysicsSystem::raycast`, `has_body` and `apply_impulse` without deadlocking.

- [ ] **Step 1: Write the failing re-entrancy tests**

Create `tests/unit/physics/physics_contact_reentry_test.cpp`:

```cpp
// A synchronous ContactEvent handler used to run INSIDE backend_mutex_, so a handler that
// called back into PhysicsSystem self-deadlocked on a plain std::mutex.
//
// The pre-fix failure mode is a HANG, not a failed assertion, and this repo sets no ctest
// TIMEOUT (so the default 1500s applies). These tests therefore run the tick on a detached
// thread and let the MAIN thread time out, which is what makes the failure a named Catch2
// assertion printed in three seconds instead of a 25-minute stall.
//
// A tripped watchdog cannot clean up: the detached thread stays blocked on the mutex
// forever, so the process may die untidily at exit. That is the pre-fix state, not
// something these tests introduce -- expect it when running the mutation probe.

#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "engine/physics/physics_components.hpp"
#include "engine/physics/physics_system.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "modules/physics_box3d/box3d_backend.hpp"

using mosaic::engine::World;
using mosaic::engine::physics::Collider;
using mosaic::engine::physics::ContactEvent;
using mosaic::engine::physics::PhysicsSystem;
using mosaic::engine::physics::RigidBody;
using mosaic::engine::physics::WorldDef;
using mosaic::engine::transform::Transform;
using mosaic::foundation::math::Quat;
using mosaic::foundation::math::Vec3;
using mosaic::physics::box3d::create_box3d_backend;

namespace
{
// Ceiling on a tick that normally takes microseconds -- four orders of magnitude of margin.
// If a loaded runner ever trips this spuriously, raise it; do not delete the test.
constexpr int kWatchdogMs = 3000;

// Ticks `world` `count` times on a detached thread and waits up to kWatchdogMs for it to
// finish. Returns false if it did not. The flag is heap-owned via shared_ptr so the
// detached thread never touches a dead stack local if it outlives this call.
bool tick_with_watchdog(World& world, int count)
{
    auto done = std::make_shared<std::atomic<bool>>(false);
    std::thread(
        [&world, count, done]
        {
            for (int i = 0; i < count; ++i) { world.tick(1.0f / 60.0f); }
            done->store(true);
        })
        .detach();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kWatchdogMs);
    while (!done->load() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return done->load();
}

// Ground plus a box dropped onto it: produces a Begin contact within ~90 ticks.
void build_falling_box_scene(World& world)
{
    const auto ground = world.create_entity();
    world.add_component<Transform>(ground,
                                   Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{10, 1, 10}, {}});
    world.add_component<RigidBody>(ground, RigidBody{false, 1.0f});
    world.add_component<Collider>(ground, Collider{Vec3{5.0f, 0.5f, 5.0f}});

    const auto box = world.create_entity();
    world.add_component<Transform>(box, Transform{Vec3{0, 3, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(box, RigidBody{true, 1.0f});
    world.add_component<Collider>(box, Collider{Vec3{0.5f, 0.5f, 0.5f}});
}
} // namespace

TEST_CASE("a ContactEvent handler can raycast without deadlocking", "[physics]")
{
    World world;
    world.add_system<PhysicsSystem>(create_box3d_backend(WorldDef{}));
    auto* physics = world.get_system<PhysicsSystem>();
    REQUIRE(physics != nullptr);

    int  handler_calls = 0;
    bool ray_hit = false;
    world.events().subscribe<ContactEvent>(
        [&](const ContactEvent&)
        {
            ++handler_calls;
            // Review Focus 4: not merely "it returned" -- step 4 wrote the poses before this
            // handler ran, so a ray straight down must actually hit the resting body. A miss
            // would mean the handler is observing a world mid-update.
            if (physics->raycast(Vec3{0, 10, 0}, Vec3{0, -1, 0}, 20.0f).hit) { ray_hit = true; }
        });

    build_falling_box_scene(world);

    REQUIRE(tick_with_watchdog(world, 180)); // false == the handler's raycast deadlocked
    REQUIRE(handler_calls > 0);              // the scenario really produced a contact
    REQUIRE(ray_hit);
}

TEST_CASE("a ContactEvent handler can apply an impulse without deadlocking", "[physics]")
{
    World world;
    world.add_system<PhysicsSystem>(create_box3d_backend(WorldDef{}));
    auto* physics = world.get_system<PhysicsSystem>();
    REQUIRE(physics != nullptr);

    int  handler_calls = 0;
    bool pushed = false;
    world.events().subscribe<ContactEvent>(
        [&](const ContactEvent& c)
        {
            ++handler_calls;
            // has_body() takes the same lock and touches no backend call, so it is the
            // cheapest proof the maps are readable from here.
            if (physics->has_body(c.a) && !pushed)
            {
                physics->apply_impulse(c.a, Vec3{25, 0, 0}, Vec3{0, 0, 0});
                pushed = true;
            }
        });

    build_falling_box_scene(world);

    REQUIRE(tick_with_watchdog(world, 240)); // false == the handler's apply_impulse deadlocked
    REQUIRE(handler_calls > 0);
    REQUIRE(pushed);

    // Review Focus 5: the impulse must reach the simulation, not merely be accepted. The box
    // starts at x == 0 and nothing else pushes it sideways.
    //
    // Why this survives to the next tick: the handler runs after step 4 has written
    // Transform = pose and recorded synced_pose_ = pose, so next tick's step 2 sees them
    // matching and does NOT teleport the body -- which would have reset the velocity the
    // impulse just gave it. If this test ever starts failing with the box unmoved, that
    // external-edit heuristic is the first thing to check.
    bool moved_in_x = false;
    world.for_each<RigidBody>(
        [&](mosaic::foundation::EntityID e, RigidBody& rb)
        {
            if (!rb.dynamic) { return; }
            const Transform* t = world.get_component<Transform>(e);
            if (t != nullptr && std::abs(t->position.x) > 0.1f) { moved_in_x = true; }
        });
    REQUIRE(moved_in_x);
}
```

Register it in `tests/CMakeLists.txt` immediately after the `unit/physics/physics_concurrency_test.cpp` line:

```
  unit/physics/physics_contact_reentry_test.cpp
```

- [ ] **Step 2: Run them to verify they fail**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
./build/debug/tests/mosaic_unit_tests "a ContactEvent handler can raycast without deadlocking"
```

Expected: FAIL on `REQUIRE(tick_with_watchdog(world, 180))` after about three seconds, because the handler's `raycast()` deadlocks on the lock `update()` still holds. The process may not exit cleanly afterwards — the detached thread is blocked forever. That is expected here; the named assertion failure is the result that matters.

Run the second case too and record the same outcome. Do not proceed if either one *passes* — that would mean the handler never ran, and the test is vacuous rather than red.

- [ ] **Step 3: Move the emit outside the lock**

In `engine/physics/physics_system.hpp`, add `#include <vector>` to the include block, keeping it alphabetical (the block currently has `<memory>`, `<mutex>`, `<unordered_map>`).

Restructure `update()` so that the `lock_guard` lives in an inner block covering steps 1–4 and the contact mapping, and the emit loop runs after that block closes. The body becomes:

```cpp
    void update(World& world, float dt) override
    {
        // Contacts are mapped under the lock but emitted outside it. A synchronous
        // ContactEvent subscriber runs on this stack, and a subscriber that calls
        // raycast()/has_body()/apply_impulse() would self-deadlock on a plain std::mutex if
        // the lock were still held. Holding it across subscriber dispatch also ordered every
        // lock a subscriber takes -- the global logger mutex among them -- underneath this
        // one. The braced block below is what keeps that from coming back: the locked region
        // is delimited lexically, so a call-out cannot be added inside it unnoticed.
        std::vector<ContactEvent> contacts;
        {
            const std::lock_guard<std::mutex> lock(backend_mutex_);
```

followed by the existing steps 1, 2, 3 and 4 **unchanged**, then step 5 rewritten to collect instead of emit:

```cpp
            // 5a. Map backend contacts to entity-level events. Still inside the lock: this
            // reads handle_to_entity_, and contacts whose handle is unmapped are dropped.
            for (const BodyContact& c : backend_->contact_events())
            {
                const auto ia = handle_to_entity_.find(c.a);
                const auto ib = handle_to_entity_.find(c.b);
                if (ia == handle_to_entity_.end() || ib == handle_to_entity_.end())
                {
                    continue;
                }
                contacts.push_back(ContactEvent{c.phase, ia->second, ib->second, c.point, c.normal,
                                                c.separation, c.normal_impulse});
            }
        }

        // 5b. Outside the lock: subscribers may now call back into this system.
        for (const ContactEvent& e : contacts)
        {
            world.events().emit<ContactEvent>(e);
        }
    }
```

Do not change steps 1–4 in any way beyond their indentation. Do not touch `raycast()`, `has_body()`, `apply_impulse()`, `shape_for` or `pose_matches`.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
./build/debug/tests/mosaic_unit_tests "[physics]"
```

Expected: PASS, including Task 1's three delivery tests, the pre-existing contact tests, and #209's overlap detector — the last of which is what proves this change did not loosen the mutual exclusion it was built to guarantee.

- [ ] **Step 5: Prove the fix is load-bearing**

Move the emit back inside the braced block: replace the `contacts.push_back(...)` call with the original `world.events().emit<ContactEvent>(ContactEvent{...})` so emission happens under the lock again, and delete the now-empty 5b loop.

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
./build/debug/tests/mosaic_unit_tests "a ContactEvent handler can raycast without deadlocking"
./build/debug/tests/mosaic_unit_tests "a ContactEvent handler can apply an impulse without deadlocking"
```

Expected: both FAIL on the watchdog within about three seconds. If either passes, the test is not exercising the deadlock — check that the handler actually ran (`handler_calls > 0`) rather than accepting the green.

Then restore Step 3's version, rebuild, and confirm both pass. Verify with `git diff` that the only change to `engine/physics/physics_system.hpp` is Step 3's.

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

Expected for both: exit 0, **zero Vulkan validation errors**, no Python errors. The demo's contact handler logs once per contact and re-emits onto the deferred bus, so both runs exercise the new emit path with real subscribers attached.

- [ ] **Step 8: Commit**

```bash
git add engine/physics/physics_system.hpp tests/unit/physics/physics_contact_reentry_test.cpp tests/CMakeLists.txt
git commit -m "fix(physics): emit contact events outside the backend lock"
```
