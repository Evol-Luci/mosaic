#pragma once

#include <functional>
#include <utility>

#include "engine/ecs/system.hpp"
#include "engine/physics/physics_system.hpp"
#include "engine/ui/pointer.hpp"
#include "engine/world/world.hpp"
#include "foundation/math/vec3.hpp"
#include "studio/pick.hpp"

namespace mosaic::app
{

// PointerState is normalized [0,1] with a TOP-LEFT origin; studio::pick_entity wants
// NDC in [-1,1] with +y UP. The Y flip is the easy thing to drop, and dropping it
// still "works" -- it just picks the vertically mirrored object.
inline float pointer_to_ndc_x(float px) { return px * 2.0f - 1.0f; }
inline float pointer_to_ndc_y(float py) { return 1.0f - py * 2.0f; }

// Turns a left-click into a physics impulse on whatever body is under the cursor,
// pushed along the camera ray so the contact point is off-centre and the body rolls.
//
// Ordering: this must be registered AFTER UiSystem, so that `ui_claimed` reflects the
// same pointer state this system is reacting to. World systems run in registration
// order, so adding it after UiSystem is sufficient.
class ClickNudgeSystem : public mosaic::engine::ecs::System
{
public:
    ClickNudgeSystem(std::function<mosaic::engine::ui::PointerState()> pointer,
                     std::function<float()>                            aspect,
                     float                                             strength,
                     std::function<bool()> ui_claimed = {})
        : pointer_(std::move(pointer)), aspect_(std::move(aspect)), strength_(strength),
          ui_claimed_(std::move(ui_claimed))
    {
    }

    void update(mosaic::engine::World& world, float /*dt*/) override
    {
        const mosaic::engine::ui::PointerState p =
            pointer_ ? pointer_() : mosaic::engine::ui::PointerState{};
        const bool press_edge = p.down && !was_down_;
        was_down_ = p.down;
        if (!press_edge)
        {
            return;
        }
        if (ui_claimed_ && ui_claimed_())
        {
            return; // the UI owns this click
        }

        const float aspect = aspect_ ? aspect_() : 1.0f;
        const mosaic::engine::physics::RaycastHit hit = mosaic::studio::pick_entity(
            world, aspect, pointer_to_ndc_x(p.x), pointer_to_ndc_y(p.y));
        if (!hit.hit)
        {
            return;
        }

        auto* physics = world.get_system<mosaic::engine::physics::PhysicsSystem>();
        if (physics == nullptr)
        {
            return;
        }

        // Push along the camera ray: from the eye through the clicked point. Because
        // the hit point is off the body's centre, this produces spin as well as
        // translation, which is what makes a struck sphere roll.
        const mosaic::foundation::math::Vec3 dir =
            mosaic::foundation::math::normalize(hit.point - camera_position(world));
        physics->apply_impulse(hit.entity, dir * strength_, hit.point);
    }

private:
    static mosaic::foundation::math::Vec3 camera_position(mosaic::engine::World& world)
    {
        mosaic::foundation::math::Vec3 out{};
        bool                           found = false;
        world.for_each<mosaic::engine::render::Camera>(
            [&](mosaic::foundation::EntityID e, mosaic::engine::render::Camera&)
            {
                if (found) { return; }
                const auto* wt =
                    world.get_component<mosaic::engine::transform::WorldTransform>(e);
                if (wt == nullptr) { return; }
                out = mosaic::foundation::math::Vec3{wt->matrix.m[12], wt->matrix.m[13],
                                                     wt->matrix.m[14]};
                found = true;
            });
        return out;
    }

    std::function<mosaic::engine::ui::PointerState()> pointer_;
    std::function<float()>                            aspect_;
    float                                             strength_ = 1.0f;
    std::function<bool()>                             ui_claimed_;
    bool                                              was_down_ = false;
};

} // namespace mosaic::app
