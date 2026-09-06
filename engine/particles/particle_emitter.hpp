#pragma once

#include <cstdint>

#include "engine/particles/particle.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::engine::particles
{
// Authored parameters for an entity that emits particles. Reflected + serialized;
// the live particle buffer lives in ParticleSystem, not here.
struct ParticleEmitter
{
    bool          emitting         = true;                       // continuous emission on/off
    float         rate             = 10.0f;                      // particles per second
    int           burst            = 0;                          // one-shot count on activation
    float         lifetime         = 1.0f;                       // seconds each particle lives
    Vec3          initial_velocity = Vec3{0.0f, 1.0f, 0.0f};     // base spawn velocity
    float         spread           = 0.0f;                       // per-axis symmetric jitter
    Vec3          acceleration     = Vec3{0.0f, -9.81f, 0.0f};   // constant per-particle accel
    std::uint64_t seed             = 1;                          // PRNG seed (determinism knob)
    int           max_particles    = 1000;                       // pool cap (safety)
    Vec3          color            = Vec3{1.0f, 1.0f, 1.0f};     // base particle color
    float         size             = 0.1f;                       // world-space billboard edge length
};

template <class V>
void reflect(V& visitor, ParticleEmitter& e)
{
    visitor.member("emitting", e.emitting);
    visitor.member("rate", e.rate);
    visitor.member("burst", e.burst);
    visitor.member("lifetime", e.lifetime);
    visitor.member("initial_velocity", e.initial_velocity);
    visitor.member("spread", e.spread);
    visitor.member("acceleration", e.acceleration);
    visitor.member("seed", e.seed);
    visitor.member("max_particles", e.max_particles);
    visitor.member("color", e.color);
    visitor.member("size", e.size);
}

// Create one particle at `origin` with a seeded per-axis velocity jitter.
inline Particle spawn_particle(const ParticleEmitter& e, const Vec3& origin, Rng& rng)
{
    const auto jitter = [&](float base)
    { return base + e.spread * (2.0f * rng.next_unit() - 1.0f); };

    Particle p;
    p.position = origin;
    p.velocity = Vec3{jitter(e.initial_velocity.x), jitter(e.initial_velocity.y),
                      jitter(e.initial_velocity.z)};
    p.age      = 0.0f;
    p.lifetime = e.lifetime;
    return p;
}
} // namespace mosaic::engine::particles
