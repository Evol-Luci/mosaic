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
    world.for_each<mosaic::engine::transform::Transform>(
        [&](World::EntityID, mosaic::engine::transform::Transform&) { ++transforms; });
    REQUIRE(transforms == 10);                      // the default demo (+ fountain emitter + puffer)
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
