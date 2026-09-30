# Scripting — Physics Bindings (design)

**Date:** 2026-09-29
**Status:** approved (design), pending implementation plan
**Branch:** `script-physics-bindings`, cut from `main` at `4f3e429`

> **Revised.** The first draft of this spec was wrong on three material
> points, found while planning against it. It claimed neither physics verb
> was reachable from Python — but `raycast()` is already bound and already
> wired to the real world. It proposed fetching `PhysicsSystem` directly
> from a new `physics_globals.cpp`, which would violate the deliberate rule
> that the script backend never depends on the physics system. And it chose
> a dict shape for vector arguments on the strength of how *components*
> marshal, without noticing that the existing physics binding takes
> 3-sequences. What follows is the corrected design. The superseded claims
> are listed under **What the first draft got wrong** so the correction is
> legible rather than silently rewritten.

## Intent

Let a scene's own Python script **push a body**, so gameplay verbs can be
written in the scene rather than compiled into C++.

Scripts can already read and write components, and can already `raycast()`.
What they cannot do is impart momentum: the `apply_impulse` primitive
shipped in #204 is reachable only from C++. "Roll this ball when the
player does X" therefore still requires a rebuild.

**Success criteria**

- A `ScriptComponent` can push a body and see it move.
- The binding reads like `raycast()`, its nearest neighbour — same
  argument style, same failure behaviour, same host indirection.
- It appears in the generated scripting reference **because it is bound**,
  not because someone remembered to add it to a list.

## What the first draft got wrong

Recorded because each error changed the design, and because the third was
a question put to the user on a false premise.

1. **`raycast()` already exists.** It is bound in
   `python_script_engine.cpp`'s `bind_host`, wired to the real world by
   `setup_runtime` (`host.raycast = make_physics_raycast(world)`), and
   covered by `tests/unit/script/script_raycast_test.cpp`. Scope therefore
   narrows to `apply_impulse` alone.
2. **A `physics_globals.cpp` reaching into `PhysicsSystem` would break an
   explicit rule.** `ScriptHost::raycast` carries the comment: *"Kept
   neutral (Vec3 in / ScriptRayHit out) so the script backend never depends
   on the physics system."* The indirection is deliberate. The binding must
   go through `ScriptHost`, not around it.
3. **Vector arguments to host bindings are 3-sequences, not dicts.** Both
   conventions genuinely coexist: reflected component values marshal as
   dicts (`tr['position']['x']`), while host callbacks take sequences and
   return dicts — `raycast([0,5,0], [0,-1,0], 10.0)` yields
   `{'hit':…, 'point':{'x':…}}`. The user's earlier "dict only" answer was
   given before this was known and is superseded: `apply_impulse` follows
   `raycast`.

## Decisions

1. **The binding goes through `ScriptHost`.** A new optional callback
   alongside `raycast`, keeping the script backend physics-agnostic. The
   rejected alternative — a `physics_globals.cpp` fetching `PhysicsSystem`
   from the world — is what the existing comment forbids, and it would
   make the Python module depend on a subsystem it currently does not
   know about.
2. **Vector arguments are 3-element sequences**, matching `raycast`. A
   dict form was considered and rejected: it would make the two physics
   bindings disagree with each other, which is worse than either
   convention alone.
3. **An unset callback is a silent no-op**, exactly as `ScriptHost`
   documents for every hook ("an unset (empty) function is a silent no-op
   (or an always-miss for raycast)"). `apply_impulse` returns `False`.
4. **The docs generator switches to iterating the installed globals.** Not
   a nicety: see below.

## The binding surface

```python
apply_impulse(entity, impulse, point) -> bool
```

`entity` is an int id. `impulse` and `point` are 3-element sequences in
world space. `point` is where the impulse is applied — off the body's
centre induces torque, which is what makes a sphere roll rather than
slide.

Returns `True` when the impulse reached a body; `False` when the host
callback is unset, there is no `PhysicsSystem`, the entity has no
simulated body, or a sequence argument is malformed.

```python
apply_impulse(ball, [2.0, 0.0, 0.0], [0.0, 0.25, 0.0])
```

**A malformed argument returns `False` rather than raising.** A sequence
shorter than three elements, or holding a non-number, is a failed call.
This keeps a buggy script from killing the tick, matching how the rest of
the scripting surface degrades.

## The documentation hole this lands in

The generated scripting reference is built from a **hardcoded array of
global names** in `app/docs/scripting_globals_reference.cpp`, and
`tests/unit/app/scripting_globals_reference_test.cpp` asserts against a
**duplicate of that array**. Nothing checks that an installed global is
documented, and the test named *"documents every world global under its
section"* in fact verifies only that every global *in its own array* is
documented.

This is not hypothetical. **Four bindings are already invisible:**
`raycast`, `play`, `pause` and `stop` are all installed by `bind_host` and
none appear in the reference. A fifth would join them.

So the generator and its test switch to **iterating the installed globals
dict**, partitioning on the `imgui_` prefix — `imgui_*` to the Studio
Panel section, everything else to World & Entity. Three name lists
collapse to zero, the four existing orphans become documented as a side
effect, and the test's claim becomes true: it fails if a binding is added
without reaching the reference.

This is a targeted fix to the code this work depends on. Without it, this
slice's own binding ships undocumented alongside the other four.

## Implementation shape

- **`engine/script/script_host.hpp`** — `ScriptHost` gains:
  ```cpp
  // Push a dynamic body. Unset => no-op returning false. Kept neutral (ids and
  // Vec3 in, bool out) so the script backend never depends on the physics system.
  std::function<bool(std::uint64_t entity,
                     mosaic::foundation::math::Vec3 impulse,
                     mosaic::foundation::math::Vec3 point)> apply_impulse;
  ```
- **`modules/script_python/python_script_engine.cpp`** — `bind_host`
  installs `apply_impulse` beside `raycast`, taking `py::sequence` and
  guarding the element reads so a malformed sequence yields `False`.
  `raycast`'s existing reads are unguarded; this binding does not copy
  that, and the gap is recorded as deferred rather than fixed here.
- **`app/run/demo_scene.hpp`** — a `make_physics_apply_impulse(World&)`
  helper mirroring `make_physics_raycast` exactly: look the system up
  lazily at call time so registration order does not matter, return
  `false` when absent. `setup_runtime` wires it with the same
  `if (!host.apply_impulse)` guard used for `raycast`.
- **`app/docs/scripting_globals_reference.cpp`** — install the host
  globals too, and iterate.

## Testing

1. A script calling `apply_impulse` moves a real body — assert the
   entity's `Transform` changed after ticking. Mirrors
   `script_raycast_test.cpp`'s structure.
2. The C++ callback receives the marshalled arguments — entity id,
   impulse, point — verified by capturing them in a stub host, as the
   raycast test does.
3. With **no host callback set**, `apply_impulse` returns `False` and the
   script still runs. `script_raycast_test.cpp` has the equivalent
   always-miss case.
4. `make_physics_apply_impulse` returns `false` in a world with **no
   `PhysicsSystem`** — a live path, since scripts also run under the MCP
   server and asset importers. `script_runtime_test.cpp` already tests the
   raycast analogue this way.
5. A malformed sequence (two elements; a non-number) returns `False`
   without raising or aborting the tick.
6. An off-centre impulse from a script makes a sphere **spin**, not just
   translate — the same rolling-versus-sliding distinction #204 turned on,
   now verified through the script path.

Documentation tests:

7. `apply_impulse` **and the four previously-undocumented host globals**
   appear in the generated reference — passing because they are installed.
8. The reference test fails when an installed non-`imgui_` global is
   missing from the World section. Verify by temporarily installing a
   dummy global and watching it fail; a test that cannot fail is the thing
   this whole section exists to prevent.

## Risks

**The failure convention is silent by design.** Every error path returns
`False`, so a script with a malformed argument does nothing and says
nothing. Correct for a game loop — a raising binding kills the tick — but
it means no feedback. Mitigation here is the `py::doc` string stating
exactly what makes the call fail; log-surfacing is deferred.

**Iterating the globals dict changes the reference's ordering**, and will
add five entries at once. The generated file churns more than the code
diff suggests; that is expected, not a content change to scrutinise
line-by-line.

## Non-goals

Recorded in `docs/project/deferred-work-backlog.md`:

- **Other physics verbs from scripts** — set velocity, teleport,
  overlap/shape queries.
- **Angular impulse from scripts** — follows the C++ angular API, itself
  deferred.
- **Raycast layer masks / all-hits** — starts with a backend change.
- **A `Vec3` Python class** — would add a third vector representation to a
  surface that already has two.
- **Surfacing binding failures to the log** — see Risks.
- **Hardening `raycast`'s unguarded sequence indexing** — it reads
  `origin[0..2]` with no length check, so a short sequence raises out of
  pybind11 rather than returning a miss. Pre-existing, out of scope here,
  and fixing it silently would change behaviour scripts may rely on.
