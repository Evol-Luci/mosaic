#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "engine/physics/physics_backend.hpp"
#include "modules/physics_box3d/box3d_backend.hpp"

using mosaic::engine::physics::BodyDef;
using mosaic::engine::physics::BodyHandle;
using mosaic::engine::physics::BodyType;
using mosaic::engine::physics::ContactPhase;
using mosaic::engine::physics::WorldDef;
using mosaic::foundation::math::Vec3;
using mosaic::physics::box3d::create_box3d_backend;

namespace
{
bool is_pair(BodyHandle a, BodyHandle b, BodyHandle x, BodyHandle y)
{
    return (a == x && b == y) || (a == y && b == x);
}
} // namespace

TEST_CASE("backend reports a begin-touch contact when a box lands on the ground", "[physics]")
{
    auto backend = create_box3d_backend(WorldDef{});

    BodyDef ground;
    ground.type = BodyType::Static;
    ground.position = Vec3{0, 0, 0};
    ground.shape.half_extents = Vec3{5, 0.5f, 5};
    const BodyHandle gh = backend->create_body(ground);

    BodyDef box;
    box.type = BodyType::Dynamic;
    box.position = Vec3{0, 3, 0};
    box.shape.half_extents = Vec3{0.5f, 0.5f, 0.5f};
    const BodyHandle bh = backend->create_body(box);

    bool begin_seen = false;
    mosaic::engine::physics::BodyContact hit{};
    for (int i = 0; i < 180; ++i)
    {
        backend->step(1.0f / 60.0f, 4);
        for (const auto& c : backend->contact_events())
        {
            if (c.phase == ContactPhase::Begin && is_pair(c.a, c.b, gh, bh))
            {
                begin_seen = true;
                hit = c;
            }
        }
    }
    REQUIRE(begin_seen);

    // Manifold detail: a box landing flat on the ground has a vertical, unit-length normal, is
    // barely separated, and a finite contact point. (The A/B ordering — hence the normal's sign —
    // is a Box3D internal, so we assert |normal.y|, not its sign.)
    const float nlen = std::sqrt(hit.normal.x * hit.normal.x + hit.normal.y * hit.normal.y +
                                 hit.normal.z * hit.normal.z);
    REQUIRE(nlen > 0.99f);
    REQUIRE(nlen < 1.01f);
    REQUIRE(std::abs(hit.normal.y) > 0.9f);
    REQUIRE(std::abs(hit.separation) < 0.5f);
    REQUIRE(std::isfinite(hit.point.x));
    REQUIRE(std::isfinite(hit.point.y));
    REQUIRE(std::isfinite(hit.point.z));
}

#include <algorithm>
#include <tuple>
#include <vector>

#include "engine/physics/physics_components.hpp"
#include "engine/physics/physics_system.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"

using mosaic::engine::World;
using mosaic::engine::physics::Collider;
using mosaic::engine::physics::ContactEvent;
using mosaic::engine::physics::PhysicsSystem;
using mosaic::engine::physics::RigidBody;
using mosaic::engine::transform::Transform;
using mosaic::foundation::EntityID;
using mosaic::foundation::math::Quat;

namespace
{
bool is_entity_pair(const ContactEvent& c, EntityID x, EntityID y)
{
    return (c.a == x && c.b == y) || (c.a == y && c.b == x);
}
} // namespace

TEST_CASE("PhysicsSystem emits a ContactEvent when a box lands on the ground", "[physics]")
{
    World world;
    world.add_system<PhysicsSystem>(create_box3d_backend(WorldDef{}));

    std::vector<ContactEvent> events;
    world.events().subscribe<ContactEvent>([&](const ContactEvent& c) { events.push_back(c); });

    const auto ground = world.create_entity();
    world.add_component<Transform>(ground, Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{10, 1, 10}, {}});
    world.add_component<RigidBody>(ground, RigidBody{false, 1.0f});
    world.add_component<Collider>(ground, Collider{Vec3{5.0f, 0.5f, 5.0f}});

    const auto box = world.create_entity();
    world.add_component<Transform>(box, Transform{Vec3{0, 3, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(box, RigidBody{true, 1.0f});
    world.add_component<Collider>(box, Collider{Vec3{0.5f, 0.5f, 0.5f}});

    for (int i = 0; i < 180; ++i)
    {
        world.tick(1.0f / 60.0f);
    }

    bool begin_seen = false;
    ContactEvent begin_evt{};
    for (const auto& c : events)
    {
        if (c.phase == ContactPhase::Begin && is_entity_pair(c, box, ground))
        {
            begin_seen = true;
            begin_evt = c;
        }
    }
    REQUIRE(begin_seen);
    // The manifold normal propagated to the entity-level event (vertical for a box on the ground).
    REQUIRE(std::abs(begin_evt.normal.y) > 0.9f);

    // Move the box far away -> it separates from the ground -> an End contact fires.
    events.clear();
    world.get_component<Transform>(box)->position = Vec3{0, 50, 0};
    for (int i = 0; i < 30; ++i)
    {
        world.tick(1.0f / 60.0f);
    }
    bool end_seen = false;
    for (const auto& c : events)
    {
        if (c.phase == ContactPhase::End && is_entity_pair(c, box, ground))
        {
            end_seen = true;
        }
    }
    REQUIRE(end_seen);
}

namespace
{
// Record the emitted contact stream (phase + order-normalized entity pair) over a scenario.
std::vector<std::tuple<ContactPhase, std::uint64_t, std::uint64_t>> record_contacts()
{
    World world;
    world.add_system<PhysicsSystem>(create_box3d_backend(WorldDef{}));

    std::vector<std::tuple<ContactPhase, std::uint64_t, std::uint64_t>> seq;
    world.events().subscribe<ContactEvent>(
        [&](const ContactEvent& c)
        {
            const std::uint64_t lo = std::min(c.a.value(), c.b.value());
            const std::uint64_t hi = std::max(c.a.value(), c.b.value());
            seq.emplace_back(c.phase, lo, hi);
        });

    const auto ground = world.create_entity();
    world.add_component<Transform>(ground, Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{10, 1, 10}, {}});
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

    for (int i = 0; i < 180; ++i)
    {
        world.tick(1.0f / 60.0f);
    }
    return seq;
}
} // namespace

TEST_CASE("the contact-event stream is deterministic across runs", "[physics]")
{
    REQUIRE(record_contacts() == record_contacts());
    REQUIRE_FALSE(record_contacts().empty()); // the scenario actually produced contacts
}
