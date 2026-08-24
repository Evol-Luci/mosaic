#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "engine/physics/physics_backend.hpp"
#include "modules/physics_box3d/box3d_backend.hpp"

using mosaic::engine::physics::BodyDef;
using mosaic::engine::physics::BodyHandle;
using mosaic::engine::physics::BodyType;
using mosaic::engine::physics::WorldDef;
using mosaic::foundation::math::Vec3;
using mosaic::physics::box3d::create_box3d_backend;

TEST_CASE("a dynamic body falls under gravity", "[physics]")
{
    auto backend = create_box3d_backend(WorldDef{});
    REQUIRE(backend != nullptr);

    BodyDef box;
    box.type = BodyType::Dynamic;
    box.position = Vec3{0.0f, 4.0f, 0.0f};
    const BodyHandle handle = backend->create_body(box);
    REQUIRE(handle != 0);

    const float y0 = backend->body_pose(handle).position.y;
    for (int i = 0; i < 10; ++i)
    {
        backend->step(1.0f / 60.0f, 4);
    }
    const float y1 = backend->body_pose(handle).position.y;
    REQUIRE(y1 < y0); // gravity pulled it down
}
