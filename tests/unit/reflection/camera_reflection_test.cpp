#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/render/camera.hpp"
#include "engine/scene/scene.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::render::Camera;
using mosaic::foundation::reflection::TypeRegistry;
using Catch::Approx;

TEST_CASE("Camera is registered as a builtin component", "[reflection]")
{
    TypeRegistry registry;
    ComponentReflection types;
    register_builtin_components(registry, types);
    REQUIRE(types.has("Camera"));
}

TEST_CASE("a Camera round-trips through serialization with its fields", "[reflection]")
{
    TypeRegistry registry;
    ComponentReflection types;
    register_builtin_components(registry, types);

    World src;
    const World::EntityID e = src.create_entity();
    src.add_component<Camera>(e, Camera{1.2f, 0.5f, 250.0f}); // non-default fov/near/far

    const std::string json = mosaic::engine::scene::save_to_json(src, types);

    TypeRegistry registry2;
    ComponentReflection types2;
    register_builtin_components(registry2, types2);

    World dst;
    REQUIRE_NOTHROW(mosaic::engine::scene::load_from_json(json, dst, types2));

    int count = 0;
    dst.for_each<Camera>(
        [&](World::EntityID, Camera& c)
        {
            ++count;
            REQUIRE(c.fov_y_radians == Approx(1.2f));
            REQUIRE(c.near_z == Approx(0.5f));
            REQUIRE(c.far_z == Approx(250.0f));
        });
    REQUIRE(count == 1);
}
