#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "engine/assets/asset_metadata.hpp"
#include "engine/assets/asset_registry.hpp"
#include "engine/ecs/system.hpp"
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
#include "foundation/math/vec3.hpp"

namespace mosaic::app
{

// Spins every entity that has a MeshRenderer (the cube), leaving the camera still.
class SpinSystem : public mosaic::engine::ecs::System
{
public:
    void update(mosaic::engine::World& world, float dt) override
    {
        namespace m = mosaic::foundation::math;
        angle_ += dt * 0.9F;
        const float   half = angle_ * 0.5F;
        const m::Quat spin{0.0F, std::sin(half), 0.0F, std::cos(half)};
        world.for_each<mosaic::engine::transform::Transform>(
            [&](mosaic::foundation::EntityID e, mosaic::engine::transform::Transform& t)
            {
                if (!world.has_component<mosaic::engine::render::MeshRenderer>(e))
                {
                    return; // only spin meshes (the cube), not the camera
                }
                t.rotation = spin;
            });
    }

private:
    float angle_ = 0.0F;
};

struct DemoScene
{
    std::shared_ptr<mosaic::engine::render::RenderSystem> render_system;
};

// Builds the standard v0.1 demo (textured spinning cube, directional + point light,
// placed camera) into `world`, uploading the cube mesh + checkerboard texture through
// `device`/`registry`. Adds a SpinSystem. The caller is responsible for adding a
// TransformSystem and ticking the world.
inline DemoScene build_demo_scene(mosaic::engine::World&                 world,
                                  mosaic::engine::render::RenderDevice&  device,
                                  mosaic::engine::assets::AssetRegistry& registry)
{
    namespace render    = mosaic::engine::render;
    namespace transform = mosaic::engine::transform;
    using mosaic::foundation::math::Vec3;

    world.add_system<SpinSystem>();

    std::vector<render::Vertex> cube_v;
    std::vector<std::uint32_t>  cube_i;
    render::make_cube_mesh(cube_v, cube_i);
    const render::MeshHandle cube = device.create_mesh(cube_v, cube_i);

    const auto cube_entity = world.create_entity();
    world.add_component<transform::Transform>(cube_entity, transform::Transform{});
    world.add_component<transform::WorldTransform>(
        cube_entity, transform::WorldTransform{mosaic::foundation::math::Mat4::identity()});
    world.add_component<render::MeshRenderer>(cube_entity, render::MeshRenderer{cube});

    auto checker    = std::make_shared<render::Image>();
    checker->width  = 64;
    checker->height = 64;
    checker->pixels.resize(64 * 64 * 4);
    for (std::uint32_t y = 0; y < 64; ++y)
        for (std::uint32_t x = 0; x < 64; ++x)
        {
            const bool         on = ((x / 8) + (y / 8)) % 2 == 0;
            const std::uint8_t c  = on ? 230 : 60;
            std::uint8_t*      p  = &checker->pixels[(y * 64 + x) * 4];
            p[0] = c;
            p[1] = c;
            p[2] = c;
            p[3] = 255;
        }
    std::array<std::uint8_t, 16> tex_uuid{};
    tex_uuid[0] = 0xC0;
    tex_uuid[1] = 0xDE;
    const mosaic::foundation::serialization::AssetID tex_id{tex_uuid};
    registry.register_resource<render::Image>(
        mosaic::engine::assets::AssetMetadata{tex_id, "texture"}, checker);
    world.add_component<render::Material>(cube_entity,
                                          render::Material{Vec3{1.0F, 1.0F, 1.0F}, tex_id});

    const auto light_entity = world.create_entity();
    world.add_component<render::DirectionalLight>(
        light_entity, render::DirectionalLight{Vec3{-0.5F, -1.0F, -0.3F}, Vec3{1.0F, 1.0F, 1.0F}});

    const auto point_light_entity = world.create_entity();
    world.add_component<transform::Transform>(
        point_light_entity,
        transform::Transform{Vec3{1.5F, 1.0F, 1.5F}, mosaic::foundation::math::Quat::identity(),
                             Vec3{1.0F, 1.0F, 1.0F}, {}});
    world.add_component<transform::WorldTransform>(
        point_light_entity, transform::WorldTransform{mosaic::foundation::math::Mat4::translation(
                                Vec3{1.5F, 1.0F, 1.5F})});
    world.add_component<render::PointLight>(point_light_entity,
                                            render::PointLight{Vec3{1.0F, 0.85F, 0.6F}, 3.0F, 6.0F});

    const auto camera_entity = world.create_entity();
    world.add_component<transform::Transform>(
        camera_entity,
        transform::Transform{Vec3{0.0F, 0.0F, 4.0F}, mosaic::foundation::math::Quat::identity(),
                             Vec3{1.0F, 1.0F, 1.0F}, {}});
    world.add_component<transform::WorldTransform>(
        camera_entity, transform::WorldTransform{mosaic::foundation::math::Mat4::translation(
                           Vec3{0.0F, 0.0F, 4.0F})});
    world.add_component<render::Camera>(camera_entity, render::Camera{});

    return DemoScene{std::make_shared<render::RenderSystem>()};
}

} // namespace mosaic::app
