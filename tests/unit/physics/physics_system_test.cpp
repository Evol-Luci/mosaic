#include <cmath>
#include <cstdint>
#include <memory>
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

    BodyHandle create_body(const BodyDef& def) override
    {
        created.push_back(def);
        return static_cast<BodyHandle>(created.size());
    }
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
