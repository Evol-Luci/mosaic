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
