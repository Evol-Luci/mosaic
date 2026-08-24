# Physics P3 — Primitive Shapes + Stacking Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add sphere + capsule collider shapes to the physics system (box exists) and prove stacking, so varied shapes settle onto the ground deterministically.

**Architecture:** A tagged neutral `Shape` (box/sphere/capsule) on the backend interface, mapped to `b3*` shapes; reflected `SphereCollider`/`CapsuleCollider` components (P2's `Collider` stays box); the `PhysicsSystem` selects the shape from whichever collider component an entity has. All headless + deterministic.

**Tech Stack:** C++20, Box3D (`b3CreateSphereShape`/`b3CreateCapsuleShape`), `engine/physics`, Catch2, CMake.

## Global Constraints

- **C++20**; header-only in `engine/physics/` (like P1/P2).
- **Tier contract:** `Shape`/colliders/`PhysicsSystem` stay neutral in `engine/physics/`; `b3Sphere`/`b3Capsule` mapping stays in `modules/physics_box3d`. `b3*` never leaves the module.
- **Determinism:** one fixed `step(dt, substeps)` per tick, single-threaded backend — unchanged.
- **"Plane" is a flat box** (Box3D has no plane primitive); no new plane collider.
- Reflected-property surface: the new colliders are ordinary reflected components.
- No new third-party deps. Catch2 test names must not start with `--`.
- **Build/test:** `cmake --build build/debug -j` · `ctest --test-dir build/debug --output-on-failure`.

## Verified Box3D API (pinned commit)

- `b3Sphere { b3Vec3 center; float radius; }`; `b3ShapeId b3CreateSphereShape(b3BodyId, const b3ShapeDef*, const b3Sphere*)`.
- `b3Capsule { b3Vec3 center1; b3Vec3 center2; float radius; }`; `b3ShapeId b3CreateCapsuleShape(b3BodyId, const b3ShapeDef*, const b3Capsule*)`.
- Box (P1): `b3BoxHull b3MakeBoxHull(float,float,float)` + `b3CreateHullShape(id, &sd, &hull.base)`.

## Current state (verified)

- `engine/physics/physics_backend.hpp`: `struct BoxShape { Vec3 half_extents; }`; `BodyDef.shape` is `BoxShape`. Referenced by name only in `tests/unit/physics/physics_backend_test.cpp`.
- `modules/physics_box3d/box3d_backend.cpp` `create_body` builds the box hull from `def.shape.half_extents`.
- `engine/physics/physics_components.hpp`: `Collider { Vec3 half_extents; }` (box).
- `engine/physics/physics_system.hpp`: create loop looks up `Collider` only.

---

## File Structure

- `engine/physics/physics_backend.hpp` — **modify.** `BoxShape` → tagged `Shape` + `ShapeKind`.
- `modules/physics_box3d/box3d_backend.cpp` — **modify.** Switch `create_body` on `Shape.kind`.
- `engine/physics/physics_components.hpp` — **modify.** Add `SphereCollider`, `CapsuleCollider`.
- `engine/reflection/builtin_components.hpp` — **modify.** Register the two.
- `engine/physics/physics_system.hpp` — **modify.** `shape_for` selection over collider components.
- `app/run/demo_scene.hpp` — **modify.** Add a sphere, a capsule, and a box stack.
- `tests/unit/physics/physics_backend_test.cpp` — **modify.** `Shape` defaults.
- `tests/unit/physics/falling_box_test.cpp` — **modify.** Backend sphere/capsule smoke.
- `tests/unit/physics/physics_components_test.cpp` — **modify.** New colliders round-trip + registered.
- `tests/unit/physics/physics_system_test.cpp` — **modify.** ECS sphere-rest, box-stack, capsule-settle.

---

## Task 1: Tagged neutral Shape + backend mapping

**Files:**
- Modify: `engine/physics/physics_backend.hpp`
- Modify: `modules/physics_box3d/box3d_backend.cpp`
- Modify: `tests/unit/physics/physics_backend_test.cpp`
- Modify: `tests/unit/physics/falling_box_test.cpp`

**Interfaces:**
- Produces: `mosaic::engine::physics::ShapeKind { Box, Sphere, Capsule }`; `Shape { ShapeKind kind; Vec3 half_extents; float radius; float half_height; }`; `BodyDef.shape` is now `Shape`. The backend maps `kind` to a `b3*` shape.

- [ ] **Step 1: Update the backend value-type test (defaults for `Shape`) + add sphere/capsule smoke**

In `tests/unit/physics/physics_backend_test.cpp`, replace the `BoxShape` usage:

```cpp
using mosaic::engine::physics::Shape;
using mosaic::engine::physics::ShapeKind;
```

and, inside `TEST_CASE("physics value-type defaults", ...)`, replace the `BoxShape shape;` block with:

```cpp
    Shape shape;
    REQUIRE(shape.kind == ShapeKind::Box);
    REQUIRE(shape.half_extents.x == Approx(0.5f));
    REQUIRE(shape.radius == Approx(0.5f));
    REQUIRE(shape.half_height == Approx(0.5f));
```

In `tests/unit/physics/falling_box_test.cpp`, add (after the existing `using` block) the
`ShapeKind` import and two backend smoke cases:

```cpp
using mosaic::engine::physics::ShapeKind;

TEST_CASE("a sphere body falls under gravity", "[physics]")
{
    auto      backend = create_box3d_backend(WorldDef{});
    BodyDef   d;
    d.type = BodyType::Dynamic;
    d.position = Vec3{0.0f, 4.0f, 0.0f};
    d.shape.kind = ShapeKind::Sphere;
    d.shape.radius = 0.5f;
    const BodyHandle h = backend->create_body(d);
    const float y0 = backend->body_pose(h).position.y;
    for (int i = 0; i < 10; ++i) { backend->step(1.0f / 60.0f, 4); }
    REQUIRE(backend->body_pose(h).position.y < y0);
}

TEST_CASE("a capsule body falls under gravity", "[physics]")
{
    auto      backend = create_box3d_backend(WorldDef{});
    BodyDef   d;
    d.type = BodyType::Dynamic;
    d.position = Vec3{0.0f, 4.0f, 0.0f};
    d.shape.kind = ShapeKind::Capsule;
    d.shape.radius = 0.5f;
    d.shape.half_height = 0.5f;
    const BodyHandle h = backend->create_body(d);
    const float y0 = backend->body_pose(h).position.y;
    for (int i = 0; i < 10; ++i) { backend->step(1.0f / 60.0f, 4); }
    REQUIRE(backend->body_pose(h).position.y < y0);
}
```

- [ ] **Step 2: Run — verify it fails to compile**

Run: `cmake --build build/debug -j 2>&1 | tail -20`
Expected: errors — `Shape`/`ShapeKind` not declared; `BoxShape` no longer named.

- [ ] **Step 3: Replace `BoxShape` with the tagged `Shape`**

In `engine/physics/physics_backend.hpp`, replace the `BoxShape` struct and the `BodyDef.shape`
field:

```cpp
enum class ShapeKind { Box, Sphere, Capsule };

struct Shape
{
    ShapeKind                      kind = ShapeKind::Box;
    mosaic::foundation::math::Vec3 half_extents{0.5f, 0.5f, 0.5f}; // box
    float                          radius = 0.5f;                  // sphere / capsule
    float                          half_height = 0.5f;             // capsule half-length (local Y)
};
```

and in `struct BodyDef`, change `BoxShape shape{};` to `Shape shape{};`.

- [ ] **Step 4: Switch the backend on `kind`**

In `modules/physics_box3d/box3d_backend.cpp` `create_body`, replace the three shape lines
(the `const Vec3 h = def.shape.half_extents;` / `b3MakeBoxHull` / `b3CreateHullShape` block)
with a switch:

```cpp
        b3ShapeDef sd = b3DefaultShapeDef();
        sd.density = def.density;
        switch (def.shape.kind)
        {
        case ep::ShapeKind::Box:
        {
            const Vec3 h = def.shape.half_extents;
            b3BoxHull  hull = b3MakeBoxHull(h.x, h.y, h.z);
            b3CreateHullShape(id, &sd, &hull.base);
            break;
        }
        case ep::ShapeKind::Sphere:
        {
            b3Sphere sphere{};
            sphere.center = b3Vec3{0.0f, 0.0f, 0.0f};
            sphere.radius = def.shape.radius;
            b3CreateSphereShape(id, &sd, &sphere);
            break;
        }
        case ep::ShapeKind::Capsule:
        {
            b3Capsule capsule{};
            capsule.center1 = b3Vec3{0.0f, -def.shape.half_height, 0.0f};
            capsule.center2 = b3Vec3{0.0f, def.shape.half_height, 0.0f};
            capsule.radius = def.shape.radius;
            b3CreateCapsuleShape(id, &sd, &capsule);
            break;
        }
        }
```

Note the existing code created `b3ShapeDef sd` before the box hull; ensure there is now
exactly one `sd` declaration (the one above) and remove the old duplicate `b3ShapeDef sd =
b3DefaultShapeDef(); sd.density = def.density;` lines if they remain outside the switch.

- [ ] **Step 5: Run — verify pass**

Run: `cmake --build build/debug -j && ctest --test-dir build/debug --output-on-failure -R "value-type defaults|sphere body falls|capsule body falls|dynamic body falls|rests on the ground"`
Expected: all PASS (Shape defaults; sphere + capsule + box smoke; the P1 falling-box still rests).

- [ ] **Step 6: Commit**

```bash
git add engine/physics/physics_backend.hpp modules/physics_box3d/box3d_backend.cpp tests/unit/physics/physics_backend_test.cpp tests/unit/physics/falling_box_test.cpp
git commit -m "physics P3: tagged neutral Shape + Box3D sphere/capsule mapping (#37)"
```

---

## Task 2: SphereCollider + CapsuleCollider components

**Files:**
- Modify: `engine/physics/physics_components.hpp`
- Modify: `engine/reflection/builtin_components.hpp`
- Modify: `tests/unit/physics/physics_components_test.cpp`

**Interfaces:**
- Produces: reflected `SphereCollider { float radius; }`, `CapsuleCollider { float radius; float half_height; }`; registered as `"SphereCollider"`/`"CapsuleCollider"`.

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/physics/physics_components_test.cpp`:

```cpp
using mosaic::engine::physics::CapsuleCollider;
using mosaic::engine::physics::SphereCollider;

TEST_CASE("sphere/capsule collider defaults + round-trip", "[physics]")
{
    SphereCollider sph;
    REQUIRE(sph.radius == Approx(0.5f));
    CapsuleCollider cap;
    REQUIRE(cap.radius == Approx(0.5f));
    REQUIRE(cap.half_height == Approx(0.5f));

    SphereCollider s_in{1.5f};
    SphereCollider s_out;
    mosaic::foundation::serialization::from_value(
        mosaic::foundation::serialization::to_value(s_in), s_out);
    REQUIRE(s_out.radius == Approx(1.5f));

    CapsuleCollider c_in{0.3f, 1.2f};
    CapsuleCollider c_out;
    mosaic::foundation::serialization::from_value(
        mosaic::foundation::serialization::to_value(c_in), c_out);
    REQUIRE(c_out.half_height == Approx(1.2f));
}

TEST_CASE("sphere/capsule colliders are registered as builtins", "[physics]")
{
    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    REQUIRE(types.has("SphereCollider"));
    REQUIRE(types.has("CapsuleCollider"));
}
```

- [ ] **Step 2: Run — verify it fails**

Run: `cmake --build build/debug -j 2>&1 | tail -20`
Expected: `SphereCollider`/`CapsuleCollider` not members of `mosaic::engine::physics`.

- [ ] **Step 3: Add the components**

In `engine/physics/physics_components.hpp`, add after `Collider`:

```cpp
// A sphere collider.
struct SphereCollider
{
    float radius = 0.5f;
};

// A capsule collider (radius + half-length along local Y).
struct CapsuleCollider
{
    float radius = 0.5f;
    float half_height = 0.5f;
};
```

and their reflect functions (after the `Collider` reflect):

```cpp
template <class V>
void reflect(V& visitor, SphereCollider& s)
{
    visitor.member("radius", s.radius);
}

template <class V>
void reflect(V& visitor, CapsuleCollider& c)
{
    visitor.member("radius", c.radius);
    visitor.member("half_height", c.half_height);
}
```

- [ ] **Step 4: Register them**

In `engine/reflection/builtin_components.hpp`, after the `Collider` registration:

```cpp
    types.register_component<mosaic::engine::physics::SphereCollider>(registry, "SphereCollider");
    types.register_component<mosaic::engine::physics::CapsuleCollider>(registry, "CapsuleCollider");
```

- [ ] **Step 5: Run — verify pass**

Run: `cmake --build build/debug -j && ctest --test-dir build/debug --output-on-failure -R "sphere/capsule collider|registered as builtins"`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add engine/physics/physics_components.hpp engine/reflection/builtin_components.hpp tests/unit/physics/physics_components_test.cpp
git commit -m "physics P3: reflected SphereCollider + CapsuleCollider components (#37)"
```

---

## Task 3: PhysicsSystem shape selection + ECS tests

**Files:**
- Modify: `engine/physics/physics_system.hpp`
- Modify: `tests/unit/physics/physics_system_test.cpp`

**Interfaces:**
- Consumes: `Shape`/`ShapeKind` (Task 1), the three collider components (Task 2), `PhysicsSystem` (P2).

- [ ] **Step 1: Write the failing ECS tests**

Append to `tests/unit/physics/physics_system_test.cpp`:

```cpp
#include <cmath>

#include "engine/physics/physics_components.hpp"

using mosaic::engine::physics::CapsuleCollider;
using mosaic::engine::physics::SphereCollider;

namespace
{
// Drop one dynamic entity (collider added by add_collider) at y=4 onto a static ground
// (top y=0.5); tick `ticks` times; return its final y (and, if given, the last-step delta).
template <class F>
float drop_entity(F&& add_collider, int ticks, float* last_delta = nullptr)
{
    using namespace mosaic::engine::physics;
    World world;
    world.add_system<PhysicsSystem>(mosaic::physics::box3d::create_box3d_backend(WorldDef{}));

    const auto ground = world.create_entity();
    world.add_component<Transform>(ground,
        Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{10, 1, 10}, {}});
    world.add_component<RigidBody>(ground, RigidBody{false, 1.0f});
    world.add_component<Collider>(ground, Collider{Vec3{5.0f, 0.5f, 5.0f}});

    const auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{Vec3{0, 4, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(e, RigidBody{true, 1.0f});
    add_collider(world, e);

    float y = 4.0f, prev = 4.0f;
    for (int i = 0; i < ticks; ++i)
    {
        prev = y;
        world.tick(1.0f / 60.0f);
        y = world.get_component<Transform>(e)->position.y;
    }
    if (last_delta != nullptr) { *last_delta = std::fabs(y - prev); }
    return y;
}
} // namespace

TEST_CASE("a sphere entity rests on the ground", "[physics]")
{
    float       d = 1.0f;
    const float y = drop_entity(
        [](World& w, mosaic::foundation::EntityID e)
        { w.add_component<SphereCollider>(e, SphereCollider{0.5f}); }, 120, &d);
    REQUIRE(y > 0.9f); // ground top (0.5) + radius (0.5)
    REQUIRE(y < 1.1f);
    REQUIRE(d < 1e-3f); // settled
}

TEST_CASE("a capsule entity falls and settles above the ground", "[physics]")
{
    float       d = 1.0f;
    const float y = drop_entity(
        [](World& w, mosaic::foundation::EntityID e)
        { w.add_component<CapsuleCollider>(e, CapsuleCollider{0.5f, 0.5f}); }, 180, &d);
    REQUIRE(y < 4.0f);  // it fell
    REQUIRE(y > 0.4f);  // above the ground
    REQUIRE(d < 1e-3f); // settled
}

TEST_CASE("shape physics is deterministic across runs", "[physics]")
{
    auto sphere = [](World& w, mosaic::foundation::EntityID e)
    { w.add_component<SphereCollider>(e, SphereCollider{0.5f}); };
    REQUIRE(drop_entity(sphere, 120) == drop_entity(sphere, 120));
}

TEST_CASE("a stack of two boxes rests stably", "[physics]")
{
    using namespace mosaic::engine::physics;
    World world;
    world.add_system<PhysicsSystem>(mosaic::physics::box3d::create_box3d_backend(WorldDef{}));

    const auto ground = world.create_entity();
    world.add_component<Transform>(ground,
        Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{10, 1, 10}, {}});
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

    for (int i = 0; i < 240; ++i)
    {
        world.tick(1.0f / 60.0f);
    }
    const float ly = world.get_component<Transform>(lower)->position.y;
    const float uy = world.get_component<Transform>(upper)->position.y;
    REQUIRE(ly > 0.85f); // bottom box ~= 1.0
    REQUIRE(ly < 1.15f);
    REQUIRE(uy > 1.85f); // top box ~= 2.0
    REQUIRE(uy < 2.15f);
    REQUIRE(uy > ly);    // did not collapse/interpenetrate
}
```

- [ ] **Step 2: Run — verify it fails**

Run: `cmake --build build/debug -j 2>&1 | tail -20`
Expected: the sphere test fails an assertion (the system still only reads `Collider`, so a
`SphereCollider`-only entity gets **no body** and never falls — `y` stays 4.0), or a compile
error if `SphereCollider` isn't yet reachable from the system.

- [ ] **Step 3: Add `shape_for` selection to the system**

In `engine/physics/physics_system.hpp`, add `#include`s are already covered (it includes
`physics_components.hpp` and `physics_backend.hpp`). Add a private static helper (inside the
class, after the members or before `update`) :

```cpp
    static bool shape_for(World& world, mosaic::foundation::EntityID e, Shape& out)
    {
        if (const Collider* c = world.get_component<Collider>(e))
        {
            out = Shape{ShapeKind::Box, c->half_extents, 0.0f, 0.0f};
            return true;
        }
        if (const SphereCollider* s = world.get_component<SphereCollider>(e))
        {
            out = Shape{ShapeKind::Sphere, {}, s->radius, 0.0f};
            return true;
        }
        if (const CapsuleCollider* c = world.get_component<CapsuleCollider>(e))
        {
            out = Shape{ShapeKind::Capsule, {}, c->radius, c->half_height};
            return true;
        }
        return false;
    }
```

Replace the body-creation lambda's collider lookup + `BodyDef` assembly with:

```cpp
        world.for_each<RigidBody>(
            [&](mosaic::foundation::EntityID e, RigidBody& rb)
            {
                if (bodies_.find(e) != bodies_.end())
                {
                    return;
                }
                const transform::Transform* t = world.get_component<transform::Transform>(e);
                if (t == nullptr)
                {
                    return;
                }
                Shape shape;
                if (!shape_for(world, e, shape))
                {
                    return;
                }
                BodyDef def;
                def.type = rb.dynamic ? BodyType::Dynamic : BodyType::Static;
                def.position = t->position;
                def.shape = shape;
                def.density = rb.density;
                bodies_[e] = backend_->create_body(def);
            });
```

Extend the access declaration in the constructor:

```cpp
        access_.reads<RigidBody>()
            .reads<Collider>()
            .reads<SphereCollider>()
            .reads<CapsuleCollider>()
            .writes<transform::Transform>();
```

- [ ] **Step 4: Run — verify pass**

Run: `cmake --build build/debug -j && ctest --test-dir build/debug --output-on-failure -R "sphere entity rests|capsule entity falls|shape physics is deterministic|stack of two boxes"`
Expected: all PASS. If "rests"/"stack" land just outside the bands (solver slop), widen to
bracket the observed values; the settled + `uy > ly` + determinism assertions must hold.

- [ ] **Step 5: Full suite + commit**

Run: `ctest --test-dir build/debug --output-on-failure`
Expected: all PASS.

```bash
git add engine/physics/physics_system.hpp tests/unit/physics/physics_system_test.cpp
git commit -m "physics P3: PhysicsSystem selects box/sphere/capsule; stacking test (#37)"
```

---

## Task 4: Demo — sphere, capsule, and a box stack

**Files:**
- Modify: `app/run/demo_scene.hpp`

**Interfaces:**
- Consumes: `SphereCollider`/`CapsuleCollider` (Task 2), `RigidBody`/`Collider` (P2).

- [ ] **Step 1: Add the extra bodies to the demo**

In `app/run/demo_scene.hpp`, after the existing dynamic `box` entity block (and before the
lights), add a sphere, a capsule, and a second stacked box. All use the cube mesh as a
visual stand-in (the *physics* is the point; distinct shape meshes are a later rendering
nicety):

```cpp
    // A sphere collider dropping beside the box (cube mesh is a visual stand-in).
    const auto sphere = world.create_entity();
    world.add_component<transform::Transform>(sphere,
        transform::Transform{Vec3{-2, 4, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<transform::WorldTransform>(sphere,
        transform::WorldTransform{Mat4::translation(Vec3{-2, 4, 0})});
    world.add_component<render::MeshRenderer>(sphere, render::MeshRenderer{cube});
    world.add_component<render::Material>(sphere, render::Material{Vec3{1, 1, 1}, tex_id});
    world.add_component<physics::RigidBody>(sphere, physics::RigidBody{true, 1.0f});
    world.add_component<physics::SphereCollider>(sphere, physics::SphereCollider{0.5f});

    // A capsule collider dropping on the other side.
    const auto capsule = world.create_entity();
    world.add_component<transform::Transform>(capsule,
        transform::Transform{Vec3{2, 4, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<transform::WorldTransform>(capsule,
        transform::WorldTransform{Mat4::translation(Vec3{2, 4, 0})});
    world.add_component<render::MeshRenderer>(capsule, render::MeshRenderer{cube});
    world.add_component<render::Material>(capsule, render::Material{Vec3{1, 1, 1}, tex_id});
    world.add_component<physics::RigidBody>(capsule, physics::RigidBody{true, 1.0f});
    world.add_component<physics::CapsuleCollider>(capsule, physics::CapsuleCollider{0.5f, 0.5f});

    // A second box stacked above the first (drops onto it -> a 2-box stack).
    const auto box2 = world.create_entity();
    world.add_component<transform::Transform>(box2,
        transform::Transform{Vec3{0, 6, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<transform::WorldTransform>(box2,
        transform::WorldTransform{Mat4::translation(Vec3{0, 6, 0})});
    world.add_component<render::MeshRenderer>(box2, render::MeshRenderer{cube});
    world.add_component<render::Material>(box2, render::Material{Vec3{1, 1, 1}, tex_id});
    world.add_component<physics::RigidBody>(box2, physics::RigidBody{true, 1.0f});
    world.add_component<physics::Collider>(box2, physics::Collider{Vec3{0.5f, 0.5f, 0.5f}});
```

- [ ] **Step 2: Build**

Run: `cmake --build build/debug -j 2>&1 | tail -5`
Expected: builds clean.

- [ ] **Step 3: Full suite + functional verification**

Run: `ctest --test-dir build/debug --output-on-failure`
Expected: all PASS.

Run: `./build/debug/app/mosaic run --window --ticks 90 2>&1 | grep -c ERROR; echo "run exit=$?"`
Run: `./build/debug/app/mosaic editor --ticks 180 2>&1 | grep -c ERROR; echo "editor exit=$?"`
Expected: `0` validation errors and `exit=0` for both — the box, sphere, capsule, and the
stacked second box all drop and settle on the ground (maintainer confirms visually).

- [ ] **Step 4: Commit**

```bash
git add app/run/demo_scene.hpp
git commit -m "physics P3: demo drops a sphere, capsule, and a box stack (#37)"
```

---

## Task 5: Update the build handoff (next-steps)

**Files:**
- Modify: `docs/project/next-steps.md`

- [ ] **Step 1: Record P3**

Update `docs/project/next-steps.md`: change the "Last updated" line and note **physics P3 is
built** — a tagged neutral `Shape` (box/sphere/capsule) mapped to Box3D shapes; reflected
`SphereCollider`/`CapsuleCollider` (P2's `Collider` stays box); the `PhysicsSystem` selects
the shape per entity; the demo drops a sphere, capsule, and a box stack; headless tests cover
sphere rest, box-stack stability, and capsule settle, all deterministic. Note "plane" = flat
box (Box3D has no plane primitive). **P4 (raycasts)** is next, then P5 (contact events +
record/replay). Link the
[P3 spec](../superpowers/specs/2026-08-24-physics-p3-shapes-design.md) and
[P3 plan](../superpowers/plans/2026-08-24-physics-p3-shapes.md).

- [ ] **Step 2: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "next-steps: physics P3 (shapes + stacking) built; next is P4"
```

---

## Self-Review

**Spec coverage:**
- Tagged neutral `Shape` + backend mapping (spec §Neutral shape, §Box3D mapping) → Task 1. ✓
- Reflected `SphereCollider`/`CapsuleCollider` + registration (spec §Reflected colliders) → Task 2. ✓
- `PhysicsSystem` shape selection (spec §PhysicsSystem shape selection) → Task 3. ✓
- Sphere-rest / box-stack / capsule-settle / determinism headless tests (spec §Testability) → Tasks 1 (backend smoke) + 3 (ECS). ✓
- Demo sphere/capsule/stack (spec §Demo) → Task 4. ✓
- "Plane" = flat box (spec §Scope) → Global Constraints + Task 5 note. ✓

**Placeholder scan:** No TBD/TODO; complete code in each step; commands have expected output. ✓

**Type consistency:** `Shape { ShapeKind kind; Vec3 half_extents; float radius; float half_height; }` used identically across Tasks 1/3; aggregate inits `Shape{kind, half_extents, radius, half_height}` match the member order; `SphereCollider{radius}` / `CapsuleCollider{radius, half_height}` consistent across Tasks 2/3/4; `create_box3d_backend`/`PhysicsSystem` signatures unchanged from P1/P2. ✓

**One risk flagged (not blocking):** the capsule's exact rest height depends on whether an
upright Y-capsule tips — so its test asserts fell + settled + above-ground (not an exact y),
and the demo capsule is a cube visual over a capsule collider (a known visual/collider
mismatch, noted). Rest tolerances for sphere/stack may need a small widen to bracket Box3D's
observed slop; the settled/ordering/determinism assertions are load-bearing.
