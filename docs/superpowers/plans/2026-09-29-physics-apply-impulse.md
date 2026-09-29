# Physics — Apply Impulse Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the engine its first way to impart momentum to a simulated body, and use it to nudge demo objects on click.

**Architecture:** A new pure-virtual `apply_impulse(BodyHandle, impulse, world_point)` on `PhysicsBackend`, implemented by the Box3D module via `b3Body_ApplyLinearImpulse`, exposed entity-level on `PhysicsSystem`. A new app-tier `ClickNudgeSystem` reads the injected pointer, picks with the existing `studio::pick_entity`, and pushes along the camera ray.

**Tech Stack:** C++20, Box3D, Catch2, CMake presets, GLFW (pointer input).

**Spec:** `docs/superpowers/specs/2026-09-29-physics-apply-impulse-design.md`

## Global Constraints

- `apply_impulse` takes a **required** world point. Do not add a center-of-mass overload or an optional point — the spec rejects both by name.
- Box3D's `wake` argument is **always `true`**. A settled body sleeps; without waking it the call is a silent no-op.
- `b3Body_ApplyLinearImpulse(b3BodyId, b3Vec3 impulse, b3Pos point, bool wake)` — `impulse` is **float**-based `b3Vec3`, `point` is **double**-based `b3Pos`. Convert explicitly.
- Handle convention, already used by `set_body_transform`: `bodies_[handle - 1]` is the `b3BodyId`; `handle == 0` is invalid; guard with `if (body == 0 || body > bodies_.size()) return;`.
- Missing data degrades silently, never throws — the existing contract for physics and render components.
- Build: `cmake --build --preset debug -j 20`. Test binary: `./build/debug/tests/mosaic_unit_tests`. Full gate: `ctest --preset debug --output-on-failure -E "threaded ScriptSystem applies script writes|ScriptWorker runs a job on its thread"`.
- **Run only one build at a time.** Concurrent `cmake --build` invocations in this tree corrupt each other's archives and produce a bogus `ranlib: 'libmosaic_app.a': No such file`.
- Never `git add -A`: the working tree has an unrelated modified file, `docs/project/advancement-plans-and-evaluation.md`, that must never be staged. Stage explicit paths.
- Catch2 `TEST_CASE` names must contain **no commas** (a comma is a filter separator), no leading `--`, and no unmatched `[`.

## Review Focus

Input classes the spec implies but which no task's own tests would otherwise exercise. Each gets a test in the task that owns the code.

1. **Clicking the ground** — the ground is a static body and is the largest click target on screen, so this is the *most likely* real click. An impulse to a static body must be a harmless no-op, not a crash and not a moving world. → Task 1.
2. **An entity with a `RigidBody` but no collider component** — `shape_for` fails, so no backend body is ever created and the entity is absent from `bodies_`. A click resolving to it must no-op rather than index a missing handle. → Task 1.
3. **A click in a world with no `PhysicsSystem` or no camera** — `pick_entity` returns a miss in both cases, but the nudge must handle that miss rather than assume a hit. → Task 2.
4. **A zero-magnitude impulse** — strength 0 or a zero direction must leave the body untouched and produce no NaN in its pose. → Task 1.
5. **A click that hits nothing** (empty sky above the scene) — no impulse, no crash, and no state left behind that fires on the next frame. → Task 2.

---

### Task 1: The impulse primitive

Adds the capability to the backend interface, the Box3D implementation, and the entity-level entry point. Self-contained: it compiles and tests standalone, because the new pure virtual and the fake backend's override land together.

**Files:**
- Modify: `engine/physics/physics_backend.hpp`
- Modify: `engine/physics/physics_system.hpp`
- Modify: `modules/physics_box3d/box3d_backend.cpp`
- Test: `tests/unit/physics/physics_system_test.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `PhysicsBackend::apply_impulse(BodyHandle, const Vec3& impulse, const Vec3& world_point)` (pure virtual) and `PhysicsSystem::apply_impulse(EntityID, const Vec3& impulse, const Vec3& world_point)`.

- [ ] **Step 1: Write the failing tests**

In `tests/unit/physics/physics_system_test.cpp`, first extend the existing `FakeBackend` (in the anonymous namespace near the top) so it records impulses. Add these members to the class:

```cpp
    struct ImpulseCall { BodyHandle body; Vec3 impulse; Vec3 point; };
    std::vector<ImpulseCall> impulses;

    void apply_impulse(BodyHandle body, const mosaic::foundation::math::Vec3& impulse,
                       const mosaic::foundation::math::Vec3& world_point) override
    {
        impulses.push_back({body, impulse, world_point});
    }
```

Then append these test cases to the end of the file:

```cpp
TEST_CASE("PhysicsSystem::apply_impulse forwards to the backend with the entity's handle", "[physics]")
{
    World world;
    auto  backend = std::make_unique<FakeBackend>();
    auto* fake    = backend.get();
    world.add_system<PhysicsSystem>(std::move(backend));

    auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{Vec3{1, 2, 3}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(e, RigidBody{true, 1.0f});
    world.add_component<Collider>(e, Collider{Vec3{0.5f, 0.5f, 0.5f}});
    world.tick(1.0f / 60.0f); // creates the body

    auto* physics = world.get_system<PhysicsSystem>();
    REQUIRE(physics != nullptr);
    physics->apply_impulse(e, Vec3{0.0f, 5.0f, 0.0f}, Vec3{1.0f, 2.5f, 3.0f});

    REQUIRE(fake->impulses.size() == 1);
    REQUIRE(fake->impulses[0].body == 1); // first created body
    REQUIRE(fake->impulses[0].impulse.y == Approx(5.0f));
    REQUIRE(fake->impulses[0].point.y == Approx(2.5f));
}

TEST_CASE("PhysicsSystem::apply_impulse is a no-op for an entity with no body", "[physics]")
{
    // Review Focus 2: a RigidBody with no collider component never gets a backend
    // body, so the entity is absent from the handle map. Pushing it must do nothing
    // rather than look up a handle that was never created.
    World world;
    auto  backend = std::make_unique<FakeBackend>();
    auto* fake    = backend.get();
    world.add_system<PhysicsSystem>(std::move(backend));

    auto no_collider = world.create_entity();
    world.add_component<Transform>(no_collider,
                                   Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(no_collider, RigidBody{true, 1.0f});
    auto never_seen = world.create_entity(); // no components at all
    world.tick(1.0f / 60.0f);

    auto* physics = world.get_system<PhysicsSystem>();
    REQUIRE(physics != nullptr);
    physics->apply_impulse(no_collider, Vec3{0, 5, 0}, Vec3{0, 0, 0});
    physics->apply_impulse(never_seen, Vec3{0, 5, 0}, Vec3{0, 0, 0});

    REQUIRE(fake->impulses.empty());
}

TEST_CASE("An off-center impulse makes a real sphere roll and not merely slide", "[physics]")
{
    // THE test for this slice. A position assertion alone passes even if someone
    // swaps in b3Body_ApplyLinearImpulseToCenter; the rotation assertion is the only
    // thing that pins rolling. Do not relax this to "the object moved".
    World world;
    world.add_system<PhysicsSystem>(
        mosaic::physics::box3d::create_box3d_backend(mosaic::engine::physics::WorldDef{}));

    auto ball = world.create_entity();
    world.add_component<Transform>(ball,
                                   Transform{Vec3{0, 5, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(ball, RigidBody{true, 1.0f});
    world.add_component<mosaic::engine::physics::SphereCollider>(
        ball, mosaic::engine::physics::SphereCollider{0.5f});
    world.tick(1.0f / 60.0f);

    auto* physics = world.get_system<PhysicsSystem>();
    REQUIRE(physics != nullptr);
    const auto* before = world.get_component<Transform>(ball);
    REQUIRE(before != nullptr);
    const Quat rot_before = before->rotation;

    // Horizontal push applied at the BOTTOM of the sphere (center y=5, radius 0.5),
    // which is off the center of mass and therefore produces torque.
    physics->apply_impulse(ball, Vec3{2.0f, 0.0f, 0.0f}, Vec3{0.0f, 4.5f, 0.0f});

    for (int i = 0; i < 30; ++i) { world.tick(1.0f / 60.0f); }

    const auto* after = world.get_component<Transform>(ball);
    REQUIRE(after != nullptr);
    REQUIRE(after->position.x > 0.05f); // it moved along the push
    const Quat rot_after = after->rotation;
    const float dq = std::fabs(rot_after.x - rot_before.x) + std::fabs(rot_after.y - rot_before.y)
                   + std::fabs(rot_after.z - rot_before.z) + std::fabs(rot_after.w - rot_before.w);
    REQUIRE(dq > 0.01f); // and it SPUN -- this is the rolling assertion
}

TEST_CASE("A settled sleeping body still responds to an impulse", "[physics]")
{
    // The wake=true guarantee. Box3D sleeps rested bodies, and the demo objects settle
    // within ~2s, so every click a user would realistically try lands on a sleeping
    // body. Without wake=true this is a silent no-op that looks like a broken feature.
    World world;
    world.add_system<PhysicsSystem>(
        mosaic::physics::box3d::create_box3d_backend(mosaic::engine::physics::WorldDef{}));

    auto ground = world.create_entity();
    world.add_component<Transform>(ground,
                                   Transform{Vec3{0, -1, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(ground, RigidBody{false, 1.0f});
    world.add_component<Collider>(ground, Collider{Vec3{20.0f, 0.5f, 20.0f}});

    auto ball = world.create_entity();
    world.add_component<Transform>(ball,
                                   Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(ball, RigidBody{true, 1.0f});
    world.add_component<mosaic::engine::physics::SphereCollider>(
        ball, mosaic::engine::physics::SphereCollider{0.5f});

    // Long enough to land and fall asleep.
    for (int i = 0; i < 400; ++i) { world.tick(1.0f / 60.0f); }
    const float x_asleep = world.get_component<Transform>(ball)->position.x;

    auto* physics = world.get_system<PhysicsSystem>();
    REQUIRE(physics != nullptr);
    physics->apply_impulse(ball, Vec3{3.0f, 0.0f, 0.0f}, Vec3{0.0f, -0.5f, 0.0f});
    for (int i = 0; i < 30; ++i) { world.tick(1.0f / 60.0f); }

    REQUIRE(world.get_component<Transform>(ball)->position.x > x_asleep + 0.05f);
}

TEST_CASE("An impulse on a static body changes nothing", "[physics]")
{
    // Review Focus 1: the ground is static and is the biggest click target on screen,
    // so this is the most likely real click. It must be harmless.
    World world;
    world.add_system<PhysicsSystem>(
        mosaic::physics::box3d::create_box3d_backend(mosaic::engine::physics::WorldDef{}));

    auto ground = world.create_entity();
    world.add_component<Transform>(ground,
                                   Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(ground, RigidBody{false, 1.0f});
    world.add_component<Collider>(ground, Collider{Vec3{5.0f, 0.5f, 5.0f}});
    world.tick(1.0f / 60.0f);

    auto* physics = world.get_system<PhysicsSystem>();
    REQUIRE(physics != nullptr);
    REQUIRE_NOTHROW(physics->apply_impulse(ground, Vec3{50.0f, 50.0f, 0.0f}, Vec3{1.0f, 0.0f, 0.0f}));
    for (int i = 0; i < 10; ++i) { world.tick(1.0f / 60.0f); }

    const auto* t = world.get_component<Transform>(ground);
    REQUIRE(t != nullptr);
    REQUIRE(t->position.x == Approx(0.0f).margin(1e-4));
    REQUIRE(t->position.y == Approx(0.0f).margin(1e-4));
}

TEST_CASE("A zero impulse leaves the body and its pose untouched", "[physics]")
{
    // Review Focus 4: strength 0 or a zero direction must be inert and must not
    // introduce NaN into the pose through a normalize-like path.
    World world;
    world.add_system<PhysicsSystem>(
        mosaic::physics::box3d::create_box3d_backend(mosaic::engine::physics::WorldDef{}));

    auto ball = world.create_entity();
    world.add_component<Transform>(ball,
                                   Transform{Vec3{0, 5, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(ball, RigidBody{true, 1.0f});
    world.add_component<mosaic::engine::physics::SphereCollider>(
        ball, mosaic::engine::physics::SphereCollider{0.5f});
    world.tick(1.0f / 60.0f);

    auto* physics = world.get_system<PhysicsSystem>();
    REQUIRE(physics != nullptr);
    physics->apply_impulse(ball, Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 5.0f, 0.0f});
    world.tick(1.0f / 60.0f);

    const auto* t = world.get_component<Transform>(ball);
    REQUIRE(t != nullptr);
    REQUIRE(std::isfinite(t->position.x));
    REQUIRE(std::isfinite(t->position.y));
    REQUIRE(std::isfinite(t->position.z));
    REQUIRE(t->position.x == Approx(0.0f).margin(1e-4)); // no sideways drift from a zero push
}
```

Add `#include "engine/physics/physics_components.hpp"` only if `SphereCollider` is not already reachable (it is included via that header, which the file already includes — verify before adding). `<cmath>` is already included.

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
```

Expected: a **compile** failure — `'class FakeBackend' ... marked 'override', but does not override` or `no member named 'apply_impulse' in 'PhysicsBackend'`. That is the correct failure for this step.

- [ ] **Step 3: Add the backend interface method**

In `engine/physics/physics_backend.hpp`, inside `class PhysicsBackend`, after `set_body_transform`:

```cpp
    // Apply an instantaneous linear impulse (N*s) to a dynamic body at a world-space
    // point. Applying it away from the center of mass induces torque -- this is what
    // makes a struck sphere roll rather than slide, and is why the point is required
    // rather than optional. A no-op for a static body.
    virtual void apply_impulse(BodyHandle body,
                               const mosaic::foundation::math::Vec3& impulse,
                               const mosaic::foundation::math::Vec3& world_point) = 0;
```

- [ ] **Step 4: Implement it in the Box3D backend**

In `modules/physics_box3d/box3d_backend.cpp`, directly after `set_body_transform`:

```cpp
    void apply_impulse(ep::BodyHandle body, const Vec3& impulse, const Vec3& world_point) override
    {
        if (body == 0 || body > bodies_.size())
        {
            return;
        }
        const b3BodyId id = bodies_[body - 1];
        b3Pos          point{};
        point.x = world_point.x;
        point.y = world_point.y;
        point.z = world_point.z;
        // wake=true is load-bearing: Box3D sleeps rested bodies, and a click on a
        // settled object is the common case. With wake=false this call succeeds,
        // reports nothing, and the object does not move.
        b3Body_ApplyLinearImpulse(id, b3Vec3{impulse.x, impulse.y, impulse.z}, point, true);
    }
```

`b3Pos` is double-based while `b3Vec3` is float-based; assigning the members individually (rather than brace-initializing `b3Pos` from floats) keeps the widening explicit and matches how `set_body_transform` builds its `b3Pos`.

- [ ] **Step 5: Add the entity-level entry point**

In `engine/physics/physics_system.hpp`, immediately after the public `raycast` method:

```cpp
    // Push a dynamic body. `world_point` is where the impulse is applied -- off-center
    // to induce spin. A no-op if the entity has no simulated body (never created, or
    // has no collider component), matching this system's "missing data degrades, does
    // not throw" contract.
    void apply_impulse(mosaic::foundation::EntityID entity,
                       const mosaic::foundation::math::Vec3& impulse,
                       const mosaic::foundation::math::Vec3& world_point)
    {
        const auto it = bodies_.find(entity);
        if (it == bodies_.end())
        {
            return;
        }
        backend_->apply_impulse(it->second, impulse, world_point);
    }
```

- [ ] **Step 6: Run the tests to verify they pass**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
./build/debug/tests/mosaic_unit_tests "[physics]"
```

Expected: PASS, including all pre-existing `[physics]` cases.

If "An off-center impulse makes a real sphere roll and not merely slide" fails on the rotation assertion while the position assertion passes, the impulse is being applied at the center of mass — check that the `point` argument is actually being passed through and is not the body's center.

- [ ] **Step 7: Commit**

```bash
git add engine/physics/physics_backend.hpp engine/physics/physics_system.hpp modules/physics_box3d/box3d_backend.cpp tests/unit/physics/physics_system_test.cpp
git commit -m "feat(physics): apply_impulse at a world point"
```

---

### Task 2: Click-to-nudge

Turns a click into an impulse. Includes the one-line `UiSystem` accessor it needs, because that accessor exists only to serve this feature and a reviewer would judge them together.

**Files:**
- Create: `app/run/click_nudge.hpp`
- Create: `tests/unit/app/click_nudge_test.cpp`
- Modify: `engine/ui/ui_system.hpp`
- Modify: `app/main.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `PhysicsSystem::apply_impulse(EntityID, const Vec3&, const Vec3&)` from Task 1; the existing `studio::pick_entity(World&, float aspect, float ndc_x, float ndc_y) -> physics::RaycastHit{hit, entity, point, normal, distance}`.
- Produces: `mosaic::app::ClickNudgeSystem`, and `mosaic::engine::ui::UiSystem::press_consumed() const -> bool`.

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/app/click_nudge_test.cpp`:

```cpp
#include <memory>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "app/run/click_nudge.hpp"
#include "engine/physics/physics_components.hpp"
#include "engine/physics/physics_system.hpp"
#include "engine/render/camera.hpp"
#include "engine/transform/transform.hpp"
#include "engine/transform/transform_system.hpp"
#include "engine/ui/pointer.hpp"
#include "engine/world/world.hpp"
#include "modules/physics_box3d/box3d_backend.hpp"

using Catch::Approx;
using mosaic::app::ClickNudgeSystem;
using mosaic::engine::World;
using mosaic::engine::physics::Collider;
using mosaic::engine::physics::PhysicsSystem;
using mosaic::engine::physics::RigidBody;
using mosaic::engine::physics::SphereCollider;
using mosaic::engine::render::Camera;
using mosaic::engine::transform::Transform;
using mosaic::engine::ui::PointerState;
using mosaic::foundation::math::Quat;
using mosaic::foundation::math::Vec3;

namespace
{
// A ball at the origin with a camera looking at it from +Z, so a click at the centre
// of the screen (ndc 0,0) casts a ray straight down -Z into the ball.
mosaic::foundation::EntityID build_scene(World& world)
{
    // TransformSystem FIRST: it derives WorldTransform from Transform, and
    // studio::pick_entity reads WorldTransform to find the camera. Without it there is
    // no camera as far as the picker is concerned and every nudge test fails for the
    // wrong reason. Systems tick in registration order, so this must precede the rest.
    world.add_system<mosaic::engine::transform::TransformSystem>();
    world.add_system<PhysicsSystem>(
        mosaic::physics::box3d::create_box3d_backend(mosaic::engine::physics::WorldDef{}));

    auto cam = world.create_entity();
    world.add_component<Transform>(cam,
                                   Transform{Vec3{0, 0, 6}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<Camera>(cam, Camera{});

    auto ball = world.create_entity();
    world.add_component<Transform>(ball,
                                   Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(ball, RigidBody{true, 1.0f});
    world.add_component<SphereCollider>(ball, SphereCollider{0.5f});
    return ball;
}
} // namespace

TEST_CASE("ClickNudgeSystem pushes the clicked body once per press", "[app]")
{
    World world;
    auto  ball = build_scene(world);

    PointerState p;
    p.x = 0.5f; // screen centre
    p.y = 0.5f;
    p.down = false;
    world.add_system<ClickNudgeSystem>([&p] { return p; }, [] { return 1.0f; }, 4.0f);

    world.tick(1.0f / 60.0f); // body created, no click yet
    const float x0 = world.get_component<Transform>(ball)->position.x;
    const float z0 = world.get_component<Transform>(ball)->position.z;

    p.down = true;
    world.tick(1.0f / 60.0f); // press edge -> one impulse
    for (int i = 0; i < 20; ++i) { world.tick(1.0f / 60.0f); } // still held
    const float z_after_hold = world.get_component<Transform>(ball)->position.z;

    // Pushed away from the camera, i.e. along -Z.
    REQUIRE(z_after_hold < z0 - 0.05f);
    // And not sideways: the click was dead centre.
    REQUIRE(world.get_component<Transform>(ball)->position.x == Approx(x0).margin(0.05f));
}

TEST_CASE("ClickNudgeSystem does not re-fire while the button is held", "[app]")
{
    World world;
    auto  ball = build_scene(world);

    PointerState p;
    p.x = 0.5f;
    p.y = 0.5f;
    p.down = false;
    world.add_system<ClickNudgeSystem>([&p] { return p; }, [] { return 1.0f; }, 4.0f);
    world.tick(1.0f / 60.0f);
    const float z0 = world.get_component<Transform>(ball)->position.z;

    p.down = true;
    for (int i = 0; i < 60; ++i) { world.tick(1.0f / 60.0f); } // pressed, then held 1s
    const float z_held = world.get_component<Transform>(ball)->position.z;

    // One impulse of 4 N*s on a ~0.52 kg ball is ~7.7 m/s, so in one second it travels
    // on the order of metres. Re-firing every frame for 60 frames would multiply that
    // by ~60 and put it hundreds of metres away. The bound below passes comfortably for
    // a single impulse and fails unmistakably for a repeating one.
    REQUIRE(z_held < z0 - 0.05f);   // it did move
    REQUIRE(z_held > z0 - 30.0f);   // but nothing like 60 impulses' worth
}

TEST_CASE("ClickNudgeSystem ignores a press the UI already claimed", "[app]")
{
    World world;
    auto  ball = build_scene(world);

    PointerState p;
    p.x = 0.5f;
    p.y = 0.5f;
    p.down = false;
    // ui_claimed returns true: the UI consumed this press.
    world.add_system<ClickNudgeSystem>([&p] { return p; }, [] { return 1.0f; }, 4.0f,
                                       [] { return true; });
    world.tick(1.0f / 60.0f);
    const float z0 = world.get_component<Transform>(ball)->position.z;

    p.down = true;
    for (int i = 0; i < 20; ++i) { world.tick(1.0f / 60.0f); }

    REQUIRE(world.get_component<Transform>(ball)->position.z == Approx(z0).margin(1e-3));
}

TEST_CASE("ClickNudgeSystem does nothing when the click hits empty space", "[app]")
{
    // Review Focus 5: clicking the sky must be inert and must leave no pending state.
    World world;
    auto  ball = build_scene(world);

    PointerState p;
    p.x = 0.02f; // far corner, ray misses the ball entirely
    p.y = 0.02f;
    p.down = false;
    world.add_system<ClickNudgeSystem>([&p] { return p; }, [] { return 1.0f; }, 4.0f);
    world.tick(1.0f / 60.0f);
    const float z0 = world.get_component<Transform>(ball)->position.z;

    p.down = true;
    for (int i = 0; i < 20; ++i) { world.tick(1.0f / 60.0f); }

    REQUIRE(world.get_component<Transform>(ball)->position.z == Approx(z0).margin(1e-3));
}

TEST_CASE("ClickNudgeSystem survives a world with no physics system and no camera", "[app]")
{
    // Review Focus 3: pick_entity returns a miss for both, but the nudge must handle
    // the miss rather than assume a hit.
    World world; // deliberately empty: no PhysicsSystem, no Camera
    PointerState p;
    p.x = 0.5f;
    p.y = 0.5f;
    p.down = true;
    world.add_system<ClickNudgeSystem>([&p] { return p; }, [] { return 1.0f; }, 4.0f);

    REQUIRE_NOTHROW(world.tick(1.0f / 60.0f));
    REQUIRE_NOTHROW(world.tick(1.0f / 60.0f));
}

TEST_CASE("ClickNudgeSystem maps normalized pointer coordinates to NDC with a Y flip", "[app]")
{
    // PointerState is [0,1] top-left origin; pick_entity wants [-1,1] with +y up.
    // Dropping the flip yields a picker that works on the vertically mirrored object.
    REQUIRE(mosaic::app::pointer_to_ndc_x(0.5f) == Approx(0.0f));
    REQUIRE(mosaic::app::pointer_to_ndc_x(1.0f) == Approx(1.0f));
    REQUIRE(mosaic::app::pointer_to_ndc_x(0.0f) == Approx(-1.0f));
    REQUIRE(mosaic::app::pointer_to_ndc_y(0.0f) == Approx(1.0f));  // top of screen = +1
    REQUIRE(mosaic::app::pointer_to_ndc_y(1.0f) == Approx(-1.0f)); // bottom = -1
    REQUIRE(mosaic::app::pointer_to_ndc_y(0.5f) == Approx(0.0f));
}
```

Register the file in `tests/CMakeLists.txt` by adding this line immediately after `unit/app/demo_scene_test.cpp`:

```
  unit/app/click_nudge_test.cpp
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
```

Expected: a **compile** failure — `app/run/click_nudge.hpp: No such file or directory`.

- [ ] **Step 3: Add the UiSystem accessor**

In `engine/ui/ui_system.hpp`, in the `public:` section of `UiSystem` (after `update`):

```cpp
    // True when the most recent press landed on a UiButton, so world-space click
    // handling (picking, nudging) can decline a click the UI already claimed.
    // Valid after this system's update() has run for the frame.
    bool press_consumed() const { return pressed_ != mosaic::foundation::EntityID{}; }
```

- [ ] **Step 4: Write the nudge system**

Create `app/run/click_nudge.hpp`:

```cpp
#pragma once

#include <functional>
#include <utility>

#include "engine/ecs/system.hpp"
#include "engine/physics/physics_system.hpp"
#include "engine/ui/pointer.hpp"
#include "engine/world/world.hpp"
#include "foundation/math/vec3.hpp"
#include "studio/pick.hpp"

namespace mosaic::app
{

// PointerState is normalized [0,1] with a TOP-LEFT origin; studio::pick_entity wants
// NDC in [-1,1] with +y UP. The Y flip is the easy thing to drop, and dropping it
// still "works" -- it just picks the vertically mirrored object.
inline float pointer_to_ndc_x(float px) { return px * 2.0f - 1.0f; }
inline float pointer_to_ndc_y(float py) { return 1.0f - py * 2.0f; }

// Turns a left-click into a physics impulse on whatever body is under the cursor,
// pushed along the camera ray so the contact point is off-centre and the body rolls.
//
// Ordering: this must be registered AFTER UiSystem, so that `ui_claimed` reflects the
// same pointer state this system is reacting to. World systems run in registration
// order, so adding it after UiSystem is sufficient.
class ClickNudgeSystem : public mosaic::engine::ecs::System
{
public:
    ClickNudgeSystem(std::function<mosaic::engine::ui::PointerState()> pointer,
                     std::function<float()>                            aspect,
                     float                                             strength,
                     std::function<bool()> ui_claimed = {})
        : pointer_(std::move(pointer)), aspect_(std::move(aspect)), strength_(strength),
          ui_claimed_(std::move(ui_claimed))
    {
    }

    void update(mosaic::engine::World& world, float /*dt*/) override
    {
        const mosaic::engine::ui::PointerState p =
            pointer_ ? pointer_() : mosaic::engine::ui::PointerState{};
        const bool press_edge = p.down && !was_down_;
        was_down_ = p.down;
        if (!press_edge)
        {
            return;
        }
        if (ui_claimed_ && ui_claimed_())
        {
            return; // the UI owns this click
        }

        const float aspect = aspect_ ? aspect_() : 1.0f;
        const mosaic::engine::physics::RaycastHit hit = mosaic::studio::pick_entity(
            world, aspect, pointer_to_ndc_x(p.x), pointer_to_ndc_y(p.y));
        if (!hit.hit)
        {
            return;
        }

        auto* physics = world.get_system<mosaic::engine::physics::PhysicsSystem>();
        if (physics == nullptr)
        {
            return;
        }

        // Push along the camera ray: from the eye through the clicked point. Because
        // the hit point is off the body's centre, this produces spin as well as
        // translation, which is what makes a struck sphere roll.
        const mosaic::foundation::math::Vec3 dir =
            mosaic::foundation::math::normalize(hit.point - camera_position(world));
        physics->apply_impulse(hit.entity, dir * strength_, hit.point);
    }

private:
    static mosaic::foundation::math::Vec3 camera_position(mosaic::engine::World& world)
    {
        mosaic::foundation::math::Vec3 out{};
        bool                           found = false;
        world.for_each<mosaic::engine::render::Camera>(
            [&](mosaic::foundation::EntityID e, mosaic::engine::render::Camera&)
            {
                if (found) { return; }
                const auto* wt =
                    world.get_component<mosaic::engine::transform::WorldTransform>(e);
                if (wt == nullptr) { return; }
                out = mosaic::foundation::math::Vec3{wt->matrix.m[12], wt->matrix.m[13],
                                                     wt->matrix.m[14]};
                found = true;
            });
        return out;
    }

    std::function<mosaic::engine::ui::PointerState()> pointer_;
    std::function<float()>                            aspect_;
    float                                             strength_ = 1.0f;
    std::function<bool()>                             ui_claimed_;
    bool                                              was_down_ = false;
};

} // namespace mosaic::app
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
./build/debug/tests/mosaic_unit_tests "[app]"
```

Expected: PASS, including all pre-existing `[app]` cases.

If "pushes the clicked body once per press" fails because the ball does not move, check the NDC helpers first: a sign error there aims the ray away from the ball and every nudge test fails identically.

- [ ] **Step 6: Wire it into the windowed run loop**

In `app/main.cpp`, after the `setup_runtime(...)` call that assigns `render_system` (the statement ending `.render_system;`), add:

```cpp
    // Click-to-nudge, windowed runs only. Registered AFTER setup_runtime, which adds
    // UiSystem, so it ticks after the UI and sees the UI's verdict on this press.
    if (glfw_window != nullptr)
    {
        world.add_system<mosaic::app::ClickNudgeSystem>(
            [&glfw_window]() -> mosaic::engine::ui::PointerState
            {
                if (glfw_window == nullptr) { return {}; }
                int ww = 0, wh = 0;
                glfwGetWindowSize(glfw_window, &ww, &wh);
                if (ww <= 0 || wh <= 0) { return {}; }
                double mx = 0.0, my = 0.0;
                glfwGetCursorPos(glfw_window, &mx, &my);
                mosaic::engine::ui::PointerState p;
                p.x = std::clamp(static_cast<float>(mx / ww), 0.0f, 1.0f);
                p.y = std::clamp(static_cast<float>(my / wh), 0.0f, 1.0f);
                p.down = glfwGetMouseButton(glfw_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
                return p;
            },
            [&glfw_window]() -> float
            {
                int fbw = 0, fbh = 0;
                glfwGetFramebufferSize(glfw_window, &fbw, &fbh);
                return (fbh > 0) ? static_cast<float>(fbw) / static_cast<float>(fbh) : 1.0f;
            },
            2.0f,
            [&world]() -> bool
            {
                auto* ui = world.get_system<mosaic::engine::ui::UiSystem>();
                return ui != nullptr && ui->press_consumed();
            });
    }
```

Add `#include "app/run/click_nudge.hpp"` and `#include "engine/ui/ui_system.hpp"` to the includes at the top of `app/main.cpp` if not already present.

The strength `2.0f` is the tuning knob: the demo sphere has radius 0.5 and density 1, so mass is about 0.52 kg and 2.0 N*s gives roughly 3.8 m/s — a firm shove that visibly rolls it without launching it off the ground plane.

- [ ] **Step 7: Build everything and run the full gate**

```bash
cmake --build --preset debug -j 20
```

Wait for this command to **exit** before continuing, then:

```bash
ctest --preset debug --output-on-failure -E "threaded ScriptSystem applies script writes|ScriptWorker runs a job on its thread"
```

Expected: PASS. Those two exclusions are known pre-existing embedded-Python threading flakes.

- [ ] **Step 8: Verify by hand in the real window**

```bash
./build/debug/app/mosaic run --window
```

Expected, with zero Vulkan validation errors on stderr:
- Clicking the red sphere pushes it away from the camera and it **rolls** — the specular highlight stays anchored in space while the surface turns under it.
- Clicking the ground does nothing (it is static).
- Clicking the blue HUD panel in the top-left does **not** also nudge whatever is behind it.
- Holding the mouse button down does not fire repeatedly; each press is one nudge.
- Clicking an object that has been sitting still for a while still moves it. This is the `wake=true` case, and it is the one most likely to be broken while every automated test passes.

Close the window when done.

- [ ] **Step 9: Commit**

```bash
git add app/run/click_nudge.hpp app/main.cpp engine/ui/ui_system.hpp tests/unit/app/click_nudge_test.cpp tests/CMakeLists.txt
git commit -m "feat(app): click to nudge a body along the camera ray"
```
