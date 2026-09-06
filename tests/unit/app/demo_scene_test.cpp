#include <catch2/catch_test_macros.hpp>
#include <string>

#include "app/run/demo_scene.hpp"
#include "engine/assets/asset_registry.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/render/builtin_meshes.hpp"
#include "engine/script/script_component.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "modules/script_python/python_script_engine.hpp"

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
    // ground, box, sphere, capsule, box2, point-light, camera, orbiter carry a Transform (8);
    // the directional light has none.
    REQUIRE(transforms == 9); // + the fountain particle emitter
}

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

TEST_CASE("setup_runtime registers built-in meshes + demo textures and returns a render system",
          "[app]")
{
    World world;
    AssetRegistry registry;
    mosaic::foundation::reflection::TypeRegistry     reg;
    mosaic::engine::reflection::ComponentReflection  types;
    mosaic::engine::reflection::register_builtin_components(reg, types);
    auto runtime = setup_runtime(
        world, registry, types,
        [] { return mosaic::script::python::create_python_script_engine(); });

    REQUIRE(runtime.render_system != nullptr);
    REQUIRE(registry.contains(mosaic::engine::render::kCubeMeshId));
    REQUIRE(registry.contains(mosaic::engine::render::kSphereMeshId));
    REQUIRE(registry.contains(mosaic::engine::render::kCapsuleMeshId));
    REQUIRE(registry.contains(kCheckerTextureId)); // demo checkerboard texture
}
