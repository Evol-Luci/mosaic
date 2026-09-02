# Scripting S2 — reflection → Python property bindings (design)

**Decomposition:** [scripting-decomposition](2026-08-30-scripting-decomposition.md) (slice S2).
**Builds on:** S1 (`ScriptEngine`/pybind11), #18 reflection (`ComponentReflection`), #10
serialization (`Value` DOM / `to_value`/`from_value`).
**Date:** 2026-08-30.

> Built semi-autonomously (user approved the S1 CPython+pybind11 dependency at PR #77).

## Summary

Let a Python script **read and write a reflected component's properties**, generically — no
per-component binding code. A script does `t = get_component(eid, 'Transform')` (a Python dict),
edits it, and `set_component(eid, 'Transform', t)`; C++ sees the mutation. The bridge marshals the
#10 `Value` DOM ↔ Python and drives it through `ComponentReflection`'s reflected `to_value`/
`from_value`, so **every reflected component is scriptable for free** (design/scripting.md:
"bindings are generated from reflection").

## Scope

**In:**
- `ComponentReflection` gains generic per-entity get/set: `component_value` (live component →
  `Value`) and `set_value` (`Value` → update-in-place-or-add).
- The neutral `ScriptEngine` gains `bind_world(World&, ComponentReflection&)` and
  `set_global(name, Value)`.
- The Python backend: `Value`↔Python marshaling + the `get_component`/`set_component` script API +
  `set_global`.

**Deferred:** entity discovery / create / destroy from a script (S3); commands/Play (S3);
`ScriptComponent` (S4); attribute-proxy ergonomics (`entity.Transform.position.x = 5`) — v0.1 uses
dict get/set. Non-`Value` field types beyond the DOM's set are already read-only in reflection and
stay so.

## Architecture

### 1. `ComponentReflection` — generic get/set (`engine/reflection/component_reflection.hpp`)

Registered as typed thunks next to the existing `attachers_`/`savers_` (where `T` is known at
`register_component<T>`):

```cpp
// Live component `name` on entity `e` as a Value (to_value), or nullopt if unregistered/absent.
std::optional<Value> component_value(World& world, EntityID e, std::string_view name) const;

// Deserialize `value` (from_value) and apply to entity `e`'s component `name`: update in place if
// present, else add. Returns false if unregistered, the entity is invalid, or deserialization
// fails. Never throws (from_value errors are caught → false).
bool set_value(World& world, EntityID e, std::string_view name, const Value& value,
               const ReferenceResolver& resolver = default_resolver()) const;
```

`set_value`'s thunk: `T c{}; from_value(value, c, resolver); if (T* p = world.get_component<T>(e))
{ *p = std::move(c); return true; } return world.add_component<T>(e, std::move(c));` — so it works
whether or not the component already exists (side-stepping `add`'s replace semantics).

### 2. Neutral `ScriptEngine` additions (`engine/script/script_engine.hpp`) — still Python-free

```cpp
// Make get_component/set_component available to scripts, bound to this world + component set.
// The engine holds the pointers (non-owning); the caller keeps them alive across run_string calls.
virtual void bind_world(World& world, mosaic::engine::reflection::ComponentReflection& types) = 0;

// Inject a value into the script's global namespace (e.g. an entity handle, a parameter),
// marshaled from the #10 Value DOM. Used to pass entity ids into scripts.
virtual void set_global(const std::string& name, const Value& value) = 0;
```

These reference only engine/foundation types (`World`, `ComponentReflection`, `Value`) — no Python
— so the tier wall holds. (`script_engine.hpp` gains includes for `world.hpp` +
`component_reflection.hpp` + `value.hpp`; still no pybind11.)

### 3. Python backend (`modules/script_python`)

- **Marshaling** (`value_marshal.hpp`, module-internal): `py::object value_to_py(const Value&)`
  and `Value py_to_value(const py::object&)`. Total over the DOM: `monostate↔None`, `bool↔bool`,
  `int64/uint64↔int`, `double↔float`, `string↔str`, `Array↔list`, `Table↔dict`. Python `int`→
  `int64` (or `uint64` if it exceeds int64), Python `float`→`double`. Unsupported Python types
  (e.g. a set) → a captured error (the calling script sees an exception via the normal
  `error_already_set` path).
- **`bind_world`**: stores `World*` + `ComponentReflection*`, then installs two functions into the
  engine's globals via pybind11 (`py::cpp_function`), closing over them:
  - `get_component(eid: int, name: str) -> dict | None` →
    `types_->component_value(*world_, EntityID(uint64(eid)), name)` → `value_to_py` (or `None`).
  - `set_component(eid: int, name: str, value) -> bool` →
    `types_->set_value(*world_, EntityID(uint64(eid)), name, py_to_value(value))`.
- **`set_global`**: `globals_[name] = value_to_py(value)`.
- **EntityID** crosses as its `uint64 .value()` (an opaque handle); reconstructed via the
  `explicit EntityID(std::uint64_t)` constructor.

## Data flow

```
engine->bind_world(world, types);
engine->set_global("eid", Value(std::int64_t(e.value())));   // pass the entity handle in
run_string(
  "t = get_component(eid, 'Transform')\n"   // -> component_value -> Value -> dict
  "t['position']['x'] = 9.0\n"
  "set_component(eid, 'Transform', t)\n");   // -> py_to_value -> Value -> set_value (update in place)
// C++: world.get_component<Transform>(e)->position.x == 9.0f
```

## Error handling

- `get_component` of an unknown/absent component → `None` (not an error).
- `set_component` of an unknown component, invalid entity, or a value `from_value` rejects → returns
  `False` (the thunk catches `SerializationError`). Never throws into Python.
- A marshaling failure (an unsupported Python type in `py_to_value`) surfaces to the script as a
  normal Python exception, captured by S1's `run_string` into `ScriptResult{ ok=false }`.
- `get_component`/`set_component` called before `bind_world` → a clear Python error (the functions
  aren't installed, so `NameError`), captured normally.

## Testability

Headless (`tests/unit/script/`):

- **Marshaling** (`value_marshal_test.cpp`): `value_to_py`→`py_to_value` round-trips each DOM type
  (None/bool/int/float/str/nested table+array); a Python dict/list built in a script marshals to the
  expected `Value`.
- **Property round-trip** (`script_world_test.cpp`): a `World` with a `Transform`; `bind_world` +
  `set_global("eid", …)`; a script reads `Transform`, mutates `position.x`, `set_component`s it back;
  C++ asserts `get_component<Transform>(e)->position.x` changed. Also: get returns the current C++
  values (set from C++, read in the script).
- **Errors:** `get_component(eid, 'Nope')` → `None`; `set_component(eid, 'Nope', {})` → `False`;
  `set_component` with a wrong-shaped dict → `False`, component unchanged; using
  `get_component` before `bind_world` → captured error.
- **`ComponentReflection` unit** (`component_value_test.cpp`): `component_value`/`set_value`
  directly (no Python) — get returns `to_value`; set updates in place; set adds when absent; set of
  an unregistered name → false.

## Ground rules honored

- **Tier wall:** `engine/script` + `engine/reflection` stay Python-free; all pybind11 marshaling is
  in `modules/script_python`. `set_value`/`component_value` are ordinary reflection methods (no
  Python).
- **Reflection-derived (#18):** the bridge is generic — it calls `to_value`/`from_value` through the
  reflected thunks; adding a reflected component makes it scriptable with no new code.
- **Serialization is #10:** values cross as the `Value` DOM; no new marshaling model beyond
  `Value`↔Python. `AssetID`/`Handle` fields already serialize as strings within `Value` and round-trip
  as such.
- No new third-party deps (pybind11 landed in S1). Catch2 names avoid a leading `--`.

## Acceptance

- [ ] `ComponentReflection::component_value`/`set_value` (generic get/set) with a direct unit test.
- [ ] `ScriptEngine::bind_world`/`set_global` on the neutral interface; the Python backend installs
      `get_component`/`set_component` + marshals `set_global`.
- [ ] `Value`↔Python marshaling total over the DOM, unit-tested.
- [ ] A script mutates a `Transform`'s property and C++ observes it; error paths return `None`/
      `False`/captured-error, never throw. Full suite green.

## Depends on / relates

- **Depends on:** S1 (`ScriptEngine`), #18 (`ComponentReflection`), #10 (`Value`/`to_value`/
  `from_value`), the runtime `World`. **Blocks:** S3 (world/commands — reuses marshaling +
  set_global), S4 (`ScriptComponent` — scripts mutate their entity's components through this),
  S5 (Studio console).
- **Relates:** the deferred automation/agent API manipulates the same reflected properties.
