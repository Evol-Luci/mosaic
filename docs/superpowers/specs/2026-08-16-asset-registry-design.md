# Asset registry (#20) — design

**Status:** Approved (brainstorming) — pending implementation plan
**Date:** 2026-08-16
**Layer:** Engine (part of the [v0.1 vertical slice](../../project/roadmap.md#the-v01-vertical-slice--the-first-mountain))
**Depends on:** #10 serialization (`AssetID`, `Handle<T>`, `ReferenceResolver`, `to_value`/`from_value`, TOML backend)
**Related:** [#20](https://github.com/Evol-Luci/mosaic/issues/20), [asset-pipeline](../../design/asset-pipeline.md), [data-model](../../architecture/data-model.md), [next-steps](../../project/next-steps.md)

## Summary

The runtime-side asset registry: resolve `AssetID`/`Handle<T>` references to loaded
resources with caching, wire into the #10 `ReferenceResolver` deserialize hook, and
round-trip asset metadata through TOML. Because no concrete resource types exist yet
(rendering is 0%), the registry is generic over resource type via `shared_ptr<void>`
+ a templated `load<T>`. Runtime portion only — the Blender importer, cooker, and
incremental recook are future issues.

## Motivation

Scenes (#19) round-trip `AssetID`/`Handle<T>` fields as ids but have nothing to
resolve them against — an asset reference is currently just an opaque id. The asset
registry is the runtime table that gives those ids meaning: it caches loaded
resources, validates references on deserialize, and carries the normative asset
metadata. It is the last reference type the data model leaves unresolved, and the
substrate the importer/cooker will later populate.

## Scope

**In scope:**
- `AssetMetadata` (normative fields), reflected, round-tripping through TOML via #10.
- `AssetRegistry`: register an asset's metadata + a lazy loader (or an eager
  resource); resolve + cache its loaded resource by `AssetID`; look up metadata.
- `AssetReferenceResolver`: a `ReferenceResolver` that validates `AssetID`/`Handle`
  references against the registry on deserialize.
- `std::hash<AssetID>` (a small supporting addition; `AssetID` has none today).

**Explicitly out of scope** (future work):
- **Any concrete loader / resource type.** There are no meshes/textures until
  rendering; loaders are supplied by callers/tests. The registry only caches
  whatever `shared_ptr<void>` a loader yields.
- **Blender importer, cooker, dependency-graph recook** (the source→IR→runtime
  pipeline in [asset-pipeline](../../design/asset-pipeline.md)).
- **Filesystem I/O.** In-memory only, mirroring Scene (#19): metadata is exchanged
  as TOML *strings* / `AssetMetadata` objects, not files. A future asset server
  owns disk layout.
- **Wiring the resolver into Scene's load path.** Scene (#19) already ships with its
  own entity-remap resolver; composing entity remap + asset validation into one
  resolver is a small follow-up and is not needed for #20's acceptance (which tests
  the resolver via `from_value` directly).

## Design decisions

Settled during brainstorming:

1. **Resource representation: `shared_ptr<void>` + templated `load<T>`.** The cache
   stores type-erased `std::shared_ptr<void>`; `load<T>(id)` returns
   `std::shared_ptr<T>` via `static_pointer_cast`. No `Resource` base class is
   imposed on future resource types — YAGNI, maximally flexible, and shared
   ownership matches "Resource = the loaded instance."
2. **Missing asset → null / nil, non-throwing.** `load<T>(unknown)` returns
   `nullptr`; `resolve_asset(unknown)` returns a nil `AssetID`. A broken asset
   reference degrades to a nil handle rather than crashing or aborting a load.
   Exceptions stay reserved for malformed metadata (propagated from the #10 bridge).
3. **Lazy loaders, cached once.** `register_asset(metadata, loader)` stores a loader
   invoked at most once — on the first `load` — with its result cached; later
   `load`s return the cached instance (no reload). `register_resource` is an eager
   convenience that wraps an already-built resource.
4. **`AssetID` gets a `std::hash`.** `AssetID` has `operator==` but no hash today
   (only `EntityID` does). The registry keys an `unordered_map` on `AssetID`, so add
   a `std::hash<AssetID>` specialization to `foundation/serialization/reference.hpp`,
   mirroring `EntityID`. Small, correct, and the natural cross-reference key.
5. **In-memory only.** No filesystem, consistent with Scene.

## Architecture

### Boundaries

`engine/assets` depends only on `foundation/serialization` (`AssetID`, `Handle`,
`ReferenceResolver`, `Value`, `to_value`/`from_value`) and the standard library. No
upward dependency; the tier contract holds. The `std::hash<AssetID>` addition lives
in foundation, next to `AssetID` itself.

### Files

| File | Change |
|------|--------|
| `foundation/serialization/reference.hpp` | Add `std::hash<AssetID>` specialization (hash of the 16 id bytes). |
| `engine/assets/asset_metadata.hpp` | New. `AssetMetadata` struct + its `reflect` overload. Namespace `mosaic::engine::assets`. |
| `engine/assets/asset_registry.hpp` | New. `AssetRegistry` + `AssetReferenceResolver`. Header-only. |
| `tests/unit/assets/asset_metadata_test.cpp` | New. Metadata TOML round-trip. |
| `tests/unit/assets/asset_registry_test.cpp` | New. Register/load/cache/resolve tests. |
| `tests/CMakeLists.txt` | Register the two new test sources. |

No `CMakeLists.txt` library changes: `engine/assets` is header-only; the TOML
backend used by the metadata test is in `mosaic_serialization`, already linked by
the tests.

### `AssetMetadata`

```cpp
namespace mosaic::engine::assets {

using AssetID = mosaic::foundation::serialization::AssetID;
using Value   = mosaic::foundation::serialization::Value;

struct AssetMetadata
{
    AssetID uuid;
    std::string type;
    std::uint32_t version = 0;
    std::string source;
    std::vector<AssetID> dependencies;
    Value metadata;          // open-ended (Value pass-through)
    Value import_settings;   // open-ended
};

template <class V> void reflect(V& visitor, AssetMetadata& m)
{
    visitor.member("uuid", m.uuid);
    visitor.member("type", m.type);
    visitor.member("version", m.version);
    visitor.member("source", m.source);
    visitor.member("dependencies", m.dependencies);
    visitor.member("metadata", m.metadata);
    visitor.member("import_settings", m.import_settings);
}

} // namespace
```

Every field is already handled by the #10 `encode`/`decode`: `AssetID` (as string),
`std::string`, unsigned integer, `std::vector<AssetID>` (array), and the open-ended
`Value` pass-through. `to_value(metadata)` yields a Table-valued `Value`, which
`to_toml` accepts (TOML top-level must be a table).

### `AssetRegistry`

```cpp
namespace mosaic::engine::assets {

class AssetRegistry
{
public:
    using Loader = std::function<std::shared_ptr<void>()>;

    // Register metadata + a lazy loader (invoked at most once, on first load).
    // Re-registering the same uuid overwrites its entry.
    void register_asset(AssetMetadata metadata, Loader loader);

    // Eager convenience: register metadata + an already-built resource.
    template <class T>
    void register_resource(AssetMetadata metadata, std::shared_ptr<T> resource);

    bool contains(AssetID id) const;

    // Metadata by id; nullptr if unregistered.
    const AssetMetadata* metadata(AssetID id) const;

    // Resolve + cache the resource. First call runs the loader and caches its
    // result; later calls return the cached instance. Returns nullptr if the id is
    // unregistered or the loader yields null. static_pointer_cast to T.
    template <class T>
    std::shared_ptr<T> load(AssetID id);

private:
    struct Entry
    {
        AssetMetadata metadata;
        Loader loader;
        std::shared_ptr<void> cached;
        bool loaded = false;
    };
    std::unordered_map<AssetID, Entry> entries_;
};

} // namespace
```

- `register_resource<T>(meta, res)` stores a loader that returns
  `std::static_pointer_cast<void>(res)` and marks the entry pre-loaded (or simply a
  loader returning `res`; first `load` caches it). It records the resource so the
  first `load<T>` returns exactly that instance.
- `load<T>` returns `nullptr` for an unknown id; otherwise runs the loader once
  (guarded by `loaded`), caches, and returns `std::static_pointer_cast<T>(cached)`.
  The `T` correctness is the caller's contract (as with any type-erased cache);
  mismatched `T` is undefined, same as a bad `any_cast` — documented, not guarded.

### `AssetReferenceResolver`

```cpp
class AssetReferenceResolver final : public mosaic::foundation::serialization::ReferenceResolver
{
public:
    explicit AssetReferenceResolver(const AssetRegistry& registry) : registry_(registry) {}

    AssetID resolve_asset(AssetID serialized) const override
    {
        return registry_.contains(serialized) ? serialized : AssetID(); // nil if unknown
    }

private:
    const AssetRegistry& registry_;
};
```

On deserialize, an `AssetID`/`Handle<T>` field decodes through
`resolver.resolve_asset(id)`: a registered id passes through (validated); an unknown
id becomes a nil `AssetID`, so the field lands as a nil handle. `resolve_entity` is
inherited as identity (entity remap is Scene's concern). Loading the referenced
resource is a separate, explicit `registry.load<T>(handle.asset_id())` — deserialize
only validates the reference, consistent with how `Handle` serializes as its id.

### Data flow

```
register_asset(meta, loader)  ──▶ entries_[uuid] = { meta, loader, cached=null }
load<T>(uuid)  ──▶ if !loaded: cached = loader(); loaded = true;  return cast<T>(cached)
                   (unknown uuid ──▶ nullptr)

from_value(value, obj, AssetReferenceResolver(registry))
   Handle<T> field ──▶ resolve_asset(id) ──▶ registered ? id : nil
```

## Error handling

- **Unknown `AssetID`:** `load<T>` → `nullptr`; `resolve_asset` → nil `AssetID`;
  `metadata` → `nullptr`; `contains` → `false`. Non-throwing.
- **Loader throws:** propagates to the `load<T>` caller (a loader is caller-supplied;
  its failures are the caller's to handle). The entry stays unloaded so a retry is
  possible.
- **Malformed metadata TOML/Value:** the existing `SerializationError` from
  `from_toml`/`from_value` propagates unchanged.
- **Type mismatch on `load<T>`:** undefined (type-erased cache contract), documented;
  not guarded in v0.1.

## Testing (TDD, `tests/unit/assets/`, tag `[assets]`)

**`asset_metadata_test.cpp`:**
- `AssetMetadata` (populated: uuid, type, version, source, a dependency, and an
  open-ended `metadata`/`import_settings` Value) → `to_value` → `to_toml` →
  `from_toml` → `from_value` reproduces every field (identity round-trip).

**`asset_registry_test.cpp`:**
- Register an asset + retrieve its metadata by `AssetID`.
- `load<T>` returns the resource; a second `load<T>` returns the **same** cached
  instance and the loader is invoked exactly once (assert an invocation counter).
- `register_resource<T>` then `load<T>` returns that exact instance.
- `load<T>`/`metadata`/`contains` on an unregistered id → `nullptr`/`nullptr`/`false`.
- A struct with a `Handle<T>` field round-trips through `from_value` with an
  `AssetReferenceResolver`: a registered id survives; an unregistered id resolves to
  a nil handle.
- `std::hash<AssetID>` lets an `AssetID` key an `unordered_map`/`unordered_set`
  (compile-and-use smoke check, exercised implicitly by the registry).

Full suite stays green (the `std::hash<AssetID>` addition and new headers must not
regress serialization/scene tests).

## Open questions / follow-ups

- **Scene ↔ registry integration:** a combined resolver (entity remap + asset
  validation) so `Scene::from_value` also validates asset references. Small
  follow-up; deferred so #20 stays focused.
- **Reference counting / eviction:** resources are cached for the registry's
  lifetime in v0.1; an LRU/refcount eviction policy comes when memory pressure is
  real.
- **Loader failure caching / retries policy** beyond "stays unloaded" — revisit when
  real loaders (I/O, cooking) exist.
- **Typed safety on `load<T>`:** could store the resource's `TypeID` (reflection #18)
  and check it on `load<T>`; deferred until it earns its keep.
</content>
