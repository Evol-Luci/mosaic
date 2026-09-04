#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

#include "engine/assets/asset_metadata.hpp"
#include "engine/assets/asset_registry.hpp"
#include "engine/physics/physics_components.hpp"
#include "engine/physics/physics_system.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/render/builtin_meshes.hpp"
#include "engine/script/script_component.hpp"
#include "engine/script/script_system.hpp"
#include "engine/render/camera.hpp"
#include "engine/render/directional_light.hpp"
#include "engine/render/image.hpp"
#include "engine/render/material.hpp"
#include "engine/render/mesh_renderer.hpp"
#include "engine/render/point_light.hpp"
#include "engine/render/render_system.hpp"
#include "engine/transform/transform.hpp"
#include "engine/transform/transform_system.hpp"
#include "engine/world/world.hpp"
#include "foundation/logging/log.hpp"
#include "foundation/math/vec3.hpp"
#include "foundation/serialization/reference.hpp"
#include "modules/physics_box3d/box3d_backend.hpp"

namespace mosaic::app
{

// Well-known AssetIDs for the demo's three procedural textures. Shared so build_default_scene
// (which sets Material.texture) and setup_runtime (which registers the images) agree — the same
// pattern as engine::render::kCubeMeshId.
inline const mosaic::foundation::serialization::AssetID kCheckerTextureId{
    std::array<std::uint8_t, 16>{0xC0, 0xDE}};
inline const mosaic::foundation::serialization::AssetID kSolidTextureId{
    std::array<std::uint8_t, 16>{0xA1}};
inline const mosaic::foundation::serialization::AssetID kStripesTextureId{
    std::array<std::uint8_t, 16>{0xB2}};

// The RenderSystem the frame loop drives. (Non-serialized runtime state.)
struct RuntimeScene
{
    std::shared_ptr<mosaic::engine::render::RenderSystem> render_system;
};

// A neutral raycast callback backed by the world's PhysicsSystem (looked up lazily at call
// time, so system-add order doesn't matter). Injected into the ScriptHost by setup_runtime so
// scripts get raycast() while the script backend stays physics-agnostic. Capturing &world is
// safe: the world owns the systems that own the ScriptHost copies.
inline std::function<mosaic::engine::script::ScriptRayHit(
    mosaic::foundation::math::Vec3, mosaic::foundation::math::Vec3, float)>
make_physics_raycast(mosaic::engine::World& world)
{
    return [&world](mosaic::foundation::math::Vec3 origin,
                    mosaic::foundation::math::Vec3 direction,
                    float max_distance) -> mosaic::engine::script::ScriptRayHit
    {
        auto* phys = world.get_system<mosaic::engine::physics::PhysicsSystem>();
        if (phys == nullptr) { return {}; }
        const auto h = phys->raycast(origin, direction, max_distance);
        return mosaic::engine::script::ScriptRayHit{
            h.hit, h.entity.value(), h.point, h.normal, h.distance};
    };
}

// Non-serializable runtime scaffolding for ANY scene: register built-in meshes + the demo
// textures, add the PhysicsSystem (Box3D), subscribe contact logging, and create the
// RenderSystem. Needs no device (GPU uploads happen at render time). Always called after the
// world is populated.
inline RuntimeScene setup_runtime(
    mosaic::engine::World&                                  world,
    mosaic::engine::assets::AssetRegistry&                  registry,
    const mosaic::engine::reflection::ComponentReflection&  reflection,
    mosaic::engine::script::ScriptSystem::EngineFactory     script_factory,
    mosaic::engine::script::ScriptHost                      host = {},
    bool                                                    script_thread = false)
{
    namespace render  = mosaic::engine::render;
    namespace physics = mosaic::engine::physics;

    // Give scripts a raycast() backed by this world's physics (unless the caller already wired one).
    if (!host.raycast) { host.raycast = make_physics_raycast(world); }

    // Gameplay scripts run BEFORE physics: a script-only object (no RigidBody) is left untouched
    // by physics, and a physics object's write-back wins over any script that also touches it.
    world.add_system<mosaic::engine::script::ScriptSystem>(
        reflection, std::move(script_factory), std::move(host), &registry, script_thread);

    world.add_system<physics::PhysicsSystem>(
        mosaic::physics::box3d::create_box3d_backend(physics::WorldDef{}));

    render::register_builtin_meshes(registry);

    world.events().subscribe<physics::ContactEvent>(
        [](const physics::ContactEvent& c)
        {
            mosaic::foundation::log::info(
                "physics", "contact {}: entity {} <-> entity {}",
                c.phase == physics::ContactPhase::Begin ? "begin" : "end", c.a.index(),
                c.b.index());
        });

    auto register_texture = [&](mosaic::foundation::serialization::AssetID id, auto&& pixel_fn)
    {
        auto image    = std::make_shared<render::Image>();
        image->width  = 64;
        image->height = 64;
        image->pixels.resize(64 * 64 * 4);
        for (std::uint32_t y = 0; y < 64; ++y)
            for (std::uint32_t x = 0; x < 64; ++x)
            {
                pixel_fn(x, y, &image->pixels[(y * 64 + x) * 4]);
            }
        registry.register_resource<render::Image>(
            mosaic::engine::assets::AssetMetadata{id, "texture"}, image);
    };
    register_texture(kCheckerTextureId,
        [](std::uint32_t x, std::uint32_t y, std::uint8_t* p)
        { const std::uint8_t c = (((x / 8) + (y / 8)) % 2 == 0) ? 230 : 60; p[0]=c;p[1]=c;p[2]=c;p[3]=255; });
    register_texture(kSolidTextureId,
        [](std::uint32_t, std::uint32_t, std::uint8_t* p) { p[0]=220;p[1]=120;p[2]=60;p[3]=255; });
    register_texture(kStripesTextureId,
        [](std::uint32_t x, std::uint32_t, std::uint8_t* p)
        { const std::uint8_t c = ((x / 8) % 2 == 0) ? 200 : 40; p[0]=c;p[1]=c;p[2]=c;p[3]=255; });

    return RuntimeScene{std::make_shared<render::RenderSystem>()};
}

// The demo's scripted object orbits a radius-3 circle at y=2. Deterministic (cos/sin of a fixed-dt
// accumulator). Reflected ScriptComponent, so it serializes with the scene and survives build/run.
inline constexpr const char* kOrbitScript = R"py(
import math
_t = 0.0
def on_update(dt):
    global _t
    _t += dt
    tr = get_component(entity, 'Transform')
    tr['position']['x'] = 3.0 * math.cos(_t)
    tr['position']['z'] = 3.0 * math.sin(_t)
    tr['position']['y'] = 2.0
    set_component(entity, 'Transform', tr)
)py";

// Pure scene DATA: the demo entities/components. No device, no registry — this is what
// save_to_json serializes and what a loaded scene replaces. WorldTransform is derived (added by
// TransformSystem), so it is intentionally not set here.
inline void build_default_scene(mosaic::engine::World& world)
{
    namespace render    = mosaic::engine::render;
    namespace transform = mosaic::engine::transform;
    namespace physics   = mosaic::engine::physics;
    using mosaic::foundation::math::Quat;
    using mosaic::foundation::math::Vec3;

    const auto tex = kCheckerTextureId;

    const auto ground = world.create_entity();
    world.add_component<transform::Transform>(ground,
        transform::Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{10, 1, 10}, {}});
    world.add_component<render::MeshRenderer>(ground, render::MeshRenderer{render::kCubeMeshId});
    world.add_component<render::Material>(ground, render::Material{Vec3{0.6f, 0.6f, 0.6f}, {}});
    world.add_component<physics::RigidBody>(ground, physics::RigidBody{false, 1.0f});
    world.add_component<physics::Collider>(ground, physics::Collider{Vec3{5.0f, 0.5f, 5.0f}});

    const auto box = world.create_entity();
    world.add_component<transform::Transform>(box,
        transform::Transform{Vec3{0, 4, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<render::MeshRenderer>(box, render::MeshRenderer{render::kCubeMeshId});
    world.add_component<render::Material>(box, render::Material{Vec3{1, 1, 1}, tex});
    world.add_component<physics::RigidBody>(box, physics::RigidBody{true, 1.0f});
    world.add_component<physics::Collider>(box, physics::Collider{Vec3{0.5f, 0.5f, 0.5f}});

    const auto sphere = world.create_entity();
    world.add_component<transform::Transform>(sphere,
        transform::Transform{Vec3{-2, 4, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<render::MeshRenderer>(sphere, render::MeshRenderer{render::kSphereMeshId});
    world.add_component<render::Material>(sphere, render::Material{Vec3{1, 1, 1}, tex});
    world.add_component<physics::RigidBody>(sphere, physics::RigidBody{true, 1.0f});
    world.add_component<physics::SphereCollider>(sphere, physics::SphereCollider{0.5f});

    const auto capsule = world.create_entity();
    world.add_component<transform::Transform>(capsule,
        transform::Transform{Vec3{2, 4, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<render::MeshRenderer>(capsule, render::MeshRenderer{render::kCapsuleMeshId});
    world.add_component<render::Material>(capsule, render::Material{Vec3{1, 1, 1}, tex});
    world.add_component<physics::RigidBody>(capsule, physics::RigidBody{true, 1.0f});
    world.add_component<physics::CapsuleCollider>(capsule, physics::CapsuleCollider{0.5f, 0.5f});

    const auto box2 = world.create_entity();
    world.add_component<transform::Transform>(box2,
        transform::Transform{Vec3{0, 6, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<render::MeshRenderer>(box2, render::MeshRenderer{render::kCubeMeshId});
    world.add_component<render::Material>(box2, render::Material{Vec3{1, 1, 1}, tex});
    world.add_component<physics::RigidBody>(box2, physics::RigidBody{true, 1.0f});
    world.add_component<physics::Collider>(box2, physics::Collider{Vec3{0.5f, 0.5f, 0.5f}});

    const auto light = world.create_entity();
    world.add_component<render::DirectionalLight>(light,
        render::DirectionalLight{Vec3{-0.5f, -1.0f, -0.3f}, Vec3{1, 1, 1}});

    const auto point = world.create_entity();
    world.add_component<transform::Transform>(point,
        transform::Transform{Vec3{2, 3, 2}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<render::PointLight>(point, render::PointLight{Vec3{1, 0.85f, 0.6f}, 3.0f, 8.0f});

    const auto camera = world.create_entity();
    world.add_component<transform::Transform>(camera,
        transform::Transform{Vec3{0, 2.5f, 9}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<render::Camera>(camera, render::Camera{});

    // A scripted, non-physics demo object: a green cube driven by kOrbitScript (see ScriptSystem).
    const auto orbiter = world.create_entity();
    world.add_component<transform::Transform>(orbiter,
        transform::Transform{Vec3{3, 2, 0}, Quat::identity(), Vec3{0.5f, 0.5f, 0.5f}, {}});
    world.add_component<render::MeshRenderer>(orbiter, render::MeshRenderer{render::kCubeMeshId});
    world.add_component<render::Material>(orbiter, render::Material{Vec3{0.2f, 0.9f, 0.3f}, {}});
    world.add_component<mosaic::engine::script::ScriptComponent>(orbiter,
        mosaic::engine::script::ScriptComponent{kOrbitScript});
}

} // namespace mosaic::app
