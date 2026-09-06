#pragma once

#include <cstdint>

#include "foundation/math/vec3.hpp"

namespace mosaic::engine::particles
{
using mosaic::foundation::math::Vec3;

// One simulated particle. Alive iff age < lifetime.
struct Particle
{
    Vec3  position;
    Vec3  velocity;
    float age      = 0.0f;
    float lifetime = 0.0f;
};

// A seeded splitmix64 PRNG. Deterministic across runs and platforms: same seed
// yields the same sequence. Used for spawn jitter so particle sims stay
// bit-identical (no std::rand(), no global state).
struct Rng
{
    std::uint64_t state = 0;

    // Returns a value in [0, 1).
    float next_unit()
    {
        state += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        z = z ^ (z >> 31);
        // Top 24 bits -> [0, 1) with a 2^-24 step (exactly representable in float).
        return static_cast<float>(z >> 40) * (1.0f / 16777216.0f);
    }
};

// Semi-implicit Euler step: velocity first, then position, then age.
inline void integrate(Particle& p, const Vec3& accel, float dt)
{
    p.velocity = p.velocity + accel * dt;
    p.position = p.position + p.velocity * dt;
    p.age += dt;
}
} // namespace mosaic::engine::particles
