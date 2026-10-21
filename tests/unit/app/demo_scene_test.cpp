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
                       mosaic::engine::render::TextureHandle texture) override
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

TEST_CASE("the demo scene authors a stack with children", "[app][layout]")
{
    // A stack with no children lays nothing out, and a child with no stack parent is not a
    // layout -- so the reachable feature is the PAIR, and that is what this asserts.
    using mosaic::engine::ui::UiParent;
    using mosaic::engine::ui::UiStack;
    World world;
    build_default_scene(world);

    int             stacks = 0;
    World::EntityID stack_entity{};
    world.for_each<UiStack>(
        [&](mosaic::foundation::EntityID e, UiStack&)
        {
            ++stacks;
            stack_entity = e;
        });
    REQUIRE(stacks == 1);

    int children = 0;
    world.for_each<UiParent>(
        [&](mosaic::foundation::EntityID, UiParent& up)
        {
            if (up.parent == stack_entity) { ++children; }
        });
    REQUIRE(children == 3);
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
