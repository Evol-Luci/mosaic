# Particle over-life easing curves — Design

## Goal (Phase 4, particles follow-up)

Let a `ParticleEmitter` interpolate each over-life property (color, size, alpha)
through a **non-linear easing curve** instead of only linear. This is the
follow-up the over-life-curves slice explicitly teed up: the `color_at` /
`size_at` / `alpha_at` seam was shaped so an eased remap of `t` drops in behind
it without touching the renderer.

Per-property easing: color, size, and alpha each pick their own curve, so (e.g.)
alpha can ease-out (linger, then fade fast) while size stays linear. Header-only,
no new dependencies, deterministic, headless-testable. Defaults reproduce today's
linear behavior exactly.

## Design

### `engine/particles/particle_emitter.hpp` — easing modes, fields, and remap

**Easing modes** — an unscoped `enum` with an explicit `int` underlying type, so
its values assign directly to the `int` fields below and serialize as plain
integers (the reflection layer supports `int` but not `enum class`):

```cpp
enum Easing : int { EaseLinear = 0, EaseIn = 1, EaseOut = 2, EaseInOut = 3 };
```

**New fields** on `ParticleEmitter`, each grouped with its property's existing
start/end pair (existing fields keep their names/positions):

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `color_easing` | `int` | `0` (`EaseLinear`) | Curve for `color` → `end_color` |
| `size_easing` | `int` | `0` (`EaseLinear`) | Curve for `size` → `end_size` |
| `alpha_easing` | `int` | `0` (`EaseLinear`) | Curve for `alpha` → `end_alpha` |

Default `0` = linear, so an existing emitter (and any scene deserialized without
these keys) behaves exactly as before. All three are added to `reflect()` next to
their property's fields, so they serialize and show in the Inspector (as an int
drag for now — a combo widget is a later UX nicety).

**Easing function** — pure, deterministic; `t` is already clamped to `[0,1]` by
the caller. Unknown modes fall back to linear:

```cpp
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
```

All four curves map `0 → 0` and `1 → 1`, so **endpoints are exact regardless of
easing** — a particle is always exactly `start` at birth and exactly `end` at
death; only the path between differs.

**Seam update** — the only change to the existing helpers is remapping `t` through
the property's easing before the lerp:

```cpp
inline Vec3 color_at(const ParticleEmitter& e, float t)
{
    const float u = apply_easing(e.color_easing, clamp01(t));
    return e.color + (e.end_color - e.color) * u;
}
// size_at  uses e.size_easing;  alpha_at uses e.alpha_easing — same shape.
```

### `engine/render/render_system.hpp` — no change

The renderer already feeds `t = age / lifetime` into `color_at`/`size_at`/
`alpha_at`. Easing lives entirely behind that seam; `ParticleInstance` and the GPU
path are untouched. This is the extensibility the over-life-curves slice designed
for.

### Demo (`app/run/demo_scene.hpp`)

Give the default-scene fountain a subtle easing (e.g. `alpha_easing = EaseOut` so
particles hold their opacity then fade quickly near end-of-life) so the feature is
exercised in `mosaic editor` / `mosaic run --window`. No new entities or
components — only field values on the existing emitter — so scene entity/component
counts are unchanged.

## Testing (headless, TDD)

Extend `tests/unit/particles/particle_test.cpp`, tag `[particles]`.

1. `apply_easing` endpoints — every mode (`EaseLinear`/`EaseIn`/`EaseOut`/
   `EaseInOut`) returns `0` at `t=0` and `1` at `t=1`.
2. `apply_easing` midpoints — `EaseIn(0.5) == 0.25`, `EaseOut(0.5) == 0.75`,
   `EaseInOut(0.5) == 0.5`, `EaseLinear(0.5) == 0.5`.
3. `apply_easing` unknown mode — an out-of-range mode value falls back to linear
   (`apply_easing(99, t) == t`).
4. Helper easing applied — with `size_easing = EaseIn`, `size_at(e, 0.5)` reflects
   the eased fraction `0.25` (not the linear `0.5`); analogous checks for
   `color_easing` / `alpha_easing`.
5. Endpoint invariant under easing — with any non-linear easing set, `color_at`/
   `size_at`/`alpha_at` still return exactly the start values at `t=0` and the end
   values at `t=1`.
6. Reflection round-trip — `color_easing`/`size_easing`/`alpha_easing` survive
   `to_value` → `from_value` alongside the existing fields.
7. Default is linear — a default emitter (`*_easing == 0`) yields linear
   interpolation across `t` (guards the backward-compatibility contract).

## Out of scope / follow-ups

- Additional curves — cubic/quartic, elastic, bounce, back.
- Per-property custom control points / arbitrary keyframed easing.
- An Inspector combo/dropdown widget for the easing mode (shows as an int drag
  today).
- Applying easing to spawn-rate or velocity (this slice is over-life visual
  properties only).
