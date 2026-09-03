#pragma once

#include <cmath>

#include "engine/physics/physics_system.hpp"
#include "engine/render/camera.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"
#include "foundation/math/mat4.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::studio
{
// Pick the entity under a viewport click. (ndc_x, ndc_y) in [-1,1] (center = 0,0; +y up); `aspect` =
// viewport width/height. Finds the active camera, unprojects a world ray, and raycasts the physics
// world. Returns the RaycastHit (hit==false if no camera / no physics system / nothing hit). Never
// throws.
inline mosaic::engine::physics::RaycastHit
pick_entity(mosaic::engine::World& world, float aspect, float ndc_x, float ndc_y)
{
    using mosaic::foundation::math::Mat4;
    using mosaic::foundation::math::Vec3;
    namespace render    = mosaic::engine::render;
    namespace transform = mosaic::engine::transform;

    Mat4  cam_world;
    float tan_half = 0.0f;
    bool  found    = false;
    world.for_each<render::Camera>(
        [&](mosaic::foundation::EntityID e, render::Camera& cam)
        {
            if (found) { return; }
            const transform::WorldTransform* wt = world.get_component<transform::WorldTransform>(e);
            if (wt == nullptr) { return; }
            cam_world = wt->matrix;
            tan_half  = std::tan(cam.fov_y_radians * 0.5f);
            found     = true;
        });
    mosaic::engine::physics::RaycastHit miss;
    if (!found) { return miss; }

    const Vec3 dir_view{ndc_x * aspect * tan_half, ndc_y * tan_half, -1.0f};
    const Vec3 dir = mosaic::foundation::math::normalize(
        mosaic::foundation::math::transform_direction(cam_world, dir_view));
    const Vec3 origin{cam_world.m[12], cam_world.m[13], cam_world.m[14]};

    mosaic::engine::physics::PhysicsSystem* physics =
        world.get_system<mosaic::engine::physics::PhysicsSystem>();
    if (physics == nullptr) { return miss; }
    return physics->raycast(origin, dir, 1000.0f);
}
} // namespace mosaic::studio
