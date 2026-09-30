# Scripting — Physics Bindings (design)

**Date:** 2026-09-29
**Status:** approved (design), pending implementation plan
**Branch:** `script-physics-bindings`, cut from `main` at `4f3e429`

## Intent

Make physics reachable from a scene's own Python scripts, so gameplay can
be written in the scene rather than only in C++.

Scripts today can read and write components — the demo's orbiter moves by
assigning `Transform.position` every tick — but they cannot push a body or
ask what lies along a ray. "Roll this ball when the player does X" is not
expressible in a scene, which means every gameplay verb has to be compiled
in. The `apply_impulse` primitive shipped in #204 and `PhysicsSystem::raycast`
has existed for longer; neither is reachable from Python.

**Success criteria**

- A `ScriptComponent` can push a body and see it move.
- A script can raycast the physics world and identify what it hit.
- The two new globals read like the ones already there — same entity
  representation, same vector representation, same failure convention.
- Both appear in the generated scripting reference **automatically**, not
  because someone remembered to add them to a list.

## Decisions

Most of the binding surface was settled by existing convention rather than
by choice. Recorded here so the next subsystem's bindings match:

1. **Entities are plain `int` ids.** Every existing global already names
   them this way (`get_component(entity, 'Transform')`).
2. **`Vec3` is a dict with `x`/`y`/`z` keys** — `{'x':1,'y':0,'z':0}` —
   confirmed with the user. It is exactly what `get_component` returns and
   `set_component` accepts, so a script can pass a component's vector
   straight through with no conversion. Two alternatives were rejected:
   *also accepting a 3-sequence*, which is nicer for hand-written literals
   but puts two accepted shapes in an API whose existing half accepts one;
   and *3-sequence only*, which would force manual unpacking for the single
   most likely use — feeding a `Transform`'s position into a raycast.
3. **Failure returns a falsy value; it does not raise.** `set_component`
   returns `False`, `destroy_entity` returns `False`, `get_component`
   returns `None` when the component is absent. So `apply_impulse` returns
   `False` and a raycast miss returns `None`. This follows the established
   convention rather than inventing a policy.
4. **The bindings live in their own file**, `physics_globals.{hpp,cpp}`,
   installed alongside the world globals. `world_globals.cpp` is 142
   focused lines about entities, components and undo; physics is a
   different subsystem needing a different dependency (`PhysicsSystem`,
   fetched from the world at call time). Extending `world_globals.cpp` was
   rejected: it conflates surfaces and makes the file's name inaccurate,
   and a third subsystem will eventually want bindings too. A generic
   "call any system method" escape hatch was rejected outright — it would
   expose unreviewed C++ surface to scripts and could not be walked back
   once scenes depended on it.

## The binding surface

```python
apply_impulse(entity, impulse, point) -> bool
```

Pushes a dynamic body. `entity` is an int id; `impulse` and `point` are
`{'x':…,'y':…,'z':…}` dicts. `point` is the world-space point the impulse
is applied at — off the body's centre induces torque, which is what makes
a sphere roll rather than slide.

Returns `True` when the impulse was delivered to a body, `False` when
there is no `PhysicsSystem`, the entity id is not live, the entity has no
simulated body, or either vector argument is malformed.

```python
raycast(origin, direction, max_distance) -> dict | None
```

Closest-hit raycast. Returns `None` on a miss (including when there is no
`PhysicsSystem`), or on a hit:

```python
{'entity': int, 'point': {'x':…,'y':…,'z':…},
 'normal': {'x':…,'y':…,'z':…}, 'distance': float}
```

No `'hit'` key: `None` already carries that, and a dict containing
`hit: False` alongside meaningless coordinates invites reading the
coordinates. A script tests `if hit is not None:` or simply `if hit:`.

**Malformed vector arguments return the failure value, not an exception.**
A dict missing a key, or holding a non-number, is treated as a failed call
— `False` from `apply_impulse`, `None` from `raycast`. This matches how
`set_component` handles a malformed dict and keeps a buggy script from
killing the tick.

## Documentation, and the drift this slice has to fix first

The generated scripting reference is built from a **hardcoded array of
global names** in `app/docs/scripting_globals_reference.cpp`, and
`tests/unit/app/scripting_globals_reference_test.cpp` asserts against a
**duplicate of that same array**. So the list of names exists in three
places — the install site, the generator, and the test — and nothing
checks that an installed global is documented.

The test's name, "documents every world global under its section", is
therefore not what it verifies: it verifies that every global *in its own
array* is documented. Adding a binding and forgetting the arrays produces
no failure anywhere, and regeneration yields identical output, so the
pre-commit staleness hook cannot catch it either.

Since this slice adds two globals that would land in exactly that hole,
the fix comes with it: **the generator and the test iterate the installed
globals dict**, partitioning on the `imgui_` prefix — `imgui_*` to the
Studio Panel section, everything else to World & Entity. Three lists
collapse to zero, a new global is documented automatically, and the test's
claim becomes true, failing if a binding is ever added without reaching
the reference.

This is a targeted improvement to code this work depends on, not
unrelated refactoring: without it, this slice's own bindings ship
undocumented.

## Implementation shape

- `modules/script_python/physics_globals.hpp/.cpp` — an
  `install_physics_globals(py::dict& globals, mosaic::engine::World** world_ptr)`
  mirroring `install_world_globals`'s pointer-to-pointer convention, so the
  installed lambdas read the world at **call** time rather than install
  time.
- A small internal helper converts a `py::object` to a `Vec3`, returning
  success/failure rather than throwing, plus one building a `py::dict` from
  a `Vec3`. Both are file-local; they are not a public conversion API.
- `python_script_engine.cpp` calls `install_physics_globals` next to its
  existing `install_world_globals` call.
- `app/docs/scripting_globals_reference.cpp` installs the physics globals
  too, and switches to iterating the dict.
- Dependency direction is already permitted: `modules/` may depend on
  `engine/`, and `PhysicsSystem` is `engine/physics`.

## Testing

Python-level tests through the real script engine, matching how existing
script behavior is tested:

1. A script calling `apply_impulse` moves a real body — assert the
   entity's `Transform` changed after ticking.
2. `apply_impulse` returns `False` for an entity with no body, and the
   tick completes normally.
3. A script `raycast` that hits returns a dict whose `entity` equals the
   expected id, with a finite `distance`.
4. A raycast that hits nothing returns `None`.
5. A malformed vector — a dict missing `'z'` — returns the failure value
   and does not raise or abort the tick.
6. A world with **no `PhysicsSystem`** returns `False`/`None` rather than
   crashing. Scripts run in contexts that have no physics (the MCP server,
   asset importers), so this is a live path, not a hypothetical.

Documentation tests:

7. Both new globals appear in the generated reference under World &
   Entity — which, after the generator change, passes *because they are
   installed*, not because they were added to a list.
8. The reference test fails if an installed non-`imgui_` global is missing
   from the World section. This is the regression guard for the drift
   described above, and should be verified by temporarily installing a
   dummy global and watching it fail.

## Risks

**The failure convention is quiet by design.** Every error path returns
`False` or `None`, so a script with a typo'd key silently does nothing.
That is the right call for a game loop — a raising binding would kill the
tick — but it means a script author gets no feedback. Mitigation for this
slice is documentation: each `py::doc` string states exactly what makes
the call fail. Surfacing a warning through the existing log is deliberately
out of scope, and recorded as deferred.

**Iterating the globals dict changes the reference's ordering** from the
arrays' hand-chosen order to the dict's. The generated file will churn
once. That is a one-time diff, and the pre-commit hook regenerates it, but
it should not be mistaken for a content change during review.

## Non-goals

All recorded in `docs/project/deferred-work-backlog.md`:

- **Other physics verbs from scripts** — setting velocity directly,
  teleporting a body, overlap/shape queries. Each needs its own semantics
  decision; impulse and raycast are the two with a caller today.
- **Angular impulse from scripts** — follows the C++ angular API, which is
  itself deferred.
- **Raycast layer masks or all-hits queries** — the backend exposes
  closest-hit only; anything more starts with a backend change.
- **A `Vec3` Python class** instead of dicts — better ergonomics, but it
  would introduce a second vector representation alongside the dicts every
  existing global uses.
- **Surfacing binding failures to the log** — see Risks.
