# Physics P5 — Contact Events + Determinism Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Surface Box3D contact events as a neutral `ContactEvent` emitted on the #11 EventBus, prove the contact sequence is deterministic, and log contacts into the 37e Console.

**Architecture:** The Box3D backend enables contact events on shapes and, after each step, maps begin/end touch events to neutral `BodyContact` (handles). `PhysicsSystem` drains them each tick, maps handles→entities (the P4 reverse map), and emits a public `ContactEvent` (entities) on the `World`'s EventBus. The app subscribes and logs. All headless-testable.

**Tech Stack:** C++20, Box3D (`b3World_GetContactEvents`), `engine/physics`, `foundation/events` (#11), `foundation/logging` (#33), Catch2, CMake.

**Spec:** `docs/superpowers/specs/2026-08-24-physics-p5-contacts-design.md`

## Global Constraints

- **C++20**; header-only in `engine/physics/`.
- **Tier contract:** `BodyContact`/`ContactEvent`/`contact_events` stay neutral in `engine/physics/`; `b3ContactEvents`/`b3World_GetContactEvents` stay in `modules/physics_box3d`. `b3*` never leaves the module.
- **Determinism:** unchanged stepping; the contact sequence is asserted reproducible.
- Contacts flow through the existing #11 `EventBus` (`World::events()`), not a physics-specific channel.
- No new third-party deps. Catch2 test names must not start with `--`.
- **Build/test:** `cmake --build build/debug -j` · `ctest --test-dir build/debug --output-on-failure`.

## Verified Box3D API (pinned commit)

- `b3ContactEvents b3World_GetContactEvents(b3WorldId)` → `{ b3ContactBeginTouchEvent* beginEvents; b3ContactEndTouchEvent* endEvents; int beginCount; int endCount; ... }`.
- `b3ContactBeginTouchEvent`/`b3ContactEndTouchEvent`: `{ b3ShapeId shapeIdA; b3ShapeId shapeIdB; ... }`.
- `b3ShapeDef` has `bool enableContactEvents` (default false — must set true; only applies to dynamic/kinematic).
- `b3BodyId b3Shape_GetBody(b3ShapeId)`; `bool b3Shape_IsValid(b3ShapeId)`; `void* b3Body_GetUserData(b3BodyId)` (holds the `BodyHandle`, from P4).

## Current state (verified)

- `modules/physics_box3d/box3d_backend.cpp` `create_body`: `b3ShapeDef sd = b3DefaultShapeDef(); sd.density = def.density;` then a `switch` on shape kind; `step()` is `b3World_Step(world_, dt, substeps)`. Body userData holds the `BodyHandle` (P4).
- `PhysicsSystem::update` (`engine/physics/physics_system.hpp`): steps, then writes back; owns `std::unordered_map<BodyHandle, EntityID> handle_to_entity_` (P4).
- `World::events()` → `EventBus&`; `emit<E>(e)` (sync subscribers fire in-tick) / `subscribe<E>(fn)`.
- `EntityID` has `value()` / `index()`.
- `tests/unit/physics/physics_system_test.cpp` `FakeBackend` implements `create_body`/`step`/`body_pose`/`set_body_transform`/`raycast` (add a `contact_events` stub).
- `app/run/demo_scene.hpp` `build_demo_scene(World&, RenderDevice&, AssetRegistry&)` includes `engine/physics/physics_system.hpp`.

---

## File Structure

- `engine/physics/physics_backend.hpp` — **modify.** `ContactPhase`, `BodyContact`, pure `contact_events`.
- `modules/physics_box3d/box3d_backend.cpp` — **modify.** Enable events + collect in `step` + `contact_events`.
- `engine/physics/physics_system.hpp` — **modify.** `ContactEvent` + emit on the EventBus.
- `app/run/demo_scene.hpp` — **modify.** Subscribe + log contacts.
- `tests/unit/physics/physics_system_test.cpp` — **modify.** `FakeBackend` `contact_events` stub.
- `tests/unit/physics/physics_contacts_test.cpp` — **create.** Backend + ECS begin/end/determinism.
- `tests/CMakeLists.txt` — **modify.** Register the new test.

---

## Task 1: Backend contact retrieval

**Files:**
- Modify: `engine/physics/physics_backend.hpp`
- Modify: `modules/physics_box3d/box3d_backend.cpp`
- Modify: `tests/unit/physics/physics_system_test.cpp`
- Create: `tests/unit/physics/physics_contacts_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `mosaic::engine::physics::ContactPhase { Begin, End }`; `BodyContact { ContactPhase phase; BodyHandle a; BodyHandle b; }`; `virtual std::vector<BodyContact> PhysicsBackend::contact_events() const = 0`.

- [ ] **Step 1: Write the failing backend test**

Create `tests/unit/physics/physics_contacts_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "engine/physics/physics_backend.hpp"
#include "modules/physics_box3d/box3d_backend.hpp"

using mosaic::engine::physics::BodyDef;
using mosaic::engine::physics::BodyHandle;
using mosaic::engine::physics::BodyType;
using mosaic::engine::physics::ContactPhase;
using mosaic::engine::physics::WorldDef;
using mosaic::foundation::math::Vec3;
using mosaic::physics::box3d::create_box3d_backend;

namespace
{
bool is_pair(BodyHandle a, BodyHandle b, BodyHandle x, BodyHandle y)
{
    return (a == x && b == y) || (a == y && b == x);
}
} // namespace

TEST_CASE("backend reports a begin-touch contact when a box lands on the ground", "[physics]")
{
    auto backend = create_box3d_backend(WorldDef{});

    BodyDef ground;
    ground.type = BodyType::Static;
    ground.position = Vec3{0, 0, 0};
    ground.shape.half_extents = Vec3{5, 0.5f, 5};
    const BodyHandle gh = backend->create_body(ground);

    BodyDef box;
    box.type = BodyType::Dynamic;
    box.position = Vec3{0, 3, 0};
    box.shape.half_extents = Vec3{0.5f, 0.5f, 0.5f};
    const BodyHandle bh = backend->create_body(box);

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

Register it in `tests/CMakeLists.txt` (after the other `unit/physics/...` entries):

```cmake
  unit/physics/physics_contacts_test.cpp
```

- [ ] **Step 2: Run — verify it fails**

Run: `cmake --build build/debug -j 2>&1 | tail -20`
Expected: `ContactPhase` undeclared; `PhysicsBackend` has no `contact_events`; `FakeBackend`
(in `physics_system_test.cpp`) becomes abstract once the method is added (fixed in Step 4).

- [ ] **Step 3: Add the neutral types + pure method**

In `engine/physics/physics_backend.hpp`, add `#include <vector>` near the top includes if not
present, then add (after `struct RaycastResult`):

```cpp
enum class ContactPhase { Begin, End };

struct BodyContact
{
    ContactPhase phase;
    BodyHandle   a;
    BodyHandle   b;
};
```

and to `class PhysicsBackend` (after `raycast`):

```cpp
    // Begin/end touch contacts produced by the most recent step().
    virtual std::vector<BodyContact> contact_events() const = 0;
```

- [ ] **Step 4: Stub `contact_events` in the test `FakeBackend`**

In `tests/unit/physics/physics_system_test.cpp`, add to `class FakeBackend` (after `raycast`):

```cpp
    std::vector<mosaic::engine::physics::BodyContact> contact_events() const override
    {
        return {};
    }
```

- [ ] **Step 5: Implement the Box3D backend**

In `modules/physics_box3d/box3d_backend.cpp`:

Enable events on every shape — in `create_body`, change the shape-def setup:

```cpp
        b3ShapeDef sd = b3DefaultShapeDef();
        sd.density = def.density;
        sd.enableContactEvents = true; // required: touch events are off by default
```

Collect events after stepping — replace `step`:

```cpp
    void step(float dt, int substeps) override
    {
        b3World_Step(world_, dt, substeps);
        last_contacts_.clear();
        const b3ContactEvents ev = b3World_GetContactEvents(world_);
        for (int i = 0; i < ev.beginCount; ++i)
        {
            last_contacts_.push_back(ep::BodyContact{
                ep::ContactPhase::Begin, handle_of(b3Shape_GetBody(ev.beginEvents[i].shapeIdA)),
                handle_of(b3Shape_GetBody(ev.beginEvents[i].shapeIdB))});
        }
        for (int i = 0; i < ev.endCount; ++i)
        {
            const b3ShapeId sa = ev.endEvents[i].shapeIdA;
            const b3ShapeId sb = ev.endEvents[i].shapeIdB;
            if (!b3Shape_IsValid(sa) || !b3Shape_IsValid(sb))
            {
                continue; // a shape was destroyed with its body; skip
            }
            last_contacts_.push_back(ep::BodyContact{
                ep::ContactPhase::End, handle_of(b3Shape_GetBody(sa)),
                handle_of(b3Shape_GetBody(sb))});
        }
    }

    std::vector<ep::BodyContact> contact_events() const override { return last_contacts_; }
```

Add the userData→handle helper (private, near the other helpers) and the member:

```cpp
    static ep::BodyHandle handle_of(b3BodyId id)
    {
        return static_cast<ep::BodyHandle>(reinterpret_cast<std::uintptr_t>(b3Body_GetUserData(id)));
    }
```

and, in the members block (next to `bodies_`):

```cpp
    std::vector<ep::BodyContact> last_contacts_;
```

- [ ] **Step 6: Run — verify pass**

Run: `cmake --build build/debug -j && ctest --test-dir build/debug --output-on-failure -R "begin-touch contact when a box lands"`
Expected: PASS. If it does **not** fire (Box3D requires *both* shapes to enable events and a
static shape can't), change the test's ground to a **dynamic** body resting on... no — instead
drop a second dynamic box onto the first (two dynamic bodies both enable events) and assert the
begin between them. But first try as written: the either-shape rule (like Box2D v3) should fire
the box↔ground begin.

- [ ] **Step 7: Full suite + commit**

Run: `ctest --test-dir build/debug --output-on-failure`
Expected: all PASS.

```bash
git add engine/physics/physics_backend.hpp modules/physics_box3d/box3d_backend.cpp tests/unit/physics/physics_system_test.cpp tests/unit/physics/physics_contacts_test.cpp tests/CMakeLists.txt
git commit -m "physics P5: Box3D contact events -> neutral BodyContact (#37)"
```

---

## Task 2: ContactEvent on the EventBus (ECS)

**Files:**
- Modify: `engine/physics/physics_system.hpp`
- Modify: `tests/unit/physics/physics_contacts_test.cpp`

**Interfaces:**
- Consumes: `BodyContact`/`ContactPhase`/`contact_events` (Task 1); `World::events()`, `handle_to_entity_` (P4).
- Produces: `mosaic::engine::physics::ContactEvent { ContactPhase phase; EntityID a; EntityID b; }`; `PhysicsSystem` emits it each tick.

- [ ] **Step 1: Write the failing ECS tests**

Append to `tests/unit/physics/physics_contacts_test.cpp`:

```cpp
#include <algorithm>
#include <vector>

#include "engine/physics/physics_components.hpp"
#include "engine/physics/physics_system.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"

using mosaic::engine::World;
using mosaic::engine::physics::Collider;
using mosaic::engine::physics::ContactEvent;
using mosaic::engine::physics::PhysicsSystem;
using mosaic::engine::physics::RigidBody;
using mosaic::engine::transform::Transform;
using mosaic::foundation::EntityID;
using mosaic::foundation::math::Quat;

namespace
{
bool is_entity_pair(const ContactEvent& c, EntityID x, EntityID y)
{
    return (c.a == x && c.b == y) || (c.a == y && c.b == x);
}
} // namespace

TEST_CASE("PhysicsSystem emits a ContactEvent when a box lands on the ground", "[physics]")
{
    World world;
    world.add_system<PhysicsSystem>(create_box3d_backend(WorldDef{}));

    std::vector<ContactEvent> events;
    world.events().subscribe<ContactEvent>([&](const ContactEvent& c) { events.push_back(c); });

    const auto ground = world.create_entity();
    world.add_component<Transform>(ground, Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{10, 1, 10}, {}});
    world.add_component<RigidBody>(ground, RigidBody{false, 1.0f});
    world.add_component<Collider>(ground, Collider{Vec3{5.0f, 0.5f, 5.0f}});

    const auto box = world.create_entity();
    world.add_component<Transform>(box, Transform{Vec3{0, 3, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(box, RigidBody{true, 1.0f});
    world.add_component<Collider>(box, Collider{Vec3{0.5f, 0.5f, 0.5f}});

    for (int i = 0; i < 180; ++i)
    {
        world.tick(1.0f / 60.0f);
    }

    bool begin_seen = false;
    for (const auto& c : events)
    {
        if (c.phase == ContactPhase::Begin && is_entity_pair(c, box, ground))
        {
            begin_seen = true;
        }
    }
    REQUIRE(begin_seen);

    // Move the box far away -> it separates from the ground -> an End contact fires.
    events.clear();
    world.get_component<Transform>(box)->position = Vec3{0, 50, 0};
    for (int i = 0; i < 30; ++i)
    {
        world.tick(1.0f / 60.0f);
    }
    bool end_seen = false;
    for (const auto& c : events)
    {
        if (c.phase == ContactPhase::End && is_entity_pair(c, box, ground))
        {
            end_seen = true;
        }
    }
    REQUIRE(end_seen);
}

namespace
{
// Record the emitted contact stream (phase + order-normalized entity pair) over a scenario.
std::vector<std::tuple<ContactPhase, std::uint64_t, std::uint64_t>> record_contacts()
{
    using mosaic::engine::physics::WorldDef;
    World world;
    world.add_system<PhysicsSystem>(create_box3d_backend(WorldDef{}));

    std::vector<std::tuple<ContactPhase, std::uint64_t, std::uint64_t>> seq;
    world.events().subscribe<ContactEvent>(
        [&](const ContactEvent& c)
        {
            const std::uint64_t lo = std::min(c.a.value(), c.b.value());
            const std::uint64_t hi = std::max(c.a.value(), c.b.value());
            seq.emplace_back(c.phase, lo, hi);
        });

    const auto ground = world.create_entity();
    world.add_component<Transform>(ground, Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{10, 1, 10}, {}});
    world.add_component<RigidBody>(ground, RigidBody{false, 1.0f});
    world.add_component<Collider>(ground, Collider{Vec3{5.0f, 0.5f, 5.0f}});

    const auto lower = world.create_entity();
    world.add_component<Transform>(lower, Transform{Vec3{0, 2, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(lower, RigidBody{true, 1.0f});
    world.add_component<Collider>(lower, Collider{Vec3{0.5f, 0.5f, 0.5f}});

    const auto upper = world.create_entity();
    world.add_component<Transform>(upper, Transform{Vec3{0, 4, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(upper, RigidBody{true, 1.0f});
    world.add_component<Collider>(upper, Collider{Vec3{0.5f, 0.5f, 0.5f}});

    for (int i = 0; i < 180; ++i)
    {
        world.tick(1.0f / 60.0f);
    }
    return seq;
}
} // namespace

TEST_CASE("the contact-event stream is deterministic across runs", "[physics]")
{
    REQUIRE(record_contacts() == record_contacts());
    REQUIRE_FALSE(record_contacts().empty()); // the scenario actually produced contacts
}
```

- [ ] **Step 2: Run — verify it fails**

Run: `cmake --build build/debug -j 2>&1 | tail -20`
Expected: `ContactEvent` undeclared / `PhysicsSystem` does not emit it (the subscriber records
nothing → `begin_seen` false).

- [ ] **Step 3: Add `ContactEvent` + emit it**

In `engine/physics/physics_system.hpp`, add the struct (next to `RaycastHit`, before `class PhysicsSystem`):

```cpp
// A physics contact between two entities, emitted on the World's EventBus each tick.
struct ContactEvent
{
    ContactPhase                 phase;
    mosaic::foundation::EntityID a;
    mosaic::foundation::EntityID b;
};
```

At the end of `update`, after the write-back loop, add:

```cpp
        // 5. Emit contact events (mapped to entities) on the world's event bus.
        for (const BodyContact& c : backend_->contact_events())
        {
            const auto ia = handle_to_entity_.find(c.a);
            const auto ib = handle_to_entity_.find(c.b);
            if (ia == handle_to_entity_.end() || ib == handle_to_entity_.end())
            {
                continue;
            }
            world.events().emit<ContactEvent>(ContactEvent{c.phase, ia->second, ib->second});
        }
```

- [ ] **Step 4: Run — verify pass**

Run: `cmake --build build/debug -j && ctest --test-dir build/debug --output-on-failure -R "emits a ContactEvent|contact-event stream is deterministic"`
Expected: both PASS.

- [ ] **Step 5: Full suite + commit**

Run: `ctest --test-dir build/debug --output-on-failure`
Expected: all PASS.

```bash
git add engine/physics/physics_system.hpp tests/unit/physics/physics_contacts_test.cpp
git commit -m "physics P5: emit ContactEvent (entities) on the EventBus (#37)"
```

---

## Task 3: Console payoff — log contacts

**Files:**
- Modify: `app/run/demo_scene.hpp`

**Interfaces:**
- Consumes: `ContactEvent`/`ContactPhase` (Task 2), `World::events()`, #33 logging.

- [ ] **Step 1: Subscribe + log in the demo**

In `app/run/demo_scene.hpp`, add the include:

```cpp
#include "foundation/logging/log.hpp"
```

In `build_demo_scene`, right after `world.add_system<physics::PhysicsSystem>(...)`, add:

```cpp
    // Log physics contacts so they show in the editor Console (#33 / 37e) and on stderr.
    world.events().subscribe<physics::ContactEvent>(
        [](const physics::ContactEvent& c)
        {
            mosaic::foundation::log::info(
                "physics", "contact {}: entity {} <-> entity {}",
                c.phase == physics::ContactPhase::Begin ? "begin" : "end", c.a.index(), c.b.index());
        });
```

- [ ] **Step 2: Build**

Run: `cmake --build build/debug -j 2>&1 | tail -5`
Expected: builds clean.

- [ ] **Step 3: Functional verification (contacts actually log)**

Run: `ctest --test-dir build/debug --output-on-failure`
Expected: all PASS.

Run: `./build/debug/app/mosaic run --window --ticks 120 2>/tmp/p5.err 1>/dev/null; echo "exit=$? validation=$(grep -c ERROR /tmp/p5.err) contacts=$(grep -c 'contact begin' /tmp/p5.err)"`
Expected: `exit=0`, `validation=0`, and `contacts` **> 0** — the demo bodies land and log
`[INFO] physics: contact begin: …` to stderr (the CLI ConsoleSink).

Run: `./build/debug/app/mosaic editor --ticks 180 2>&1 | grep -c ERROR; echo "editor exit=$?"`
Expected: `0` validation errors, `exit=0`. In the editor, the **Console panel** shows the
`contact begin: …` lines as the bodies land (maintainer confirms visually).

- [ ] **Step 4: Commit**

```bash
git add app/run/demo_scene.hpp
git commit -m "physics P5: log contacts into the Console (editor + mosaic run) (#37)"
```

---

## Task 4: Update the build handoff (next-steps)

**Files:**
- Modify: `docs/project/next-steps.md`

- [ ] **Step 1: Record P5 + mark the physics pillar complete**

Update `docs/project/next-steps.md`: change the "Last updated" line and note **physics P5 is
built** — Box3D contact events → a neutral `BodyContact`, surfaced as a public `ContactEvent`
(entities) on the #11 `EventBus` by `PhysicsSystem`, with a contact-sequence determinism test
and a Console payoff (the editor/`mosaic run` log contacts via #33). State that **the v0.1
physics pillar (P1–P5) is complete**. Note what remains for the whole v0.1 vertical slice:
**Scripting (Python)** and the **tools bookends** (`mosaic create`/`build`, project generator,
asset compiler); plus the deferred follow-ups (sphere/capsule demo meshes; full
record/replay-to-disk; contact manifolds). Link the
[P5 spec](../superpowers/specs/2026-08-24-physics-p5-contacts-design.md) and
[P5 plan](../superpowers/plans/2026-08-24-physics-p5-contacts.md).

- [ ] **Step 2: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "next-steps: physics P5 (contacts) built; v0.1 physics pillar complete"
```

---

## Self-Review

**Spec coverage:**
- Backend enables events + returns `BodyContact` from the last step (spec §Backend contact retrieval) → Task 1. ✓
- `PhysicsSystem` emits `ContactEvent` (entities) on the EventBus (spec §Engine event) → Task 2. ✓
- Console payoff — app subscribes + logs (spec §Console payoff) → Task 3. ✓
- Begin / end / contact-sequence-determinism headless tests (spec §Testability) → Tasks 1 & 2. ✓
- Functional: contacts log + exit 0, 0 validation (spec §Testability) → Task 3 Step 3. ✓

**Placeholder scan:** No TBD/TODO; complete code in each step; commands have expected output. ✓

**Type consistency:** `ContactPhase { Begin, End }` shared across Tasks 1/2/3; `BodyContact { phase, a(BodyHandle), b }` (Task 1) drained in Task 2; `ContactEvent { phase, a(EntityID), b }` (Task 2) consumed in Task 3; `contact_events() -> std::vector<BodyContact>` matches the fake stub and the Box3D impl; `handle_to_entity_` (P4) reused for the handle→entity map. ✓

**One risk flagged (not blocking):** the contact-firing rule — if Box3D requires *both* shapes
to enable events (static can't), the box↔ground begin may not fire; Task 1 Step 6 says to fall
back to a two-dynamic-box begin assertion. The determinism test uses a box-on-ground + a
stacked dynamic box, so it exercises a dynamic-dynamic contact regardless.
