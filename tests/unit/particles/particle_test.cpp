#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/particles/particle.hpp"

using Catch::Approx;
using mosaic::engine::particles::integrate;
using mosaic::engine::particles::Particle;
using mosaic::engine::particles::Rng;
using mosaic::foundation::math::Vec3;

TEST_CASE("Rng is deterministic for a given seed", "[particles]")
{
    Rng a{42};
    Rng b{42};
    for (int i = 0; i < 5; ++i) { REQUIRE(a.next_unit() == b.next_unit()); }

    Rng c{43};
    Rng d{42};
    REQUIRE(c.next_unit() != d.next_unit());
}

TEST_CASE("Rng values are non-negative and less than one", "[particles]")
{
    Rng r{7};
    for (int i = 0; i < 1000; ++i)
    {
        const float u = r.next_unit();
        REQUIRE(u >= 0.0f);
        REQUIRE(u < 1.0f);
    }
}

TEST_CASE("integrate advances velocity, position, and age under constant accel", "[particles]")
{
    Particle p;
    p.position = Vec3{0.0f, 0.0f, 0.0f};
    p.velocity = Vec3{1.0f, 0.0f, 0.0f};
    p.lifetime = 10.0f;
    const Vec3 accel{0.0f, -10.0f, 0.0f};

    integrate(p, accel, 0.5f); // v += a*dt -> (1,-5,0); pos += v*dt -> (0.5,-2.5,0); age 0.5
    REQUIRE(p.velocity.x == Approx(1.0f));
    REQUIRE(p.velocity.y == Approx(-5.0f));
    REQUIRE(p.position.x == Approx(0.5f));
    REQUIRE(p.position.y == Approx(-2.5f));
    REQUIRE(p.age == Approx(0.5f));

    integrate(p, accel, 0.5f); // v -> (1,-10,0); pos -> (1.0,-7.5,0); age 1.0
    REQUIRE(p.velocity.y == Approx(-10.0f));
    REQUIRE(p.position.y == Approx(-7.5f));
    REQUIRE(p.age == Approx(1.0f));
}

#include "engine/particles/particle_emitter.hpp"
#include "foundation/serialization/reflect_value.hpp"

using mosaic::engine::particles::ParticleEmitter;
using mosaic::engine::particles::spawn_particle;

TEST_CASE("spawn_particle with zero spread uses the base velocity exactly", "[particles]")
{
    ParticleEmitter e;
    e.initial_velocity = Vec3{0.0f, 2.0f, 0.0f};
    e.spread           = 0.0f;
    e.lifetime         = 3.0f;
    Rng rng{1};

    const Particle p = spawn_particle(e, Vec3{5.0f, 0.0f, -1.0f}, rng);
    REQUIRE(p.position.x == Approx(5.0f));
    REQUIRE(p.position.z == Approx(-1.0f));
    REQUIRE(p.velocity.x == Approx(0.0f));
    REQUIRE(p.velocity.y == Approx(2.0f));
    REQUIRE(p.age == Approx(0.0f));
    REQUIRE(p.lifetime == Approx(3.0f));
}

TEST_CASE("spawn_particle keeps each velocity axis within +/- spread", "[particles]")
{
    ParticleEmitter e;
    e.initial_velocity = Vec3{0.0f, 0.0f, 0.0f};
    e.spread           = 2.0f;
    Rng rng{99};

    for (int i = 0; i < 500; ++i)
    {
        const Particle p = spawn_particle(e, Vec3{}, rng);
        REQUIRE(p.velocity.x >= -2.0f);
        REQUIRE(p.velocity.x <= 2.0f);
        REQUIRE(p.velocity.y >= -2.0f);
        REQUIRE(p.velocity.y <= 2.0f);
        REQUIRE(p.velocity.z >= -2.0f);
        REQUIRE(p.velocity.z <= 2.0f);
    }
}

TEST_CASE("ParticleEmitter round-trips through reflection", "[particles]")
{
    ParticleEmitter e;
    e.emitting         = false;
    e.rate             = 25.0f;
    e.burst            = 8;
    e.lifetime         = 4.5f;
    e.initial_velocity = Vec3{1.0f, 2.0f, 3.0f};
    e.spread           = 0.75f;
    e.acceleration     = Vec3{0.0f, -3.0f, 0.0f};
    e.seed             = 12345ULL;
    e.max_particles    = 64;
    e.color            = Vec3{0.2f, 0.4f, 0.6f};
    e.size             = 0.35f;
    e.end_color        = Vec3{0.9f, 0.1f, 0.2f};
    e.end_size         = 0.05f;
    e.alpha            = 0.8f;
    e.end_alpha        = 0.25f;

    const auto value = mosaic::foundation::serialization::to_value(e);
    ParticleEmitter back;
    mosaic::foundation::serialization::from_value(value, back);

    REQUIRE(back.emitting == false);
    REQUIRE(back.rate == Approx(25.0f));
    REQUIRE(back.burst == 8);
    REQUIRE(back.lifetime == Approx(4.5f));
    REQUIRE(back.initial_velocity.y == Approx(2.0f));
    REQUIRE(back.spread == Approx(0.75f));
    REQUIRE(back.acceleration.y == Approx(-3.0f));
    REQUIRE(back.seed == 12345ULL);
    REQUIRE(back.max_particles == 64);
    REQUIRE(back.color.x == Approx(0.2f));
    REQUIRE(back.color.y == Approx(0.4f));
    REQUIRE(back.color.z == Approx(0.6f));
    REQUIRE(back.size == Approx(0.35f));
    REQUIRE(back.end_color.x == Approx(0.9f));
    REQUIRE(back.end_color.y == Approx(0.1f));
    REQUIRE(back.end_color.z == Approx(0.2f));
    REQUIRE(back.end_size == Approx(0.05f));
    REQUIRE(back.alpha == Approx(0.8f));
    REQUIRE(back.end_alpha == Approx(0.25f));
}

using mosaic::engine::particles::alpha_at;
using mosaic::engine::particles::color_at;
using mosaic::engine::particles::size_at;

TEST_CASE("over-life helpers return start at t=0 and end at t=1", "[particles]")
{
    ParticleEmitter e;
    e.color = Vec3{0.2f, 0.4f, 0.6f};
    e.end_color = Vec3{0.8f, 0.1f, 0.0f};
    e.size = 0.25f;
    e.end_size = 1.0f;
    e.alpha = 1.0f;
    e.end_alpha = 0.0f;

    REQUIRE(color_at(e, 0.0f).x == Approx(0.2f));
    REQUIRE(color_at(e, 1.0f).x == Approx(0.8f));
    REQUIRE(size_at(e, 0.0f) == Approx(0.25f));
    REQUIRE(size_at(e, 1.0f) == Approx(1.0f));
    REQUIRE(alpha_at(e, 0.0f) == Approx(1.0f));
    REQUIRE(alpha_at(e, 1.0f) == Approx(0.0f));
}

TEST_CASE("over-life helpers interpolate linearly at the midpoint", "[particles]")
{
    ParticleEmitter e;
    e.color = Vec3{0.0f, 0.0f, 0.0f};
    e.end_color = Vec3{1.0f, 1.0f, 1.0f};
    e.size = 0.0f;
    e.end_size = 2.0f;
    e.alpha = 1.0f;
    e.end_alpha = 0.0f;

    REQUIRE(color_at(e, 0.5f).y == Approx(0.5f));
    REQUIRE(size_at(e, 0.5f) == Approx(1.0f));
    REQUIRE(alpha_at(e, 0.5f) == Approx(0.5f));
}

TEST_CASE("over-life helpers clamp t to [0,1] (no extrapolation)", "[particles]")
{
    ParticleEmitter e;
    e.color = Vec3{0.2f, 0.2f, 0.2f};
    e.end_color = Vec3{0.8f, 0.8f, 0.8f};
    e.size = 0.25f;
    e.end_size = 1.0f;
    e.alpha = 1.0f;
    e.end_alpha = 0.0f;

    REQUIRE(color_at(e, -1.0f).x == Approx(0.2f)); // clamps to start
    REQUIRE(color_at(e, 2.0f).x == Approx(0.8f));  // clamps to end
    REQUIRE(size_at(e, -1.0f) == Approx(0.25f));
    REQUIRE(size_at(e, 2.0f) == Approx(1.0f));
    REQUIRE(alpha_at(e, 5.0f) == Approx(0.0f));
}

TEST_CASE("default emitter reproduces the previous constant/fade behavior", "[particles]")
{
    ParticleEmitter e; // all defaults
    // Constant color and size across life:
    REQUIRE(color_at(e, 0.0f).x == Approx(1.0f));
    REQUIRE(color_at(e, 1.0f).x == Approx(1.0f));
    REQUIRE(size_at(e, 0.0f) == Approx(0.1f));
    REQUIRE(size_at(e, 1.0f) == Approx(0.1f));
    // Alpha fades 1 -> 0 linearly (the old hardcoded renderer fade):
    REQUIRE(alpha_at(e, 0.0f) == Approx(1.0f));
    REQUIRE(alpha_at(e, 0.25f) == Approx(0.75f));
    REQUIRE(alpha_at(e, 1.0f) == Approx(0.0f));
}
