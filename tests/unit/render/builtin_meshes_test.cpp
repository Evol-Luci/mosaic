#include <catch2/catch_test_macros.hpp>

#include "engine/assets/asset_registry.hpp"
#include "engine/render/builtin_meshes.hpp"
#include "engine/render/mesh.hpp"

using mosaic::engine::assets::AssetRegistry;
using mosaic::engine::render::kCapsuleMeshId;
using mosaic::engine::render::kCubeMeshId;
using mosaic::engine::render::kSphereMeshId;
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

TEST_CASE("register_builtin_meshes registers loadable sphere and capsule meshes", "[render]")
{
    AssetRegistry registry;
    REQUIRE_FALSE(registry.contains(kSphereMeshId));
    REQUIRE_FALSE(registry.contains(kCapsuleMeshId));

    register_builtin_meshes(registry);
    REQUIRE(registry.contains(kSphereMeshId));
    REQUIRE(registry.contains(kCapsuleMeshId));

    auto sphere = registry.load<Mesh>(kSphereMeshId);
    REQUIRE(sphere != nullptr);
    REQUIRE(sphere->vertices.size() > 3);
    REQUIRE(sphere->indices.size() % 3 == 0);

    auto capsule = registry.load<Mesh>(kCapsuleMeshId);
    REQUIRE(capsule != nullptr);
    REQUIRE(capsule->vertices.size() > 3);
    REQUIRE(capsule->indices.size() % 3 == 0);

    // The three built-in ids are distinct.
    REQUIRE_FALSE(kSphereMeshId == kCubeMeshId);
    REQUIRE_FALSE(kCapsuleMeshId == kCubeMeshId);
    REQUIRE_FALSE(kSphereMeshId == kCapsuleMeshId);
}
