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
        b3BodyId id = b3CreateBody(world_, &bd);

        b3ShapeDef sd = b3DefaultShapeDef();
        sd.density = def.density;
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

    void step(float dt, int substeps) override { b3World_Step(world_, dt, substeps); }

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
        if (body == 0 || body > bodies_.size())
        {
            return ep::Pose{};
        }
        const auto   p = b3Body_GetPosition(bodies_[body - 1]); // b3Pos == b3Vec3 {x,y,z}
        const b3Quat q = b3Body_GetRotation(bodies_[body - 1]); // b3Quat {b3Vec3 v; float s;}
        return ep::Pose{Vec3{p.x, p.y, p.z}, Quat{q.v.x, q.v.y, q.v.z, q.s}};
    }

private:
    b3WorldId             world_{};
    std::vector<b3BodyId> bodies_;
};
} // namespace

std::unique_ptr<ep::PhysicsBackend> create_box3d_backend(const ep::WorldDef& def)
{
    return std::make_unique<Box3DBackend>(def);
}
} // namespace mosaic::physics::box3d
