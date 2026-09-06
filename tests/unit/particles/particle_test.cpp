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
