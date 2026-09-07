# VFX V1 — Trails (billboard-chain) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A `TrailRenderer` component leaves a fading billboard-chain tail behind a moving entity, rendered through the existing `draw_particles` seam.

**Architecture:** Mirrors particles: a reflected `TrailRenderer`, a `TrailSystem` that samples each entity's `Transform.position` into a bounded per-entity history (transient state), and a `RenderSystem` pass that appends tapered `ParticleInstance`s (one per history point) to the same billboard batch the particle pass builds. No new device method or Vulkan pipeline.

**Tech Stack:** C++20, header-only engine modules, Catch2, CMake/CTest. No new dependencies.

**Spec:** [docs/superpowers/specs/2026-09-07-vfx-trails-design.md](2026-09-07-vfx-trails-design.md)

## Global Constraints

- C++20; header-only under `engine/vfx/`; no new dependency; no new render-device method / Vulkan pipeline (reuse `draw_particles`).
- `TrailSystem` writes nothing back to components; history is transient state keyed by entity id; prune on entity destroy.
- Sample at most one point per tick (an `if` on the accumulator, not a `while`); seed the head on first sight and start sampling the next tick (no duplicate head).
- Ring buffer: drop oldest from the front past `max_points` (min effective cap 1).
- Render taper: point `i` of `n` (0=oldest) → `frac = (i+1)/n`; `ParticleInstance{ pos, size = trail.size*frac, color = trail.color, alpha = frac }` (newest full/opaque).
- Tests tagged `[vfx]` / `[render]`; new test file added to `tests/CMakeLists.txt`. Build target `mosaic_unit_tests`; preset dir `build/debug`.

---

### Task 1: `TrailRenderer` component + `TrailSystem`

**Files:**
- Create: `engine/vfx/trail_renderer.hpp`
- Create: `engine/vfx/trail_system.hpp`
- Modify: `engine/reflection/builtin_components.hpp` (include + register)
- Create: `tests/unit/vfx/trail_system_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `mosaic::engine::vfx::TrailRenderer{ Vec3 color; float size; int max_points; float sample_interval; }` + `reflect`; `mosaic::engine::vfx::TrailSystem : ecs::System` with `points_for(EntityID) -> const std::vector<Vec3>*`.

- [ ] **Step 1: Create `engine/vfx/trail_renderer.hpp`**

```cpp
#pragma once

#include <cstdint>

#include "foundation/math/vec3.hpp"

namespace mosaic::engine::vfx
{
using mosaic::foundation::math::Vec3;

// A fading tail left behind a moving entity. Reflected + serialized; the live position
// history lives in TrailSystem, not here.
struct TrailRenderer
{
    Vec3  color           = Vec3{1.0f, 1.0f, 1.0f}; // trail color
    float size            = 0.1f;                    // billboard edge length at the head (newest)
    int   max_points      = 32;                      // history length (ring-buffer cap)
    float sample_interval = 0.03f;                   // seconds between position samples
};

template <class V>
void reflect(V& visitor, TrailRenderer& t)
{
    visitor.member("color", t.color);
    visitor.member("size", t.size);
    visitor.member("max_points", t.max_points);
    visitor.member("sample_interval", t.sample_interval);
}
} // namespace mosaic::engine::vfx
```

- [ ] **Step 2: Create `engine/vfx/trail_system.hpp`**

```cpp
#pragma once

#include <cstdint>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "engine/ecs/system.hpp"
#include "engine/transform/transform.hpp"
#include "engine/vfx/trail_renderer.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::engine::vfx
{
// Samples each TrailRenderer entity's Transform.position into a bounded history so the
// renderer can draw a fading tail. Transient state keyed by entity id (the ParticleSystem
// pattern); never serialized; writes nothing back to components.
class TrailSystem : public ecs::System
{
public:
    void update(World& world, float dt) override
    {
        world_ = &world;

        std::unordered_set<std::uint64_t> seen;
        world.for_each<TrailRenderer>(
            [&](mosaic::foundation::EntityID e, TrailRenderer& tr)
            {
                auto* tf = world.get_component<transform::Transform>(e);
                if (tf == nullptr) { return; }
                const std::uint64_t id  = e.value();
                seen.insert(id);
                const int           cap = tr.max_points > 0 ? tr.max_points : 1;

                auto it = trails_.find(id);
                if (it == trails_.end())
                {
                    TrailState st;
                    st.points.push_back(tf->position); // seed the head; sample from next tick
                    trails_.emplace(id, std::move(st));
                    return;
                }
                TrailState& st = it->second;

                st.accumulator += dt;
                if (st.accumulator >= tr.sample_interval)
                {
                    st.accumulator -= tr.sample_interval;
                    st.points.push_back(tf->position);
                }
                while (static_cast<int>(st.points.size()) > cap) { st.points.erase(st.points.begin()); }
            });

        for (auto it = trails_.begin(); it != trails_.end();)
        {
            it = (seen.count(it->first) != 0) ? std::next(it) : trails_.erase(it);
        }
    }

    // Position history oldest -> newest for `e`, or nullptr if none / the entity was destroyed.
    const std::vector<mosaic::foundation::math::Vec3>* points_for(
        mosaic::foundation::EntityID e) const
    {
        if (world_ && !world_->is_valid(e)) { return nullptr; }
        auto it = trails_.find(e.value());
        return it == trails_.end() ? nullptr : &it->second.points;
    }

private:
    struct TrailState
    {
        std::vector<mosaic::foundation::math::Vec3> points;
        float                                       accumulator = 0.0f;
    };

    std::unordered_map<std::uint64_t, TrailState> trails_;
    const World* world_ = nullptr;
};
} // namespace mosaic::engine::vfx
```

- [ ] **Step 3: Register `TrailRenderer` as a builtin**

In `engine/reflection/builtin_components.hpp`, add the include next to the others:

```cpp
#include "engine/vfx/trail_renderer.hpp"
```

and, inside `register_builtin_components`, after the `AudioListener` registration line:

```cpp
    types.register_component<mosaic::engine::vfx::TrailRenderer>(registry, "TrailRenderer");
```

- [ ] **Step 4: Create `tests/unit/vfx/trail_system_test.cpp`**

```cpp
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/transform/transform.hpp"
#include "engine/vfx/trail_renderer.hpp"
#include "engine/vfx/trail_system.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reflect_value.hpp"

using Catch::Approx;
using mosaic::engine::World;
using mosaic::engine::transform::Transform;
using mosaic::engine::vfx::TrailRenderer;
using mosaic::engine::vfx::TrailSystem;
using mosaic::foundation::math::Vec3;

namespace
{
void set_pos(World& world, mosaic::foundation::EntityID e, float x)
{
    world.get_component<Transform>(e)->position = Vec3{x, 0.0f, 0.0f};
}
} // namespace

TEST_CASE("TrailSystem samples one point per interval and tracks the newest position", "[vfx]")
{
    World world;
    auto& sys = world.add_system<TrailSystem>();
    const auto e = world.create_entity();
    Transform tf; tf.position = Vec3{0.0f, 0.0f, 0.0f};
    world.add_component<Transform>(e, tf);
    TrailRenderer tr; tr.sample_interval = 0.1f; tr.max_points = 10;
    world.add_component<TrailRenderer>(e, tr);

    world.tick(0.1f);                 // first sight: seed head at x=0
    set_pos(world, e, 1.0f); world.tick(0.1f); // sample x=1
    set_pos(world, e, 2.0f); world.tick(0.1f); // sample x=2

    const auto* pts = sys.points_for(e);
    REQUIRE(pts != nullptr);
    REQUIRE(pts->size() == 3);
    REQUIRE(pts->front().x == Approx(0.0f)); // oldest
    REQUIRE(pts->back().x == Approx(2.0f));  // newest = current position
}

TEST_CASE("TrailSystem respects max_points and drops the oldest", "[vfx]")
{
    World world;
    auto& sys = world.add_system<TrailSystem>();
    const auto e = world.create_entity();
    Transform tf; tf.position = Vec3{0.0f, 0.0f, 0.0f};
    world.add_component<Transform>(e, tf);
    TrailRenderer tr; tr.sample_interval = 0.1f; tr.max_points = 2;
    world.add_component<TrailRenderer>(e, tr);

    world.tick(0.1f);                                   // seed x=0 -> [0]
    set_pos(world, e, 1.0f); world.tick(0.1f);          // [0,1]
    set_pos(world, e, 2.0f); world.tick(0.1f);          // [0,1,2] -> trim -> [1,2]
    set_pos(world, e, 3.0f); world.tick(0.1f);          // [1,2,3] -> trim -> [2,3]

    const auto* pts = sys.points_for(e);
    REQUIRE(pts->size() == 2);
    REQUIRE(pts->front().x == Approx(2.0f));
    REQUIRE(pts->back().x == Approx(3.0f));
}

TEST_CASE("TrailSystem drops history when the entity is destroyed", "[vfx]")
{
    World world;
    auto& sys = world.add_system<TrailSystem>();
    const auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    world.add_component<TrailRenderer>(e, TrailRenderer{});

    world.tick(0.1f);
    REQUIRE(sys.points_for(e) != nullptr);
    world.destroy_entity(e);
    world.tick(0.1f); // still seen this tick
    world.tick(0.1f); // now pruned
    REQUIRE(sys.points_for(e) == nullptr);
}

TEST_CASE("TrailSystem history is deterministic across runs", "[vfx]")
{
    auto run = []
    {
        World world;
        auto& sys = world.add_system<TrailSystem>();
        const auto e = world.create_entity();
        Transform tf; tf.position = Vec3{0.0f, 0.0f, 0.0f};
        world.add_component<Transform>(e, tf);
        TrailRenderer tr; tr.sample_interval = 0.05f; tr.max_points = 8;
        world.add_component<TrailRenderer>(e, tr);
        for (int i = 1; i <= 6; ++i) { set_pos(world, e, static_cast<float>(i)); world.tick(0.05f); }
        return *sys.points_for(e);
    };
    const auto a = run();
    const auto b = run();
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) { REQUIRE(a[i].x == b[i].x); }
}

TEST_CASE("TrailRenderer round-trips through reflection", "[vfx]")
{
    TrailRenderer t;
    t.color           = Vec3{0.2f, 0.4f, 0.6f};
    t.size            = 0.25f;
    t.max_points      = 48;
    t.sample_interval = 0.02f;

    const auto value = mosaic::foundation::serialization::to_value(t);
    TrailRenderer back;
    mosaic::foundation::serialization::from_value(value, back);

    REQUIRE(back.color.x == Approx(0.2f));
    REQUIRE(back.size == Approx(0.25f));
    REQUIRE(back.max_points == 48);
    REQUIRE(back.sample_interval == Approx(0.02f));
}

TEST_CASE("TrailRenderer is a registered built-in component", "[vfx]")
{
    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    REQUIRE(types.has("TrailRenderer"));
}
```

- [ ] **Step 5: Add the test file to `tests/CMakeLists.txt`**

In `tests/CMakeLists.txt`, after the line `  unit/app/audio_asset_test.cpp`, add:

```cmake
  unit/vfx/trail_system_test.cpp
```

- [ ] **Step 6: Build and run the `[vfx]` tests**

Run: `cmake --build build/debug --target mosaic_unit_tests && ctest --test-dir build/debug -R "TrailSystem|TrailRenderer" --output-on-failure`
Expected: PASS — sampling, max_points, prune, determinism, reflection round-trip, builtin registration.

- [ ] **Step 7: Commit**

```bash
git add engine/vfx/trail_renderer.hpp engine/vfx/trail_system.hpp engine/reflection/builtin_components.hpp tests/unit/vfx/trail_system_test.cpp tests/CMakeLists.txt
git commit -m "feat(vfx): TrailRenderer component + TrailSystem (position-history sampling)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: RenderSystem trail pass

**Files:**
- Modify: `engine/render/render_system.hpp` (billboard pass: particles + trails)
- Modify: `tests/unit/render/render_system_test.cpp` (trail taper test)

**Interfaces:**
- Consumes: `TrailSystem::points_for`, `TrailRenderer` (Task 1); `ParticleInstance`/`draw_particles` (existing).

- [ ] **Step 1: Include the VFX headers in `render_system.hpp`**

In `engine/render/render_system.hpp`, add near the other engine includes:

```cpp
#include "engine/vfx/trail_renderer.hpp"
#include "engine/vfx/trail_system.hpp"
```

- [ ] **Step 2: Replace the particle pass with a particles+trails billboard pass**

In `engine/render/render_system.hpp`, replace the block that begins with the comment
`// Particle pass:` and runs through its closing `if (!instances.empty()) { device.draw_particles(...); }`
(the whole `auto* particles = ...; if (particles != nullptr) { ... }` section) with:

```cpp
        // Billboard pass: particles + trails share one instanced, camera-facing draw. Runs
        // last in the pass (the device binds the particle pipeline; no mesh draw may follow).
        auto* particles = world.get_system<mosaic::engine::particles::ParticleSystem>();
        auto* trails    = world.get_system<mosaic::engine::vfx::TrailSystem>();
        if (particles != nullptr || trails != nullptr)
        {
            const Mat4 cam_world = Mat4::inverse_affine(view);
            const Vec3 cam_right = mosaic::foundation::math::normalize(
                Vec3{cam_world.m[0], cam_world.m[1], cam_world.m[2]});
            const Vec3 cam_up = mosaic::foundation::math::normalize(
                Vec3{cam_world.m[4], cam_world.m[5], cam_world.m[6]});

            std::vector<ParticleInstance> instances;

            if (particles != nullptr)
            {
                world.for_each<mosaic::engine::particles::ParticleEmitter>(
                    [&](mosaic::foundation::EntityID e, mosaic::engine::particles::ParticleEmitter& em)
                    {
                        const auto* pool = particles->particles_for(e);
                        if (pool == nullptr) { return; }
                        for (const auto& p : *pool)
                        {
                            const float t = (p.lifetime > 0.0f) ? p.age / p.lifetime : 0.0f;
                            instances.push_back(ParticleInstance{
                                p.position,
                                mosaic::engine::particles::size_at(em, t),
                                mosaic::engine::particles::color_at(em, t),
                                mosaic::engine::particles::alpha_at(em, t)});
                        }
                    });
            }

            if (trails != nullptr)
            {
                world.for_each<mosaic::engine::vfx::TrailRenderer>(
                    [&](mosaic::foundation::EntityID e, mosaic::engine::vfx::TrailRenderer& tr)
                    {
                        const auto* pts = trails->points_for(e);
                        if (pts == nullptr || pts->empty()) { return; }
                        const int n = static_cast<int>(pts->size());
                        for (int i = 0; i < n; ++i)
                        {
                            const float frac = static_cast<float>(i + 1) / static_cast<float>(n);
                            instances.push_back(
                                ParticleInstance{(*pts)[i], tr.size * frac, tr.color, frac});
                        }
                    });
            }

            if (!instances.empty())
            {
                device.draw_particles(instances,
                                      ParticleDrawParams{projection * view, cam_right, cam_up});
            }
        }
```

- [ ] **Step 3: Write the trail taper render test**

In `tests/unit/render/render_system_test.cpp`, add near the other includes:

```cpp
#include "engine/vfx/trail_renderer.hpp"
#include "engine/vfx/trail_system.hpp"
```

and append:

```cpp
TEST_CASE("RenderSystem draws a trail as tapered billboards", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);
    add_camera(world, Vec3{0, 0, 4});
    world.add_system<mosaic::engine::vfx::TrailSystem>();

    auto e = world.create_entity();
    world.add_component<mosaic::engine::transform::Transform>(
        e, mosaic::engine::transform::Transform{});
    mosaic::engine::vfx::TrailRenderer tr;
    tr.size            = 0.2f;
    tr.color           = Vec3{0.1f, 0.2f, 0.3f};
    tr.sample_interval = 0.05f;
    tr.max_points      = 10;
    world.add_component<mosaic::engine::vfx::TrailRenderer>(e, tr);

    for (int i = 0; i < 4; ++i) { world.tick(0.05f); } // seed + 3 samples -> 4 points

    rs.render(world, device, registry);
    REQUIRE(device.particle_draws.size() == 1);
    const auto& call = device.particle_draws[0];
    REQUIRE(call.instances.size() == 4);
    // Oldest (index 0) is faintest/smallest; newest (last) is full alpha + full size.
    REQUIRE(call.instances.front().alpha == Approx(0.25f));
    REQUIRE(call.instances.back().alpha == Approx(1.0f));
    REQUIRE(call.instances.back().size == Approx(0.2f));
    REQUIRE(call.instances.back().color.x == Approx(0.1f));
}
```

- [ ] **Step 4: Build and run the render tests**

Run: `cmake --build build/debug --target mosaic_unit_tests && ctest --test-dir build/debug -R "RenderSystem|draws a trail" --output-on-failure`
Expected: PASS — the new trail taper case plus all existing `[render]` particle cases (unchanged; the particle path still runs when a `ParticleSystem` is present).

- [ ] **Step 5: Commit**

```bash
git add engine/render/render_system.hpp tests/unit/render/render_system_test.cpp
git commit -m "feat(vfx): render trails as tapered billboards in the shared particle pass

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: Wire `TrailSystem` into the runtime + demo + verify + handoff

**Files:**
- Modify: `app/run/demo_scene.hpp` (`setup_runtime` adds `TrailSystem`; sphere gets a `TrailRenderer`)
- Modify: `docs/project/next-steps.md`

**Interfaces:**
- Consumes: `TrailSystem`, `TrailRenderer` (Task 1). No new API.

- [ ] **Step 1: Include the VFX headers in `demo_scene.hpp`**

In `app/run/demo_scene.hpp`, add near the other engine includes:

```cpp
#include "engine/vfx/trail_renderer.hpp"
#include "engine/vfx/trail_system.hpp"
```

- [ ] **Step 2: Add `TrailSystem` in `setup_runtime`**

In `setup_runtime`, immediately after the `ParticleSystem` line
(`world.add_system<mosaic::engine::particles::ParticleSystem>();`), add:

```cpp
    // Trails: sample moving entities' positions for the VFX billboard-chain pass.
    world.add_system<mosaic::engine::vfx::TrailSystem>();
```

- [ ] **Step 3: Give the falling sphere a demo trail**

In `build_default_scene`, after the sphere's `SphereCollider` line
(`world.add_component<physics::SphereCollider>(sphere, physics::SphereCollider{0.5f});`), add:

```cpp
    mosaic::engine::vfx::TrailRenderer sphere_trail;
    sphere_trail.color           = Vec3{0.4f, 0.7f, 1.0f}; // pale blue tail
    sphere_trail.size            = 0.18f;
    sphere_trail.max_points      = 40;
    sphere_trail.sample_interval = 0.02f;
    world.add_component<mosaic::engine::vfx::TrailRenderer>(sphere, sphere_trail);
```

- [ ] **Step 4: Build and run the full suite**

Run: `cmake --build build/debug --target mosaic_unit_tests && ctest --test-dir build/debug`
Expected: PASS. Default-scene entity/component counts are unchanged (a component was added to the existing sphere; no new entity). (If the threaded `ScriptWorker` case flakes, re-run: `ctest --test-dir build/debug --rerun-failed --output-on-failure`.)

- [ ] **Step 5: Functional smoke check**

Run: `cmake --build build/debug --target mosaic && ./build/debug/app/mosaic run --window --ticks 90 2>&1 | tail -5; echo "exit: ${PIPESTATUS[0]}"`
Expected: exit 0, no new Vulkan validation errors (the falling sphere now leaves a pale-blue billboard tail).

- [ ] **Step 6: Update the handoff doc**

In `docs/project/next-steps.md`: add a **VFX — V1 (trails) done** entry — a
`TrailRenderer` component + `TrailSystem` (position-history sampling, transient state,
prune-on-destroy) rendered as a fading **billboard chain** through the existing
`draw_particles` seam (no new device method / Vulkan pipeline); wired into
`setup_runtime`; demo trail on the falling sphere. Note VFX follow-ups: connected
**ribbon** geometry (`draw_ribbon` seam + Vulkan pipeline), per-trail texture /
width- & color-over-length curves, then **decals** and **screen/post-processing**
effects. Update the top "Last updated" summary; keep "Remaining Phase-4 subsystems:
UI, networking" (VFX now has a first slice).

- [ ] **Step 7: Commit**

```bash
git add app/run/demo_scene.hpp docs/project/next-steps.md
git commit -m "feat(vfx): wire TrailSystem into setup_runtime + demo trail on the sphere

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Self-Review

**Spec coverage:**
- `TrailRenderer` reflected component (color/size/max_points/sample_interval) + builtin → Task 1 Steps 1,3; tests Step 4. ✓
- `TrailSystem` sampling (one/tick, seed head), ring-buffer cap, prune, `points_for`, determinism → Task 1 Step 2; tests Step 4. ✓
- Render pass appends tapered `ParticleInstance`s to the shared batch, `frac=(i+1)/n` → Task 2 Step 2; taper test Step 3. ✓
- No new device method / Vulkan (reuse `draw_particles`) → Task 2 (only `render_system.hpp` touched). ✓
- Wiring in `setup_runtime` + demo on the falling sphere (existing entity → counts unchanged) → Task 3 Steps 2-4. ✓

**Placeholder scan:** No TBD/TODO; every code step has complete code. ✓

**Type consistency:** `TrailRenderer{Vec3 color; float size; int max_points; float sample_interval}`; `TrailSystem::points_for -> const std::vector<Vec3>*`; render uses `ParticleInstance{Vec3, float size, Vec3 color, float alpha}` (matches `render_device.hpp`); `world.get_system<vfx::TrailSystem>()` / `world.add_system<vfx::TrailSystem>()` / `world.get_component<Transform>` all match existing usage; `types.has("TrailRenderer")` matches the builtin-test API. ✓
