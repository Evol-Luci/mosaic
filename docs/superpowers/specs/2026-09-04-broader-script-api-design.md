# Broader Scripting API — component authoring & queries (Design)

## Problem

Scripts can read/write existing components (`get_component`/`set_component`) and
create/destroy entities, but there is **no way to add a component to an entity
from a script** — so `create_entity()` yields a bare entity a script can't
furnish. Nor can a script ask *which* entities have a component, or list an
entity's components. This blocks real gameplay/tooling scripting (spawn a thing
and give it a Transform/RigidBody; find all enemies; introspect an entity).

## Goal

Five new inline-mode script globals, built entirely on the reflection + `World`
the engine already binds (no new dependencies, no new engine code):

| Global | Signature | Backed by |
|--------|-----------|-----------|
| `add_component(eid, name)` | → `bool` (False if already present) | `ComponentReflection::add_default` |
| `remove_component(eid, name)` | → `bool` (False if absent) | `ComponentReflection::remove_value` |
| `has_component(eid, name)` | → `bool` | membership in `components_on` |
| `component_names(eid)` | → `list[str]` | `ComponentReflection::components_on` |
| `entities_with(name)` | → `list[int]` | `World::for_each_entity` + `components_on` |

A script can now: `e = create_entity(); add_component(e, 'Transform'); set_component(e, 'Transform', {...})`.

## Scope / non-goals

- These live in **`bind_world`** (inline mode). The threaded worker's
  `bind_context` sandbox stays get/set-only (adding/removing components would
  race `World`) — consistent with the existing threaded-mode limitation.
- **Deferred to the next Phase-3 slice:** `raycast()` and event emit/subscribe —
  they need `World::get_system<PhysicsSystem>()` / the event bus, i.e. a
  scripting↔physics/events coupling decision, so they're kept out of this
  reflection-only slice.

## Implementation

All five are added in `PythonScriptEngine::bind_world`
(`modules/script_python/python_script_engine.cpp`), same
`globals_["name"] = py::cpp_function(...)` pattern as the existing globals,
using the already-stored `world_` + `types_`. `add_default`/`remove_value`/
`components_on` are `const` methods on `ComponentReflection`, callable through
the stored `const ComponentReflection* types_` (they mutate the non-const
`*world_`). No header/CMake changes.

## Testing (headless)

`tests/unit/script/script_authoring_test.cpp` (new): bind a `World` + builtin
reflection; a script asserts the full lifecycle —
`not has_component` → `add_component` True → second `add_component` False
(already present) → `has_component` True → `'Transform' in component_names(e)`
→ `e in entities_with('Transform')` → `remove_component` True → `not
has_component`. Cross-checked from C++: `world.has_component<Transform>(e)` is
true after the script adds it and false after it removes it.

Fully headless, single new test file + the one backend file.
