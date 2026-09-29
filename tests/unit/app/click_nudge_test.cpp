#include <memory>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "app/run/click_nudge.hpp"
#include "engine/physics/physics_components.hpp"
#include "engine/physics/physics_system.hpp"
#include "engine/render/camera.hpp"
#include "engine/transform/transform.hpp"
#include "engine/transform/transform_system.hpp"
#include "engine/ui/pointer.hpp"
#include "engine/world/world.hpp"
#include "modules/physics_box3d/box3d_backend.hpp"

using Catch::Approx;
using mosaic::app::ClickNudgeSystem;
using mosaic::engine::World;
using mosaic::engine::physics::Collider;
using mosaic::engine::physics::PhysicsSystem;
using mosaic::engine::physics::RigidBody;
using mosaic::engine::physics::SphereCollider;
using mosaic::engine::render::Camera;
using mosaic::engine::transform::Transform;
using mosaic::engine::ui::PointerState;
using mosaic::foundation::math::Quat;
using mosaic::foundation::math::Vec3;

namespace
{
// A ball at the origin with a camera looking at it from +Z, so a click at the centre
// of the screen (ndc 0,0) casts a ray straight down -Z into the ball.
mosaic::foundation::EntityID build_scene(World& world)
{
    // TransformSystem FIRST: it derives WorldTransform from Transform, and
    // studio::pick_entity reads WorldTransform to find the camera. Without it there is
    // no camera as far as the picker is concerned and every nudge test fails for the
    // wrong reason. Systems tick in registration order, so this must precede the rest.
    world.add_system<mosaic::engine::transform::TransformSystem>();
    world.add_system<PhysicsSystem>(
        mosaic::physics::box3d::create_box3d_backend(mosaic::engine::physics::WorldDef{}));

    auto cam = world.create_entity();
    world.add_component<Transform>(cam,
                                   Transform{Vec3{0, 0, 6}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<Camera>(cam, Camera{});

    auto ball = world.create_entity();
    world.add_component<Transform>(ball,
                                   Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(ball, RigidBody{true, 1.0f});
    world.add_component<SphereCollider>(ball, SphereCollider{0.5f});
    return ball;
}
} // namespace

TEST_CASE("ClickNudgeSystem pushes the clicked body once per press", "[app]")
{
    World world;
    auto  ball = build_scene(world);

    PointerState p;
    p.x = 0.5f; // screen centre
    p.y = 0.5f;
    p.down = false;
    world.add_system<ClickNudgeSystem>([&p] { return p; }, [] { return 1.0f; }, 4.0f);

    world.tick(1.0f / 60.0f); // body created, no click yet
    const float x0 = world.get_component<Transform>(ball)->position.x;
    const float z0 = world.get_component<Transform>(ball)->position.z;

    p.down = true;
    world.tick(1.0f / 60.0f); // press edge -> one impulse
    for (int i = 0; i < 20; ++i) { world.tick(1.0f / 60.0f); } // still held
    const float z_after_hold = world.get_component<Transform>(ball)->position.z;

    // Pushed away from the camera, i.e. along -Z.
    REQUIRE(z_after_hold < z0 - 0.05f);
    // And not sideways: the click was dead centre.
    REQUIRE(world.get_component<Transform>(ball)->position.x == Approx(x0).margin(0.05f));
}

TEST_CASE("ClickNudgeSystem does not re-fire while the button is held", "[app]")
{
    World world;
    auto  ball = build_scene(world);

    PointerState p;
    p.x = 0.5f;
    p.y = 0.5f;
    p.down = false;
    world.add_system<ClickNudgeSystem>([&p] { return p; }, [] { return 1.0f; }, 4.0f);
    world.tick(1.0f / 60.0f);
    const float z0 = world.get_component<Transform>(ball)->position.z;

    p.down = true;
    for (int i = 0; i < 60; ++i) { world.tick(1.0f / 60.0f); } // pressed, then held 1s
    const float z_held = world.get_component<Transform>(ball)->position.z;

    // One impulse of 4 N*s on a ~0.52 kg ball is ~7.7 m/s, so in one second it travels
    // on the order of metres. Re-firing every frame for 60 frames would multiply that
    // by ~60 and put it hundreds of metres away. The bound below passes comfortably for
    // a single impulse and fails unmistakably for a repeating one.
    REQUIRE(z_held < z0 - 0.05f);   // it did move
    REQUIRE(z_held > z0 - 30.0f);   // but nothing like 60 impulses' worth
}

TEST_CASE("ClickNudgeSystem ignores a press the UI already claimed", "[app]")
{
    World world;
    auto  ball = build_scene(world);

    PointerState p;
    p.x = 0.5f;
    p.y = 0.5f;
    p.down = false;
    // ui_claimed returns true: the UI consumed this press.
    world.add_system<ClickNudgeSystem>([&p] { return p; }, [] { return 1.0f; }, 4.0f,
                                       [] { return true; });
    world.tick(1.0f / 60.0f);
    const float z0 = world.get_component<Transform>(ball)->position.z;

    p.down = true;
    for (int i = 0; i < 20; ++i) { world.tick(1.0f / 60.0f); }

    REQUIRE(world.get_component<Transform>(ball)->position.z == Approx(z0).margin(1e-3));
}

TEST_CASE("ClickNudgeSystem does nothing when the click hits empty space", "[app]")
{
    // Review Focus 5: clicking the sky must be inert and must leave no pending state.
    World world;
    auto  ball = build_scene(world);

    PointerState p;
    p.x = 0.02f; // far corner, ray misses the ball entirely
    p.y = 0.02f;
    p.down = false;
    world.add_system<ClickNudgeSystem>([&p] { return p; }, [] { return 1.0f; }, 4.0f);
    world.tick(1.0f / 60.0f);
    const float z0 = world.get_component<Transform>(ball)->position.z;

    p.down = true;
    for (int i = 0; i < 20; ++i) { world.tick(1.0f / 60.0f); }

    REQUIRE(world.get_component<Transform>(ball)->position.z == Approx(z0).margin(1e-3));
}

TEST_CASE("ClickNudgeSystem survives a world with no physics system and no camera", "[app]")
{
    // Review Focus 3: pick_entity returns a miss for both, but the nudge must handle
    // the miss rather than assume a hit.
    World world; // deliberately empty: no PhysicsSystem, no Camera
    PointerState p;
    p.x = 0.5f;
    p.y = 0.5f;
    p.down = true;
    world.add_system<ClickNudgeSystem>([&p] { return p; }, [] { return 1.0f; }, 4.0f);

    REQUIRE_NOTHROW(world.tick(1.0f / 60.0f));
    REQUIRE_NOTHROW(world.tick(1.0f / 60.0f));
}

TEST_CASE("ClickNudgeSystem maps normalized pointer coordinates to NDC with a Y flip", "[app]")
{
    // PointerState is [0,1] top-left origin; pick_entity wants [-1,1] with +y up.
    // Dropping the flip yields a picker that works on the vertically mirrored object.
    REQUIRE(mosaic::app::pointer_to_ndc_x(0.5f) == Approx(0.0f));
    REQUIRE(mosaic::app::pointer_to_ndc_x(1.0f) == Approx(1.0f));
    REQUIRE(mosaic::app::pointer_to_ndc_x(0.0f) == Approx(-1.0f));
    REQUIRE(mosaic::app::pointer_to_ndc_y(0.0f) == Approx(1.0f));  // top of screen = +1
    REQUIRE(mosaic::app::pointer_to_ndc_y(1.0f) == Approx(-1.0f)); // bottom = -1
    REQUIRE(mosaic::app::pointer_to_ndc_y(0.5f) == Approx(0.0f));
}
