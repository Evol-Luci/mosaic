# Physics P2 — ECS PhysicsSystem + Visible Drop (design)

**Decomposition:** [physics-decomposition](2026-08-24-physics-decomposition.md) (slice P2).
**Builds on:** P1 (neutral `PhysicsBackend` + Box3D backend), #9 ECS, #18 reflection, #21
transform, #22 runtime loop, 37d viewport, 37e play/stop.
**Date:** 2026-08-24.

## Summary

Make physics **visible and part of the ECS**: reflected `RigidBody` + `Collider`
components, and a `PhysicsSystem` that steps the P1 backend inside `World::tick` and syncs
simulated poses back to `Transform`. The editor demo becomes a **box dropping onto a ground
plane** — Play finally *simulates* (and Pause/Step let you single-step the fall). This is the
"Simulation" leap the roadmap's Phase 2 promises.

## Scope

**In:**
- Reflected `RigidBody` + `Collider` components (`engine/physics/`), registered in
  `register_builtin_components`.
- `PhysicsSystem` (`engine/physics/`): owns an **injected** `PhysicsBackend`, lazily creates
  a backend body per physics entity, steps each tick, writes dynamic-body poses back to
  `Transform` (position + rotation).
- Real **rotation read-back** in the Box3D backend (completing P1's deferral).
- Demo rewire: `build_demo_scene` becomes a static ground + a dynamic falling box (spinning
  cube removed); `mosaic run --window` and `mosaic editor` both show the drop.

**Deferred (later slices / YAGNI):** body removal on entity destroy, non-box shapes (P3),
raycasts (P4), contact events (P5), a reset-world control, and physics for headless
`mosaic run` without a window (the demo is built only on the render path).

## Architecture

### Components (`engine/physics/physics_components.hpp`)

```cpp
struct RigidBody
{
    bool  dynamic = true;   // false = static (never moved by the sim)
    float density = 1.0f;   // for dynamic bodies
};

struct Collider
{
    mosaic::foundation::math::Vec3 half_extents{0.5f, 0.5f, 0.5f}; // box (P2)
};

template <class V> void reflect(V&, RigidBody&);  // members: dynamic (bool), density (float)
template <class V> void reflect(V&, Collider&);    // member: half_extents (Vec3)
```

`RigidBody.dynamic` is a **`bool`, not `BodyType`** — bool is directly reflected (serializes
via #10, shows read-only in the #37c inspector); an enum would render `<unsupported>`. Both
components are registered in `register_builtin_components` so they round-trip and appear in
the inspector like any other component.

### `PhysicsSystem` (`engine/physics/physics_system.hpp`)

```cpp
class PhysicsSystem : public ecs::System
{
public:
    explicit PhysicsSystem(std::unique_ptr<PhysicsBackend> backend, int substeps = 4);
    void update(World& world, float dt) override;
private:
    std::unique_ptr<PhysicsBackend>                          backend_;
    std::unordered_map<mosaic::foundation::EntityID, BodyHandle> bodies_;
    int                                                      substeps_;
};
```

- **Injected backend.** `add_system<S>(Args&&...)` forwards ctor args, so the app does
  `world.add_system<PhysicsSystem>(mosaic::physics::box3d::create_box3d_backend(WorldDef{}))`.
  `PhysicsSystem` depends only on the neutral `PhysicsBackend` — never on
  `modules/physics_box3d`. (`EntityID` needs `std::hash`; it already has one — used by the
  scene resolver.)
- **`update(world, dt)` does, in order:**
  1. **Lazy body creation.** For each entity with `RigidBody` + `Collider` + `Transform` not
     already in `bodies_`: assemble a `BodyDef` (`type` from `RigidBody.dynamic`, `position`
     from `Transform.position`, `shape.half_extents` from `Collider`, `density` from
     `RigidBody`) → `backend_->create_body(def)` → store the handle. (Initial rotation is
     not passed to the backend in P2 — bodies start axis-aligned; authored rotation support
     is a later refinement.)
  2. **Step.** `backend_->step(dt, substeps_)` — one fixed step per tick (deterministic).
  3. **Write-back.** For each tracked entity whose `RigidBody.dynamic` is true, read
     `backend_->body_pose(handle)` and write `pose.position` → `Transform.position` and
     `pose.rotation` → `Transform.rotation`. Static bodies are not written back.
- Declares `access_.reads<RigidBody>().reads<Collider>().writes<Transform>()`.

### Tick / frame integration

`PhysicsSystem` is a **tick system** (added to the world), so the editor's `RuntimeLoop`
gates it: **Play** steps physics, **Pause** freezes it (no tick → `advance` no-ops), **Step**
advances one tick — you can single-step the fall. The editor's per-frame manual
`TransformSystem` (the 37e derived-state refresh) then recomputes `WorldTransform` from the
physics-updated `Transform`, and the viewport renders it. Order within a played frame:
`FramePump.advance` → tick (`PhysicsSystem` steps + writes `Transform`) → `on_frame`
(manual `TransformSystem` → `WorldTransform`, then render).

### Backend rotation read-back (`modules/physics_box3d/box3d_backend.cpp`)

`body_pose` currently returns identity rotation (P1). Complete it: read
`b3Quat q = b3Body_GetRotation(id)` and map `b3Quat{b3Vec3 v; float s;}` → our
`Quat{q.v.x, q.v.y, q.v.z, q.s}` (our `Quat` is `{x,y,z,w}`; `s` is the scalar `w`). No
interface change; the `Pose.rotation` field already exists.

### Demo rewire (`app/run/demo_scene.hpp`)

`build_demo_scene` becomes the physics demo (spinning cube + `SpinSystem` removed):
- A **static ground**: `RigidBody{dynamic=false}` + `Collider{half_extents={5,0.5,5}}` +
  `Transform{scale={10,1,10}}` at y=0, with a `MeshRenderer` (cube mesh) + `Material` so it's
  visible. The unit cube (±0.5) scaled by `{10,1,10}` spans ±5 in x/z and ±0.5 in y —
  matching the collider — so its top surface is at y=0.5.
- A **dynamic box**: `RigidBody{dynamic=true}` + `Collider{half_extents={0.5,0.5,0.5}}` +
  `Transform{position={0,5,0}}` + `MeshRenderer` + textured `Material`; falls and rests at
  y≈1.0 (ground top 0.5 + box half 0.5).
- Camera + directional/point light as before.
- **Physics wiring lives here:** `build_demo_scene` adds the `PhysicsSystem` with an injected
  Box3D backend (`app/run/demo_scene.hpp` may include `modules/physics_box3d`; the app tier
  already depends on `modules/render_vulkan`), so `mosaic run --window` and `mosaic editor`
  both get physics. The camera is raised/aimed so the ground + drop are in frame.

`WorldTransform` for physics entities is added at scene-build (identity/initial) so the first
render before the first tick is valid, exactly as the current demo does for the cube.

## Testability

Physics is headless + deterministic, so P2 gets real assertions plus a functional check.

- **Fake-backend sync test** (`tests/unit/physics/physics_system_test.cpp`): a stub
  `PhysicsBackend` that records `create_body` calls and returns a **scripted descending**
  `body_pose`. Assert `PhysicsSystem`: creates exactly one body per `RigidBody`+`Collider`
  entity (not for non-physics entities), steps once per `update`, and writes the scripted
  pose into each dynamic entity's `Transform` (static entities untouched). Isolates the sync
  logic from Box3D.
- **Headless integration test** (real Box3D backend): a `World` with a `PhysicsSystem`
  (injected `create_box3d_backend`), a dynamic box entity at y=5, and a static ground; tick
  ~120 times at 1/60; assert the box entity's `Transform.position.y` **fell and settled**
  near the expected rest height, deterministically (two runs identical). Proves the full
  ECS↔physics path.
- **Reflection round-trip** (fold into the existing reflection tests or the sync test):
  `RigidBody`/`Collider` reflect → `to_value` → `construct` round-trips.
- **Functional:** `mosaic editor --ticks N` and `mosaic run --window --ticks N` exit 0 with
  **0 validation errors**; the box visibly drops onto the ground (maintainer confirms; Pause
  + Step single-steps the fall).

## Ground rules honored

- **Tier contract:** `PhysicsSystem` + components live in `engine/physics/` and depend only
  on the neutral `PhysicsBackend`; only the app injects the concrete Box3D backend. `b3*`
  stays in `modules/physics_box3d`.
- **Determinism:** one fixed `step(dt, substeps)` per tick; single-threaded backend.
- **Reflected-property surface:** `RigidBody`/`Collider` are ordinary reflected components —
  they serialize and appear in the inspector with no physics-specific editor.
- No new third-party deps. Catch2 test names avoid a leading `--`.

## Acceptance

- [ ] Reflected `RigidBody` + `Collider` registered; round-trip + inspector-visible.
- [ ] `PhysicsSystem` (injected backend) creates bodies, steps in tick, writes dynamic poses
      to `Transform`; fake-backend sync unit test green.
- [ ] Headless integration: an ECS box entity falls + settles deterministically.
- [ ] Box3D backend reads real rotation.
- [ ] `mosaic editor`/`mosaic run --window` show a box dropping onto a ground plane; exit 0,
      0 validation errors.
- [ ] Full unit suite green.

## Depends on / relates

- **Depends on:** P1 (`PhysicsBackend` + Box3D), the `System`/`World::tick` contract, the
  transform system, the viewport (37d) + play/stop (37e). **Blocks:** P3 (shapes/stacking
  build on this system), P5 (contact events plug into the same step).
- **Relates:** scripting (a future pillar) will author `RigidBody`/`Collider` through the
  same reflected surface.
