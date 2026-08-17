# Reflection system (#18) — design

**Status:** Approved (brainstorming) — pending implementation plan
**Date:** 2026-08-16
**Layer:** Foundation + Engine (part of the [v0.1 vertical slice](../../project/roadmap.md#the-v01-vertical-slice--the-first-mountain))
**Depends on:** #9 ECS core (`ComponentTypeID` shim), #10 serialization (reflection bridge), the runtime `World`
**Related:** [#18](https://github.com/Evol-Luci/mosaic/issues/18), [RFC 0001](../../rfcs/0001-core-data-model.md), [reflection](../../design/reflection.md), [next-steps](../../project/next-steps.md)

## Summary

Introduce the **reflection system**: a runtime type registry that keys reflected
types by a stable name and a within-run `TypeID`, enumerates their fields, and —
through a thin engine-layer bridge — constructs a component from a
`(type-name, Value)` pair and attaches it to an entity in a `World` with **no
compile-time knowledge of the concrete C++ type at the call site**. This is the
gap the `World` runtime deliberately left open, and it gates Scene load/save.

## Motivation

The `World` can only add components through `add_component<T>`, which needs `T` at
the call site. Scene load/save must materialize a component from a serialized type
name — it does not have `T`. The ECS core (#9) shipped a minimal `ComponentTypeID`
shim (type identity only, no field reflection); the serialization bridge (#10)
can round-trip *a known* `T` through `to_value`/`from_value` but has no registry
to find a type from a name. Reflection closes both gaps: one registry of reflected
types that names, enumerates, and generically constructs them.

## Scope

**In scope:**
- A foundation-layer **type registry**: register a reflected type by stable name,
  look it up by name or `TypeID`, enumerate its fields (name + type).
- Reconciling/subsuming the #9 `ComponentTypeID` shim so there is one stable
  identity source.
- An engine-layer **generic construction bridge**: build a component from
  `(type-name, Value)` and attach it to an entity in a `World`, no concrete type
  at the call site.

**Explicitly out of scope** (their own future specs):
- **Scene load/save** (#19) — the first consumer of generic construction.
- **Type-erased generic *value* round-trip in foundation** (a `to_value`/`from_value`
  over a `void*` held by the registry). Not needed for #18: the engine bridge uses
  the existing *templated* `from_value` inside a thunk that knows `T`. Deferred
  until a foundation-only consumer (inspector, docs generator) needs it. YAGNI.
- **Macro-based auto-registration.** Registration is explicit
  (`register_component<T>("Name")`). A registration macro is a convenience to add
  once there are many component types, not a v0.1 need.
- **Field mutation by name / reflected accessors** (setting a field through the
  registry). Enumeration only for now; the inspector/automation layers that need
  set-by-path come later.

## Design decisions

Settled during brainstorming:

1. **Tier split: foundation registry + engine bridge.** The issue nominally places
   reflection in `foundation/reflection`, but "attach a constructed component to a
   `World`" is inherently engine-layer, and `foundation/` must not depend on
   `engine/` (the [ARCHITECTURE.md](../../architecture/ARCHITECTURE.md) tier
   contract). So: `foundation/reflection/` owns the registry, field metadata, and
   stable identity (pure, engine-free); `engine/reflection/` owns the World-attach
   bridge (knows both `World` and, per registration, the concrete `T`). The issue's
   "lives in foundation/reflection" is thereby partially superseded — noted on the
   issue rather than silently deviated from.

2. **One identity source; the *name* is durable, the `TypeID` is not.** The stable,
   persisted identity of a type is its **name** (a string) — Scene files, network
   replication, and every on-disk form key on it, consistent with RFC 0001's
   serialization stance. The integer `TypeID` is only a fast within-run handle and
   may be assignment-order-dependent without harming durability. Given that, keep a
   single identity mechanism: move the id generator down into
   `foundation/reflection` as `TypeID` / `type_id<T>()`, and have
   `engine/ecs/component_type_id.hpp` **re-export** it as an alias so every existing
   `ComponentRegistry` call site keeps compiling unchanged. This subsumes the #9
   shim with no second mechanism to drift — best for usability, extensibility, and
   durability.

3. **Explicit registration, no singleton.** A `TypeRegistry` is an owned instance
   passed explicitly (matching the codebase's no-hidden-global style — `World` owns
   its registries explicitly). One `register_component<T>(registry, "Name")` call
   populates both the foundation metadata registry and the engine attach table,
   sharing the `type_id<T>()` and name.

## Architecture

### Boundaries

- `foundation/reflection/` depends only on `foundation/` (the existing
  `reflect.hpp` concept and, for field-type naming, the serialization type
  categories). It never mentions `World`.
- `engine/reflection/` depends on `foundation/reflection/`,
  `foundation/serialization/` (header-only `Value` / `from_value` / `to_value` /
  `ReferenceResolver`), and `engine/world/`. No upward dependency.

The tier contract in [ARCHITECTURE.md](../../architecture/ARCHITECTURE.md) is
preserved: `foundation/` knows nothing above it.

### Files

| File | Change |
|------|--------|
| `foundation/reflection/type_id.hpp` | New. `TypeID` + `type_id<T>()` — stable within-run handle. Namespace `mosaic::foundation::reflection`. |
| `foundation/reflection/type_registry.hpp` | New. `FieldInfo`, `TypeInfo`, `TypeRegistry`, `field_type_name<M>()`. Header-only. |
| `engine/reflection/component_reflection.hpp` | New. `ComponentReflection`: `register_component<T>` + `construct(...)`. Namespace `mosaic::engine::reflection`. Header-only. |
| `engine/ecs/component_type_id.hpp` | Modify. Replace the standalone counter with a re-export of `foundation::reflection::TypeID` / `type_id<T>()`. |
| `tests/unit/reflection/type_registry_test.cpp` | New. Foundation registry unit tests. |
| `tests/unit/reflection/component_reflection_test.cpp` | New. Engine bridge / generic construction / round-trip tests. |
| `tests/CMakeLists.txt` | Modify. Register the two new test sources. |

No `CMakeLists.txt` library changes: `foundation/reflection` and
`engine/reflection` are header-only and reachable via the existing
`${CMAKE_SOURCE_DIR}` include root. `engine` already links `mosaic_foundation`;
the serialization headers it uses are header-only.

### `foundation/reflection` API

```cpp
namespace mosaic::foundation::reflection {

// Stable *within-run* handle. Assignment order is unspecified across runs; the
// durable identity of a type is its registered NAME, not this integer. Subsumes
// the #9 ComponentTypeID shim.
using TypeID = std::uint32_t;
template <class T> TypeID type_id() noexcept;   // static-local: stable per run

struct FieldInfo {
    std::string name;   // reflected member name
    std::string type;   // canonical type label (see field_type_name)
};

struct TypeInfo {
    TypeID id;
    std::string name;                 // stable, persisted identity
    std::vector<FieldInfo> fields;    // in reflect() declaration order
};

// Owned instance, no global. Register once per type; look up by name or id.
class TypeRegistry {
public:
    // Records TypeInfo{ type_id<T>(), name, fields-of-T }. Enumerates fields by
    // running reflect(visitor, T{}) over a default-constructed instance (T must be
    // default-constructible). Idempotent for the same T+name; throws on a name
    // collision with a different type. Returns the stored TypeInfo.
    template <class T> const TypeInfo& register_type(std::string name);

    const TypeInfo* find(std::string_view name) const;   // nullptr if unknown
    const TypeInfo* find(TypeID id) const;               // nullptr if unknown
};

// Canonical, stable field-type label mirroring the serialization encode()
// categories: "bool", "i8".."i64", "u8".."u64", "f32"/"f64", "string",
// "EntityID", "AssetID", "Handle", "array<E>", "object" (nested reflectable),
// "value" (open-ended Value).
template <class M> std::string field_type_name();

} // namespace
```

`field_type_name` gives the "type" half of acceptance criterion 2. It is a
descriptive label, not a re-parseable type expression — enough to drive an
inspector column and documentation, and deliberately not a second serialization
format.

### `engine/reflection` API

```cpp
namespace mosaic::engine::reflection {

// Bridges a foundation TypeRegistry to generic component construction into a
// World. Holds one attach thunk per registered component type, keyed by name.
class ComponentReflection {
public:
    using Value = mosaic::foundation::serialization::Value;
    using ReferenceResolver = mosaic::foundation::serialization::ReferenceResolver;

    // Registers T's metadata into `registry` AND an attach thunk here, sharing
    // type_id<T>() and `name`. The thunk knows T; callers of construct() do not.
    template <class T>
    void register_component(foundation::reflection::TypeRegistry& registry, std::string name);

    // Look up by name, deserialize `value` into a fresh T via the templated
    // from_value, and world.add_component<T>(e, tmp). Returns false if `name` is
    // not a registered component (or the entity is invalid — add_component's own
    // contract). No concrete type at this call site.
    bool construct(World& world, World::EntityID e, std::string_view name,
                   const Value& value,
                   const ReferenceResolver& resolver = /* default */) const;
};

} // namespace
```

### Data flow

```
Scene / caller                     ComponentReflection            World
--------------                     -------------------            -----
(type-name, Value)  ──construct──▶ find thunk by name
                                   T tmp{};
                                   from_value(Value, tmp) ───────▶ add_component<T>(e, tmp)
```

The attach thunk is `bool(World&, EntityID, const Value&, const ReferenceResolver&)`,
captured at `register_component<T>` time where `T` is known:

```cpp
[](World& w, World::EntityID e, const Value& v, const ReferenceResolver& r) {
    T tmp{};
    foundation::serialization::from_value(v, tmp, r);
    return w.add_component<T>(e, std::move(tmp));
}
```

### `ComponentTypeID` reconciliation

`engine/ecs/component_type_id.hpp` collapses to a re-export:

```cpp
#include "foundation/reflection/type_id.hpp"
namespace mosaic::engine::ecs {
    using ComponentTypeID = mosaic::foundation::reflection::TypeID;
    template <typename T> ComponentTypeID component_type_id() noexcept {
        return mosaic::foundation::reflection::type_id<T>();
    }
}
```

`ComponentRegistry` (which indexes `storages_` by `component_type_id<T>()`) is
untouched and now shares its identity with the reflection registry: a type's
storage slot and its `TypeInfo::id` are the same integer within a run.

## Error handling

- **Unknown type name** at `construct` → returns `false` (a caller/Scene concern to
  surface; not an exception — an unknown component in a scene file is recoverable /
  skippable). `find(name)` returns `nullptr`.
- **Name collision** (same name, different `T`) at `register_type` → throws
  `std::logic_error`: a programming error, caught at startup registration.
- **Malformed `Value`** during `from_value` → propagates the existing
  `SerializationError` from the serialization bridge unchanged.
- **Invalid entity** → `construct` returns whatever `add_component` returns
  (`false`), per the registry's existing validity contract.

## Testing (TDD, `tests/unit/reflection/`, tag `[reflection]`)

Sample component types (default-constructible, reflected via a local
`reflect(visitor, value)` overload) defined in the test TUs.

**`type_registry_test.cpp` (foundation):**
- Register a type; `find` by name and by `TypeID` both return the same `TypeInfo`.
- Field enumeration: names and `field_type_name` labels match the declared members,
  in declaration order.
- `find` on an unknown name / id returns `nullptr`.
- Re-registering the same `T`+name is idempotent; a name collision with a different
  type throws.
- `type_id<T>()` is stable within the run and distinct across types; it equals
  `engine::ecs::component_type_id<T>()` (reconciliation).

**`component_reflection_test.cpp` (engine bridge):**
- `register_component<T>` then `construct(world, e, "Name", value)` attaches a `T`
  the World can `get_component<T>` — with no `T` at the construct call site.
- **Round-trip (acceptance 5):** build a `T`, `to_value(t)`, `construct` into a
  World from that `Value`, read the component back, assert field-equal.
- `construct` with an unregistered name returns `false` and attaches nothing.
- `construct` onto an invalid entity handle returns `false`.
- A component with a nested reflected field and a `std::vector` field round-trips
  (exercises the recursive `from_value` path through the thunk).

Full suite must stay green (the shim re-export must not regress any `[world]` /
`[component_registry]` / serialization test).

## Open questions / follow-ups

- **Shared `TypeID` space inflates `ComponentRegistry::storages_`** ([#25](https://github.com/Evol-Luci/mosaic/issues/25),
  post-v0.1). `type_id<T>()` draws from one global counter shared across *all*
  reflected types, but `ComponentRegistry` sizes its `storages_` vector to
  `max_component_id + 1` — so non-component reflected types leave null gaps in that
  vector. Bounded, correctness-safe memory waste accepted for v0.1; a later version
  should give components a dense id space (or key `storages_` by `TypeID`) without
  reintroducing the #9 identity drift.
- **Foundation-only generic value round-trip** (type-erased `to_value`/`from_value`
  held by the registry) — deferred until an inspector/docs consumer needs it
  without the engine bridge. The registration path is structured so it can be added
  to `TypeInfo` later without changing the engine bridge.
- **Set-a-field-by-name** (reflected write accessors) — needed by the inspector and
  automation layers; out of scope here.
- **Cross-run stable `TypeID`** — intentionally not provided; if a subsystem ever
  needs a stable integer (e.g. compact network ids), derive it from a hash of the
  name at that point, keeping the name as source of truth.
- **Default-constructibility requirement** for registered types (field enumeration
  builds a `T{}`) — acceptable for components in v0.1; revisit if a non-default-
  constructible reflected type appears.
</content>
</invoke>
