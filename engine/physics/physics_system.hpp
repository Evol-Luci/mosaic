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
// A physics contact between two entities, emitted on the World's EventBus each tick.
struct ContactEvent
{
    ContactPhase                 phase;
    mosaic::foundation::EntityID a;
    mosaic::foundation::EntityID b;
    mosaic::foundation::math::Vec3 point{};   // world contact point (Begin only; zero for End)
    mosaic::foundation::math::Vec3 normal{};  // world contact normal
    float                          separation = 0.0f;
    float                          normal_impulse = 0.0f;
};

// The entity-level result of PhysicsSystem::raycast (the backend's RaycastResult mapped to
// the hit entity).
struct RaycastHit
{
    bool                           hit = false;
    mosaic::foundation::EntityID   entity{}; // valid only when hit
    mosaic::foundation::math::Vec3 point{};
    mosaic::foundation::math::Vec3 normal{};
    float                          distance = 0.0f;
};

// Steps a PhysicsBackend inside World::tick and syncs simulated poses to Transform.
// The backend is injected (via add_system<PhysicsSystem>(backend)); this system depends
// only on the neutral interface, never on any concrete backend module.
class PhysicsSystem : public ecs::System
{
public:
    explicit PhysicsSystem(std::unique_ptr<PhysicsBackend> backend, int substeps = 4)
        : backend_(std::move(backend)), substeps_(substeps)
    {
        access_.reads<RigidBody>()
            .reads<Collider>()
            .reads<SphereCollider>()
            .reads<CapsuleCollider>()
            .writes<transform::Transform>();
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
                const transform::Transform* t = world.get_component<transform::Transform>(e);
                if (t == nullptr)
                {
                    return;
                }
                Shape shape;
                if (!shape_for(world, e, shape))
                {
                    return;
                }
                BodyDef def;
                def.type = rb.dynamic ? BodyType::Dynamic : BodyType::Static;
                def.position = t->position;
                // Pass the authored rotation through, so the body the backend creates
                // matches the Transform. This also makes the synced_pose_ record below
                // truthful -- it has always claimed the body carried t->rotation.
                def.rotation = t->rotation;
                def.shape = shape;
                def.density = rb.density;
                const BodyHandle handle = backend_->create_body(def);
                bodies_[e] = handle;
                handle_to_entity_[handle] = e;
                synced_pose_[e] = Pose{t->position, t->rotation};
            });

        // 2. Push external Transform edits (e.g. inspector drags) into the simulation:
        // if an entity's Transform no longer matches the pose we last wrote to it, the
        // user moved it, so teleport the body to the new Transform.
        for (const auto& [e, handle] : bodies_)
        {
            const transform::Transform* t = world.get_component<transform::Transform>(e);
            if (t == nullptr)
            {
                continue;
            }
            if (!pose_matches(*t, synced_pose_[e]))
            {
                backend_->set_body_transform(handle, t->position, t->rotation);
                synced_pose_[e] = Pose{t->position, t->rotation};
            }
        }

        // 3. Advance the simulation one fixed step.
        backend_->step(dt, substeps_);

        // 4. Write dynamic-body poses back into Transform (static bodies do not move).
        // Record the written pose so the next tick's step 2 can tell a genuine external
        // edit from our own write-back (exact compare: we assign Transform = pose here).
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
            synced_pose_[e] = pose;
        }

        // 5. Emit contact events (mapped to entities) on the world's event bus.
        for (const BodyContact& c : backend_->contact_events())
        {
            const auto ia = handle_to_entity_.find(c.a);
            const auto ib = handle_to_entity_.find(c.b);
            if (ia == handle_to_entity_.end() || ib == handle_to_entity_.end())
            {
                continue;
            }
            world.events().emit<ContactEvent>(ContactEvent{c.phase, ia->second, ib->second, c.point,
                                                           c.normal, c.separation, c.normal_impulse});
        }
    }

    // Closest-hit raycast; returns the hit entity (hit == false on a miss).
    RaycastHit raycast(const mosaic::foundation::math::Vec3& origin,
                       const mosaic::foundation::math::Vec3& direction,
                       float max_distance) const
    {
        const RaycastResult r = backend_->raycast(origin, direction, max_distance);
        RaycastHit          hit;
        if (!r.hit)
        {
            return hit;
        }
        const auto it = handle_to_entity_.find(r.body);
        if (it == handle_to_entity_.end())
        {
            return hit;
        }
        hit.hit = true;
        hit.entity = it->second;
        hit.point = r.point;
        hit.normal = r.normal;
        hit.distance = r.distance;
        return hit;
    }

    // Whether this entity currently has a simulated body. Callers that must report
    // success -- the scripting binding does -- need this, because apply_impulse
    // returns void and no-ops silently for an entity with no body.
    bool has_body(mosaic::foundation::EntityID entity) const
    {
        return bodies_.find(entity) != bodies_.end();
    }

    // Push a dynamic body. `world_point` is where the impulse is applied -- off-center
    // to induce spin. A no-op if the entity has no simulated body (never created, or
    // has no collider component), matching this system's "missing data degrades, does
    // not throw" contract.
    void apply_impulse(mosaic::foundation::EntityID entity,
                       const mosaic::foundation::math::Vec3& impulse,
                       const mosaic::foundation::math::Vec3& world_point)
    {
        const auto it = bodies_.find(entity);
        if (it == bodies_.end())
        {
            return;
        }
        backend_->apply_impulse(it->second, impulse, world_point);
    }

private:
    // Fills `out` from whichever collider-shape component the entity has (box, then sphere,
    // then capsule). Returns false if the entity has no collider-shape component.
    static bool shape_for(World& world, mosaic::foundation::EntityID e, Shape& out)
    {
        if (const Collider* c = world.get_component<Collider>(e))
        {
            out = Shape{ShapeKind::Box, c->half_extents, 0.0f, 0.0f};
            return true;
        }
        if (const SphereCollider* s = world.get_component<SphereCollider>(e))
        {
            out = Shape{ShapeKind::Sphere, {}, s->radius, 0.0f};
            return true;
        }
        if (const CapsuleCollider* c = world.get_component<CapsuleCollider>(e))
        {
            out = Shape{ShapeKind::Capsule, {}, c->radius, c->half_height};
            return true;
        }
        return false;
    }

    // Exact match between an entity's Transform and the pose we last synced. The write-back
    // assigns Transform = pose bit-exactly, so a mismatch means an external edit.
    static bool pose_matches(const transform::Transform& t, const Pose& p)
    {
        return t.position.x == p.position.x && t.position.y == p.position.y &&
               t.position.z == p.position.z && t.rotation.x == p.rotation.x &&
               t.rotation.y == p.rotation.y && t.rotation.z == p.rotation.z &&
               t.rotation.w == p.rotation.w;
    }

    std::unique_ptr<PhysicsBackend>                              backend_;
    std::unordered_map<mosaic::foundation::EntityID, BodyHandle> bodies_;
    std::unordered_map<BodyHandle, mosaic::foundation::EntityID> handle_to_entity_;
    std::unordered_map<mosaic::foundation::EntityID, Pose>       synced_pose_;
    int                                                          substeps_;
};
} // namespace mosaic::engine::physics
