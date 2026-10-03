#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/physics/physics_backend.hpp"
#include "engine/physics/physics_components.hpp"
#include "engine/physics/physics_system.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "modules/physics_box3d/box3d_backend.hpp"

using Catch::Approx;
using mosaic::engine::World;
using mosaic::engine::physics::BodyDef;
using mosaic::engine::physics::BodyHandle;
using mosaic::engine::physics::Collider;
using mosaic::engine::physics::PhysicsBackend;
using mosaic::engine::physics::PhysicsSystem;
using mosaic::engine::physics::Pose;
using mosaic::engine::physics::RigidBody;
using mosaic::engine::transform::Transform;
using mosaic::foundation::math::Quat;
using mosaic::foundation::math::Vec3;

namespace
{
// Records create_body calls + step count; returns a fixed pose from body_pose.
class FakeBackend : public PhysicsBackend
{
public:
    std::vector<BodyDef> created;
    int                  steps = 0;
    Pose                 fixed{Vec3{7.0f, 8.0f, 9.0f}, Quat::identity()};

    struct ImpulseCall { BodyHandle body; Vec3 impulse; Vec3 point; };
    std::vector<ImpulseCall> impulses;
    std::vector<BodyHandle>  destroyed;

    void apply_impulse(BodyHandle body, const mosaic::foundation::math::Vec3& impulse,
                       const mosaic::foundation::math::Vec3& world_point) override
    {
        impulses.push_back({body, impulse, world_point});
    }

    BodyHandle create_body(const BodyDef& def) override
    {
        created.push_back(def);
        return static_cast<BodyHandle>(created.size());
    }
    void destroy_body(BodyHandle body) override { destroyed.push_back(body); }
    void step(float, int) override { ++steps; }
    Pose body_pose(BodyHandle) const override { return fixed; }
    void set_body_transform(BodyHandle, const mosaic::foundation::math::Vec3&,
                            const mosaic::foundation::math::Quat&) override
    {
    }
    mosaic::engine::physics::RaycastResult raycast(const mosaic::foundation::math::Vec3&,
                                                   const mosaic::foundation::math::Vec3&,
                                                   float) const override
    {
        return mosaic::engine::physics::RaycastResult{};
    }
    std::vector<mosaic::engine::physics::BodyContact> contact_events() const override
    {
        return {};
    }
};
} // namespace

TEST_CASE("PhysicsSystem creates bodies, steps, and writes dynamic poses back", "[physics]")
{
    World world;
    auto  fake = std::make_unique<FakeBackend>();
    auto* fp = fake.get();
    world.add_system<PhysicsSystem>(std::move(fake));

    // A dynamic physics box.
    const auto box = world.create_entity();
    world.add_component<Transform>(box, Transform{Vec3{0, 5, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(box, RigidBody{true, 1.0f});
    world.add_component<Collider>(box, Collider{Vec3{0.5f, 0.5f, 0.5f}});

    // A static ground.
    const auto ground = world.create_entity();
    world.add_component<Transform>(ground, Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(ground, RigidBody{false, 1.0f});
    world.add_component<Collider>(ground, Collider{Vec3{5.0f, 0.5f, 5.0f}});

    // A non-physics entity (Transform only).
    const auto other = world.create_entity();
    world.add_component<Transform>(other, Transform{Vec3{1, 1, 1}, Quat::identity(), Vec3{1, 1, 1}, {}});

    world.tick(1.0f / 60.0f);

    REQUIRE(fp->created.size() == 2); // box + ground, not the non-physics entity
    REQUIRE(fp->steps == 1);
    // Dynamic box got the backend pose written into its Transform.
    REQUIRE(world.get_component<Transform>(box)->position.y == Approx(8.0f));
    // Static ground is not written back.
    REQUIRE(world.get_component<Transform>(ground)->position.y == Approx(0.0f));
    // Non-physics entity untouched.
    REQUIRE(world.get_component<Transform>(other)->position.x == Approx(1.0f));

    // Second tick: no new bodies (lazy), one more step.
    world.tick(1.0f / 60.0f);
    REQUIRE(fp->created.size() == 2);
    REQUIRE(fp->steps == 2);
}

namespace
{
// Build a World with a real Box3D PhysicsSystem, a dynamic box at y=4, and a static ground
// (top at y=0.5); tick 120x; return the box entity's final Transform.position.y.
float drop_box_in_world()
{
    using namespace mosaic::engine::physics;
    World world;
    world.add_system<PhysicsSystem>(mosaic::physics::box3d::create_box3d_backend(WorldDef{}));

    const auto ground = world.create_entity();
    world.add_component<Transform>(ground,
        Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{10, 1, 10}, {}});
    world.add_component<RigidBody>(ground, RigidBody{false, 1.0f});
    world.add_component<Collider>(ground, Collider{Vec3{5.0f, 0.5f, 5.0f}});

    const auto box = world.create_entity();
    world.add_component<Transform>(box,
        Transform{Vec3{0, 4, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(box, RigidBody{true, 1.0f});
    world.add_component<Collider>(box, Collider{Vec3{0.5f, 0.5f, 0.5f}});

    for (int i = 0; i < 120; ++i)
    {
        world.tick(1.0f / 60.0f);
    }
    return world.get_component<Transform>(box)->position.y;
}
} // namespace

TEST_CASE("an ECS box entity falls and rests on a ground plane", "[physics]")
{
    const float y = drop_box_in_world();
    REQUIRE(y > 0.9f); // rest center ~= ground top (0.5) + box half-extent (0.5)
    REQUIRE(y < 1.1f);
}

TEST_CASE("ECS physics is deterministic across runs", "[physics]")
{
    REQUIRE(drop_box_in_world() == drop_box_in_world());
}

using mosaic::engine::physics::CapsuleCollider;
using mosaic::engine::physics::SphereCollider;

namespace
{
// Drop one dynamic entity (collider added by add_collider) at y=4 onto a static ground
// (top y=0.5); tick `ticks` times; return its final y (and, if given, the last-step delta).
template <class F>
float drop_entity(F&& add_collider, int ticks, float* last_delta = nullptr)
{
    using namespace mosaic::engine::physics;
    World world;
    world.add_system<PhysicsSystem>(mosaic::physics::box3d::create_box3d_backend(WorldDef{}));

    const auto ground = world.create_entity();
    world.add_component<Transform>(ground,
        Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{10, 1, 10}, {}});
    world.add_component<RigidBody>(ground, RigidBody{false, 1.0f});
    world.add_component<Collider>(ground, Collider{Vec3{5.0f, 0.5f, 5.0f}});

    const auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{Vec3{0, 4, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(e, RigidBody{true, 1.0f});
    add_collider(world, e);

    float y = 4.0f, prev = 4.0f;
    for (int i = 0; i < ticks; ++i)
    {
        prev = y;
        world.tick(1.0f / 60.0f);
        y = world.get_component<Transform>(e)->position.y;
    }
    if (last_delta != nullptr) { *last_delta = std::fabs(y - prev); }
    return y;
}
} // namespace

TEST_CASE("a sphere entity rests on the ground", "[physics]")
{
    float       d = 1.0f;
    const float y = drop_entity(
        [](World& w, mosaic::foundation::EntityID e)
        { w.add_component<SphereCollider>(e, SphereCollider{0.5f}); }, 120, &d);
    REQUIRE(y > 0.9f); // ground top (0.5) + radius (0.5)
    REQUIRE(y < 1.1f);
    REQUIRE(d < 1e-3f); // settled
}

TEST_CASE("a capsule entity falls and settles above the ground", "[physics]")
{
    float       d = 1.0f;
    const float y = drop_entity(
        [](World& w, mosaic::foundation::EntityID e)
        { w.add_component<CapsuleCollider>(e, CapsuleCollider{0.5f, 0.5f}); }, 180, &d);
    REQUIRE(y < 4.0f);  // it fell
    REQUIRE(y > 0.4f);  // above the ground
    REQUIRE(d < 1e-3f); // settled
}

TEST_CASE("shape physics is deterministic across runs", "[physics]")
{
    auto sphere = [](World& w, mosaic::foundation::EntityID e)
    { w.add_component<SphereCollider>(e, SphereCollider{0.5f}); };
    REQUIRE(drop_entity(sphere, 120) == drop_entity(sphere, 120));
}

TEST_CASE("a stack of two boxes rests stably", "[physics]")
{
    using namespace mosaic::engine::physics;
    World world;
    world.add_system<PhysicsSystem>(mosaic::physics::box3d::create_box3d_backend(WorldDef{}));

    const auto ground = world.create_entity();
    world.add_component<Transform>(ground,
        Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{10, 1, 10}, {}});
    world.add_component<RigidBody>(ground, RigidBody{false, 1.0f});
    world.add_component<Collider>(ground, Collider{Vec3{5.0f, 0.5f, 5.0f}});

    const auto lower = world.create_entity();
    world.add_component<Transform>(lower, Transform{Vec3{0, 2, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(lower, RigidBody{true, 1.0f});
    world.add_component<Collider>(lower, Collider{Vec3{0.5f, 0.5f, 0.5f}});

    const auto upper = world.create_entity();
    world.add_component<Transform>(upper, Transform{Vec3{0, 4, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(upper, RigidBody{true, 1.0f});
    world.add_component<Collider>(upper, Collider{Vec3{0.5f, 0.5f, 0.5f}});

    for (int i = 0; i < 240; ++i)
    {
        world.tick(1.0f / 60.0f);
    }
    const float ly = world.get_component<Transform>(lower)->position.y;
    const float uy = world.get_component<Transform>(upper)->position.y;
    REQUIRE(ly > 0.85f); // bottom box ~= 1.0
    REQUIRE(ly < 1.15f);
    REQUIRE(uy > 1.85f); // top box ~= 2.0
    REQUIRE(uy < 2.15f);
    REQUIRE(uy > ly);    // did not collapse/interpenetrate
}

TEST_CASE("an external Transform edit teleports the body (not clobbered by the sim)", "[physics]")
{
    using namespace mosaic::engine::physics;
    World world;
    world.add_system<PhysicsSystem>(mosaic::physics::box3d::create_box3d_backend(WorldDef{}));

    const auto ground = world.create_entity();
    world.add_component<Transform>(ground,
        Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{10, 1, 10}, {}});
    world.add_component<RigidBody>(ground, RigidBody{false, 1.0f});
    world.add_component<Collider>(ground, Collider{Vec3{5.0f, 0.5f, 5.0f}});

    const auto box = world.create_entity();
    world.add_component<Transform>(box, Transform{Vec3{0, 4, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(box, RigidBody{true, 1.0f});
    world.add_component<Collider>(box, Collider{Vec3{0.5f, 0.5f, 0.5f}});

    for (int i = 0; i < 120; ++i) // let it fall and rest at x~=0
    {
        world.tick(1.0f / 60.0f);
    }
    REQUIRE(world.get_component<Transform>(box)->position.x == Approx(0.0f).margin(0.1f));

    // Simulate an inspector edit: move the box sideways to x=3 (like dragging Transform).
    world.get_component<Transform>(box)->position = Vec3{3.0f, 1.0f, 0.0f};

    for (int i = 0; i < 5; ++i) // continue simulating (as if Play resumed)
    {
        world.tick(1.0f / 60.0f);
    }
    // The body must have followed the edit: the box stays near x=3, not clobbered back to 0.
    REQUIRE(world.get_component<Transform>(box)->position.x == Approx(3.0f).margin(0.2f));
}

TEST_CASE("a teleported body wakes and simulates from the new location", "[physics]")
{
    using namespace mosaic::engine::physics;
    World world;
    world.add_system<PhysicsSystem>(mosaic::physics::box3d::create_box3d_backend(WorldDef{}));

    const auto ground = world.create_entity();
    world.add_component<Transform>(ground,
        Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{10, 1, 10}, {}});
    world.add_component<RigidBody>(ground, RigidBody{false, 1.0f});
    world.add_component<Collider>(ground, Collider{Vec3{5.0f, 0.5f, 5.0f}});

    const auto box = world.create_entity();
    world.add_component<Transform>(box, Transform{Vec3{0, 4, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(box, RigidBody{true, 1.0f});
    world.add_component<Collider>(box, Collider{Vec3{0.5f, 0.5f, 0.5f}});

    for (int i = 0; i < 180; ++i) // fall, rest, and (Box3D) go to sleep
    {
        world.tick(1.0f / 60.0f);
    }

    // Lift the box into the air (inspector edit).
    world.get_component<Transform>(box)->position = Vec3{0, 5, 0};

    for (int i = 0; i < 120; ++i) // it must fall back toward the ground, not hang in the air
    {
        world.tick(1.0f / 60.0f);
    }
    const float y = world.get_component<Transform>(box)->position.y;
    REQUIRE(y < 2.0f); // fell from y=5 back toward rest (~1.0), i.e. the body woke and simulated
}

TEST_CASE("PhysicsSystem::apply_impulse forwards to the backend with the entity's handle", "[physics]")
{
    World world;
    auto  backend = std::make_unique<FakeBackend>();
    auto* fake    = backend.get();
    world.add_system<PhysicsSystem>(std::move(backend));

    auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{Vec3{1, 2, 3}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(e, RigidBody{true, 1.0f});
    world.add_component<Collider>(e, Collider{Vec3{0.5f, 0.5f, 0.5f}});
    world.tick(1.0f / 60.0f); // creates the body

    auto* physics = world.get_system<PhysicsSystem>();
    REQUIRE(physics != nullptr);
    physics->apply_impulse(e, Vec3{0.0f, 5.0f, 0.0f}, Vec3{1.0f, 2.5f, 3.0f});

    REQUIRE(fake->impulses.size() == 1);
    REQUIRE(fake->impulses[0].body == 1); // first created body
    REQUIRE(fake->impulses[0].impulse.y == Approx(5.0f));
    REQUIRE(fake->impulses[0].point.y == Approx(2.5f));
}

TEST_CASE("PhysicsSystem::apply_impulse is a no-op for an entity with no body", "[physics]")
{
    // Review Focus 2: a RigidBody with no collider component never gets a backend
    // body, so the entity is absent from the handle map. Pushing it must do nothing
    // rather than look up a handle that was never created.
    World world;
    auto  backend = std::make_unique<FakeBackend>();
    auto* fake    = backend.get();
    world.add_system<PhysicsSystem>(std::move(backend));

    auto no_collider = world.create_entity();
    world.add_component<Transform>(no_collider,
                                   Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(no_collider, RigidBody{true, 1.0f});
    auto never_seen = world.create_entity(); // no components at all
    world.tick(1.0f / 60.0f);

    auto* physics = world.get_system<PhysicsSystem>();
    REQUIRE(physics != nullptr);
    physics->apply_impulse(no_collider, Vec3{0, 5, 0}, Vec3{0, 0, 0});
    physics->apply_impulse(never_seen, Vec3{0, 5, 0}, Vec3{0, 0, 0});

    REQUIRE(fake->impulses.empty());
}

TEST_CASE("An off-center impulse makes a real sphere roll and not merely slide", "[physics]")
{
    // THE test for this slice. A position assertion alone passes even if someone
    // swaps in b3Body_ApplyLinearImpulseToCenter; the rotation assertion is the only
    // thing that pins rolling. Do not relax this to "the object moved".
    World world;
    world.add_system<PhysicsSystem>(
        mosaic::physics::box3d::create_box3d_backend(mosaic::engine::physics::WorldDef{}));

    auto ball = world.create_entity();
    world.add_component<Transform>(ball,
                                   Transform{Vec3{0, 5, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(ball, RigidBody{true, 1.0f});
    world.add_component<mosaic::engine::physics::SphereCollider>(
        ball, mosaic::engine::physics::SphereCollider{0.5f});
    world.tick(1.0f / 60.0f);

    auto* physics = world.get_system<PhysicsSystem>();
    REQUIRE(physics != nullptr);
    const auto* before = world.get_component<Transform>(ball);
    REQUIRE(before != nullptr);
    const Quat rot_before = before->rotation;

    // Horizontal push applied at the BOTTOM of the sphere (center y=5, radius 0.5),
    // which is off the center of mass and therefore produces torque.
    physics->apply_impulse(ball, Vec3{2.0f, 0.0f, 0.0f}, Vec3{0.0f, 4.5f, 0.0f});

    for (int i = 0; i < 30; ++i) { world.tick(1.0f / 60.0f); }

    const auto* after = world.get_component<Transform>(ball);
    REQUIRE(after != nullptr);
    REQUIRE(after->position.x > 0.05f); // it moved along the push
    const Quat rot_after = after->rotation;
    const float dq = std::fabs(rot_after.x - rot_before.x) + std::fabs(rot_after.y - rot_before.y)
                   + std::fabs(rot_after.z - rot_before.z) + std::fabs(rot_after.w - rot_before.w);
    REQUIRE(dq > 0.01f); // and it SPUN -- this is the rolling assertion
}

TEST_CASE("A settled sleeping body still responds to an impulse", "[physics]")
{
    // The wake=true guarantee. Box3D sleeps rested bodies, and the demo objects settle
    // within ~2s, so every click a user would realistically try lands on a sleeping
    // body. Without wake=true this is a silent no-op that looks like a broken feature.
    World world;
    world.add_system<PhysicsSystem>(
        mosaic::physics::box3d::create_box3d_backend(mosaic::engine::physics::WorldDef{}));

    auto ground = world.create_entity();
    world.add_component<Transform>(ground,
                                   Transform{Vec3{0, -1, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(ground, RigidBody{false, 1.0f});
    world.add_component<Collider>(ground, Collider{Vec3{20.0f, 0.5f, 20.0f}});

    auto ball = world.create_entity();
    world.add_component<Transform>(ball,
                                   Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(ball, RigidBody{true, 1.0f});
    world.add_component<mosaic::engine::physics::SphereCollider>(
        ball, mosaic::engine::physics::SphereCollider{0.5f});

    // Long enough to land and fall asleep.
    for (int i = 0; i < 400; ++i) { world.tick(1.0f / 60.0f); }
    const float x_asleep = world.get_component<Transform>(ball)->position.x;

    auto* physics = world.get_system<PhysicsSystem>();
    REQUIRE(physics != nullptr);
    physics->apply_impulse(ball, Vec3{3.0f, 0.0f, 0.0f}, Vec3{0.0f, -0.5f, 0.0f});
    for (int i = 0; i < 30; ++i) { world.tick(1.0f / 60.0f); }

    REQUIRE(world.get_component<Transform>(ball)->position.x > x_asleep + 0.05f);
}

TEST_CASE("An impulse on a static body changes nothing", "[physics]")
{
    // Review Focus 1: the ground is static and is the biggest click target on screen,
    // so this is the most likely real click. It must be harmless.
    World world;
    world.add_system<PhysicsSystem>(
        mosaic::physics::box3d::create_box3d_backend(mosaic::engine::physics::WorldDef{}));

    auto ground = world.create_entity();
    world.add_component<Transform>(ground,
                                   Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(ground, RigidBody{false, 1.0f});
    world.add_component<Collider>(ground, Collider{Vec3{5.0f, 0.5f, 5.0f}});
    world.tick(1.0f / 60.0f);

    auto* physics = world.get_system<PhysicsSystem>();
    REQUIRE(physics != nullptr);
    REQUIRE_NOTHROW(physics->apply_impulse(ground, Vec3{50.0f, 50.0f, 0.0f}, Vec3{1.0f, 0.0f, 0.0f}));
    for (int i = 0; i < 10; ++i) { world.tick(1.0f / 60.0f); }

    const auto* t = world.get_component<Transform>(ground);
    REQUIRE(t != nullptr);
    REQUIRE(t->position.x == Approx(0.0f).margin(1e-4));
    REQUIRE(t->position.y == Approx(0.0f).margin(1e-4));
}

TEST_CASE("A zero impulse leaves the body and its pose untouched", "[physics]")
{
    // Review Focus 4: strength 0 or a zero direction must be inert and must not
    // introduce NaN into the pose through a normalize-like path.
    World world;
    world.add_system<PhysicsSystem>(
        mosaic::physics::box3d::create_box3d_backend(mosaic::engine::physics::WorldDef{}));

    auto ball = world.create_entity();
    world.add_component<Transform>(ball,
                                   Transform{Vec3{0, 5, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(ball, RigidBody{true, 1.0f});
    world.add_component<mosaic::engine::physics::SphereCollider>(
        ball, mosaic::engine::physics::SphereCollider{0.5f});
    world.tick(1.0f / 60.0f);

    auto* physics = world.get_system<PhysicsSystem>();
    REQUIRE(physics != nullptr);
    physics->apply_impulse(ball, Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 5.0f, 0.0f});
    world.tick(1.0f / 60.0f);

    const auto* t = world.get_component<Transform>(ball);
    REQUIRE(t != nullptr);
    REQUIRE(std::isfinite(t->position.x));
    REQUIRE(std::isfinite(t->position.y));
    REQUIRE(std::isfinite(t->position.z));
    REQUIRE(t->position.x == Approx(0.0f).margin(1e-4)); // no sideways drift from a zero push
}

TEST_CASE("A rotated static body gets a rotated collider and not just a rotated Transform", "[physics]")
{
    // The discriminating test. A thin horizontal plate (half extents 2 x 0.1 x 2)
    // rotated 90 degrees about Z stands up vertical, so a horizontal ray at y = 1
    // hits it. Unrotated, that ray passes over the plate entirely and misses. A pose
    // assertion alone would pass today, because the write-back skips static bodies and
    // leaves the Transform's rotation alone -- only the collider is wrong. This is what
    // catches that.
    World world;
    world.add_system<PhysicsSystem>(
        mosaic::physics::box3d::create_box3d_backend(mosaic::engine::physics::WorldDef{}));

    auto plate = world.create_entity();
    world.add_component<Transform>(
        plate, Transform{Vec3{0, 0, 0},
                         mosaic::foundation::math::from_axis_angle(Vec3{0, 0, 1}, 1.5707963f),
                         Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(plate, RigidBody{false, 1.0f});
    world.add_component<Collider>(plate, Collider{Vec3{2.0f, 0.1f, 2.0f}});
    world.tick(1.0f / 60.0f);

    auto* physics = world.get_system<PhysicsSystem>();
    REQUIRE(physics != nullptr);

    const auto hit = physics->raycast(Vec3{-5.0f, 1.0f, 0.0f}, Vec3{1.0f, 0.0f, 0.0f}, 10.0f);
    REQUIRE(hit.hit);
    REQUIRE(hit.entity == plate);
}

TEST_CASE("An unrotated plate is missed by the same ray", "[physics]")
{
    // The guard for the test above: if this ray hit an UNROTATED plate too, the test
    // above would prove nothing about rotation.
    World world;
    world.add_system<PhysicsSystem>(
        mosaic::physics::box3d::create_box3d_backend(mosaic::engine::physics::WorldDef{}));

    auto plate = world.create_entity();
    world.add_component<Transform>(
        plate, Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(plate, RigidBody{false, 1.0f});
    world.add_component<Collider>(plate, Collider{Vec3{2.0f, 0.1f, 2.0f}});
    world.tick(1.0f / 60.0f);

    auto* physics = world.get_system<PhysicsSystem>();
    REQUIRE(physics != nullptr);

    REQUIRE_FALSE(physics->raycast(Vec3{-5.0f, 1.0f, 0.0f}, Vec3{1.0f, 0.0f, 0.0f}, 10.0f).hit);
}

TEST_CASE("A rotated dynamic body keeps its rotation instead of snapping to identity", "[physics]")
{
    // Dynamic bodies ARE written back every tick, so before this fix a rotated crate
    // was created unrotated and then had its authored rotation overwritten with the
    // simulation's identity on the very first tick -- visibly flattening itself the
    // instant physics started.
    World world;
    world.add_system<PhysicsSystem>(
        mosaic::physics::box3d::create_box3d_backend(mosaic::engine::physics::WorldDef{}));

    const Quat tilted = mosaic::foundation::math::from_axis_angle(Vec3{0, 0, 1}, 1.5707963f);
    auto crate = world.create_entity();
    world.add_component<Transform>(crate,
                                   Transform{Vec3{0, 10, 0}, tilted, Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(crate, RigidBody{true, 1.0f});
    world.add_component<Collider>(crate, Collider{Vec3{0.5f, 0.5f, 0.5f}});

    world.tick(1.0f / 60.0f); // free fall, nothing to collide with, so no spin is induced

    const auto* t = world.get_component<Transform>(crate);
    REQUIRE(t != nullptr);
    REQUIRE(t->rotation.z == Approx(tilted.z).margin(1e-3));
    REQUIRE(t->rotation.w == Approx(tilted.w).margin(1e-3));
}

TEST_CASE("A body authored with no rotation still simulates unrotated", "[physics]")
{
    // No-regression guard: the default Quat is w=1 (identity), so bodies that never
    // asked for a rotation must behave exactly as they did before.
    World world;
    world.add_system<PhysicsSystem>(
        mosaic::physics::box3d::create_box3d_backend(mosaic::engine::physics::WorldDef{}));

    auto box = world.create_entity();
    world.add_component<Transform>(box,
                                   Transform{Vec3{0, 10, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(box, RigidBody{true, 1.0f});
    world.add_component<Collider>(box, Collider{Vec3{0.5f, 0.5f, 0.5f}});

    world.tick(1.0f / 60.0f);

    const auto* t = world.get_component<Transform>(box);
    REQUIRE(t != nullptr);
    REQUIRE(t->rotation.x == Approx(0.0f).margin(1e-4));
    REQUIRE(t->rotation.y == Approx(0.0f).margin(1e-4));
    REQUIRE(t->rotation.z == Approx(0.0f).margin(1e-4));
    REQUIRE(t->rotation.w == Approx(1.0f).margin(1e-4));
    REQUIRE(t->position.y < 10.0f); // it fell
}

TEST_CASE("PhysicsSystem::raycast is callable from another thread while update runs", "[physics]")
{
    // Review Focus 3 and 5: the ordinary main-thread raycast path must still work, and
    // a second thread hammering raycast while the main thread ticks must neither
    // deadlock nor crash. This is the smoke test; the overlap DETECTOR is Task 2.
    World world;
    world.add_system<PhysicsSystem>(
        mosaic::physics::box3d::create_box3d_backend(mosaic::engine::physics::WorldDef{}));

    auto ground = world.create_entity();
    world.add_component<Transform>(ground,
                                   Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{10, 1, 10}, {}});
    world.add_component<RigidBody>(ground, RigidBody{false, 1.0f});
    world.add_component<Collider>(ground, Collider{Vec3{5.0f, 0.5f, 5.0f}});
    world.tick(1.0f / 60.0f);

    auto* physics = world.get_system<PhysicsSystem>();
    REQUIRE(physics != nullptr);

    std::atomic<bool> stop{false};
    std::atomic<int>  hits{0};
    std::thread reader(
        [&]
        {
            while (!stop.load())
            {
                if (physics->raycast(Vec3{0, 5, 0}, Vec3{0, -1, 0}, 10.0f).hit) { ++hits; }
            }
        });

    for (int i = 0; i < 200; ++i) { world.tick(1.0f / 60.0f); }
    stop.store(true);
    reader.join();

    REQUIRE(hits.load() > 0); // the ray really was hitting the ground, not just not-crashing
}

TEST_CASE("PhysicsSystem locked methods do not re-enter one another", "[physics]")
{
    // Review Focus 4: a plain std::mutex self-deadlocks on re-entry. The claim is that
    // update() never calls raycast/apply_impulse/has_body and vice versa. If that ever
    // stops being true this test hangs rather than failing quietly, which is the point.
    World world;
    world.add_system<PhysicsSystem>(
        mosaic::physics::box3d::create_box3d_backend(mosaic::engine::physics::WorldDef{}));

    auto ball = world.create_entity();
    world.add_component<Transform>(ball,
                                   Transform{Vec3{0, 5, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(ball, RigidBody{true, 1.0f});
    world.add_component<SphereCollider>(ball, SphereCollider{0.5f});
    world.tick(1.0f / 60.0f);

    auto* physics = world.get_system<PhysicsSystem>();
    REQUIRE(physics != nullptr);

    // Each of these takes the lock; calling them back to back on one thread proves the
    // lock is released between calls.
    REQUIRE(physics->has_body(ball));
    REQUIRE_NOTHROW(physics->apply_impulse(ball, Vec3{1, 0, 0}, Vec3{0, 4.5f, 0}));
    REQUIRE_NOTHROW(physics->raycast(Vec3{0, 10, 0}, Vec3{0, -1, 0}, 20.0f));
    REQUIRE_NOTHROW(world.tick(1.0f / 60.0f));
    REQUIRE(physics->has_body(ball));
}

TEST_CASE("PhysicsSystem destroys the body of a destroyed entity", "[physics]")
{
    World world;
    auto  backend = std::make_unique<FakeBackend>();
    auto* fake = backend.get();
    world.add_system<PhysicsSystem>(std::move(backend));

    const auto box = world.create_entity();
    world.add_component<Transform>(box, Transform{Vec3{0, 3, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(box, RigidBody{true, 1.0f});
    world.add_component<Collider>(box, Collider{Vec3{0.5f, 0.5f, 0.5f}});

    world.tick(1.0f / 60.0f);
    auto* physics = world.get_system<PhysicsSystem>();
    REQUIRE(physics != nullptr);
    REQUIRE(physics->has_body(box));
    REQUIRE(fake->destroyed.empty());

    world.destroy_entity(box); // deferred: commits at the END of this tick
    world.tick(1.0f / 60.0f);  // commits the destroy, reap has not run yet
    world.tick(1.0f / 60.0f);  // this tick's reap sees the vanished RigidBody

    // Exactly one: a double destroy is as much a bug as none, and the maps must stop it
    // firing again on later ticks.
    REQUIRE(fake->destroyed.size() == 1);
    world.tick(1.0f / 60.0f);
    REQUIRE(fake->destroyed.size() == 1);

    // Review Focus 5: a script can hold an entity id across a destroy. Both of these are
    // reachable from the Python bindings shipped in #207.
    REQUIRE_FALSE(physics->has_body(box));
    physics->apply_impulse(box, Vec3{10, 0, 0}, Vec3{0, 0, 0});
    // Not merely "it did not throw": FakeBackend records every impulse it is handed, so an
    // empty log is what actually proves the stale handle never reached the backend.
    REQUIRE(fake->impulses.empty());
}

TEST_CASE("PhysicsSystem reaps a body when RigidBody is removed from a living entity", "[physics]")
{
    // The reap predicate is "RigidBody is gone", not "the entity is invalid" -- which is
    // what makes this case work at all. world.is_valid() would still be true here.
    World world;
    auto  backend = std::make_unique<FakeBackend>();
    auto* fake = backend.get();
    world.add_system<PhysicsSystem>(std::move(backend));

    const auto box = world.create_entity();
    world.add_component<Transform>(box, Transform{Vec3{0, 3, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(box, RigidBody{true, 1.0f});
    world.add_component<Collider>(box, Collider{Vec3{0.5f, 0.5f, 0.5f}});
    world.tick(1.0f / 60.0f);

    auto* physics = world.get_system<PhysicsSystem>();
    REQUIRE(physics->has_body(box));

    REQUIRE(world.remove_component<RigidBody>(box));
    world.tick(1.0f / 60.0f);

    REQUIRE(world.is_valid(box)); // the entity is still alive -- only the component went
    REQUIRE(fake->destroyed.size() == 1);
    REQUIRE_FALSE(physics->has_body(box));
}

TEST_CASE("PhysicsSystem reaps every body after World clear", "[physics]")
{
    // clear() bypasses the deferred destroy queue and runs no systems, so the reap is the
    // only thing that can notice. Without it, a scene reload leaks every body in the
    // outgoing scene.
    World world;
    auto  backend = std::make_unique<FakeBackend>();
    auto* fake = backend.get();
    world.add_system<PhysicsSystem>(std::move(backend));

    for (int i = 0; i < 3; ++i)
    {
        const auto e = world.create_entity();
        world.add_component<Transform>(
            e, Transform{Vec3{static_cast<float>(i), 3, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
        world.add_component<RigidBody>(e, RigidBody{true, 1.0f});
        world.add_component<Collider>(e, Collider{Vec3{0.5f, 0.5f, 0.5f}});
    }
    world.tick(1.0f / 60.0f);
    REQUIRE(fake->created.size() == 3);

    world.clear();
    REQUIRE_NOTHROW(world.tick(1.0f / 60.0f));

    REQUIRE(fake->destroyed.size() == 3);
}

TEST_CASE("PhysicsSystem keeps surviving bodies when one is reaped", "[physics]")
{
    // Review Focus 3: `it = bodies_.erase(it)` is correct, but a plain ++it after erase --
    // or erasing inside a range-for -- silently loses live bodies. Every other case here
    // uses one body, so none of them would notice.
    World world;
    auto  backend = std::make_unique<FakeBackend>();
    auto* fake = backend.get();
    world.add_system<PhysicsSystem>(std::move(backend));

    std::vector<mosaic::foundation::EntityID> boxes;
    for (int i = 0; i < 5; ++i)
    {
        const auto e = world.create_entity();
        world.add_component<Transform>(
            e, Transform{Vec3{static_cast<float>(i), 3, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
        world.add_component<RigidBody>(e, RigidBody{true, 1.0f});
        world.add_component<Collider>(e, Collider{Vec3{0.5f, 0.5f, 0.5f}});
        boxes.push_back(e);
    }
    world.tick(1.0f / 60.0f);

    auto* physics = world.get_system<PhysicsSystem>();
    for (const auto e : boxes) { REQUIRE(physics->has_body(e)); }

    // Destroy one from the middle, so a broken iterator would strand a neighbour.
    world.destroy_entity(boxes[2]);
    world.tick(1.0f / 60.0f);
    world.tick(1.0f / 60.0f);

    REQUIRE(fake->destroyed.size() == 1);
    // The handle of boxes[2] specifically. FakeBackend hands out 1..5 in creation order, so
    // asserting `!= 0` would be true for any of them and would not catch a reap that
    // destroyed an arbitrary entry instead of the dead one.
    REQUIRE(fake->destroyed[0] == 3);
    for (std::size_t i = 0; i < boxes.size(); ++i)
    {
        if (i == 2) { REQUIRE_FALSE(physics->has_body(boxes[i])); }
        else { REQUIRE(physics->has_body(boxes[i])); }
    }
}

TEST_CASE("PhysicsSystem emits no ContactEvent naming a reaped entity", "[physics]")
{
    // Review Focus 4: a contact naming a reaped body must never reach a subscriber.
    //
    // There are TWO guards, and this test covers both. b3DestroyBody pushes an end-touch
    // event for the destroyed body's shapes, and the buffer is swapped at the end of
    // b3World_Step -- so the reap at step 0 produces an event that step 3 surfaces in the
    // same tick. First the Box3D backend's step() drops end events whose shapes are no
    // longer valid; then step 5a drops any contact whose handle is missing from
    // handle_to_entity_, which the reap erased.
    //
    // Falsifiable, and measured: removing the backend's shape-validity filter makes this
    // test fail (it crashes on b3Shape_GetBody over a freed shape). An earlier note here
    // claimed the test "cannot be made to fail" because Box3D emitted no event -- that was
    // wrong on the mechanism, and review caught it; the probe had only been run against the
    // second guard, which the first one shields.
    World world;
    world.add_system<PhysicsSystem>(
        mosaic::physics::box3d::create_box3d_backend(mosaic::engine::physics::WorldDef{}));

    const auto ground = world.create_entity();
    world.add_component<Transform>(ground,
                                   Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{10, 1, 10}, {}});
    world.add_component<RigidBody>(ground, RigidBody{false, 1.0f});
    world.add_component<Collider>(ground, Collider{Vec3{5.0f, 0.5f, 5.0f}});

    const auto box = world.create_entity();
    world.add_component<Transform>(box, Transform{Vec3{0, 2, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(box, RigidBody{true, 1.0f});
    world.add_component<Collider>(box, Collider{Vec3{0.5f, 0.5f, 0.5f}});

    // Subscribe BEFORE settling, so the first phase proves the channel actually delivers
    // box-naming contacts. Without that, "no contact named the box" after the destroy would
    // be satisfied by a subscriber that never fires at all -- and whether Box3D emits an
    // end-touch event for a destroyed body is not something this test should assume.
    bool named_the_box = false;
    world.events().subscribe<mosaic::engine::physics::ContactEvent>(
        [&](const mosaic::engine::physics::ContactEvent& c)
        {
            if (c.a == box || c.b == box) { named_the_box = true; }
        });

    for (int i = 0; i < 120; ++i) { world.tick(1.0f / 60.0f); } // settle, generating contacts
    REQUIRE(named_the_box); // the channel works; now it must go quiet for the right reason

    named_the_box = false;
    world.destroy_entity(box);
    for (int i = 0; i < 10; ++i) { world.tick(1.0f / 60.0f); }

    REQUIRE_FALSE(named_the_box);
}

TEST_CASE("a destroyed entity stops blocking the things resting on it", "[physics]")
{
    // THE load-bearing case, and the only one that fails if destroy_body is never called
    // while the maps are still erased. Everything else above would pass with the body left
    // alive in the backend -- this is the user-visible bug: destroy a crate and it still
    // holds up whatever was stacked on it, with no mesh to explain why.
    World world;
    world.add_system<PhysicsSystem>(
        mosaic::physics::box3d::create_box3d_backend(mosaic::engine::physics::WorldDef{}));

    const auto ground = world.create_entity();
    world.add_component<Transform>(ground,
                                   Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{10, 1, 10}, {}});
    world.add_component<RigidBody>(ground, RigidBody{false, 1.0f});
    world.add_component<Collider>(ground, Collider{Vec3{5.0f, 0.5f, 5.0f}});

    const auto lower = world.create_entity();
    world.add_component<Transform>(lower, Transform{Vec3{0, 1, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(lower, RigidBody{true, 1.0f});
    world.add_component<Collider>(lower, Collider{Vec3{0.5f, 0.5f, 0.5f}});

    const auto upper = world.create_entity();
    world.add_component<Transform>(upper, Transform{Vec3{0, 2.1f, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(upper, RigidBody{true, 1.0f});
    world.add_component<Collider>(upper, Collider{Vec3{0.5f, 0.5f, 0.5f}});

    for (int i = 0; i < 180; ++i) { world.tick(1.0f / 60.0f); } // both settle into a stack

    const float stacked_y = world.get_component<Transform>(upper)->position.y;
    REQUIRE(stacked_y > 1.4f); // the upper box really is resting on the lower one

    world.destroy_entity(lower);
    for (int i = 0; i < 240; ++i) { world.tick(1.0f / 60.0f); }

    // With the lower body destroyed, the upper box falls to the ground: its collider's
    // half-extent is 0.5 and the ground's top face is at y = 0.5, so it rests near y = 1.0.
    const float fallen_y = world.get_component<Transform>(upper)->position.y;
    REQUIRE(fallen_y < stacked_y - 0.5f);
    REQUIRE(fallen_y == Approx(1.0f).margin(0.2f));
}
