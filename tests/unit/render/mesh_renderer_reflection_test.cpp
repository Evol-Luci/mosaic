#include <array>
#include <cstdint>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/render/mesh_renderer.hpp"
#include "engine/scene/scene.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reference.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::render::MeshRenderer;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::foundation::serialization::AssetID;

TEST_CASE("MeshRenderer is a registered builtin component", "[reflection]")
{
    TypeRegistry registry;
    ComponentReflection types;
    register_builtin_components(registry, types);
    REQUIRE(types.has("MeshRenderer"));
}

TEST_CASE("MeshRenderer mesh AssetID round-trips through a scene", "[reflection]")
{
    TypeRegistry registry;
    ComponentReflection types;
    register_builtin_components(registry, types);

    std::array<std::uint8_t, 16> bytes{};
    bytes[0] = 0x7A;
    const AssetID mesh_id{bytes};

    World src;
    auto e = src.create_entity();
    src.add_component<MeshRenderer>(e, MeshRenderer{mesh_id});

    const std::string json = mosaic::engine::scene::save_to_json(src, types);

    TypeRegistry registry2;
    ComponentReflection types2;
    register_builtin_components(registry2, types2);
    World dst;
    REQUIRE_NOTHROW(mosaic::engine::scene::load_from_json(json, dst, types2));

    int count = 0;
    dst.for_each<MeshRenderer>([&](World::EntityID, MeshRenderer& mr) {
        REQUIRE(mr.mesh == mesh_id); // mesh AssetID preserved
        ++count;
    });
    REQUIRE(count == 1);
}
