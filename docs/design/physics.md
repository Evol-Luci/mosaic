# Physics

Mosaic's physics is Box3D — behind a wall. The public engine API exposes neutral
types only; Box3D's `b3*` API never leaks. See the
[physics boundary](../architecture/boundaries.md#2-the-physics-boundary).

## Public API surface

```text
PhysicsWorld     RigidBody     Collider     Shape
Joint            RaycastResult ContactEvent
```

Developers work with these. That Box3D sits underneath is an implementation
detail — deliberately, because Box3D 0.1.0 is alpha and its API is still moving.
The abstraction lets us track (or replace) the backend without churning game code.

## Determinism as a boundary guarantee

Box3D is deterministic and supports record/replay. Mosaic elevates that from a
backend feature to a **guarantee of the public API** — a gap neither source plan
closed explicitly. The `PhysicsBackend` contract requires:

- **Fixed timestep + substeps.** Simulation advances in fixed `dt` with a
  configurable substep count, decoupled from frame/render rate.
- **Stable ordering.** Body and contact iteration order is stable across runs, so
  the same inputs produce the same outputs bit-for-bit on a given platform.
- **Deterministic queries.** Raycasts, shape casts, and overlaps return results in
  a stable, defined order.
- **Record / replay at the abstraction layer.** Recording and deterministic replay
  are exposed through the *public* API, not only the backend — so tools, tests,
  and agents can step tick-by-tick and reproduce runs regardless of the backend in
  use.

This is what lets agents step physics deterministically to evaluate reward/loss
functions, and lets tests assert exact trajectories.

## Live-link preview & bake (Studio feature)

The rich Blender-side physics authoring from the source plans lives in **Mosaic
Studio's live-link preview**, above the engine boundary — *not* in the engine (see
[boundaries](../architecture/boundaries.md#where-the-blender-bridge-code-lives)).
It provides:

- **Compound colliders.** A single body hosts multiple shapes at local transform
  offsets; child meshes flagged as colliders are extracted relative to the root
  body's world matrix.
- **Joints.** Fixed, revolute (hinge), prismatic (slider), distance/spring, and
  generic 6-DoF — with limits, motors, and spring/damper parameters. World-space
  joint frames are transformed into each body's local anchor and axis before
  registration.
- **Headless bake-to-F-curve.** The simulation steps deterministically in native
  memory, then bulk-writes trajectories into Blender F-curves (pre-allocated
  keyframe points via `foreach_set`), with optional tolerance-based decimation to
  clean resting/linear motion.

These are authoring conveniences that produce animation data or preview the
engine's physics inside Blender. The engine's own runtime physics always goes
through `PhysicsBackend` → Box3D; the two share concepts, not code paths.

## What v0.1 includes

Per the [roadmap](../project/roadmap.md), v0.1 physics is deliberately narrow:
rigid bodies, primitive colliders, raycasts, contacts, and a deterministic step.
Joints, character controllers, and the full bake toolchain come later.
