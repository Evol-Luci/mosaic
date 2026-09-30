// Tests the property, not the symptom.
//
// "Ran a threaded script, saw no crash" is not evidence of mutual exclusion -- it is
// the reasoning that let a real race hide behind a CI exclusion in this project for
// nine days. This backend records whether two of its methods were ever in flight at
// once, and sleeps inside each one to widen the window.

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/physics/physics_backend.hpp"
#include "engine/physics/physics_components.hpp"
#include "engine/physics/physics_system.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"

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
// Every method enters through Guard, which trips overlap_seen if a second call is
// already inside, and sleeps to make a genuine overlap overwhelmingly likely to be
// observed rather than missed.
class OverlapDetectingBackend : public PhysicsBackend
{
public:
    std::atomic<int>  depth{0};
    std::atomic<bool> overlap_seen{false};

    struct Guard
    {
        OverlapDetectingBackend* b;
        explicit Guard(OverlapDetectingBackend* be) : b(be)
        {
            if (b->depth.fetch_add(1) > 0) { b->overlap_seen.store(true); }
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        ~Guard()
        {
            if (b->depth.fetch_sub(1) > 1) { b->overlap_seen.store(true); }
        }
    };

    BodyHandle create_body(const BodyDef&) override
    {
        Guard g(this);
        return ++next_;
    }
    void step(float, int) override { Guard g(this); }
    Pose body_pose(BodyHandle) const override
    {
        Guard g(const_cast<OverlapDetectingBackend*>(this));
        return Pose{};
    }
    void set_body_transform(BodyHandle, const Vec3&, const Quat&) override { Guard g(this); }
    void apply_impulse(BodyHandle, const Vec3&, const Vec3&) override { Guard g(this); }
    mosaic::engine::physics::RaycastResult raycast(const Vec3&, const Vec3&,
                                                   float) const override
    {
        Guard g(const_cast<OverlapDetectingBackend*>(this));
        return mosaic::engine::physics::RaycastResult{};
    }
    std::vector<mosaic::engine::physics::BodyContact> contact_events() const override
    {
        Guard g(const_cast<OverlapDetectingBackend*>(this));
        return {};
    }

private:
    BodyHandle next_ = 0;
};
} // namespace

TEST_CASE("PhysicsSystem never has two backend calls in flight at once", "[physics]")
{
    World world;
    auto  backend = std::make_unique<OverlapDetectingBackend>();
    auto* probe   = backend.get();
    world.add_system<PhysicsSystem>(std::move(backend));

    // Several bodies, so update() spends real time inside create_body and the
    // bodies_ insert -- Review Focus 1: the earliest part of update(), not just step().
    for (int i = 0; i < 8; ++i)
    {
        auto e = world.create_entity();
        world.add_component<Transform>(
            e, Transform{Vec3{static_cast<float>(i), 2, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
        world.add_component<RigidBody>(e, RigidBody{true, 1.0f});
        world.add_component<Collider>(e, Collider{Vec3{0.5f, 0.5f, 0.5f}});
    }

    auto* physics = world.get_system<PhysicsSystem>();
    REQUIRE(physics != nullptr);

    std::atomic<bool> stop{false};
    // Review Focus 2: has_body() reads bodies_ while update() inserts into it. That is a
    // map race, not a backend call, so it is invisible to anyone grepping for backend_->.
    std::thread reader(
        [&]
        {
            while (!stop.load())
            {
                physics->raycast(Vec3{0, 5, 0}, Vec3{0, -1, 0}, 10.0f);
                physics->has_body(mosaic::foundation::EntityID{});
            }
        });

    for (int i = 0; i < 150; ++i) { world.tick(1.0f / 60.0f); }
    stop.store(true);
    reader.join();

    REQUIRE_FALSE(probe->overlap_seen.load());
}
