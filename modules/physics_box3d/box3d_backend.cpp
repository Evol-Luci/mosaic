#include "modules/physics_box3d/box3d_backend.hpp"

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

        const Vec3 h = def.shape.half_extents;
        b3BoxHull  hull = b3MakeBoxHull(h.x, h.y, h.z);
        b3ShapeDef sd = b3DefaultShapeDef();
        sd.density = def.density;
        b3CreateHullShape(id, &sd, &hull.base);

        bodies_.push_back(id);
        return static_cast<ep::BodyHandle>(bodies_.size()); // handle == index + 1
    }

    void step(float dt, int substeps) override { b3World_Step(world_, dt, substeps); }

    ep::Pose body_pose(ep::BodyHandle body) const override
    {
        if (body == 0 || body > bodies_.size())
        {
            return ep::Pose{};
        }
        const auto p = b3Body_GetPosition(bodies_[body - 1]); // b3Pos == b3Vec3 {x,y,z}
        // P1 reads position only; a straight-falling box does not rotate. Real rotation
        // read-back (b3Body_GetRotation -> b3Quat{v,s}) arrives in P2.
        return ep::Pose{Vec3{p.x, p.y, p.z}, Quat::identity()};
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
