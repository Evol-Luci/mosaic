#pragma once

#include <string>

#include "engine/physics/physics_components.hpp"
#include "engine/script/script_component.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::studio
{
// Switch the demo object `e` between script-driven and physics-driven. A ScriptComponent and a
// dynamic RigidBody cannot both drive one Transform (they fight), so this swaps rather than layers.
// Idempotent per target state (remove of an absent component / add of a present one are no-ops).
inline void set_demo_physics(mosaic::engine::World& world, mosaic::foundation::EntityID e,
                             const std::string& script_source, bool physics)
{
    namespace phys = mosaic::engine::physics;
    if (physics)
    {
        world.remove_component<mosaic::engine::script::ScriptComponent>(e);
        world.add_component<phys::RigidBody>(e, phys::RigidBody{true, 1.0f});
        world.add_component<phys::Collider>(e,
            phys::Collider{mosaic::foundation::math::Vec3{0.5f, 0.5f, 0.5f}});
    }
    else
    {
        world.remove_component<phys::RigidBody>(e);
        world.remove_component<phys::Collider>(e);
        world.add_component<mosaic::engine::script::ScriptComponent>(e,
            mosaic::engine::script::ScriptComponent{script_source});
    }
}
} // namespace mosaic::studio
