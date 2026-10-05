#include <array>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/render/directional_light.hpp"
#include "engine/render/material.hpp"
#include "engine/scene/scene.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reference.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::render::DirectionalLight;
using mosaic::engine::render::Material;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::foundation::math::Vec3;
using mosaic::foundation::serialization::AssetID;
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

    // Build a non-nil AssetID for the texture field.
    std::array<std::uint8_t, 16> tex_bytes{};
    tex_bytes[0] = 0xAB;
    const AssetID tex_id{tex_bytes};
    std::array<std::uint8_t, 16> mr_bytes{};
    mr_bytes[0] = 0xCD;
    const AssetID mr_id{mr_bytes};

    World src;
    auto e = src.create_entity();
    Material mat{Vec3{0.1f, 0.2f, 0.3f}};
    mat.texture = tex_id;
    mat.metallic = 1.0f;
    mat.roughness = 0.25f;
    mat.metallic_roughness_texture = mr_id;
    src.add_component<Material>(e, mat);
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
        REQUIRE(m.texture == tex_id); // texture AssetID round-trips
        REQUIRE(m.metallic == Approx(1.0f));
        REQUIRE(m.roughness == Approx(0.25f));
        // A second, DISTINCT AssetID: using the same one as `texture` would pass even
        // if the two fields were crossed in reflect().
        REQUIRE(m.metallic_roughness_texture == mr_id);
        REQUIRE(m.texture != m.metallic_roughness_texture);
    });
    dst.for_each<DirectionalLight>([&](World::EntityID, DirectionalLight& l) {
        REQUIRE(l.color.y == Approx(0.6f));
        REQUIRE(l.direction.y == Approx(-1.0f));
    });
}

TEST_CASE("Material shading defaults and out-of-range values survive a round-trip", "[reflection]")
{
    // metallic and roughness are authorable by hand in a scene file, so an out-of-range
    // value must round-trip UNCHANGED -- cube.frag clamps roughness to >= 0.045, the
    // engine deliberately does not, and an engine-side clamp added later would make the
    // two silently disagree about what a scene file means.
    // shader_specular_contract_test.cpp pins the shader half of that contract.
    TypeRegistry registry;
    ComponentReflection types;
    register_builtin_components(registry, types);

    World src;
    auto defaults = src.create_entity();
    src.add_component<Material>(defaults, Material{});
    auto weird = src.create_entity();
    Material odd{};
    odd.metallic = 2.0f;
    odd.roughness = -1.0f;
    src.add_component<Material>(weird, odd);

    const std::string json = mosaic::engine::scene::save_to_json(src, types);

    TypeRegistry registry2;
    ComponentReflection types2;
    register_builtin_components(registry2, types2);
    World dst;
    REQUIRE_NOTHROW(mosaic::engine::scene::load_from_json(json, dst, types2));

    std::vector<float> metallic;
    std::vector<float> roughness;
    dst.for_each<Material>([&](World::EntityID, Material& m) {
        metallic.push_back(m.metallic);
        roughness.push_back(m.roughness);
    });
    REQUIRE(metallic.size() == 2);
    // Order follows entity creation order.
    REQUIRE(metallic[0] == Approx(0.0f));    // documented default: dielectric
    REQUIRE(roughness[0] == Approx(0.5f));   // documented default
    REQUIRE(metallic[1] == Approx(2.0f));    // NOT clamped by the engine
    REQUIRE(roughness[1] == Approx(-1.0f));  // NOT clamped by the engine
}
