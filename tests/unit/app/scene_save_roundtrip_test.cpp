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
    REQUIRE(b == 11); // 8 demo entities + the fountain emitter + the scripted puffer
    std::remove(path.c_str());
}
