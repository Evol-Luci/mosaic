# VFX V1 — Trails (billboard-chain) — Design

## Goal (Phase 4, VFX pillar — first slice)

Stand up the VFX pillar with **trails**: a `TrailRenderer` component leaves a
fading tail behind a moving entity. This slice renders the trail as a **chain of
camera-facing billboards reusing the existing `draw_particles` seam** — so it needs
**no new device method and no new Vulkan pipeline**, yet is visible on hardware and
fully headless-testable. It mirrors the particles shape (reflected component +
`ecs::System` with transient per-entity state + a `RenderSystem` pass), so it slots
into the engine's established patterns.

A connected "ribbon" (continuous strip geometry) is a **later refinement**; V1 is
the billboard-chain.

## Design

### `engine/vfx/trail_renderer.hpp` — reflected component

```cpp
struct TrailRenderer
{
    Vec3  color           = Vec3{1.0f, 1.0f, 1.0f}; // trail color
    float size            = 0.1f;                    // billboard edge length at the head
    int   max_points      = 32;                      // history length (ring-buffer cap)
    float sample_interval = 0.03f;                   // seconds between position samples
};
```

Reflected + registered as `"TrailRenderer"` in `register_builtin_components`
(serializes with the scene, shows in the Inspector). The live position history is
transient system state, **not** serialized (like particle pools / `WorldTransform`).

### `engine/vfx/trail_system.hpp` — `TrailSystem : ecs::System`

Owns transient per-entity position history keyed by entity id (the
`ParticleSystem`/`NavigationSystem` pattern):

```cpp
struct TrailState { std::vector<Vec3> points; float accumulator = 0.0f; };
std::unordered_map<std::uint64_t, TrailState> trails_;
```

Each `update(world, dt)`, per entity with a `TrailRenderer` (mark it `seen`):

1. Read the entity's `Transform.position` (skip the entity if it has no Transform).
2. On first sight, seed the history with the current position (so a trail has a head
   immediately).
3. `accumulator += dt`; **if** `accumulator >= sample_interval`, push the current
   position and `accumulator -= sample_interval` (one sample per tick max — an `if`,
   not a `while`, so a long tick can't spam duplicate points).
4. If `points.size() > max_points`, drop oldest points from the front until within
   the cap (`max_points >= 1` enforced; a non-positive cap keeps just the head).

After the pass, prune `TrailState` for entities no longer present (mirrors the nav /
particle prune).

**Query seam** (renderer + tests): `points_for(EntityID) -> const std::vector<Vec3>*`
— the history **oldest → newest**, or `nullptr` if none / the entity is invalid
(same post-destruction validity check as `ParticleSystem::particles_for`).

**Determinism:** history is a pure function of the sampled positions + the fixed-dt
accumulator; two runs of the same schedule produce identical buffers.

### `engine/render/render_system.hpp` — extend the existing particle pass

No new device method. In the block that already gathers `ParticleInstance`s and
issues the single `draw_particles`, also iterate `TrailRenderer` entities (via
`world.get_system<vfx::TrailSystem>()`), and for each trail's `points` append a
`ParticleInstance` per point:

- For point `i` of `n` (0 = oldest, n-1 = newest): `frac = (i + 1) / n` in `(0, 1]`.
- `ParticleInstance{ position = points[i], size = trail.size * frac,
  color = trail.color, alpha = frac }` — newest is full-size/opaque, oldest is
  small/faint.

Trail billboards join the same `instances` vector as particles, so the existing
single `draw_particles(instances, params)` renders both. (If there is no
`TrailSystem`, the trail loop is skipped — same guard style as the particle pass.)

### Wiring

- `app/run/demo_scene.hpp::setup_runtime` — add `TrailSystem` alongside
  `ParticleSystem` (reads Transform, writes nothing back; order not load-bearing).
- `engine/reflection/builtin_components.hpp` — register `"TrailRenderer"`.
- Demo: attach a `TrailRenderer` to the **falling sphere** (existing entity), so a
  fading tail follows it as it drops — no new entity, default-scene counts unchanged.

## Testing (headless, TDD)

`tests/unit/vfx/trail_system_test.cpp` (tag `[vfx]`, added to `tests/CMakeLists.txt`)
and an extension to the render-system tests.

1. **Samples at the interval** — a `TrailRenderer` with `sample_interval = 0.1`; a
   moving Transform ticked at `dt = 0.1` accumulates one point per tick; the newest
   point equals the current position.
2. **Respects `max_points`** — after many samples the history never exceeds
   `max_points`, and the oldest points are the ones dropped (head retained).
3. **Prune on destroy** — destroying the entity drops its history
   (`points_for → nullptr`).
4. **Determinism** — two worlds run the same move+tick schedule → bit-identical
   histories.
5. **Reflection round-trip** — `TrailRenderer` fields survive `to_value` →
   `from_value`.
6. **Builtin registration** — `types.has("TrailRenderer")`.
7. **Render taper** (in `render_system_test.cpp`, `[render]`) — with a `TrailSystem`
   holding `n` points, `RenderSystem` emits `n` extra billboard instances whose
   `alpha`/`size` increase from oldest→newest (newest `alpha == 1`, `size ==
   trail.size`), batched into the single `draw_particles` call.

## Out of scope / follow-ups (later VFX slices)

- Connected **ribbon** geometry (a continuous camera-facing strip with proper UVs),
  a dedicated `draw_ribbon` device seam + Vulkan pipeline.
- Per-trail texture, width-over-length curve, color-over-length gradient (reuse the
  particle over-life easing idea).
- Distance/pause behavior (freeze sampling when the emitter is still), world vs local
  space.
- Decals and screen/post-processing effects (separate VFX slices).
