# Scripting Runtime Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire the existing `ScriptSystem` into the runtime so `ScriptComponent`s tick on Play in both the editor and a built standalone, ship an orbiting scripted demo object, and add an editor toggle that swaps that object between script-driven and physics-driven.

**Architecture:** `ScriptSystem` is added inside `setup_runtime` (before `PhysicsSystem`), the one scaffolding function both the editor and `mosaic run --window` (the bundle) call; its signature grows to take the `ComponentReflection` + an injected engine factory. `build_default_scene` gains an orbiting green cube (`ScriptComponent`, no RigidBody). A pure `set_demo_physics()` swap function + a thin editor checkbox toggle the object's mode.

**Tech Stack:** C++20, CMake, Catch2, ImGui, pybind11 (from S1, via the injected factory). Reuses S4 (`ScriptComponent`/`ScriptSystem`), S5 (`bind_world(const&)`, app links `mosaic_script_python`), `PhysicsSystem`.

**Spec:** `docs/superpowers/specs/2026-09-02-scripting-runtime-integration-design.md`

## Global Constraints

- **Tier wall:** `app/run/demo_scene.hpp` and `studio/` use the neutral `ScriptSystem`/`ScriptComponent` + reflected physics components + a `std::function` factory — NO pybind11. CPython enters only via the app-supplied factory `[] { return mosaic::script::python::create_python_script_engine(); }`.
- **Tick order:** `ScriptSystem` is added BEFORE `PhysicsSystem` in `setup_runtime`.
- **Reflection-derived / #10:** the demo object is ordinary reflected components; `ScriptComponent` serializes as a string and round-trips through save/build/run.
- **Never throw:** a failing per-tick script is non-fatal (S4 `ScriptSystem` behavior, unchanged).
- **Build:** `cmake --build build/debug -j`; **Test:** `ctest --test-dir build/debug`. Catch2 names not starting with `--`. Namespaces: `mosaic::app`, `mosaic::studio`, `mosaic::engine::script`, `mosaic::engine::reflection`, `mosaic::engine::physics`, `mosaic::script::python`.
- **API facts (verified):** `RigidBody{bool dynamic=true; float density=1.0f}`; `Collider{Vec3 half_extents{0.5,0.5,0.5}}`; both reflected. `World::add_component<T>`/`remove_component<T>`/`has_component<T>`/`for_each<T>(fn: EntityID,T&)`. `ScriptSystem(reflection::ComponentReflection&, EngineFactory)` where `EngineFactory = std::function<std::unique_ptr<ScriptEngine>()>`. `setup_runtime` currently `(World&, AssetRegistry&)` at `app/run/demo_scene.hpp`, called at `app/main.cpp:188` (windowed run; `types` in scope) and `app/run/editor.cpp:120` (`reflection` in scope; already `#include`s python_script_engine.hpp). `mosaic_unit_tests` links `mosaic_app` + `mosaic_studio` + `mosaic_script_python`. Default scene has 7 Transform-bearing entities; `demo_scene_test.cpp` asserts `transforms == 7` and calls the 2-arg `setup_runtime`; `scene_save_roundtrip_test.cpp:40` asserts `b == 7`.

---

### Task 1: Wire `ScriptSystem` into `setup_runtime`

**Files:**
- Modify: `app/run/demo_scene.hpp` (signature + add ScriptSystem; includes)
- Modify: `app/main.cpp:188` (pass types + factory; add include)
- Modify: `app/run/editor.cpp:120` (pass reflection + factory)
- Modify: `tests/unit/app/demo_scene_test.cpp` (update the `setup_runtime` call — signature changed)
- Create: `tests/unit/app/script_runtime_test.cpp` (wiring test)
- Modify: `tests/CMakeLists.txt` (register the new test)

**Interfaces:**
- Produces: `RuntimeScene setup_runtime(World&, AssetRegistry&, const ComponentReflection&, ScriptSystem::EngineFactory)` — now adds `ScriptSystem` before `PhysicsSystem`.

- [ ] **Step 1: Write the failing wiring test `tests/unit/app/script_runtime_test.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

#include "app/run/demo_scene.hpp"
#include "engine/assets/asset_registry.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/script/script_component.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "modules/script_python/python_script_engine.hpp"

using mosaic::app::setup_runtime;
using mosaic::engine::World;
using mosaic::engine::assets::AssetRegistry;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::script::ScriptComponent;
using mosaic::engine::transform::Transform;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::script::python::create_python_script_engine;

TEST_CASE("setup_runtime wires ScriptSystem so scripts tick in the runtime", "[app]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    world.add_component<ScriptComponent>(e, ScriptComponent{
        "def on_update(dt):\n"
        "    t = get_component(entity, 'Transform')\n"
        "    t['position']['x'] += dt\n"
        "    set_component(entity, 'Transform', t)\n"});

    AssetRegistry registry;
    setup_runtime(world, registry, types, [] { return create_python_script_engine(); });

    for (int i = 0; i < 4; ++i) { world.tick(0.25f); } // 0.25 * 4, binary-exact
    REQUIRE(world.get_component<Transform>(e)->position.x == 1.0f);
}
```

- [ ] **Step 2: Register the test + confirm it fails to build**

Add to `tests/CMakeLists.txt` after `unit/app/demo_scene_test.cpp`:
```cmake
  unit/app/script_runtime_test.cpp
```
Build → FAIL (`setup_runtime` takes 2 args, not 4).

- [ ] **Step 3: Change `setup_runtime` in `app/run/demo_scene.hpp`**

Add includes near the other engine includes:
```cpp
#include "engine/script/script_component.hpp"
#include "engine/script/script_system.hpp"
```
Change the signature and add `ScriptSystem` as the FIRST system (before the existing `PhysicsSystem` add). The function currently begins:
```cpp
inline RuntimeScene setup_runtime(mosaic::engine::World&                 world,
                                  mosaic::engine::assets::AssetRegistry& registry)
{
    namespace render  = mosaic::engine::render;
    namespace physics = mosaic::engine::physics;

    world.add_system<physics::PhysicsSystem>(
        mosaic::physics::box3d::create_box3d_backend(physics::WorldDef{}));
```
Replace those lines with:
```cpp
inline RuntimeScene setup_runtime(
    mosaic::engine::World&                                  world,
    mosaic::engine::assets::AssetRegistry&                  registry,
    const mosaic::engine::reflection::ComponentReflection&  reflection,
    mosaic::engine::script::ScriptSystem::EngineFactory     script_factory)
{
    namespace render  = mosaic::engine::render;
    namespace physics = mosaic::engine::physics;

    // Gameplay scripts run BEFORE physics: a script-only object (no RigidBody) is left untouched
    // by physics, and a physics object's write-back wins over any script that also touches it.
    world.add_system<mosaic::engine::script::ScriptSystem>(reflection, std::move(script_factory));

    world.add_system<physics::PhysicsSystem>(
        mosaic::physics::box3d::create_box3d_backend(physics::WorldDef{}));
```
(Everything after the PhysicsSystem line — texture registration, RenderSystem — is unchanged.) Add `#include <utility>` for `std::move` if not already present; also ensure `#include "engine/reflection/component_reflection.hpp"` is included (it is transitively via world/reflection headers, but add it explicitly for clarity).

- [ ] **Step 4: Update the caller in `app/main.cpp`**

Add near the other includes:
```cpp
#include "modules/script_python/python_script_engine.hpp"
```
Change line 188 from:
```cpp
        auto render_system = setup_runtime(world, asset_registry).render_system;
```
to:
```cpp
        auto render_system =
            setup_runtime(world, asset_registry, types,
                          [] { return mosaic::script::python::create_python_script_engine(); })
                .render_system;
```
(`types` is the `ComponentReflection` declared earlier in the run path.)

- [ ] **Step 5: Update the caller in `app/run/editor.cpp`**

`editor.cpp` already `#include`s `modules/script_python/python_script_engine.hpp`. Change line 120 from:
```cpp
    auto render_system = setup_runtime(world, asset_registry).render_system;
```
to:
```cpp
    auto render_system =
        setup_runtime(world, asset_registry, reflection,
                      [] { return mosaic::script::python::create_python_script_engine(); })
            .render_system;
```
(`reflection` is the local `ComponentReflection`.)

- [ ] **Step 6: Update `demo_scene_test.cpp`'s `setup_runtime` call (its Test 2)**

The test "setup_runtime registers built-in meshes..." calls the old 2-arg form. Add includes:
```cpp
#include "engine/reflection/builtin_components.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "modules/script_python/python_script_engine.hpp"
```
Change:
```cpp
    auto runtime = setup_runtime(world, registry);
```
to:
```cpp
    mosaic::foundation::reflection::TypeRegistry     reg;
    mosaic::engine::reflection::ComponentReflection  types;
    mosaic::engine::reflection::register_builtin_components(reg, types);
    auto runtime = setup_runtime(
        world, registry, types,
        [] { return mosaic::script::python::create_python_script_engine(); });
```
(Do NOT touch its first test — `build_default_scene` still has 7 Transforms until Task 2.)

- [ ] **Step 7: Build + test**
```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "wires ScriptSystem|setup_runtime registers" --output-on-failure
```
Expected: both PASS. Then the full suite once (`ctest --test-dir build/debug 2>&1 | tail -3`) — 0 failures.

- [ ] **Step 8: Commit**
```bash
git add app/run/demo_scene.hpp app/main.cpp app/run/editor.cpp tests/unit/app/demo_scene_test.cpp tests/unit/app/script_runtime_test.cpp tests/CMakeLists.txt
git commit -m "feat(app): wire ScriptSystem into setup_runtime (scripts tick in editor + runtime)"
```

---

### Task 2: Orbiting scripted demo object in the default scene

**Files:**
- Modify: `app/run/demo_scene.hpp` (`build_default_scene` + a `kOrbitScript` constant)
- Modify: `tests/unit/app/demo_scene_test.cpp` (Transform count 7 → 8; add an orbiter assertion)
- Modify: `tests/unit/app/scene_save_roundtrip_test.cpp:40` (count 7 → 8)

**Interfaces:**
- Consumes: `ScriptComponent` (from S4), the reflected component set.
- Produces: `build_default_scene` now creates one entity with a `ScriptComponent` (the orbiter).

- [ ] **Step 1: Update the failing count in `demo_scene_test.cpp` + add the orbiter assertion**

Change the first test's assertion + comment (the orbiter adds an 8th Transform-bearing entity):
```cpp
    // ground, box, sphere, capsule, box2, point-light, camera, orbiter carry a Transform (8);
    // the directional light has none.
    REQUIRE(transforms == 8);
```
Add a new test after it:
```cpp
TEST_CASE("build_default_scene includes one scripted (ScriptComponent) demo object", "[app]")
{
    World world;
    build_default_scene(world);

    int scripted = 0;
    world.for_each<mosaic::engine::script::ScriptComponent>(
        [&](World::EntityID, mosaic::engine::script::ScriptComponent& sc)
        {
            ++scripted;
            REQUIRE(sc.source.find("on_update") != std::string::npos);
        });
    REQUIRE(scripted == 1);
}
```
Add the include `#include "engine/script/script_component.hpp"` and `#include <string>` to the test file.

- [ ] **Step 2: Update `scene_save_roundtrip_test.cpp`**

Change line 40 from `REQUIRE(b == 7);` to `REQUIRE(b == 8);` (the orbiter's Transform round-trips too). If the test has an inline comment about the count, update it to mention the orbiter.

- [ ] **Step 3: Run to verify the two tests now fail**
```bash
cmake --build build/debug -j 2>&1 | tail -3
ctest --test-dir build/debug -R "creates the demo entities|scripted .ScriptComponent. demo|round" --output-on-failure
```
Expected: FAIL (count is still 7 / no scripted object — not implemented yet).

- [ ] **Step 4: Add the orbiter to `build_default_scene` in `app/run/demo_scene.hpp`**

Add this constant just above `build_default_scene` (a raw string so the Python needs no escaping):
```cpp
// The demo's scripted object orbits a radius-3 circle at y=2. Deterministic (cos/sin of a fixed-dt
// accumulator). Reflected ScriptComponent, so it serializes with the scene and survives build/run.
inline constexpr const char* kOrbitScript = R"py(
import math
_t = 0.0
def on_update(dt):
    global _t
    _t += dt
    tr = get_component(entity, 'Transform')
    tr['position']['x'] = 3.0 * math.cos(_t)
    tr['position']['z'] = 3.0 * math.sin(_t)
    tr['position']['y'] = 2.0
    set_component(entity, 'Transform', tr)
)py";
```
Inside `build_default_scene`, after the `camera` entity (the last one), add:
```cpp
    // A scripted, non-physics demo object: a green cube driven by kOrbitScript (see ScriptSystem).
    const auto orbiter = world.create_entity();
    world.add_component<transform::Transform>(orbiter,
        transform::Transform{Vec3{3, 2, 0}, Quat::identity(), Vec3{0.5f, 0.5f, 0.5f}, {}});
    world.add_component<render::MeshRenderer>(orbiter, render::MeshRenderer{render::kCubeMeshId});
    world.add_component<render::Material>(orbiter, render::Material{Vec3{0.2f, 0.9f, 0.3f}, {}});
    world.add_component<mosaic::engine::script::ScriptComponent>(orbiter,
        mosaic::engine::script::ScriptComponent{kOrbitScript});
```
(`transform`, `render`, `Vec3`, `Quat` aliases already exist in this function; `ScriptComponent`'s header is included by Task 1. No RigidBody — the script owns its Transform.)

- [ ] **Step 5: Build + test**
```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "creates the demo entities|scripted .ScriptComponent. demo|round" --output-on-failure
```
Expected: all PASS. Full suite once — 0 failures.

- [ ] **Step 6: Commit**
```bash
git add app/run/demo_scene.hpp tests/unit/app/demo_scene_test.cpp tests/unit/app/scene_save_roundtrip_test.cpp
git commit -m "feat(app): orbiting scripted demo object in the default scene"
```

---

### Task 3: Editor physics toggle for the demo object

**Files:**
- Create: `studio/demo_physics.hpp` (pure `set_demo_physics` swap)
- Test: `tests/unit/studio/demo_physics_test.cpp`
- Modify: `tests/CMakeLists.txt` (register the test)
- Modify: `studio/studio_app.hpp` (members + include)
- Modify: `studio/studio_app.cpp` (cache the demo entity + the toolbar checkbox)

**Interfaces:**
- Consumes: `ScriptComponent`, `RigidBody`, `Collider`, `World`.
- Produces: `void mosaic::studio::set_demo_physics(World&, EntityID, const std::string&, bool)`.

- [ ] **Step 1: Write the failing test `tests/unit/studio/demo_physics_test.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

#include "engine/physics/physics_components.hpp"
#include "engine/script/script_component.hpp"
#include "engine/world/world.hpp"
#include "studio/demo_physics.hpp"

using mosaic::engine::World;
using mosaic::engine::physics::RigidBody;
using mosaic::engine::script::ScriptComponent;
using mosaic::studio::set_demo_physics;

TEST_CASE("set_demo_physics swaps the demo object to physics-driven", "[studio]")
{
    World world;
    auto e = world.create_entity();
    world.add_component<ScriptComponent>(e, ScriptComponent{"src"});

    set_demo_physics(world, e, "src", true);
    REQUIRE(world.has_component<RigidBody>(e));
    REQUIRE_FALSE(world.has_component<ScriptComponent>(e));
}

TEST_CASE("set_demo_physics restores the demo object to script-driven", "[studio]")
{
    World world;
    auto e = world.create_entity();
    world.add_component<RigidBody>(e, RigidBody{true, 1.0f});

    set_demo_physics(world, e, "orbit-source", false);
    REQUIRE_FALSE(world.has_component<RigidBody>(e));
    REQUIRE(world.has_component<ScriptComponent>(e));
    REQUIRE(world.get_component<ScriptComponent>(e)->source == "orbit-source");
}
```

- [ ] **Step 2: Register the test + confirm it fails**

Add to `tests/CMakeLists.txt` after `unit/studio/script_console_test.cpp`:
```cmake
  unit/studio/demo_physics_test.cpp
```
Build → FAIL (`studio/demo_physics.hpp` missing).

- [ ] **Step 3: Create `studio/demo_physics.hpp`**

```cpp
#pragma once

#include <string>

#include "engine/physics/physics_components.hpp"
#include "engine/script/script_component.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::studio
{
// Switch the demo object `e` between script-driven and physics-driven. A ScriptComponent and a
// dynamic RigidBody cannot both drive one Transform (they fight), so this swaps rather than layers.
// Idempotent per target state (remove of an absent component / add of a present one are no-ops).
inline void set_demo_physics(mosaic::engine::World& world, mosaic::foundation::EntityID e,
                             const std::string& script_source, bool physics)
{
    namespace phys = mosaic::engine::physics;
    if (physics)
    {
        world.remove_component<mosaic::engine::script::ScriptComponent>(e);
        world.add_component<phys::RigidBody>(e, phys::RigidBody{true, 1.0f});
        world.add_component<phys::Collider>(e,
            phys::Collider{mosaic::foundation::math::Vec3{0.5f, 0.5f, 0.5f}});
    }
    else
    {
        world.remove_component<phys::RigidBody>(e);
        world.remove_component<phys::Collider>(e);
        world.add_component<mosaic::engine::script::ScriptComponent>(e,
            mosaic::engine::script::ScriptComponent{script_source});
    }
}
} // namespace mosaic::studio
```

- [ ] **Step 4: Build + test the pure function**
```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "set_demo_physics" --output-on-failure
```
Expected: both PASS.

- [ ] **Step 5: Add members to `studio/studio_app.hpp`**

Add includes: `#include <optional>`, `#include <string>`, `#include "foundation/core/entity_id.hpp"`, `#include "studio/demo_physics.hpp"`. Add private members near the other panel state:
```cpp
    std::optional<mosaic::foundation::EntityID> demo_entity_; // the scripted demo object, cached
    std::string                                 demo_script_; // its source (to restore on toggle-off)
    bool                                        demo_physics_ = false;
```

- [ ] **Step 6: Cache the demo object + add the toolbar checkbox in `studio/studio_app.cpp`**

Add the include `#include "engine/script/script_component.hpp"` (for `ScriptComponent` + `for_each`). In `draw_ui`, right after `ImGui::DockSpaceOverViewport(...)` and before `BeginMainMenuBar`, cache the demo entity on first sight:
```cpp
    if (!demo_entity_)
    {
        world.for_each<mosaic::engine::script::ScriptComponent>(
            [&](mosaic::engine::World::EntityID e, mosaic::engine::script::ScriptComponent& sc)
            {
                if (!demo_entity_) { demo_entity_ = e; demo_script_ = sc.source; }
            });
    }
```
In the main menu bar, after the `Step` button's `ImGui::EndDisabled();` (and before the scene-path `ImGui::Text`), add the checkbox:
```cpp
        ImGui::BeginDisabled(!demo_entity_.has_value());
        if (ImGui::Checkbox("Demo: physics", &demo_physics_))
        {
            set_demo_physics(world, *demo_entity_, demo_script_, demo_physics_);
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered() && !demo_entity_.has_value())
        {
            ImGui::SetTooltip("no scripted demo object in this scene");
        }
```

- [ ] **Step 7: Build + full suite + smoke**
```bash
cmake --build build/debug -j && ctest --test-dir build/debug 2>&1 | tail -3
grep -rn pybind studio/ && echo "TIER WALL VIOLATION" || echo "tier wall clean (no pybind in studio/)"
./build/debug/app/mosaic run --window 2>err.txt; echo "exit=$?"; head -3 err.txt; rm -f err.txt
```
Expected: build clean, full suite 0 failures, no pybind in studio/. The `mosaic run --window` smoke may exit non-zero / time out if no display is available — that is NOT a failure (note what you observed; build + suite are the gate; the editor GUI is user-verified).

- [ ] **Step 8: Commit**
```bash
git add studio/demo_physics.hpp tests/unit/studio/demo_physics_test.cpp tests/CMakeLists.txt studio/studio_app.hpp studio/studio_app.cpp
git commit -m "feat(studio): Demo:physics toggle swaps the scripted object script<->physics"
```

---

## Notes for the executor

- **`docs/project/next-steps.md`** — after Task 3, add a short entry: the v0.1 "attach a script → press Play → Build a standalone that plays" workflow is now wired end-to-end (`ScriptSystem` in `setup_runtime`, an orbiting scripted demo object, an editor physics toggle); record the **slider/color-picker controls panel** as a deferred roadmap item; note headless `mosaic run` (no `--window`) still does not tick scripts (bundle uses `--window`). Update the running test count. Commit on this branch (rides in the PR).
- **Finishing:** use `superpowers:finishing-a-development-branch`; base branch is `main` (this branch was cut from `main` after the S1–S5 merges).
- **Visual acceptance is the user's:** `mosaic editor` → Play (green cube orbits, boxes fall) → tick "Demo: physics" (cube falls) → optionally `mosaic build` a bundle and run it (cube orbits on its own). The sandbox can't screenshot the GUI.
- The GIL/single-thread assumption holds (all script execution on the tick/main thread).
