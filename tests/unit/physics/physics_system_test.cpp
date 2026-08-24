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
