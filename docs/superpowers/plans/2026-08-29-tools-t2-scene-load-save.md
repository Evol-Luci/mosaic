# Tools T2 — Scene load/save in the Studio Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `mosaic run` and the Studio operate on real scene files — split `build_demo_scene` into scaffolding + data, load `--scene` (or the default demo) through one path, and add editor File → Save.

**Architecture:** `build_demo_scene` becomes `setup_runtime(world, registry)` (non-serialized scaffolding: assets + `PhysicsSystem` + `RenderSystem`) plus `build_default_scene(world)` (pure entities). A shared `populate_world` loads from `--scene` or the default demo and derives `WorldTransform`. A headless `SceneFileModel` + a `scene_io` filesystem seam drive File → Save.

**Tech Stack:** C++20, CMake presets, Catch2, ImGui (Studio), CLI11 (app). No new third-party deps.

**Spec:** `docs/superpowers/specs/2026-08-29-tools-t2-scene-load-save-design.md`

## Global Constraints

- **Tier contract:** scene-data/scaffolding split + fs seam live in the **`app/`** tier; `engine/scene` stays in-memory-only. Studio model (`SceneFileModel`) is headless; ImGui stays in `studio_app.cpp`.
- **Serialization is #10/#19** (`save_to_json`/`load_from_json` over the reflected component graph). Assets are #20 registry resources referenced by `AssetID`. No new mechanism.
- **`setup_runtime` takes no device** — assets/systems/`RenderSystem` need only world + registry; GPU uploads happen at render time.
- **`WorldTransform` is derived, not serialized.** `TransformSystem::update` adds it when missing; `populate_world` runs one pass so loaded scenes render.
- **In-editor Reload is out of scope** (deferred follow-up per the spec).
- **Build:** `cmake --preset debug` (once) then `cmake --build build/debug -j`. **Test:** `ctest --test-dir build/debug`. Exe: `build/debug/app/mosaic`.
- Catch2 test names must not start with `--`. New test sources go in the single `tests/CMakeLists.txt` `mosaic_unit_tests` list (tests already link `mosaic_app`, `mosaic_studio`, `mosaic_physics_box3d`).
- Namespaces: `mosaic::app` (app), `mosaic::studio` (studio), `mosaic::engine::…`; `AssetID` is `mosaic::foundation::serialization::AssetID`.

---

### Task 1: Split `build_demo_scene` into `setup_runtime` + `build_default_scene`

Refactor `app/run/demo_scene.hpp` in place. `build_demo_scene` is kept as a thin shim (same signature) so `main.cpp`/`editor.cpp` stay green until Task 4. Behavior is identical.

**Files:**
- Modify: `app/run/demo_scene.hpp` (full rewrite of the body; new functions + shared texture ids)
- Test: `tests/unit/app/demo_scene_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `const AssetID kCheckerTextureId / kSolidTextureId / kStripesTextureId` (shared demo texture ids).
  - `struct RuntimeScene { std::shared_ptr<engine::render::RenderSystem> render_system; };`
  - `RuntimeScene setup_runtime(engine::World& world, engine::assets::AssetRegistry& registry);` — registers built-in meshes + the three demo textures, adds `PhysicsSystem` (Box3D), subscribes contact logging, returns a `RenderSystem`.
  - `void build_default_scene(engine::World& world);` — creates the demo entities (ground/box/sphere/capsule/stack, lights, camera); Materials reference `kCheckerTextureId`; **no `WorldTransform`** (derived).
  - `RuntimeScene build_demo_scene(World&, RenderDevice&, AssetRegistry&);` — shim: `build_default_scene` + a `TransformSystem` pass + `setup_runtime`.

- [ ] **Step 1: Write the failing test `tests/unit/app/demo_scene_test.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

#include "app/run/demo_scene.hpp"
#include "engine/assets/asset_registry.hpp"
#include "engine/render/builtin_meshes.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"

using mosaic::app::build_default_scene;
using mosaic::app::kCheckerTextureId;
using mosaic::app::setup_runtime;
using mosaic::engine::World;
using mosaic::engine::assets::AssetRegistry;

TEST_CASE("build_default_scene creates the demo entities with Transforms", "[app]")
{
    World world;
    build_default_scene(world);

    int transforms = 0;
    world.for_each<mosaic::engine::transform::Transform>(
        [&](World::EntityID, mosaic::engine::transform::Transform&) { ++transforms; });
    // ground, box, sphere, capsule, box2, point-light, camera all carry a Transform (7);
    // the directional light has none.
    REQUIRE(transforms == 7);
}

TEST_CASE("setup_runtime registers built-in meshes + demo textures and returns a render system",
          "[app]")
{
    World world;
    AssetRegistry registry;
    auto runtime = setup_runtime(world, registry);

    REQUIRE(runtime.render_system != nullptr);
    REQUIRE(registry.contains(mosaic::engine::render::kCubeMeshId));
    REQUIRE(registry.contains(mosaic::engine::render::kSphereMeshId));
    REQUIRE(registry.contains(mosaic::engine::render::kCapsuleMeshId));
    REQUIRE(registry.contains(kCheckerTextureId)); // demo checkerboard texture
}
```

- [ ] **Step 2: Register the test + run to see it fail**

Add to `tests/CMakeLists.txt` after `unit/app/frame_pump_test.cpp`:

```cmake
  unit/app/demo_scene_test.cpp
```

Run `cmake --build build/debug -j 2>&1 | tail -20`. Expected: FAIL — `setup_runtime`/`build_default_scene`/`kCheckerTextureId` don't exist.

- [ ] **Step 3: Rewrite `app/run/demo_scene.hpp`**

Replace the whole file with:

```cpp
#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "engine/assets/asset_metadata.hpp"
#include "engine/assets/asset_registry.hpp"
#include "engine/physics/physics_components.hpp"
#include "engine/physics/physics_system.hpp"
#include "engine/render/builtin_meshes.hpp"
#include "engine/render/camera.hpp"
#include "engine/render/directional_light.hpp"
#include "engine/render/image.hpp"
#include "engine/render/material.hpp"
#include "engine/render/mesh_renderer.hpp"
#include "engine/render/point_light.hpp"
#include "engine/render/render_device.hpp"
#include "engine/render/render_system.hpp"
#include "engine/transform/transform.hpp"
#include "engine/transform/transform_system.hpp"
#include "engine/world/world.hpp"
#include "foundation/logging/log.hpp"
#include "foundation/math/vec3.hpp"
#include "foundation/serialization/reference.hpp"
#include "modules/physics_box3d/box3d_backend.hpp"

namespace mosaic::app
{

// Well-known AssetIDs for the demo's three procedural textures. Shared so build_default_scene
// (which sets Material.texture) and setup_runtime (which registers the images) agree — the same
// pattern as engine::render::kCubeMeshId.
inline const mosaic::foundation::serialization::AssetID kCheckerTextureId{
    std::array<std::uint8_t, 16>{0xC0, 0xDE}};
inline const mosaic::foundation::serialization::AssetID kSolidTextureId{
    std::array<std::uint8_t, 16>{0xA1}};
inline const mosaic::foundation::serialization::AssetID kStripesTextureId{
    std::array<std::uint8_t, 16>{0xB2}};

// The RenderSystem the frame loop drives. (Non-serialized runtime state.)
struct RuntimeScene
{
    std::shared_ptr<mosaic::engine::render::RenderSystem> render_system;
};

// Non-serializable runtime scaffolding for ANY scene: register built-in meshes + the demo
// textures, add the PhysicsSystem (Box3D), subscribe contact logging, and create the
// RenderSystem. Needs no device (GPU uploads happen at render time). Always called after the
// world is populated.
inline RuntimeScene setup_runtime(mosaic::engine::World&                 world,
                                  mosaic::engine::assets::AssetRegistry& registry)
{
    namespace render  = mosaic::engine::render;
    namespace physics = mosaic::engine::physics;

    world.add_system<physics::PhysicsSystem>(
        mosaic::physics::box3d::create_box3d_backend(physics::WorldDef{}));

    render::register_builtin_meshes(registry);

    world.events().subscribe<physics::ContactEvent>(
        [](const physics::ContactEvent& c)
        {
            mosaic::foundation::log::info(
                "physics", "contact {}: entity {} <-> entity {}",
                c.phase == physics::ContactPhase::Begin ? "begin" : "end", c.a.index(),
                c.b.index());
        });

    auto register_texture = [&](mosaic::foundation::serialization::AssetID id, auto&& pixel_fn)
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
        registry.register_resource<render::Image>(
            mosaic::engine::assets::AssetMetadata{id, "texture"}, image);
    };
    register_texture(kCheckerTextureId,
        [](std::uint32_t x, std::uint32_t y, std::uint8_t* p)
        { const std::uint8_t c = (((x / 8) + (y / 8)) % 2 == 0) ? 230 : 60; p[0]=c;p[1]=c;p[2]=c;p[3]=255; });
    register_texture(kSolidTextureId,
        [](std::uint32_t, std::uint32_t, std::uint8_t* p) { p[0]=220;p[1]=120;p[2]=60;p[3]=255; });
    register_texture(kStripesTextureId,
        [](std::uint32_t x, std::uint32_t, std::uint8_t* p)
        { const std::uint8_t c = ((x / 8) % 2 == 0) ? 200 : 40; p[0]=c;p[1]=c;p[2]=c;p[3]=255; });

    return RuntimeScene{std::make_shared<render::RenderSystem>()};
}

// Pure scene DATA: the demo entities/components. No device, no registry — this is what
// save_to_json serializes and what a loaded scene replaces. WorldTransform is derived (added by
// TransformSystem), so it is intentionally not set here.
inline void build_default_scene(mosaic::engine::World& world)
{
    namespace render    = mosaic::engine::render;
    namespace transform = mosaic::engine::transform;
    namespace physics   = mosaic::engine::physics;
    using mosaic::foundation::math::Quat;
    using mosaic::foundation::math::Vec3;

    const auto tex = kCheckerTextureId;

    const auto ground = world.create_entity();
    world.add_component<transform::Transform>(ground,
        transform::Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{10, 1, 10}, {}});
    world.add_component<render::MeshRenderer>(ground, render::MeshRenderer{render::kCubeMeshId});
    world.add_component<render::Material>(ground, render::Material{Vec3{0.6f, 0.6f, 0.6f}, {}});
    world.add_component<physics::RigidBody>(ground, physics::RigidBody{false, 1.0f});
    world.add_component<physics::Collider>(ground, physics::Collider{Vec3{5.0f, 0.5f, 5.0f}});

    const auto box = world.create_entity();
    world.add_component<transform::Transform>(box,
        transform::Transform{Vec3{0, 4, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<render::MeshRenderer>(box, render::MeshRenderer{render::kCubeMeshId});
    world.add_component<render::Material>(box, render::Material{Vec3{1, 1, 1}, tex});
    world.add_component<physics::RigidBody>(box, physics::RigidBody{true, 1.0f});
    world.add_component<physics::Collider>(box, physics::Collider{Vec3{0.5f, 0.5f, 0.5f}});

    const auto sphere = world.create_entity();
    world.add_component<transform::Transform>(sphere,
        transform::Transform{Vec3{-2, 4, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<render::MeshRenderer>(sphere, render::MeshRenderer{render::kSphereMeshId});
    world.add_component<render::Material>(sphere, render::Material{Vec3{1, 1, 1}, tex});
    world.add_component<physics::RigidBody>(sphere, physics::RigidBody{true, 1.0f});
    world.add_component<physics::SphereCollider>(sphere, physics::SphereCollider{0.5f});

    const auto capsule = world.create_entity();
    world.add_component<transform::Transform>(capsule,
        transform::Transform{Vec3{2, 4, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<render::MeshRenderer>(capsule, render::MeshRenderer{render::kCapsuleMeshId});
    world.add_component<render::Material>(capsule, render::Material{Vec3{1, 1, 1}, tex});
    world.add_component<physics::RigidBody>(capsule, physics::RigidBody{true, 1.0f});
    world.add_component<physics::CapsuleCollider>(capsule, physics::CapsuleCollider{0.5f, 0.5f});

    const auto box2 = world.create_entity();
    world.add_component<transform::Transform>(box2,
        transform::Transform{Vec3{0, 6, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<render::MeshRenderer>(box2, render::MeshRenderer{render::kCubeMeshId});
    world.add_component<render::Material>(box2, render::Material{Vec3{1, 1, 1}, tex});
    world.add_component<physics::RigidBody>(box2, physics::RigidBody{true, 1.0f});
    world.add_component<physics::Collider>(box2, physics::Collider{Vec3{0.5f, 0.5f, 0.5f}});

    const auto light = world.create_entity();
    world.add_component<render::DirectionalLight>(light,
        render::DirectionalLight{Vec3{-0.5f, -1.0f, -0.3f}, Vec3{1, 1, 1}});

    const auto point = world.create_entity();
    world.add_component<transform::Transform>(point,
        transform::Transform{Vec3{2, 3, 2}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<render::PointLight>(point, render::PointLight{Vec3{1, 0.85f, 0.6f}, 3.0f, 8.0f});

    const auto camera = world.create_entity();
    world.add_component<transform::Transform>(camera,
        transform::Transform{Vec3{0, 2.5f, 9}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<render::Camera>(camera, render::Camera{});
}

// Backward-compatible shim (used by main.cpp/editor.cpp until Task 4): default entities +
// derived WorldTransform + runtime scaffolding. Device is unused (kept for the call sites).
inline RuntimeScene build_demo_scene(mosaic::engine::World&                  world,
                                     mosaic::engine::render::RenderDevice&   /*device*/,
                                     mosaic::engine::assets::AssetRegistry&  registry)
{
    build_default_scene(world);
    mosaic::engine::transform::TransformSystem{}.update(world, 0.0f);
    return setup_runtime(world, registry);
}

} // namespace mosaic::app
```

- [ ] **Step 4: Build and run the demo-scene tests**

```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "build_default_scene|setup_runtime" --output-on-failure
```
Expected: both PASS.

- [ ] **Step 5: Full suite green (shim keeps callers working)**

```bash
ctest --test-dir build/debug 2>&1 | tail -3
```
Expected: 100% pass. (`DemoScene`→`RuntimeScene` rename is transparent — `main.cpp`/`editor.cpp` use `auto … = build_demo_scene(...).render_system`.)

- [ ] **Step 6: Commit**

```bash
git add app/run/demo_scene.hpp tests/unit/app/demo_scene_test.cpp tests/CMakeLists.txt
git commit -m "refactor(app): split build_demo_scene into setup_runtime + build_default_scene"
```

---

### Task 2: Filesystem seam `scene_io`

The only place that touches the filesystem; keeps `engine/scene` in-memory-only and makes I/O unit-testable.

**Files:**
- Create: `app/run/scene_io.hpp`
- Test: `tests/unit/app/scene_io_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `std::optional<std::string> load_scene_file(const std::string& path);` — whole file as text; `std::nullopt` if it cannot be opened.
  - `bool save_scene_file(const std::string& path, const std::string& contents);` — write text; `false` on failure.

- [ ] **Step 1: Write the failing test `tests/unit/app/scene_io_test.cpp`**

```cpp
#include <cstdio>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "app/run/scene_io.hpp"

using mosaic::app::load_scene_file;
using mosaic::app::save_scene_file;

namespace
{
std::string temp_path(const char* tag)
{
    return std::string(std::tmpnam(nullptr)) + tag + ".json";
}
} // namespace

TEST_CASE("save_scene_file then load_scene_file round-trips the contents", "[app]")
{
    const std::string path = temp_path("_sio");
    const std::string body = R"({"entities":[]})";
    REQUIRE(save_scene_file(path, body));

    const auto read = load_scene_file(path);
    REQUIRE(read.has_value());
    REQUIRE(*read == body);
    std::remove(path.c_str());
}

TEST_CASE("load_scene_file returns nullopt for a missing file", "[app]")
{
    REQUIRE_FALSE(load_scene_file("/no/such/mosaic/scene_xyz.json").has_value());
}

TEST_CASE("save_scene_file returns false for an unwritable path", "[app]")
{
    REQUIRE_FALSE(save_scene_file("/no/such/dir/scene.json", "{}"));
}
```

- [ ] **Step 2: Register + run to fail**

Add to `tests/CMakeLists.txt` after `unit/app/demo_scene_test.cpp`:
```cmake
  unit/app/scene_io_test.cpp
```
`cmake --build build/debug -j 2>&1 | tail -15` → FAIL (`scene_io.hpp` missing).

- [ ] **Step 3: Write `app/run/scene_io.hpp`**

```cpp
#pragma once

#include <fstream>
#include <optional>
#include <sstream>
#include <string>

namespace mosaic::app
{
// Reads a whole file to a string; std::nullopt if it cannot be opened. The single filesystem
// read seam for scenes (engine/scene stays in-memory-only).
inline std::optional<std::string> load_scene_file(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return std::nullopt;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Writes text to a file; false on failure. The single filesystem write seam for scenes.
inline bool save_scene_file(const std::string& path, const std::string& contents)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        return false;
    }
    file << contents;
    return static_cast<bool>(file);
}
} // namespace mosaic::app
```

- [ ] **Step 4: Build + test**

```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "scene_file" --output-on-failure
```
Expected: all three `scene_io` cases PASS.

- [ ] **Step 5: Commit**

```bash
git add app/run/scene_io.hpp tests/unit/app/scene_io_test.cpp tests/CMakeLists.txt
git commit -m "feat(app): scene_io filesystem seam (load/save scene text)"
```

---

### Task 3: `populate_world` — the shared load path

Decides the scene source (file **or** default demo) and derives `WorldTransform` so a loaded scene renders. Pure entity population — no device, no scaffolding.

**Files:**
- Create: `app/run/scene_load.hpp`
- Test: `tests/unit/app/populate_world_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `build_default_scene` (Task 1), `load_scene_file` (Task 2), `engine::scene::load_from_json`, `TransformSystem`, `Options`, `ComponentReflection`.
- Produces: `bool populate_world(engine::World& world, const Options& options, const engine::reflection::ComponentReflection& types);` — `false` only on a genuine load failure (missing/unparseable `--scene`); an absent `--scene` selects the default demo and returns `true`.

- [ ] **Step 1: Write the failing test `tests/unit/app/populate_world_test.cpp`**

```cpp
#include <cstdio>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "app/cli/options.hpp"
#include "app/run/scene_io.hpp"
#include "app/run/scene_load.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/scene/scene.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"

using mosaic::app::Options;
using mosaic::app::populate_world;
using mosaic::app::save_scene_file;
using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::transform::Transform;
using mosaic::engine::transform::WorldTransform;
using mosaic::foundation::reflection::TypeRegistry;

namespace
{
ComponentReflection make_types(TypeRegistry& reg)
{
    ComponentReflection t;
    register_builtin_components(reg, t);
    return t;
}
int count_worldtransforms(World& w)
{
    int n = 0;
    w.for_each<WorldTransform>([&](World::EntityID, WorldTransform&) { ++n; });
    return n;
}
} // namespace

TEST_CASE("populate_world uses the default demo when no scene is given", "[app]")
{
    TypeRegistry reg; ComponentReflection types = make_types(reg);
    World world;
    Options options; // no scene
    REQUIRE(populate_world(world, options, types));

    int transforms = 0;
    world.for_each<Transform>([&](World::EntityID, Transform&) { ++transforms; });
    REQUIRE(transforms == 7);                       // the default demo
    REQUIRE(count_worldtransforms(world) == transforms); // every Transform got a WorldTransform
}

TEST_CASE("populate_world loads a scene file when --scene is given", "[app]")
{
    TypeRegistry reg; ComponentReflection types = make_types(reg);

    // Build a one-entity scene and serialize it to a temp file.
    World src;
    auto e = src.create_entity();
    src.add_component<Transform>(e, Transform{});
    const std::string json = mosaic::engine::scene::save_to_json(src, types);
    const std::string path = std::string(std::tmpnam(nullptr)) + "_pw.json";
    REQUIRE(save_scene_file(path, json));

    World world;
    Options options; options.scene = path;
    REQUIRE(populate_world(world, options, types));

    int transforms = 0;
    world.for_each<Transform>([&](World::EntityID, Transform&) { ++transforms; });
    REQUIRE(transforms == 1);                        // loaded, not the demo
    REQUIRE(count_worldtransforms(world) == 1);      // WorldTransform derived on load
    std::remove(path.c_str());
}

TEST_CASE("populate_world fails on a missing or unparseable scene", "[app]")
{
    TypeRegistry reg; ComponentReflection types = make_types(reg);

    World w1; Options missing; missing.scene = "/no/such/scene_abc.json";
    REQUIRE_FALSE(populate_world(w1, missing, types));

    const std::string path = std::string(std::tmpnam(nullptr)) + "_bad.json";
    REQUIRE(mosaic::app::save_scene_file(path, "{ this is not valid json"));
    World w2; Options bad; bad.scene = path;
    REQUIRE_FALSE(populate_world(w2, bad, types));
    std::remove(path.c_str());
}
```

- [ ] **Step 2: Register + run to fail**

Add to `tests/CMakeLists.txt` after `unit/app/scene_io_test.cpp`:
```cmake
  unit/app/populate_world_test.cpp
```
Build → FAIL (`scene_load.hpp` missing).

- [ ] **Step 3: Write `app/run/scene_load.hpp`**

```cpp
#pragma once

#include "app/cli/options.hpp"
#include "app/run/demo_scene.hpp"
#include "app/run/scene_io.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/scene/scene.hpp"
#include "engine/transform/transform_system.hpp"
#include "engine/world/world.hpp"
#include "foundation/logging/log.hpp"
#include "foundation/serialization/error.hpp"

namespace mosaic::app
{
// Populate `world` for a run/editor session: from options.scene if set, else the built-in
// default demo. Derives WorldTransform (not serialized) so a loaded scene renders. Returns
// false only on a genuine load failure (missing/unparseable --scene); an absent --scene is not
// an error (it selects the demo).
inline bool populate_world(mosaic::engine::World& world, const Options& options,
                           const mosaic::engine::reflection::ComponentReflection& types)
{
    if (options.scene)
    {
        const std::optional<std::string> text = load_scene_file(*options.scene);
        if (!text)
        {
            mosaic::foundation::log::error("scene", "scene not found: {}", *options.scene);
            return false;
        }
        try
        {
            mosaic::engine::scene::load_from_json(*text, world, types);
        }
        catch (const mosaic::foundation::serialization::SerializationError& e)
        {
            mosaic::foundation::log::error("scene", "failed to load scene: {}", e.what());
            return false;
        }
    }
    else
    {
        build_default_scene(world);
    }
    // WorldTransform is derived, not serialized: one pass so loaded/default scenes render.
    mosaic::engine::transform::TransformSystem{}.update(world, 0.0f);
    return true;
}
} // namespace mosaic::app
```

Note: confirm the `SerializationError` include path — grep `SerializationError` under `foundation/serialization/` and use the header that declares it (the existing `main.cpp` catches the same type; copy its include).

- [ ] **Step 4: Build + test**

```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "populate_world" --output-on-failure
```
Expected: all three `populate_world` cases PASS.

- [ ] **Step 5: Commit**

```bash
git add app/run/scene_load.hpp tests/unit/app/populate_world_test.cpp tests/CMakeLists.txt
git commit -m "feat(app): populate_world (load --scene or the default demo; derive WorldTransform)"
```

---

### Task 4: Wire `run` + `editor` to the shared path; `editor --scene`

Replace the ad-hoc scene loading + double-populate with `populate_world` + `setup_runtime`, and add `--scene` to the `editor` subcommand. Removes the `build_demo_scene` shim.

**Files:**
- Modify: `app/cli/options.cpp` (add `--scene` to `editor`)
- Modify: `app/main.cpp` (`run`: use `populate_world`; window: `setup_runtime`)
- Modify: `app/run/editor.cpp` (use `populate_world` + `setup_runtime`)
- Modify: `app/run/demo_scene.hpp` (delete the `build_demo_scene` shim)
- Test: extend `tests/unit/app/options_test.cpp`

**Interfaces:**
- Consumes: `populate_world` (Task 3), `setup_runtime` (Task 1).

- [ ] **Step 1: Add `--scene` to the `editor` subcommand — `app/cli/options.cpp`**

After the editor subcommand's `--ticks` option (line ~36), add:
```cpp
    editor->add_option("--scene", scene, "Scene JSON file to load before editing");
```
(`scene` is the shared local already captured into `result.options.scene` when non-empty — no other change needed.)

- [ ] **Step 2: Write the failing options test — extend `tests/unit/app/options_test.cpp`**

Add a case using the file's existing `parse_args({...})` helper (defined in its anonymous namespace):
```cpp
TEST_CASE("editor accepts --scene", "[cli]")
{
    auto pr = parse_args({"mosaic", "editor", "--scene", "level.json"});
    REQUIRE_FALSE(pr.exit_code.has_value());
    REQUIRE(pr.options.command == mosaic::app::Command::Editor);
    REQUIRE(pr.options.scene.has_value());
    REQUIRE(*pr.options.scene == "level.json");
}
```
Build + run `ctest --test-dir build/debug -R "editor accepts --scene"` → FAIL (editor rejects `--scene`: non-zero `exit_code`).

- [ ] **Step 3: Make it pass** — Step 1's edit already implements it. Rebuild + run the case → PASS.

- [ ] **Step 4: Rewire `mosaic run` — `app/main.cpp`**

Replace the scene-load block (the `if (options.scene) { std::ifstream … load_from_json … }`, ~lines 92–111) with:
```cpp
    if (!populate_world(world, options, types))
    {
        return 1; // populate_world already logged the reason
    }
```
Add `#include "app/run/scene_load.hpp"` with the other app includes. In the `if (options.window)` block, replace:
```cpp
        auto render_system =
            build_demo_scene(world, *render_device, asset_registry).render_system;
```
with:
```cpp
        auto render_system = setup_runtime(world, asset_registry).render_system;
```
(`world` is already populated above; `setup_runtime` adds physics + assets + the render system. Headless `run` keeps only `TransformSystem` as before.) Keep the existing `#include "app/run/demo_scene.hpp"`.

- [ ] **Step 5: Rewire the editor — `app/run/editor.cpp`**

Add `#include "app/run/scene_load.hpp"`. Replace:
```cpp
    auto render_system = build_demo_scene(world, *device, asset_registry).render_system;
```
with:
```cpp
    if (!populate_world(world, options, reflection))
    {
        reporter->error("failed to load scene");
        device.reset();
        glfwDestroyWindow(window);
        glfwTerminate();
        mosaic::foundation::log::clear_sinks();
        return 1;
    }
    auto render_system = setup_runtime(world, asset_registry).render_system;
```

- [ ] **Step 6: Delete the `build_demo_scene` shim — `app/run/demo_scene.hpp`**

Remove the `build_demo_scene(...)` function (now unused). `setup_runtime` + `build_default_scene` remain. `#include "engine/render/render_device.hpp"` can stay (harmless) or be removed if unused — leave it to avoid churn.

- [ ] **Step 7: Build, full suite, functional (defaults)**

```bash
cmake --build build/debug -j && ctest --test-dir build/debug 2>&1 | tail -3
timeout 30 ./build/debug/app/mosaic run --window --ticks 3 2>/tmp/t2_run.err; echo "run exit=$?"; grep -iE "error|validation|VUID" /tmp/t2_run.err || echo "(clean)"
timeout 30 ./build/debug/app/mosaic editor --ticks 3 2>/tmp/t2_ed.err; echo "editor exit=$?"; grep -iE "error|validation|VUID" /tmp/t2_ed.err || echo "(clean)"
```
Expected: suite green; both exit 0, no validation errors; the demo still renders (visual parity — same scene, now via `populate_world`+`setup_runtime`).

- [ ] **Step 8: Commit**

```bash
git add app/cli/options.cpp app/main.cpp app/run/editor.cpp app/run/demo_scene.hpp tests/unit/app/options_test.cpp
git commit -m "feat(app): run/editor load via populate_world + setup_runtime; editor --scene"
```

---

### Task 5: `SceneFileModel` (Studio)

Headless model for the editor's current-scene path + dirty + one-shot save request. Mirrors `PlayState`.

**Files:**
- Create: `studio/scene_file.hpp`
- Test: `tests/unit/studio/scene_file_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `class mosaic::studio::SceneFileModel` — `SceneFileModel(std::string path)`, `path()`, `dirty()`, `mark_dirty()`, `mark_clean()`, `request_save()`, `consume_save_request()`.

- [ ] **Step 1: Write the failing test `tests/unit/studio/scene_file_test.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

#include "studio/scene_file.hpp"

using mosaic::studio::SceneFileModel;

TEST_CASE("SceneFileModel tracks path, dirty, and a one-shot save request", "[studio]")
{
    SceneFileModel m("scene.json");
    REQUIRE(m.path() == "scene.json");
    REQUIRE_FALSE(m.dirty());

    m.mark_dirty();
    REQUIRE(m.dirty());

    REQUIRE_FALSE(m.consume_save_request()); // none pending
    m.request_save();
    REQUIRE(m.consume_save_request());       // one-shot
    REQUIRE_FALSE(m.consume_save_request()); // cleared after consume

    m.mark_clean();
    REQUIRE_FALSE(m.dirty());
}
```

- [ ] **Step 2: Register + fail**

Add to `tests/CMakeLists.txt` after `unit/studio/play_state_test.cpp`:
```cmake
  unit/studio/scene_file_test.cpp
```
Build → FAIL.

- [ ] **Step 3: Write `studio/scene_file.hpp`**

```cpp
#pragma once

#include <string>
#include <utility>

namespace mosaic::studio
{
// The editor's current-scene-file state: the path Save writes to, an unsaved-edits flag (a
// convenience indicator), and a one-shot save request set by the File menu and consumed by the
// editor loop. Pure data — no ImGui, no filesystem. Mirrors PlayState.
class SceneFileModel
{
public:
    explicit SceneFileModel(std::string path) : path_(std::move(path)) {}

    const std::string& path() const { return path_; }

    bool dirty() const { return dirty_; }
    void mark_dirty() { dirty_ = true; }
    void mark_clean() { dirty_ = false; }

    void request_save() { save_req_ = true; }
    bool consume_save_request()
    {
        const bool requested = save_req_;
        save_req_ = false;
        return requested;
    }

private:
    std::string path_;
    bool        dirty_    = false;
    bool        save_req_ = false;
};
} // namespace mosaic::studio
```

- [ ] **Step 4: Build + test**

```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "SceneFileModel" --output-on-failure
```
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add studio/scene_file.hpp tests/unit/studio/scene_file_test.cpp tests/CMakeLists.txt
git commit -m "feat(studio): SceneFileModel (current-scene path + dirty + one-shot save request)"
```

---

### Task 6: File → Save in the editor

Add the File → Save menu item, own a `SceneFileModel` in `StudioApp` (path from `--scene`), mark dirty on inspector edits, and perform the save in the editor loop.

**Files:**
- Modify: `studio/studio_app.hpp` (own `SceneFileModel`; constructor path; `scene_file()`; `draw_inspector` returns changed)
- Modify: `studio/studio_app.cpp` (File → Save menu item + path/dirty indicator; mark_dirty on edit)
- Modify: `studio/inspector.hpp` + `studio/inspector.cpp` (visitor reports whether a field changed)
- Modify: `app/run/editor.cpp` (construct `StudioApp` with the path; consume save request → write file)
- Test: extend `tests/unit/studio/scene_file_test.cpp` is not needed; add a save round-trip in `tests/unit/app/` reusing `scene_io` + `save_to_json`

**Interfaces:**
- Consumes: `SceneFileModel` (Task 5), `save_scene_file` (Task 2), `engine::scene::save_to_json`.

- [ ] **Step 1: Make the inspector report edits — `studio/inspector.hpp`/`.cpp`**

In `ImGuiFieldVisitor` (`inspector.hpp`), add a change flag — a public accessor and a private member:
```cpp
    bool changed() const { return changed_; }
    // ... in the private: section, next to selected_/has_selection_:
    bool changed_ = false;
```
In `inspector.cpp`, set `changed_` where an edit occurs. Change `field_float`:
```cpp
void ImGuiFieldVisitor::field_float(std::string_view name, float& value)
{
    if (ImGui::DragFloat(std::string(name).c_str(), &value, 0.05f)) { changed_ = true; }
}
```
and the Assign branch of `field_asset`:
```cpp
    if (ImGui::Button("Assign selected"))
    {
        value = selected_;
        changed_ = true;
    }
```

- [ ] **Step 2: `StudioApp` owns a `SceneFileModel` and reports inspector edits — `studio/studio_app.hpp`**

Add the include `#include "studio/scene_file.hpp"`. Add a constructor + accessor and change `draw_inspector` to return `bool`:
```cpp
    explicit StudioApp(std::string scene_path = "scene.json") : scene_file_(std::move(scene_path)) {}
    // ... in public accessors:
    SceneFileModel& scene_file() { return scene_file_; }
    // ... change the private declaration:
    bool draw_inspector(mosaic::engine::World& world,
                        const mosaic::engine::reflection::ComponentReflection& reflection);
    // ... in private members:
    SceneFileModel scene_file_{"scene.json"};
```

- [ ] **Step 3: File → Save + dirty indicator + mark_dirty — `studio/studio_app.cpp`**

In the File menu, above `Exit`:
```cpp
            if (ImGui::MenuItem("Save"))
            {
                scene_file_.request_save();
            }
            ImGui::Separator();
```
In the menu bar (after the `Step` button block, before `ImGui::EndMainMenuBar()`), show the path + dirty marker:
```cpp
        ImGui::Text("  %s%s", scene_file_.path().c_str(), scene_file_.dirty() ? " *" : "");
```
Where `draw_inspector` is called in the panel loop, capture its result and mark dirty:
```cpp
            else if (panel.name == "Inspector")
            {
                if (draw_inspector(world, reflection))
                {
                    scene_file_.mark_dirty();
                }
            }
```
And update `draw_inspector`'s definition to `return` whether the visitor changed (thread the `ImGuiFieldVisitor::changed()` out; if multiple components are inspected, OR the flags).

- [ ] **Step 4: Construct `StudioApp` with the scene path + perform the save — `app/run/editor.cpp`**

Change `mosaic::studio::StudioApp studio;` to:
```cpp
    mosaic::studio::StudioApp studio(options.scene.value_or("scene.json"));
```
Add `#include "engine/scene/scene.hpp"` and `#include "app/run/scene_io.hpp"`. In the `on_frame` lambda, after `device->end_frame();`, add the save handler (capture `&studio`, `&world`, `&reflection` — already captured):
```cpp
        if (studio.scene_file().consume_save_request())
        {
            const std::string json = mosaic::engine::scene::save_to_json(world, reflection);
            if (mosaic::app::save_scene_file(studio.scene_file().path(), json))
            {
                studio.scene_file().mark_clean();
                mosaic::foundation::log::info("scene", "saved {}", studio.scene_file().path());
            }
            else
            {
                mosaic::foundation::log::error("scene", "save failed: {}",
                                               studio.scene_file().path());
            }
        }
```
(`save_to_json` takes `const ComponentReflection&`; `reflection` qualifies. The lambda already captures `&reflection`.)

- [ ] **Step 5: Save→load round-trip test — `tests/unit/app/scene_save_roundtrip_test.cpp`**

Proves the editor's save path is faithful headlessly (no ImGui): default demo → `save_to_json` → `save_scene_file` → `populate_world(--scene=that file)` → same entity count.

```cpp
#include <cstdio>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "app/cli/options.hpp"
#include "app/run/demo_scene.hpp"
#include "app/run/scene_io.hpp"
#include "app/run/scene_load.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/scene/scene.hpp"
#include "engine/transform/transform.hpp"
#include "engine/transform/transform_system.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::transform::Transform;
using mosaic::foundation::reflection::TypeRegistry;

TEST_CASE("a default scene saved to disk reloads to the same entity set", "[app]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);

    World src; mosaic::app::build_default_scene(src);
    mosaic::engine::transform::TransformSystem{}.update(src, 0.0f);
    const std::string json = mosaic::engine::scene::save_to_json(src, types);
    const std::string path = std::string(std::tmpnam(nullptr)) + "_rt.json";
    REQUIRE(mosaic::app::save_scene_file(path, json));

    World dst; mosaic::app::Options options; options.scene = path;
    REQUIRE(mosaic::app::populate_world(dst, options, types));

    int a = 0, b = 0;
    src.for_each<Transform>([&](World::EntityID, Transform&) { ++a; });
    dst.for_each<Transform>([&](World::EntityID, Transform&) { ++b; });
    REQUIRE(a == b);
    REQUIRE(b == 7);
    std::remove(path.c_str());
}
```
Add to `tests/CMakeLists.txt` after `unit/app/populate_world_test.cpp`:
```cmake
  unit/app/scene_save_roundtrip_test.cpp
```

- [ ] **Step 6: Build, full suite, functional save→load on disk**

```bash
cmake --build build/debug -j && ctest --test-dir build/debug 2>&1 | tail -3
# Save round-trip through the real binary: editor writes scene.json on a Save… but Save is a
# GUI action; instead prove the wired path headlessly is green (above) and confirm launches:
timeout 30 ./build/debug/app/mosaic editor --ticks 3 2>/tmp/t2_ed2.err; echo "editor exit=$?"; grep -iE "error|validation|VUID" /tmp/t2_ed2.err || echo "(clean)"
```
Expected: suite green; editor exits 0, no validation errors. (Interactive Save/`*` indicator is visually confirmed by the user; the save *logic* is covered by the round-trip test.)

- [ ] **Step 7: Commit**

```bash
git add studio/studio_app.hpp studio/studio_app.cpp studio/inspector.hpp studio/inspector.cpp \
        app/run/editor.cpp tests/unit/app/scene_save_roundtrip_test.cpp tests/CMakeLists.txt
git commit -m "feat(studio): File -> Save writes the current scene; dirty indicator on edits"
```

---

## Notes for the executor

- **`docs/project/next-steps.md`** — after Task 6, mark T2 complete and point to T3 (`mosaic create` + project model), noting Reload is a deferred follow-up. Commit that update on this branch (rides in the T2 PR — do not give it its own PR).
- **Finishing:** use `superpowers:finishing-a-development-branch` (present the merge/PR menu; do not merge without the user's choice). Base branch is `main`.
- **Visual check to request from the user:** `mosaic editor` shows the current path + `*` on edit; File → Save writes `scene.json`; relaunching `mosaic editor --scene scene.json` loads the saved state; `mosaic run --window --scene scene.json` renders it.
- If the `SerializationError` include path (Task 3) differs, copy the include the current `app/main.cpp` uses for the same catch.
```
