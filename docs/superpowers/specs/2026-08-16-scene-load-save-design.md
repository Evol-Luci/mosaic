# Scene load/save (#19) — design

**Status:** Approved (brainstorming) — pending implementation plan
**Date:** 2026-08-16
**Layer:** Engine (part of the [v0.1 vertical slice](../../project/roadmap.md#the-v01-vertical-slice--the-first-mountain))
**Depends on:** #9 ECS/World, #10 serialization (`Value`, `to_value`/`from_value`, JSON backend, `ReferenceResolver`), #18 reflection (`ComponentReflection` generic construction)
**Related:** [#19](https://github.com/Evol-Luci/mosaic/issues/19), [RFC 0001](../../rfcs/0001-core-data-model.md), [data-model](../../architecture/data-model.md), [next-steps](../../project/next-steps.md)

## Summary

Serialize a `World`'s entity+component graph to JSON and load it back into a fresh
`World`. Field-level projection reuses #10; per-type construction on load is the
#18 reflection bridge (no concrete C++ type at the call site). Intra-scene
`EntityID` references remap to the new handles on load. This is the first
persistence path in Mosaic and the first tangible on-disk artifact.

## Motivation

Everything below Scene exists (entities, components, reflection, serialization),
but nothing composes them into a durable graph. Scene is the join: it walks a live
`World`, projects each component through reflection, and writes a relocatable
document; on load it rebuilds the entities, remaps intra-graph references, and
generically reconstructs each component. It is the gate for the editor (a scene to
open), the runtime (a level to play), and prefabs.

## Scope

**In scope:**
- Serialize a `World`'s live entities + their registered components to a `Value`,
  rendered to a JSON string.
- Load a JSON string back into a `World`: create entities, remap intra-scene
  `EntityID` references, generically construct components via #18.
- Preserve component-less entities (a faithful entity set).
- Fail loudly on an unknown/renamed component type name.

**Explicitly out of scope** (their own future work):
- **Filesystem I/O.** Scene operates on in-memory JSON strings / `Value`s. Reading
  and writing files is a caller / asset-registry (#20) concern.
- **Systems.** Systems are registered in code (data-model: "configuration, not
  scene data"); they are never serialized.
- **Asset *resolution*.** `AssetID`/`Handle<T>` fields round-trip as ids now
  (#10 already encodes them as strings); resolving those ids to live resources is
  the asset registry (#20). Scene passes them through unchanged.
- **Scene-as-asset metadata** (UUID/version/dependencies envelope). #19 persists the
  entity graph; wrapping it as a first-class `Asset` comes with the asset registry.

## Design decisions

Settled during brainstorming:

1. **Faithful entity set — add entity enumeration.** A `World` cannot enumerate its
   live entities today, and an entity can exist with zero components. Rather than
   derive the entity set from component storages (which drops empties), add
   `EntityRegistry::for_each` / `World::for_each_entity` and preserve component-less
   entities. The enumeration is reusable (the editor scene hierarchy needs it too).
2. **In-memory only.** Scene save/load take/return a JSON string (and a `Value` at
   the core), never a file path. Keeps `engine/scene` free of filesystem coupling
   and trivially testable; disk I/O is the caller's job.
3. **Unknown type → fail loudly.** A scene naming an unregistered component type
   aborts the load with a `SerializationError` naming the type — no silently-partial
   entities. Matches the issue's "fails cleanly, no crash" acceptance.
4. **Save-direction reflection is symmetric.** `ComponentReflection` already holds a
   load (attach) thunk per type; add a save thunk in the same `register_component<T>`
   call, so there is one registration point and both directions share the type name
   and `TypeID`.

## Architecture

### Boundaries

`engine/scene` depends on `engine/world`, `engine/reflection`, and
`foundation/serialization` (`Value`, `to_json`/`from_json`, `ReferenceResolver`).
No upward dependency; the tier contract in
[ARCHITECTURE.md](../../architecture/ARCHITECTURE.md) holds.

### Files

| File | Change |
|------|--------|
| `engine/ecs/entity_registry.hpp` | Add `template<class Fn> void for_each(Fn&&) const` — yield each live `EntityID` in ascending slot order. |
| `engine/world/world.hpp` | Add `template<class Fn> void for_each_entity(Fn&&) const` — delegate to the registry. |
| `engine/reflection/component_reflection.hpp` | Add a per-type save thunk in `register_component<T>`; add `void save(World&, const SaveSink&) const` and `bool has(std::string_view) const`. |
| `engine/scene/scene.hpp` | New. `to_value`/`from_value`/`save_to_json`/`load_from_json` + internal `SceneReferenceResolver`. Header-only, namespace `mosaic::engine::scene`. |
| `tests/unit/scene/scene_test.cpp` | New. `[scene]` round-trip / remap / error tests. |
| `tests/CMakeLists.txt` | Register the new test source. |

No `CMakeLists.txt` library changes: `engine/scene` is header-only; the JSON
backend it calls (`to_json`/`from_json`) is in `mosaic_serialization`, which the
tests already link.

### Entity enumeration

```cpp
// EntityRegistry — ascending slot order; skips retired and free-listed slots.
template <class Fn>
void for_each(Fn&& fn) const;   // fn(EntityID)
```

Implementation: build a `std::vector<bool>` (or set) marking `free_list_` slots,
then for each slot index `i` with `generations_[i] != kRetiredGeneration` and not
free, invoke `fn(EntityID{i, generations_[i]})`. `World::for_each_entity` forwards.

### Save-direction reflection

```cpp
// ComponentReflection additions
using SaveSink = std::function<void(EntityID, const std::string& type_name, const Value&)>;

// For every registered type, emit (entity, name, to_value(component)) for each
// entity holding it.
void save(World& world, const SaveSink& sink) const;

// True if `name` is a registered component type (precise unknown-type errors).
bool has(std::string_view name) const;
```

The save thunk captured at `register_component<T>` time:
`[](World& w, const std::string& name, const SaveSink& sink){ w.for_each<T>([&](EntityID e, T& c){ sink(e, name, to_value(c)); }); }`.

### Scene document

A `Value` (Table), rendered to JSON:

```json
{
  "version": 1,
  "entities": [
    { "id": "42", "components": { "Position": {"x":1.0,"y":2.0}, "Link": {"target":"7"} } },
    { "id": "7",  "components": {} }
  ]
}
```

- `id` is the entity's raw 64-bit handle as a string — identical to how
  `serialization::encode(EntityID)` renders an entity field, so the same string form
  is used for both the entity key and any reference to it.
- `components` is a Table keyed by the registered type name; its value is the
  component's `to_value` projection.
- Component-less entities appear with an empty `components` table.

### Scene API

```cpp
namespace mosaic::engine::scene {

using Value = mosaic::foundation::serialization::Value;

// Serialize every live entity and its registered components.
Value to_value(World& world, const reflection::ComponentReflection& types);

// Rebuild entities+components into `world` from a scene Value. `world` is expected
// empty for a faithful round-trip. Throws SerializationError on a malformed
// document or an unknown component type name.
void from_value(const Value& scene, World& world,
                const reflection::ComponentReflection& types);

// JSON string conveniences over to_value/from_value (via the #10 JSON backend).
std::string save_to_json(World& world, const reflection::ComponentReflection& types);
void load_from_json(const std::string& json, World& world,
                    const reflection::ComponentReflection& types);

} // namespace
```

### Save algorithm

1. Collect components: `std::unordered_map<std::uint64_t, Table> comps;`
   `types.save(world, [&](EntityID e, const std::string& name, const Value& v){ comps[e.value()][name] = v; });`
2. Assemble entities in enumeration order:
   `Array entities; world.for_each_entity([&](EntityID e){ Table node; node["id"] = Value(std::to_string(e.value())); node["components"] = Value(comps[e.value()]/*or empty*/); entities.push_back(Value(std::move(node))); });`
3. `Table root; root["version"] = Value(std::int64_t{1}); root["entities"] = Value(std::move(entities)); return Value(std::move(root));`

### Load algorithm (two-pass)

1. **Validate:** `scene` is a Table; `entities` is an Array. (Optionally check
   `version == 1`; a mismatched version throws.)
2. **Pass 1 — create + remap:** for each entity node, read `id` (a string) →
   `std::uint64_t raw = stoull(id)`. `EntityID new_e = world.create_entity();`
   `remap[raw] = new_e;`. Malformed `id` → `SerializationError`.
3. Build `SceneReferenceResolver{remap}` (subclass of `ReferenceResolver`):
   `resolve_entity(EntityID serialized)` returns `remap[serialized.value()]` if
   present, else `EntityID(0)` (null — a reference outside the scene is a broken
   link, not a crash). `resolve_asset` is left as the base identity (ids pass
   through until #20).
4. **Pass 2 — construct:** for each entity node, `new_e = remap[raw]`; for each
   `(type_name, field_value)` in its `components` Table: if
   `!types.has(type_name)` → `throw SerializationError("Scene: unknown component type '" + type_name + "'")`;
   else `types.construct(world, new_e, type_name, field_value, resolver)`. The
   resolver remaps `EntityID` fields during `from_value`.

## Error handling

- **Malformed JSON** → `SerializationError` from `from_json` (includes the existing
  depth-cap guard).
- **Wrong document shape** (root not a Table, `entities` not an Array, entity node
  not a Table, non-string `id`, unparseable `id`) → `SerializationError` with a
  specific message.
- **Unknown/renamed component type** → `SerializationError` naming the type (load
  aborts; the `World` may hold entities created in pass 1 — callers load into a
  fresh `World`, so a failed load is discarded).
- **Malformed component field data** → the existing `SerializationError` from
  `from_value` propagates.
- **Reference to an entity outside the scene** → resolves to null `EntityID(0)`
  (not an error).

## Testing (TDD, `tests/unit/scene/`, tag `[scene]`)

Sample reflected components in the test TU (e.g. `Position{float x,y}`,
`Link{EntityID target}`), registered on a `ComponentReflection`.

- **Populated round-trip:** a `World` with several entities and mixed components →
  `to_value` → fresh `World` `from_value` → entity count and every component's
  fields match (graph-equivalent).
- **Empty entity preserved:** an entity with no components survives the round-trip
  (present and valid, still component-less).
- **Intra-scene reference remap:** entity A has `Link{target = B}`; after load,
  A's `Link.target` equals the *new* handle for B (not B's old raw value), and is
  valid in the loaded world.
- **Broken reference → null:** a `Link` whose target id is absent from the scene
  resolves to `EntityID(0)` after load.
- **Unknown component type throws:** a scene JSON naming a type not registered on
  the `ComponentReflection` → `from_value`/`load_from_json` throws
  `SerializationError`.
- **Malformed JSON throws:** `load_from_json("{ not json")` throws
  `SerializationError`.
- **JSON convenience path:** `save_to_json` → `load_from_json` reproduces the graph
  (exercises the format wrappers end to end).

Full suite stays green (the new `for_each`/`for_each_entity`/`save`/`has` additions
must not regress `[world]`, `[reflection]`, or serialization tests).

## Open questions / follow-ups

- **Deterministic ordering:** entities serialize in ascending slot order and a
  Table preserves insertion order, so output is stable for a given construction
  sequence. A canonical/sorted form (for diff-friendly VCS scenes) can come later.
- **Scene-as-asset envelope** (UUID/version/dependencies) — arrives with the asset
  registry (#20); `version: 1` is a minimal forward-compat seam.
- **Partial/streaming load** and large-scene performance — out of scope; v0.1
  scenes are small.
</content>
