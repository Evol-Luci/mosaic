# Particles (emitter + deterministic sim) — Design

## Goal (Phase 4, subsystem 4)

A CPU particle subsystem: attach a `ParticleEmitter` to an entity and it spawns,
ages, and integrates particles each tick. Maintainer-approved scope: **core sim,
rendering deferred** — exactly how navigation shipped headless with no visuals.

Follows the established Phase-4 shape (animation → sequencer → navigation): a
**neutral pure-C++ model + a reflected component + a generic `System`**,
header-only under `engine/particles/`, no new dependencies, fully headless, and
**bit-identical deterministic** across runs (the guarantee every recent slice
holds). Live particle buffers are transient system-owned state — like
`WorldTransform`, physics bodies, and the nav step-target cache — **not
serialized**; only the emitter's authored parameters serialize.

## Design

### `engine/particles/particle.hpp` — neutral model (no ECS/component deps)

- `struct Particle { Vec3 position; Vec3 velocity; float age; float lifetime; }`.
  A particle is alive iff `age < lifetime`.
- `struct Rng { std::uint64_t state; float next_unit(); }` — a **seeded**
  splitmix64 step returning a deterministic value in `[0, 1)`. This is why spawn
  jitter is bit-identical across runs: no `std::rand()`, no global RNG state.
- `void integrate(Particle& p, const Vec3& accel, float dt)` — semi-implicit:
  `p.velocity += accel * dt; p.position += p.velocity * dt; p.age += dt`.

### `engine/particles/particle_emitter.hpp` — reflected component

`ParticleEmitter` (reflected; registered as `"ParticleEmitter"`; serializes with
the scene and shows in the Inspector). Authored parameters only:

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `emitting` | `bool` | `true` | Whether continuous emission is running |
| `rate` | `float` | `10` | Particles per second (continuous) |
| `burst` | `int` | `0` | One-shot count fired once on activation |
| `lifetime` | `float` | `1` | Seconds each particle lives |
| `initial_velocity` | `Vec3` | `{0,1,0}` | Base spawn velocity |
| `spread` | `float` | `0` | Per-axis symmetric velocity jitter magnitude (seeded) |
| `acceleration` | `Vec3` | `{0,-9.81,0}` | Constant per-particle acceleration (gravity) |
| `seed` | `std::uint64_t` | `1` | PRNG seed — determinism knob |
| `max_particles` | `int` | `1000` | Pool cap (safety) |

- `spawn_particle(const ParticleEmitter& e, const Vec3& origin, Rng& rng) -> Particle`
  — `position = origin`, `velocity[axis] = e.initial_velocity[axis] + e.spread * (2*rng.next_unit() - 1)`
  per axis, `age = 0`, `lifetime = e.lifetime`.

### `engine/particles/particle_system.hpp`

`ParticleSystem : ecs::System`. Owns transient per-emitter state keyed by entity
id (the `NavigationSystem::targets_` pattern):

```
struct EmitterState { std::vector<Particle> particles; Rng rng; float accumulator; bool bursted; };
std::unordered_map<std::uint64_t, EmitterState> emitters_;
```

Each `update(world, dt)`, per entity with a `ParticleEmitter`:

1. Mark it seen. Spawn `origin` = the entity's `Transform.position` if it has one,
   else `{0,0,0}` (same authored-`Transform` read as nav — not `WorldTransform`).
2. First time the entity is seen: create `EmitterState`, seed `rng` from
   `emitter.seed`, and if `burst > 0` spawn `burst` particles immediately (capped
   at `max_particles`), setting `bursted = true`.
3. If `emitting`: `accumulator += rate * dt`; while `accumulator >= 1` and
   `particles.size() < max_particles`, `spawn_particle(...)` and `accumulator -= 1`.
   (Carrying the fractional accumulator across ticks keeps the rate exact and
   deterministic.)
4. `integrate` every particle by `emitter.acceleration`.
5. Erase aged-out particles (`age >= lifetime`) — `erase`/`remove_if`, order-stable.

After the pass, prune `EmitterState`s for entities no longer present (mirrors the
nav cache prune).

**Query seam** (for tests + the future renderer):
`const std::vector<Particle>* particles_for(EntityID) const` — the live pool, or
`nullptr` if the entity has none.

### Determinism

Per-emitter state is fully independent, its `Rng` is seeded from the component, and
the fractional `accumulator` carries across ticks. So the same tick schedule yields
**bit-identical per-entity particle buffers**. `std::unordered_map` iteration order
is irrelevant: pools never interact, so the per-entity determinism the test asserts
does not depend on map order.

### Wiring

- `app/run/demo_scene.hpp::setup_runtime` — add `ParticleSystem` **after**
  `SequenceSystem`, before physics. It reads authored `Transform.position` and
  writes nothing back to any component, so ordering is not load-bearing; grouping it
  with the other gameplay systems is just for consistency. No constructor deps.
- `engine/reflection/builtin_components.hpp` — register
  `"ParticleEmitter"` alongside `AnimationPlayer`/`Sequence`/`NavAgent`.

## Testing (headless, TDD)

`tests/unit/particles/particle_test.cpp` and `particle_system_test.cpp`, tag
`[particles]`, wired into `tests/CMakeLists.txt`.

**Model (`particle_test.cpp`):**
1. `Rng` — same seed reproduces the same sequence; a different seed diverges; every
   value is in `[0, 1)`.
2. `integrate` — under constant acceleration, velocity/position/age advance as
   expected over several steps.
3. `spawn_particle` — with `spread == 0`, velocity equals `initial_velocity`
   exactly; with `spread > 0`, each axis stays within
   `initial_velocity[axis] ± spread`.
4. `ParticleEmitter` reflection round-trip (`to_value` → `from_value` preserves
   every field).

**System (`particle_system_test.cpp`):**
5. Continuous emission — after simulating `t` seconds at `rate`, the live count
   matches the expected spawned-minus-aged-out count.
6. Burst — `burst = K`, `emitting = false` spawns exactly `K` particles on the
   first tick and none after.
7. Age-out — a particle is gone once `age >= lifetime`.
8. `max_particles` cap — the pool never exceeds it even at a high rate.
9. Spawn origin — particles spawn at the entity's `Transform.position`.
10. Prune — destroying the emitter entity drops its pool (`particles_for` → null).
11. **Determinism (headline)** — two independent worlds run the same schedule and
    produce bit-identical particle buffers (position/velocity/age) per entity.

## Out of scope / follow-ups

- **Rendering** (instanced billboards / point sprites) — the first consumer of
  `particles_for`.
- Color-over-life and size-over-life curves (reflected start/end params).
- Sub-frame spawn interpolation (spawn particles along the tick, not all at
  tick start) to smooth high-speed emitters.
- Burst re-arming / explicitly triggered bursts; script- or event-triggered
  emission (`emit`-driven bursts).
- A `setup_runtime` demo emitter (once rendering exists to show it).
- World-space vs local-space emission modes; emission shapes (cone/sphere/box).
