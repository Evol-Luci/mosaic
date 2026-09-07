#pragma once

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "engine/ecs/system.hpp"
#include "engine/particles/particle.hpp"
#include "engine/particles/particle_emitter.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::engine::particles
{
// Simulates ParticleEmitters: spawns (burst on activation + continuous by rate),
// integrates under the emitter's acceleration, and removes aged-out particles.
// Live buffers are transient state keyed by entity id (like the nav step cache),
// never serialized. Particles spawn at the entity's authored Transform.position
// (or the origin if it has none). Nothing is written back to any component.
class ParticleSystem : public ecs::System
{
public:
    void update(World& world, float dt) override
    {
        world_ = &world;

        std::unordered_set<std::uint64_t> seen;
        world.for_each<ParticleEmitter>(
            [&](mosaic::foundation::EntityID e, ParticleEmitter& em)
            {
                const std::uint64_t id = e.value();
                seen.insert(id);

                mosaic::foundation::math::Vec3 origin{};
                if (auto* tf = world.get_component<transform::Transform>(e))
                {
                    origin = tf->position;
                }

                // Spawn `burst` particles (shared by the on-sight burst and re-arms),
                // capped at max_particles.
                auto fire_burst = [&](EmitterState& s)
                {
                    for (int i = 0; i < em.burst &&
                         static_cast<int>(s.particles.size()) < em.max_particles;
                         ++i)
                    {
                        s.particles.push_back(spawn_particle(em, origin, s.rng));
                    }
                };

                auto it = states_.find(id);
                if (it == states_.end())
                {
                    EmitterState st;
                    st.rng.state = em.seed;
                    st.last_burst_trigger = em.burst_trigger; // counter must not also fire on creation
                    fire_burst(st);                            // one-shot burst on first sight
                    it = states_.emplace(id, std::move(st)).first;
                }
                EmitterState& st = it->second;

                // Re-arm: any change to burst_trigger fires exactly one more burst.
                if (em.burst_trigger != st.last_burst_trigger)
                {
                    fire_burst(st);
                    st.last_burst_trigger = em.burst_trigger;
                }

                if (em.emitting)
                {
                    st.accumulator += em.rate * dt;
                    while (st.accumulator >= 1.0f &&
                           static_cast<int>(st.particles.size()) < em.max_particles)
                    {
                        st.particles.push_back(spawn_particle(em, origin, st.rng));
                        st.accumulator -= 1.0f;
                    }
                }

                for (Particle& p : st.particles) { integrate(p, em.acceleration, dt); }

                st.particles.erase(
                    std::remove_if(st.particles.begin(), st.particles.end(),
                                   [](const Particle& p) { return p.age >= p.lifetime; }),
                    st.particles.end());
            });

        // Drop state for emitters that no longer exist.
        for (auto it = states_.begin(); it != states_.end();)
        {
            it = (seen.count(it->first) != 0) ? std::next(it) : states_.erase(it);
        }
    }

    // The live particle pool for `e`, or nullptr if it has none. Checks entity
    // validity so callers see nullptr immediately after the entity is destroyed
    // (deferred destruction commits at end-of-tick; is_valid returns false after
    // that point). For tests and the future renderer.
    const std::vector<Particle>* particles_for(mosaic::foundation::EntityID e) const
    {
        if (world_ && !world_->is_valid(e)) { return nullptr; }
        auto it = states_.find(e.value());
        return it == states_.end() ? nullptr : &it->second.particles;
    }

private:
    struct EmitterState
    {
        std::vector<Particle> particles;
        Rng                   rng;
        float                 accumulator = 0.0f;
        int                   last_burst_trigger = 0;
    };

    std::unordered_map<std::uint64_t, EmitterState> states_;
    const World* world_ = nullptr; // set on every update; used for post-destruction validity check
};
} // namespace mosaic::engine::particles
