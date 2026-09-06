# Particle color / size / alpha over-life curves — Design

## Goal (Phase 4, particles follow-up)

Let a `ParticleEmitter`'s **color, size, and opacity vary over each particle's
life** instead of being constant. Today every particle of an emitter draws with
the emitter's single `color` and `size`, and the renderer fades `alpha` with a
hardcoded `1 - age/lifetime`. This slice reinterprets `color`/`size` as the
*start* (birth) values, adds matching *end* (death) values, and promotes alpha to
an authored start/end pair — so an emitter can, e.g., spawn bright-orange and
large, then fade to dim-red, small, and transparent.

Follows the established particles shape: a **neutral pure-C++ interpolation seam**
on the reflected component, consumed by the renderer; header-only, no new
dependencies, deterministic, and headless-testable at the interpolation-math
level. `ParticleSystem` stays a **pure simulator** — it stores no per-particle
color/size/alpha; those are derived at draw time from the particle's normalized
age. Only the emitter's authored parameters serialize.

## Design

### `engine/particles/particle_emitter.hpp` — new fields + interpolation seam

The existing `color` (Vec3) and `size` (float) become the **start** (life = 0)
values. Three fields are added; the field set becomes:

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| … (existing emission/sim fields unchanged) … | | | |
| `color` | `Vec3` | `{1,1,1}` | Color at birth (was: constant color) |
| `end_color` | `Vec3` | `{1,1,1}` | Color at death (**new**) |
| `size` | `float` | `0.1` | Billboard edge length at birth (was: constant size) |
| `end_size` | `float` | `0.1` | Billboard edge length at death (**new**) |
| `alpha` | `float` | `1.0` | Opacity at birth (**new**) |
| `end_alpha` | `float` | `0.0` | Opacity at death (**new**) |

**Backward compatibility is by construction.** A freshly-added emitter with these
defaults renders exactly as today: constant white, constant `0.1` size, opacity
fading `1 → 0` over life. Existing serialized scenes lack the three new fields, so
they deserialize to these defaults — the current look is preserved (constant
color/size, linear alpha fade). `color`/`size` keep their names and positions, so
their serialized form is unchanged; only three new keys appear.

All three new fields are added to `reflect()` (placed next to their start
counterparts: `color, end_color, … size, end_size, alpha, end_alpha`), so they
serialize with the scene and show in the Inspector.

**Interpolation seam** — three pure free functions, the testable boundary shared
by the renderer and the unit tests:

```cpp
// t is a particle's normalized age (age / lifetime); clamped to [0,1] internally.
Vec3  color_at(const ParticleEmitter& e, float t);   // lerp(e.color,  e.end_color,  clamp01(t))
float size_at (const ParticleEmitter& e, float t);   // lerp(e.size,   e.end_size,   clamp01(t))
float alpha_at(const ParticleEmitter& e, float t);   // lerp(e.alpha,  e.end_alpha,  clamp01(t))
```

Interpolation is **linear** (`a + (b - a) * clamp01(t)`). A doc comment on the seam
records the intended generalization: replace the raw `t` with an eased `t` (per-
property easing curve) as a later follow-up — the call sites and signatures stay
the same, so easing drops in behind this seam without touching the renderer.

### `engine/render/render_system.hpp` — consume the seam

`ParticleSystem` is untouched (still a pure simulator; `ParticleInstance` still a
flat per-particle `{world_position, size, color, alpha}` — the GPU path is
unchanged). Only the instance-build loop changes: for each live particle compute
`t = (lifetime > 0) ? age / lifetime : 0`, then push
`ParticleInstance{ p.position, size_at(em, t), color_at(em, t), alpha_at(em, t) }`.
This replaces today's inline `em.size` / `em.color` and the hardcoded
`clamp(1 - age/lifetime)` alpha (the clamp now lives inside the helpers).

### Demo (`app/run/demo_scene.hpp`)

Update the default-scene fountain emitter to set an intentional gradient (a warm→
cool color shift, a slight shrink, and the default `1 → 0` alpha fade) so the
feature is visibly exercised by `mosaic editor` and `mosaic run --window`. No new
entities or components are added — only field values on the existing emitter — so
scene entity/component **counts are unchanged**.

## Testing (headless, TDD)

Extend `tests/unit/particles/particle_test.cpp`, tag `[particles]`.

1. `color_at` / `size_at` / `alpha_at` at `t = 0` return the start values exactly;
   at `t = 1` return the end values exactly; at `t = 0.5` return the midpoint.
2. Clamping — `t < 0` returns the start value and `t > 1` returns the end value
   (no extrapolation).
3. Default-emitter equivalence — with default fields, `color_at`/`size_at` are
   constant across `t`, and `alpha_at(t)` equals `1 - t` on `[0,1]` (documents that
   the defaults reproduce the previous hardcoded behavior).
4. `ParticleEmitter` reflection round-trip (`to_value` → `from_value`) preserves
   the three new fields alongside the existing ones.

Before finishing, grep for any test that snapshots a `ParticleEmitter`'s or the
default scene's full serialized form; the three added keys would perturb such a
snapshot. Update it if present. The default-scene entity/component **count** tests
are unaffected (no structural change) but will be re-run to confirm.

## Out of scope / follow-ups

- **Easing curves** — non-linear `t` remap per property (ease-in/out). Explicitly
  intended; the interpolation seam is shaped to accept it without renderer changes.
- Multi-stop gradients / keyframed color ramps (beyond a single start→end pair).
- Per-particle random color/size variance (seeded jitter, like velocity `spread`).
- Sub-frame spawn interpolation; triggered/re-armed bursts; script-driven emission
  (the other particles follow-ups).
