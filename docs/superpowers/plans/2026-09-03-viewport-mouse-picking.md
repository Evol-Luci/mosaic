# Viewport Mouse-Picking Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Click an object in the viewport to select it — a click unprojects to a world ray, `PhysicsSystem::raycast` resolves the hit entity, and `SelectionModel` updates.

**Architecture:** `World::get_system<T>()` reaches the physics system; a self-contained `studio/pick.hpp::pick_entity` finds the active camera, unprojects a ray (via `fov`+`aspect` and the camera's `WorldTransform` — no matrix inversion), and raycasts. The viewport click handler feeds it normalized device coords.

**Tech Stack:** C++20, CMake, Catch2, ImGui. Reuses P4 `PhysicsSystem::raycast`, #37d viewport, `SelectionModel`, foundation math.

**Spec:** `docs/superpowers/specs/2026-09-03-viewport-mouse-picking-design.md`

## Global Constraints

- **Tier wall:** `pick_entity` is Studio-tier logic over engine (render camera + physics raycast) + foundation math — Python-free, no Box3D above the module wall (uses the neutral `PhysicsSystem::raycast`), no new deps.
- **Never throw:** no camera / no `PhysicsSystem` / a miss → `RaycastHit{hit=false}`; a miss clears the selection.
- **Camera convention:** camera looks down **-Z** (RH); `ndc_x`/`ndc_y` in `[-1,1]`, center `(0,0)`, **+y up**.
- **Build:** `cmake --build build/debug -j`; **Test:** `ctest --test-dir build/debug`. Catch2 names not starting with `--`. Namespaces: `mosaic::studio`, `mosaic::engine::physics`, `mosaic::engine::render`, `mosaic::engine::transform`, `mosaic::foundation::math`, `mosaic::physics::box3d`.
- **API facts (verified):** `RaycastHit { bool hit; EntityID entity; Vec3 point; Vec3 normal; float distance; }`; `PhysicsSystem::raycast(const Vec3& origin, const Vec3& dir, float max_dist) -> RaycastHit`. `Camera { float fov_y_radians=1.0472; float near_z; float far_z; }`. `transform::WorldTransform { Mat4 matrix; }` (derived by `TransformSystem`). `Mat4` is column-major: `m[12],m[13],m[14]` = translation; `transform_direction(const Mat4&, const Vec3&) -> Vec3` (mat4.hpp), `normalize(const Vec3&) -> Vec3` (vec3.hpp). `World::for_each<Camera>(fn: EntityID, Camera&)`, `get_component<WorldTransform>`, `add_system<S>`, private `std::vector<std::unique_ptr<ecs::System>> systems_`. `System` is polymorphic (virtual `update`). `draw_viewport(RenderDevice&)` at studio_app.hpp:57 / dispatch studio_app.cpp:89 (`draw_viewport(device);`) / impl :134. `studio_app.cpp` includes `studio/hierarchy.hpp`, `studio/inspector.hpp`. `mosaic_unit_tests` links `mosaic_studio` + `mosaic_physics_box3d`. `create_box3d_backend(WorldDef{})` (`modules/physics_box3d/box3d_backend.hpp`).

---

### Task 1: `World::get_system<T>()` + `studio/pick.hpp`

**Files:**
- Modify: `engine/world/world.hpp` (add `get_system`)
- Create: `studio/pick.hpp`
- Test: `tests/unit/studio/pick_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `template<class S> S* World::get_system();` and `mosaic::studio::pick_entity(World&, float aspect, float ndc_x, float ndc_y) -> RaycastHit`.

- [ ] **Step 1: Write the failing test `tests/unit/studio/pick_test.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

#include "engine/physics/physics_components.hpp"
#include "engine/physics/physics_system.hpp"
#include "engine/render/camera.hpp"
#include "engine/transform/transform.hpp"
#include "engine/transform/transform_system.hpp"
#include "engine/world/world.hpp"
#include "modules/physics_box3d/box3d_backend.hpp"
#include "studio/pick.hpp"

using mosaic::engine::World;
using mosaic::engine::physics::Collider;
using mosaic::engine::physics::PhysicsSystem;
using mosaic::engine::physics::RigidBody;
using mosaic::engine::physics::WorldDef;
using mosaic::engine::render::Camera;
using mosaic::engine::transform::Transform;
using mosaic::engine::transform::TransformSystem;
using mosaic::foundation::math::Quat;
using mosaic::foundation::math::Vec3;
using mosaic::physics::box3d::create_box3d_backend;
using mosaic::studio::pick_entity;

namespace
{
// camera at (0,0,5) looking -Z; a static box at the origin. Returns the box entity.
mosaic::foundation::EntityID setup(World& world)
{
    world.add_system<TransformSystem>();
    world.add_system<PhysicsSystem>(create_box3d_backend(WorldDef{}));

    const auto cam = world.create_entity();
    world.add_component<Transform>(cam, Transform{Vec3{0, 0, 5}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<Camera>(cam, Camera{});

    const auto box = world.create_entity();
    world.add_component<Transform>(box, Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(box, RigidBody{false, 1.0f}); // static
    world.add_component<Collider>(box, Collider{Vec3{0.5f, 0.5f, 0.5f}});

    world.tick(1.0f / 60.0f); // derive WorldTransform + register the body
    return box;
}
} // namespace

TEST_CASE("mouse-pick hits the object under a center click", "[studio]")
{
    World world;
    const auto box = setup(world);
    const auto hit = pick_entity(world, 1.0f, 0.0f, 0.0f);
    REQUIRE(hit.hit);
    REQUIRE(hit.entity == box);
}

TEST_CASE("mouse-pick misses on a click into empty space", "[studio]")
{
    World world;
    setup(world);
    const auto hit = pick_entity(world, 1.0f, 0.95f, 0.95f); // corner ray clears the small box
    REQUIRE_FALSE(hit.hit);
}

TEST_CASE("mouse-pick with no camera is a miss", "[studio]")
{
    World world;
    world.add_system<PhysicsSystem>(create_box3d_backend(WorldDef{}));
    const auto hit = pick_entity(world, 1.0f, 0.0f, 0.0f);
    REQUIRE_FALSE(hit.hit);
}

TEST_CASE("World::get_system returns a registered system or nullptr", "[world]")
{
    World world;
    REQUIRE(world.get_system<PhysicsSystem>() == nullptr);
    world.add_system<PhysicsSystem>(create_box3d_backend(WorldDef{}));
    REQUIRE(world.get_system<PhysicsSystem>() != nullptr);
    REQUIRE(world.get_system<TransformSystem>() == nullptr);
}
```

- [ ] **Step 2: Register the test + confirm it fails**

Add to `tests/CMakeLists.txt` after `unit/studio/script_editor_test.cpp`:
```cmake
  unit/studio/pick_test.cpp
```
Build → FAIL (`studio/pick.hpp` missing; `World::get_system` undefined).

- [ ] **Step 3: Add `get_system` to `engine/world/world.hpp`**

Add this public template method (near `add_system`):
```cpp
    // The first registered system that is a T (dynamic_cast), or nullptr. Systems are polymorphic
    // (virtual update), so this is a safe cross-cast. O(#systems); the set is tiny.
    template <typename S>
    S* get_system()
    {
        for (std::unique_ptr<ecs::System>& sys : systems_)
        {
            if (S* s = dynamic_cast<S*>(sys.get())) { return s; }
        }
        return nullptr;
    }
```

- [ ] **Step 4: Create `studio/pick.hpp`**

```cpp
#pragma once

#include <cmath>

#include "engine/physics/physics_system.hpp"
#include "engine/render/camera.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"
#include "foundation/math/mat4.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::studio
{
// Pick the entity under a viewport click. (ndc_x, ndc_y) in [-1,1] (center = 0,0; +y up); `aspect` =
// viewport width/height. Finds the active camera, unprojects a world ray, and raycasts the physics
// world. Returns the RaycastHit (hit==false if no camera / no physics system / nothing hit). Never
// throws.
inline mosaic::engine::physics::RaycastHit
pick_entity(mosaic::engine::World& world, float aspect, float ndc_x, float ndc_y)
{
    using mosaic::foundation::math::Mat4;
    using mosaic::foundation::math::Vec3;
    namespace render    = mosaic::engine::render;
    namespace transform = mosaic::engine::transform;

    Mat4  cam_world;
    float tan_half = 0.0f;
    bool  found    = false;
    world.for_each<render::Camera>(
        [&](mosaic::foundation::EntityID e, render::Camera& cam)
        {
            if (found) { return; }
            const transform::WorldTransform* wt = world.get_component<transform::WorldTransform>(e);
            if (wt == nullptr) { return; }
            cam_world = wt->matrix;
            tan_half  = std::tan(cam.fov_y_radians * 0.5f);
            found     = true;
        });
    mosaic::engine::physics::RaycastHit miss;
    if (!found) { return miss; }

    const Vec3 dir_view{ndc_x * aspect * tan_half, ndc_y * tan_half, -1.0f};
    const Vec3 dir = mosaic::foundation::math::normalize(
        mosaic::foundation::math::transform_direction(cam_world, dir_view));
    const Vec3 origin{cam_world.m[12], cam_world.m[13], cam_world.m[14]};

    mosaic::engine::physics::PhysicsSystem* physics =
        world.get_system<mosaic::engine::physics::PhysicsSystem>();
    if (physics == nullptr) { return miss; }
    return physics->raycast(origin, dir, 1000.0f);
}
} // namespace mosaic::studio
```

- [ ] **Step 5: Build + test**
```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "mouse-pick|World::get_system returns" --output-on-failure
```
Expected: the four cases PASS. Then the full suite once (`ctest --test-dir build/debug 2>&1 | tail -3`) — 0 failures.

- [ ] **Step 6: Commit**
```bash
git add engine/world/world.hpp studio/pick.hpp tests/unit/studio/pick_test.cpp tests/CMakeLists.txt
git commit -m "feat(studio): pick_entity — unproject a viewport click + raycast; World::get_system"
```

---

### Task 2: Viewport click → select

**Files:**
- Modify: `studio/studio_app.hpp` (`draw_viewport` decl gains `World&`)
- Modify: `studio/studio_app.cpp` (include, dispatch, `draw_viewport` body)

**Interfaces:**
- Consumes: `pick_entity` (Task 1), `SelectionModel::select`/`clear`.

This task's deliverable is GUI glue; verified by **build + full suite green** (the pick logic is covered by Task 1's headless tests). Visual acceptance (click objects in `mosaic editor`, Hierarchy/Inspector follow) is the user's.

- [ ] **Step 1: Update the declaration in `studio/studio_app.hpp`**

Change (line ~57):
```cpp
    void draw_viewport(mosaic::engine::render::RenderDevice& device);
```
to:
```cpp
    void draw_viewport(mosaic::engine::render::RenderDevice& device, mosaic::engine::World& world);
```

- [ ] **Step 2: Update the include, dispatch, and body in `studio/studio_app.cpp`**

Add near the other studio includes:
```cpp
#include "studio/pick.hpp"
```
Change the dispatch (line ~89) from `draw_viewport(device);` to `draw_viewport(device, world);`.
Replace the `draw_viewport` body:
```cpp
void StudioApp::draw_viewport(mosaic::engine::render::RenderDevice& device)
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    viewport_.set_measured_size(avail.x, avail.y);

    const auto tex = device.viewport_texture();
    if (tex != 0)
    {
        const ViewportSize s = viewport_.size();
        ImGui::Image(static_cast<ImTextureID>(tex),
                     ImVec2(static_cast<float>(s.width), static_cast<float>(s.height)));
    }
    else
    {
        ImGui::TextUnformatted("(no viewport)");
    }
}
```
with:
```cpp
void StudioApp::draw_viewport(mosaic::engine::render::RenderDevice& device,
                              mosaic::engine::World& world)
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    viewport_.set_measured_size(avail.x, avail.y);

    const auto tex = device.viewport_texture();
    if (tex != 0)
    {
        const ViewportSize s = viewport_.size();
        ImGui::Image(static_cast<ImTextureID>(tex),
                     ImVec2(static_cast<float>(s.width), static_cast<float>(s.height)));
        if (ImGui::IsItemClicked())
        {
            const ImVec2 mn = ImGui::GetItemRectMin();
            const ImVec2 mp = ImGui::GetMousePos();
            const float  w  = static_cast<float>(s.width);
            const float  h  = static_cast<float>(s.height);
            const float  ndc_x = 2.0f * (mp.x - mn.x) / w - 1.0f;
            const float  ndc_y = 1.0f - 2.0f * (mp.y - mn.y) / h; // screen y-down -> ndc y-up
            const mosaic::engine::physics::RaycastHit hit = pick_entity(world, w / h, ndc_x, ndc_y);
            if (hit.hit) { selection_.select(hit.entity); }
            else         { selection_.clear(); }
        }
    }
    else
    {
        ImGui::TextUnformatted("(no viewport)");
    }
}
```

- [ ] **Step 3: Build + full suite + smoke**
```bash
cmake --build build/debug -j && ctest --test-dir build/debug 2>&1 | tail -3
timeout 3 ./build/debug/app/mosaic run --window 2>err.txt; echo "exit=$?"; head -3 err.txt; rm -f err.txt
```
Expected: build clean, full suite 0 failures. The `mosaic run --window` smoke may exit non-zero / time out (no display) — NOT a failure; note what you observed (build + suite are the gate; the editor viewport click is user-verified).

- [ ] **Step 4: Commit**
```bash
git add studio/studio_app.hpp studio/studio_app.cpp
git commit -m "feat(studio): click the viewport to select the object under the cursor"
```

---

## Notes for the executor

- **`docs/project/next-steps.md`** — after Task 2, mark viewport **mouse-picking — DONE** in the Editor-UX bullet (click-to-select via `PhysicsSystem::raycast`); note deferred collider-less picking + gizmos. Update the running test count. Commit on this branch (rides in the PR).
- **Finishing:** use `superpowers:finishing-a-development-branch`; base branch is `main`.
- The viewport click glue is thin; visual acceptance is the user's. The pick math + `get_system` are fully headless-tested in Task 1.
