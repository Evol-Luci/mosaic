# Particles (emitter + deterministic sim) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a CPU particle subsystem — attach a `ParticleEmitter` to an entity and a `ParticleSystem` spawns, ages, and integrates particles each tick, bit-identically across runs.

**Architecture:** Follows the Phase-4 shape (animation → sequencer → navigation): a neutral pure-C++ model (`particle.hpp`) + a reflected component (`particle_emitter.hpp`) + a generic `System` (`particle_system.hpp`), header-only under `engine/particles/`. Live particle buffers are transient system-owned state keyed by entity id (like the nav step-target cache) — **not serialized**; only the emitter's authored parameters serialize. Determinism comes from a per-emitter seeded PRNG plus a carried fractional emission accumulator.

**Tech Stack:** C++20, header-only engine headers, Catch2 (FetchContent) for tests, CMake presets.

**Spec:** `docs/superpowers/specs/2026-09-06-particles-design.md`

## Global Constraints

- Language: **C++20**; engine headers are **header-only** (`#pragma once`, all logic inline).
- Namespaces: model/component/system live in `mosaic::engine::particles`.
- Math type: `mosaic::foundation::math::Vec3` (fields `x`, `y`, `z`; `operator+`, `operator-`, `operator*(Vec3,float)` available; reflected).
- **Determinism is normative:** no `std::rand()`, no global RNG, no wall-clock. Same tick schedule ⇒ bit-identical per-entity particle buffers.
- Reflection: components expose `template <class V> void reflect(V& visitor, T&)` using `visitor.member("name", field)`; scalar field types in use elsewhere: `bool`, `int`, `float`, `std::uint64_t`, and reflected `Vec3`.
- Component registration: add to `engine/reflection/builtin_components.hpp` via `types.register_component<T>(registry, "Name")`.
- Test build: `cmake --build --preset debug`; run a tag with `./build/debug/tests/mosaic_unit_tests "[particles]"`; full suite `ctest --preset debug`.
- Catch2 test-case names must not start with `--` (breaks ctest discovery).
- New test sources must be listed in `tests/CMakeLists.txt`.
- Tag every new test `[particles]`.

---

### Task 1: Neutral particle model — `Rng` + `Particle` + `integrate`

**Files:**
- Create: `engine/particles/particle.hpp`
- Test: `tests/unit/particles/particle_test.cpp`
- Modify: `tests/CMakeLists.txt` (add the new test source)

**Interfaces:**
- Consumes: `mosaic::foundation::math::Vec3` from `foundation/math/vec3.hpp`.
- Produces:
  - `struct mosaic::engine::particles::Particle { Vec3 position; Vec3 velocity; float age = 0.0f; float lifetime = 0.0f; };`
  - `struct mosaic::engine::particles::Rng { std::uint64_t state = 0; float next_unit(); };` — advances `state` (splitmix64) and returns a value in `[0, 1)`.
  - `void mosaic::engine::particles::integrate(Particle& p, const Vec3& accel, float dt);`

- [ ] **Step 1: Write the failing test**

Create `tests/unit/particles/particle_test.cpp`:

```cpp
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

TEST_CASE("Rng values fall in [0, 1)", "[particles]")
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
```

- [ ] **Step 2: Add the test source to CMake**

In `tests/CMakeLists.txt`, add after the `unit/navigation/navigation_system_test.cpp` line:

```cmake
  unit/particles/particle_test.cpp
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build --preset debug`
Expected: FAIL — `engine/particles/particle.hpp` does not exist (compile error).

- [ ] **Step 4: Write minimal implementation**

Create `engine/particles/particle.hpp`:

```cpp
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
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[particles]"`
Expected: PASS (3 test cases).

- [ ] **Step 6: Commit**

```bash
git add engine/particles/particle.hpp tests/unit/particles/particle_test.cpp tests/CMakeLists.txt
git commit -m "feat(particles): neutral Particle model + seeded Rng + integrate"
```

---

### Task 2: `ParticleEmitter` component + `spawn_particle`

**Files:**
- Create: `engine/particles/particle_emitter.hpp`
- Test: `tests/unit/particles/particle_test.cpp` (append cases)

**Interfaces:**
- Consumes: `Particle`, `Rng` from `engine/particles/particle.hpp`; `Vec3`.
- Produces:
  - `struct mosaic::engine::particles::ParticleEmitter` with fields `bool emitting = true;`, `float rate = 10.0f;`, `int burst = 0;`, `float lifetime = 1.0f;`, `Vec3 initial_velocity{0.0f, 1.0f, 0.0f};`, `float spread = 0.0f;`, `Vec3 acceleration{0.0f, -9.81f, 0.0f};`, `std::uint64_t seed = 1;`, `int max_particles = 1000;` — reflected.
  - `Particle mosaic::engine::particles::spawn_particle(const ParticleEmitter& e, const Vec3& origin, Rng& rng);` — `position = origin`, per-axis `velocity = initial_velocity + spread * (2*next_unit()-1)`, `age = 0`, `lifetime = e.lifetime`.

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/particles/particle_test.cpp`:

```cpp
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
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset debug`
Expected: FAIL — `engine/particles/particle_emitter.hpp` does not exist.

- [ ] **Step 3: Write minimal implementation**

Create `engine/particles/particle_emitter.hpp`:

```cpp
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
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[particles]"`
Expected: PASS (6 test cases total).

- [ ] **Step 5: Commit**

```bash
git add engine/particles/particle_emitter.hpp tests/unit/particles/particle_test.cpp
git commit -m "feat(particles): reflected ParticleEmitter component + spawn_particle"
```

---

### Task 3: `ParticleSystem` — spawn / integrate / age-out / prune

**Files:**
- Create: `engine/particles/particle_system.hpp`
- Test: `tests/unit/particles/particle_system_test.cpp`
- Modify: `tests/CMakeLists.txt` (add the new test source)

**Interfaces:**
- Consumes: `Particle`, `Rng`, `integrate` from `particle.hpp`; `ParticleEmitter`, `spawn_particle` from `particle_emitter.hpp`; `mosaic::engine::ecs::System`; `mosaic::engine::World` (`for_each<T>((EntityID, T&))`, `get_component<T>(EntityID)`); `mosaic::engine::transform::Transform` (`.position` is a `Vec3`); `mosaic::foundation::EntityID` (`.value()` → `std::uint64_t`).
- Produces:
  - `class mosaic::engine::particles::ParticleSystem : public ecs::System` with `void update(World&, float dt) override;` and a query seam `const std::vector<Particle>* particles_for(mosaic::foundation::EntityID e) const;`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/particles/particle_system_test.cpp`:

```cpp
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
```

- [ ] **Step 2: Add the test source to CMake**

In `tests/CMakeLists.txt`, add after the `unit/particles/particle_test.cpp` line:

```cmake
  unit/particles/particle_system_test.cpp
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build --preset debug`
Expected: FAIL — `engine/particles/particle_system.hpp` does not exist.

- [ ] **Step 4: Write minimal implementation**

Create `engine/particles/particle_system.hpp`:

```cpp
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

                auto it = states_.find(id);
                if (it == states_.end())
                {
                    EmitterState st;
                    st.rng.state = em.seed;
                    // One-shot burst on first sight.
                    for (int i = 0; i < em.burst && static_cast<int>(st.particles.size()) <
                                        em.max_particles;
                         ++i)
                    {
                        st.particles.push_back(spawn_particle(em, origin, st.rng));
                    }
                    it = states_.emplace(id, std::move(st)).first;
                }
                EmitterState& st = it->second;

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

    // The live particle pool for `e`, or nullptr if it has none. For tests and the
    // future renderer.
    const std::vector<Particle>* particles_for(mosaic::foundation::EntityID e) const
    {
        auto it = states_.find(e.value());
        return it == states_.end() ? nullptr : &it->second.particles;
    }

private:
    struct EmitterState
    {
        std::vector<Particle> particles;
        Rng                   rng;
        float                 accumulator = 0.0f;
    };

    std::unordered_map<std::uint64_t, EmitterState> states_;
};
} // namespace mosaic::engine::particles
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[particles]"`
Expected: PASS (13 test cases total across both files).

- [ ] **Step 6: Commit**

```bash
git add engine/particles/particle_system.hpp tests/unit/particles/particle_system_test.cpp tests/CMakeLists.txt
git commit -m "feat(particles): ParticleSystem (spawn/integrate/age-out/prune, deterministic)"
```

---

### Task 4: Register the component + wire into `setup_runtime`

**Files:**
- Modify: `engine/reflection/builtin_components.hpp` (add include + registration)
- Modify: `app/run/demo_scene.hpp` (add `#include` + `add_system` in `setup_runtime`)

**Interfaces:**
- Consumes: `ParticleEmitter` (registration), `ParticleSystem` (system add).
- Produces: `"ParticleEmitter"` is a registered built-in component; `ParticleSystem` runs each tick under `setup_runtime`.

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/particles/particle_system_test.cpp`:

```cpp
#include "engine/reflection/builtin_components.hpp"
#include "foundation/reflection/type_registry.hpp"

TEST_CASE("ParticleEmitter is a registered built-in component", "[particles]")
{
    mosaic::foundation::reflection::TypeRegistry     registry;
    mosaic::engine::reflection::ComponentReflection  types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    REQUIRE(types.has("ParticleEmitter"));
}
```

(`ComponentReflection::has(std::string_view) const` is the membership predicate — verified in `engine/reflection/component_reflection.hpp`.)

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[particles]"`
Expected: FAIL — `"ParticleEmitter"` is not registered.

- [ ] **Step 3: Register the component**

In `engine/reflection/builtin_components.hpp`, add the include alongside the other engine component includes:

```cpp
#include "engine/particles/particle_emitter.hpp"
```

and add this line after the `NavAgent` registration inside `register_builtin_components`:

```cpp
    types.register_component<mosaic::engine::particles::ParticleEmitter>(registry, "ParticleEmitter");
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[particles]"`
Expected: PASS (14 test cases).

- [ ] **Step 5: Wire the system into `setup_runtime`**

In `app/run/demo_scene.hpp`, add the include near the other engine subsystem includes:

```cpp
#include "engine/particles/particle_system.hpp"
```

and inside `setup_runtime`, add this line immediately after the `SequenceSystem` add (before the `PhysicsSystem` add):

```cpp
    // Particles: spawn/age transient particles per emitter (reads Transform, writes nothing back).
    world.add_system<mosaic::engine::particles::ParticleSystem>();
```

- [ ] **Step 6: Build the whole project to verify the wiring compiles**

Run: `cmake --build --preset debug`
Expected: SUCCESS (app + tests compile).

- [ ] **Step 7: Run the full suite**

Run: `ctest --preset debug`
Expected: all tests pass (prior count + 14 new `[particles]` cases).

- [ ] **Step 8: Commit**

```bash
git add engine/reflection/builtin_components.hpp app/run/demo_scene.hpp tests/unit/particles/particle_system_test.cpp
git commit -m "feat(particles): register ParticleEmitter + wire ParticleSystem into setup_runtime"
```

---

### Task 5: Update the next-steps handoff

**Files:**
- Modify: `docs/project/next-steps.md`

**Interfaces:** none (documentation).

Per the project convention, the next-steps update rides on this feature branch (no separate PR). Record the particles slice as done and point at what comes next.

- [ ] **Step 1: Update the Phase 4 status line**

In `docs/project/next-steps.md`, in the Phase 4 bullet, mark **Particles — done** with a one-paragraph summary mirroring the navigation/sequencer entries: neutral `Particle`/`Rng`/`integrate` model + reflected `ParticleEmitter` + generic `ParticleSystem` (burst + rate emission, seeded-deterministic spread, constant acceleration, age-out, `max_particles` cap, transient per-emitter pools keyed by entity, pruned on destroy), wired into `setup_runtime`, headless & bit-identical deterministic; link the spec `../superpowers/specs/2026-09-06-particles-design.md`. Note the deferred follow-ups (rendering, color/size-over-life curves, sub-frame spawn interpolation, triggered/re-armed bursts, script-driven emission, a demo emitter). Update the "Remaining Phase-4 subsystems" list to drop particles (leaving audio, VFX, UI, networking) and bump the **Last updated** date to 2026-09-06.

- [ ] **Step 2: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "docs(next-steps): particles subsystem complete"
```

---

## Self-Review

**1. Spec coverage:**
- Neutral model (`Particle`, `Rng`, `integrate`) → Task 1. ✓
- Reflected `ParticleEmitter` (all 9 fields) + `spawn_particle` → Task 2. ✓
- `ParticleSystem` (burst on activation, rate accumulator, integrate, age-out, `max_particles` cap, spawn at Transform origin, prune on destroy, `particles_for` seam) → Task 3. ✓
- Determinism (seeded RNG + carried accumulator, bit-identical per-entity) → Task 3 headline test. ✓
- Registration in `builtin_components.hpp` + `setup_runtime` wiring → Task 4. ✓
- All 11 spec test cases mapped: Rng (1), integrate (2), spawn base/spread (3), reflection round-trip (4) → Tasks 1–2; continuous emission (5), burst (6), age-out (7), cap (8), origin (9), prune (10), determinism (11) → Task 3. ✓
- next-steps handoff (project convention) → Task 5. ✓
- Deferred items (rendering, curves, etc.) are documented as out-of-scope, not implemented. ✓

**2. Placeholder scan:** No "TBD"/"handle edge cases"/"similar to Task N"; every code step has full code. The single conditional note in Task 4 Step 1 (membership predicate name) gives an exact fallback and file to check — not a placeholder. ✓

**3. Type consistency:** `Rng.state` (`std::uint64_t`), `Rng.next_unit()`, `integrate(Particle&, const Vec3&, float)`, `ParticleEmitter` field names/types, `spawn_particle(const ParticleEmitter&, const Vec3&, Rng&)`, `ParticleSystem::update`/`particles_for(EntityID)` are used identically across Tasks 1–4. `World` API (`add_system`, `create_entity`, `destroy_entity`, `add_component`, `get_component`, `for_each`, `tick`) matches `engine/world/world.hpp`. `EntityID::value()` returns `std::uint64_t` (matches the nav cache key usage). ✓
