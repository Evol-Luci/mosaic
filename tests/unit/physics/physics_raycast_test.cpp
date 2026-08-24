#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/physics/physics_backend.hpp"
#include "modules/physics_box3d/box3d_backend.hpp"

using Catch::Approx;
using mosaic::engine::physics::BodyDef;
using mosaic::engine::physics::BodyHandle;
using mosaic::engine::physics::BodyType;
using mosaic::engine::physics::RaycastResult;
using mosaic::engine::physics::WorldDef;
using mosaic::foundation::math::Vec3;
using mosaic::physics::box3d::create_box3d_backend;

namespace
{
BodyHandle add_static_box(mosaic::engine::physics::PhysicsBackend& b, Vec3 pos, Vec3 half)
{
    BodyDef d;
    d.type = BodyType::Static;
    d.position = pos;
    d.shape.half_extents = half;
    return b.create_body(d);
}
} // namespace

TEST_CASE("backend raycast hits a box and reports point/normal/distance/body", "[physics]")
{
    auto             backend = create_box3d_backend(WorldDef{});
    const BodyHandle ground = add_static_box(*backend, Vec3{0, 0, 0}, Vec3{5, 0.5f, 5}); // top y=0.5

    const RaycastResult r = backend->raycast(Vec3{0, 5, 0}, Vec3{0, -1, 0}, 10.0f);
    REQUIRE(r.hit);
    REQUIRE(r.body == ground);
    REQUIRE(r.distance == Approx(4.5f).margin(0.05f)); // 5 - 0.5
    REQUIRE(r.point.y == Approx(0.5f).margin(0.05f));
    REQUIRE(r.normal.y > 0.5f); // upward-facing top surface
}

TEST_CASE("backend raycast misses empty space", "[physics]")
{
    auto backend = create_box3d_backend(WorldDef{});
    add_static_box(*backend, Vec3{0, 0, 0}, Vec3{5, 0.5f, 5});

    const RaycastResult r = backend->raycast(Vec3{100, 5, 0}, Vec3{0, -1, 0}, 10.0f);
    REQUIRE_FALSE(r.hit);
}

TEST_CASE("backend raycast returns the nearest of two boxes", "[physics]")
{
    auto             backend = create_box3d_backend(WorldDef{});
    add_static_box(*backend, Vec3{0, 1, 0}, Vec3{0.5f, 0.5f, 0.5f});               // lower
    const BodyHandle high = add_static_box(*backend, Vec3{0, 3, 0}, Vec3{0.5f, 0.5f, 0.5f}); // upper

    const RaycastResult r = backend->raycast(Vec3{0, 5, 0}, Vec3{0, -1, 0}, 10.0f);
    REQUIRE(r.hit);
    REQUIRE(r.body == high);                            // nearest (top at y=3.5)
    REQUIRE(r.distance == Approx(1.5f).margin(0.05f));  // 5 - 3.5
}

TEST_CASE("backend raycast is deterministic", "[physics]")
{
    auto backend = create_box3d_backend(WorldDef{});
    add_static_box(*backend, Vec3{0, 0, 0}, Vec3{5, 0.5f, 5});
    REQUIRE(backend->raycast(Vec3{0, 5, 0}, Vec3{0, -1, 0}, 10.0f).distance ==
            backend->raycast(Vec3{0, 5, 0}, Vec3{0, -1, 0}, 10.0f).distance);
}

#include "engine/physics/physics_components.hpp"
#include "engine/physics/physics_system.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"

using mosaic::engine::World;
using mosaic::engine::physics::Collider;
using mosaic::engine::physics::PhysicsSystem;
using mosaic::engine::physics::RigidBody;
using mosaic::engine::transform::Transform;
using mosaic::foundation::math::Quat;

TEST_CASE("PhysicsSystem::raycast returns the hit entity", "[physics]")
{
    World world;
    auto& sys = world.add_system<PhysicsSystem>(create_box3d_backend(WorldDef{}));

    const auto ground = world.create_entity();
    world.add_component<Transform>(ground, Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{10, 1, 10}, {}});
    world.add_component<RigidBody>(ground, RigidBody{false, 1.0f});
    world.add_component<Collider>(ground, Collider{Vec3{5.0f, 0.5f, 5.0f}}); // top y=0.5

    world.tick(1.0f / 60.0f); // creates the backend body + populates the handle->entity map

    const auto hit = sys.raycast(Vec3{0, 5, 0}, Vec3{0, -1, 0}, 10.0f);
    REQUIRE(hit.hit);
    REQUIRE(hit.entity == ground);
    REQUIRE(hit.distance == Approx(4.5f).margin(0.05f));

    const auto miss = sys.raycast(Vec3{100, 5, 0}, Vec3{0, -1, 0}, 10.0f);
    REQUIRE_FALSE(miss.hit);
}
