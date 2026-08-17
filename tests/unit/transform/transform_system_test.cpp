#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/transform/transform.hpp"
#include "engine/transform/transform_system.hpp"
#include "engine/world/world.hpp"
#include "foundation/math/quat.hpp"
#include "foundation/math/vec3.hpp"

using Catch::Approx;
using mosaic::engine::World;
using mosaic::engine::transform::Transform;
using mosaic::engine::transform::TransformSystem;
using mosaic::engine::transform::WorldTransform;
using mosaic::foundation::math::from_axis_angle;
using mosaic::foundation::math::transform_point;
using mosaic::foundation::math::Vec3;
using EntityID = World::EntityID;

TEST_CASE("single entity local transform becomes world transform", "[transform]")
{
    World world;
    world.add_system<TransformSystem>();
    const EntityID e = world.create_entity();
    Transform t;
    t.position = Vec3{1.0f, 2.0f, 3.0f};
    world.add_component<Transform>(e, t);

    world.tick(0.0f);

    const WorldTransform* wt = world.get_component<WorldTransform>(e);
    REQUIRE(wt != nullptr);
    const Vec3 p = transform_point(wt->matrix, Vec3{0.0f, 0.0f, 0.0f});
    REQUIRE(p.x == Approx(1.0f));
    REQUIRE(p.y == Approx(2.0f));
    REQUIRE(p.z == Approx(3.0f));
}

TEST_CASE("child world transform composes with its parent", "[transform]")
{
    World world;
    world.add_system<TransformSystem>();
    const EntityID parent = world.create_entity();
    Transform pt;
    pt.position = Vec3{10.0f, 0.0f, 0.0f};
    world.add_component<Transform>(parent, pt);

    const EntityID child = world.create_entity();
    Transform ct;
    ct.position = Vec3{1.0f, 0.0f, 0.0f};
    ct.parent = parent;
    world.add_component<Transform>(child, ct);

    world.tick(0.0f);

    const WorldTransform* cw = world.get_component<WorldTransform>(child);
    REQUIRE(cw != nullptr);
    const Vec3 p = transform_point(cw->matrix, Vec3{0.0f, 0.0f, 0.0f});
    REQUIRE(p.x == Approx(11.0f));
    REQUIRE(p.y == Approx(0.0f).margin(1e-5));
    REQUIRE(p.z == Approx(0.0f).margin(1e-5));
}

TEST_CASE("a rotated parent rotates the child offset", "[transform]")
{
    const float pi = 3.14159265358979323846f;
    World world;
    world.add_system<TransformSystem>();
    const EntityID parent = world.create_entity();
    Transform pt;
    pt.rotation = from_axis_angle(Vec3{0.0f, 0.0f, 1.0f}, pi / 2.0f); // 90 deg about Z
    world.add_component<Transform>(parent, pt);

    const EntityID child = world.create_entity();
    Transform ct;
    ct.position = Vec3{1.0f, 0.0f, 0.0f};
    ct.parent = parent;
    world.add_component<Transform>(child, ct);

    world.tick(0.0f);

    const Vec3 p = transform_point(world.get_component<WorldTransform>(child)->matrix,
                                   Vec3{0.0f, 0.0f, 0.0f});
    REQUIRE(p.x == Approx(0.0f).margin(1e-5));
    REQUIRE(p.y == Approx(1.0f).margin(1e-5));
    REQUIRE(p.z == Approx(0.0f).margin(1e-5));
}

TEST_CASE("moving a parent updates its children on the next tick", "[transform]")
{
    World world;
    world.add_system<TransformSystem>();
    const EntityID parent = world.create_entity();
    world.add_component<Transform>(parent, Transform{}); // origin

    const EntityID child = world.create_entity();
    Transform ct;
    ct.position = Vec3{1.0f, 0.0f, 0.0f};
    ct.parent = parent;
    world.add_component<Transform>(child, ct);

    world.tick(0.0f);
    REQUIRE(transform_point(world.get_component<WorldTransform>(child)->matrix,
                            Vec3{0.0f, 0.0f, 0.0f})
                .x == Approx(1.0f));

    world.get_component<Transform>(parent)->position = Vec3{5.0f, 0.0f, 0.0f};
    world.tick(0.0f);
    REQUIRE(transform_point(world.get_component<WorldTransform>(child)->matrix,
                            Vec3{0.0f, 0.0f, 0.0f})
                .x == Approx(6.0f));
}
