# Tools T1 — Meshes as Assets (design)

**Decomposition:** [tools-workflow-decomposition](2026-08-29-tools-workflow-decomposition.md) (slice T1).
**Builds on:** #20 asset registry, #19 scene load/save, #18 reflection, 36d-2 texture-as-asset.
**Date:** 2026-08-29.

## Summary

Make `MeshRenderer` reference a mesh **`AssetID`** (reflected/serializable) resolved to an
uploaded GPU mesh through a `MeshCache`, mirroring the 36d-2 texture pattern. Register the
built-in cube as a mesh asset under a well-known id. After this, a scene with renderable
entities **round-trips** through #19 — the prerequisite the rest of the tools pillar needs.
This is the "36d" the current `MeshRenderer` comment deferred.

## Scope

**In:**
- A `Mesh` CPU resource (`{vertices, indices}`) — the asset a mesh `AssetID` resolves to.
- A `MeshCache` resolving `AssetID → MeshHandle` via the registry (upload once, cache).
- Reflected `MeshRenderer { AssetID mesh }` (was `MeshHandle`), registered as a builtin.
- `RenderSystem` resolves the mesh per entity through a `MeshCache`.
- `register_builtin_meshes(registry)` registering the cube `Mesh` under `kCubeMeshId`.
- Demo (`build_demo_scene`) uses the cube mesh asset.

**Deferred (later slices / YAGNI):** mesh *importing* from external files (T5), per-submesh
materials, LODs, and scene *loading* wired into the editor (T2 — T1 only makes it *possible*).

## Architecture

Mirrors `engine/render/{image.hpp, texture_cache.hpp}` exactly.

### `Mesh` resource (`engine/render/mesh.hpp`)

```cpp
#pragma once
#include <cstdint>
#include <vector>
#include "engine/render/render_device.hpp" // Vertex

namespace mosaic::engine::render
{
// CPU mesh (the asset resource a mesh AssetID resolves to; uploaded by
// RenderDevice::create_mesh). Not reflected — it is a resource, referenced by AssetID.
struct Mesh
{
    std::vector<Vertex>        vertices;
    std::vector<std::uint32_t> indices;
};
} // namespace mosaic::engine::render
```

### `MeshCache` (`engine/render/mesh_cache.hpp`)

Structurally identical to `TextureCache`:

```cpp
class MeshCache
{
public:
    MeshHandle get(mosaic::foundation::serialization::AssetID id,
                   mosaic::engine::assets::AssetRegistry& registry, RenderDevice& device)
    {
        if (id == mosaic::foundation::serialization::AssetID{}) { return 0; }
        auto it = cache_.find(id);
        if (it != cache_.end()) { return it->second; }
        MeshHandle handle = 0;
        if (std::shared_ptr<Mesh> m = registry.load<Mesh>(id))
        {
            handle = device.create_mesh(m->vertices, m->indices);
        }
        cache_.emplace(id, handle);
        return handle;
    }
private:
    std::unordered_map<mosaic::foundation::serialization::AssetID, MeshHandle> cache_;
};
```

### Reflected `MeshRenderer` (`engine/render/mesh_renderer.hpp`)

```cpp
struct MeshRenderer
{
    mosaic::foundation::serialization::AssetID mesh{}; // the mesh asset to draw (nil = none)
};

template <class V>
void reflect(V& visitor, MeshRenderer& mr) { visitor.member("mesh", mr.mesh); }
```

Registered in `engine/reflection/builtin_components.hpp` as `"MeshRenderer"`, so it serializes
(#19) and appears in the inspector (an `AssetID` field — the asset browser can assign a mesh to
it, exactly as it assigns a texture to `Material.texture`).

### `RenderSystem` resolves the mesh (`engine/render/render_system.hpp`)

Today `render()` reads `mr.mesh` (a `MeshHandle`) directly and guards `if (mr.mesh == 0)`. It
gains a `MeshCache` (like the existing `texture_cache_`): per entity, resolve
`MeshHandle mesh = mesh_cache_.get(mr.mesh, registry, device); if (mesh == 0) return;` then
`device.draw(mesh, texture, …)`. The nil/missing guard moves into the cache (returns 0).

### Built-in meshes (`engine/render/builtin_meshes.hpp`)

Mirroring `register_builtin_components`:

```cpp
// Well-known AssetID for the engine's built-in unit cube mesh. Scenes reference this id;
// register_builtin_meshes makes it resolvable.
inline const mosaic::foundation::serialization::AssetID kCubeMeshId{
    std::array<std::uint8_t, 16>{0xCB, 0xE0}}; // remaining 14 bytes zero

// Registers the built-in cube (via make_cube_mesh) as a Mesh resource under kCubeMeshId.
// The app calls this where it sets up the AssetRegistry (and, later, the scene-load path),
// so a saved scene referencing kCubeMeshId resolves.
inline void register_builtin_meshes(mosaic::engine::assets::AssetRegistry& registry)
{
    auto mesh = std::make_shared<Mesh>();
    make_cube_mesh(mesh->vertices, mesh->indices);
    registry.register_resource<Mesh>(mosaic::engine::assets::AssetMetadata{kCubeMeshId, "mesh"}, mesh);
}
```

### Demo (`app/run/demo_scene.hpp`)

`build_demo_scene` calls `register_builtin_meshes(registry)` and sets each renderable entity's
`MeshRenderer{kCubeMeshId}` (instead of `create_mesh` → a `MeshHandle`). The `RenderSystem`'s
`MeshCache` uploads the cube once on first draw. No behavioral change on screen.

## Data flow

```
build_demo_scene: register_builtin_meshes(registry)  // cube Mesh -> kCubeMeshId
entity.MeshRenderer.mesh = kCubeMeshId               // an AssetID (serializable)
RenderSystem.render: MeshCache.get(kCubeMeshId, registry, device)
    -> registry.load<Mesh>(kCubeMeshId) -> device.create_mesh(...) -> MeshHandle (cached)
    -> device.draw(handle, texture, params)
Scene save/load: MeshRenderer.mesh (AssetID) round-trips through #19; on load it resolves
    because register_builtin_meshes was called.
```

## Testability

Mostly headless (the texture pattern already has a reference test).

- **`MeshCache`** (`tests/unit/render/mesh_cache_test.cpp`, fake device recording `create_mesh`):
  a registered `Mesh` resolves to a non-zero handle; a second `get` returns the cached handle
  (uploaded once); a nil id and an unregistered id return 0. Mirrors `texture_cache_test`.
- **`MeshRenderer` reflection** (fold into a render/reflection test): `to_value`/`from_value`
  preserves the `mesh` `AssetID`; `register_builtin_components` registers `"MeshRenderer"`.
- **`RenderSystem` mesh resolution** (extend `render_system_test`): an entity with
  `MeshRenderer{id}` + a registered mesh + a fake device → `device.draw` is called with the
  resolved (non-zero) handle; a nil/unregistered mesh draws nothing.
- **Scene round-trip** (`tests/unit/scene/…` or the render test): a `World` with a
  `MeshRenderer` entity → `save_to_json` → `load_from_json` (with `MeshRenderer` registered) →
  the loaded entity's `MeshRenderer.mesh` equals the original `AssetID`.
- **Functional:** `mosaic run --window`/`editor` still render the cube (now asset-resolved);
  exit 0, 0 validation errors.

## Ground rules honored

- **Tier contract:** `Mesh`/`MeshCache`/`MeshRenderer`/`register_builtin_meshes` live in
  `engine/render` next to the texture equivalents; no new Vulkan surface (uses existing
  `create_mesh`/`draw`). `AssetID` is the persisted mesh reference.
- **Serialization/assets:** meshes join textures as #20 registry resources; #19/#10 unchanged.
- **Reflected-property surface:** `MeshRenderer` is an ordinary reflected component.
- No new third-party deps. Catch2 test names avoid a leading `--`.

## Acceptance

- [ ] `Mesh` resource + `MeshCache` (resolve/cache/nil) unit-tested.
- [ ] Reflected `MeshRenderer { AssetID mesh }` registered; round-trips.
- [ ] `RenderSystem` resolves the mesh via the cache.
- [ ] `register_builtin_meshes` registers the cube under `kCubeMeshId`; the demo uses it.
- [ ] A `World` with a `MeshRenderer` entity round-trips through #19 (mesh id preserved).
- [ ] `mosaic run --window`/`editor` render; exit 0, 0 validation errors. Full suite green.

## Depends on / relates

- **Depends on:** #20 asset registry (`load<T>`/`register_resource`), 36d-2 `TextureCache`
  (the reference), #19 scene, #18 reflection, `RenderDevice::create_mesh`. **Blocks:** T2
  (scene load/save in the Studio renders only once meshes round-trip).
- **Relates:** the asset browser can now assign meshes; the importer (T5) will produce `Mesh`
  registry entries.
