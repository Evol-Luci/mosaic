# Particle sub-frame spawn interpolation — Design

## Goal (Phase 4, particles follow-up)

Stop a **moving** emitter from clumping all of a tick's particles at one point.
Today every particle spawned during a `World::tick` appears at the emitter's
current `Transform.position`; when the emitter moves quickly (e.g. attached to the
orbiting demo cube), a tick's worth of continuous spawns lands in a single blob,
then the next tick's blob lands elsewhere — a beads-on-a-string look instead of a
smooth stream. This slice spreads a tick's **continuous** (rate-based) spawns
along the segment the emitter swept this tick, from its previous origin to its
current one.

Purely a `ParticleSystem` simulation improvement: no new component field, no
reflection change, no renderer change. It is a **no-op for a stationary emitter**
(previous origin == current origin), so all existing behavior and determinism are
preserved by construction.

## Design

### What interpolates, and what does not

- **Continuous (rate-based) emission** — spread along the swept path this tick.
- **Bursts** (on-sight and re-armed) — remain instantaneous: all burst particles
  spawn at the *current* origin. A burst is a single event, not a stream, so
  spreading it along the path would be wrong.

### `engine/particles/particle_system.hpp`

`EmitterState` gains one transient field:

```cpp
Vec3 last_origin{}; // emitter origin at the end of the previous tick (spawn-path start)
```

Per emitter each `update`:

1. Compute `origin` (current) as today: the entity's `Transform.position`, else
   `{0,0,0}`.
2. On first sight (state creation): set `st.last_origin = origin` so the first tick
   has a zero-length path (no interpolation before the emitter has moved).
3. Bursts (on-sight + re-arm) spawn at `origin` — unchanged.
4. **Continuous emission** — replace the single-origin spawn loop with a
   path-spread version. After `accumulator += rate * dt`, count how many particles
   `n` will spawn this tick (respecting the `max_particles` cap), then spawn each at
   an interpolated origin:

   ```cpp
   const Vec3 prev = st.last_origin;
   const Vec3 curr = origin;
   for (int i = 0; i < n; ++i)
   {
       const float f = (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
       const Vec3  spos = prev + (curr - prev) * f;      // path point for this spawn
       st.particles.push_back(spawn_particle(em, spos, st.rng));
   }
   st.accumulator -= static_cast<float>(n);              // exact carry, as before
   ```

   Fractions are `(i + 0.5) / n`, so `n` spawns are centered in equal sub-intervals
   of `(0,1)`; a single spawn (`n == 1`) lands at the path midpoint (`f = 0.5`), a
   fair "average position over the tick".
5. Integrate + age-out — unchanged.
6. At the end of the per-emitter body, set `st.last_origin = origin` so next tick's
   path starts where this one ended.

**Determinism & backward compatibility.** The RNG is still drawn once per spawn in
the same order (inside `spawn_particle`); only the spawn *position* changes, and
only when `prev != curr`. For a stationary emitter `prev == curr`, so every `spos`
equals `origin` and the result is **bit-identical** to today. The accumulator carry
is preserved exactly (`-= n`, the same total the old `while` loop subtracted).

### Scope note: position, not sub-frame age

Newly spawned particles are still integrated by the full `dt` this tick (as today);
this slice interpolates spawn *position* only. Back-dating each particle's age to
its sub-frame spawn time is a further refinement (listed below), not needed for the
anti-clumping win and kept out to preserve the exact accumulator/RNG behavior.

### Demo (`app/run/demo_scene.hpp`)

Attach a modest continuous `ParticleEmitter` to the existing orbiting scripted cube
(the `kOrbitScript` entity), which sweeps a circle each tick — so its trail is
smooth (path-spread) instead of blobbed. This adds a component to an existing
entity, not a new entity, so default-scene entity/component **counts are
unchanged**.

## Testing (headless, TDD)

Extend `tests/unit/particles/particle_system_test.cpp`, tag `[particles]`.

1. **Single continuous spawn lands at the path midpoint** — emitter with
   `rate = 10`, `lifetime = 100`, `initial_velocity = {0,0,0}`, `acceleration =
   {0,0,0}`, `emitting = true`, on a `Transform` at `A = (0,0,0)`. Tick `dt = 0.1`
   (spawns 1 at `A`, first tick → no interpolation). Move the `Transform` to
   `B = (10,0,0)`, tick `dt = 0.1` again (spawns 1). The second particle sits at
   `x == 5.0` (midpoint of `A→B`), not `x == 10` — proving interpolation.
2. **Multiple continuous spawns spread along the path** — same setup but
   `rate = 30` (3 spawns/tick). After the move+tick, the three new particles have
   `x == {5/3, 5, 25/3}` (i.e. `(i+0.5)/3 * 10`), a spread, not all at `x == 10`.
3. **A stationary emitter spawns at its origin (no-op)** — a non-moving emitter's
   continuous particles all spawn at the entity's `Transform.position` (guards the
   backward-compatible no-op path).
4. **A re-armed burst is not interpolated** — with the emitter moved `A→B`, a burst
   (via `burst_trigger`) spawns all its particles at `B` (the current origin), not
   spread along the path.
5. **Determinism under motion** — two worlds run the same move+tick schedule and
   produce bit-identical particle buffers.

(The existing "spawns particles at the entity's Transform position" and
determinism tests continue to pass unchanged, since they use stationary emitters.)

## Out of scope / follow-ups

- Sub-frame **age** integration (back-date a particle's age to its spawn time within
  the tick) for fully continuous fast streams.
- Interpolating along the emitter's *rotation* (for directional emission), not just
  position.
- Curved-path interpolation (this uses a straight segment prev→curr per tick).
