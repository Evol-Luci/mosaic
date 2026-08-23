# Studio 37c — Reflection-Driven Inspector (design)

**Tracking issue:** [#37 Studio shell](https://github.com/Evol-Luci/mosaic/issues/37).
**Decomposition:** [studio-decomposition](2026-08-22-studio-decomposition.md) (slice 37c).
**Builds on:** 37a (editor shell) + 37b (hierarchy + selection).
**Date:** 2026-08-23.

## Summary

Make the Inspector panel edit the **selected entity's components** through reflection —
the strategic payoff of #18. Enumerate the components an entity has, render a widget per
field, and write edits **directly into the live component**. The acceptance target is
editing a `Transform`'s fields live. This slice also establishes the **reflected-property
API** (a mutable `FieldVisitor`) that humans (the inspector) and the future
automation/agent API share.

## Scope

**In:** a `FieldVisitor` abstraction + a `reflect()`-bridging adapter (engine tier); a
`ComponentReflection` extension to enumerate an entity's components and run a visitor over
one (`components_on`, `inspect`, plus per-type `present`/`inspect` thunks); a studio
`ImGuiFieldVisitor` + inspector rendering; `run_editor` reflection setup with a varied
demo world.

**Deferred (later / YAGNI):** editing non-`float` scalars (bool/int/string) and handles
(`EntityID`/`AssetID` are read-only here), a euler-angle rotation widget (raw quaternion
floats for now), add/remove component, multi-select editing, and undo/redo.

## The edit mechanism (decided)

The renderer edits fields **directly** through a mutable visitor rather than round-tripping
through the serialization `Value` DOM. The existing `reflect(visitor, obj)` hook already
calls `visitor.member(name, field_lvalue)` for each field (this is how `to_value`/
`from_value` work); a mutable visitor edits those lvalues in place. This is the
reflected-property API the agent tooling will reuse — not a serialization reuse hack — and
it is unit-testable without a GPU by driving the same visitor from a test.

## Architecture

### `FieldVisitor` + `FieldVisitorAdapter` (`engine/reflection/field_visitor.hpp`)

An abstract visitor over a component's fields. No ImGui, no studio dependency — pure
engine/reflection:

```cpp
class FieldVisitor
{
public:
    virtual ~FieldVisitor() = default;
    virtual void field_float(std::string_view name, float& value) = 0;             // editable
    virtual void field_readonly(std::string_view name, const std::string& display) = 0;
    virtual void begin_group(std::string_view name) = 0; // entering a nested reflectable
    virtual void end_group() = 0;
};
```

`FieldVisitorAdapter` is a templated struct holding a `FieldVisitor&`; its
`member(std::string_view, M&)` dispatches on `M` with an `if constexpr` ladder that mirrors
serialization's `encode` (`foundation/serialization/reflect_value.hpp`):

- `float` → `field_float(name, value)` (the one editable leaf in 37c).
- A reflectable `M` (`Vec3`, `Quat`, nested components) → `begin_group(name)`;
  `reflect(*this, value)` (recurse); `end_group()`.
- `mosaic::foundation::EntityID` → `field_readonly(name, std::to_string(value.value()))`.
- `AssetID` → `field_readonly(name, value.to_string())`; `Handle<R>` →
  `field_readonly(name, value.asset_id().to_string())`.
- Any other supported leaf (`bool`, integral, `double`, `std::string`, `std::vector`) →
  `field_readonly(name, <formatted string>)` — displayed, not editable in 37c.

Using `field_readonly` as the catch-all (rather than a `static_assert`) keeps the adapter
total over every field type the serialization layer supports, so a future component with,
say, an `int` field still renders (read-only) instead of failing to compile.

### `ComponentReflection` additions (`engine/reflection/component_reflection.hpp`)

`register_component<T>` additionally captures two thunks keyed by name:

- **present:** `[](World& w, EntityID e) { return w.has_component<T>(e); }`.
- **inspect:** `[](World& w, EntityID e, FieldVisitor& fv) { if (T* c = w.get_component<T>(e)) { FieldVisitorAdapter a{fv}; reflect(a, *c); } }` — runs the visitor over the **live** component (`get_component<T>` returns a mutable `T*`), so a mutating visitor edits it directly.

Two new public methods:

- `std::vector<std::string> components_on(World&, EntityID) const` — the registered
  component type names the entity holds, in sorted (deterministic) order, via the present
  thunks. Reuses the existing sorted `std::map` keying used by `save`.
- `void inspect(World&, EntityID, std::string_view name, FieldVisitor&) const` — invokes
  the inspect thunk for `name` (no-op if `name` is unregistered or the entity lacks it).

These are symmetric with the existing `construct`/`save` directions. `ComponentReflection`
gains a dependency on `field_visitor.hpp` (same tier).

### Studio inspector (`studio/inspector.hpp`, `studio/inspector.cpp`)

`ImGuiFieldVisitor : engine::reflection::FieldVisitor` renders each field with an ImGui
widget (ImGui only in the `.cpp`):

- `field_float(name, v)` → `ImGui::DragFloat(name.c_str(), &v, 0.05f)` — edits the live
  field in place.
- `field_readonly(name, display)` → `ImGui::LabelText(name, "%s", display.c_str())`.
- `begin_group(name)` → `ImGui::TreeNodeEx(name, DefaultOpen)` guarded so children render
  only when open; `end_group()` closes it. (A small open-depth stack handles nesting.)

`StudioApp::draw_inspector` (called from `draw_ui`) becomes: if `selection_` has a
selection, for each `reflection.components_on(world, sel)` name draw an
`ImGui::CollapsingHeader(name)` and, when open, `reflection.inspect(world, sel, name,
imgui_visitor)`; otherwise show `"(nothing selected)"`.

`StudioApp::draw_ui` changes signature to
**`draw_ui(mosaic::engine::World& world, const mosaic::engine::reflection::ComponentReflection& reflection)`** —
the world is now mutable (the inspector edits components); the hierarchy still only reads
it. `build_hierarchy(const World&)` is unaffected (callable on a mutable world).

### `run_editor` (`app/run/editor.cpp`)

Set up reflection like the `run` path does, and vary the demo world so the inspector shows
multiple component types with editable fields:

```cpp
mosaic::foundation::reflection::TypeRegistry registry;
mosaic::engine::reflection::ComponentReflection reflection;
mosaic::engine::reflection::register_builtin_components(registry, reflection);

engine::World world;
// entity 0: Transform + Camera; 1: Transform + Material; 2: Transform + PointLight; 3: Transform
```

The `on_frame` lambda passes both: `studio.draw_ui(world, reflection);`. The world stays
idle (no systems). `registry`/`reflection` are function-scoped so they outlive
`pump.run()`.

## Data flow

```
run_editor: registry + reflection (builtins registered); demo world with varied components
  on_frame → studio.draw_ui(world, reflection)
     Hierarchy: build_hierarchy(world) → click sets selection_          (37b, unchanged)
     Inspector: components_on(world, sel) → per component:
                  CollapsingHeader(name) → reflection.inspect(world, sel, name, imguiVisitor)
                    → FieldVisitorAdapter drives reflect() over the live component
                    → field_float renders DragFloat bound to the live field (edits in place)
```

## Error handling

`inspect` on an unregistered name or an entity missing the component is a no-op (the thunk
lookup / `get_component` guard both fail closed). A stale selection (entity not present) is
impossible in the static demo world, but `components_on` for an absent entity returns an
empty list, so the Inspector simply shows the header-less "nothing" state. No exceptions
cross the frame loop.

## Testing

The reflected-property API is fully unit-testable without a GPU by driving the visitor from
tests; only the ImGui rendering is functional-only.

- **Unit — adapter + `inspect` read** (`[reflection]`): a `RecordingFieldVisitor` (records
  `begin_group`/`end_group`/`field_float`/`field_readonly` calls) driven via
  `ComponentReflection::inspect` over an entity's `Transform`. Assert the structure:
  groups `position`(3 floats x/y/z), `rotation`(4 floats), `scale`(3 floats), and a
  read-only `parent`. Confirms generic enumeration + the group/leaf dispatch.
- **Unit — `inspect` edits the live component** (`[reflection]`): a `WritingFieldVisitor`
  that sets every `field_float` it visits to a fixed value; run it via `inspect` over an
  entity's `Transform`, then read the entity's `Transform` back through
  `world.get_component<Transform>` and assert `position`/`scale`/`rotation` floats changed.
  Proves edits reach the live component — the #18 payoff.
- **Unit — `components_on`** (`[reflection]`): an entity with `Transform` + `Camera`
  yields `{"Camera", "Transform"}` (sorted); an entity with only `Transform` yields
  `{"Transform"}`; an unknown entity yields `{}`.
- **Functional:** `mosaic editor --duration 1` launches with the varied demo world, renders
  the inspector for a selected entity, and exits 0 with silent stderr (the GUI cannot be
  screenshotted in the sandbox; a clean bounded launch is the acceptance for the draw path).

## Non-goals

- Editing bool/int/string scalars or handles (`EntityID`/`AssetID` are read-only here).
- A euler-angle rotation widget (raw quaternion float editing for now).
- Add/remove component, multi-select, drag, undo/redo.
- The viewport (37d) and play/stop + console (37e).
