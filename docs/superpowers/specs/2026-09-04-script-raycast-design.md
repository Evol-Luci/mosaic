# Scripting API: `raycast()` via a neutral host seam (Design)

## Problem

`PhysicsSystem::raycast` exists and is unit-tested, but scripts can't reach it —
so no line-of-sight, ground checks, or in-script picking. The Python backend is
deliberately physics-agnostic, so we need a way to expose raycast **without**
the script backend depending on `PhysicsSystem`.

## Decision (maintainer-approved)

- **Neutral host-callback seam** (not a direct `get_system` in the backend):
  the runtime injects a `raycast` callback into the engine, exactly like
  `stop`/`pause`/`play` already flow through `ScriptHost`. The Python backend
  calls the callback and never sees physics. This scales to future
  system-queries.
- **Events are a separate slice** (dynamic script-event type + subscribe
  routing) — out of scope here.

## Design

### 1. `engine/script/script_host.hpp` — neutral result + callback

```cpp
#include "foundation/math/vec3.hpp"
...
struct ScriptRayHit
{
    bool                           hit = false;
    std::uint64_t                  entity = 0;   // valid only when hit
    mosaic::foundation::math::Vec3 point{};
    mosaic::foundation::math::Vec3 normal{};
    float                          distance = 0.0f;
};

struct ScriptHost
{
    std::function<void(bool)> set_playing;
    std::function<void()>     request_stop;
    // Closest-hit ray query. Unset (empty) => always-miss. No physics type crosses here.
    std::function<ScriptRayHit(mosaic::foundation::math::Vec3 origin,
                               mosaic::foundation::math::Vec3 direction,
                               float max_distance)> raycast;
};
```

### 2. `modules/script_python/python_script_engine.cpp` — `raycast()` global

In `bind_host`, install a `raycast(origin, direction, max_distance)` global.
`origin`/`direction` are 3-element Python sequences (`[x, y, z]`); the result is
a dict `{hit: bool, entity: int, distance: float, point: {x,y,z}, normal: {x,y,z}}`
(point/normal as `{x,y,z}` dicts to match the component-vector convention). If
`host_.raycast` is unset, returns a miss dict (consistent shape).

### 3. `app/run/demo_scene.hpp` — inject the physics-backed callback

A helper keeps the physics wiring in the app tier (where physics already lives):

```cpp
inline std::function<mosaic::engine::script::ScriptRayHit(Vec3, Vec3, float)>
make_physics_raycast(mosaic::engine::World& world)
{
    return [&world](Vec3 o, Vec3 d, float max) -> mosaic::engine::script::ScriptRayHit
    {
        auto* phys = world.get_system<mosaic::engine::physics::PhysicsSystem>();
        if (phys == nullptr) { return {}; }
        const auto h = phys->raycast(o, d, max);
        return {h.hit, h.entity.value(), h.point, h.normal, h.distance};
    };
}
```

In `setup_runtime`, before adding `ScriptSystem`: `if (!host.raycast) { host.raycast = make_physics_raycast(world); }`.
Lazy `get_system` at call time means system-add order doesn't matter; capturing
`&world` is safe (the world owns the systems that own the host copies). Every
`setup_runtime` caller (`mosaic run`, editor) gets `raycast()` for free; the
Python backend stays physics-free.

## Testing (headless)

1. **Seam / marshaling** (`script_raycast_test.cpp`): `bind_host` with a stub
   `raycast` returning a known hit; a script calls
   `raycast([0,0,5],[0,0,-1],100)` and asserts the dict
   (`hit`, `entity`, `point['y']`, `distance`). No physics — proves the
   backend seam + arg/result marshaling. Also: unset callback => `hit == False`.
2. **Physics-backed helper** (in `script_runtime_test.cpp`): mirror
   `physics_raycast_test` — a static box (Transform+RigidBody+Collider), `tick`,
   then `make_physics_raycast(world)(...)` returns the hit entity + distance.
3. **End-to-end** (optional, cheap): `setup_runtime` + the static box + a
   scripted entity whose `on_update` raycasts and writes `hit` into its own
   Transform; after a tick the value reflects the hit — proves the full
   script→host→physics path.

## Out of scope / follow-ups

- Events (emit/subscribe) — own slice.
- All-hits / shape casts, layer masks — future.
- Editor-only raycast wiring beyond what `setup_runtime` already covers.
