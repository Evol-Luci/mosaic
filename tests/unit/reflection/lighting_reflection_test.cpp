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

    World src;
    auto e = src.create_entity();
    Material mat{Vec3{0.1f, 0.2f, 0.3f}};
    mat.texture = tex_id;
    mat.shininess = 64.0f;
    mat.specular_strength = 0.25f;
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
        REQUIRE(m.shininess == Approx(64.0f));
        REQUIRE(m.specular_strength == Approx(0.25f));
    });
    dst.for_each<DirectionalLight>([&](World::EntityID, DirectionalLight& l) {
        REQUIRE(l.color.y == Approx(0.6f));
        REQUIRE(l.direction.y == Approx(-1.0f));
    });
}

TEST_CASE("Material specular defaults and out-of-range values survive a round-trip", "[reflection]")
{
    // Review Focus 4: shininess is authorable by hand in a scene file. A zero or
    // negative exponent must round-trip UNCHANGED -- the shader clamps to >= 1, the
    // engine deliberately does not, and an engine-side clamp added later would make
    // the two silently disagree about what a scene file means.
    TypeRegistry registry;
    ComponentReflection types;
    register_builtin_components(registry, types);

    World src;
    auto defaults = src.create_entity();
    src.add_component<Material>(defaults, Material{});
    auto weird = src.create_entity();
    Material odd{};
    odd.shininess = -4.0f;
    odd.specular_strength = 2.5f;
    src.add_component<Material>(weird, odd);

    const std::string json = mosaic::engine::scene::save_to_json(src, types);

    TypeRegistry registry2;
    ComponentReflection types2;
    register_builtin_components(registry2, types2);
    World dst;
    REQUIRE_NOTHROW(mosaic::engine::scene::load_from_json(json, dst, types2));

    std::vector<float> shininess;
    std::vector<float> strength;
    dst.for_each<Material>([&](World::EntityID, Material& m) {
        shininess.push_back(m.shininess);
        strength.push_back(m.specular_strength);
    });
    REQUIRE(shininess.size() == 2);
    // Order follows entity creation order.
    REQUIRE(shininess[0] == Approx(32.0f));   // documented default
    REQUIRE(strength[0] == Approx(0.5f));     // documented default
    REQUIRE(shininess[1] == Approx(-4.0f));   // NOT clamped by the engine
    REQUIRE(strength[1] == Approx(2.5f));     // NOT clamped by the engine
}
