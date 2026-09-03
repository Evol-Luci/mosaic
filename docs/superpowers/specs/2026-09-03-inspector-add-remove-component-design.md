# Inspector add/remove components (design)

**Phase:** post-v0.1 Editor UX (first of the Editor-UX priority bucket).
**Builds on:** #18 reflection (`ComponentReflection`), #37c Inspector, S3/S4 (`remove_value`).
**Date:** 2026-09-03.

> Built autonomously (user authorized unattended roadmap work + Antigravity). The reflection core is
> headless-testable; the Inspector UI is thin glue for the user's visual acceptance.

## Summary

The Inspector shows and edits a selected entity's components, but it can't **add or remove** them —
you can't attach a `RigidBody` (or a `ScriptComponent`) to an entity, or drop one. This adds a
generic **"+ Add Component"** control (lists every registered component type not already present) and
a per-component **"Remove"** button, driven entirely through reflection — so it works for *every*
registered component with no per-type UI. It generalizes the hardcoded demo-physics toggle and
unblocks "attach a script to any entity" (add a `ScriptComponent` from the Inspector).

## Scope

**In:**
- `ComponentReflection` gains `registered_names()` (all registered component type names, sorted) and
  `add_default(world, e, name)` (attach a **default-constructed** component by name).
- The Inspector: a per-component **Remove** button (uses the existing `remove_value`), and an **Add
  Component** combo listing `registered_names()` minus the components already on the entity; both
  mark the scene dirty.

**Deferred (documented):**
- **Undoable** add/remove (through the #11 `CommandStack`) — the editor doesn't wire a `CommandStack`
  outside the Script Console yet; add/remove are direct for now (like inspector field edits).
- **Add-component search/filter** and grouping — v0.1 is a flat combo of the ~10 builtin types.

## Architecture

### 1. `ComponentReflection` (`engine/reflection/component_reflection.hpp`)

A new **adder** thunk registered in `register_component<T>` (beside `attachers_`/`removers_`), plus
two methods:

```cpp
// register_component<T>: alongside the existing thunks —
adders_[name] = [](World& world, EntityID e) -> bool
{
    if (world.has_component<T>(e)) { return false; }   // never replace
    return world.add_component<T>(e, T{});             // default-constructed
};

// All registered component type names, sorted (for an "Add Component" menu). Enumerates `presence_`
// (a std::map, already sorted).
std::vector<std::string> registered_names() const;

// Attach a default-constructed component `name` to `e`. False if unregistered or already present.
bool add_default(World& world, EntityID e, std::string_view name) const;
```
`removers_`/`remove_value` already exist (S3). `add_default`/`registered_names`/`remove_value` are
`const` (they mutate the `World`, not the reflection) — so the Inspector's `const ComponentReflection&`
can call them.

### 2. Inspector (`draw_inspector` in `studio/studio_app.cpp`)

The existing loop over `components_on(world, entity)` gains, inside each component's
`CollapsingHeader`, a small **Remove** button; a click records the name in a local `to_remove`
(applied after the loop, so we don't mutate mid-iteration / render a stale header). After the loop, a
**"+ Add Component"** `ImGui::BeginCombo` lists `registered_names()` filtered to those **not** in the
entity's current set; selecting one calls `add_default`. Any add/remove sets a local `changed` flag
that is OR'd into the function's return (so the caller marks the scene dirty, exactly like a field
edit).

```
for name in components_on(entity):
    header(name) { inspect(...);  if Button("Remove") -> to_remove = name }
if to_remove: reflection.remove_value(world, entity, to_remove);  changed = true
present = set(components_on(entity))
if BeginCombo("+ Add Component"):
    for name in registered_names() if name not in present:
        if Selectable(name) -> reflection.add_default(world, entity, name); changed = true
return visitor.changed() || changed
```

## Data flow

```
select entity -> Inspector lists its components (each with a Remove button) + an Add-Component combo
Remove(name)  -> reflection.remove_value(world, e, name)  -> component gone next frame; scene dirty
Add(name)     -> reflection.add_default(world, e, name)   -> default component attached; scene dirty
```

## Error handling

- `add_default` of an unregistered name or an already-present component → `false` (no-op); the combo
  only offers absent registered names, so this is a defensive guard.
- `remove_value` of an absent/unregistered name → `false` (existing behavior). Never throws.
- Removing the component whose fields are being rendered is deferred to after the loop, so no
  use-after-render.

## Testability

- **`ComponentReflection` unit** (`tests/unit/reflection/component_value_test.cpp`, extend):
  `registered_names()` returns a sorted list containing the builtins (e.g. `"RigidBody"`,
  `"Transform"`, `"ScriptComponent"`) and is sorted; `add_default(world, e, "RigidBody")` on a bare
  entity → `true` and `world.has_component<RigidBody>(e)`; a second `add_default` → `false` (already
  present); `add_default(..., "Nope")` → `false`; `remove_value` then drops it. All headless (no
  Python, no ImGui).
- The Inspector UI is thin ImGui glue verified by build + full suite + the user's visual acceptance
  (select an entity, add a `RigidBody`, watch it fall on Play; add a `ScriptComponent` and script it).

## Ground rules honored

- **Reflection-derived (#18):** add/remove are generic thunks — every registered component is
  add/removable with no per-type code (the same "one path, humans + agents" surface).
- **Tier wall:** `engine/reflection` stays Python-free; the Inspector is Studio ImGui glue over the
  reflected surface. No new deps. Catch2 names avoid a leading `--`.

## Acceptance

- [ ] `ComponentReflection::registered_names()` (sorted) + `add_default()` (default-attach by name,
      false if present/unregistered), unit-tested.
- [ ] The Inspector adds a per-component Remove button + an Add-Component combo (registered minus
      present); both mark the scene dirty. Build + full suite green; user visually confirms.

## Depends on / relates

- **Depends on:** #18 reflection, S3 `remove_value`, the #37c Inspector. **Unblocks:** attaching a
  `ScriptComponent` to any entity (Scripting bucket); generalizes the demo-physics toggle.
  **Relates:** deferred undoable add/remove via the `CommandStack`.
