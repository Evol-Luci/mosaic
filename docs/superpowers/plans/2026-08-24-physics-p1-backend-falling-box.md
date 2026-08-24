# Physics P1 — Backend Bootstrap + Falling Box Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A neutral `PhysicsBackend` + a Box3D backend behind the wall, proven by a headless, deterministic unit test where a box falls under gravity and rests on a ground plane.

**Architecture:** A neutral interface + value types in `engine/physics/` (no `b3*`), implemented by a new `modules/physics_box3d` tier that wraps Box3D (FetchContent, pinned). Physics is inherently headless and deterministic, so the acceptance is real trajectory assertions, not "exits 0."

**Tech Stack:** C++20, Box3D (C17, MIT, FetchContent-pinned), `foundation/math`, Catch2, CMake.

## Global Constraints

- **C++20**; the neutral interface is header-only (like `engine/render/render_device.hpp`).
- **Tier contract:** `b3*` types **never leave `modules/physics_box3d`** — no `b3World`/`b3Body`/`b3Vec3` in any `engine/`/`app/`/`studio/`/test header. `engine/physics/` depends only on `foundation/`; `modules/physics_box3d` depends on `engine/physics` + `box3d::box3d`.
- **Box3D pinned** to commit `30c67b5e6d0a3a66f0f506c69ce9e9e0587e3b7c` via FetchContent; `BOX3D_SAMPLES OFF`. MIT license (compatible).
- **Determinism:** the world steps **single-threaded** (no task callbacks in the world def) so identical inputs give bit-identical output.
- **No new third-party deps** beyond Box3D. Catch2 test names must not start with `--`.
- **Math conventions:** Y-up, meters, radians; gravity default `{0, -9.81, 0}`.
- **Build/test commands** (binaryDir `build/debug`):
  - Configure (this fetches Box3D — the risk gate): `cmake --preset debug`
  - Build: `cmake --build build/debug -j`
  - Test: `ctest --test-dir build/debug --output-on-failure`

## Verified Box3D API (pinned commit)

- Header: `#include <box3d/box3d.h>` (pulls `types.h`, `math_functions.h`, `collision.h`).
- `b3WorldId b3CreateWorld(const b3WorldDef*)`, `void b3DestroyWorld(b3WorldId)`, `void b3World_Step(b3WorldId, float timeStep, int subStepCount)`.
- `b3WorldDef b3DefaultWorldDef(void)` — has a `b3Vec3 gravity` field.
- `b3BodyId b3CreateBody(b3WorldId, const b3BodyDef*)`; `b3BodyDef b3DefaultBodyDef(void)` — fields `b3BodyType type` (`b3_staticBody`/`b3_dynamicBody`) and a position field.
- `b3BoxHull b3MakeBoxHull(float hx, float hy, float hz)` (half-extents); `b3ShapeDef b3DefaultShapeDef(void)` (field `float density`); `b3ShapeId b3CreateHullShape(b3BodyId, const b3ShapeDef*, const b3HullData*)` — pass `&hull.base`.
- `b3Pos b3Body_GetPosition(b3BodyId)` — `b3Pos` == `b3Vec3` `{float x,y,z}` in the default single-precision build.
- Structs: `b3Vec3 {float x,y,z;}`, `b3Quat {b3Vec3 v; float s;}`.

*(Task 2's first sub-step re-confirms the default/`b3Make*` helper names against the fetched headers before compiling — the API is alpha.)*

---

## File Structure

- `engine/physics/physics_backend.hpp` — **create.** Neutral interface + value types.
- `modules/physics_box3d/box3d_backend.hpp` — **create.** The `create_box3d_backend` factory declaration.
- `modules/physics_box3d/box3d_backend.cpp` — **create.** `Box3DBackend` + factory; the only place `b3*` appears.
- `modules/physics_box3d/CMakeLists.txt` — **create.** The `mosaic_physics_box3d` static lib.
- `CMakeLists.txt` — **modify.** FetchContent(box3d) + `add_subdirectory(modules/physics_box3d)`.
- `tests/unit/physics/physics_backend_test.cpp` — **create.** Neutral value-type defaults.
- `tests/unit/physics/falling_box_test.cpp` — **create.** Smoke (Task 2) + rest/determinism (Task 3).
- `tests/CMakeLists.txt` — **modify.** Register the two tests + link `mosaic_physics_box3d`.

---

## Task 1: Neutral PhysicsBackend interface

**Files:**
- Create: `engine/physics/physics_backend.hpp`
- Create: `tests/unit/physics/physics_backend_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `mosaic::engine::physics::{BodyHandle, BodyType, BoxShape, BodyDef, Pose, WorldDef, PhysicsBackend}` (see code below). No Box3D — pure, no new link.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/physics/physics_backend_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/physics/physics_backend.hpp"

using Catch::Approx;
using mosaic::engine::physics::BodyDef;
using mosaic::engine::physics::BodyType;
using mosaic::engine::physics::BoxShape;
using mosaic::engine::physics::WorldDef;

TEST_CASE("physics value-type defaults", "[physics]")
{
    WorldDef world;
    REQUIRE(world.gravity.y == Approx(-9.81f));
    REQUIRE(world.gravity.x == Approx(0.0f));

    BodyDef body;
    REQUIRE(body.type == BodyType::Dynamic);
    REQUIRE(body.density == Approx(1.0f));

    BoxShape shape;
    REQUIRE(shape.half_extents.x == Approx(0.5f));
    REQUIRE(shape.half_extents.z == Approx(0.5f));
}
```

Register it in `tests/CMakeLists.txt` (add to the `add_executable(mosaic_unit_tests ...)` source list, after the `unit/studio/...` block):

```cmake
  unit/physics/physics_backend_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build/debug -j 2>&1 | tail -20`
Expected: compile error — `engine/physics/physics_backend.hpp` not found.

- [ ] **Step 3: Write the neutral interface header**

Create `engine/physics/physics_backend.hpp`:

```cpp
#pragma once

#include <cstdint>

#include "foundation/math/quat.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::engine::physics
{
using BodyHandle = std::uint32_t; // opaque; 0 == invalid

enum class BodyType { Static, Dynamic };

struct BoxShape
{
    mosaic::foundation::math::Vec3 half_extents{0.5f, 0.5f, 0.5f};
};

struct BodyDef
{
    BodyType                       type = BodyType::Dynamic;
    mosaic::foundation::math::Vec3 position{};
    BoxShape                       shape{};
    float                          density = 1.0f; // ignored for static bodies
};

struct Pose
{
    mosaic::foundation::math::Vec3 position{};
    mosaic::foundation::math::Quat rotation{}; // P1: identity for valid bodies
};

struct WorldDef
{
    mosaic::foundation::math::Vec3 gravity{0.0f, -9.81f, 0.0f}; // Y-up, meters, m/s^2
};

// The physics world contract; the Box3D backend implements it behind the wall. Stepping is
// deterministic: identical construction + calls yield identical poses across runs.
class PhysicsBackend
{
public:
    virtual ~PhysicsBackend() = default;
    virtual BodyHandle create_body(const BodyDef& def) = 0;
    virtual void       step(float dt, int substeps) = 0;
    virtual Pose       body_pose(BodyHandle body) const = 0;
};
} // namespace mosaic::engine::physics
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build/debug -j && ctest --test-dir build/debug --output-on-failure -R "physics value-type"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/physics/physics_backend.hpp tests/unit/physics/physics_backend_test.cpp tests/CMakeLists.txt
git commit -m "physics P1: neutral PhysicsBackend interface + value types"
```

---

## Task 2: Box3D build spike + backend (smoke test)

This task wires the alpha Box3D dependency and implements the backend, gated by a smoke
test. **If `cmake --preset debug` fails to fetch/configure Box3D, stop and report** — the
interface (Task 1) and the acceptance tests (Task 3) stand regardless of backend.

**Files:**
- Modify: `CMakeLists.txt`
- Create: `modules/physics_box3d/CMakeLists.txt`, `box3d_backend.hpp`, `box3d_backend.cpp`
- Create: `tests/unit/physics/falling_box_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: the neutral types from Task 1.
- Produces: `std::unique_ptr<mosaic::engine::physics::PhysicsBackend> mosaic::physics::box3d::create_box3d_backend(const mosaic::engine::physics::WorldDef&)`.

- [ ] **Step 1: Declare Box3D + the module in the top-level CMake**

In `CMakeLists.txt`, after the `FetchContent_MakeAvailable(glfw)` block (before the imgui block is fine too), add:

```cmake
FetchContent_Declare(
  box3d
  GIT_REPOSITORY https://github.com/erincatto/box3d.git
  GIT_TAG        30c67b5e6d0a3a66f0f506c69ce9e9e0587e3b7c
)
set(BOX3D_SAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(box3d)
```

Then add the module subdirectory right after `add_subdirectory(modules/render_vulkan)`:

```cmake
add_subdirectory(modules/physics_box3d)
```

- [ ] **Step 2: Create the module CMakeLists**

Create `modules/physics_box3d/CMakeLists.txt`:

```cmake
add_library(mosaic_physics_box3d STATIC box3d_backend.cpp)
target_include_directories(mosaic_physics_box3d PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(mosaic_physics_box3d
  PUBLIC  mosaic_engine
  PRIVATE box3d::box3d mosaic_foundation)
target_compile_features(mosaic_physics_box3d PUBLIC cxx_std_20)
```

- [ ] **Step 3: Configure — the Box3D fetch/build risk gate**

Run: `cmake --preset debug 2>&1 | tail -25`
Expected: configuration succeeds (Box3D is fetched at the pinned commit and its CMake
configures). If it fails here, **stop and report the error** — do not proceed.

Then confirm the helper/default API names against the fetched headers (the API is alpha):

Run: `grep -rhoE 'b3(DefaultWorldDef|DefaultBodyDef|DefaultShapeDef|MakeBoxHull|CreateHullShape|CreateBody|CreateWorld|World_Step|Body_GetPosition)[A-Za-z0-9_]*' build/debug/_deps/box3d-src/include | sort -u`
Expected: the names used in Step 5 appear. If a name differs, adjust the code in Step 5 to match (e.g. an argument-count or spelling change).

- [ ] **Step 4: Write the factory header**

Create `modules/physics_box3d/box3d_backend.hpp`:

```cpp
#pragma once

#include <memory>

#include "engine/physics/physics_backend.hpp"

namespace mosaic::physics::box3d
{
// Constructs a Box3D-backed PhysicsBackend. b3* stays inside the .cpp.
std::unique_ptr<mosaic::engine::physics::PhysicsBackend>
create_box3d_backend(const mosaic::engine::physics::WorldDef& def);
} // namespace mosaic::physics::box3d
```

- [ ] **Step 5: Write the backend implementation**

Create `modules/physics_box3d/box3d_backend.cpp`:

```cpp
#include "modules/physics_box3d/box3d_backend.hpp"

#include <vector>

#include <box3d/box3d.h>

#include "foundation/math/quat.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::physics::box3d
{
namespace
{
namespace ep = mosaic::engine::physics;
using mosaic::foundation::math::Quat;
using mosaic::foundation::math::Vec3;

class Box3DBackend final : public ep::PhysicsBackend
{
public:
    explicit Box3DBackend(const ep::WorldDef& def)
    {
        b3WorldDef wd = b3DefaultWorldDef();
        wd.gravity.x = def.gravity.x;
        wd.gravity.y = def.gravity.y;
        wd.gravity.z = def.gravity.z;
        world_ = b3CreateWorld(&wd); // no task callbacks -> single-threaded, deterministic
    }

    ~Box3DBackend() override { b3DestroyWorld(world_); }

    ep::BodyHandle create_body(const ep::BodyDef& def) override
    {
        b3BodyDef bd = b3DefaultBodyDef();
        bd.type = (def.type == ep::BodyType::Dynamic) ? b3_dynamicBody : b3_staticBody;
        bd.position.x = def.position.x;
        bd.position.y = def.position.y;
        bd.position.z = def.position.z;
        b3BodyId id = b3CreateBody(world_, &bd);

        const Vec3 h = def.shape.half_extents;
        b3BoxHull  hull = b3MakeBoxHull(h.x, h.y, h.z);
        b3ShapeDef sd = b3DefaultShapeDef();
        sd.density = def.density;
        b3CreateHullShape(id, &sd, &hull.base);

        bodies_.push_back(id);
        return static_cast<ep::BodyHandle>(bodies_.size()); // handle == index + 1
    }

    void step(float dt, int substeps) override { b3World_Step(world_, dt, substeps); }

    ep::Pose body_pose(ep::BodyHandle body) const override
    {
        if (body == 0 || body > bodies_.size())
        {
            return ep::Pose{};
        }
        const auto p = b3Body_GetPosition(bodies_[body - 1]); // b3Pos == b3Vec3 {x,y,z}
        // P1 reads position only; a straight-falling box does not rotate. Real rotation
        // read-back (b3Body_GetRotation -> b3Quat{v,s}) arrives in P2.
        return ep::Pose{Vec3{p.x, p.y, p.z}, Quat::identity()};
    }

private:
    b3WorldId             world_{};
    std::vector<b3BodyId> bodies_;
};
} // namespace

std::unique_ptr<ep::PhysicsBackend> create_box3d_backend(const ep::WorldDef& def)
{
    return std::make_unique<Box3DBackend>(def);
}
} // namespace mosaic::physics::box3d
```

- [ ] **Step 6: Write the smoke test + link the module**

Create `tests/unit/physics/falling_box_test.cpp`:

```cpp
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

TEST_CASE("a dynamic body falls under gravity", "[physics]")
{
    auto backend = create_box3d_backend(WorldDef{});
    REQUIRE(backend != nullptr);

    BodyDef box;
    box.type = BodyType::Dynamic;
    box.position = Vec3{0.0f, 4.0f, 0.0f};
    const BodyHandle handle = backend->create_body(box);
    REQUIRE(handle != 0);

    const float y0 = backend->body_pose(handle).position.y;
    for (int i = 0; i < 10; ++i)
    {
        backend->step(1.0f / 60.0f, 4);
    }
    const float y1 = backend->body_pose(handle).position.y;
    REQUIRE(y1 < y0); // gravity pulled it down
}
```

Register it in `tests/CMakeLists.txt` (source list, after the Task 1 physics test):

```cmake
  unit/physics/falling_box_test.cpp
```

And add `mosaic_physics_box3d` to the `target_link_libraries(mosaic_unit_tests PRIVATE ...)` list (next to `mosaic_render_vulkan`):

```cmake
  mosaic_physics_box3d
```

- [ ] **Step 7: Build + run the smoke test**

Run: `cmake --build build/debug -j 2>&1 | tail -15 && ctest --test-dir build/debug --output-on-failure -R "a dynamic body falls"`
Expected: builds clean (Box3D links), test PASSES (the body's y decreases after stepping).
If the build fails on a `b3*` name mismatch, fix per Step 3's grep and rebuild.

- [ ] **Step 8: Verify the tier contract (no b3* leak)**

Run: `grep -rn "b3" engine/ app/ studio/ tests/ --include=*.hpp --include=*.cpp | grep -vE "^\S+:\s*//|b3d" || echo "clean: no b3* outside the module"`
Expected: `clean` — `b3*` appears only in `modules/physics_box3d/box3d_backend.cpp`.

- [ ] **Step 9: Commit**

```bash
git add CMakeLists.txt modules/physics_box3d tests/unit/physics/falling_box_test.cpp tests/CMakeLists.txt
git commit -m "physics P1: Box3D backend behind the wall (FetchContent, pinned) + smoke test"
```

---

## Task 3: Deterministic falling-box acceptance

**Files:**
- Modify: `tests/unit/physics/falling_box_test.cpp`

**Interfaces:**
- Consumes: `create_box3d_backend`, the neutral types (Tasks 1–2).

- [ ] **Step 1: Add the rest + determinism cases**

Append to `tests/unit/physics/falling_box_test.cpp`:

```cpp
#include <cmath>

#include "engine/physics/physics_backend.hpp"

namespace
{
// Drop a unit box (half-extent 0.5) from y=4 onto a static ground whose top surface is at
// y=0, step 90x at 1/60s with 4 substeps, and return the final y. If last_delta is given,
// it receives |y(last) - y(prev)| so a caller can assert the box has settled.
float run_drop(float* last_delta = nullptr)
{
    using namespace mosaic::engine::physics;
    auto backend = mosaic::physics::box3d::create_box3d_backend(WorldDef{});

    BodyDef ground;
    ground.type = BodyType::Static;
    ground.position = mosaic::foundation::math::Vec3{0.0f, -10.0f, 0.0f};
    ground.shape.half_extents = mosaic::foundation::math::Vec3{50.0f, 10.0f, 50.0f};
    backend->create_body(ground);

    BodyDef box;
    box.type = BodyType::Dynamic;
    box.position = mosaic::foundation::math::Vec3{0.0f, 4.0f, 0.0f};
    box.shape.half_extents = mosaic::foundation::math::Vec3{0.5f, 0.5f, 0.5f};
    const BodyHandle handle = backend->create_body(box);

    float y = box.position.y;
    float prev = y;
    for (int i = 0; i < 90; ++i)
    {
        prev = y;
        backend->step(1.0f / 60.0f, 4);
        y = backend->body_pose(handle).position.y;
    }
    if (last_delta != nullptr)
    {
        *last_delta = std::fabs(y - prev);
    }
    return y;
}
} // namespace

TEST_CASE("a dropped box rests on the ground", "[physics]")
{
    float delta = 1.0f;
    const float y = run_drop(&delta);
    REQUIRE(y > 0.45f); // rest center ~= ground top (0) + half-extent (0.5), minus solver slop
    REQUIRE(y < 0.55f);
    REQUIRE(delta < 1e-3f); // settled: no longer falling
}

TEST_CASE("physics stepping is deterministic across runs", "[physics]")
{
    REQUIRE(run_drop() == run_drop()); // identical inputs -> bit-identical output
}
```

- [ ] **Step 2: Build + run the acceptance tests**

Run: `cmake --build build/debug -j && ctest --test-dir build/debug --output-on-failure -R "rests on the ground|deterministic across runs"`
Expected: both PASS. If "rests" fails because the observed rest y sits just outside
`[0.45, 0.55]` (Box3D solver slop), widen the band to bracket the observed value — but the
**settled** (`delta < 1e-3`) and **determinism** (`==`) assertions must hold as written; if
determinism fails, the world is not stepping single-threaded — recheck the world-def task
callbacks before adjusting the test.

- [ ] **Step 3: Full suite**

Run: `ctest --test-dir build/debug --output-on-failure`
Expected: all tests PASS.

- [ ] **Step 4: Commit**

```bash
git add tests/unit/physics/falling_box_test.cpp
git commit -m "physics P1: deterministic falling-box rest + determinism tests"
```

---

## Task 4: Update the build handoff (next-steps)

**Files:**
- Modify: `docs/project/next-steps.md`

- [ ] **Step 1: Record P1**

Update `docs/project/next-steps.md`: change the "Last updated" line and note that **physics
P1 is built** — a neutral `PhysicsBackend` (`engine/physics/`) + a Box3D backend
(`modules/physics_box3d`, FetchContent-pinned to `30c67b5`) behind the wall, proven by a
headless deterministic falling-box unit test (box rests on a ground plane; identical runs
are bit-identical). Note the physics decomposition (P1→P5) and that **P2 (ECS
`PhysicsSystem` + a visible box drop in the editor)** is next. Link the
[decomposition](../superpowers/specs/2026-08-24-physics-decomposition.md),
[P1 spec](../superpowers/specs/2026-08-24-physics-p1-backend-falling-box-design.md), and
[P1 plan](../superpowers/plans/2026-08-24-physics-p1-backend-falling-box.md).

- [ ] **Step 2: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "next-steps: physics P1 (backend + falling box) built; next is P2"
```

---

## Self-Review

**Spec coverage:**
- Neutral `PhysicsBackend` + value types (spec §Neutral interface) → Task 1. ✓
- Box3D backend behind the wall + FetchContent pin + CMake (spec §Box3D backend, §CMake) → Task 2. ✓
- De-risking build spike (spec §Risks) → Task 2 Steps 1–3, 7. ✓
- Neutral↔Box3D mapping (spec §mapping table) → Task 2 Step 5. ✓
- Deterministic falling-box rest + determinism tests (spec §Testability) → Task 3. ✓
- Tier contract (no `b3*` leak) → Task 2 Step 8. ✓

**Placeholder scan:** No TBD/TODO; every code step shows complete code; commands have expected output. The one deliberate deferral (re-confirm alpha `b3*` helper names) is a concrete grep step, not a placeholder. ✓

**Type consistency:** `create_box3d_backend(const WorldDef&) -> unique_ptr<PhysicsBackend>` identical across Task 2 header/impl and Tasks 2–3 tests; neutral `BodyDef`/`BodyType`/`BoxShape`/`Pose`/`WorldDef`/`BodyHandle` consistent across Tasks 1–3; `body_pose().position.y` used consistently. ✓

**One risk flagged (not blocking):** Box3D is alpha — Task 2 Step 3 is a hard gate (stop if it won't configure), and Step 3's grep + Step 7's build catch any `b3*` name drift at the pinned commit. The exact rest tolerance in Task 3 may need widening to bracket the observed slop; the settled + determinism assertions are the load-bearing ones.
