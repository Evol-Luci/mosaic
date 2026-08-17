# Foundation math + Transform system (#21) — design

**Status:** Approved (brainstorming) — pending implementation plan
**Date:** 2026-08-16
**Layer:** Foundation (math) + Engine (transform) — part of the [v0.1 vertical slice](../../project/roadmap.md#the-v01-vertical-slice--the-first-mountain)
**Depends on:** #9 (World/ECS, `for_each`, `System`), #18 (reflection — to declare the component)
**Related:** [#21](https://github.com/Evol-Luci/mosaic/issues/21), [#26](https://github.com/Evol-Luci/mosaic/issues/26) (foundation/math tracking), [reflection](../../design/reflection.md), [next-steps](../../project/next-steps.md)

## Summary

The first concrete `System`: a `Transform` component (local position/rotation/scale
+ optional parent) and a `TransformSystem` that computes each entity's world
transform from its local + parent chain during `World::tick`. Because
`foundation/` has no math types yet, this also introduces a minimal `foundation/math`
(`Vec3`, `Quat`, `Mat4`), scoped to exactly what the transform system needs.

## Motivation

`World::tick` runs systems, but no concrete `System` exists to exercise it with real
work. The transform system is the natural first one: it turns authored local
transforms into world transforms, the computation every renderer, physics body, and
editor gizmo consumes. It also forces the math layer the whole downstream slice
(rendering, physics) depends on, establishing the coordinate and matrix conventions
once.

## Scope

**In scope:**
- `foundation/math`: `Vec3`, `Quat`, `Mat4` with only the operations the transform
  system needs, plus `reflect` overloads for `Vec3`/`Quat` (so `Transform`
  serializes).
- `Transform` component (reflected) + `WorldTransform` component (derived).
- `TransformSystem`: computes world transforms from local + parent chain during
  `update`, driven by `World` system registration.
- Parent reference forming a hierarchy.

**Explicitly out of scope** (future work):
- **General linear algebra** — dot/cross/length, projections, euler conversions,
  matrix decomposition, SIMD. Added when a consumer needs them (tracked in #26).
- **Transform memoization / dirty-flagging** — v0.1 recomputes every tick.
- **A third-party math dependency (GLM).** Hand-rolled keeps `foundation/`
  dependency-free.
- **Rendering/physics consumption of `WorldTransform`** — later systems.

## Design decisions

Settled during brainstorming:

1. **One combined effort; math folded into #21.** Minimal `foundation/math` first,
   then `Transform`/`TransformSystem`. A `foundation/math` tracking issue (#26) is
   filed for the record; the work ships with #21.
2. **World transform lives in a separate `WorldTransform` component.** `Transform`
   holds authored local data (serialized); the system writes a separate
   `WorldTransform{ Mat4 }` (derived, not serialized). Keeps authored and derived
   data cleanly apart so reflection only ever serializes authored fields.
3. **Column-major, right-handed, Y-up, radians (GLM-like).** Column-major storage,
   column vectors; `world = parent * local`, `local = T * R * S`. Matches the
   roadmap (right-handed, Y-up, meters, radians) and the graphics mainstream. This
   convention is load-bearing — rendering and physics inherit it.

## Architecture

### Boundaries

`foundation/math` depends on nothing above it (header-only, standard library only;
the `reflect` overloads are free templates needing no reflection header).
`engine/transform` depends on `foundation/math`, `foundation/core` (`EntityID`),
and `engine/` (`World`, `System`). Tier contract preserved.

### Files

| File | Change |
|------|--------|
| `foundation/math/vec3.hpp` | New. `Vec3` + ops + `reflect`. |
| `foundation/math/quat.hpp` | New. `Quat` + ops + `reflect`. |
| `foundation/math/mat4.hpp` | New. `Mat4` (column-major) + ops. |
| `engine/transform/transform.hpp` | New. `Transform` (reflected) + `WorldTransform`. |
| `engine/transform/transform_system.hpp` | New. `TransformSystem : System`. Header-only. |
| `tests/unit/math/{vec3,quat,mat4}_test.cpp` | New. `[math]` tests. |
| `tests/unit/transform/transform_system_test.cpp` | New. `[transform]` tests. |
| `tests/CMakeLists.txt` | Register the new test sources. |

No `CMakeLists.txt` library changes: all new code is header-only and reachable via
the existing `${CMAKE_SOURCE_DIR}` include root; tests link existing libs.

### `foundation/math`

Layering: `vec3` (base) → `quat` (uses `Vec3` for axis) → `mat4` (uses both).

```cpp
namespace mosaic::foundation::math {

struct Vec3 { float x = 0.0f, y = 0.0f, z = 0.0f; };
// operator+ , operator- , operator* (Vec3*float and float*Vec3), operator== / !=
template <class V> void reflect(V&, Vec3&);   // x, y, z

struct Quat { float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f; }; // identity default
Quat identity();
Quat from_axis_angle(const Vec3& axis, float radians);         // axis normalized internally
Quat normalized(const Quat&);
Quat operator*(const Quat&, const Quat&);                      // Hamilton compose
template <class V> void reflect(V&, Quat&);   // x, y, z, w

struct Mat4 { std::array<float, 16> m; };     // column-major: element(r,c) = m[c*4 + r]
Mat4 identity();
Mat4 operator*(const Mat4&, const Mat4&);
Mat4 translation(const Vec3&);
Mat4 scale(const Vec3&);
Mat4 rotation(const Quat&);                   // normalized-quat -> rotation matrix
Mat4 trs(const Vec3& pos, const Quat& rot, const Vec3& scl); // translation * rotation * scale
Vec3 transform_point(const Mat4&, const Vec3&);              // M * (x,y,z,1), returns xyz

} // namespace
```

Matrix conventions (column-major, `element(r,c) = m[c*4 + r]`):
- `translation(v)`: identity with column 3 = `(v.x, v.y, v.z)` → `m[12..14]`.
- `scale(v)`: diagonal `m[0]=v.x, m[5]=v.y, m[10]=v.z, m[15]=1`.
- `rotation(q)` for normalized `q=(x,y,z,w)`:
  `m00=1-2(y²+z²)`, `m01=2(xy-wz)`, `m02=2(xz+wy)`,
  `m10=2(xy+wz)`, `m11=1-2(x²+z²)`, `m12=2(yz-wx)`,
  `m20=2(xz-wy)`, `m21=2(yz+wx)`, `m22=1-2(x²+y²)`,
  stored column-major (`m[0]=m00, m[1]=m10, m[2]=m20, m[4]=m01, …`), `m[15]=1`.
- `operator*`: `result(r,c) = Σ_k A(r,k)·B(k,c)`.
- `transform_point(M,v)`: `x' = m[0]x+m[4]y+m[8]z+m[12]` (and y',z' analogously).
- `from_axis_angle(a,θ)`: `w=cos(θ/2)`, `xyz = normalize(a)·sin(θ/2)`.

### `engine/transform`

```cpp
namespace mosaic::engine::transform {

struct Transform
{
    foundation::math::Vec3 position;
    foundation::math::Quat rotation;
    foundation::math::Vec3 scale{1.0f, 1.0f, 1.0f};
    foundation::EntityID parent;   // nil (default) = root
};
template <class V> void reflect(V&, Transform&);   // position, rotation, scale, parent

struct WorldTransform { foundation::math::Mat4 matrix; }; // derived; not reflected

class TransformSystem : public ecs::System
{
public:
    TransformSystem();                       // access_.reads<Transform>().writes<WorldTransform>()
    void update(World& world, float dt) override;
};

} // namespace
```

### Transform algorithm

`update`, once per tick, for each entity `e` holding a `Transform`:

```
local(e) = Mat4::trs(t.position, t.rotation, t.scale)
world(e) = (t.parent valid && has Transform) ? world(parent) * local(e) : local(e)
```

`world(e)` is computed by walking the parent chain to the root (recursive), so the
result is correct regardless of `for_each` iteration order. The system then ensures
`e` has a `WorldTransform` and stores the matrix (add if absent, overwrite if
present). Adding `WorldTransform` while iterating `Transform` is safe — it is a
different component type, so it does not disturb the `Transform` storage under
iteration.

A depth guard (cap 256) stops a malformed parent cycle from overflowing the stack —
beyond the cap the chain is treated as ending (returns the accumulated matrix).
Acyclic hierarchies are the documented authoring assumption.

### Data flow

```
World::tick ──▶ TransformSystem::update
                  for each Transform e:
                    world(e) = compose parent chain (Mat4::trs each, left-multiply parents)
                    e.WorldTransform.matrix = world(e)   (add or overwrite)
```

Moving a parent's `Transform` then ticking recomputes children from the new parent
local — "moving a parent updates its children on the next tick".

## Error handling

- **Missing/invalid `parent`:** treated as root (`world = local`). A `parent` that is
  nil, not `is_valid`, or has no `Transform` short-circuits to local.
- **Parent cycle:** depth guard (256) prevents stack overflow; result is undefined
  but bounded (documented: hierarchies must be acyclic).
- **Non-normalized rotation:** `rotation(q)` assumes a normalized quat; callers use
  `normalized()` / `from_axis_angle` (which normalize). Documented, not guarded per
  call (hot path).

## Testing

**Math (`tests/unit/math/`, tag `[math]`):**
- `Vec3`: `+`, `-`, scalar `*` (both sides), `==`/`!=`.
- `Quat`: `identity` is `(0,0,0,1)`; `from_axis_angle` about an axis; `normalized`
  yields unit length; compose `*` (identity ∘ q = q).
- `Mat4`: `identity`; `translation`/`scale` element layout; `operator*` (identity is
  neutral); `trs` translation column equals `pos`; `transform_point` of the origin
  equals the translation; a 90°-about-Z rotation maps `(1,0,0)`→`(0,1,0)` (via
  `Catch::Approx`).

**Transform (`tests/unit/transform/`, tag `[transform]`):**
- Single entity: `Transform` position `(1,2,3)`, identity rotation, unit scale →
  after `tick`, `WorldTransform` translation is `(1,2,3)`.
- Hierarchy compose: parent at `(10,0,0)`, child local `(1,0,0)` with `parent` set →
  child world translation `(11,0,0)`.
- Rotated parent: parent rotated 90° about Z at origin, child local `(1,0,0)` →
  child world position ≈ `(0,1,0)` (`Approx`).
- Moving a parent updates children: change parent position, `tick`, child world
  translation shifts accordingly.
- Registration/drive: `TransformSystem` added via `World::add_system`, run by
  `World::tick`; the system reads `Transform` and writes `WorldTransform`.
- Reflection: `Transform` registered with a `ComponentReflection`, `to_value` →
  generic `construct` reproduces position/rotation/scale/parent.

Full suite stays green.

## Open questions / follow-ups

- **Perf:** recompute-per-entity re-walks ancestors; memoize world matrices within a
  tick (or process in topological/parent-first order) when scenes grow. Tracked as a
  follow-up.
- **Dirty-flagging:** only recompute changed subtrees — later, event-driven.
- **General math ops** (dot/cross/length, decomposition) — add on demand (#26).
- **Non-uniform scale through a rotated parent** shears children (expected with a
  matrix pipeline); revisit if a consumer needs decomposed TRS propagation.
</content>
