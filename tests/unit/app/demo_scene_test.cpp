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
