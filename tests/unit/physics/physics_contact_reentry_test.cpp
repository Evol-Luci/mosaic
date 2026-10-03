// A synchronous ContactEvent handler used to run INSIDE backend_mutex_, so a handler that
// called back into PhysicsSystem self-deadlocked on a plain std::mutex.
//
// The pre-fix failure mode is a HANG, not a failed assertion, and this repo sets no ctest
// TIMEOUT (so the default 1500s applies). These tests therefore run the tick on a detached
// thread and let the MAIN thread time out, which is what makes the failure a named Catch2
// assertion printed in three seconds instead of a 25-minute stall.
//
// A tripped watchdog cannot clean up: the detached thread stays blocked on the mutex
// forever, so the process may die untidily at exit. That is the pre-fix state, not
// something these tests introduce -- expect it when running the mutation probe.

#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "engine/physics/physics_components.hpp"
#include "engine/physics/physics_system.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "modules/physics_box3d/box3d_backend.hpp"

using mosaic::engine::World;
using mosaic::engine::physics::Collider;
using mosaic::engine::physics::ContactEvent;
using mosaic::engine::physics::PhysicsSystem;
using mosaic::engine::physics::RigidBody;
using mosaic::engine::physics::WorldDef;
using mosaic::engine::transform::Transform;
using mosaic::foundation::math::Quat;
using mosaic::foundation::math::Vec3;
using mosaic::physics::box3d::create_box3d_backend;

namespace
{
// Ceiling on a tick that normally takes microseconds -- four orders of magnitude of margin.
// If a loaded runner ever trips this spuriously, raise it; do not delete the test.
constexpr int kWatchdogMs = 3000;

// Everything the detached tick thread can still reach after the TEST_CASE has unwound.
//
// Heap-owned on purpose. These used to be TEST_CASE stack locals captured by reference, which
// made a tripped watchdog undefined behaviour: REQUIRE throws, the case unwinds, the World and
// these locals die, and the thread is either parked on a destroyed mutex or -- in the case the
// 3000 ms margin actually guards against, a slow runner rather than a deadlock -- still
// actively ticking them. Measured 2026-10-03: that crashed with SIGABRT or SIGSEGV *after*
// printing the assertion, so a CI run saw a signal instead of the named failure this watchdog
// exists to produce. Now the thread holds its World and Probe alive and they LEAK on a trip,
// which is strictly better than a use-after-free.
//
// Two trip kinds, two leak durations -- worth distinguishing. A spurious trip (slow runner, no
// deadlock) is self-healing: the thread finishes its ticks late, drops its references, and the
// objects go. A genuine deadlock trip is not: the thread parks forever on a mutex and the
// objects live for the rest of the test PROCESS, because Catch2 keeps running later cases
// after a failed REQUIRE. Both are bounded by process exit, and the mutex is per-instance, so
// a leaked parked thread cannot block the next case's own World.
struct Probe
{
    int                          handler_calls = 0;
    bool                         hit_the_box = false;
    bool                         pushed = false;
    mosaic::foundation::EntityID box{};
};

// Ticks `world` `count` times on a detached thread and waits up to kWatchdogMs for it to
// finish. Returns false if it did not. Both shared_ptrs are captured BY VALUE, so a thread
// that outlives this call keeps what it touches alive.
//
// Probe deliberately does NOT own the World: the subscriber lambda stored in the World's
// EventBus captures the Probe, so a Probe owning the World back would close a reference cycle
// and leak both on the PASSING path too. The handlers hold the World and PhysicsSystem as raw
// non-owning pointers instead -- safe because a handler only ever runs inside a tick, and a
// tick only happens on the thread that is holding the World alive.
bool tick_with_watchdog(std::shared_ptr<World> world, std::shared_ptr<Probe> probe, int count)
{
    auto done = std::make_shared<std::atomic<bool>>(false);
    std::thread(
        [world, probe, count, done]
        {
            // `probe` is unused here on purpose: naming it in the capture list above is what
            // extends its lifetime past a tripped watchdog (a by-value capture becomes a
            // closure member whether the body reads it or not). This cast only silences the
            // unused-capture warning -- it is not what keeps the Probe alive.
            (void)probe;
            for (int i = 0; i < count; ++i) { world->tick(1.0f / 60.0f); }
            done->store(true);
        })
        .detach();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kWatchdogMs);
    while (!done->load() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return done->load();
}

// Ground plus a box dropped onto it: produces a Begin contact within ~90 ticks. Returns the
// box, so a caller can assert against the entity a ray hit rather than merely that it hit
// something -- the ground slab is also under the ray.
mosaic::foundation::EntityID build_falling_box_scene(World& world)
{
    const auto ground = world.create_entity();
    world.add_component<Transform>(ground,
                                   Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{10, 1, 10}, {}});
    world.add_component<RigidBody>(ground, RigidBody{false, 1.0f});
    world.add_component<Collider>(ground, Collider{Vec3{5.0f, 0.5f, 5.0f}});

    const auto box = world.create_entity();
    world.add_component<Transform>(box, Transform{Vec3{0, 3, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(box, RigidBody{true, 1.0f});
    world.add_component<Collider>(box, Collider{Vec3{0.5f, 0.5f, 0.5f}});
    return box;
}
} // namespace

TEST_CASE("a ContactEvent handler can raycast without deadlocking", "[physics]")
{
    auto world = std::make_shared<World>();
    world->add_system<PhysicsSystem>(create_box3d_backend(WorldDef{}));
    auto* physics = world->get_system<PhysicsSystem>();
    REQUIRE(physics != nullptr);

    auto probe = std::make_shared<Probe>();
    world->events().subscribe<ContactEvent>(
        [probe, physics](const ContactEvent&)
        {
            ++probe->handler_calls;
            // Review Focus 4: not merely "it returned", and not merely that it hit SOMETHING
            // -- the ground slab lies under this ray too, so a bare `.hit` would be true even
            // with the box's pose wrong or its body missing. Step 4 wrote the poses before
            // this handler ran, so the closest hit on the way down must be the box itself.
            const auto r = physics->raycast(Vec3{0, 10, 0}, Vec3{0, -1, 0}, 20.0f);
            if (r.hit && r.entity == probe->box) { probe->hit_the_box = true; }
        });

    probe->box = build_falling_box_scene(*world);

    REQUIRE(tick_with_watchdog(world, probe, 180)); // false == the raycast deadlocked
    REQUIRE(probe->handler_calls > 0);              // the scenario really produced a contact
    REQUIRE(probe->hit_the_box);
}

TEST_CASE("a ContactEvent handler can apply an impulse without deadlocking", "[physics]")
{
    auto world = std::make_shared<World>();
    world->add_system<PhysicsSystem>(create_box3d_backend(WorldDef{}));
    auto* physics = world->get_system<PhysicsSystem>();
    REQUIRE(physics != nullptr);

    auto   probe = std::make_shared<Probe>();
    World* raw_world = world.get(); // non-owning: see tick_with_watchdog's note on the cycle
    world->events().subscribe<ContactEvent>(
        [probe, physics, raw_world](const ContactEvent& c)
        {
            ++probe->handler_calls;
            // Push the DYNAMIC side of the pair. Box3D does not guarantee which of a/b is
            // which, and apply_impulse is documented as a no-op for a static body -- so
            // pushing c.a blindly silently does nothing whenever a is the ground, and the
            // test would then be asserting against a push that never happened.
            const auto is_dynamic = [raw_world](mosaic::foundation::EntityID e)
            {
                const RigidBody* rb = raw_world->get_component<RigidBody>(e);
                return rb != nullptr && rb->dynamic;
            };
            const mosaic::foundation::EntityID target =
                is_dynamic(c.a) ? c.a : (is_dynamic(c.b) ? c.b : mosaic::foundation::EntityID{});

            // has_body() takes the same lock and touches no backend call, so it is the
            // cheapest proof the maps are readable from here.
            if (!probe->pushed && physics->has_body(target))
            {
                physics->apply_impulse(target, Vec3{25, 0, 0}, Vec3{0, 0, 0});
                probe->pushed = true;
            }
        });

    (void)build_falling_box_scene(*world);

    REQUIRE(tick_with_watchdog(world, probe, 240)); // false == apply_impulse deadlocked
    REQUIRE(probe->handler_calls > 0);
    REQUIRE(probe->pushed);

    // Review Focus 5: the impulse must reach the simulation, not merely be accepted. The box
    // starts at x == 0 and nothing else pushes it sideways.
    //
    // Why this survives to the next tick: the handler runs after step 4 has written
    // Transform = pose and recorded synced_pose_ = pose, so next tick's step 2 sees them
    // matching and does NOT teleport the body -- which would have reset the velocity the
    // impulse just gave it. If this test ever starts failing with the box unmoved, that
    // external-edit heuristic is the first thing to check.
    bool moved_in_x = false;
    world->for_each<RigidBody>(
        [&](mosaic::foundation::EntityID e, RigidBody& rb)
        {
            if (!rb.dynamic) { return; }
            const Transform* t = world->get_component<Transform>(e);
            if (t != nullptr && std::abs(t->position.x) > 0.1f) { moved_in_x = true; }
        });
    REQUIRE(moved_in_x);
}
