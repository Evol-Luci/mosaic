# Physics P1 — Backend Bootstrap + Falling Box (design)

**Decomposition:** [physics-decomposition](2026-08-24-physics-decomposition.md) (slice P1).
**Design intent:** [design/physics.md](../../design/physics.md),
[architecture/boundaries.md §2](../../architecture/boundaries.md).
**Date:** 2026-08-24.

## Summary

Stand up the physics **backend wall** and prove it: a neutral `PhysicsBackend` interface in
`engine/physics/`, a `modules/physics_box3d` tier that implements it with Box3D (pinned,
FetchContent), and a **headless, deterministic** unit test — a dynamic box falls under
gravity and rests on a static ground, bit-identically across runs. This is the "clear
screen" equivalent for physics: no ECS, no visuals, just the wall + stepping + determinism
in isolation.

## Scope

**In:**
- Neutral `PhysicsBackend` interface + value types (`BodyHandle`, `BodyType`, `BoxShape`,
  `BodyDef`, `Pose`, `WorldDef`) in `engine/physics/` — no `b3*` types.
- `modules/physics_box3d`: `Box3DBackend : PhysicsBackend` + a `create_box3d_backend`
  factory, with Box3D wired via FetchContent (pinned commit).
- Box shape only; static + dynamic bodies; gravity; fixed-step `step(dt, substeps)`;
  position + rotation read-back.
- A deterministic falling-box unit test.

**Deferred (later slices):** ECS `RigidBody`/`Collider` components + `PhysicsSystem` (P2),
sphere/capsule/plane shapes (P3), raycasts (P4), contact events + record/replay (P5). No
developer-facing `PhysicsWorld` wrapper yet — `PhysicsBackend` is the world contract for P1.

## Architecture

### Tier & boundary

Mirrors `modules/render_vulkan`: the neutral interface lives in `engine/physics/`, the
Box3D implementation in a new `modules/physics_box3d` tier. **`b3*` types never leave
`modules/physics_box3d`** — no `b3World`, `b3Body`, `b3Vec3` in any `engine/`/`app/`/`studio/`
header. `engine/physics/` depends only on `foundation/` (math); `modules/physics_box3d`
depends on `engine/physics` + `box3d::box3d`; nothing depends on the module except the
composition root that constructs a backend (the P1 test, later the app).

### Neutral interface (`engine/physics/physics_backend.hpp`)

```cpp
#pragma once
#include <cstdint>
#include "foundation/math/vec3.hpp"
#include "foundation/math/quat.hpp"

namespace mosaic::engine::physics
{
using BodyHandle = std::uint32_t; // opaque; 0 == invalid

enum class BodyType { Static, Dynamic };

struct BoxShape { mosaic::foundation::math::Vec3 half_extents{0.5f, 0.5f, 0.5f}; };

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
    mosaic::foundation::math::Quat rotation{}; // identity default
};

struct WorldDef
{
    mosaic::foundation::math::Vec3 gravity{0.0f, -9.81f, 0.0f}; // Y-up, meters, m/s^2
};

// The physics world contract. A backend owns a simulation world; b3* stays behind it.
// Stepping is deterministic: identical construction + calls yield identical poses.
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

`gravity` is set at construction (matching `b3CreateWorld(&worldDef)`); there is no
`set_gravity` in P1 (YAGNI — add if a slice needs it). `body_pose` on an invalid handle
returns a default `Pose` (identity) rather than crashing.

### Box3D backend (`modules/physics_box3d/`)

`box3d_backend.hpp` declares the factory:

```cpp
std::unique_ptr<mosaic::engine::physics::PhysicsBackend>
create_box3d_backend(const mosaic::engine::physics::WorldDef& def);
```

`box3d_backend.cpp` implements `Box3DBackend`:

- **Construction:** `b3WorldDef wd = b3DefaultWorldDef(); wd.gravity = {g.x,g.y,g.z};`
  `world_ = b3CreateWorld(&wd);`. **No task callbacks** are set, so Box3D steps
  single-threaded — required for bit-determinism.
- **`create_body`:** `b3BodyDef bd = b3DefaultBodyDef();` set `bd.type`
  (`b3_staticBody`/`b3_dynamicBody`) and `bd.position`; `b3BodyId id = b3CreateBody(world_, &bd);`
  build the box hull `b3BoxHull hull = b3MakeBoxHull(h.x, h.y, h.z);` (Box3D box half-extents,
  matching Box2D's half-width convention), `b3ShapeDef sd = b3DefaultShapeDef(); sd.density = def.density;`
  `b3CreateHullShape(id, &sd, &hull.base);`. Push `id` into a `std::vector<b3BodyId>`; return
  `index + 1` as the `BodyHandle`.
- **`step`:** `b3World_Step(world_, dt, substeps);`
- **`body_pose`:** map handle → `b3BodyId`; `b3Body_GetPosition(id)` → `Vec3`;
  `b3Body_GetRotation(id)` (or `b3Body_GetTransform`) → `Quat`. *(The exact rotation getter
  name is confirmed against the pinned Box3D headers during the spike; position is the
  asserted quantity in P1.)*
- **Destructor:** `b3DestroyWorld(world_);`.

The `std::vector<b3BodyId>` and `b3WorldId` are the only state; `b3*` appears only in the
`.cpp`.

### CMake

Top-level `CMakeLists.txt`:
```cmake
FetchContent_Declare(box3d
  GIT_REPOSITORY https://github.com/erincatto/box3d.git
  GIT_TAG        30c67b5e6d0a3a66f0f506c69ce9e9e0587e3b7c)  # pinned commit (2026-08-13)
set(BOX3D_SAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(box3d)
...
add_subdirectory(modules/physics_box3d)   # after add_subdirectory(engine)
```

`modules/physics_box3d/CMakeLists.txt`:
```cmake
add_library(mosaic_physics_box3d STATIC box3d_backend.cpp)
target_include_directories(mosaic_physics_box3d PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(mosaic_physics_box3d
  PUBLIC  mosaic_engine
  PRIVATE box3d::box3d mosaic_foundation)
target_compile_features(mosaic_physics_box3d PUBLIC cxx_std_20)
```

The unit-test target links `mosaic_physics_box3d`.

### Neutral ↔ Box3D mapping (summary)

| Neutral | Box3D |
|---|---|
| `WorldDef.gravity` | `b3WorldDef.gravity` → `b3CreateWorld` |
| `BodyType::Static/Dynamic` | `b3_staticBody`/`b3_dynamicBody` |
| `BodyDef.position` | `b3BodyDef.position` |
| `BoxShape.half_extents` | `b3MakeBoxHull(hx,hy,hz)` + `b3CreateHullShape` |
| `BodyDef.density` | `b3ShapeDef.density` |
| `step(dt, substeps)` | `b3World_Step(world, dt, substeps)` |
| `body_pose().position` | `b3Body_GetPosition` |
| `body_pose().rotation` | `b3Body_GetRotation` / `b3Body_GetTransform` |

## Testability

Physics is inherently headless + deterministic, so P1 gets **real assertions** (not "exits 0"):

- **Rest test** (`tests/unit/physics/falling_box_test.cpp`, tag `[physics]`): construct a
  backend with gravity `{0,-9.81,0}`; a static ground body at `{0,-10,0}` with box
  half-extents `{50,10,50}` (top surface at y=0); a dynamic box at `{0,4,0}` with half-extents
  `{0.5,0.5,0.5}`. Step **90 times** at `dt = 1/60`, `substeps = 4`. Assert:
  - the box came to **rest** on the ground: `pose.position.y` within `[0.45, 0.55]` (rest
    center ≈ 0.5 = ground top 0 + half-extent 0.5, allowing solver slop), and
  - it is **settled**, not still falling: the y delta between the last two steps is `< 1e-3`.
- **Determinism test**: run the identical setup + 90 steps **twice**; assert the two final
  `pose.position.y` values are **bit-identical** (`==`), the public determinism guarantee.

If the exact rest tolerance needs adjustment after observing Box3D's solver slop, widen the
band — but the *settled* and *deterministic* assertions are the load-bearing ones.

## Risks & the de-risking spike

Box3D is **alpha** (v0.1.0). The first implementation task is a **build spike**: wire the
FetchContent declaration + an empty `modules/physics_box3d` that links `box3d::box3d`, and
confirm `cmake --preset debug && cmake --build build/debug` **configures, builds, and links**
before writing any backend logic. Known unknowns to resolve in the spike:
- exact target name (`box3d::box3d` per the README) and any required `BOX3D_*` cache options;
- whether stepping needs an explicit single-threaded/task config for determinism (default is
  expected to be single-threaded with no task callbacks);
- the exact `b3Body_Get*` rotation getter name against the pinned headers;
- C17-lib-into-C++20 link cleanliness under the project's warning flags.

If the spike surfaces a blocker (Box3D won't build on the toolchain), stop and report before
proceeding — the neutral interface + test design stand regardless of backend.

## Acceptance

- [ ] Box3D builds + links via FetchContent (pinned commit); `modules/physics_box3d` compiles.
- [ ] Neutral `PhysicsBackend` interface + value types in `engine/physics/` (no `b3*` leak).
- [ ] `create_box3d_backend` returns a working backend; box shape, static + dynamic bodies,
      gravity, fixed step, pose read-back.
- [ ] Falling-box **rest** + **determinism** unit tests green.
- [ ] Full unit suite green; the tier/dependency contract holds (no `b3*` outside the module).

## Depends on / relates

- **Depends on:** `foundation/math` (`Vec3`, `Quat`), CMake FetchContent (existing pattern),
  Box3D (new, pinned). **Blocks:** P2 (ECS `PhysicsSystem` builds on this backend).
- **Relates:** the neutral interface is the seam a future backend or a headless test-double
  implements; determinism here is the base of P5's record/replay test.
