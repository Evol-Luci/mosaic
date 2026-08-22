#pragma once

#include "engine/render/camera.hpp"
#include "engine/render/directional_light.hpp"
#include "engine/render/material.hpp"
#include "engine/render/mesh_renderer.hpp"
#include "engine/render/render_device.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/math/mat4.hpp"
#include "foundation/math/vec3.hpp"

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

    void render(World& world, RenderDevice& device) const
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

        world.for_each<MeshRenderer>(
            [&](mosaic::foundation::EntityID e, MeshRenderer& mr)
            {
                if (mr.mesh == 0)
                {
                    return;
                }
                const auto* wt = world.get_component<transform::WorldTransform>(e);
                if (wt == nullptr)
                {
                    return;
                }
                Vec3 base_color{0.8f, 0.8f, 0.8f};
                if (const Material* mat = world.get_component<Material>(e))
                {
                    base_color = mat->base_color;
                }
                const Vec3 light_obj = mosaic::foundation::math::normalize(
                    mosaic::foundation::math::transform_direction(
                        Mat4::inverse_affine(wt->matrix), light.direction));

                device.draw(mr.mesh, DrawParams{projection * view * wt->matrix, light_obj,
                                                light.color, base_color});
            });
    }

private:
    static constexpr float kFallbackFovY = 1.0472f;
    static constexpr float kFallbackNear = 0.1f;
    static constexpr float kFallbackFar = 100.0f;

    static mosaic::foundation::math::Mat4 fallback_view()
    {
        using mosaic::foundation::math::Mat4;
        using mosaic::foundation::math::Vec3;
        return Mat4::look_at(Vec3{0.0f, 1.5f, 4.0f}, Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f});
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
};
} // namespace mosaic::engine::render
