# Particle over-life easing curves — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let each of a `ParticleEmitter`'s over-life properties (color, size, alpha) interpolate through a selectable non-linear easing curve instead of only linear.

**Architecture:** Add three `int` easing-mode fields (`color_easing`/`size_easing`/`alpha_easing`, default `0` = linear) plus a pure `apply_easing(mode, t)` remap, and apply the remap to `t` inside the existing `color_at`/`size_at`/`alpha_at` helpers before the lerp. The renderer already consumes those helpers, so it needs no change. Defaults reproduce today's linear behavior.

**Tech Stack:** C++20, header-only engine modules, Catch2 (FetchContent), CMake/CTest. No new dependencies.

**Spec:** [docs/superpowers/specs/2026-09-06-particle-easing-design.md](2026-09-06-particle-easing-design.md)

## Global Constraints

- C++20; header-only under `engine/`; no new third-party dependencies.
- Easing modes are an unscoped `enum Easing : int { EaseLinear = 0, EaseIn = 1, EaseOut = 2, EaseInOut = 3 };` — values assign directly to the `int` fields and serialize as plain integers (the reflection layer supports `int`, not `enum class`).
- Easing fields default to `0` (`EaseLinear`); a default emitter and any scene deserialized without the new keys behave exactly as before.
- `apply_easing` is pure and deterministic; caller passes an already-clamped `t ∈ [0,1]`. Every curve maps `0 → 0` and `1 → 1`, so over-life endpoints stay exact regardless of easing.
- Unknown/out-of-range easing modes fall back to linear.
- Renderer (`engine/render/render_system.hpp`) and `ParticleInstance` are unchanged — easing lives entirely behind the `*_at` seam.
- Persisted type identity is the component name `"ParticleEmitter"` (unchanged); adding reflected fields is a compatible extension.
- Tests tagged `[particles]`; the test files are already wired into `tests/CMakeLists.txt` (no CMake changes). Build target is `mosaic_unit_tests`; configured preset dir is `build/debug`.

---

### Task 1: Easing modes, fields, and seam remap on `ParticleEmitter`

**Files:**
- Modify: `engine/particles/particle_emitter.hpp` (add enum + 3 fields + reflect entries + `apply_easing`; update the 3 helpers and the seam doc comment)
- Test: `tests/unit/particles/particle_test.cpp` (append easing tests; extend the reflection round-trip)

**Interfaces:**
- Consumes: existing `ParticleEmitter`, `clamp01`, `color_at`/`size_at`/`alpha_at`, `Vec3` operators (all present in the file).
- Produces:
  - `enum Easing : int { EaseLinear=0, EaseIn=1, EaseOut=2, EaseInOut=3 }` in `mosaic::engine::particles`.
  - Fields `int color_easing`, `int size_easing`, `int alpha_easing` on `ParticleEmitter`.
  - `float mosaic::engine::particles::apply_easing(int mode, float t)`.
  - `color_at`/`size_at`/`alpha_at` now remap `t` through the property's easing.

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/particles/particle_test.cpp`:

```cpp
using mosaic::engine::particles::apply_easing;
using mosaic::engine::particles::EaseIn;
using mosaic::engine::particles::EaseInOut;
using mosaic::engine::particles::EaseLinear;
using mosaic::engine::particles::EaseOut;

TEST_CASE("apply_easing maps endpoints 0->0 and 1->1 for every mode", "[particles]")
{
    for (int mode : {EaseLinear, EaseIn, EaseOut, EaseInOut})
    {
        REQUIRE(apply_easing(mode, 0.0f) == Approx(0.0f));
        REQUIRE(apply_easing(mode, 1.0f) == Approx(1.0f));
    }
}

TEST_CASE("apply_easing produces the expected midpoint per mode", "[particles]")
{
    REQUIRE(apply_easing(EaseLinear, 0.5f) == Approx(0.5f));
    REQUIRE(apply_easing(EaseIn, 0.5f) == Approx(0.25f));
    REQUIRE(apply_easing(EaseOut, 0.5f) == Approx(0.75f));
    REQUIRE(apply_easing(EaseInOut, 0.5f) == Approx(0.5f)); // smoothstep(0.5)=0.5
}

TEST_CASE("apply_easing falls back to linear for an unknown mode", "[particles]")
{
    REQUIRE(apply_easing(99, 0.25f) == Approx(0.25f));
    REQUIRE(apply_easing(-3, 0.8f) == Approx(0.8f));
}

TEST_CASE("over-life helpers apply the property's easing", "[particles]")
{
    ParticleEmitter e;
    e.color = Vec3{0.0f, 0.0f, 0.0f};
    e.end_color = Vec3{1.0f, 1.0f, 1.0f};
    e.size = 0.0f;
    e.end_size = 1.0f;
    e.alpha = 0.0f;
    e.end_alpha = 1.0f;
    e.color_easing = EaseIn;   // t^2 -> 0.25 at t=0.5
    e.size_easing = EaseOut;   // 1-(1-t)^2 -> 0.75 at t=0.5
    e.alpha_easing = EaseInOut; // smoothstep -> 0.5 at t=0.5

    REQUIRE(color_at(e, 0.5f).x == Approx(0.25f));
    REQUIRE(size_at(e, 0.5f) == Approx(0.75f));
    REQUIRE(alpha_at(e, 0.5f) == Approx(0.5f));
}

TEST_CASE("over-life endpoints stay exact under any easing", "[particles]")
{
    ParticleEmitter e;
    e.color = Vec3{0.2f, 0.4f, 0.6f};
    e.end_color = Vec3{0.8f, 0.1f, 0.0f};
    e.size = 0.25f;
    e.end_size = 1.0f;
    e.alpha = 1.0f;
    e.end_alpha = 0.0f;
    e.color_easing = EaseIn;
    e.size_easing = EaseOut;
    e.alpha_easing = EaseInOut;

    REQUIRE(color_at(e, 0.0f).x == Approx(0.2f));
    REQUIRE(color_at(e, 1.0f).x == Approx(0.8f));
    REQUIRE(size_at(e, 0.0f) == Approx(0.25f));
    REQUIRE(size_at(e, 1.0f) == Approx(1.0f));
    REQUIRE(alpha_at(e, 0.0f) == Approx(1.0f));
    REQUIRE(alpha_at(e, 1.0f) == Approx(0.0f));
}

TEST_CASE("default emitter easing is linear", "[particles]")
{
    ParticleEmitter e; // *_easing all default 0
    REQUIRE(e.color_easing == 0);
    REQUIRE(e.size_easing == 0);
    REQUIRE(e.alpha_easing == 0);
    e.size = 0.0f;
    e.end_size = 1.0f;
    REQUIRE(size_at(e, 0.5f) == Approx(0.5f)); // linear midpoint
}
```

Also extend the existing `TEST_CASE("ParticleEmitter round-trips through reflection", ...)`. After the current `e.end_alpha = 0.25f;` line in the setup block, add:

```cpp
    e.color_easing = EaseIn;
    e.size_easing = EaseOut;
    e.alpha_easing = EaseInOut;
```

and after the existing `REQUIRE(back.end_alpha == Approx(0.25f));` assertion, add:

```cpp
    REQUIRE(back.color_easing == EaseIn);
    REQUIRE(back.size_easing == EaseOut);
    REQUIRE(back.alpha_easing == EaseInOut);
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build/debug --target mosaic_unit_tests`
Expected: FAIL to compile — `apply_easing`, `EaseLinear`/`EaseIn`/`EaseOut`/`EaseInOut`, and the `*_easing` fields are undeclared.

- [ ] **Step 3: Add the easing enum and fields**

In `engine/particles/particle_emitter.hpp`, add the enum just inside the namespace, immediately before `struct ParticleEmitter` (after the `using ... Vec3;` line):

```cpp
// Over-life interpolation curves selectable per property. Serialized as int
// (the reflection layer supports int, not enum class), so these values are a
// stable on-disk contract — append new modes, do not renumber.
enum Easing : int { EaseLinear = 0, EaseIn = 1, EaseOut = 2, EaseInOut = 3 };
```

Then, inside `struct ParticleEmitter`, replace the current end-of-struct block

```cpp
    Vec3          color            = Vec3{1.0f, 1.0f, 1.0f};     // color at birth (life = 0)
    Vec3          end_color        = Vec3{1.0f, 1.0f, 1.0f};     // color at death (life = 1)
    float         size             = 0.1f;                       // billboard edge length at birth
    float         end_size         = 0.1f;                       // billboard edge length at death
    float         alpha            = 1.0f;                       // opacity at birth (life = 0)
    float         end_alpha        = 0.0f;                       // opacity at death (life = 1)
};
```

with:

```cpp
    Vec3          color            = Vec3{1.0f, 1.0f, 1.0f};     // color at birth (life = 0)
    Vec3          end_color        = Vec3{1.0f, 1.0f, 1.0f};     // color at death (life = 1)
    int           color_easing     = EaseLinear;                 // curve for color -> end_color
    float         size             = 0.1f;                       // billboard edge length at birth
    float         end_size         = 0.1f;                       // billboard edge length at death
    int           size_easing      = EaseLinear;                 // curve for size -> end_size
    float         alpha            = 1.0f;                       // opacity at birth (life = 0)
    float         end_alpha        = 0.0f;                       // opacity at death (life = 1)
    int           alpha_easing     = EaseLinear;                 // curve for alpha -> end_alpha
};
```

- [ ] **Step 4: Reflect the new fields**

In `reflect(...)`, replace the current color/size/alpha member block

```cpp
    visitor.member("color", e.color);
    visitor.member("end_color", e.end_color);
    visitor.member("size", e.size);
    visitor.member("end_size", e.end_size);
    visitor.member("alpha", e.alpha);
    visitor.member("end_alpha", e.end_alpha);
```

with:

```cpp
    visitor.member("color", e.color);
    visitor.member("end_color", e.end_color);
    visitor.member("color_easing", e.color_easing);
    visitor.member("size", e.size);
    visitor.member("end_size", e.end_size);
    visitor.member("size_easing", e.size_easing);
    visitor.member("alpha", e.alpha);
    visitor.member("end_alpha", e.end_alpha);
    visitor.member("alpha_easing", e.alpha_easing);
```

- [ ] **Step 5: Add `apply_easing` and remap the helpers**

In `engine/particles/particle_emitter.hpp`, replace the whole block from the `// Linear interpolation over a particle's normalized life ...` comment through the three helper functions (i.e. the `clamp01` + `color_at` + `size_at` + `alpha_at` block) with:

```cpp
// Remap a normalized, clamped life t in [0, 1] through the selected easing curve.
// Pure and deterministic; every curve maps 0->0 and 1->1 so over-life endpoints
// stay exact. Unknown modes fall back to linear.
inline float apply_easing(int mode, float t)
{
    switch (mode)
    {
        case EaseIn:    return t * t;
        case EaseOut:   return 1.0f - (1.0f - t) * (1.0f - t);
        case EaseInOut: return t * t * (3.0f - 2.0f * t); // smoothstep
        case EaseLinear:
        default:        return t;
    }
}

// Interpolate an over-life property at normalized life t = age / lifetime.
// t is clamped to [0, 1] (no extrapolation) and remapped through the property's
// easing curve before the lerp.
inline float clamp01(float t) { return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); }

inline Vec3 color_at(const ParticleEmitter& e, float t)
{
    const float u = apply_easing(e.color_easing, clamp01(t));
    return e.color + (e.end_color - e.color) * u;
}

inline float size_at(const ParticleEmitter& e, float t)
{
    const float u = apply_easing(e.size_easing, clamp01(t));
    return e.size + (e.end_size - e.size) * u;
}

inline float alpha_at(const ParticleEmitter& e, float t)
{
    const float u = apply_easing(e.alpha_easing, clamp01(t));
    return e.alpha + (e.end_alpha - e.alpha) * u;
}
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `cmake --build build/debug --target mosaic_unit_tests && ctest --test-dir build/debug -R "apply_easing|over-life|round-trips through reflection|default emitter" --output-on-failure`
Expected: PASS — the new `apply_easing`/easing cases, the endpoint-invariant case, the default-is-linear case, and the extended reflection round-trip.

- [ ] **Step 7: Commit**

```bash
git add engine/particles/particle_emitter.hpp tests/unit/particles/particle_test.cpp
git commit -m "feat(particles): per-property over-life easing curves

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: Demo easing, full-suite verification, and handoff update

**Files:**
- Modify: `app/run/demo_scene.hpp` (fountain emitter block)
- Modify: `docs/project/next-steps.md` (mark this follow-up done — rides on this branch)

**Interfaces:**
- Consumes: `EaseOut` and the `alpha_easing` field from Task 1. No new API.
- Produces: a visible eased fade in `mosaic editor` / `mosaic run --window`.

- [ ] **Step 1: Give the demo fountain an eased fade**

In `app/run/demo_scene.hpp`, in the fountain emitter block, after the line `emitter.end_alpha        = 0.0f;                      // fade fully out at death` add:

```cpp
    emitter.alpha_easing     = mosaic::engine::particles::EaseOut; // hold opacity, then fade fast
```

- [ ] **Step 2: Build and run the full test suite**

Run: `cmake --build build/debug --target mosaic_unit_tests && ctest --test-dir build/debug`
Expected: PASS — the full suite. The default-scene entity/component count tests are unaffected (no entities/components added, only a field value on the existing emitter).

- [ ] **Step 3: Functional smoke check of the windowed app**

Run: `cmake --build build/debug --target mosaic && ./build/debug/app/mosaic run --window --ticks 3 2>&1 | tail -5; echo "exit: ${PIPESTATUS[0]}"`
Expected: exit 0 and no Vulkan validation errors on stderr. (Verify functionally — exit 0 + clean stderr — not via screenshot.)

- [ ] **Step 4: Update the handoff doc**

In `docs/project/next-steps.md`: in the particles line, change the follow-up entry so **easing curves** is marked done — note per-property `color_easing`/`size_easing`/`alpha_easing` (Linear/EaseIn/EaseOut/EaseInOut) behind the same `color_at`/`size_at`/`alpha_at` seam, renderer unchanged. Update the top "Last updated" summary line to mention this slice. Keep the remaining particles follow-ups (sub-frame spawn interpolation, triggered/re-armed bursts, script-driven emission) listed as still open, and drop "easing curves" from that open list.

- [ ] **Step 5: Commit**

```bash
git add app/run/demo_scene.hpp docs/project/next-steps.md
git commit -m "feat(particles): demo fountain eased fade + next-steps (easing done)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Self-Review

**Spec coverage:**
- `enum Easing : int` modes → Task 1 Step 3. ✓
- `color_easing`/`size_easing`/`alpha_easing` int fields, default 0, reflected → Task 1 Steps 3-4. ✓
- `apply_easing(mode, t)` with Linear/In/Out/InOut + unknown→linear → Task 1 Step 5; tests Step 1 (cases 1-3). ✓
- Seam remap in `color_at`/`size_at`/`alpha_at`, renderer unchanged → Task 1 Step 5 (no render file touched). ✓
- Endpoint-exact-under-easing invariant → Task 1 test "over-life endpoints stay exact under any easing". ✓
- Reflection round-trip of the 3 ints → Task 1 reflection extension. ✓
- Default-is-linear backward-compat → Task 1 test "default emitter easing is linear". ✓
- Demo easing → Task 2 Step 1. ✓
- Handoff update → Task 2 Step 4. ✓

**Placeholder scan:** No TBD/TODO; every code step has concrete code. ✓

**Type consistency:** `apply_easing(int, float) -> float`; fields `int color_easing`/`size_easing`/`alpha_easing`; enumerators `EaseLinear`/`EaseIn`/`EaseOut`/`EaseInOut` used identically in fields, helpers, and tests. Helper signatures unchanged (`color_at -> Vec3`, `size_at`/`alpha_at -> float`). ✓
