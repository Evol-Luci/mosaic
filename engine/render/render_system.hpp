#pragma once

#include "engine/assets/asset_registry.hpp"
#include "engine/render/camera.hpp"
#include "engine/render/directional_light.hpp"
#include "engine/render/point_light.hpp"
#include "engine/render/image.hpp"
#include "engine/render/material.hpp"
#include "engine/render/mesh_cache.hpp"
#include "engine/render/mesh_renderer.hpp"
#include "engine/render/render_device.hpp"
#include "engine/render/texture_cache.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "engine/particles/particle_system.hpp"
#include "engine/vfx/trail_renderer.hpp"
#include "engine/vfx/trail_system.hpp"
#include "foundation/math/mat4.hpp"
#include "foundation/math/vec3.hpp"
#include <algorithm>
#include <vector>

namespace mosaic::engine::render
{
// Render-time traversal: finds the active camera + directional light and draws every
// entity with WorldTransform + MeshRenderer, lit (Lambert; light transformed into each
// mesh's object space so the backend needs no model matrix). Fallbacks keep the scene
// lit/rendered when a camera/light/material is absent.
class RenderSystem
{
public:
    void set_aspect(float aspect) { aspect_ = aspect; }

    void render(World& world, RenderDevice& device,
                mosaic::engine::assets::AssetRegistry& registry) const
    {
        using mosaic::foundation::math::Mat4;
        using mosaic::foundation::math::Vec3;

        Mat4 view;
        Mat4 projection;
        if (!find_active_camera(world, view, projection))
        {
            view = fallback_view();
            projection = Mat4::perspective(kFallbackFovY, aspect_, kFallbackNear, kFallbackFar);
        }

        DirectionalLight light = active_light(world);
        const PointLightState point = active_point_light(world);

        world.for_each<MeshRenderer>(
            [&](mosaic::foundation::EntityID e, MeshRenderer& mr)
            {
                const MeshHandle mesh = mesh_cache_.get(mr.mesh, registry, device);
                if (mesh == 0) // nil / unregistered / failed upload -> nothing to draw
                {
                    return;
                }
                const auto* wt = world.get_component<transform::WorldTransform>(e);
                if (wt == nullptr)
                {
                    return;
                }
                Vec3 base_color{0.8f, 0.8f, 0.8f};
                TextureHandle texture = 0;
                if (const Material* mat = world.get_component<Material>(e))
                {
                    base_color = mat->base_color;
                    texture = texture_cache_.get(mat->texture, registry, device);
                }
                if (texture == 0)
                {
                    texture = default_white(device);
                }
                const Vec3 light_obj = mosaic::foundation::math::normalize(
                    mosaic::foundation::math::transform_direction(
                        Mat4::inverse_affine(wt->matrix), light.direction));

                Vec3 point_position{0.0f, 0.0f, 0.0f};
                Vec3 point_color{0.0f, 0.0f, 0.0f};
                float point_range = 0.0f;
                if (point.present)
                {
                    point_position = mosaic::foundation::math::transform_point(
                        Mat4::inverse_affine(wt->matrix), point.world_pos);
                    point_color = point.color;
                    point_range = point.range;
                }

                device.draw(mesh, texture,
                            DrawParams{projection * view * wt->matrix, light_obj,
                                       light.color, base_color, point_position, point_color, point_range});
            });

        // Billboard pass: particles + trails share one instanced, camera-facing draw. Runs
        // last in the pass (the device binds the particle pipeline; no mesh draw may follow).
        auto* particles = world.get_system<mosaic::engine::particles::ParticleSystem>();
        auto* trails    = world.get_system<mosaic::engine::vfx::TrailSystem>();
        if (particles != nullptr || trails != nullptr)
        {
            const Mat4 cam_world = Mat4::inverse_affine(view);
            const Vec3 cam_right = mosaic::foundation::math::normalize(
                Vec3{cam_world.m[0], cam_world.m[1], cam_world.m[2]});
            const Vec3 cam_up = mosaic::foundation::math::normalize(
                Vec3{cam_world.m[4], cam_world.m[5], cam_world.m[6]});

            std::vector<ParticleInstance> instances;

            if (particles != nullptr)
            {
                world.for_each<mosaic::engine::particles::ParticleEmitter>(
                    [&](mosaic::foundation::EntityID e, mosaic::engine::particles::ParticleEmitter& em)
                    {
                        const auto* pool = particles->particles_for(e);
                        if (pool == nullptr) { return; }
                        for (const auto& p : *pool)
                        {
                            const float t = (p.lifetime > 0.0f) ? p.age / p.lifetime : 0.0f;
                            instances.push_back(ParticleInstance{
                                p.position,
                                mosaic::engine::particles::size_at(em, t),
                                mosaic::engine::particles::color_at(em, t),
                                mosaic::engine::particles::alpha_at(em, t)});
                        }
                    });
            }

            if (trails != nullptr)
            {
                world.for_each<mosaic::engine::vfx::TrailRenderer>(
                    [&](mosaic::foundation::EntityID e, mosaic::engine::vfx::TrailRenderer& tr)
                    {
                        const auto* pts = trails->points_for(e);
                        if (pts == nullptr || pts->empty()) { return; }
                        const int n = static_cast<int>(pts->size());
                        for (int i = 0; i < n; ++i)
                        {
                            const float frac = static_cast<float>(i + 1) / static_cast<float>(n);
                            instances.push_back(
                                ParticleInstance{(*pts)[i], tr.size * frac, tr.color, frac});
                        }
                    });
            }

            if (!instances.empty())
            {
                device.draw_particles(instances,
                                      ParticleDrawParams{projection * view, cam_right, cam_up});
            }
        }
    }

private:
    static constexpr float kFallbackFovY = 1.0472f;
    static constexpr float kFallbackNear = 0.1f;
    static constexpr float kFallbackFar = 100.0f;

    TextureHandle default_white(RenderDevice& device) const
    {
        if (default_white_ == 0)
        {
            Image white;
            white.width = 1;
            white.height = 1;
            white.pixels = {255, 255, 255, 255};
            default_white_ = device.create_texture(white);
        }
        return default_white_;
    }

    static mosaic::foundation::math::Mat4 fallback_view()
    {
        using mosaic::foundation::math::Mat4;
        using mosaic::foundation::math::Vec3;
        return Mat4::look_at(Vec3{0.0f, 1.5f, 4.0f}, Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f});
    }

    struct PointLightState
    {
        bool                           present = false;
        mosaic::foundation::math::Vec3 world_pos{};
        mosaic::foundation::math::Vec3 color{}; // premultiplied by intensity
        float                          range = 0.0f;
    };

    // First PointLight whose entity has a WorldTransform; world position = its translation.
    PointLightState active_point_light(World& world) const
    {
        using mosaic::foundation::math::Vec3;
        PointLightState s;
        world.for_each<PointLight>(
            [&](mosaic::foundation::EntityID e, PointLight& p)
            {
                if (s.present)
                {
                    return;
                }
                const auto* wt = world.get_component<transform::WorldTransform>(e);
                if (wt == nullptr)
                {
                    return;
                }
                s.present = true;
                s.world_pos =
                    mosaic::foundation::math::transform_point(wt->matrix, Vec3{0.0f, 0.0f, 0.0f});
                s.color = p.color * p.intensity;
                s.range = p.range;
            });
        return s;
    }

    // First DirectionalLight, else a default.
    DirectionalLight active_light(World& world) const
    {
        DirectionalLight light{mosaic::foundation::math::Vec3{-0.5f, -1.0f, -0.3f},
                               mosaic::foundation::math::Vec3{1.0f, 1.0f, 1.0f}};
        bool found = false;
        world.for_each<DirectionalLight>(
            [&](mosaic::foundation::EntityID, DirectionalLight& l)
            {
                if (!found)
                {
                    light = l;
                    found = true;
                }
            });
        return light;
    }

    bool find_active_camera(World& world, mosaic::foundation::math::Mat4& view,
                            mosaic::foundation::math::Mat4& projection) const
    {
        using mosaic::foundation::math::Mat4;
        bool found = false;
        world.for_each<Camera>(
            [&](mosaic::foundation::EntityID e, Camera& cam)
            {
                if (found)
                {
                    return;
                }
                const auto* wt = world.get_component<transform::WorldTransform>(e);
                if (wt == nullptr)
                {
                    return;
                }
                view = Mat4::inverse_affine(wt->matrix);
                projection = Mat4::perspective(cam.fov_y_radians, aspect_, cam.near_z, cam.far_z);
                found = true;
            });
        return found;
    }

    float aspect_ = 16.0f / 9.0f;
    mutable MeshCache     mesh_cache_;
    mutable TextureCache  texture_cache_;
    mutable TextureHandle default_white_ = 0;
};
} // namespace mosaic::engine::render
