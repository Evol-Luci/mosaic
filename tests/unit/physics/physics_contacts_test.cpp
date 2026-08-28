#include <catch2/catch_test_macros.hpp>

#include "engine/physics/physics_backend.hpp"
#include "modules/physics_box3d/box3d_backend.hpp"

using mosaic::engine::physics::BodyDef;
using mosaic::engine::physics::BodyHandle;
using mosaic::engine::physics::BodyType;
using mosaic::engine::physics::ContactPhase;
using mosaic::engine::physics::WorldDef;
using mosaic::foundation::math::Vec3;
using mosaic::physics::box3d::create_box3d_backend;

namespace
{
bool is_pair(BodyHandle a, BodyHandle b, BodyHandle x, BodyHandle y)
{
    return (a == x && b == y) || (a == y && b == x);
}
} // namespace

TEST_CASE("backend reports a begin-touch contact when a box lands on the ground", "[physics]")
{
    auto backend = create_box3d_backend(WorldDef{});

    BodyDef ground;
    ground.type = BodyType::Static;
    ground.position = Vec3{0, 0, 0};
    ground.shape.half_extents = Vec3{5, 0.5f, 5};
    const BodyHandle gh = backend->create_body(ground);

    BodyDef box;
    box.type = BodyType::Dynamic;
    box.position = Vec3{0, 3, 0};
    box.shape.half_extents = Vec3{0.5f, 0.5f, 0.5f};
    const BodyHandle bh = backend->create_body(box);

    bool begin_seen = false;
    for (int i = 0; i < 180; ++i)
    {
        backend->step(1.0f / 60.0f, 4);
        for (const auto& c : backend->contact_events())
        {
            if (c.phase == ContactPhase::Begin && is_pair(c.a, c.b, gh, bh))
            {
                begin_seen = true;
            }
        }
    }
    REQUIRE(begin_seen);
}
