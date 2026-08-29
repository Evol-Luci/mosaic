#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "engine/assets/asset_metadata.hpp"
#include "engine/assets/asset_registry.hpp"
#include "engine/physics/physics_components.hpp"
#include "engine/physics/physics_system.hpp"
#include "engine/render/builtin_meshes.hpp"
#include "engine/render/camera.hpp"
#include "engine/render/cube.hpp"
#include "engine/render/directional_light.hpp"
#include "engine/render/image.hpp"
#include "engine/render/material.hpp"
#include "engine/render/mesh_renderer.hpp"
#include "engine/render/point_light.hpp"
#include "engine/render/render_device.hpp"
#include "engine/render/render_system.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/logging/log.hpp"
#include "foundation/math/vec3.hpp"
#include "modules/physics_box3d/box3d_backend.hpp"

namespace mosaic::app
{

struct DemoScene
{
    std::shared_ptr<mosaic::engine::render::RenderSystem> render_system;
};

// Builds the physics demo: a textured box drops onto a static ground slab. Adds a
// PhysicsSystem (Box3D backend injected) and returns a RenderSystem. The three procedural
// textures remain (the asset browser assigns them to the box's Material).
inline DemoScene build_demo_scene(mosaic::engine::World&                 world,
                                  mosaic::engine::render::RenderDevice&  device,
                                  mosaic::engine::assets::AssetRegistry& registry)
{
    namespace render    = mosaic::engine::render;
    namespace transform = mosaic::engine::transform;
    namespace physics   = mosaic::engine::physics;
    using mosaic::foundation::math::Mat4;
    using mosaic::foundation::math::Quat;
    using mosaic::foundation::math::Vec3;

    world.add_system<physics::PhysicsSystem>(
        mosaic::physics::box3d::create_box3d_backend(physics::WorldDef{}));

    // The built-in cube is a registered mesh asset; every MeshRenderer references it by
    // AssetID (kCubeMeshId) and the RenderSystem's MeshCache uploads it once on first draw.
    render::register_builtin_meshes(registry);

    // Log physics contacts so they show in the editor Console (#33 / 37e) and on stderr.
    world.events().subscribe<physics::ContactEvent>(
        [](const physics::ContactEvent& c)
        {
            mosaic::foundation::log::info(
                "physics", "contact {}: entity {} <-> entity {}",
                c.phase == physics::ContactPhase::Begin ? "begin" : "end", c.a.index(),
                c.b.index());
        });

    // Three procedural textures (asset-browser targets); the box starts on the checkerboard.
    auto register_texture = [&](std::array<std::uint8_t, 16> uuid, auto&& pixel_fn)
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
        const mosaic::foundation::serialization::AssetID id{uuid};
        registry.register_resource<render::Image>(
            mosaic::engine::assets::AssetMetadata{id, "texture"}, image);
        return id;
    };
    std::array<std::uint8_t, 16> checker_uuid{};
    checker_uuid[0] = 0xC0;
    checker_uuid[1] = 0xDE;
    std::array<std::uint8_t, 16> solid_uuid{};
    solid_uuid[0] = 0xA1;
    std::array<std::uint8_t, 16> stripes_uuid{};
    stripes_uuid[0] = 0xB2;
    const auto tex_id = register_texture(checker_uuid,
        [](std::uint32_t x, std::uint32_t y, std::uint8_t* p)
        { const std::uint8_t c = (((x / 8) + (y / 8)) % 2 == 0) ? 230 : 60; p[0]=c;p[1]=c;p[2]=c;p[3]=255; });
    register_texture(solid_uuid,
        [](std::uint32_t, std::uint32_t, std::uint8_t* p) { p[0]=220;p[1]=120;p[2]=60;p[3]=255; });
    register_texture(stripes_uuid,
        [](std::uint32_t x, std::uint32_t, std::uint8_t* p)
        { const std::uint8_t c = ((x / 8) % 2 == 0) ? 200 : 40; p[0]=c;p[1]=c;p[2]=c;p[3]=255; });

    // Static ground slab: cube scaled to a 10x1x10 slab (collider half-extents 5,0.5,5),
    // top surface at y=0.5.
    const auto ground = world.create_entity();
    world.add_component<transform::Transform>(ground,
        transform::Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{10, 1, 10}, {}});
    world.add_component<transform::WorldTransform>(ground,
        transform::WorldTransform{Mat4::trs(Vec3{0, 0, 0}, Quat::identity(), Vec3{10, 1, 10})});
    world.add_component<render::MeshRenderer>(ground, render::MeshRenderer{render::kCubeMeshId});
    world.add_component<render::Material>(ground, render::Material{Vec3{0.6f, 0.6f, 0.6f}, {}});
    world.add_component<physics::RigidBody>(ground, physics::RigidBody{false, 1.0f});
    world.add_component<physics::Collider>(ground, physics::Collider{Vec3{5.0f, 0.5f, 5.0f}});

    // Dynamic box: falls from y=4, rests at y~=1.0.
    const auto box = world.create_entity();
    world.add_component<transform::Transform>(box,
        transform::Transform{Vec3{0, 4, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<transform::WorldTransform>(box,
        transform::WorldTransform{Mat4::translation(Vec3{0, 4, 0})});
    world.add_component<render::MeshRenderer>(box, render::MeshRenderer{render::kCubeMeshId});
    world.add_component<render::Material>(box, render::Material{Vec3{1, 1, 1}, tex_id});
    world.add_component<physics::RigidBody>(box, physics::RigidBody{true, 1.0f});
    world.add_component<physics::Collider>(box, physics::Collider{Vec3{0.5f, 0.5f, 0.5f}});

    // A sphere collider dropping beside the box (cube mesh is a visual stand-in).
    const auto sphere = world.create_entity();
    world.add_component<transform::Transform>(sphere,
        transform::Transform{Vec3{-2, 4, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<transform::WorldTransform>(sphere,
        transform::WorldTransform{Mat4::translation(Vec3{-2, 4, 0})});
    world.add_component<render::MeshRenderer>(sphere, render::MeshRenderer{render::kCubeMeshId});
    world.add_component<render::Material>(sphere, render::Material{Vec3{1, 1, 1}, tex_id});
    world.add_component<physics::RigidBody>(sphere, physics::RigidBody{true, 1.0f});
    world.add_component<physics::SphereCollider>(sphere, physics::SphereCollider{0.5f});

    // A capsule collider dropping on the other side.
    const auto capsule = world.create_entity();
    world.add_component<transform::Transform>(capsule,
        transform::Transform{Vec3{2, 4, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<transform::WorldTransform>(capsule,
        transform::WorldTransform{Mat4::translation(Vec3{2, 4, 0})});
    world.add_component<render::MeshRenderer>(capsule, render::MeshRenderer{render::kCubeMeshId});
    world.add_component<render::Material>(capsule, render::Material{Vec3{1, 1, 1}, tex_id});
    world.add_component<physics::RigidBody>(capsule, physics::RigidBody{true, 1.0f});
    world.add_component<physics::CapsuleCollider>(capsule, physics::CapsuleCollider{0.5f, 0.5f});

    // A second box stacked above the first (drops onto it -> a 2-box stack).
    const auto box2 = world.create_entity();
    world.add_component<transform::Transform>(box2,
        transform::Transform{Vec3{0, 6, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<transform::WorldTransform>(box2,
        transform::WorldTransform{Mat4::translation(Vec3{0, 6, 0})});
    world.add_component<render::MeshRenderer>(box2, render::MeshRenderer{render::kCubeMeshId});
    world.add_component<render::Material>(box2, render::Material{Vec3{1, 1, 1}, tex_id});
    world.add_component<physics::RigidBody>(box2, physics::RigidBody{true, 1.0f});
    world.add_component<physics::Collider>(box2, physics::Collider{Vec3{0.5f, 0.5f, 0.5f}});

    // Lights.
    const auto light = world.create_entity();
    world.add_component<render::DirectionalLight>(light,
        render::DirectionalLight{Vec3{-0.5f, -1.0f, -0.3f}, Vec3{1, 1, 1}});
    const auto point = world.create_entity();
    world.add_component<transform::Transform>(point,
        transform::Transform{Vec3{2, 3, 2}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<transform::WorldTransform>(point,
        transform::WorldTransform{Mat4::translation(Vec3{2, 3, 2})});
    world.add_component<render::PointLight>(point, render::PointLight{Vec3{1, 0.85f, 0.6f}, 3.0f, 8.0f});

    // Camera: raised at (0, 2.5, 9) looking down -Z, framing the ground slab + the drop.
    const auto camera = world.create_entity();
    world.add_component<transform::Transform>(camera,
        transform::Transform{Vec3{0, 2.5f, 9}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<transform::WorldTransform>(camera,
        transform::WorldTransform{Mat4::translation(Vec3{0, 2.5f, 9})});
    world.add_component<render::Camera>(camera, render::Camera{});

    return DemoScene{std::make_shared<render::RenderSystem>()};
}

} // namespace mosaic::app
