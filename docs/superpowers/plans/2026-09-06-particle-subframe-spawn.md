# Particle sub-frame spawn interpolation — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Spread a moving emitter's continuous (rate-based) spawns along the path it swept this tick, instead of clumping them all at its current position.

**Architecture:** `ParticleSystem` remembers each emitter's origin from the previous tick (`EmitterState.last_origin`) and places this tick's continuous spawns at evenly-spaced points along `prev → curr`. Bursts stay instantaneous (spawn at current origin). No component field, no reflection or renderer change; a stationary emitter (`prev == curr`) is bit-identical to today.

**Tech Stack:** C++20, header-only engine modules, Catch2 (FetchContent), CMake/CTest. No new dependencies.

**Spec:** [docs/superpowers/specs/2026-09-06-particle-subframe-spawn-design.md](2026-09-06-particle-subframe-spawn-design.md)

## Global Constraints

- C++20; header-only under `engine/`; no new third-party dependencies.
- Only **continuous** emission interpolates along the path; **bursts** (on-sight + re-arm) spawn at the current origin, unchanged.
- Spawn fraction is `(i + 0.5) / n` for spawn `i` of `n` this tick (a single spawn → path midpoint `f = 0.5`).
- No-op for a stationary emitter (`prev == curr`) → bit-identical to current behavior; RNG is still drawn once per spawn in the same order; the accumulator carry is preserved exactly (`-= n`).
- No new component field, no reflection change, no renderer change.
- Tests tagged `[particles]`; test file already wired into `tests/CMakeLists.txt`. Build target `mosaic_unit_tests`; preset build dir `build/debug`.

---

### Task 1: `ParticleSystem` path-spread continuous emission

**Files:**
- Modify: `engine/particles/particle_system.hpp` (`EmitterState` + `update`)
- Test: `tests/unit/particles/particle_system_test.cpp` (append motion tests)

**Interfaces:**
- Consumes: `spawn_particle`, `EmitterState`, `Transform` (existing); `World::get_component<Transform>` returns `Transform*`.
- Produces: continuous spawns interpolated along `prev → curr`; `EmitterState.last_origin` tracks the previous origin. No new public API.

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/particles/particle_system_test.cpp`:

```cpp
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
```

(`<algorithm>` for `std::max`/`std::sort`, `<vector>`, and `<cstddef>` for `std::size_t` are already transitively available via the existing includes in this file / the engine headers; if the build reports any as missing, add the include.)

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build/debug --target mosaic_unit_tests && ctest --test-dir build/debug -R "interpolates a single continuous|spreads multiple continuous|re-armed burst is not path|deterministic under motion" --output-on-failure`
Expected: FAIL — the two interpolation cases show `max_x == 10` / all-at-10 (current code spawns everything at the current origin); they compile (no new API).

- [ ] **Step 3: Add `last_origin` to `EmitterState`**

In `engine/particles/particle_system.hpp`, in the private `struct EmitterState`, add the field:

```cpp
    struct EmitterState
    {
        std::vector<Particle> particles;
        Rng                   rng;
        float                 accumulator = 0.0f;
        int                   last_burst_trigger = 0;
        mosaic::foundation::math::Vec3 last_origin{}; // emitter origin at the previous tick (path start)
    };
```

- [ ] **Step 4: Initialize `last_origin` on first sight**

In the state-creation block (the `if (it == states_.end())` body), after `st.last_burst_trigger = em.burst_trigger;`, add:

```cpp
                    st.last_origin = origin; // no interpolation before the emitter has moved
```

- [ ] **Step 5: Path-spread the continuous emission and record the origin**

Replace the current continuous-emission block:

```cpp
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
```

with:

```cpp
                if (em.emitting)
                {
                    st.accumulator += em.rate * dt;
                    // Count how many spawn this tick (respecting the cap), then place them along
                    // the path the emitter swept this tick (prev -> curr) so a moving emitter
                    // streams instead of clumping. Stationary => prev == curr => spos == origin.
                    int n = 0;
                    {
                        float acc = st.accumulator;
                        int   sz  = static_cast<int>(st.particles.size());
                        while (acc >= 1.0f && sz < em.max_particles) { ++n; acc -= 1.0f; ++sz; }
                    }
                    const mosaic::foundation::math::Vec3 prev = st.last_origin;
                    for (int i = 0; i < n; ++i)
                    {
                        const float f = (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
                        const mosaic::foundation::math::Vec3 spos = prev + (origin - prev) * f;
                        st.particles.push_back(spawn_particle(em, spos, st.rng));
                    }
                    st.accumulator -= static_cast<float>(n);
                }

                st.last_origin = origin; // path start for next tick (set every tick, moving or not)
```

Leave the `integrate` loop and the age-out `erase` that follow unchanged.

- [ ] **Step 6: Run the tests to verify they pass**

Run: `cmake --build build/debug --target mosaic_unit_tests && ctest --test-dir build/debug -R "ParticleSystem" --output-on-failure`
Expected: PASS — the four new motion cases plus all existing `ParticleSystem` cases (including "spawns particles at the entity's Transform position" and the determinism cases, which use stationary emitters and stay bit-identical).

- [ ] **Step 7: Commit**

```bash
git add engine/particles/particle_system.hpp tests/unit/particles/particle_system_test.cpp
git commit -m "feat(particles): sub-frame spawn interpolation for moving emitters

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: Demo trailing emitter + verification + handoff

**Files:**
- Modify: `app/run/demo_scene.hpp` (attach an emitter to the orbiter)
- Modify: `docs/project/next-steps.md`

**Interfaces:**
- Consumes: the interpolation from Task 1. No new API.

- [ ] **Step 1: Attach a trailing emitter to the orbiting cube**

In `app/run/demo_scene.hpp`, immediately after the orbiter's `ScriptComponent` is added (the `world.add_component<...ScriptComponent>(orbiter, ...kOrbitScript...)` call), add:

```cpp
    // A continuous emitter on the moving orbiter: sub-frame spawn interpolation
    // spreads its trail smoothly along the orbit arc instead of clumping per tick.
    mosaic::engine::particles::ParticleEmitter trail;
    trail.emitting         = true;
    trail.rate             = 40.0f;
    trail.lifetime         = 0.8f;
    trail.initial_velocity = Vec3{0.0f, 0.0f, 0.0f};
    trail.spread           = 0.2f;
    trail.acceleration     = Vec3{0.0f, 0.0f, 0.0f};
    trail.color            = Vec3{0.2f, 0.9f, 0.3f};   // match the cube (green)
    trail.size             = 0.08f;
    trail.end_color        = Vec3{0.9f, 0.9f, 0.2f};   // fade to yellow
    trail.end_size         = 0.01f;
    trail.alpha            = 1.0f;
    trail.end_alpha        = 0.0f;
    trail.alpha_easing     = mosaic::engine::particles::EaseOut;
    world.add_component<mosaic::engine::particles::ParticleEmitter>(orbiter, trail);
```

- [ ] **Step 2: Build and run the full test suite**

Run: `cmake --build build/debug --target mosaic_unit_tests && ctest --test-dir build/debug`
Expected: PASS. Default-scene entity/component counts are unchanged (a component was added to an existing entity, no new entity), so the count tests are unaffected. (If the threaded `ScriptWorker` case flakes, re-run: `ctest --test-dir build/debug --rerun-failed --output-on-failure`.)

- [ ] **Step 3: Functional smoke check**

Run: `cmake --build build/debug --target mosaic && ./build/debug/app/mosaic run --window --ticks 90 2>&1 | tail -5; echo "exit: ${PIPESTATUS[0]}"`
Expected: exit 0, no Vulkan validation errors on stderr (the orbiter now trails particles as it moves).

- [ ] **Step 4: Update the handoff doc**

In `docs/project/next-steps.md`: add a **Sub-frame spawn interpolation — DONE** entry in the particles line — note continuous spawns are spread along the emitter's swept path (`prev → curr`, fractions `(i+0.5)/n`), bursts stay instantaneous, no component field, no-op/bit-identical for a stationary emitter, and the orbiter now trails smoothly. Update the top "Last updated" summary. In the "Particles follow-ups" open list, drop "sub-frame spawn interpolation", leaving "more easing curves (cubic/elastic/bounce)" (and note sub-frame *age* integration as a deeper follow-up).

- [ ] **Step 5: Commit**

```bash
git add app/run/demo_scene.hpp docs/project/next-steps.md
git commit -m "feat(particles): orbiter trail demo + next-steps (sub-frame spawn done)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Self-Review

**Spec coverage:**
- `EmitterState.last_origin`, init-on-sight → Task 1 Steps 3-4. ✓
- Continuous spawns spread along `prev→curr` with `(i+0.5)/n`, exact accumulator carry → Task 1 Step 5. ✓
- Bursts not interpolated → unchanged `fire_burst` (uses `origin`); Task 1 test "re-armed burst is not path-interpolated". ✓
- `last_origin` recorded every tick → Task 1 Step 5 (`st.last_origin = origin;` outside the `emitting` guard). ✓
- No-op/bit-identical for stationary → preserved (spos == origin when prev == curr); guarded by existing stationary tests + determinism test. ✓
- Test: midpoint (n=1), spread (n=3), burst-not-interpolated, determinism-under-motion → Task 1 Step 1. ✓
- Demo trailing emitter → Task 2 Step 1. ✓
- Handoff → Task 2 Step 4. ✓

**Placeholder scan:** No TBD/TODO; every code step has concrete code. ✓

**Type consistency:** `last_origin` is `mosaic::foundation::math::Vec3`; `n` is `int`; `spos`/`prev` are `Vec3`; `spawn_particle(em, spos, st.rng)` matches the existing `spawn_particle(const ParticleEmitter&, const Vec3&, Rng&)` signature; `world.get_component<Transform>(e)` returns `Transform*`. ✓
