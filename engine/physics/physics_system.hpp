#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

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

    // Reconcile the physics world with the ECS WITHOUT advancing the simulation: reap
    // bodies whose entity is gone, create bodies for new physics entities, and push
    // external Transform edits into the backend.
    //
    // update() runs this as its first phase. The EDITOR needs it on its own, because while
    // stopped `RuntimeLoop::advance` returns without ticking, so update() never runs -- and
    // bodies are created lazily in here. A freshly opened editor therefore had NO bodies at
    // all: click-to-select raycasts this world (studio/pick.hpp), so every viewport click
    // cleared the selection instead of making one, and inspector edits to a RigidBody's
    // Transform never reached the backend either.
    //
    // Deliberately NOT reachable as `update(world, 0.0f)`. That still calls
    // backend_->step(0, substeps_) and then harvests contact events, so a stopped editor
    // would emit contact-begin events for anything already touching. Reconciling and
    // stepping are different operations, and keeping them separable is what makes an
    // authoring-time sync safe.
    void sync_to_world(World& world)
    {
        const std::lock_guard<std::mutex> lock(backend_mutex_);
        reconcile_locked(world);
    }

    void update(World& world, float dt) override
    {
        // Contacts are mapped under the lock but emitted outside it. A synchronous
        // ContactEvent subscriber runs on this stack, and a subscriber that calls
        // raycast()/has_body()/apply_impulse() would self-deadlock on a plain std::mutex if
        // the lock were still held. Holding it across subscriber dispatch also ordered every
        // lock a subscriber takes -- the global logger mutex among them -- underneath this
        // one. The braced block below is what keeps that from coming back: the locked region
        // is delimited lexically, so a call-out cannot be added inside it unnoticed.
        std::vector<ContactEvent> contacts;
        {
            const std::lock_guard<std::mutex> lock(backend_mutex_);
            reconcile_locked(world);

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

            // 5a. Map backend contacts to entity-level events. Still inside the lock: this
            // reads handle_to_entity_, and contacts whose handle is unmapped are dropped.
            for (const BodyContact& c : backend_->contact_events())
            {
                const auto ia = handle_to_entity_.find(c.a);
                const auto ib = handle_to_entity_.find(c.b);
                if (ia == handle_to_entity_.end() || ib == handle_to_entity_.end())
                {
                    continue;
                }
                contacts.push_back(ContactEvent{c.phase, ia->second, ib->second, c.point, c.normal,
                                                c.separation, c.normal_impulse});
            }
        }

        // 5b. Outside the lock: subscribers may now call back into this system.
        for (const ContactEvent& e : contacts)
        {
            world.events().emit<ContactEvent>(e);
        }
    }

    // Closest-hit raycast; returns the hit entity (hit == false on a miss).
    RaycastHit raycast(const mosaic::foundation::math::Vec3& origin,
                       const mosaic::foundation::math::Vec3& direction,
                       float max_distance) const
    {
        const std::lock_guard<std::mutex> lock(backend_mutex_);
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
        const std::lock_guard<std::mutex> lock(backend_mutex_);
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
        const std::lock_guard<std::mutex> lock(backend_mutex_);
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

    // Steps 0-2 of a tick: everything that makes the backend agree with the ECS, and
    // nothing that advances time. Caller holds backend_mutex_.
    void reconcile_locked(World& world)
    {
        // 0. Reap bodies whose entity is gone. destroy_entity() is deferred to the end
        // of tick() and on_destroy() erases every component, so an entry whose
        // RigidBody has vanished belongs to a destroyed entity -- or to a living one
        // someone removed RigidBody from, which should also stop simulating. Either way
        // the body must go: left alone it keeps stepping and keeps colliding, an
        // invisible obstacle with no mesh to explain it.
        //
        // handle_to_entity_ is erased here too, and that matters within THIS tick, not
        // across ticks: the Box3D backend clears its contact buffer at the top of every
        // step(), but b3DestroyBody pushes an end-touch event for the destroyed body's
        // shapes, which step 3 then surfaces. The backend filters those on shape
        // validity first; this erase is the second line of defence, so step 5a cannot
        // map a destroyed body's handle back to an entity.
        for (auto it = bodies_.begin(); it != bodies_.end();)
        {
            if (world.get_component<RigidBody>(it->first) != nullptr)
            {
                ++it;
                continue;
            }
            backend_->destroy_body(it->second);
            handle_to_entity_.erase(it->second);
            synced_pose_.erase(it->first);
            it = bodies_.erase(it);
        }

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

    // Serializes every entry point that touches backend_ or the handle maps. Scripts
    // can run on a worker thread (--script-thread), and a raycast from there would
    // otherwise run concurrently with the main thread's update() -- reading a Box3D
    // world mid-step, and reading bodies_/handle_to_entity_ mid-insert.
    //
    // In update() it is held across steps 0 to 5a as one braced block -- every access to
    // backend_ and to the three maps, and no more. Contact emission runs *after* that block
    // closes, deliberately: a synchronous subscriber runs on update()'s stack, and one that
    // called raycast()/has_body()/apply_impulse() would self-deadlock here. Do not widen the
    // block to cover the emit again, and do not replace it with a manual unlock -- the brace
    // is what stops a future call-out being added inside the locked region unnoticed.
    mutable std::mutex backend_mutex_;

    std::unique_ptr<PhysicsBackend>                              backend_;
    std::unordered_map<mosaic::foundation::EntityID, BodyHandle> bodies_;
    std::unordered_map<BodyHandle, mosaic::foundation::EntityID> handle_to_entity_;
    std::unordered_map<mosaic::foundation::EntityID, Pose>       synced_pose_;
    int                                                          substeps_;
};
} // namespace mosaic::engine::physics
