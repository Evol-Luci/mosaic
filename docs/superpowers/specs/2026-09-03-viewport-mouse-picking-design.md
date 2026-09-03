# Viewport mouse-picking (design)

**Phase:** post-v0.1 Editor UX (second item, after Inspector add/remove).
**Builds on:** P4 (`PhysicsSystem::raycast`), #37d viewport, #37b `SelectionModel`, foundation math.
**Date:** 2026-09-03.

> Built autonomously (user authorized unattended roadmap work + Antigravity). The pick logic
> (camera-unproject → raycast) is fully headless-testable; the click capture is thin ImGui glue for
> the user's visual acceptance.

## Summary

Click an object in the viewport to **select** it. A click maps to a world-space ray (unprojected
through the active camera), which `PhysicsSystem::raycast` (P4) resolves to the hit entity; the
`SelectionModel` is updated (Hierarchy + Inspector follow). Clicking empty space clears the
selection. The core (`pick_entity`) is a self-contained headless-testable function; the viewport just
feeds it a normalized click coordinate.

## Scope

**In:**
- `World::get_system<T>()` — retrieve a registered system instance (nullptr if none). Needed so the
  editor can reach the `PhysicsSystem` that `setup_runtime` adds internally.
- `studio/pick.hpp` — `pick_entity(World&, aspect, ndc_x, ndc_y) -> RaycastHit`: finds the active
  camera, unprojects a world ray for the click, and raycasts the physics world.
- Viewport click capture in `draw_viewport`: an in-panel click → normalized device coords → `pick_entity`
  → `SelectionModel::select` (or `clear` on a miss).

**Deferred (documented):**
- Picking objects **without a physics collider** (a render-mesh raycast / ID-buffer path) — v0.1
  picks collider-backed entities via `PhysicsSystem::raycast`.
- Drag-select / marquee, gizmo manipulation, hover highlight.

## Architecture

### 1. `World::get_system<T>()` (`engine/world/world.hpp`)

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

### 2. `studio/pick.hpp` — `pick_entity` (self-contained)

The camera unproject uses the camera's world transform (`WorldTransform`) + its `fov`/`aspect`
directly — no matrix inversion beyond what `WorldTransform` already is. Camera looks down **-Z** (RH).

```cpp
inline mosaic::engine::physics::RaycastHit
pick_entity(mosaic::engine::World& world, float aspect, float ndc_x, float ndc_y)
{
    using mosaic::foundation::math::Mat4;
    using mosaic::foundation::math::Vec3;
    namespace render    = mosaic::engine::render;
    namespace transform = mosaic::engine::transform;

    // 1. Active camera: its world transform + vertical fov (mirrors RenderSystem::find_active_camera).
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

    // 2. View-space ray for the click (perspective), then rotate into world.
    const Vec3 dir_view{ndc_x * aspect * tan_half, ndc_y * tan_half, -1.0f};
    const Vec3 dir = mosaic::foundation::math::normalize(
        mosaic::foundation::math::transform_direction(cam_world, dir_view));
    const Vec3 origin{cam_world.m[12], cam_world.m[13], cam_world.m[14]}; // camera world position

    // 3. Raycast the physics world.
    mosaic::engine::physics::PhysicsSystem* physics =
        world.get_system<mosaic::engine::physics::PhysicsSystem>();
    if (physics == nullptr) { return miss; }
    return physics->raycast(origin, dir, 1000.0f);
}
```
`RaycastHit { bool hit; EntityID entity; Vec3 point; Vec3 normal; float distance; }` (P4). `ndc_x`/
`ndc_y` are in `[-1, 1]` (center = 0,0; **+y up**). `studio/` may include `engine/physics` +
`engine/render` (it depends on `mosaic_engine`); Python-free, no new deps.

### 3. Viewport click (`draw_viewport` in `studio/studio_app.cpp`)

`draw_viewport` gains a `World&` parameter (the panel dispatch already has `world`). After the
`ImGui::Image`, capture an in-image click and pick:
```cpp
if (ImGui::IsItemClicked())
{
    const ImVec2 mn = ImGui::GetItemRectMin();
    const ImVec2 mp = ImGui::GetMousePos();
    const float  w  = static_cast<float>(s.width);
    const float  h  = static_cast<float>(s.height);
    const float  ndc_x = 2.0f * (mp.x - mn.x) / w - 1.0f;
    const float  ndc_y = 1.0f - 2.0f * (mp.y - mn.y) / h; // screen y-down -> ndc y-up
    const auto   hit   = pick_entity(world, w / h, ndc_x, ndc_y);
    if (hit.hit) { selection_.select(hit.entity); }
    else         { selection_.clear(); }
}
```

## Data flow

```
click in viewport image -> (u,v) in the image -> ndc (x in [-1,1], y up)
pick_entity: active camera world transform + fov -> view-space ray -> world ray (origin, dir)
             PhysicsSystem::raycast(origin, dir, 1000) -> RaycastHit
hit ? SelectionModel.select(hit.entity) : clear()   -> Hierarchy + Inspector follow
```

## Error handling

- No active camera (or none with a `WorldTransform`) → `pick_entity` returns a miss (`hit=false`); a
  click just clears the selection. Never throws.
- No `PhysicsSystem` in the world → miss. A click on empty space / a collider-less object → miss →
  clears the selection (expected).

## Testability

Headless, deterministic (`tests/unit/studio/pick_test.cpp`; links studio + physics_box3d):
- **Center click hits the object in front:** a `World` with a `TransformSystem` + `PhysicsSystem`; a
  camera entity at `(0,0,5)` (identity rotation → looks -Z) with `Transform`+`Camera`; a **static**
  box at the origin with `Transform`+`RigidBody`+`Collider`; `world.tick` once (derive `WorldTransform`,
  register the body). `pick_entity(world, 1.0f, 0.0f, 0.0f)` → `hit == true` and `entity == box`.
- **Empty space misses:** `pick_entity(world, 1.0f, 0.9f, 0.9f)` (a corner ray that clears the box) →
  `hit == false`.
- **No camera → miss:** a world with a `PhysicsSystem` but no camera → `hit == false`.
- **`World::get_system`** (in `tests/unit/world/` or the pick test): add a `PhysicsSystem`, get it
  back non-null; `get_system<TransformSystem>()` on a world without one → nullptr.
- The `draw_viewport` click glue is thin; verified by build + full suite + the user's visual
  acceptance (click objects in `mosaic editor`, watch the Hierarchy/Inspector follow).

## Ground rules honored

- **Tier wall:** `pick_entity` is Studio-tier logic over engine (render camera + physics raycast) +
  foundation math — Python-free, no new deps. `get_system` is a small, general `World` accessor.
- **Reuses P4** `PhysicsSystem::raycast` (backend-neutral) — no Box3D above the module wall. Catch2
  names avoid a leading `--`.

## Acceptance

- [ ] `World::get_system<T>()` returns a registered system or nullptr (unit-tested).
- [ ] `pick_entity` picks the collider-backed entity under a normalized click (center-hit,
      corner-miss, no-camera-miss), unit-tested headlessly.
- [ ] The viewport selects the clicked entity (and clears on empty) — build + full suite green; user
      visually confirms.

## Depends on / relates

- **Depends on:** P4 `PhysicsSystem::raycast`, #37d viewport, `SelectionModel`, `TransformSystem`
  (camera `WorldTransform`). **Relates:** the deferred gizmo manipulation + collider-less picking;
  the Inspector (selection drives it).
