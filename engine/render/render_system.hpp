#pragma once

#include "engine/render/mesh_renderer.hpp"
#include "engine/render/render_device.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/math/mat4.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::engine::render
{
// Render-time traversal (NOT a World tick System): draws every entity with a
// WorldTransform + MeshRenderer, transformed by a fixed camera. The app calls
// render() inside on_frame, between clear() and end_frame().
class RenderSystem
{
public:
    RenderSystem()
    {
        using mosaic::foundation::math::Mat4;
        using mosaic::foundation::math::Vec3;
        view_ = Mat4::look_at(Vec3{0.0f, 1.5f, 4.0f}, Vec3{0.0f, 0.0f, 0.0f},
                              Vec3{0.0f, 1.0f, 0.0f});
        projection_ = Mat4::perspective(kFovY, kDefaultAspect, kNear, kFar);
    }

    // Rebuild the projection for a new aspect ratio (called by the app on resize).
    void set_aspect(float aspect)
    {
        projection_ = mosaic::foundation::math::Mat4::perspective(kFovY, aspect, kNear, kFar);
    }

    void render(World& world, RenderDevice& device) const
    {
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
                device.draw(mr.mesh, projection_ * view_ * wt->matrix);
            });
    }

    const mosaic::foundation::math::Mat4& view() const { return view_; }
    const mosaic::foundation::math::Mat4& projection() const { return projection_; }

private:
    static constexpr float kFovY = 1.0472f;        // 60 degrees
    static constexpr float kDefaultAspect = 16.0f / 9.0f;
    static constexpr float kNear = 0.1f;
    static constexpr float kFar = 100.0f;

    mosaic::foundation::math::Mat4 view_;
    mosaic::foundation::math::Mat4 projection_;
};
} // namespace mosaic::engine::render
