# Particle triggered / re-armed bursts — Design

## Goal (Phase 4, particles follow-up)

Let a `ParticleEmitter`'s burst fire **again** after the initial one-shot, instead
of only when the emitter is first seen. Today `burst` particles spawn once, when
the `ParticleSystem` first observes the emitter, and there is no way to fire
another burst. This slice adds a data-driven **edge trigger** so a burst can be
re-armed at runtime — enabling gameplay effects (explosions, hit sparks, jumps)
and serving as the primitive the upcoming script-driven-emission slice drives.

Follows the established particles shape: a reflected field + transient
system-owned state, header-only, no new dependencies, deterministic and
headless-testable. The `ParticleSystem` stays a pure simulator that **writes
nothing back to components** — the trigger is observed, not mutated.

## Design

### The mechanism: an edge-triggered counter

The `ParticleSystem` never mutates components (its live buffers are transient
state keyed by entity id). A self-resetting `bool fire` would violate that (the
system would have to write `false` back) and would serialize a transient flag.
Instead the emitter carries a monotonic-ish **counter**: bump it to request a
burst. The system remembers the last value it acted on (in its own transient
per-emitter state) and fires when the component's value differs.

### `engine/particles/particle_emitter.hpp` — one new field

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `burst_trigger` | `int` | `0` | Change this (e.g. `burst_trigger += 1`) to fire another burst of `burst` particles |

`burst` keeps its meaning ("how many particles per burst"); `burst_trigger` is the
edge signal. Added to `reflect()` (next to `burst`), so it serializes and shows in
the Inspector. Default `0` → no behavior change for existing emitters/scenes.

### `engine/particles/particle_system.hpp`

`EmitterState` gains `int last_burst_trigger = 0`.

Refactor the burst-spawn loop (currently inline in the first-sight branch) into a
small local helper shared by both the on-sight burst and the re-arm burst, e.g.:

```cpp
auto fire_burst = [&](EmitterState& st)
{
    for (int i = 0; i < em.burst &&
         static_cast<int>(st.particles.size()) < em.max_particles; ++i)
    {
        st.particles.push_back(spawn_particle(em, origin, st.rng));
    }
};
```

Per emitter each `update`:

1. **First sight** (state created): seed `rng` from `em.seed`, set
   `last_burst_trigger = em.burst_trigger` (so the counter does not also fire on
   creation), then `fire_burst(st)` — the existing one-shot burst, unchanged.
2. **Re-arm:** if `em.burst_trigger != st.last_burst_trigger`, `fire_burst(st)` and
   set `st.last_burst_trigger = em.burst_trigger`.
3. Continuous emission (`emitting`/`rate`), integrate, age-out — all unchanged.

**Semantics:** any observed change to `burst_trigger` fires **exactly one** burst
that tick, regardless of how many times the value changed since the previous tick
(simple, deterministic, no risk of a runaway spawn count). The comparison is
inequality, not delta, so decrements or wrap-around also count as "changed" — the
field is a signal, not a quantity.

### Determinism

The counter comparison is deterministic and bursts draw from the emitter's seeded
`rng`, so two runs with the same trigger schedule produce bit-identical particle
buffers. Per-emitter state stays independent; `unordered_map` iteration order is
irrelevant (pools never interact).

### Renderer / wiring

No change. `RenderSystem`, `setup_runtime`, and `ParticleInstance` are untouched;
this is purely emitter data + system logic.

### Demo — deferred (documented)

A trigger needs a runtime stimulus (input or a script), which the **script-driven
emission** slice provides next. A non-scripted demo would be artificial (nothing
would bump the counter), so no default-scene change ships here — mirroring how
navigation shipped headless. Verification is headless tests + a no-crash smoke
run. Scene entity/component counts are unaffected.

## Testing (headless, TDD)

Extend `tests/unit/particles/particle_system_test.cpp` (tag `[particles]`) and the
emitter reflection test in `tests/unit/particles/particle_test.cpp`.

1. **Re-arm fires again** — emitter with `burst = K`, `emitting = false`. Tick once
   (K particles from the on-sight burst). Bump `burst_trigger` (via
   `world.get_component<ParticleEmitter>(e)->burst_trigger += 1`), tick again with a
   `dt` short enough that none age out → live count is `2*K`.
2. **No spurious re-arm** — same setup, but tick several more times *without*
   changing `burst_trigger` → no additional burst (count stays at the on-sight `K`,
   modulo age-out with a long lifetime).
3. **One burst per tick regardless of multiple bumps** — increment `burst_trigger`
   three times between ticks, then tick once → exactly one additional burst (`K`),
   not three.
4. **Cap respected** — a re-arm never pushes the pool past `max_particles`.
5. **Determinism** — two independent worlds run the same trigger+tick schedule and
   produce bit-identical particle buffers.
6. **Backward compatibility** — with `burst_trigger` left at `0`, behavior is the
   existing burst-once-on-first-sight (guards no accidental extra fire).
7. **Reflection round-trip** — `burst_trigger` survives `to_value` → `from_value`
   alongside the existing fields.

## Out of scope / follow-ups

- **Script-driven emission** — bind `burst_trigger`/`emitting`/`rate` to Python so a
  script can fire bursts (the natural next slice; it supplies the demo stimulus).
- Auto/periodic re-arm on an interval timer.
- Per-burst parameter overrides (a burst with a different count/velocity than the
  emitter's steady state).
- Sub-frame spawn interpolation for a moving emitter (separate follow-up).
