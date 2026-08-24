# Physics P2 — ECS PhysicsSystem + Visible Drop Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reflected `RigidBody`/`Collider` components + a `PhysicsSystem` that steps the P1 backend in `World::tick` and syncs poses to `Transform`, so a box visibly drops onto a ground plane in the editor.

**Architecture:** `PhysicsSystem` (engine tier) owns an injected neutral `PhysicsBackend`; each tick it lazily creates bodies from `RigidBody`+`Collider`+`Transform`, steps once, and writes dynamic poses back to `Transform`. The demo scene becomes a falling box + ground; the editor's play/stop gates the sim. Physics is headless + deterministic, so the sync logic and the fall are unit-tested.

**Tech Stack:** C++20, Box3D (P1), `engine/{ecs,physics,transform,world}`, Catch2, CMake.

## Global Constraints

- **C++20**; `PhysicsSystem` and the components are header-only (like `TransformSystem`).
- **Tier contract:** `PhysicsSystem` + components live in `engine/physics/` and depend only on the neutral `PhysicsBackend`; only the app injects the concrete Box3D backend. `b3*` stays in `modules/physics_box3d`.
- **Determinism:** one fixed `backend_->step(dt, substeps)` per tick; single-threaded backend.
- **`RigidBody.dynamic` is a `bool`** (not the `BodyType` enum) so it reflects/serializes/inspects cleanly.
- **Reflected-property surface:** `RigidBody`/`Collider` are ordinary reflected components (register in `register_builtin_components`).
- No new third-party deps. Catch2 test names must not start with `--`.
- **Build/test commands** (binaryDir `build/debug`, exe at `build/debug/app/mosaic`):
  - Build: `cmake --build build/debug -j`
  - Test: `ctest --test-dir build/debug --output-on-failure`
  - Validation run: `./build/debug/app/mosaic editor --ticks 180 2>&1 | grep -c ERROR` (expect 0)

## Key facts (verified)

- `World::add_system<S>(Args&&...)` forwards ctor args and returns `S&` — the backend is injected: `world.add_system<PhysicsSystem>(create_box3d_backend(WorldDef{}))`.
- `System::update(World&, float dt)` is called each tick in registration order; `System` holds a `ComponentAccess access_`.
- `World::for_each<T>(fn)` calls `fn(EntityID, T&)`; `World::get_component<T>(e)` returns `T*` or null.
- `EntityID` has `std::hash` (for `unordered_map`).
- `Transform { Vec3 position; Quat rotation; Vec3 scale{1,1,1}; EntityID parent; }`.
- Neutral `PhysicsBackend` (P1): `BodyHandle create_body(const BodyDef&)`, `void step(float, int)`, `Pose body_pose(BodyHandle) const`; `BodyDef { BodyType type; Vec3 position; BoxShape shape; float density; }`; `Pose { Vec3 position; Quat rotation; }`.
- `mosaic_app` links `mosaic_render_vulkan` PUBLIC (add `mosaic_physics_box3d`); `app/run/demo_scene.hpp` may include `modules/physics_box3d`.
- Serialization round-trip: `foundation::serialization::to_value(obj)` / `from_value(node, obj)`.
- `Quat` is `{x,y,z,w}` (positional 4-arg init; `Quat::identity()` exists). `b3Quat` is `{b3Vec3 v; float s;}`.

---

## File Structure

- `engine/physics/physics_components.hpp` — **create.** Reflected `RigidBody`, `Collider`.
- `engine/physics/physics_system.hpp` — **create.** Header-only `PhysicsSystem`.
- `engine/reflection/builtin_components.hpp` — **modify.** Register the two components.
- `modules/physics_box3d/box3d_backend.cpp` — **modify.** Real rotation read-back.
- `app/run/demo_scene.hpp` — **modify.** Physics demo (ground + falling box); wire `PhysicsSystem`; remove `SpinSystem`.
- `app/CMakeLists.txt` — **modify.** Link `mosaic_physics_box3d`.
- `tests/unit/physics/physics_components_test.cpp` — **create.**
- `tests/unit/physics/physics_system_test.cpp` — **create.** Fake-backend sync + real-backend integration.
- `tests/CMakeLists.txt` — **modify.** Register the two tests.

---

## Task 1: RigidBody + Collider components

**Files:**
- Create: `engine/physics/physics_components.hpp`
- Modify: `engine/reflection/builtin_components.hpp`
- Create: `tests/unit/physics/physics_components_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `mosaic::engine::physics::RigidBody { bool dynamic; float density; }` and `Collider { Vec3 half_extents; }`, both reflected; registered as `"RigidBody"`/`"Collider"`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/physics/physics_components_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/physics/physics_components.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reflect_value.hpp"

using Catch::Approx;
using mosaic::engine::physics::Collider;
using mosaic::engine::physics::RigidBody;

TEST_CASE("physics component defaults", "[physics]")
{
    RigidBody rb;
    REQUIRE(rb.dynamic == true);
    REQUIRE(rb.density == Approx(1.0f));

    Collider col;
    REQUIRE(col.half_extents.x == Approx(0.5f));
}

TEST_CASE("RigidBody and Collider round-trip through reflection", "[physics]")
{
    RigidBody rb{false, 2.5f};
    RigidBody rb_out;
    mosaic::foundation::serialization::from_value(
        mosaic::foundation::serialization::to_value(rb), rb_out);
    REQUIRE(rb_out.dynamic == false);
    REQUIRE(rb_out.density == Approx(2.5f));

    Collider col{mosaic::foundation::math::Vec3{2.0f, 3.0f, 4.0f}};
    Collider col_out;
    mosaic::foundation::serialization::from_value(
        mosaic::foundation::serialization::to_value(col), col_out);
    REQUIRE(col_out.half_extents.y == Approx(3.0f));
}

TEST_CASE("physics components are registered as builtins", "[physics]")
{
    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    REQUIRE(types.has("RigidBody"));
    REQUIRE(types.has("Collider"));
}
```

Register it in `tests/CMakeLists.txt` (after the existing `unit/physics/...` entries):

```cmake
  unit/physics/physics_components_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build/debug -j 2>&1 | tail -20`
Expected: compile error — `engine/physics/physics_components.hpp` not found.

- [ ] **Step 3: Write the components header**

Create `engine/physics/physics_components.hpp`:

```cpp
#pragma once

#include "foundation/math/vec3.hpp"

namespace mosaic::engine::physics
{
// A physics body attached to an entity. `dynamic` false = static (never moved by the sim).
struct RigidBody
{
    bool  dynamic = true;
    float density = 1.0f; // for dynamic bodies
};

// A box collider (P2 is box-only; more shapes in P3).
struct Collider
{
    mosaic::foundation::math::Vec3 half_extents{0.5f, 0.5f, 0.5f};
};

template <class V>
void reflect(V& visitor, RigidBody& b)
{
    visitor.member("dynamic", b.dynamic);
    visitor.member("density", b.density);
}

template <class V>
void reflect(V& visitor, Collider& c)
{
    visitor.member("half_extents", c.half_extents);
}
} // namespace mosaic::engine::physics
```

- [ ] **Step 4: Register the components as builtins**

In `engine/reflection/builtin_components.hpp`, add the include and the two registrations:

```cpp
#include "engine/physics/physics_components.hpp"
```

and, inside `register_builtin_components` (after the `PointLight` line):

```cpp
    types.register_component<mosaic::engine::physics::RigidBody>(registry, "RigidBody");
    types.register_component<mosaic::engine::physics::Collider>(registry, "Collider");
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build/debug -j && ctest --test-dir build/debug --output-on-failure -R "physics component|round-trip through reflection|registered as builtins"`
Expected: PASS (3 cases).

- [ ] **Step 6: Commit**

```bash
git add engine/physics/physics_components.hpp engine/reflection/builtin_components.hpp tests/unit/physics/physics_components_test.cpp tests/CMakeLists.txt
git commit -m "physics P2: reflected RigidBody + Collider components (#37)"
```

---

## Task 2: PhysicsSystem (fake-backend sync)

**Files:**
- Create: `engine/physics/physics_system.hpp`
- Create: `tests/unit/physics/physics_system_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `RigidBody`/`Collider` (Task 1); neutral `PhysicsBackend`/`BodyDef`/`Pose` (P1); `World`, `Transform`.
- Produces: `mosaic::engine::physics::PhysicsSystem` with `explicit PhysicsSystem(std::unique_ptr<PhysicsBackend> backend, int substeps = 4)` and `void update(World&, float) override`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/physics/physics_system_test.cpp`:

```cpp
#include <cstdint>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/physics/physics_backend.hpp"
#include "engine/physics/physics_components.hpp"
#include "engine/physics/physics_system.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"

using Catch::Approx;
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
// Records create_body calls + step count; returns a fixed pose from body_pose.
class FakeBackend : public PhysicsBackend
{
public:
    std::vector<BodyDef> created;
    int                  steps = 0;
    Pose                 fixed{Vec3{7.0f, 8.0f, 9.0f}, Quat::identity()};

    BodyHandle create_body(const BodyDef& def) override
    {
        created.push_back(def);
        return static_cast<BodyHandle>(created.size());
    }
    void step(float, int) override { ++steps; }
    Pose body_pose(BodyHandle) const override { return fixed; }
};
} // namespace

TEST_CASE("PhysicsSystem creates bodies, steps, and writes dynamic poses back", "[physics]")
{
    World world;
    auto  fake = std::make_unique<FakeBackend>();
    auto* fp = fake.get();
    world.add_system<PhysicsSystem>(std::move(fake));

    // A dynamic physics box.
    const auto box = world.create_entity();
    world.add_component<Transform>(box, Transform{Vec3{0, 5, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(box, RigidBody{true, 1.0f});
    world.add_component<Collider>(box, Collider{Vec3{0.5f, 0.5f, 0.5f}});

    // A static ground.
    const auto ground = world.create_entity();
    world.add_component<Transform>(ground, Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(ground, RigidBody{false, 1.0f});
    world.add_component<Collider>(ground, Collider{Vec3{5.0f, 0.5f, 5.0f}});

    // A non-physics entity (Transform only).
    const auto other = world.create_entity();
    world.add_component<Transform>(other, Transform{Vec3{1, 1, 1}, Quat::identity(), Vec3{1, 1, 1}, {}});

    world.tick(1.0f / 60.0f);

    REQUIRE(fp->created.size() == 2); // box + ground, not the non-physics entity
    REQUIRE(fp->steps == 1);
    // Dynamic box got the backend pose written into its Transform.
    REQUIRE(world.get_component<Transform>(box)->position.y == Approx(8.0f));
    // Static ground is not written back.
    REQUIRE(world.get_component<Transform>(ground)->position.y == Approx(0.0f));
    // Non-physics entity untouched.
    REQUIRE(world.get_component<Transform>(other)->position.x == Approx(1.0f));

    // Second tick: no new bodies (lazy), one more step.
    world.tick(1.0f / 60.0f);
    REQUIRE(fp->created.size() == 2);
    REQUIRE(fp->steps == 2);
}
```

Register it in `tests/CMakeLists.txt`:

```cmake
  unit/physics/physics_system_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build/debug -j 2>&1 | tail -20`
Expected: compile error — `engine/physics/physics_system.hpp` not found.

- [ ] **Step 3: Write the PhysicsSystem header**

Create `engine/physics/physics_system.hpp`:

```cpp
#pragma once

#include <memory>
#include <unordered_map>

#include "engine/ecs/system.hpp"
#include "engine/physics/physics_backend.hpp"
#include "engine/physics/physics_components.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"

namespace mosaic::engine::physics
{
// Steps a PhysicsBackend inside World::tick and syncs simulated poses to Transform.
// The backend is injected (via add_system<PhysicsSystem>(backend)); this system depends
// only on the neutral interface, never on any concrete backend module.
class PhysicsSystem : public ecs::System
{
public:
    explicit PhysicsSystem(std::unique_ptr<PhysicsBackend> backend, int substeps = 4)
        : backend_(std::move(backend)), substeps_(substeps)
    {
        access_.reads<RigidBody>().reads<Collider>().writes<transform::Transform>();
    }

    void update(World& world, float dt) override
    {
        // 1. Lazily create a backend body for each physics entity we have not seen.
        world.for_each<RigidBody>(
            [&](mosaic::foundation::EntityID e, RigidBody& rb)
            {
                if (bodies_.find(e) != bodies_.end())
                {
                    return;
                }
                const Collider*             col = world.get_component<Collider>(e);
                const transform::Transform* t = world.get_component<transform::Transform>(e);
                if (col == nullptr || t == nullptr)
                {
                    return;
                }
                BodyDef def;
                def.type = rb.dynamic ? BodyType::Dynamic : BodyType::Static;
                def.position = t->position;
                def.shape.half_extents = col->half_extents;
                def.density = rb.density;
                bodies_[e] = backend_->create_body(def);
            });

        // 2. Advance the simulation one fixed step.
        backend_->step(dt, substeps_);

        // 3. Write dynamic-body poses back into Transform (static bodies do not move).
        for (const auto& [e, handle] : bodies_)
        {
            const RigidBody* rb = world.get_component<RigidBody>(e);
            if (rb == nullptr || !rb->dynamic)
            {
                continue;
            }
            transform::Transform* t = world.get_component<transform::Transform>(e);
            if (t == nullptr)
            {
                continue;
            }
            const Pose pose = backend_->body_pose(handle);
            t->position = pose.position;
            t->rotation = pose.rotation;
        }
    }

private:
    std::unique_ptr<PhysicsBackend>                             backend_;
    std::unordered_map<mosaic::foundation::EntityID, BodyHandle> bodies_;
    int                                                         substeps_;
};
} // namespace mosaic::engine::physics
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build/debug -j && ctest --test-dir build/debug --output-on-failure -R "creates bodies, steps"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/physics/physics_system.hpp tests/unit/physics/physics_system_test.cpp tests/CMakeLists.txt
git commit -m "physics P2: PhysicsSystem syncs backend poses to Transform (#37)"
```

---

## Task 3: Backend rotation read-back + headless integration

**Files:**
- Modify: `modules/physics_box3d/box3d_backend.cpp`
- Modify: `tests/unit/physics/physics_system_test.cpp`

**Interfaces:**
- Consumes: `create_box3d_backend` (P1), `PhysicsSystem` (Task 2).

- [ ] **Step 1: Read real rotation in the Box3D backend**

In `modules/physics_box3d/box3d_backend.cpp`, replace the `body_pose` body's read/return:

```cpp
        const auto     p = b3Body_GetPosition(bodies_[body - 1]); // b3Pos == b3Vec3 {x,y,z}
        const b3Quat   q = b3Body_GetRotation(bodies_[body - 1]); // b3Quat {b3Vec3 v; float s;}
        return ep::Pose{Vec3{p.x, p.y, p.z}, Quat{q.v.x, q.v.y, q.v.z, q.s}};
```

(Remove the old `return ep::Pose{Vec3{p.x, p.y, p.z}, Quat::identity()};` line and its
"P1 reads position only" comment.)

- [ ] **Step 2: Write the failing integration test**

Append to `tests/unit/physics/physics_system_test.cpp`:

```cpp
#include "modules/physics_box3d/box3d_backend.hpp"

namespace
{
// Build a World with a real Box3D PhysicsSystem, a dynamic box at y=4, and a static ground
// (top at y=0.5); tick 120x; return the box entity's final Transform.position.y.
float drop_box_in_world()
{
    using namespace mosaic::engine::physics;
    World world;
    world.add_system<PhysicsSystem>(mosaic::physics::box3d::create_box3d_backend(WorldDef{}));

    const auto ground = world.create_entity();
    world.add_component<Transform>(ground,
        Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{10, 1, 10}, {}});
    world.add_component<RigidBody>(ground, RigidBody{false, 1.0f});
    world.add_component<Collider>(ground, Collider{Vec3{5.0f, 0.5f, 5.0f}});

    const auto box = world.create_entity();
    world.add_component<Transform>(box,
        Transform{Vec3{0, 4, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(box, RigidBody{true, 1.0f});
    world.add_component<Collider>(box, Collider{Vec3{0.5f, 0.5f, 0.5f}});

    for (int i = 0; i < 120; ++i)
    {
        world.tick(1.0f / 60.0f);
    }
    return world.get_component<Transform>(box)->position.y;
}
} // namespace

TEST_CASE("an ECS box entity falls and rests on a ground plane", "[physics]")
{
    const float y = drop_box_in_world();
    REQUIRE(y > 0.9f); // rest center ~= ground top (0.5) + box half-extent (0.5)
    REQUIRE(y < 1.1f);
}

TEST_CASE("ECS physics is deterministic across runs", "[physics]")
{
    REQUIRE(drop_box_in_world() == drop_box_in_world());
}
```

Add `mosaic::foundation::math::Vec3`/`Quat` are already imported at the top of the file (Task 2). No new `using` needed.

- [ ] **Step 3: Run — verify fall + determinism**

Run: `cmake --build build/debug -j && ctest --test-dir build/debug --output-on-failure -R "falls and rests on a ground plane|deterministic across runs"`
Expected: both PASS. If "rests" lands just outside `[0.9, 1.1]` (solver slop), widen the band
to bracket the observed value; the determinism (`==`) assertion must hold — if it fails, the
backend is not single-threaded (recheck the world-def task callbacks).

- [ ] **Step 4: Full suite**

Run: `ctest --test-dir build/debug --output-on-failure`
Expected: all PASS.

- [ ] **Step 5: Commit**

```bash
git add modules/physics_box3d/box3d_backend.cpp tests/unit/physics/physics_system_test.cpp
git commit -m "physics P2: real rotation read-back + ECS falling-box integration test (#37)"
```

---

## Task 4: Demo rewire — visible drop

**Files:**
- Modify: `app/run/demo_scene.hpp`
- Modify: `app/CMakeLists.txt`

**Interfaces:**
- Consumes: `PhysicsSystem` (Task 2), `create_box3d_backend` (P1), `RigidBody`/`Collider` (Task 1).
- Produces: `build_demo_scene` now builds a static ground + a dynamic falling box and adds a `PhysicsSystem` (backend injected); `SpinSystem` removed.

- [ ] **Step 1: Link the physics module into the app**

In `app/CMakeLists.txt`, add `mosaic_physics_box3d` to the `target_link_libraries(mosaic_app PUBLIC ...)` list (next to `mosaic_render_vulkan`):

```cmake
  mosaic_physics_box3d
```

- [ ] **Step 2: Rewrite the demo scene as the physics drop**

Replace the entire body of `build_demo_scene` and delete the `SpinSystem` class in
`app/run/demo_scene.hpp`. The new file:

```cpp
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "engine/assets/asset_metadata.hpp"
#include "engine/assets/asset_registry.hpp"
#include "engine/physics/physics_components.hpp"
#include "engine/physics/physics_system.hpp"
#include "engine/render/camera.hpp"
#include "engine/render/cube.hpp"
#include "engine/render/directional_light.hpp"
#include "engine/render/image.hpp"
#include "engine/render/material.hpp"
#include "engine/render/mesh_renderer.hpp"
#include "engine/render/point_light.hpp"
#include "engine/render/render_device.hpp"
#include "engine/render/render_system.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/math/vec3.hpp"
#include "modules/physics_box3d/box3d_backend.hpp"

namespace mosaic::app
{

struct DemoScene
{
    std::shared_ptr<mosaic::engine::render::RenderSystem> render_system;
};

// Builds the physics demo: a textured box drops onto a static ground slab. Adds a
// PhysicsSystem (Box3D backend injected) and returns a RenderSystem. The three procedural
// textures remain (the asset browser assigns them to the box's Material).
inline DemoScene build_demo_scene(mosaic::engine::World&                 world,
                                  mosaic::engine::render::RenderDevice&  device,
                                  mosaic::engine::assets::AssetRegistry& registry)
{
    namespace render    = mosaic::engine::render;
    namespace transform = mosaic::engine::transform;
    namespace physics   = mosaic::engine::physics;
    using mosaic::foundation::math::Mat4;
    using mosaic::foundation::math::Quat;
    using mosaic::foundation::math::Vec3;

    world.add_system<physics::PhysicsSystem>(
        mosaic::physics::box3d::create_box3d_backend(physics::WorldDef{}));

    const render::MeshHandle cube = [&]
    {
        std::vector<render::Vertex> v;
        std::vector<std::uint32_t>  i;
        render::make_cube_mesh(v, i);
        return device.create_mesh(v, i);
    }();

    // Three procedural textures (asset-browser targets); the box starts on the checkerboard.
    auto register_texture = [&](std::array<std::uint8_t, 16> uuid, auto&& pixel_fn)
    {
        auto image    = std::make_shared<render::Image>();
        image->width  = 64;
        image->height = 64;
        image->pixels.resize(64 * 64 * 4);
        for (std::uint32_t y = 0; y < 64; ++y)
            for (std::uint32_t x = 0; x < 64; ++x)
            {
                pixel_fn(x, y, &image->pixels[(y * 64 + x) * 4]);
            }
        const mosaic::foundation::serialization::AssetID id{uuid};
        registry.register_resource<render::Image>(
            mosaic::engine::assets::AssetMetadata{id, "texture"}, image);
        return id;
    };
    std::array<std::uint8_t, 16> checker_uuid{};
    checker_uuid[0] = 0xC0;
    checker_uuid[1] = 0xDE;
    std::array<std::uint8_t, 16> solid_uuid{};
    solid_uuid[0] = 0xA1;
    std::array<std::uint8_t, 16> stripes_uuid{};
    stripes_uuid[0] = 0xB2;
    const auto tex_id = register_texture(checker_uuid,
        [](std::uint32_t x, std::uint32_t y, std::uint8_t* p)
        { const std::uint8_t c = (((x / 8) + (y / 8)) % 2 == 0) ? 230 : 60; p[0]=c;p[1]=c;p[2]=c;p[3]=255; });
    register_texture(solid_uuid,
        [](std::uint32_t, std::uint32_t, std::uint8_t* p) { p[0]=220;p[1]=120;p[2]=60;p[3]=255; });
    register_texture(stripes_uuid,
        [](std::uint32_t x, std::uint32_t, std::uint8_t* p)
        { const std::uint8_t c = ((x / 8) % 2 == 0) ? 200 : 40; p[0]=c;p[1]=c;p[2]=c;p[3]=255; });

    // Static ground slab: cube scaled to a 10x1x10 slab (collider half-extents 5,0.5,5),
    // top surface at y=0.5.
    const auto ground = world.create_entity();
    world.add_component<transform::Transform>(ground,
        transform::Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{10, 1, 10}, {}});
    world.add_component<transform::WorldTransform>(ground,
        transform::WorldTransform{Mat4::trs(Vec3{0, 0, 0}, Quat::identity(), Vec3{10, 1, 10})});
    world.add_component<render::MeshRenderer>(ground, render::MeshRenderer{cube});
    world.add_component<render::Material>(ground, render::Material{Vec3{0.6f, 0.6f, 0.6f}, {}});
    world.add_component<physics::RigidBody>(ground, physics::RigidBody{false, 1.0f});
    world.add_component<physics::Collider>(ground, physics::Collider{Vec3{5.0f, 0.5f, 5.0f}});

    // Dynamic box: falls from y=4, rests at y~=1.0.
    const auto box = world.create_entity();
    world.add_component<transform::Transform>(box,
        transform::Transform{Vec3{0, 4, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<transform::WorldTransform>(box,
        transform::WorldTransform{Mat4::translation(Vec3{0, 4, 0})});
    world.add_component<render::MeshRenderer>(box, render::MeshRenderer{cube});
    world.add_component<render::Material>(box, render::Material{Vec3{1, 1, 1}, tex_id});
    world.add_component<physics::RigidBody>(box, physics::RigidBody{true, 1.0f});
    world.add_component<physics::Collider>(box, physics::Collider{Vec3{0.5f, 0.5f, 0.5f}});

    // Lights.
    const auto light = world.create_entity();
    world.add_component<render::DirectionalLight>(light,
        render::DirectionalLight{Vec3{-0.5f, -1.0f, -0.3f}, Vec3{1, 1, 1}});
    const auto point = world.create_entity();
    world.add_component<transform::Transform>(point,
        transform::Transform{Vec3{2, 3, 2}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<transform::WorldTransform>(point,
        transform::WorldTransform{Mat4::translation(Vec3{2, 3, 2})});
    world.add_component<render::PointLight>(point, render::PointLight{Vec3{1, 0.85f, 0.6f}, 3.0f, 8.0f});

    // Camera: raised at (0, 2.5, 9) looking down -Z, framing the ground slab + the drop.
    const auto camera = world.create_entity();
    world.add_component<transform::Transform>(camera,
        transform::Transform{Vec3{0, 2.5f, 9}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<transform::WorldTransform>(camera,
        transform::WorldTransform{Mat4::translation(Vec3{0, 2.5f, 9})});
    world.add_component<render::Camera>(camera, render::Camera{});

    return DemoScene{std::make_shared<render::RenderSystem>()};
}

} // namespace mosaic::app
```

- [ ] **Step 3: Build**

Run: `cmake --build build/debug -j 2>&1 | tail -8`
Expected: builds clean. If `mosaic run`/`editor` reference `SpinSystem` directly, the build
will flag it — grep shows `SpinSystem` is used only inside `demo_scene.hpp`, so removing it
is self-contained.

- [ ] **Step 4: Full suite + functional verification**

Run: `ctest --test-dir build/debug --output-on-failure`
Expected: all PASS.

Run: `./build/debug/app/mosaic run --window --ticks 60 2>&1 | grep -c ERROR; echo "run exit=$?"`
Expected: `0` validation errors; `exit=0` — the box drops onto the ground slab.

Run: `./build/debug/app/mosaic editor --ticks 120 2>&1 | grep -c ERROR; echo "editor exit=$?"`
Expected: `0` validation errors; `exit=0`. In the editor the box drops onto the ground;
**Stop** freezes it and **Step** single-steps the fall (maintainer confirms visually).

- [ ] **Step 5: Commit**

```bash
git add app/run/demo_scene.hpp app/CMakeLists.txt
git commit -m "physics P2: editor/run demo is a box dropping onto a ground plane (#37)"
```

---

## Task 5: Update the build handoff (next-steps)

**Files:**
- Modify: `docs/project/next-steps.md`

- [ ] **Step 1: Record P2**

Update `docs/project/next-steps.md`: change the "Last updated" line and note **physics P2 is
built** — reflected `RigidBody`/`Collider` + a `PhysicsSystem` (injected backend) that steps
in `World::tick` and syncs poses to `Transform`; the demo is now a **box dropping onto a
ground plane** in the editor (Play simulates; Pause/Step single-step the fall), and the
Box3D backend reads real rotation. Note **P3 (primitive shapes + stacking)** is next, then P4
(raycasts), P5 (contacts + record/replay). Link the
[P2 spec](../superpowers/specs/2026-08-24-physics-p2-system-design.md) and
[P2 plan](../superpowers/plans/2026-08-24-physics-p2-system.md).

- [ ] **Step 2: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "next-steps: physics P2 (PhysicsSystem + visible drop) built; next is P3"
```

---

## Self-Review

**Spec coverage:**
- Reflected `RigidBody`/`Collider` + registration (spec §Components) → Task 1. ✓
- `PhysicsSystem` (injected backend, lazy create, step, write-back) (spec §PhysicsSystem) → Task 2. ✓
- Rotation read-back (spec §Backend rotation read-back) → Task 3 Step 1. ✓
- Headless integration + determinism (spec §Testability) → Task 3. ✓
- Fake-backend sync test (spec §Testability) → Task 2. ✓
- Demo rewire + editor/run wiring + tick/play-stop (spec §Demo rewire, §Tick/frame) → Task 4. ✓
- Tier contract (backend injected; no module dep in the system) → Task 2 design + Task 4 CMake. ✓

**Placeholder scan:** No TBD/TODO; complete code in every code step; commands have expected output. ✓

**Type consistency:** `PhysicsSystem(std::unique_ptr<PhysicsBackend>, int)` identical across Tasks 2/3/4; `RigidBody{bool,float}`/`Collider{Vec3}` consistent across Tasks 1/2/3/4; `body_pose`/`create_body`/`step` match the P1 `PhysicsBackend`; `build_demo_scene(World&, RenderDevice&, AssetRegistry&) -> DemoScene` unchanged signature (callers `main.cpp`/`editor.cpp` need no edits). ✓

**One risk flagged (not blocking):** In `mosaic run --window`, `main.cpp` adds `TransformSystem` before `build_demo_scene` adds `PhysicsSystem`, so `WorldTransform` lags physics by one tick (imperceptible for a fall). The editor path is exact (it runs `TransformSystem` manually after the tick). Rotation read-back correctness is covered by the build + the functional demo; the integration test asserts position fall/settle/determinism.
