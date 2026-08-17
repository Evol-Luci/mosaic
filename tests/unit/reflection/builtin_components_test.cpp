#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/scene/scene.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::foundation::reflection::TypeRegistry;

TEST_CASE("register_builtin_components registers Transform by name", "[reflection]")
{
    TypeRegistry registry;
    ComponentReflection types;
    register_builtin_components(registry, types);

    REQUIRE(types.has("Transform"));
    REQUIRE_FALSE(types.has("NotAComponent"));
}

TEST_CASE("a Transform scene round-trips through the builtin registration", "[reflection]")
{
    // Save: build a world with one entity carrying a default Transform.
    TypeRegistry registry;
    ComponentReflection types;
    register_builtin_components(registry, types);

    World src;
    const World::EntityID e = src.create_entity();
    src.add_component<mosaic::engine::transform::Transform>(e, mosaic::engine::transform::Transform{});

    const std::string json = mosaic::engine::scene::save_to_json(src, types);

    // Load: into a fresh world using a freshly-registered types object.
    TypeRegistry registry2;
    ComponentReflection types2;
    register_builtin_components(registry2, types2);

    World dst;
    REQUIRE_NOTHROW(mosaic::engine::scene::load_from_json(json, dst, types2));

    int transform_count = 0;
    dst.for_each<mosaic::engine::transform::Transform>(
        [&](World::EntityID, mosaic::engine::transform::Transform&) { ++transform_count; });
    REQUIRE(transform_count == 1);
}
