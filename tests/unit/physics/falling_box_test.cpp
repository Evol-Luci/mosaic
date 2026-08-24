#include <cmath>
#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "engine/physics/physics_backend.hpp"
#include "modules/physics_box3d/box3d_backend.hpp"

using mosaic::engine::physics::BodyDef;
using mosaic::engine::physics::BodyHandle;
using mosaic::engine::physics::BodyType;
using mosaic::engine::physics::ShapeKind;
using mosaic::engine::physics::WorldDef;
using mosaic::foundation::math::Vec3;
using mosaic::physics::box3d::create_box3d_backend;

TEST_CASE("a sphere body falls under gravity", "[physics]")
{
    auto    backend = create_box3d_backend(WorldDef{});
    BodyDef d;
    d.type = BodyType::Dynamic;
    d.position = Vec3{0.0f, 4.0f, 0.0f};
    d.shape.kind = ShapeKind::Sphere;
    d.shape.radius = 0.5f;
    const BodyHandle h = backend->create_body(d);
    const float      y0 = backend->body_pose(h).position.y;
    for (int i = 0; i < 10; ++i) { backend->step(1.0f / 60.0f, 4); }
    REQUIRE(backend->body_pose(h).position.y < y0);
}

TEST_CASE("a capsule body falls under gravity", "[physics]")
{
    auto    backend = create_box3d_backend(WorldDef{});
    BodyDef d;
    d.type = BodyType::Dynamic;
    d.position = Vec3{0.0f, 4.0f, 0.0f};
    d.shape.kind = ShapeKind::Capsule;
    d.shape.radius = 0.5f;
    d.shape.half_height = 0.5f;
    const BodyHandle h = backend->create_body(d);
    const float      y0 = backend->body_pose(h).position.y;
    for (int i = 0; i < 10; ++i) { backend->step(1.0f / 60.0f, 4); }
    REQUIRE(backend->body_pose(h).position.y < y0);
}

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

namespace
{
// Drop a unit box (half-extent 0.5) from y=4 onto a static ground whose top surface is at
// y=0, step 90x at 1/60s with 4 substeps, and return the final y. If last_delta is given,
// it receives |y(last) - y(prev)| so a caller can assert the box has settled.
float run_drop(float* last_delta = nullptr)
{
    using namespace mosaic::engine::physics;
    auto backend = create_box3d_backend(WorldDef{});

    BodyDef ground;
    ground.type = BodyType::Static;
    ground.position = Vec3{0.0f, -10.0f, 0.0f};
    ground.shape.half_extents = Vec3{50.0f, 10.0f, 50.0f};
    backend->create_body(ground);

    BodyDef box;
    box.type = BodyType::Dynamic;
    box.position = Vec3{0.0f, 4.0f, 0.0f};
    box.shape.half_extents = Vec3{0.5f, 0.5f, 0.5f};
    const BodyHandle handle = backend->create_body(box);

    float y = box.position.y;
    float prev = y;
    for (int i = 0; i < 90; ++i)
    {
        prev = y;
        backend->step(1.0f / 60.0f, 4);
        y = backend->body_pose(handle).position.y;
    }
    if (last_delta != nullptr)
    {
        *last_delta = std::fabs(y - prev);
    }
    return y;
}
} // namespace

TEST_CASE("a dropped box rests on the ground", "[physics]")
{
    float       delta = 1.0f;
    const float y = run_drop(&delta);
    REQUIRE(y > 0.45f); // rest center ~= ground top (0) + half-extent (0.5), minus solver slop
    REQUIRE(y < 0.55f);
    REQUIRE(delta < 1e-3f); // settled: no longer falling
}

TEST_CASE("physics stepping is deterministic across runs", "[physics]")
{
    REQUIRE(run_drop() == run_drop()); // identical inputs -> bit-identical output
}
