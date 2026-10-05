#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <string>
#include <algorithm>
#include <vector>

#include "engine/physics/physics_components.hpp"

#include "app/run/demo_scene.hpp"
#include "engine/audio/audio_backend.hpp"
#include "modules/audio_miniaudio/miniaudio_backend.hpp"
#include "engine/assets/asset_registry.hpp"
#include "engine/net/input_system.hpp"
#include "engine/net/loopback_transport.hpp"
#include "engine/net/network_system.hpp"
#include "engine/net/replication_system.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/render/builtin_meshes.hpp"
#include "engine/render/material.hpp"
#include "engine/render/spot_light.hpp"
#include "engine/script/script_component.hpp"
#include "engine/script/script_host.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"
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
    // ground, box, sphere, capsule, box2, point-light, spot-light, camera, orbiter carry a
    // Transform (9); the directional light has none.
    REQUIRE(transforms == 12); // + the fountain particle emitter + the scripted puffer emitter
}

TEST_CASE("build_default_scene includes the scripted (ScriptComponent) demo objects", "[app]")
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
    REQUIRE(scripted == 3); // the orbiter cube + the scripted puffer emitter + the ball pusher
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

TEST_CASE("setup_runtime + default scene starts the demo sound on a real backend", "[app]")
{
    World world;
    AssetRegistry registry;
    mosaic::foundation::reflection::TypeRegistry    reg;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(reg, types);

    auto  backend = mosaic::audio::miniaudio::create_miniaudio_backend(/*null_device=*/true);
    REQUIRE(backend != nullptr);
    auto* raw = backend.get();
    setup_runtime(world, registry, types,
                  [] { return mosaic::script::python::create_python_script_engine(); }, {}, false,
                  std::move(backend));
    build_default_scene(world);

    world.tick(0.0f); // AudioSystem start-edge -> the orbiter's AudioSource plays voice 1
    REQUIRE(raw->is_playing(1));
}

TEST_CASE("setup_runtime + default scene is silent and safe with a null audio backend", "[app]")
{
    World world;
    AssetRegistry registry;
    mosaic::foundation::reflection::TypeRegistry    reg;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(reg, types);

    setup_runtime(world, registry, types,
                  [] { return mosaic::script::python::create_python_script_engine(); }, {}, false,
                  nullptr); // no audio backend
    build_default_scene(world);
    REQUIRE_NOTHROW(world.tick(0.0f)); // AudioSystem no-ops; no crash
}

TEST_CASE("setup_runtime wires NetworkSystem/ReplicationSystem/InputSystem when given a transport",
          "[app][net]")
{
    World world;
    AssetRegistry registry;
    mosaic::foundation::reflection::TypeRegistry     reg;
    mosaic::engine::reflection::ComponentReflection  types;
    mosaic::engine::reflection::register_builtin_components(reg, types);

    auto [server_tx, client_tx] = mosaic::engine::net::make_loopback_pair();

    setup_runtime(
        world, registry, types,
        [] { return mosaic::script::python::create_python_script_engine(); },
        mosaic::engine::script::ScriptHost{}, false, nullptr, {},
        mosaic::engine::net::Role::Server, server_tx);

    REQUIRE(world.get_system<mosaic::engine::net::NetworkSystem>() != nullptr);
    REQUIRE(world.get_system<mosaic::engine::net::ReplicationSystem>() != nullptr);
    REQUIRE(world.get_system<mosaic::engine::net::InputSystem>() != nullptr);
}

TEST_CASE("setup_runtime without a transport wires no networking", "[app]")
{
    World world;
    AssetRegistry registry;
    mosaic::foundation::reflection::TypeRegistry     reg;
    mosaic::engine::reflection::ComponentReflection  types;
    mosaic::engine::reflection::register_builtin_components(reg, types);

    setup_runtime(
        world, registry, types,
        [] { return mosaic::script::python::create_python_script_engine(); });

    REQUIRE(world.get_system<mosaic::engine::net::NetworkSystem>() == nullptr);
}

TEST_CASE("build_default_scene includes a spot light so the demo exercises all three light types", "[app]")
{
    // Without this the demo scene lights nothing with a SpotLight, so the spot
    // specular and cone falloff paths ship with no way to see them running.
    World world;
    build_default_scene(world);

    int spots = 0;
    world.for_each<mosaic::engine::render::SpotLight>(
        [&](World::EntityID e, mosaic::engine::render::SpotLight& l)
        {
            ++spots;
            REQUIRE(l.range > 0.0f);
            REQUIRE(l.inner_angle <= l.outer_angle); // inverted cone would fade backwards
            // Position AND direction both come from the entity's transform, so a spot
            // light without one contributes nothing at all.
            REQUIRE(world.get_component<mosaic::engine::transform::Transform>(e) != nullptr);
        });
    REQUIRE(spots == 1);
}

TEST_CASE("build_default_scene varies metallic and roughness so the highlights are comparable", "[app]")
{
    // All six demo materials previously took the same defaults, which shows THAT
    // specular exists but gives nothing to judge it against. The scene now spans
    // matte to tight-and-bright, including the strength == 0 opt-out that is promised
    // to render exactly as it did before specular existed.
    World world;
    build_default_scene(world);

    std::vector<float> roughness;
    std::vector<float> metallic;
    world.for_each<mosaic::engine::render::Material>(
        [&](World::EntityID, mosaic::engine::render::Material& m)
        {
            roughness.push_back(m.roughness);
            metallic.push_back(m.metallic);
        });

    REQUIRE(roughness.size() == 6);

    // A genuine spread rather than six copies of the default. The old form of this
    // test also asserted "at least one fully matte material" via
    // specular_strength == 0, which has no PBR equivalent -- it was an authored fudge
    // factor, and the nearest concept is simply high roughness. The spread below is
    // what that assertion was really checking.
    float lo = roughness[0], hi = roughness[0];
    for (float r : roughness) { lo = (r < lo) ? r : lo; hi = (r > hi) ? r : hi; }
    REQUIRE(hi >= 0.8f);  // a genuinely matte surface
    REQUIRE(lo <= 0.1f);  // a tight, obvious one

    // And no two surfaces identical. The mechanical shininess -> roughness conversion
    // mapped two different materials -- the matte box and the defaults box -- onto the
    // same 0.243, which silently cost the scene one of its six distinct surfaces while
    // every assertion above still passed. Found in review 2026-10-04.
    std::vector<float> sorted = roughness;
    std::sort(sorted.begin(), sorted.end());
    REQUIRE(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());

    // The metallic parameter needs a visible user, or the slice ships a knob nothing
    // exercises. Exactly one metal keeps Task 2's visual change to one variable --
    // and seeing metal look like metal is the one thing a human check is for, since no
    // test here can show the BRDF is correct.
    int metals = 0;
    for (float m : metallic) { if (m >= 0.5f) { ++metals; } }
    REQUIRE(metals == 1);
}

TEST_CASE("build_default_scene includes a script that pushes a body", "[app]")
{
    // The binding needs a visible user, and a scripted push is the one thing that
    // proves the whole path -- script -> ScriptHost -> PhysicsSystem -- in a real run.
    World world;
    build_default_scene(world);

    int pushers = 0;
    world.for_each<mosaic::engine::script::ScriptComponent>(
        [&](World::EntityID, mosaic::engine::script::ScriptComponent& sc)
        {
            if (sc.source.find("apply_impulse") != std::string::npos) { ++pushers; }
        });
    REQUIRE(pushers == 1);
}

TEST_CASE("the demo's ball-pusher script actually moves the sphere", "[app]")
{
    World world;
    AssetRegistry registry;
    mosaic::foundation::reflection::TypeRegistry     reg;
    mosaic::engine::reflection::ComponentReflection  types;
    mosaic::engine::reflection::register_builtin_components(reg, types);

    build_default_scene(world);
    auto runtime = setup_runtime(
        world, registry, types,
        [] { return mosaic::script::python::create_python_script_engine(); });

    mosaic::foundation::EntityID target_sphere{};
    world.for_each<mosaic::engine::script::ScriptComponent>(
        [&](mosaic::foundation::EntityID e, mosaic::engine::script::ScriptComponent&)
        {
            if (world.get_component<mosaic::engine::physics::SphereCollider>(e) != nullptr)
            {
                target_sphere = e;
            }
        });
    REQUIRE(target_sphere != mosaic::foundation::EntityID{});

    auto* tf = world.get_component<mosaic::engine::transform::Transform>(target_sphere);
    REQUIRE(tf != nullptr);
    float start_x = tf->position.x;

    for (int i = 0; i < 220; ++i)
    {
        world.tick(1.0f / 60.0f);
    }

    REQUIRE(std::fabs(tf->position.x - start_x) > 0.05f);
}

TEST_CASE("build_default_scene gives exactly one material a metallic/roughness map", "[app]")
{
    // The slice's success criterion: something in the demo must vary per texel, or the
    // feature ships with nothing exercising it visually -- the same reasoning that made
    // the sphere metallic in slice 2. Exactly one is deliberate: a second would not add
    // coverage, and the point is that a human can see the variation on one object.
    World world;
    build_default_scene(world);

    int mapped = 0;
    world.for_each<mosaic::engine::render::Material>(
        [&](World::EntityID, mosaic::engine::render::Material& m)
        {
            if (m.metallic_roughness_texture != mosaic::foundation::serialization::AssetID{})
            {
                ++mapped;
                // It must be the metal. A roughness gradient on a dielectric is far less
                // legible, since most of what you see there is the diffuse lobe, which
                // roughness does not shape.
                REQUIRE(m.metallic == Catch::Approx(1.0f));
            }
        });
    REQUIRE(mapped == 1);
}
