#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <string>
#include <algorithm>
#include <set>
#include <fstream>
#include <sstream>
#include <vector>

#include "app/asset/image_cook.hpp"

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
#include "engine/vfx/bloom.hpp"
#include "engine/vfx/decal.hpp"
#include "engine/ui/ui_parent.hpp"
#include "engine/ui/ui_stack.hpp"
#include "engine/ui/ui_image.hpp"
#include "engine/ui/ui_panel.hpp"
#include "engine/ui/ui_rect.hpp"
#include "engine/ui/ui_layout.hpp"
#include "engine/ui/ui_hierarchy.hpp"
#include "engine/script/script_component.hpp"
#include "engine/script/script_host.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "modules/script_python/python_script_engine.hpp"

using mosaic::app::build_default_scene;
using mosaic::app::kCheckerTextureId;
using mosaic::app::kEnvironmentCubemapId;
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
    REQUIRE(transforms == 14); // + the fountain particle emitter + the scripted puffer emitter + the ribbon comet + the glow label
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
    REQUIRE(scripted == 4); // the orbiter cube + the scripted puffer emitter + the ball pusher + the ribbon comet
}

namespace
{
// Records only the HUD image draws, in call order -- the one thing this file needs from a
// device. Six pure virtuals is the whole RenderDevice contract; everything else defaults to
// a no-op on the base class.
class UiImageRecordingDevice : public mosaic::engine::render::RenderDevice
{
public:
    struct Call
    {
        std::vector<mosaic::engine::render::UiImageQuad> quads;
        mosaic::engine::render::TextureHandle            texture;
    };
    std::vector<Call> ui_image_draws;

    void begin_frame() override {}
    void clear(const mosaic::engine::render::Color&) override {}
    void end_frame() override {}
    mosaic::engine::render::MeshHandle create_mesh(
        const std::vector<mosaic::engine::render::Vertex>&,
        const std::vector<std::uint32_t>&) override
    {
        return 1;
    }
    mosaic::engine::render::TextureHandle create_texture(
        const mosaic::engine::render::Image&,
        mosaic::engine::render::TextureColorSpace) override
    {
        return 1;
    }
    void draw(mosaic::engine::render::MeshHandle, mosaic::engine::render::TextureHandle,
              const mosaic::engine::render::DrawParams&) override
    {
    }
    void draw_ui_image(const std::vector<mosaic::engine::render::UiImageQuad>& quads,
                       mosaic::engine::render::TextureHandle texture,
                       mosaic::engine::render::RenderDevice::UiClip) override
    {
        ui_image_draws.push_back({quads, texture});
    }
};
} // namespace

TEST_CASE("the default scene's HUD underlay image draws beneath the checker", "[app]")
{
    // Pins the demo's OWN claim, which is otherwise only checkable by eye: the warm-tinted
    // underlay carries order -1 and the checker carries 0, so the underlay must be submitted
    // FIRST and the checker drawn over it.
    //
    // This is the demo's purpose -- it exists so the image pass's sort is visible in a real
    // frame -- and before the passes sorted, submission order came from `for_each`, where
    // the later-created underlay would have landed on top. A sandbox cannot screenshot the
    // window, so without this the demo's claim would rest on nothing.
    World                                           world;
    AssetRegistry                                   registry;
    mosaic::foundation::reflection::TypeRegistry    reg;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(reg, types);
    auto runtime = setup_runtime(
        world, registry, types,
        [] { return mosaic::script::python::create_python_script_engine(); });
    build_default_scene(world);
    mosaic::engine::transform::TransformSystem{}.update(world, 0.0f);

    // Un-point the renderer from the environment. setup_runtime registers the sky as a lazy
    // asset whose loader bakes the full-size IBL products (equirect -> 512 cubemap, irradiance,
    // prefiltered chain, BRDF LUT) on the FIRST FRAME that has a device -- and this case is the
    // only one in the suite that renders a frame. Left alone, that bake was 14.2 s of this
    // case's 14.2 s locally (23-27 s on CI), 38% of the serial CI test step and the whole floor
    // of the parallel one.
    //
    // This case's claim is UI image DRAW ORDER; the image pass reads no environment, and
    // RenderSystem only touches the environment (and the LUT factory, which sits inside the
    // same branch) while `environment_asset_` is set. So clearing the id through the existing
    // public setter removes the bake and nothing else: no production change, no test-only
    // flag, no smaller bake size to drift from the real one. The bake's own correctness is
    // ibl_bake_test's job, at small sizes, in milliseconds; the wiring (registered, right
    // type, renderer pointed at it) is owned by the setup_runtime environment case below.
    // The draws and assertions below are untouched.
    runtime.render_system->set_environment_asset({});

    UiImageRecordingDevice device;
    runtime.render_system->render(world, device, registry);

    REQUIRE(device.ui_image_draws.size() == 2);
    // Identified by tint, not by index: the underlay is warm (1.0, 0.5, 0.2) and the checker
    // is untinted white, so this cannot pass by accident if the two were swapped.
    REQUIRE(device.ui_image_draws[0].quads[0].tint.y == Catch::Approx(0.5f)); // order -1
    REQUIRE(device.ui_image_draws[1].quads[0].tint.y == Catch::Approx(1.0f)); // order 0
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

TEST_CASE("the demo's roughness map renders six distinct bands after factor and clamp", "[app]")
{
    // The slice's only human acceptance criterion is "six vertical bands from a tight
    // highlight to a broad soft one", and nothing checked it. The first committed map
    // did NOT satisfy it: the material's 0.174 scalar multiplied the authored range
    // down into 0.014..0.150, and cube.frag floors roughness at 0.045, so the two
    // lowest bands collapsed to the same value -- five bands, all near-mirror. A human
    // would have found that and reasonably blamed the feature rather than the numbers.
    // Found in review 2026-10-04.
    //
    // This reproduces the shader's arithmetic in C++: roughness = clamp(scalar * G/255,
    // 0.045, 1.0). It cannot prove the image LOOKS right, but it does prove the authored
    // numbers survive the pipeline as distinct values, which is what went wrong.
    const std::string path =
        std::string(MOSAIC_TEST_DIR) + "/../assets/textures/sphere_mr.png";
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    const auto map = mosaic::app::read_image(ss.str());
    REQUIRE(map.has_value());

    // The scalar the demo actually uses, read from the scene rather than hardcoded.
    World world;
    build_default_scene(world);
    float scalar = -1.0f;
    world.for_each<mosaic::engine::render::Material>(
        [&](World::EntityID, mosaic::engine::render::Material& m)
        {
            if (m.metallic_roughness_texture != mosaic::foundation::serialization::AssetID{})
            {
                scalar = m.roughness;
            }
        });
    REQUIRE(scalar >= 0.0f);

    std::set<int> distinct;
    float lo = 2.0f, hi = -1.0f;
    for (std::size_t i = 0; i < map->pixels.size(); i += 4)
    {
        const float g = static_cast<float>(map->pixels[i + 1]) / 255.0f;
        float r = scalar * g;
        r = (r < 0.045f) ? 0.045f : ((r > 1.0f) ? 1.0f : r); // cube.frag's clamp
        distinct.insert(static_cast<int>(r * 1000.0f + 0.5f));
        lo = (r < lo) ? r : lo;
        hi = (r > hi) ? r : hi;
    }
    REQUIRE(distinct.size() == 6);   // six bands actually reach the surface as six values
    REQUIRE(lo > 0.045f);            // the lowest band is NOT sitting on the clamp
    REQUIRE(hi >= 0.9f);             // and the top is genuinely rough, not near-mirror

    // Metallic is uniform: the bands must read as a change of FINISH, not of colour.
    for (std::size_t i = 0; i < map->pixels.size(); i += 4)
    {
        REQUIRE(map->pixels[i + 2] == 255);
    }
}

TEST_CASE("setup_runtime registers the environment cubemap and points the renderer at it", "[app]")
{
    // The slice's visible deliverable. Without this the whole path is built and nothing
    // uses it -- exactly the situation the skybox was pulled into this slice to avoid.
    //
    // The faces are registered as an asset rather than uploaded here, because no
    // RenderDevice exists at setup time; RenderSystem resolves them on its first frame,
    // the same lazy pattern TextureCache already uses for 2D textures.
    World world;
    AssetRegistry registry;
    mosaic::foundation::reflection::TypeRegistry     reg;
    mosaic::engine::reflection::ComponentReflection  types;
    mosaic::engine::reflection::register_builtin_components(reg, types);
    auto runtime = setup_runtime(
        world, registry, types,
        [] { return mosaic::script::python::create_python_script_engine(); });

    REQUIRE(runtime.render_system != nullptr);
    REQUIRE(registry.contains(mosaic::app::kEnvironmentCubemapId));
    REQUIRE(runtime.render_system->environment_asset() == mosaic::app::kEnvironmentCubemapId);

    // Deliberately NOT loading the asset here. setup_runtime registers a lazy loader
    // that bakes the IBL products on first load -- seconds of work -- and five test
    // files call setup_runtime, each in its own ctest process, so forcing the bake here
    // would add minutes to the suite for coverage that already exists.
    //
    // What this case owns is the WIRING: the asset is registered, under the right type,
    // and the renderer points at it. The baked products' shapes and correctness are
    // covered by ibl_bake_test at small sizes, where the same functions run in
    // milliseconds.
    REQUIRE(registry.metadata(mosaic::app::kEnvironmentCubemapId) != nullptr);
    REQUIRE(registry.metadata(mosaic::app::kEnvironmentCubemapId)->type == "environment");
}

TEST_CASE("the demo scene authors a visible bloom on its camera", "[app][bloom]")
{
    // Bloom::intensity defaults to 0, so a demo without this renders byte-identically to
    // one built before the feature existed -- the feature would ship unreachable, and the
    // human verification pass would have nothing to look at.
    //
    // On the CAMERA, because that is where RenderSystem reads it; a Bloom anywhere else in
    // the scene is deliberately ignored.
    using mosaic::engine::vfx::Bloom;
    World world;
    build_default_scene(world);

    int   with_bloom = 0;
    float intensity  = 0.0f;
    world.for_each<mosaic::engine::render::Camera>(
        [&](mosaic::foundation::EntityID e, mosaic::engine::render::Camera&)
        {
            if (const auto* b = world.get_component<Bloom>(e))
            {
                ++with_bloom;
                intensity = b->intensity;
            }
        });
    REQUIRE(with_bloom == 1);
    REQUIRE(intensity > 0.0f);
}

TEST_CASE("the demo's bloom threshold is display white", "[app][bloom]")
{
    // The load-bearing constant for the next human verification pass. The retuned particle
    // and decal colours peak at 2.85, 1.75 and 2.85 in their brightest channel; the ground
    // is lit but never AUTHORED above 1. A threshold of exactly 1.0 is therefore what makes
    // "the fountain glows, the ground does not" a checkable claim rather than an
    // impression -- and if the verification pass says the sky veils, THIS is the number
    // that changes, not the pipeline.
    //
    // The pass ran on 2026-10-20 and answered KEEP: the glowing sky reads as a bright sky,
    // not as a veil. Do not "fix" it by raising the threshold -- that was considered and
    // deliberately declined.
    using mosaic::engine::vfx::Bloom;
    World world;
    build_default_scene(world);

    // Read OUT of the lambda rather than asserted inside it: assertions inside an `if`
    // that never fires are a test that passes by doing nothing, which is what this one did
    // when first written.
    bool  found     = false;
    float threshold = -1.0f;
    float knee      = -1.0f;
    world.for_each<mosaic::engine::render::Camera>(
        [&](mosaic::foundation::EntityID e, mosaic::engine::render::Camera&)
        {
            if (const auto* b = world.get_component<Bloom>(e))
            {
                found     = true;
                threshold = b->threshold;
                knee      = b->knee;
            }
        });
    REQUIRE(found);
    REQUIRE(threshold == Catch::Approx(1.0f));
    REQUIRE(knee > 0.0f); // a zero knee is a NaN chain
}

TEST_CASE("the demo decal lies above the ground's top face", "[app][vfx]")
{
    // Found by a human verification pass: the decal was authored at y=0.02 while the
    // ground is a unit cube (+/-0.5) scaled to {10,1,10} at the origin, so its top face is
    // at y=0.5 -- the decal sat 0.48 BELOW the surface it was meant to lie on. The decal
    // pipeline depth-tests (LESS), so it was rejected by the ground every frame and had
    // never once been visible since it was added. Nothing in the suite looked at the two
    // numbers together, and the demo's own comment described a colour no one had seen.
    //
    // The ground is identified by the scene's only static RigidBody, not by a hardcoded
    // entity index, so this keeps holding if the scene is reordered.
    using mosaic::engine::transform::Transform;
    World world;
    build_default_scene(world);

    bool  found_ground = false;
    float ground_top   = 0.0f;
    world.for_each<mosaic::engine::physics::RigidBody>(
        [&](mosaic::foundation::EntityID e, mosaic::engine::physics::RigidBody& rb)
        {
            if (rb.dynamic) { return; }
            if (const auto* t = world.get_component<Transform>(e))
            {
                found_ground = true;
                ground_top   = t->position.y + (0.5f * t->scale.y);
            }
        });

    bool  found_decal = false;
    float decal_y     = 0.0f;
    world.for_each<mosaic::engine::vfx::Decal>(
        [&](mosaic::foundation::EntityID e, mosaic::engine::vfx::Decal&)
        {
            if (const auto* t = world.get_component<Transform>(e))
            {
                found_decal = true;
                decal_y     = t->position.y;
            }
        });

    REQUIRE(found_ground);
    REQUIRE(found_decal);
    REQUIRE(decal_y > ground_top);
}

TEST_CASE("nothing authored in the demo sits below the ground's top face", "[app][vfx]")
{
    // The CLASS behind the decal case above. That bug (a prop placed against the ground's
    // ORIGIN, y=0, rather than its top FACE, y=0.5) sat behind a green suite for six weeks,
    // and the decal case pins only the decal: the next prop authored the same way would
    // repeat it and nothing would say so.
    //
    // A GENERAL test rather than a `place_on_surface` scene helper -- ruled 2026-10-21. It
    // pins the whole class with no new API; one data point (the decal) does not justify a
    // helper, and one can follow if a second case ever wants to PLACE rather than check.
    //
    // "Props" = every non-ground entity that draws something in the world (a mesh, a decal,
    // a ribbon trail, a world label or a particle emitter) and sits over the ground's
    // footprint. Each must have its origin at or above the top face. Origins, not extents:
    // every mesh here is centred, so a prop resting on the ground has origin >= top, and a
    // prop whose origin is below it is partly or wholly buried. Lights, the camera and
    // other non-drawing entities are excluded; they may legitimately be anywhere.
    //
    // The ground is the scene's only static RigidBody, not a hardcoded index, so this
    // survives a reorder (the same rule as the decal case).
    using mosaic::engine::transform::Transform;
    World world;
    build_default_scene(world);

    bool         found_ground = false;
    float        ground_top   = 0.0f;
    float        half_x       = 0.0f;
    float        half_z       = 0.0f;
    mosaic::foundation::EntityID ground_id{};
    world.for_each<mosaic::engine::physics::RigidBody>(
        [&](mosaic::foundation::EntityID e, mosaic::engine::physics::RigidBody& rb)
        {
            if (rb.dynamic) { return; }
            if (const auto* t = world.get_component<Transform>(e))
            {
                found_ground = true;
                ground_id    = e;
                ground_top   = t->position.y + (0.5f * t->scale.y);
                half_x       = 0.5f * t->scale.x;
                half_z       = 0.5f * t->scale.z;
            }
        });
    REQUIRE(found_ground);

    int checked = 0;
    world.for_each<Transform>(
        [&](mosaic::foundation::EntityID e, Transform& t)
        {
            if (e == ground_id) { return; }
            const bool draws =
                world.get_component<mosaic::engine::render::MeshRenderer>(e) != nullptr
                || world.get_component<mosaic::engine::vfx::Decal>(e) != nullptr
                || world.get_component<mosaic::engine::vfx::TrailRenderer>(e) != nullptr
                || world.get_component<mosaic::engine::ui::WorldLabel>(e) != nullptr
                || world.get_component<mosaic::engine::particles::ParticleEmitter>(e) != nullptr;
            if (!draws) { return; }
            if (std::abs(t.position.x) > half_x || std::abs(t.position.z) > half_z) { return; }
            ++checked;
            INFO("a drawing prop has origin y=" << t.position.y << " vs ground top " << ground_top);
            CHECK(t.position.y >= ground_top);
        });
    // Guards the vacuous pass: a filter that matched nothing would make this green forever.
    REQUIRE(checked >= 8);
}

TEST_CASE("the demo scene authors a stack with children", "[app][layout]")
{
    // A stack with no children lays nothing out, and a child with no stack parent is not a
    // layout -- so the reachable feature is the PAIR, and that is what this asserts.
    using mosaic::engine::ui::UiParent;
    using mosaic::engine::ui::UiStack;
    World world;
    build_default_scene(world);

    // TWO stacks since U8b: U8a's visible top-right container, and the layout-only
    // (`visible = false`) container bottom-left. Counted and then told apart by their
    // container panel's own `visible`, rather than by position in iteration order, which is
    // not stable across an unrelated entity's destruction.
    std::vector<World::EntityID> stack_entities;
    world.for_each<UiStack>([&](mosaic::foundation::EntityID e, UiStack&)
                            { stack_entities.push_back(e); });
    REQUIRE(stack_entities.size() == 2);

    int shown_containers = 0;
    int hidden_containers = 0;
    for (const World::EntityID stack_entity : stack_entities)
    {
        // Every container must carry a UiPanel: resolve_parent_frame returns the whole
        // SCREEN for a parent without one, so a UiStack alone lays nothing out.
        const auto* panel = world.get_component<mosaic::engine::ui::UiPanel>(stack_entity);
        REQUIRE(panel != nullptr);
        if (panel->visible)
        {
            ++shown_containers;
        }
        else
        {
            ++hidden_containers;
            // The layout-only recipe, asserted on the AUTHORED data: alpha 1 is the identity
            // of the inherited-alpha product, so an invisible container authored below 1
            // would fade three children nobody can see through.
            REQUIRE(panel->alpha == Catch::Approx(1.0f));
        }

        int children = 0;
        world.for_each<UiParent>(
            [&](mosaic::foundation::EntityID, UiParent& up)
            {
                if (up.parent == stack_entity) { ++children; }
            });
        REQUIRE(children == 3);
    }
    REQUIRE(shown_containers == 1);
    REQUIRE(hidden_containers == 1);
}

TEST_CASE("the demo's layout-only container lays out while drawing nothing", "[app][layout]")
{
    // The authored feature of U8b, asserted on the SHIPPED scene rather than on a fixture,
    // because the thing that can rot is the authoring and not the arithmetic.
    using mosaic::engine::ui::UiPanel;
    using mosaic::engine::ui::UiParent;
    using mosaic::engine::ui::UiStack;
    World world;
    build_default_scene(world);

    World::EntityID container{};
    world.for_each<UiStack>(
        [&](mosaic::foundation::EntityID e, UiStack&)
        {
            const auto* p = world.get_component<UiPanel>(e);
            if (p != nullptr && !p->visible) { container = e; }
        });
    REQUIRE(world.is_valid(container));

    const auto kids = mosaic::engine::ui::stack_children(world, container);
    REQUIRE(kids.size() == 3);

    // Each child resolved into its OWN slot, and the slots partition the container along
    // the authored axis. The failure this guards is the sharp one: if `visible = false` made
    // resolve_parent_frame treat the container as absent, all three frames would be the
    // whole screen {0,0,1,1} -- identical to each other and far wider than the container.
    const auto* cp = world.get_component<UiPanel>(container);
    REQUIRE(cp != nullptr);
    float prev_x = -1.0f;
    for (const auto kid : kids)
    {
        const auto frame = mosaic::engine::ui::resolve_parent_frame(world, kid);
        REQUIRE(frame.w == Catch::Approx(cp->size.x / 3.0f)); // a third of the container
        REQUIRE(frame.w < 1.0f);                              // NOT the screen
        REQUIRE(frame.x > prev_x);                            // and strictly left to right
        prev_x = frame.x;
    }
}

TEST_CASE("the demo's grid exactly tiles its container", "[app][layout]")
{
    // THE GATE OF U8c, on the SHIPPED scene rather than on a fixture, because the thing that
    // can rot is the authoring and not the arithmetic. U8a's own standard: `spacing` and
    // `padding` are authored 0 and each child's size is {1,1}, so the six children must
    // exactly tile the container -- a SEAM means the slot division leaves a gap, an OVERLAP
    // means it double-counts spacing.
    //
    // Asserted in NORMALIZED space, which is where the tiling claim actually lives: nothing
    // in resolve_parent_frame or grid_slot refers to pixels or to the window, so "at any
    // window size" follows from the arithmetic being window-independent rather than from
    // sampling two sizes.
    using mosaic::engine::ui::UiGrid;
    using mosaic::engine::ui::UiPanel;
    World world;
    build_default_scene(world);

    std::vector<World::EntityID> grids;
    world.for_each<UiGrid>([&](mosaic::foundation::EntityID e, UiGrid&)
                           { grids.push_back(e); });
    REQUIRE(grids.size() == 1);
    const World::EntityID container = grids[0];

    // A grid NEEDS a UiPanel: resolve_parent_frame returns the whole SCREEN for a parent
    // without one, so a UiGrid alone would divide the screen and promote every child to a
    // top-level element.
    const auto* cp = world.get_component<UiPanel>(container);
    REQUIRE(cp != nullptr);
    const auto* cg = world.get_component<UiGrid>(container);
    REQUIRE(cg != nullptr);
    REQUIRE(cg->cols == 3);
    REQUIRE(cg->rows == 2);
    REQUIRE(cg->spacing == Catch::Approx(0.0f));
    REQUIRE(cg->padding == Catch::Approx(0.0f));

    const auto kids = mosaic::engine::ui::stack_children(world, container);
    REQUIRE(kids.size() == 6); // exactly full, so no cell is empty and no child is dropped

    std::vector<mosaic::engine::ui::Rect> cells;
    for (const auto kid : kids)
    {
        // The child's own size is {1,1}, so its rect IS its slot -- which is what makes the
        // tiling judgeable on the resolved frames.
        const auto* kp = world.get_component<UiPanel>(kid);
        REQUIRE(kp != nullptr);
        REQUIRE(kp->size.x == Catch::Approx(1.0f));
        REQUIRE(kp->size.y == Catch::Approx(1.0f));
        cells.push_back(mosaic::engine::ui::resolve_parent_frame(world, kid));
    }

    const float cell_w = cp->size.x / 3.0f;
    const float cell_h = cp->size.y / 2.0f;
    for (std::size_t i = 0; i < 6; ++i)
    {
        INFO("cell " << i);
        REQUIRE(cells[i].w == Catch::Approx(cell_w));
        REQUIRE(cells[i].h == Catch::Approx(cell_h));
        REQUIRE(cells[i].w < 1.0f); // NOT the screen, which is the degenerate pass
    }
    // No seam and no overlap: within each row the cells' edges MEET, and down each column
    // likewise. Written as an edge match rather than as six hand-computed rects, because a
    // seam and an overlap are the same assertion with opposite signs.
    for (std::size_t row = 0; row < 2; ++row)
    {
        for (std::size_t col = 0; col + 1 < 3; ++col)
        {
            const auto& left  = cells[row * 3 + col];
            const auto& right = cells[row * 3 + col + 1];
            INFO("row " << row << " columns " << col << " and " << col + 1);
            REQUIRE(left.x + left.w == Catch::Approx(right.x));
            REQUIRE(left.y == Catch::Approx(right.y));
        }
    }
    for (std::size_t col = 0; col < 3; ++col)
    {
        const auto& top    = cells[col];
        const auto& bottom = cells[3 + col];
        INFO("column " << col);
        REQUIRE(top.y + top.h == Catch::Approx(bottom.y));
        REQUIRE(top.x == Catch::Approx(bottom.x));
    }
    // And together they cover the container exactly, which is the claim in one line.
    REQUIRE(cells[0].x + 3.0f * cell_w == Catch::Approx(cells[2].x + cells[2].w));
    REQUIRE(cells[0].y + 2.0f * cell_h == Catch::Approx(cells[5].y + cells[5].h));
}

TEST_CASE("the demo's flow keeps its partial final line at full-line size",
          "[app][layout]")
{
    // The settled decision of this slice, authored so it is checkable by eye and asserted so
    // it cannot be undone silently. Five children at three per line: the two on the second
    // line must be a THIRD of the container wide and flush with the two cells above them.
    //
    // If this slice's choice were ever flipped to "divide the final line among the children
    // present", the two cells would be a HALF wide each and `REQUIRE(w == cell_w)` is the
    // assertion that fires, reporting 0.12 against 0.08.
    using mosaic::engine::ui::UiFlow;
    using mosaic::engine::ui::UiPanel;
    World world;
    build_default_scene(world);

    std::vector<World::EntityID> flows;
    world.for_each<UiFlow>([&](mosaic::foundation::EntityID e, UiFlow&)
                           { flows.push_back(e); });
    REQUIRE(flows.size() == 1);
    const World::EntityID container = flows[0];
    const auto*           cp        = world.get_component<UiPanel>(container);
    REQUIRE(cp != nullptr);
    const auto* cf = world.get_component<UiFlow>(container);
    REQUIRE(cf != nullptr);
    REQUIRE(cf->per_line == 3);
    REQUIRE(cf->axis == 1); // horizontal, so lines are rows

    const auto kids = mosaic::engine::ui::stack_children(world, container);
    REQUIRE(kids.size() == 5); // FIVE, so the final line is partial -- that is the point

    std::vector<mosaic::engine::ui::Rect> slots;
    for (const auto kid : kids)
    {
        slots.push_back(mosaic::engine::ui::resolve_parent_frame(world, kid));
    }
    const float cell_w = cp->size.x / 3.0f;
    const float cell_h = cp->size.y / 2.0f; // ceil(5 / 3) == 2 lines
    for (std::size_t i = 0; i < 5; ++i)
    {
        INFO("item " << i);
        REQUIRE(slots[i].w == Catch::Approx(cell_w));
        REQUIRE(slots[i].h == Catch::Approx(cell_h));
    }
    // The two on the partial line line up with the two above them, and the third column of
    // that line is EMPTY -- the container's own colour shows through, which is the by-eye
    // evidence the comment in demo_scene.hpp points at.
    REQUIRE(slots[3].x == Catch::Approx(slots[0].x));
    REQUIRE(slots[4].x == Catch::Approx(slots[1].x));
    REQUIRE(slots[3].y == Catch::Approx(slots[0].y + cell_h));
    REQUIRE(slots[4].y == Catch::Approx(slots[3].y));
    // Nothing occupies the sixth slot: the line is sized as if full, not divided.
    REQUIRE(slots[4].x + slots[4].w == Catch::Approx(cp->size.x * (2.0f / 3.0f)
                                                     + slots[0].x));
}

TEST_CASE("the demo's flow block is clear of the GLOW label's screen band and of every "
          "other HUD element", "[app][layout]")
{
    // MUTATION GUARD for the 2026-10-26 composition fix. The 2026-10-26 verification pass's
    // screenshot showed U8c's flow block covering the lower half of the lime "GLOW"
    // WorldLabel: render_system.hpp draws the WorldLabel pass at :445 and the UiPanel pass
    // at :503, so a panel covers a world label unconditionally. That is composition, not a
    // renderer bug, and the fix moved the flow block off bottom-centre.
    //
    // WHERE THE PROBE POINTS COME FROM, because this is the part that must not be a number
    // somebody eyeballed:
    //
    //   x = 0.5 is DERIVED. build_world_text centres the glyph run horizontally on the
    //   label's anchor; the label's Transform is at world x = 0 and the camera's Transform
    //   is at world x = 0 with an identity rotation, so cam_right is +X and the run's centre
    //   projects to the horizontal middle of the screen. Both of those are authored
    //   constants in demo_scene.hpp, and the two x probes either side are inside the band
    //   the old flow covered whatever the label's width turns out to be.
    //
    //   y = 0.80 and 0.90 are inside the band the OLD AUTHORING covered, and that band is
    //   authored arithmetic too: Anchor::Bottom with pos.y = 0.03 and size.y = 0.18 resolves
    //   to y = 1 - 0.18 - 0.03 = 0.79 through 0.97, which is exactly the height at which the
    //   screenshot showed the glyphs cut. So these points are "screen positions a HUD panel
    //   used to occupy and where the label demonstrably is", not a measured label rect.
    //
    // WHAT THIS CASE CANNOT DO, stated rather than implied: it does not compute the label's
    // own screen rect, because that is a projection of a world Transform through the camera
    // and the font atlas, none of which is an authored normalized constant. It therefore
    // proves that no HUD panel covers the points where the label was seen to be cut; it does
    // not prove the whole word is now unobstructed. That remains an eye check.
    //
    // THE MUTATION: restore `.anchor = Anchor::Bottom` and `.pos = {0.0f, 0.03f}` on
    // hud_flow. The flow then resolves to x [0.38, 0.62] y [0.79, 0.97], which contains
    // (0.5, 0.80) and (0.5, 0.90), and both REQUIREs in the probe loop fire.
    using mosaic::engine::ui::Rect;
    using mosaic::engine::ui::UiFlow;
    using mosaic::engine::ui::UiImage;
    using mosaic::engine::ui::UiPanel;
    using mosaic::engine::ui::UiParent;
    World world;
    build_default_scene(world);

    std::vector<World::EntityID> flows;
    world.for_each<UiFlow>([&](mosaic::foundation::EntityID e, UiFlow&)
                           { flows.push_back(e); });
    REQUIRE(flows.size() == 1);
    const World::EntityID flow_entity = flows[0];

    const auto* fp = world.get_component<UiPanel>(flow_entity);
    REQUIRE(fp != nullptr);
    // Top-level: if the flow block were ever parented, its rect would be a slot of something
    // else and every clearance below would be measuring the wrong rectangle.
    REQUIRE_FALSE(world.has_component<UiParent>(flow_entity));
    const Rect flow = mosaic::engine::ui::resolve_ui_rect(
        world, flow_entity, mosaic::engine::ui::to_anchor(fp->anchor), fp->pos, fp->size);

    // Half-open containment: a rect's own far edge is not inside it, so two blocks whose
    // edges merely MEET are not reported as overlapping.
    const auto contains = [](const Rect& r, float px, float py)
    { return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h; };
    const auto overlaps = [](const Rect& a, const Rect& b)
    {
        return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
    };

    // (1) The flow block covers neither point on the label's band.
    const float probe_y[2] = {0.80f, 0.90f};
    const float probe_x[3] = {0.46f, 0.50f, 0.54f};
    for (const float py : probe_y)
    {
        for (const float px : probe_x)
        {
            INFO("probe (" << px << ", " << py << ") on the GLOW label's screen band");
            REQUIRE_FALSE(contains(flow, px, py));
        }
    }

    // (2) And it has not landed on anything else. Gathered from the scene rather than from a
    // list written here, so a HUD element added later is included automatically and a future
    // move cannot be checked against a stale inventory.
    //
    // Scoped to the flow block specifically, NOT asserted pairwise over all of them: the two
    // checker sprites overlap EACH OTHER on purpose (the image pass's `order` sort is only
    // checkable by eye if they do) and both sit over the top-right stack's panel, so
    // whole-HUD disjointness is false and would have been a wrong property to assert.
    std::size_t compared = 0;
    world.for_each<UiPanel>(
        [&](mosaic::foundation::EntityID e, UiPanel& p)
        {
            if (e == flow_entity || world.has_component<UiParent>(e)) { return; }
            const Rect r = mosaic::engine::ui::resolve_ui_rect(
                world, e, mosaic::engine::ui::to_anchor(p.anchor), p.pos, p.size);
            INFO("top-level UiPanel on entity " << e.index() << " at " << r.x << "," << r.y
                                                << " " << r.w << "x" << r.h);
            REQUIRE_FALSE(overlaps(flow, r));
            ++compared;
        });
    world.for_each<UiImage>(
        [&](mosaic::foundation::EntityID e, UiImage& im)
        {
            if (world.has_component<UiParent>(e)) { return; }
            const Rect r = mosaic::engine::ui::resolve_ui_rect(
                world, e, mosaic::engine::ui::to_anchor(im.anchor), im.pos, im.size);
            INFO("top-level UiImage on entity " << e.index() << " at " << r.x << "," << r.y
                                                << " " << r.w << "x" << r.h);
            REQUIRE_FALSE(overlaps(flow, r));
            ++compared;
        });
    // The HUD bar, the top-right stack container, the bottom-left layout-only container, the
    // bottom-right grid container, the checker sprite and its underlay: six. Asserted so a
    // scene reorganisation that drops one cannot make this case silently vacuous -- which is
    // the failure mode of every "assert over whatever we find" test.
    REQUIRE(compared == 6);
}

TEST_CASE("the demo's stack container is not the camera", "[app][layout]")
{
    // The camera already parents the "HP" label (U5a hierarchy), so a UiStack on the camera
    // would reflow that label into a slot and change existing, authored, verified
    // behaviour. The container is deliberately its own entity.
    using mosaic::engine::ui::UiStack;
    World world;
    build_default_scene(world);

    world.for_each<UiStack>(
        [&](mosaic::foundation::EntityID e, UiStack&)
        {
            REQUIRE_FALSE(world.has_component<mosaic::engine::render::Camera>(e));
        });
}
