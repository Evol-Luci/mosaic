# Particle triggered / re-armed bursts — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a `ParticleEmitter`'s burst fire again at runtime via a data-driven edge trigger, not only once when the emitter is first seen.

**Architecture:** Add a reflected `int burst_trigger` field to `ParticleEmitter`. `ParticleSystem` remembers the last value it acted on per emitter (transient `EmitterState.last_burst_trigger`) and fires one burst whenever the component's value differs — without mutating the component. The existing on-sight one-shot burst is preserved; a shared local `fire_burst` lambda spawns for both paths.

**Tech Stack:** C++20, header-only engine modules, Catch2 (FetchContent), CMake/CTest. No new dependencies.

**Spec:** [docs/superpowers/specs/2026-09-06-particle-triggered-bursts-design.md](2026-09-06-particle-triggered-bursts-design.md)

## Global Constraints

- C++20; header-only under `engine/`; no new third-party dependencies.
- `ParticleSystem` writes nothing back to components; the trigger is observed, never reset by the system.
- `burst_trigger` default `0`; a default emitter and any scene deserialized without the key behave exactly as before (burst once on first sight).
- Re-arm semantics: **inequality**, not delta — any observed change to `burst_trigger` fires exactly **one** burst that tick, regardless of how many times it changed since the previous tick.
- On first sight, `last_burst_trigger` is initialized to the component's current `burst_trigger` so the counter does not double-fire on creation.
- Bursts respect `max_particles` and draw from the emitter's seeded `rng`, preserving bit-identical determinism for a fixed trigger+tick schedule.
- Persisted type identity is the component name `"ParticleEmitter"` (unchanged); adding a reflected field is a compatible extension.
- Tests tagged `[particles]`; test files already wired into `tests/CMakeLists.txt`. Build target `mosaic_unit_tests`; preset build dir `build/debug`.

---

### Task 1: `burst_trigger` field + reflection

**Files:**
- Modify: `engine/particles/particle_emitter.hpp` (add field + reflect entry)
- Test: `tests/unit/particles/particle_test.cpp` (extend the reflection round-trip)

**Interfaces:**
- Consumes: existing `ParticleEmitter`.
- Produces: `int ParticleEmitter::burst_trigger` (default `0`), serialized under key `"burst_trigger"`.

- [ ] **Step 1: Write the failing test**

In `tests/unit/particles/particle_test.cpp`, extend `TEST_CASE("ParticleEmitter round-trips through reflection", ...)`. After the existing `e.burst            = 8;` line in the setup block, add:

```cpp
    e.burst_trigger    = 4;
```

and after the existing `REQUIRE(back.burst == 8);` assertion, add:

```cpp
    REQUIRE(back.burst_trigger == 4);
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build/debug --target mosaic_unit_tests`
Expected: FAIL to compile — `burst_trigger` is not a member of `ParticleEmitter`.

- [ ] **Step 3: Add the field**

In `engine/particles/particle_emitter.hpp`, inside `struct ParticleEmitter`, add the field immediately after the existing `burst` line. Change:

```cpp
    int           burst            = 0;                          // one-shot count on activation
```

to:

```cpp
    int           burst            = 0;                          // count spawned per burst event
    int           burst_trigger    = 0;                          // bump (e.g. += 1) to fire another burst
```

- [ ] **Step 4: Reflect the field**

In the same file's `reflect(...)`, add the `burst_trigger` member immediately after the `burst` member. Change:

```cpp
    visitor.member("burst", e.burst);
```

to:

```cpp
    visitor.member("burst", e.burst);
    visitor.member("burst_trigger", e.burst_trigger);
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake --build build/debug --target mosaic_unit_tests && ctest --test-dir build/debug -R "round-trips through reflection" --output-on-failure`
Expected: PASS — the reflection round-trip now covers `burst_trigger`.

- [ ] **Step 6: Commit**

```bash
git add engine/particles/particle_emitter.hpp tests/unit/particles/particle_test.cpp
git commit -m "feat(particles): add burst_trigger edge-counter field to ParticleEmitter

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: `ParticleSystem` re-arm logic

**Files:**
- Modify: `engine/particles/particle_system.hpp` (`EmitterState` + `update`)
- Test: `tests/unit/particles/particle_system_test.cpp` (append re-arm behavior tests)

**Interfaces:**
- Consumes: `ParticleEmitter::burst_trigger` (Task 1); `spawn_particle`, `EmitterState`, `particles_for` (existing); `World::get_component<ParticleEmitter>` (returns `ParticleEmitter*`).
- Produces: re-arm behavior — a change to `burst_trigger` spawns one more burst; no new public API.

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/particles/particle_system_test.cpp` (before the final `#include "engine/reflection/builtin_components.hpp"` block if present, otherwise at end — placement is not load-bearing, but keep them among the other `ParticleSystem` cases):

```cpp
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
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build/debug --target mosaic_unit_tests && ctest --test-dir build/debug -R "re-arm|re-fire|one burst per tick" --output-on-failure`
Expected: FAIL — the re-arm cases show `5`/`6`/`10` particles where the current code still shows only the on-sight `5`/`3`/`8` (no re-arm yet). (They compile — `burst_trigger` exists from Task 1.)

- [ ] **Step 3: Add `last_burst_trigger` to `EmitterState`**

In `engine/particles/particle_system.hpp`, in the private `struct EmitterState`, add the field:

```cpp
    struct EmitterState
    {
        std::vector<Particle> particles;
        Rng                   rng;
        float                 accumulator = 0.0f;
        int                   last_burst_trigger = 0;
    };
```

- [ ] **Step 4: Refactor the burst spawn and add the re-arm check**

In `engine/particles/particle_system.hpp`, replace the current find/create block

```cpp
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
```

with:

```cpp
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
```

Leave the `if (em.emitting)` continuous-emission block, the `integrate` loop, and the age-out `erase` that follow exactly as they are.

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build/debug --target mosaic_unit_tests && ctest --test-dir build/debug -R "ParticleSystem" --output-on-failure`
Expected: PASS — the five new re-arm cases plus all existing `ParticleSystem` cases (continuous emission, on-sight burst, age-out, max cap, spawn origin, prune, determinism) still green.

- [ ] **Step 6: Run the full suite**

Run: `ctest --test-dir build/debug`
Expected: PASS. (If the threaded `ScriptWorker` case flakes under parallel load, re-run it: `ctest --test-dir build/debug --rerun-failed --output-on-failure` — it is a documented contention flake, unrelated to particles.)

- [ ] **Step 7: Commit**

```bash
git add engine/particles/particle_system.hpp tests/unit/particles/particle_system_test.cpp
git commit -m "feat(particles): re-arm bursts on burst_trigger change

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: Handoff doc update

**Files:**
- Modify: `docs/project/next-steps.md`

**Interfaces:**
- Consumes: nothing. Documentation only.

- [ ] **Step 1: Update the handoff doc**

In `docs/project/next-steps.md`: in the particles line, add a **Triggered / re-armed bursts — DONE** entry — note the `burst_trigger` edge counter on `ParticleEmitter`, `ParticleSystem` firing one burst per observed change (inequality, not delta) via a shared `fire_burst`, on-sight burst preserved, default `0` unchanged, renderer untouched, and that the demo is deferred to the script-driven-emission slice (which supplies the trigger stimulus). Update the top "Last updated" summary line to mention this slice. In the "Particles follow-ups" open list, drop "triggered/re-armed bursts" and keep the rest (sub-frame spawn interpolation, script-driven emission, more easing curves).

- [ ] **Step 2: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "docs(particles): next-steps — triggered/re-armed bursts done

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Self-Review

**Spec coverage:**
- `burst_trigger` int field, default 0, reflected → Task 1. ✓
- `EmitterState.last_burst_trigger`, init-on-sight so no double-fire → Task 2 Steps 3-4. ✓
- Re-arm on inequality, shared `fire_burst`, on-sight burst preserved → Task 2 Step 4. ✓
- System does not mutate the component (only reads `burst_trigger`) → Task 2 Step 4 (no write to `em`). ✓
- Test: re-arm fires again → "re-arms a burst when burst_trigger changes". ✓
- Test: no spurious re-arm → "does not re-fire a burst without a trigger change". ✓
- Test: one burst per tick despite multiple bumps → "fires one burst per tick regardless of multiple trigger bumps". ✓
- Test: cap respected → "burst re-arm respects max_particles". ✓
- Test: determinism → "burst re-arm is deterministic across runs". ✓
- Backward-compat (default 0 = burst-once) → existing "fires a one-shot burst on activation" test stays green (asserts no re-fire on the second tick with `burst_trigger` untouched) + Task 2 Step 5/6. ✓
- Reflection round-trip → Task 1. ✓
- Demo deferred → captured in Task 3 handoff note; no demo change (spec's documented decision). ✓

**Placeholder scan:** No TBD/TODO; every code step has concrete code. ✓

**Type consistency:** `burst_trigger` is `int` in the field, the reflect entry, the tests, and the `!=` comparison; `last_burst_trigger` is `int`; `fire_burst` takes `EmitterState&`; `world.get_component<ParticleEmitter>(e)` returns `ParticleEmitter*` (matching its use for `Transform` in the same file). ✓
