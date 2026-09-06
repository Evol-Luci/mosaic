# Particle color / size / alpha over-life curves — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a `ParticleEmitter`'s color, size, and opacity interpolate linearly over each particle's life instead of being constant.

**Architecture:** Reinterpret the emitter's existing `color`/`size` as *start* (birth) values, add `end_color`/`end_size` and a new authored `alpha`/`end_alpha` pair, and expose three pure `*_at(emitter, t)` interpolation helpers (t = age/lifetime). `ParticleSystem` stays a pure simulator; the renderer derives per-particle color/size/alpha from the helpers at draw time. Defaults reproduce today's behavior exactly.

**Tech Stack:** C++20, header-only engine modules, Catch2 (FetchContent), CMake/CTest. No new dependencies.

**Spec:** [docs/superpowers/specs/2026-09-06-particle-overlife-curves-design.md](2026-09-06-particle-overlife-curves-design.md)

## Global Constraints

- C++20; header-only under `engine/`; no new third-party dependencies.
- Interpolation is **linear** for now: `a + (b - a) * clamp01(t)`, no extrapolation. Document the intended easing generalization at the seam.
- **Backward compatibility by construction:** `color`/`size` keep their names, types, defaults (`{1,1,1}` / `0.1`) and reflection positions; new fields default to `end_color={1,1,1}`, `end_size=0.1`, `alpha=1.0`, `end_alpha=0.0` so a default emitter renders identically to today (constant white, constant size, alpha `1→0`).
- Determinism unchanged: helpers are pure functions of `(emitter, t)`; no new state, no RNG.
- Persisted type identity is the component **name** `"ParticleEmitter"` (unchanged). Adding reflected fields is a compatible extension.
- Tests tagged `[particles]` / `[render]`, wired through the existing `tests/CMakeLists.txt` (no CMake changes needed — files already listed).

---

### Task 1: Fields + interpolation seam on `ParticleEmitter`

**Files:**
- Modify: `engine/particles/particle_emitter.hpp` (add 4 fields, reflect them, add 3 helpers)
- Test: `tests/unit/particles/particle_test.cpp` (append helper tests; extend the reflection round-trip)

**Interfaces:**
- Consumes: `ParticleEmitter` (existing), `Vec3` operators `+ - *` from `foundation/math/vec3.hpp` (existing).
- Produces:
  - Fields `Vec3 end_color`, `float end_size`, `float alpha`, `float end_alpha` on `ParticleEmitter`.
  - `Vec3  mosaic::engine::particles::color_at(const ParticleEmitter&, float t)`
  - `float mosaic::engine::particles::size_at (const ParticleEmitter&, float t)`
  - `float mosaic::engine::particles::alpha_at(const ParticleEmitter&, float t)`

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/particles/particle_test.cpp`:

```cpp
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
```

Also extend the existing `TEST_CASE("ParticleEmitter round-trips through reflection", ...)` — after the current `e.size = 0.35f;` line, add authored end/alpha values, and after the existing `REQUIRE(back.size == Approx(0.35f));` add assertions for the new fields:

```cpp
    // (add to the setup block, after e.size = 0.35f;)
    e.end_color = Vec3{0.9f, 0.1f, 0.2f};
    e.end_size  = 0.05f;
    e.alpha     = 0.8f;
    e.end_alpha = 0.25f;
```
```cpp
    // (add to the assertion block, after REQUIRE(back.size == Approx(0.35f));)
    REQUIRE(back.end_color.x == Approx(0.9f));
    REQUIRE(back.end_color.y == Approx(0.1f));
    REQUIRE(back.end_color.z == Approx(0.2f));
    REQUIRE(back.end_size == Approx(0.05f));
    REQUIRE(back.alpha == Approx(0.8f));
    REQUIRE(back.end_alpha == Approx(0.25f));
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build --target mosaic_tests && ctest --test-dir build -R particles -V`
Expected: FAIL to compile — `end_color`/`end_size`/`alpha`/`end_alpha` and `color_at`/`size_at`/`alpha_at` are undeclared.

- [ ] **Step 3: Add the fields**

In `engine/particles/particle_emitter.hpp`, replace the last two members of `struct ParticleEmitter` (currently `color` then `size`) with this ordered block:

```cpp
    Vec3          color            = Vec3{1.0f, 1.0f, 1.0f};     // color at birth (life = 0)
    Vec3          end_color        = Vec3{1.0f, 1.0f, 1.0f};     // color at death (life = 1)
    float         size             = 0.1f;                       // billboard edge length at birth
    float         end_size         = 0.1f;                       // billboard edge length at death
    float         alpha            = 1.0f;                       // opacity at birth (life = 0)
    float         end_alpha        = 0.0f;                       // opacity at death (life = 1)
```

- [ ] **Step 4: Reflect the new fields**

In the same file's `reflect(...)`, replace the final `visitor.member("color", ...)` / `visitor.member("size", ...)` lines with:

```cpp
    visitor.member("color", e.color);
    visitor.member("end_color", e.end_color);
    visitor.member("size", e.size);
    visitor.member("end_size", e.end_size);
    visitor.member("alpha", e.alpha);
    visitor.member("end_alpha", e.end_alpha);
```

- [ ] **Step 5: Add the interpolation helpers**

In the same file, after `spawn_particle(...)` and before the closing namespace brace, add:

```cpp
// Linear interpolation over a particle's normalized life t = age / lifetime.
// t is clamped to [0, 1] (no extrapolation). Interpolation is linear for now;
// the intended generalization is to remap t through a per-property easing curve
// right here — the renderer consumes these helpers unchanged, so easing drops in
// behind this seam without touching draw code.
inline float clamp01(float t) { return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); }

inline Vec3 color_at(const ParticleEmitter& e, float t)
{
    const float u = clamp01(t);
    return e.color + (e.end_color - e.color) * u;
}

inline float size_at(const ParticleEmitter& e, float t)
{
    const float u = clamp01(t);
    return e.size + (e.end_size - e.size) * u;
}

inline float alpha_at(const ParticleEmitter& e, float t)
{
    const float u = clamp01(t);
    return e.alpha + (e.end_alpha - e.alpha) * u;
}
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `cmake --build build --target mosaic_tests && ctest --test-dir build -R particles -V`
Expected: PASS — all `[particles]` tests, including the four new helper cases and the extended reflection round-trip.

- [ ] **Step 7: Commit**

```bash
git add engine/particles/particle_emitter.hpp tests/unit/particles/particle_test.cpp
git commit -m "feat(particles): color/size/alpha over-life fields + interpolation seam

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: Renderer consumes the over-life seam

**Files:**
- Modify: `engine/render/render_system.hpp` (the particle instance-build loop, ~lines 114-120)
- Modify: `tests/unit/render/render_system_test.cpp` (fix the exact-value test; add an over-life assertion)

**Interfaces:**
- Consumes: `color_at`/`size_at`/`alpha_at` from Task 1; `ParticleInstance{ world_position, size, color, alpha }` (existing, unchanged).
- Produces: no new API — the renderer now emits per-particle interpolated `size`/`color`/`alpha`.

- [ ] **Step 1: Update the exact-value test to keep passing, and add an over-life test**

In `tests/unit/render/render_system_test.cpp`, in `TEST_CASE("RenderSystem ... " )` that sets `em.color = Vec3{0.9f, 0.5f, 0.1f}; em.size = 0.25f;` (the one asserting `call.instances[0].color.x == Approx(0.9f)` and `.size == Approx(0.25f)`), pin the endpoints to the start so the assertion stays meaningful. Immediately after `em.size = 0.25f;` add:

```cpp
    em.end_color = em.color; // constant color/size for this exact-value check
    em.end_size = em.size;
```

Then append a new test that exercises interpolation at a known age:

```cpp
TEST_CASE("RenderSystem interpolates particle color/size/alpha over life", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);
    add_camera(world, Vec3{0, 0, 4});
    world.add_system<ParticleSystem>();

    auto e = world.create_entity();
    world.add_component<mosaic::engine::transform::Transform>(
        e, mosaic::engine::transform::Transform{});
    ParticleEmitter em;
    em.emitting = true;
    em.rate = 10.0f;                 // rate*dt = 1.0 -> exactly one spawn this tick
    em.lifetime = 1.0f;
    em.initial_velocity = Vec3{0, 0, 0};
    em.acceleration = Vec3{0, 0, 0};
    em.color = Vec3{0.0f, 0.0f, 0.0f};
    em.end_color = Vec3{1.0f, 1.0f, 1.0f};
    em.size = 0.0f;
    em.end_size = 1.0f;
    em.alpha = 1.0f;
    em.end_alpha = 0.0f;
    world.add_component<ParticleEmitter>(e, em);

    world.tick(0.1f); // one particle, aged 0.1 -> t = 0.1
    rs.render(world, device, registry);

    REQUIRE(device.particle_draws.size() == 1);
    const auto& call = device.particle_draws[0];
    REQUIRE(call.instances.size() == 1);
    REQUIRE(call.instances[0].color.x == Approx(0.1f));
    REQUIRE(call.instances[0].size == Approx(0.1f));
    REQUIRE(call.instances[0].alpha == Approx(0.9f));
}
```

- [ ] **Step 2: Run the render tests to verify the new one fails**

Run: `cmake --build build --target mosaic_tests && ctest --test-dir build -R render -V`
Expected: the new "interpolates ... over life" case FAILS (draws constant `em.color`/`em.size` and the old hardcoded alpha), while the exact-value case (now pinned) PASSES.

- [ ] **Step 3: Update the renderer to use the seam**

In `engine/render/render_system.hpp`, replace the per-particle loop body (currently computing `alpha` inline and pushing `ParticleInstance{p.position, em.size, em.color, alpha}`) with:

```cpp
                    for (const auto& p : *pool)
                    {
                        const float t = (p.lifetime > 0.0f) ? p.age / p.lifetime : 0.0f;
                        instances.push_back(ParticleInstance{
                            p.position,
                            mosaic::engine::particles::size_at(em, t),
                            mosaic::engine::particles::color_at(em, t),
                            mosaic::engine::particles::alpha_at(em, t)});
                    }
```

(The `<algorithm>` include previously needed for `std::clamp` on alpha may now be unused here; leave other includes as-is unless the build warns.)

- [ ] **Step 4: Run the render tests to verify they pass**

Run: `cmake --build build --target mosaic_tests && ctest --test-dir build -R render -V`
Expected: PASS — all `[render]` tests, including the new over-life case, the pinned exact-value case, the alpha-fade case, and the two-emitter batching case.

- [ ] **Step 5: Commit**

```bash
git add engine/render/render_system.hpp tests/unit/render/render_system_test.cpp
git commit -m "feat(render): particles fade color/size/alpha over life via the emitter seam

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: Demo fountain gradient, full-suite verification, and handoff update

**Files:**
- Modify: `app/run/demo_scene.hpp` (the fountain emitter block, ~lines 274-283)
- Modify: `docs/project/next-steps.md` (mark this follow-up done — rides on this branch, not its own PR)

**Interfaces:**
- Consumes: the `ParticleEmitter` fields from Task 1. No new API.
- Produces: a visible over-life gradient in `mosaic editor` / `mosaic run --window`.

- [ ] **Step 1: Give the demo fountain an intentional gradient**

In `app/run/demo_scene.hpp`, in the fountain emitter block, after `emitter.size = 0.15f;` add:

```cpp
    emitter.end_color        = Vec3{0.7f, 0.1f, 0.4f};   // orange -> magenta over life
    emitter.end_size         = 0.03f;                    // shrink as it fades
    emitter.alpha            = 1.0f;                      // opaque at birth
    emitter.end_alpha        = 0.0f;                      // fade fully out at death
```

- [ ] **Step 2: Build and run the full test suite**

Run: `cmake --build build --target mosaic_tests && ctest --test-dir build`
Expected: PASS — the full suite. In particular the default-scene count tests (`tests/unit/app/demo_scene_test.cpp` and the three other count assertions) still pass: no entities or components were added, only field values on the existing emitter.

- [ ] **Step 3: Functional smoke check of the windowed app**

Run: `cmake --build build --target mosaic && ./build/app/mosaic run --window --ticks 3 2>&1 | tail -5; echo "exit: ${PIPESTATUS[0]}"`
Expected: exit 0 and no Vulkan validation errors on stderr. (Per the sandbox-no-gui-screenshots note, verify functionally — exit 0 + silent stderr — rather than capturing the window.)

- [ ] **Step 4: Update the handoff doc**

In `docs/project/next-steps.md`, in the particles line under Phase 4 (the "Particles follow-ups:" list), mark **color/size-over-life curves — done**, note it now covers color, size, **and alpha** (start→end, linear), and that easing remains the documented next follow-up. Update the "Last updated" date/summary line at the top to mention this slice. Keep the remaining follow-ups (sub-frame spawn interpolation, triggered/re-armed bursts, script-driven emission, easing) listed as still open.

- [ ] **Step 5: Commit**

```bash
git add app/run/demo_scene.hpp docs/project/next-steps.md
git commit -m "feat(particles): demo fountain over-life gradient + next-steps (over-life curves done)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Self-Review

**Spec coverage:**
- New fields `end_color`/`end_size`/`alpha`/`end_alpha` + reflection → Task 1 (Steps 3-4). ✓
- Interpolation seam `color_at`/`size_at`/`alpha_at`, linear, clamped, easing documented → Task 1 (Step 5). ✓
- Backward-compat defaults reproduce today's look → Task 1 test "default emitter reproduces the previous constant/fade behavior" (Step 1). ✓
- Renderer consumes the seam, `ParticleInstance` unchanged → Task 2 (Step 3). ✓
- Demo gradient → Task 3 (Step 1). ✓
- Headless tests: helpers at t=0/0.5/1, clamping, reflection round-trip → Task 1. ✓
- Grep/guard for snapshot & count tests → covered: the one exact-value render test is fixed (Task 2 Step 1); count tests confirmed unaffected and re-run (Task 3 Step 2). ✓

**Placeholder scan:** No TBD/TODO; every code step has concrete code. ✓

**Type consistency:** `color_at`→`Vec3`, `size_at`/`alpha_at`→`float`, all `(const ParticleEmitter&, float)`; identical names in Tasks 1-2; `ParticleInstance{world_position, size, color, alpha}` field order matches `engine/render/render_device.hpp`. ✓
