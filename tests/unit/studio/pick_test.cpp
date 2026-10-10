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

// The same scene, WITHOUT ticking -- which is what a stopped editor is. Returns the box.
//
// Every other case in this file ticks first, and the comment above says exactly what that
// buys: the tick is what registers the backend body. So no test here could see what a
// freshly opened editor sees, and that is why the regression shipped.
mosaic::foundation::EntityID setup_unticked(World& world)
{
    world.add_system<TransformSystem>();
    world.add_system<PhysicsSystem>(create_box3d_backend(WorldDef{}));

    const auto cam = world.create_entity();
    world.add_component<Transform>(cam, Transform{Vec3{0, 0, 5}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<Camera>(cam, Camera{});

    const auto box = world.create_entity();
    world.add_component<Transform>(box, Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(box, RigidBody{false, 1.0f});
    world.add_component<Collider>(box, Collider{Vec3{0.5f, 0.5f, 0.5f}});
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

TEST_CASE("mouse-pick works on a stopped editor frame that never ticked", "[studio]")
{
    // THE regression. `mosaic editor` starts stopped, so RuntimeLoop::advance returns
    // without ticking and PhysicsSystem::update never runs -- and bodies are created lazily
    // inside update(). The physics world therefore had NO bodies on a freshly opened
    // editor, pick_entity raycast an empty world, and every viewport click cleared the
    // selection instead of making one.
    //
    // This models exactly what the editor does on a stopped frame: refresh derived
    // transforms, reconcile physics with the ECS, and nothing else.
    World world;
    const auto box = setup_unticked(world);

    world.get_system<TransformSystem>()->update(world, 0.0f);
    world.get_system<PhysicsSystem>()->sync_to_world(world);

    const auto hit = pick_entity(world, 1.0f, 0.0f, 0.0f);
    REQUIRE(hit.hit);
    REQUIRE(hit.entity == box);
}

TEST_CASE("syncing physics without stepping emits no contact events", "[studio]")
{
    // Why the fix is a separate entry point rather than `update(world, 0.0f)`: that still
    // reaches backend_->step(0, substeps) and then harvests contact events, so a STOPPED
    // editor would emit contact-begin events for anything already touching -- which the
    // demo logs. Two boxes are placed overlapping so there is a contact to find.
    World world;
    world.add_system<TransformSystem>();
    world.add_system<PhysicsSystem>(create_box3d_backend(WorldDef{}));

    for (int i = 0; i < 2; ++i)
    {
        const auto e = world.create_entity();
        world.add_component<Transform>(
            e, Transform{Vec3{0.1f * static_cast<float>(i), 0, 0}, Quat::identity(),
                         Vec3{1, 1, 1}, {}});
        world.add_component<RigidBody>(e, RigidBody{true, 1.0f}); // dynamic, so they touch
        world.add_component<Collider>(e, Collider{Vec3{0.5f, 0.5f, 0.5f}});
    }

    int contacts = 0;
    world.events().subscribe<mosaic::engine::physics::ContactEvent>(
        [&contacts](const mosaic::engine::physics::ContactEvent&) { ++contacts; });

    auto* physics = world.get_system<PhysicsSystem>();
    physics->sync_to_world(world);
    physics->sync_to_world(world); // twice: the bodies exist by now and are overlapping
    REQUIRE(contacts == 0);
}
