#pragma once

#include "engine/render/camera.hpp"
#include "engine/render/mesh_renderer.hpp"
#include "engine/render/render_device.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/math/mat4.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::engine::render
{
// Render-time traversal (NOT a World tick System): each frame it finds the active
// camera (first entity with Camera + WorldTransform), builds view = inverse of the
// camera's world matrix and a perspective projection from its lens, and draws every
// entity with WorldTransform + MeshRenderer. With no camera it falls back to a default
// so the scene still renders. The app calls render() in on_frame between clear() and
// end_frame().
class RenderSystem
{
public:
    // Set the viewport aspect ratio (width/height); used when building the projection.
    void set_aspect(float aspect) { aspect_ = aspect; }

    void render(World& world, RenderDevice& device) const
    {
        using mosaic::foundation::math::Mat4;

        Mat4 view;
        Mat4 projection;
        if (!find_active_camera(world, view, projection))
        {
            view = fallback_view();
            projection = mosaic::foundation::math::Mat4::perspective(kFallbackFovY, aspect_,
                                                                     kFallbackNear, kFallbackFar);
        }

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
                device.draw(mr.mesh, projection * view * wt->matrix);
            });
    }

private:
    static constexpr float kFallbackFovY = 1.0472f; // 60 degrees
    static constexpr float kFallbackNear = 0.1f;
    static constexpr float kFallbackFar = 100.0f;

    // Default camera used only when the world has no Camera entity.
    static mosaic::foundation::math::Mat4 fallback_view()
    {
        using mosaic::foundation::math::Mat4;
        using mosaic::foundation::math::Vec3;
        return Mat4::look_at(Vec3{0.0f, 1.5f, 4.0f}, Vec3{0.0f, 0.0f, 0.0f},
                             Vec3{0.0f, 1.0f, 0.0f});
    }

    // First entity with Camera + WorldTransform sets view/projection; returns false if
    // none. Deterministic (component iteration order).
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
