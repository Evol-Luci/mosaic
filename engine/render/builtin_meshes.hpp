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
