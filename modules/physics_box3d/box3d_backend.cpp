#include "modules/physics_box3d/box3d_backend.hpp"

#include <cstdint>
#include <vector>

#include <box3d/box3d.h>

#include "foundation/math/quat.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::physics::box3d
{
namespace
{
namespace ep = mosaic::engine::physics;
using mosaic::foundation::math::Quat;
using mosaic::foundation::math::Vec3;

class Box3DBackend final : public ep::PhysicsBackend
{
public:
    explicit Box3DBackend(const ep::WorldDef& def)
    {
        b3WorldDef wd = b3DefaultWorldDef();
        wd.gravity.x = def.gravity.x;
        wd.gravity.y = def.gravity.y;
        wd.gravity.z = def.gravity.z;
        world_ = b3CreateWorld(&wd); // no task callbacks -> single-threaded, deterministic
    }

    ~Box3DBackend() override { b3DestroyWorld(world_); }

    ep::BodyHandle create_body(const ep::BodyDef& def) override
    {
        b3BodyDef bd = b3DefaultBodyDef();
        bd.type = (def.type == ep::BodyType::Dynamic) ? b3_dynamicBody : b3_staticBody;
        bd.position.x = def.position.x;
        bd.position.y = def.position.y;
        bd.position.z = def.position.z;
        bd.rotation.v = b3Vec3{def.rotation.x, def.rotation.y, def.rotation.z};
        bd.rotation.s = def.rotation.w;
        b3BodyId id = b3CreateBody(world_, &bd);

        b3ShapeDef sd = b3DefaultShapeDef();
        sd.density = def.density;
        sd.enableContactEvents = true; // required: touch events are off by default
        switch (def.shape.kind)
        {
        case ep::ShapeKind::Box:
        {
            const Vec3 h = def.shape.half_extents;
            b3BoxHull  hull = b3MakeBoxHull(h.x, h.y, h.z);
            b3CreateHullShape(id, &sd, &hull.base);
            break;
        }
        case ep::ShapeKind::Sphere:
        {
            b3Sphere sphere{};
            sphere.center = b3Vec3{0.0f, 0.0f, 0.0f};
            sphere.radius = def.shape.radius;
            b3CreateSphereShape(id, &sd, &sphere);
            break;
        }
        case ep::ShapeKind::Capsule:
        {
            b3Capsule capsule{};
            capsule.center1 = b3Vec3{0.0f, -def.shape.half_height, 0.0f};
            capsule.center2 = b3Vec3{0.0f, def.shape.half_height, 0.0f};
            capsule.radius = def.shape.radius;
            b3CreateCapsuleShape(id, &sd, &capsule);
            break;
        }
        }

        bodies_.push_back(id);
        const auto handle = static_cast<ep::BodyHandle>(bodies_.size()); // handle == index + 1
        b3Body_SetUserData(id, reinterpret_cast<void*>(static_cast<std::uintptr_t>(handle)));
        return handle;
    }

    void destroy_body(ep::BodyHandle body) override
    {
        const b3BodyId* slot = live_body(body);
        if (slot == nullptr)
        {
            return;
        }
        const b3BodyId id = *slot;         // copy before tombstoning the slot
        bodies_[body - 1] = b3_nullBodyId; // handles are indices; never erase
        b3DestroyBody(id);
    }

    void step(float dt, int substeps) override
    {
        b3World_Step(world_, dt, substeps);
        last_contacts_.clear();
        const b3ContactEvents ev = b3World_GetContactEvents(world_);
        for (int i = 0; i < ev.beginCount; ++i)
        {
            const b3ContactBeginTouchEvent& be = ev.beginEvents[i];
            ep::BodyContact c{ep::ContactPhase::Begin, handle_of(b3Shape_GetBody(be.shapeIdA)),
                              handle_of(b3Shape_GetBody(be.shapeIdB))};
            if (b3Contact_IsValid(be.contactId))
            {
                const b3ContactData cd = b3Contact_GetData(be.contactId);
                if (cd.manifoldCount > 0 && cd.manifolds[0].pointCount > 0)
                {
                    const b3Manifold&      m = cd.manifolds[0];
                    const b3ManifoldPoint& p = m.points[0];
                    const b3Pos            ca = b3Body_GetWorldCenter(b3Shape_GetBody(be.shapeIdA));
                    c.point  = Vec3{static_cast<float>(ca.x) + p.anchorA.x,
                                    static_cast<float>(ca.y) + p.anchorA.y,
                                    static_cast<float>(ca.z) + p.anchorA.z};
                    c.normal = Vec3{m.normal.x, m.normal.y, m.normal.z};
                    c.separation     = p.separation;
                    c.normal_impulse = p.normalImpulse;
                }
            }
            last_contacts_.push_back(c);
        }
        for (int i = 0; i < ev.endCount; ++i)
        {
            const b3ShapeId sa = ev.endEvents[i].shapeIdA;
            const b3ShapeId sb = ev.endEvents[i].shapeIdB;
            if (!b3Shape_IsValid(sa) || !b3Shape_IsValid(sb))
            {
                continue; // a shape was destroyed with its body; skip
            }
            last_contacts_.push_back(ep::BodyContact{
                ep::ContactPhase::End, handle_of(b3Shape_GetBody(sa)),
                handle_of(b3Shape_GetBody(sb))});
        }
    }

    std::vector<ep::BodyContact> contact_events() const override { return last_contacts_; }

    void set_body_transform(ep::BodyHandle body, const Vec3& position,
                            const Quat& rotation) override
    {
        const b3BodyId* slot = live_body(body);
        if (slot == nullptr)
        {
            return;
        }
        const b3BodyId id = *slot;
        b3Pos          pos{};
        pos.x = position.x;
        pos.y = position.y;
        pos.z = position.z;
        b3Quat rot{};
        rot.v = b3Vec3{rotation.x, rotation.y, rotation.z};
        rot.s = rotation.w;
        b3Body_SetTransform(id, pos, rot);
        b3Body_SetLinearVelocity(id, b3Vec3{0.0f, 0.0f, 0.0f});
        b3Body_SetAngularVelocity(id, b3Vec3{0.0f, 0.0f, 0.0f});
        // Wake the body: a rested body sleeps, and a teleport must re-enter the simulation
        // (otherwise it hangs at the new location instead of continuing to simulate).
        b3Body_SetAwake(id, true);
    }

    void apply_impulse(ep::BodyHandle body, const Vec3& impulse, const Vec3& world_point) override
    {
        const b3BodyId* slot = live_body(body);
        if (slot == nullptr)
        {
            return;
        }
        const b3BodyId id = *slot;
        b3Pos          point{};
        point.x = world_point.x;
        point.y = world_point.y;
        point.z = world_point.z;
        // wake=true is load-bearing: Box3D sleeps rested bodies, and a click on a
        // settled object is the common case. With wake=false this call succeeds,
        // reports nothing, and the object does not move.
        b3Body_ApplyLinearImpulse(id, b3Vec3{impulse.x, impulse.y, impulse.z}, point, true);
    }

    ep::RaycastResult raycast(const Vec3& origin, const Vec3& direction,
                              float max_distance) const override
    {
        ep::RaycastResult out;
        if (max_distance <= 0.0f)
        {
            return out;
        }
        const Vec3 d = mosaic::foundation::math::normalize(direction);

        b3Pos o{};
        o.x = origin.x;
        o.y = origin.y;
        o.z = origin.z;
        b3Vec3 translation{d.x * max_distance, d.y * max_distance, d.z * max_distance};

        const b3RayResult r = b3World_CastRayClosest(world_, o, translation, b3DefaultQueryFilter());
        if (!r.hit)
        {
            return out;
        }
        out.hit = true;
        out.point = Vec3{r.point.x, r.point.y, r.point.z};
        out.normal = Vec3{r.normal.x, r.normal.y, r.normal.z};
        out.distance = r.fraction * max_distance;
        const b3BodyId body = b3Shape_GetBody(r.shapeId);
        out.body = static_cast<ep::BodyHandle>(
            reinterpret_cast<std::uintptr_t>(b3Body_GetUserData(body)));
        return out;
    }

    ep::Pose body_pose(ep::BodyHandle body) const override
    {
        const b3BodyId* slot = live_body(body);
        if (slot == nullptr)
        {
            return ep::Pose{};
        }
        const auto   p = b3Body_GetPosition(*slot); // b3Pos == b3Vec3 {x,y,z}
        const b3Quat q = b3Body_GetRotation(*slot); // b3Quat {b3Vec3 v; float s;}
        return ep::Pose{Vec3{p.x, p.y, p.z}, Quat{q.v.x, q.v.y, q.v.z, q.s}};
    }

private:
    static ep::BodyHandle handle_of(b3BodyId id)
    {
        return static_cast<ep::BodyHandle>(
            reinterpret_cast<std::uintptr_t>(b3Body_GetUserData(id)));
    }

    // The b3BodyId for a handle, or nullptr when the handle is out of range or names a
    // body that has been destroyed. Every handle-taking method goes through this. A
    // destroyed slot is tombstoned with b3_nullBodyId rather than erased, because
    // handle == index + 1 and erasing would shift every later handle.
    const b3BodyId* live_body(ep::BodyHandle body) const
    {
        if (body == 0 || body > bodies_.size())
        {
            return nullptr;
        }
        const b3BodyId& id = bodies_[body - 1];
        // b3Body_IsValid returns false when index1 < 1, so this is safe on b3_nullBodyId.
        return b3Body_IsValid(id) ? &id : nullptr;
    }

    b3WorldId                    world_{};
    std::vector<b3BodyId>        bodies_;
    std::vector<ep::BodyContact> last_contacts_;
};
} // namespace

std::unique_ptr<ep::PhysicsBackend> create_box3d_backend(const ep::WorldDef& def)
{
    return std::make_unique<Box3DBackend>(def);
}
} // namespace mosaic::physics::box3d
