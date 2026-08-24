# Physics P3 — Primitive Shapes + Stacking (design)

**Decomposition:** [physics-decomposition](2026-08-24-physics-decomposition.md) (slice P3).
**Builds on:** P1 (`PhysicsBackend` + Box3D), P2 (`PhysicsSystem` + `Collider`), #18 reflection.
**Date:** 2026-08-24.

## Summary

Add **sphere** and **capsule** collider shapes to the physics system (box exists from P1/P2),
and prove **stacking** — multiple bodies colliding and resting stably. The demo gains varied
shapes settling onto the ground; the tests assert their rest states deterministically.

## Scope

**In:**
- A tagged neutral `Shape` (box/sphere/capsule) on the backend interface, replacing P1's
  `BoxShape`; the Box3D backend maps each to a `b3*` shape.
- Reflected `SphereCollider` + `CapsuleCollider` components (P2's `Collider` stays the box
  collider); registered in `register_builtin_components`.
- `PhysicsSystem` selects the shape from whichever collider component an entity has.
- Demo: a sphere + capsule + a small box stack dropping onto the ground.
- Headless deterministic tests: sphere rest, box-stack stability, capsule settle.

**Not a separate primitive — "plane":** Box3D has **no plane shape**; a "plane" is a large
flat static box, which the P2 ground already is. No new plane collider.

**Deferred (later / YAGNI):** mesh / heightfield / compound colliders, per-shape physical
materials (friction, restitution, bounciness), and a shape-picker inspector widget.

## Architecture

### Neutral shape (`engine/physics/physics_backend.hpp`)

Replace `BoxShape` with a tagged `Shape` (backend-facing, not reflected — an enum is fine):

```cpp
enum class ShapeKind { Box, Sphere, Capsule };

struct Shape
{
    ShapeKind                      kind = ShapeKind::Box;
    mosaic::foundation::math::Vec3 half_extents{0.5f, 0.5f, 0.5f}; // box
    float                          radius = 0.5f;                  // sphere / capsule
    float                          half_height = 0.5f;             // capsule half-length (local Y)
};
```

`BodyDef.shape` becomes `Shape` (was `BoxShape`). Defaults are unchanged for a box (kind
defaults `Box`, `half_extents` = `{0.5,0.5,0.5}`), so existing box call-sites keep working.

### Box3D backend mapping (`modules/physics_box3d/box3d_backend.cpp`)

`create_body` switches on `def.shape.kind`:

- **Box:** `b3BoxHull hull = b3MakeBoxHull(h.x, h.y, h.z); b3CreateHullShape(id, &sd, &hull.base);` (P1, unchanged).
- **Sphere:** `b3Sphere s{{0,0,0}, radius}; b3CreateSphereShape(id, &sd, &s);`
  (`b3Sphere { b3Vec3 center; float radius; }`).
- **Capsule:** `b3Capsule c{{0,-half_height,0}, {0,+half_height,0}, radius}; b3CreateCapsuleShape(id, &sd, &c);`
  (`b3Capsule { b3Vec3 center1; b3Vec3 center2; float radius; }` — a Y-axis capsule).

Verified in the pinned headers: `b3CreateSphereShape(b3BodyId, const b3ShapeDef*, const b3Sphere*)`,
`b3CreateCapsuleShape(b3BodyId, const b3ShapeDef*, const b3Capsule*)`.

### Reflected collider components (`engine/physics/physics_components.hpp`)

Keep `Collider { Vec3 half_extents; }` (the box collider). Add:

```cpp
struct SphereCollider  { float radius = 0.5f; };
struct CapsuleCollider { float radius = 0.5f; float half_height = 0.5f; };
```

Both reflected (single/two float members) and registered in `register_builtin_components`
as `"SphereCollider"` / `"CapsuleCollider"`. Separate components (not one tagged `Collider`)
so the inspector shows exactly the relevant fields per shape and each reflects trivially —
the reflected-property payoff. **One collider-shape component per entity** is assumed; if an
entity carries more than one, the `PhysicsSystem` uses the first found in priority order
(box, then sphere, then capsule).

### `PhysicsSystem` shape selection (`engine/physics/physics_system.hpp`)

In the lazy body-creation path, replace the `Collider`-only lookup with a helper that reads
whichever collider component is present and fills the neutral `Shape`:

```cpp
// returns false if the entity has no collider-shape component
static bool shape_for(World& world, EntityID e, Shape& out)
{
    if (const Collider* c = world.get_component<Collider>(e))
    { out = Shape{ShapeKind::Box, c->half_extents, 0.0f, 0.0f}; return true; }
    if (const SphereCollider* s = world.get_component<SphereCollider>(e))
    { out = Shape{ShapeKind::Sphere, {}, s->radius, 0.0f}; return true; }
    if (const CapsuleCollider* c = world.get_component<CapsuleCollider>(e))
    { out = Shape{ShapeKind::Capsule, {}, c->radius, c->half_height}; return true; }
    return false;
}
```

Body creation now iterates `for_each<RigidBody>`, calls `shape_for`; if it returns false (no
collider), the entity is skipped. Everything else in `PhysicsSystem` (step, write-back) is
unchanged. `access_` gains `reads<SphereCollider>().reads<CapsuleCollider>()`.

### Demo (`app/run/demo_scene.hpp`)

Alongside the existing dynamic box, add:
- A **sphere** (`SphereCollider{0.5}`, `RigidBody{dynamic}`, a scaled cube or the cube mesh
  as a stand-in visual) dropping beside the box.
- A **capsule** (`CapsuleCollider{0.5, 0.5}`, dynamic) dropping.
- A **second box stacked** above the first (or two boxes offset) to show a stable stack.

Meshes: P3 keeps using the cube mesh for all bodies (a dedicated sphere/capsule mesh is a
rendering nicety, out of scope) — the *physics* is the point; the visual is a cube standing
in for the collider. (Note this mismatch in the demo comment.) Camera framing widened if
needed so the extra bodies are in view.

## Testability

Headless + deterministic — real Box3D, real assertions (the physics payoff).

- **Sphere rest** (`tests/unit/physics/physics_shapes_test.cpp`): a dynamic `SphereCollider`
  entity (radius 0.5) at y=4 over the P2 ground (top y=0.5); tick 120×; assert
  `Transform.position.y` in `[0.9, 1.1]` (ground top + radius) and settled (Δ < 1e-3).
- **Box stack stability:** two dynamic box entities, one at y=2 and one at y=4 (both above
  the ground); tick 180×; assert the lower rests near y=1.0 and the upper near y=2.0 (each
  ±0.15), both settled, and upper.y > lower.y (didn't interpenetrate/collapse).
- **Capsule settle:** a dynamic `CapsuleCollider` entity dropped from y=4; tick 180×; assert
  it **fell** (`y_final < 4`) and **settled** (Δ < 1e-3) and is above the ground
  (`y_final > 0.4`) — a looser assertion, since an upright capsule can tip; exact rest height
  is not asserted.
- **Determinism:** each scenario's final state is bit-identical across two runs.
- **Reflection:** `SphereCollider`/`CapsuleCollider` round-trip through `to_value`/`from_value`
  and are registered (fold into the shapes test or the P2 components test).
- **Functional:** `mosaic editor`/`mosaic run --window` exit 0 with 0 validation errors; the
  sphere, capsule, box, and stack visibly settle on the ground.

## Ground rules honored

- **Tier contract:** `Shape`/colliders/`PhysicsSystem` stay neutral in `engine/physics/`;
  `b3Sphere`/`b3Capsule` mapping stays in `modules/physics_box3d`.
- **Determinism:** unchanged (one fixed step per tick, single-threaded backend).
- **Reflected-property surface:** the new colliders are ordinary reflected components.
- No new third-party deps. Catch2 test names avoid a leading `--`.

## Acceptance

- [ ] Tagged neutral `Shape`; Box3D backend creates box/sphere/capsule shapes.
- [ ] Reflected `SphereCollider`/`CapsuleCollider` registered + round-trip.
- [ ] `PhysicsSystem` selects the shape from the entity's collider component.
- [ ] Sphere-rest, box-stack, and capsule-settle headless tests green + deterministic.
- [ ] `mosaic editor`/`mosaic run --window` show varied shapes + a stack settling; exit 0,
      0 validation errors.
- [ ] Full unit suite green.

## Depends on / relates

- **Depends on:** P1 (`Shape`/`BodyDef` on the backend), P2 (`PhysicsSystem`, `Collider`,
  the drop demo). **Blocks:** none directly; P4 (raycasts) and P5 (contacts) are independent.
- **Relates:** later physical-materials work attaches friction/restitution to these shapes.
