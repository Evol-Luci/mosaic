#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/particles/particle_emitter.hpp"
#include "engine/particles/particle_system.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"

using Catch::Approx;
using mosaic::engine::World;
using mosaic::engine::particles::ParticleEmitter;
using mosaic::engine::particles::ParticleSystem;
using mosaic::engine::transform::Transform;

TEST_CASE("ParticleSystem emits continuously at the configured rate", "[particles]")
{
    World world;
    auto& sys = world.add_system<ParticleSystem>();

    const auto e = world.create_entity();
    ParticleEmitter em;
    em.rate          = 10.0f;   // 10 per second
    em.lifetime      = 100.0f;  // long-lived, so none age out during the test
    em.acceleration  = mosaic::foundation::math::Vec3{}; // no gravity, keep it simple
    world.add_component<ParticleEmitter>(e, em);

    for (int i = 0; i < 10; ++i) { world.tick(0.1f); } // 1.0s total -> ~10 particles

    const auto* pool = sys.particles_for(e);
    REQUIRE(pool != nullptr);
    REQUIRE(pool->size() == 10);
}

TEST_CASE("ParticleSystem fires a one-shot burst on activation", "[particles]")
{
    World world;
    auto& sys = world.add_system<ParticleSystem>();

    const auto e = world.create_entity();
    ParticleEmitter em;
    em.emitting = false; // no continuous emission
    em.burst    = 5;
    em.lifetime = 100.0f;
    world.add_component<ParticleEmitter>(e, em);

    world.tick(0.1f);
    REQUIRE(sys.particles_for(e)->size() == 5);
    world.tick(0.1f); // burst does not re-fire
    REQUIRE(sys.particles_for(e)->size() == 5);
}

TEST_CASE("ParticleSystem removes particles once they age out", "[particles]")
{
    World world;
    auto& sys = world.add_system<ParticleSystem>();

    const auto e = world.create_entity();
    ParticleEmitter em;
    em.emitting = false;
    em.burst    = 3;
    em.lifetime = 0.25f;
    world.add_component<ParticleEmitter>(e, em);

    world.tick(0.1f);
    REQUIRE(sys.particles_for(e)->size() == 3);
    world.tick(0.1f); // age 0.2 < 0.25, still alive
    REQUIRE(sys.particles_for(e)->size() == 3);
    world.tick(0.1f); // age 0.3 >= 0.25, gone
    REQUIRE(sys.particles_for(e)->empty());
}

TEST_CASE("ParticleSystem never exceeds max_particles", "[particles]")
{
    World world;
    auto& sys = world.add_system<ParticleSystem>();

    const auto e = world.create_entity();
    ParticleEmitter em;
    em.rate          = 1000.0f;
    em.lifetime      = 100.0f;
    em.max_particles = 20;
    world.add_component<ParticleEmitter>(e, em);

    for (int i = 0; i < 50; ++i) { world.tick(0.1f); }
    REQUIRE(sys.particles_for(e)->size() == 20);
}

TEST_CASE("ParticleSystem spawns particles at the entity's Transform position", "[particles]")
{
    World world;
    auto& sys = world.add_system<ParticleSystem>();

    const auto e = world.create_entity();
    Transform tf;
    tf.position = mosaic::foundation::math::Vec3{4.0f, 5.0f, 6.0f};
    world.add_component<Transform>(e, tf);

    ParticleEmitter em;
    em.emitting         = false;
    em.burst            = 1;
    em.spread           = 0.0f;
    em.initial_velocity = mosaic::foundation::math::Vec3{}; // isolate position
    em.acceleration     = mosaic::foundation::math::Vec3{}; // no drift on spawn tick's integrate
    em.lifetime         = 100.0f;
    world.add_component<ParticleEmitter>(e, em);

    world.tick(0.0f); // dt 0: spawn, integrate is a no-op, position unchanged
    const auto& pool = *sys.particles_for(e);
    REQUIRE(pool.size() == 1);
    REQUIRE(pool[0].position.x == Approx(4.0f));
    REQUIRE(pool[0].position.y == Approx(5.0f));
    REQUIRE(pool[0].position.z == Approx(6.0f));
}

TEST_CASE("ParticleSystem drops the pool when the emitter entity is destroyed", "[particles]")
{
    World world;
    auto& sys = world.add_system<ParticleSystem>();

    const auto e = world.create_entity();
    ParticleEmitter em;
    em.burst    = 2;
    em.lifetime = 100.0f;
    world.add_component<ParticleEmitter>(e, em);

    world.tick(0.1f);
    REQUIRE(sys.particles_for(e) != nullptr);

    world.destroy_entity(e);
    world.tick(0.1f);
    REQUIRE(sys.particles_for(e) == nullptr);
}

TEST_CASE("ParticleSystem is bit-identical across two runs of the same schedule", "[particles]")
{
    const auto run = []()
    {
        World world;
        auto& sys = world.add_system<ParticleSystem>();
        const auto e = world.create_entity();
        ParticleEmitter em;
        em.rate     = 30.0f;
        em.spread   = 1.5f;
        em.seed     = 777ULL;
        em.lifetime = 0.5f;
        world.add_component<ParticleEmitter>(e, em);
        for (int i = 0; i < 20; ++i) { world.tick(0.05f); }
        return *sys.particles_for(e); // copy out the pool
    };

    const auto a = run();
    const auto b = run();
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        REQUIRE(a[i].position.x == b[i].position.x);
        REQUIRE(a[i].position.y == b[i].position.y);
        REQUIRE(a[i].position.z == b[i].position.z);
        REQUIRE(a[i].velocity.x == b[i].velocity.x);
        REQUIRE(a[i].velocity.y == b[i].velocity.y);
        REQUIRE(a[i].velocity.z == b[i].velocity.z);
        REQUIRE(a[i].age == b[i].age);
    }
}

TEST_CASE("ParticleSystem re-arms a burst when burst_trigger changes", "[particles]")
{
    World world;
    auto& sys = world.add_system<ParticleSystem>();

    const auto e = world.create_entity();
    ParticleEmitter em;
    em.emitting = false; // isolate burst behavior
    em.burst    = 5;
    em.lifetime = 100.0f;
    world.add_component<ParticleEmitter>(e, em);

    world.tick(0.1f);
    REQUIRE(sys.particles_for(e)->size() == 5); // on-sight burst

    world.get_component<ParticleEmitter>(e)->burst_trigger += 1;
    world.tick(0.1f);
    REQUIRE(sys.particles_for(e)->size() == 10); // re-armed burst
}

TEST_CASE("ParticleSystem does not re-fire a burst without a trigger change", "[particles]")
{
    World world;
    auto& sys = world.add_system<ParticleSystem>();

    const auto e = world.create_entity();
    ParticleEmitter em;
    em.emitting = false;
    em.burst    = 4;
    em.lifetime = 100.0f;
    world.add_component<ParticleEmitter>(e, em);

    world.tick(0.1f);
    world.tick(0.1f);
    world.tick(0.1f);
    REQUIRE(sys.particles_for(e)->size() == 4); // only the on-sight burst
}

TEST_CASE("ParticleSystem fires one burst per tick regardless of multiple trigger bumps", "[particles]")
{
    World world;
    auto& sys = world.add_system<ParticleSystem>();

    const auto e = world.create_entity();
    ParticleEmitter em;
    em.emitting = false;
    em.burst    = 3;
    em.lifetime = 100.0f;
    world.add_component<ParticleEmitter>(e, em);

    world.tick(0.1f);
    REQUIRE(sys.particles_for(e)->size() == 3);

    auto* comp = world.get_component<ParticleEmitter>(e);
    comp->burst_trigger += 1;
    comp->burst_trigger += 1;
    comp->burst_trigger += 1;
    world.tick(0.1f);
    REQUIRE(sys.particles_for(e)->size() == 6); // one extra burst, not three
}

TEST_CASE("ParticleSystem burst re-arm respects max_particles", "[particles]")
{
    World world;
    auto& sys = world.add_system<ParticleSystem>();

    const auto e = world.create_entity();
    ParticleEmitter em;
    em.emitting      = false;
    em.burst         = 8;
    em.max_particles = 10;
    em.lifetime      = 100.0f;
    world.add_component<ParticleEmitter>(e, em);

    world.tick(0.1f);
    REQUIRE(sys.particles_for(e)->size() == 8);

    world.get_component<ParticleEmitter>(e)->burst_trigger += 1;
    world.tick(0.1f);
    REQUIRE(sys.particles_for(e)->size() == 10); // capped, not 16
}

TEST_CASE("ParticleSystem burst re-arm is deterministic across runs", "[particles]")
{
    auto run = []
    {
        World world;
        auto& sys = world.add_system<ParticleSystem>();
        const auto e = world.create_entity();
        ParticleEmitter em;
        em.emitting = false;
        em.burst    = 4;
        em.spread   = 1.0f; // seeded jitter so buffers are non-trivial
        em.lifetime = 100.0f;
        em.seed     = 7;
        world.add_component<ParticleEmitter>(e, em);
        world.tick(0.1f);
        world.get_component<ParticleEmitter>(e)->burst_trigger += 1;
        world.tick(0.1f);
        return *sys.particles_for(e); // copy the pool
    };

    const auto a = run();
    const auto b = run();
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        REQUIRE(a[i].position.x == b[i].position.x);
        REQUIRE(a[i].velocity.x == b[i].velocity.x);
        REQUIRE(a[i].age == b[i].age);
    }
}

TEST_CASE("ParticleSystem interpolates a single continuous spawn to the path midpoint", "[particles]")
{
    World world;
    auto& sys = world.add_system<ParticleSystem>();

    const auto e = world.create_entity();
    Transform tf;
    tf.position = mosaic::foundation::math::Vec3{0.0f, 0.0f, 0.0f};
    world.add_component<Transform>(e, tf);

    ParticleEmitter em;
    em.emitting         = true;
    em.rate             = 10.0f; // rate*dt = 1 -> exactly one spawn per tick
    em.spread           = 0.0f;
    em.initial_velocity = mosaic::foundation::math::Vec3{}; // isolate position
    em.acceleration     = mosaic::foundation::math::Vec3{};
    em.lifetime         = 100.0f;
    world.add_component<ParticleEmitter>(e, em);

    world.tick(0.1f); // first tick: prev == curr == (0,0,0); one particle at x=0
    world.get_component<Transform>(e)->position = mosaic::foundation::math::Vec3{10.0f, 0.0f, 0.0f};
    world.tick(0.1f); // swept (0)->(10); the one continuous spawn lands at the midpoint x=5

    const auto& pool = *sys.particles_for(e);
    REQUIRE(pool.size() == 2);
    // The tick-2 particle is the only one with x>0; it must sit at the path midpoint, not at 10.
    float max_x = 0.0f;
    for (const auto& p : pool) { max_x = std::max(max_x, p.position.x); }
    REQUIRE(max_x == Approx(5.0f));
}

TEST_CASE("ParticleSystem spreads multiple continuous spawns along the path", "[particles]")
{
    World world;
    auto& sys = world.add_system<ParticleSystem>();

    const auto e = world.create_entity();
    Transform tf;
    tf.position = mosaic::foundation::math::Vec3{0.0f, 0.0f, 0.0f};
    world.add_component<Transform>(e, tf);

    ParticleEmitter em;
    em.emitting         = true;
    em.rate             = 30.0f; // 3 spawns per 0.1s tick
    em.spread           = 0.0f;
    em.initial_velocity = mosaic::foundation::math::Vec3{};
    em.acceleration     = mosaic::foundation::math::Vec3{};
    em.lifetime         = 100.0f;
    world.add_component<ParticleEmitter>(e, em);

    world.tick(0.1f); // 3 particles at x=0 (first tick, no interpolation)
    world.get_component<Transform>(e)->position = mosaic::foundation::math::Vec3{10.0f, 0.0f, 0.0f};
    world.tick(0.1f); // 3 more spread along (0)->(10) at f = 0.5/3, 1.5/3, 2.5/3

    const auto& pool = *sys.particles_for(e);
    REQUIRE(pool.size() == 6);
    // Collect the tick-2 particles (x > 0) and check they are the spread {5/3, 5, 25/3}.
    std::vector<float> xs;
    for (const auto& p : pool) { if (p.position.x > 0.001f) { xs.push_back(p.position.x); } }
    std::sort(xs.begin(), xs.end());
    REQUIRE(xs.size() == 3);
    REQUIRE(xs[0] == Approx(5.0f / 3.0f));
    REQUIRE(xs[1] == Approx(5.0f));
    REQUIRE(xs[2] == Approx(25.0f / 3.0f));
}

TEST_CASE("ParticleSystem re-armed burst is not path-interpolated", "[particles]")
{
    World world;
    auto& sys = world.add_system<ParticleSystem>();

    const auto e = world.create_entity();
    Transform tf;
    tf.position = mosaic::foundation::math::Vec3{0.0f, 0.0f, 0.0f};
    world.add_component<Transform>(e, tf);

    ParticleEmitter em;
    em.emitting         = false; // burst-only, isolate burst behavior
    em.burst            = 3;
    em.spread           = 0.0f;
    em.initial_velocity = mosaic::foundation::math::Vec3{};
    em.acceleration     = mosaic::foundation::math::Vec3{};
    em.lifetime         = 100.0f;
    world.add_component<ParticleEmitter>(e, em);

    world.tick(0.1f); // on-sight burst of 3 at x=0
    world.get_component<Transform>(e)->position = mosaic::foundation::math::Vec3{10.0f, 0.0f, 0.0f};
    world.get_component<ParticleEmitter>(e)->burst_trigger += 1;
    world.tick(0.1f); // re-armed burst: all 3 at the current origin x=10, NOT spread

    const auto& pool = *sys.particles_for(e);
    REQUIRE(pool.size() == 6);
    int at_ten = 0;
    for (const auto& p : pool) { if (p.position.x == Approx(10.0f)) { ++at_ten; } }
    REQUIRE(at_ten == 3); // the whole re-armed burst spawned at the current origin
}

TEST_CASE("ParticleSystem path interpolation is deterministic under motion", "[particles]")
{
    auto run = []
    {
        World world;
        auto& sys = world.add_system<ParticleSystem>();
        const auto e = world.create_entity();
        Transform tf;
        tf.position = mosaic::foundation::math::Vec3{0.0f, 0.0f, 0.0f};
        world.add_component<Transform>(e, tf);
        ParticleEmitter em;
        em.emitting = true;
        em.rate     = 30.0f;
        em.spread   = 1.0f; // seeded jitter so buffers are non-trivial
        em.lifetime = 100.0f;
        em.seed     = 5;
        world.add_component<ParticleEmitter>(e, em);
        world.tick(0.1f);
        world.get_component<Transform>(e)->position = mosaic::foundation::math::Vec3{7.0f, 3.0f, 0.0f};
        world.tick(0.1f);
        return *sys.particles_for(e);
    };
    const auto a = run();
    const auto b = run();
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        REQUIRE(a[i].position.x == b[i].position.x);
        REQUIRE(a[i].position.y == b[i].position.y);
        REQUIRE(a[i].velocity.x == b[i].velocity.x);
    }
}

#include "engine/reflection/builtin_components.hpp"
#include "foundation/reflection/type_registry.hpp"

TEST_CASE("ParticleEmitter is a registered built-in component", "[particles]")
{
    mosaic::foundation::reflection::TypeRegistry     registry;
    mosaic::engine::reflection::ComponentReflection  types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    REQUIRE(types.has("ParticleEmitter"));
}
