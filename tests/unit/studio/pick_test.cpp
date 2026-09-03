#include <catch2/catch_test_macros.hpp>

#include "engine/physics/physics_components.hpp"
#include "engine/physics/physics_system.hpp"
#include "engine/render/camera.hpp"
#include "engine/transform/transform.hpp"
#include "engine/transform/transform_system.hpp"
#include "engine/world/world.hpp"
#include "modules/physics_box3d/box3d_backend.hpp"
#include "studio/pick.hpp"

using mosaic::engine::World;
using mosaic::engine::physics::Collider;
using mosaic::engine::physics::PhysicsSystem;
using mosaic::engine::physics::RigidBody;
using mosaic::engine::physics::WorldDef;
using mosaic::engine::render::Camera;
using mosaic::engine::transform::Transform;
using mosaic::engine::transform::TransformSystem;
using mosaic::foundation::math::Quat;
using mosaic::foundation::math::Vec3;
using mosaic::physics::box3d::create_box3d_backend;
using mosaic::studio::pick_entity;

namespace
{
// camera at (0,0,5) looking -Z; a static box at the origin. Returns the box entity.
mosaic::foundation::EntityID setup(World& world)
{
    world.add_system<TransformSystem>();
    world.add_system<PhysicsSystem>(create_box3d_backend(WorldDef{}));

    const auto cam = world.create_entity();
    world.add_component<Transform>(cam, Transform{Vec3{0, 0, 5}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<Camera>(cam, Camera{});

    const auto box = world.create_entity();
    world.add_component<Transform>(box, Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(box, RigidBody{false, 1.0f}); // static
    world.add_component<Collider>(box, Collider{Vec3{0.5f, 0.5f, 0.5f}});

    world.tick(1.0f / 60.0f); // derive WorldTransform + register the body
    return box;
}
} // namespace

TEST_CASE("mouse-pick hits the object under a center click", "[studio]")
{
    World world;
    const auto box = setup(world);
    const auto hit = pick_entity(world, 1.0f, 0.0f, 0.0f);
    REQUIRE(hit.hit);
    REQUIRE(hit.entity == box);
}

TEST_CASE("mouse-pick misses on a click into empty space", "[studio]")
{
    World world;
    setup(world);
    const auto hit = pick_entity(world, 1.0f, 0.95f, 0.95f); // corner ray clears the small box
    REQUIRE_FALSE(hit.hit);
}

TEST_CASE("mouse-pick with no camera is a miss", "[studio]")
{
    World world;
    world.add_system<PhysicsSystem>(create_box3d_backend(WorldDef{}));
    const auto hit = pick_entity(world, 1.0f, 0.0f, 0.0f);
    REQUIRE_FALSE(hit.hit);
}

TEST_CASE("World::get_system returns a registered system or nullptr", "[world]")
{
    World world;
    REQUIRE(world.get_system<PhysicsSystem>() == nullptr);
    world.add_system<PhysicsSystem>(create_box3d_backend(WorldDef{}));
    REQUIRE(world.get_system<PhysicsSystem>() != nullptr);
    REQUIRE(world.get_system<TransformSystem>() == nullptr);
}
