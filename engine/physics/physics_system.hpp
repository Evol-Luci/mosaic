#pragma once

#include <memory>
#include <unordered_map>

#include "engine/ecs/system.hpp"
#include "engine/physics/physics_backend.hpp"
#include "engine/physics/physics_components.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"

namespace mosaic::engine::physics
{
// Steps a PhysicsBackend inside World::tick and syncs simulated poses to Transform.
// The backend is injected (via add_system<PhysicsSystem>(backend)); this system depends
// only on the neutral interface, never on any concrete backend module.
class PhysicsSystem : public ecs::System
{
public:
    explicit PhysicsSystem(std::unique_ptr<PhysicsBackend> backend, int substeps = 4)
        : backend_(std::move(backend)), substeps_(substeps)
    {
        access_.reads<RigidBody>().reads<Collider>().writes<transform::Transform>();
    }

    void update(World& world, float dt) override
    {
        // 1. Lazily create a backend body for each physics entity we have not seen.
        world.for_each<RigidBody>(
            [&](mosaic::foundation::EntityID e, RigidBody& rb)
            {
                if (bodies_.find(e) != bodies_.end())
                {
                    return;
                }
                const Collider*             col = world.get_component<Collider>(e);
                const transform::Transform* t = world.get_component<transform::Transform>(e);
                if (col == nullptr || t == nullptr)
                {
                    return;
                }
                BodyDef def;
                def.type = rb.dynamic ? BodyType::Dynamic : BodyType::Static;
                def.position = t->position;
                def.shape.half_extents = col->half_extents;
                def.density = rb.density;
                bodies_[e] = backend_->create_body(def);
            });

        // 2. Advance the simulation one fixed step.
        backend_->step(dt, substeps_);

        // 3. Write dynamic-body poses back into Transform (static bodies do not move).
        for (const auto& [e, handle] : bodies_)
        {
            const RigidBody* rb = world.get_component<RigidBody>(e);
            if (rb == nullptr || !rb->dynamic)
            {
                continue;
            }
            transform::Transform* t = world.get_component<transform::Transform>(e);
            if (t == nullptr)
            {
                continue;
            }
            const Pose pose = backend_->body_pose(handle);
            t->position = pose.position;
            t->rotation = pose.rotation;
        }
    }

private:
    std::unique_ptr<PhysicsBackend>                              backend_;
    std::unordered_map<mosaic::foundation::EntityID, BodyHandle> bodies_;
    int                                                          substeps_;
};
} // namespace mosaic::engine::physics
