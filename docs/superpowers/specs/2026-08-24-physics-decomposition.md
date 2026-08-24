# Physics (Box3D) — Decomposition

**Roadmap pillar:** [Phase 2 — Simulation](../../project/roadmap.md#phases); the physics
portion of the [v0.1 vertical slice](../../project/roadmap.md#the-v01-vertical-slice--the-first-mountain).
**Design intent:** [design/physics.md](../../design/physics.md),
[architecture/boundaries.md §2 — the physics boundary](../../architecture/boundaries.md).
**Status:** Accepted (decomposition; each sub-issue gets its own spec).
**Date:** 2026-08-24.

This is a **decomposition**, not an implementation spec. It breaks the physics pillar into
buildable sub-slices, fixes the cross-cutting decisions they all inherit, and sets their
order. Each sub-slice is then designed and built through its own brainstorm → spec → plan →
TDD cycle — mirroring the [rendering](2026-08-17-rendering-decomposition.md) and
[Studio](2026-08-22-studio-decomposition.md) decompositions.

## Purpose

Physics is what makes **Play** *simulate* instead of replaying a scripted spin — the
"First World → Simulation" leap. v0.1 physics is deliberately narrow (per the roadmap):
**rigid bodies, primitive colliders, raycasts, contacts, and a deterministic step.** Joints,
character controllers, and the Blender bake toolchain are explicitly **later**.

## Scope (v0.1)

**In:** rigid bodies (dynamic + static), primitive colliders (box, sphere, capsule, plane),
gravity, a deterministic fixed-timestep step with substeps, raycasts/queries, and contact
events — all behind a neutral `PhysicsBackend`, integrated into the ECS via a `PhysicsSystem`.

**Deferred (post-v0.1, their own future work):** joints (fixed/hinge/slider/spring/6-DoF),
character controllers, compound colliders, shape casts/overlaps beyond raycast, and the
Blender-side live-link physics authoring + bake-to-F-curve (a Studio feature above the
engine boundary — see [physics.md](../../design/physics.md)).

## Architectural spine (every slice honors this)

### The dependency: Box3D, pinned, walled off

Box3D ([github.com/erincatto/box3d](https://github.com/erincatto/box3d)) — Erin Catto's 3D
engine, **MIT**, **C17** core, CMake/CMakePresets, deterministic with record/replay — is the
backend. It is **alpha** (v0.1.0, API still moving), so:

- Pulled via **FetchContent, pinned to a specific commit** (not a moving branch). Candidate
  pin at decomposition time: `30c67b5e6d0a3a66f0f506c69ce9e9e0587e3b7c` (2026-08-13); the
  exact SHA is captured when **P1** wires it.
- Compiled as a static lib in a **new `modules/physics_box3d` tier**, mirroring
  `modules/render_vulkan`. The `b3*` API **never leaves that module** — no `b3World`,
  `b3Body`, `b3Shape` in any engine/game/studio header.

### The boundary: a neutral `PhysicsBackend`

A neutral interface in **`engine/physics/`**, exactly like `engine/render/render_device.hpp`:

```text
PhysicsBackend  (interface; modules/physics_box3d implements it)
PhysicsWorld    RigidBody     Collider     Shape
RaycastResult   ContactEvent
```

Engine/game/studio code sees only these neutral types. A future backend swap (or a headless
test double) implements the same interface. Per [boundaries.md §2](../../architecture/boundaries.md),
this is a hard wall, and **determinism is a guarantee of the public interface**, not just a
Box3D feature:

- **Fixed timestep + configurable substeps**, decoupled from frame/render rate.
- **Stable ordering** of bodies and contacts across runs.
- **Deterministic queries** (raycasts return a stable, defined order).
- **Record / replay** exposed through the public API (so tools, tests, and agents can step
  tick-by-tick and reproduce runs) — introduced as the API is built, hardened in the final
  slice.

### ECS integration: a `PhysicsSystem`

A concrete `System` (like `TransformSystem`/`RenderSystem`) owns a `PhysicsWorld` and, each
`World::tick`: reads authored `Transform` + reflected `RigidBody`/`Collider` components,
steps the physics world at the fixed `dt`, and writes simulated poses back to `Transform`.
This is where physics meets the existing tick order (drain events → run systems → commit
destructions).

### Testability — the standout advantage

Unlike rendering (GPU, un-screenshottable in this sandbox), **physics is inherently headless
and deterministic**, so most slices get **real unit assertions on exact trajectories** — a
box's rest height, a stack's stability, a ray's hit point, a contact's begin/end tick. The
Box3D determinism guarantee makes "same inputs → identical output" a testable property. Only
the *visible* demo (P2's box drop in the editor) leans on functional verification.

## Sub-slice breakdown & order

1. **P1 — backend bootstrap + falling box (headless).** The `PhysicsBackend` interface +
   neutral `PhysicsWorld`/`RigidBody`/box `Collider`/`Shape` types in `engine/physics/`; the
   `modules/physics_box3d` tier with Box3D wired (FetchContent, pinned) implementing the
   interface; gravity + fixed-step `step(dt)`; create a dynamic box body + a static ground,
   step, read back the pose. **Deterministic unit test: the box falls and comes to rest on
   the ground at the expected height, bit-identical across runs.** No ECS, no visuals — the
   "clear screen" equivalent; proves the wall + stepping + determinism in isolation.
2. **P2 — ECS `PhysicsSystem` (visible drop).** Reflected `RigidBody` + `Collider`
   components; a `PhysicsSystem` that builds backend bodies from ECS entities, steps in
   `World::tick`, and syncs `Transform`. **`mosaic editor` shows a box dropping onto a ground
   plane in the viewport** — Play finally simulates. Headless test: entity `Transform`s
   update deterministically; functional: the drop renders + exits 0, 0 validation errors.
3. **P3 — primitive shapes + stacking.** sphere, capsule, and plane shapes (box from P1);
   the `Collider` carries shape params; multiple bodies collide and rest. Headless tests:
   a sphere rolls/settles, a small stack stays stable, resting contacts hold.
4. **P4 — raycasts + queries.** `PhysicsWorld::raycast(origin, dir, max) -> RaycastResult`
   (neutral: hit bool, body handle, point, normal, distance), with deterministic ordering
   for multiple candidates. Headless tests: hit/miss, nearest-hit selection, stable order.
5. **P5 — contact events + determinism hardening.** `ContactEvent` (begin/end, the two
   bodies) emitted through the #11 `EventBus`; plus the explicit substep-count config and a
   cross-run **record/replay determinism** test that asserts identical trajectories from
   identical inputs. Headless tests: contact begin/end fire on the expected ticks.

**Suggested order:** P1 → P2 → P3 → P4 → P5 (linear). First buildable is **P1**.

## Cross-cutting decisions (fixed here)

- **Box3D** is the backend, **FetchContent-pinned to a commit**, walled in
  `modules/physics_box3d` (MIT license — compatible with the project).
- **Neutral `PhysicsBackend`** in `engine/physics/`; `b3*` never leaks.
- **Determinism is a public guarantee** (fixed step + substeps, stable ordering,
  deterministic queries, record/replay), introduced from P1 and hardened in P5.
- **`PhysicsSystem`** integrates into `World::tick`, syncing `Transform` ↔ bodies.
- **Headless-first:** P1 is a pure headless, deterministically-unit-tested core; visuals
  arrive in P2. Physics gets real trajectory assertions, not just "exits 0."
- Each sub-slice gets its own brainstorm → spec → plan → TDD cycle.

## Tier & dependency contract

- New **`modules/physics_box3d`** tier (like `modules/render_vulkan`): depends on
  `engine/physics` (the interface) + Box3D; nothing depends on it except the app/composition
  root that selects the backend. `foundation/` and the rest of `engine/` never see Box3D.
- `engine/physics/` (neutral interface + types) depends only on `foundation/` (math, ids).
- `PhysicsSystem` lives in `engine/physics/`, depends on the neutral interface +
  `World`/`Transform` — never on `modules/physics_box3d`.

## Acceptance (tracking-level; each sub-issue refines its own)

- [ ] A neutral `PhysicsBackend` + Box3D backend behind the wall; deterministic falling box (P1).
- [ ] `PhysicsSystem` + reflected components; a visible box drop in the editor (P2).
- [ ] Primitive shapes + stable stacking (P3).
- [ ] Raycasts returning neutral `RaycastResult` with deterministic ordering (P4).
- [ ] Contact events via the EventBus + a cross-run determinism/replay test (P5).

## Depends on / relates

- **Depends on:** `foundation/math` (transforms), the runtime `World` + `System` contract
  (#9/#22), the `EventBus` (#11, for contact events), the transform system (poses), and the
  viewport (#37d, to see P2's drop). CMake FetchContent (existing pattern).
- **Relates:** scripting (a future pillar) will drive bodies through the reflected API;
  the Blender live-link physics authoring is a deferred Studio feature above this boundary.
