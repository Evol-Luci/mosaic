#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include "engine/assets/asset_metadata.hpp"
#include "engine/assets/asset_registry.hpp"
#include "engine/render/capsule.hpp"
#include "engine/render/cube.hpp"
#include "engine/render/mesh.hpp"
#include "engine/render/sphere.hpp"
#include "foundation/serialization/reference.hpp"

namespace mosaic::engine::render
{
// Well-known AssetIDs for the engine's built-in primitive meshes. Scenes reference these ids;
// register_builtin_meshes makes them resolvable through the AssetRegistry. The sphere/capsule
// are sized to match their unit colliders (radius 0.5; capsule half_height 0.5), just as the
// cube is a unit cube scaled per-entity via Transform.
inline const mosaic::foundation::serialization::AssetID kCubeMeshId{
    std::array<std::uint8_t, 16>{0xCB, 0xE0}}; // remaining 14 bytes zero
inline const mosaic::foundation::serialization::AssetID kSphereMeshId{
    std::array<std::uint8_t, 16>{0x5B, 0xE0}}; // "SphErE"; remaining 14 bytes zero
inline const mosaic::foundation::serialization::AssetID kCapsuleMeshId{
    std::array<std::uint8_t, 16>{0xCA, 0xB5}}; // "CAPSule"; remaining 14 bytes zero

// Registers the built-in primitive meshes (cube, sphere, capsule) as Mesh resources under
// their well-known ids. The app calls this where it sets up the AssetRegistry so a scene
// referencing these ids resolves. Idempotent for practical purposes: re-registering overwrites
// the same entries.
inline void register_builtin_meshes(mosaic::engine::assets::AssetRegistry& registry)
{
    auto cube = std::make_shared<Mesh>();
    make_cube_mesh(cube->vertices, cube->indices);
    registry.register_resource<Mesh>(
        mosaic::engine::assets::AssetMetadata{kCubeMeshId, "mesh"}, cube);

    auto sphere = std::make_shared<Mesh>();
    make_sphere_mesh(sphere->vertices, sphere->indices, 0.5f, 24);
    registry.register_resource<Mesh>(
        mosaic::engine::assets::AssetMetadata{kSphereMeshId, "mesh"}, sphere);

    auto capsule = std::make_shared<Mesh>();
    make_capsule_mesh(capsule->vertices, capsule->indices, 0.5f, 0.5f, 24);
    registry.register_resource<Mesh>(
        mosaic::engine::assets::AssetMetadata{kCapsuleMeshId, "mesh"}, capsule);
}
} // namespace mosaic::engine::render
