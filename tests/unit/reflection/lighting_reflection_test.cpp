#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/render/directional_light.hpp"
#include "engine/render/material.hpp"
#include "engine/scene/scene.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::render::DirectionalLight;
using mosaic::engine::render::Material;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::foundation::math::Vec3;
using Catch::Approx;

TEST_CASE("Material and DirectionalLight are registered builtins", "[reflection]")
{
    TypeRegistry registry;
    ComponentReflection types;
    register_builtin_components(registry, types);
    REQUIRE(types.has("Material"));
    REQUIRE(types.has("DirectionalLight"));
}

TEST_CASE("Material and DirectionalLight round-trip through serialization", "[reflection]")
{
    TypeRegistry registry;
    ComponentReflection types;
    register_builtin_components(registry, types);

    World src;
    auto e = src.create_entity();
    src.add_component<Material>(e, Material{Vec3{0.1f, 0.2f, 0.3f}});
    src.add_component<DirectionalLight>(e, DirectionalLight{Vec3{0, -1, 0}, Vec3{0.5f, 0.6f, 0.7f}});

    const std::string json = mosaic::engine::scene::save_to_json(src, types);

    TypeRegistry registry2;
    ComponentReflection types2;
    register_builtin_components(registry2, types2);
    World dst;
    REQUIRE_NOTHROW(mosaic::engine::scene::load_from_json(json, dst, types2));

    dst.for_each<Material>([&](World::EntityID, Material& m) {
        REQUIRE(m.base_color.x == Approx(0.1f));
        REQUIRE(m.base_color.z == Approx(0.3f));
    });
    dst.for_each<DirectionalLight>([&](World::EntityID, DirectionalLight& l) {
        REQUIRE(l.color.y == Approx(0.6f));
        REQUIRE(l.direction.y == Approx(-1.0f));
    });
}
