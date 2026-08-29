# Tools T1 — Meshes as Assets Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `MeshRenderer` reference a mesh `AssetID` resolved to a GPU mesh through a `MeshCache`, so scenes round-trip renderable entities.

**Architecture:** Mirror the existing texture-as-asset pattern exactly (`Image` + `TextureCache` + `Material.texture` = `AssetID`). Add a `Mesh` CPU resource and a `MeshCache` (`AssetID → MeshHandle`, upload-once) as additive files; then flip `MeshRenderer.mesh` from a runtime `MeshHandle` to a reflected `AssetID`, have `RenderSystem` resolve it through the cache, and register the built-in cube as a mesh asset under a well-known id.

**Tech Stack:** C++20, CMake presets, Catch2. Engine tier `engine/render`; no new third-party deps, no new Vulkan surface (uses existing `RenderDevice::create_mesh`/`draw`).

**Spec:** `docs/superpowers/specs/2026-08-29-tools-t1-meshes-as-assets-design.md`

## Global Constraints

- **Tier contract:** new code lives in `engine/render` next to `image.hpp`/`texture_cache.hpp`; `engine/` depends only on `foundation/`. No Vulkan/backend changes.
- **Serialization/assets:** the persisted mesh reference is an `AssetID`; meshes are #20 registry resources resolved via `AssetRegistry::load<T>`. No new serialization mechanism.
- **Catch2 test names must not start with `--`** (breaks `catch_discover_tests`/ctest).
- **Build:** `cmake --preset debug` (once) then `cmake --build build/debug -j`. **Test:** `ctest --test-dir build/debug`. Exe: `build/debug/app/mosaic`.
- **Namespaces:** resources/caches in `mosaic::engine::render`; `AssetID` is `mosaic::foundation::serialization::AssetID`; registry is `mosaic::engine::assets::AssetRegistry` / `AssetMetadata`.
- Every new test source must be added to the `mosaic_unit_tests` source list in `tests/CMakeLists.txt` (there is a single test CMake file; no per-dir CMake).

---

### Task 1: `Mesh` resource + `MeshCache`

Additive only — no existing file changes, build stays green. Mirrors `engine/render/image.hpp` + `engine/render/texture_cache.hpp`.

**Files:**
- Create: `engine/render/mesh.hpp`
- Create: `engine/render/mesh_cache.hpp`
- Test: `tests/unit/render/mesh_cache_test.cpp`
- Modify: `tests/CMakeLists.txt` (add the new test source)

**Interfaces:**
- Consumes: `mosaic::engine::render::Vertex`, `MeshHandle`, `RenderDevice::create_mesh(const std::vector<Vertex>&, const std::vector<std::uint32_t>&)` from `engine/render/render_device.hpp`; `AssetRegistry::load<T>`, `register_resource<T>`, `AssetMetadata` from `engine/assets/asset_registry.hpp`; `AssetID` from `foundation/serialization/reference.hpp`.
- Produces:
  - `struct mosaic::engine::render::Mesh { std::vector<Vertex> vertices; std::vector<std::uint32_t> indices; };`
  - `class mosaic::engine::render::MeshCache` with `MeshHandle get(AssetID id, AssetRegistry& registry, RenderDevice& device)` — returns 0 for a nil/unregistered id, uploads via `create_mesh` at most once per id, caches the handle.

- [ ] **Step 1: Write `engine/render/mesh.hpp`**

```cpp
#pragma once
#include <cstdint>
#include <vector>
#include "engine/render/render_device.hpp" // Vertex

namespace mosaic::engine::render
{
// CPU mesh: the asset resource a mesh AssetID resolves to; uploaded by
// RenderDevice::create_mesh. Not reflected — it is a resource, referenced by AssetID
// (the same relationship Image has to a texture AssetID).
struct Mesh
{
    std::vector<Vertex>        vertices;
    std::vector<std::uint32_t> indices;
};
} // namespace mosaic::engine::render
```

- [ ] **Step 2: Write `engine/render/mesh_cache.hpp`**

```cpp
#pragma once
#include <memory>
#include <unordered_map>
#include "engine/assets/asset_registry.hpp"
#include "engine/render/mesh.hpp"
#include "engine/render/render_device.hpp"
#include "foundation/serialization/reference.hpp"
namespace mosaic::engine::render
{
// Resolves a mesh AssetID to a device MeshHandle, uploading via the registry's Mesh at
// most once per id. Nil/missing/failed -> 0. Mirrors TextureCache.
class MeshCache
{
public:
    MeshHandle get(mosaic::foundation::serialization::AssetID id,
                   mosaic::engine::assets::AssetRegistry& registry, RenderDevice& device)
    {
        if (id == mosaic::foundation::serialization::AssetID{})
        {
            return 0;
        }
        auto it = cache_.find(id);
        if (it != cache_.end())
        {
            return it->second;
        }
        MeshHandle handle = 0;
        if (std::shared_ptr<Mesh> mesh = registry.load<Mesh>(id))
        {
            handle = device.create_mesh(mesh->vertices, mesh->indices);
        }
        cache_.emplace(id, handle);
        return handle;
    }

private:
    std::unordered_map<mosaic::foundation::serialization::AssetID, MeshHandle> cache_;
};
} // namespace mosaic::engine::render
```

- [ ] **Step 3: Write the failing test `tests/unit/render/mesh_cache_test.cpp`**

```cpp
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/assets/asset_registry.hpp"
#include "engine/render/mesh.hpp"
#include "engine/render/mesh_cache.hpp"
#include "engine/render/render_device.hpp"
#include "foundation/serialization/reference.hpp"

using mosaic::engine::assets::AssetMetadata;
using mosaic::engine::assets::AssetRegistry;
using mosaic::engine::render::Color;
using mosaic::engine::render::DrawParams;
using mosaic::engine::render::Image;
using mosaic::engine::render::Mesh;
using mosaic::engine::render::MeshCache;
using mosaic::engine::render::MeshHandle;
using mosaic::engine::render::RenderDevice;
using mosaic::engine::render::TextureHandle;
using mosaic::engine::render::Vertex;
using mosaic::foundation::serialization::AssetID;

namespace
{
class CountingDevice : public RenderDevice
{
public:
    int mesh_uploads = 0;
    void begin_frame() override {}
    void clear(const Color&) override {}
    void end_frame() override {}
    MeshHandle create_mesh(const std::vector<Vertex>& v,
                           const std::vector<std::uint32_t>& i) override
    {
        if (v.empty() || i.empty()) return 0;
        return static_cast<MeshHandle>(++mesh_uploads);
    }
    TextureHandle create_texture(const Image&) override { return 0; }
    void draw(MeshHandle, TextureHandle, const DrawParams&) override {}
};

AssetID make_id(std::uint8_t tag)
{
    std::array<std::uint8_t, 16> b{};
    b[0] = tag;
    return AssetID{b};
}

std::shared_ptr<Mesh> triangle()
{
    auto m = std::make_shared<Mesh>();
    m->vertices.resize(3); // contents irrelevant; non-empty is what matters
    m->indices = {0, 1, 2};
    return m;
}
} // namespace

TEST_CASE("MeshCache uploads once and caches by AssetID", "[render]")
{
    AssetRegistry registry;
    const AssetID id = make_id(1);
    registry.register_resource<Mesh>(AssetMetadata{id, "mesh"}, triangle());

    CountingDevice device;
    MeshCache cache;

    const MeshHandle h1 = cache.get(id, registry, device);
    const MeshHandle h2 = cache.get(id, registry, device);
    REQUIRE(h1 != 0);
    REQUIRE(h2 == h1);
    REQUIRE(device.mesh_uploads == 1); // uploaded once, then cached
}

TEST_CASE("MeshCache returns 0 for a nil or unregistered id", "[render]")
{
    AssetRegistry registry;
    CountingDevice device;
    MeshCache cache;
    REQUIRE(cache.get(AssetID{}, registry, device) == 0);  // nil
    REQUIRE(cache.get(make_id(9), registry, device) == 0); // unregistered
    REQUIRE(device.mesh_uploads == 0);
}
```

- [ ] **Step 4: Register the test source in `tests/CMakeLists.txt`**

Add this line to the `mosaic_unit_tests` source list, right after `unit/render/texture_cache_test.cpp`:

```cmake
  unit/render/mesh_cache_test.cpp
```

- [ ] **Step 5: Build and run — verify the new tests pass**

```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "MeshCache" --output-on-failure
```
Expected: both `MeshCache …` cases PASS. (If `build/debug` does not exist yet, run `cmake --preset debug` first.)

- [ ] **Step 6: Commit**

```bash
git add engine/render/mesh.hpp engine/render/mesh_cache.hpp tests/unit/render/mesh_cache_test.cpp tests/CMakeLists.txt
git commit -m "feat(render): Mesh resource + MeshCache (AssetID -> MeshHandle)"
```

---

### Task 2: Built-in cube mesh asset (`kCubeMeshId` + `register_builtin_meshes`)

Additive only — depends on Task 1's `Mesh` and existing `make_cube_mesh`; touches no existing behavior, build stays green. Mirrors `register_builtin_components`.

**Files:**
- Create: `engine/render/builtin_meshes.hpp`
- Test: `tests/unit/render/builtin_meshes_test.cpp`
- Modify: `tests/CMakeLists.txt` (add the new test source)

**Interfaces:**
- Consumes: `Mesh` (Task 1); `make_cube_mesh(std::vector<Vertex>&, std::vector<std::uint32_t>&)` from `engine/render/cube.hpp`; `AssetRegistry::register_resource<T>`, `AssetMetadata`, `load<Mesh>`, `contains` from `engine/assets/asset_registry.hpp`; `AssetID` from `foundation/serialization/reference.hpp`.
- Produces:
  - `const mosaic::engine::render::AssetID kCubeMeshId` — well-known id for the built-in unit cube.
  - `void mosaic::engine::render::register_builtin_meshes(AssetRegistry& registry)` — registers the cube `Mesh` under `kCubeMeshId`.

- [ ] **Step 1: Write `engine/render/builtin_meshes.hpp`**

```cpp
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include "engine/assets/asset_metadata.hpp"
#include "engine/assets/asset_registry.hpp"
#include "engine/render/cube.hpp"
#include "engine/render/mesh.hpp"
#include "foundation/serialization/reference.hpp"

namespace mosaic::engine::render
{
// Well-known AssetID for the engine's built-in unit cube mesh. Scenes reference this id;
// register_builtin_meshes makes it resolvable through the AssetRegistry.
inline const mosaic::foundation::serialization::AssetID kCubeMeshId{
    std::array<std::uint8_t, 16>{0xCB, 0xE0}}; // remaining 14 bytes zero

// Registers the built-in cube (via make_cube_mesh) as a Mesh resource under kCubeMeshId.
// The app calls this where it sets up the AssetRegistry so a scene referencing kCubeMeshId
// resolves. Idempotent for practical purposes: re-registering overwrites the same entry.
inline void register_builtin_meshes(mosaic::engine::assets::AssetRegistry& registry)
{
    auto mesh = std::make_shared<Mesh>();
    make_cube_mesh(mesh->vertices, mesh->indices);
    registry.register_resource<Mesh>(
        mosaic::engine::assets::AssetMetadata{kCubeMeshId, "mesh"}, mesh);
}
} // namespace mosaic::engine::render
```

- [ ] **Step 2: Write the failing test `tests/unit/render/builtin_meshes_test.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

#include "engine/assets/asset_registry.hpp"
#include "engine/render/builtin_meshes.hpp"
#include "engine/render/mesh.hpp"

using mosaic::engine::assets::AssetRegistry;
using mosaic::engine::render::kCubeMeshId;
using mosaic::engine::render::Mesh;
using mosaic::engine::render::register_builtin_meshes;

TEST_CASE("register_builtin_meshes registers a loadable cube under kCubeMeshId", "[render]")
{
    AssetRegistry registry;
    REQUIRE_FALSE(registry.contains(kCubeMeshId));

    register_builtin_meshes(registry);
    REQUIRE(registry.contains(kCubeMeshId));

    auto mesh = registry.load<Mesh>(kCubeMeshId);
    REQUIRE(mesh != nullptr);
    REQUIRE(mesh->vertices.size() == 24); // unit cube: 4 verts/face * 6
    REQUIRE(mesh->indices.size() == 36);  // 2 tris/face * 6 * 3
}
```

- [ ] **Step 3: Register the test source in `tests/CMakeLists.txt`**

Add, right after `unit/render/mesh_cache_test.cpp`:

```cmake
  unit/render/builtin_meshes_test.cpp
```

- [ ] **Step 4: Build and run — verify the new test passes**

```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "kCubeMeshId" --output-on-failure
```
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/render/builtin_meshes.hpp tests/unit/render/builtin_meshes_test.cpp tests/CMakeLists.txt
git commit -m "feat(render): register built-in cube as a Mesh asset under kCubeMeshId"
```

---

### Task 3: Flip `MeshRenderer` to an `AssetID` resolved through `MeshCache`

The coupled change: `MeshRenderer.mesh` becomes a reflected `AssetID`; `RenderSystem` resolves it via a `MeshCache`; the demo and all render tests move to asset-referenced meshes. These files change together because the field-type change ripples through every consumer — a partial commit would not compile. Deliverable: full suite green + a scene round-trip test proving a `MeshRenderer` preserves its mesh `AssetID`, and the windowed app still renders.

**Files:**
- Modify: `engine/render/mesh_renderer.hpp` (struct + `reflect`)
- Modify: `engine/reflection/builtin_components.hpp` (register `"MeshRenderer"`)
- Modify: `engine/render/render_system.hpp` (resolve via `MeshCache`)
- Modify: `app/run/demo_scene.hpp` (use `register_builtin_meshes` + `kCubeMeshId`)
- Modify: `tests/unit/render/render_system_test.cpp` (asset-referenced meshes)
- Create: `tests/unit/render/mesh_renderer_reflection_test.cpp` (reflection + scene round-trip)
- Modify: `tests/CMakeLists.txt` (add the new test source)

**Interfaces:**
- Consumes: `MeshCache`, `Mesh` (Task 1); `kCubeMeshId`, `register_builtin_meshes` (Task 2); `AssetID`; existing `RenderDevice::create_mesh`/`draw`; `ComponentReflection::register_component<T>`; scene `save_to_json`/`load_from_json`.
- Produces: `struct MeshRenderer { AssetID mesh{}; }` with `template <class V> void reflect(V&, MeshRenderer&)`; `RenderSystem::render` resolving each entity's mesh through a member `MeshCache`.

- [ ] **Step 1: Write the failing reflection + scene round-trip test `tests/unit/render/mesh_renderer_reflection_test.cpp`**

```cpp
#include <array>
#include <cstdint>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/render/mesh_renderer.hpp"
#include "engine/scene/scene.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reference.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::render::MeshRenderer;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::foundation::serialization::AssetID;

TEST_CASE("MeshRenderer is a registered builtin component", "[reflection]")
{
    TypeRegistry registry;
    ComponentReflection types;
    register_builtin_components(registry, types);
    REQUIRE(types.has("MeshRenderer"));
}

TEST_CASE("MeshRenderer mesh AssetID round-trips through a scene", "[reflection]")
{
    TypeRegistry registry;
    ComponentReflection types;
    register_builtin_components(registry, types);

    std::array<std::uint8_t, 16> bytes{};
    bytes[0] = 0x7A;
    const AssetID mesh_id{bytes};

    World src;
    auto e = src.create_entity();
    src.add_component<MeshRenderer>(e, MeshRenderer{mesh_id});

    const std::string json = mosaic::engine::scene::save_to_json(src, types);

    TypeRegistry registry2;
    ComponentReflection types2;
    register_builtin_components(registry2, types2);
    World dst;
    REQUIRE_NOTHROW(mosaic::engine::scene::load_from_json(json, dst, types2));

    int count = 0;
    dst.for_each<MeshRenderer>([&](World::EntityID, MeshRenderer& mr) {
        REQUIRE(mr.mesh == mesh_id); // mesh AssetID preserved
        ++count;
    });
    REQUIRE(count == 1);
}
```

- [ ] **Step 2: Run it — verify it fails to compile / fails**

Add `unit/render/mesh_renderer_reflection_test.cpp` to the `mosaic_unit_tests` source list in `tests/CMakeLists.txt` (after `unit/render/builtin_meshes_test.cpp`), then:

```bash
cmake --build build/debug -j 2>&1 | tail -20
```
Expected: FAILS to compile — `MeshRenderer` has no `AssetID mesh` / no `reflect`, and `"MeshRenderer"` is not registered.

- [ ] **Step 3: Change `MeshRenderer` to hold an `AssetID` + add `reflect` — `engine/render/mesh_renderer.hpp`**

Replace the whole file with:

```cpp
#pragma once

#include "foundation/serialization/reference.hpp" // AssetID

namespace mosaic::engine::render
{
// Which mesh asset an entity draws. The mesh is referenced by AssetID (nil = none) and
// resolved to a device MeshHandle at render time via MeshCache — mirroring how
// Material.texture references an Image. Reflected, so it serializes with the scene.
struct MeshRenderer
{
    mosaic::foundation::serialization::AssetID mesh{}; // nil = nothing to draw
};

template <class V>
void reflect(V& visitor, MeshRenderer& mr)
{
    visitor.member("mesh", mr.mesh);
}
} // namespace mosaic::engine::render
```

- [ ] **Step 4: Register `MeshRenderer` as a builtin — `engine/reflection/builtin_components.hpp`**

Add the include near the other render includes:

```cpp
#include "engine/render/mesh_renderer.hpp"
```

Add this line inside `register_builtin_components`, right after the `Material` registration:

```cpp
    types.register_component<mosaic::engine::render::MeshRenderer>(registry, "MeshRenderer");
```

- [ ] **Step 5: Resolve the mesh through a `MeshCache` in `RenderSystem` — `engine/render/render_system.hpp`**

Add the include with the other render includes (after `#include "engine/render/mesh_renderer.hpp"`):

```cpp
#include "engine/render/mesh_cache.hpp"
```

In `render()`, replace the mesh-0 guard and the draw's mesh argument. Change this block:

```cpp
                if (mr.mesh == 0)
                {
                    return;
                }
                const auto* wt = world.get_component<transform::WorldTransform>(e);
```

to:

```cpp
                const MeshHandle mesh = mesh_cache_.get(mr.mesh, registry, device);
                if (mesh == 0) // nil / unregistered / failed upload -> nothing to draw
                {
                    return;
                }
                const auto* wt = world.get_component<transform::WorldTransform>(e);
```

Change the draw call's first argument from `mr.mesh` to `mesh`:

```cpp
                device.draw(mesh, texture,
                            DrawParams{projection * view * wt->matrix, light_obj,
                                       light.color, base_color, point_position, point_color, point_range});
```

Add a `MeshCache` data member next to `texture_cache_` (near the bottom, with the other `mutable` members):

```cpp
    mutable MeshCache     mesh_cache_;
```

- [ ] **Step 6: Update the demo to use the cube mesh asset — `app/run/demo_scene.hpp`**

Add the include (with the other `engine/render` includes):

```cpp
#include "engine/render/builtin_meshes.hpp"
```

Register the built-in meshes at the top of `build_demo_scene` (right after the `world.add_system<physics::PhysicsSystem>(…)` call is fine):

```cpp
    render::register_builtin_meshes(registry);
```

Delete the local mesh-upload lambda (the `const render::MeshHandle cube = [&]{ … }();` block, lines that build `v`/`i`, call `make_cube_mesh`, and `device.create_mesh`). Then replace every `render::MeshRenderer{cube}` with `render::MeshRenderer{render::kCubeMeshId}` (5 occurrences: ground, box, sphere, capsule, box2).

Note: `device` is now unused in `build_demo_scene` only if nothing else uses it — the texture registration still doesn't use `device`, but the signature must stay (callers pass it, and T2 will use it). Leave the `RenderDevice& device` parameter in place; if the compiler warns about an unused parameter, that is acceptable (the project already passes `device` through here). Do not remove the parameter.

- [ ] **Step 7: Update the render-system tests to asset-referenced meshes — `tests/unit/render/render_system_test.cpp`**

Add includes (with the other render includes):

```cpp
#include "engine/render/builtin_meshes.hpp"
#include "engine/render/mesh.hpp"
```

Add a helper in the anonymous `namespace { … }` (after `add_camera`), which registers the cube mesh and returns its id so meshes resolve to a non-zero handle:

```cpp
// Registers the built-in cube in `registry` and returns its AssetID, so a MeshRenderer
// referencing it resolves to a non-zero MeshHandle through the RenderSystem's MeshCache.
AssetID register_cube(AssetRegistry& registry)
{
    mosaic::engine::render::register_builtin_meshes(registry);
    return mosaic::engine::render::kCubeMeshId;
}
```

Now update each test that builds a `MeshRenderer` so the mesh is a registered asset:

- In **"RenderSystem produces lit DrawParams from Material + DirectionalLight"**: replace `world.add_component<MeshRenderer>(mesh, MeshRenderer{7});` with
  ```cpp
  const AssetID mesh_id = register_cube(registry);
  world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id});
  ```
  and change the assertion `REQUIRE(device.draws[0].mesh == 7);` to `REQUIRE(device.draws[0].mesh != 0);` (the handle is now assigned by the fake device on upload, not a literal).

- In **"RenderSystem uses default base color and light when none present"**: replace `MeshRenderer{5}` with a registered cube:
  ```cpp
  const AssetID mesh_id = register_cube(registry);
  world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id}); // no Material
  ```

- In **"RenderSystem skips mesh 0 / missing WorldTransform"**: rewrite the two entities so one has a missing `WorldTransform` (still skipped) and one has a **nil** mesh (skipped by the cache). Replace the body's two `add_component<MeshRenderer>` lines:
  ```cpp
  const AssetID mesh_id = register_cube(registry);
  auto only_mesh = world.create_entity();
  world.add_component<MeshRenderer>(only_mesh, MeshRenderer{mesh_id}); // no WorldTransform -> skipped

  auto nil = world.create_entity();
  world.add_component<WorldTransform>(nil, WorldTransform{Mat4::identity()});
  world.add_component<MeshRenderer>(nil, MeshRenderer{}); // nil mesh -> skipped by cache
  ```
  Rename the test's display string to drop the literal `0`: `"RenderSystem skips a nil mesh or a missing WorldTransform"`.

- In **"RenderSystem resolves Material texture via TextureCache"**, **"RenderSystem uses non-zero default white texture for untextured material"**, **"RenderSystem transforms point light into mesh object space"**, and **"RenderSystem disables point light when none present"**: each has a `MeshRenderer{5}` or `MeshRenderer{7}` — replace each with a registered cube. Pattern for each: add `const AssetID mesh_id = register_cube(registry);` before the `add_component<MeshRenderer>` line and change that line to `MeshRenderer{mesh_id}`.

(The `make_cube_mesh` geometry test — "make_cube_mesh yields 24 vertices …" — needs no change; it does not use `MeshRenderer`.)

- [ ] **Step 8: Build and run the whole suite**

```bash
cmake --build build/debug -j && ctest --test-dir build/debug --output-on-failure
```
Expected: PASS — including the new `MeshRenderer …` reflection/round-trip cases and all updated render-system cases. If any render-system case fails because its mesh resolves to 0, confirm `register_cube(registry)` runs before `rs.render(...)` in that case.

- [ ] **Step 9: Functional launch check (windowed render still works)**

```bash
./build/debug/app/mosaic run --window --ticks 3; echo "exit=$?"
```
Expected: `exit=0` and no Vulkan validation errors on stderr. (Per project memory, the sandbox cannot screenshot the GUI; exit 0 + silent stderr is the functional gate. The cube now renders via the asset path; the user confirms visual correctness separately.)

- [ ] **Step 10: Commit**

```bash
git add engine/render/mesh_renderer.hpp engine/reflection/builtin_components.hpp \
        engine/render/render_system.hpp app/run/demo_scene.hpp \
        tests/unit/render/render_system_test.cpp \
        tests/unit/render/mesh_renderer_reflection_test.cpp tests/CMakeLists.txt
git commit -m "feat(render): MeshRenderer references a mesh AssetID resolved via MeshCache

MeshRenderer.mesh is now a reflected AssetID (was a runtime MeshHandle), registered
as a builtin component and resolved to a MeshHandle by RenderSystem through a MeshCache.
The demo uses the built-in cube asset (kCubeMeshId). Scenes now round-trip renderable
entities — the enabler for the tools pillar (T2-T5)."
```

---

## Notes for the executor

- **`docs/project/next-steps.md`** is the live handoff. After Task 3 lands, update it to mark T1 complete and point to T2 (scene load/save in the Studio). Commit that update on this same feature branch — do **not** give it its own PR (per project convention).
- When finishing the branch, use the `superpowers:finishing-a-development-branch` skill (present the merge/PR menu; do not merge without the user's choice).
