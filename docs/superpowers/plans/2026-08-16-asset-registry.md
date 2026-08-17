# Asset Registry Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the runtime asset registry (#20) — resolve/cache `AssetID` → loaded resources generically, validate `AssetID`/`Handle<T>` references on deserialize via the #10 `ReferenceResolver` hook, and round-trip asset metadata through TOML.

**Architecture:** Add a `std::hash<AssetID>` so `AssetID` can key a hash map. Add a reflected `AssetMetadata` struct that round-trips through TOML via #10. Add a header-only `AssetRegistry` that stores `{metadata, loader, cached shared_ptr<void>, loaded}` per `AssetID`, with a templated `load<T>` that invokes the loader once and caches. Add an `AssetReferenceResolver` that validates asset ids against the registry on deserialize (unknown → nil).

**Tech Stack:** C++20, CMake + CMakePresets, Catch2 (via FetchContent).

**Design spec:** [docs/superpowers/specs/2026-08-16-asset-registry-design.md](../specs/2026-08-16-asset-registry-design.md)

## Global Constraints

- **C++20.** `target_compile_features(... cxx_std_20)`; no compiler extensions.
- **Tier/dependency contract:** `foundation/` knows nothing above it; `engine/` depends only on `foundation/` and other `engine/` code. (See [ARCHITECTURE.md](../../architecture/ARCHITECTURE.md).)
- **Namespaces:** asset code is `mosaic::engine::assets`; serialization is `mosaic::foundation::serialization`.
- **Style (match existing files):** 4-space indent, Allman braces, `snake_case` methods/functions, `PascalCase` types, trailing-underscore private members.
- **Resource representation:** type-erased `std::shared_ptr<void>` cache; `load<T>` `static_pointer_cast`s. No `Resource` base class.
- **Missing asset:** non-throwing — `load<T>`→`nullptr`, `resolve_asset`→nil `AssetID`, `metadata`→`nullptr`, `contains`→`false`.
- **In-memory only:** no filesystem; metadata exchanged as TOML strings / `AssetMetadata` objects.
- **Testing:** Catch2, unit tests under `tests/unit/assets/`, tag `[assets]`; register each new `.cpp` in `tests/CMakeLists.txt`. TDD — write the failing test first and watch it fail before implementing.
- **Build/test commands:**
  - Build: `cmake --build --preset debug`
  - Run a subset: `./build/debug/tests/mosaic_unit_tests "[assets]"`
  - Full suite: `ctest --preset debug`
- **Commit** after each task (this branch is `claude/practical-williams-bde237`).

## File Structure

| File | Responsibility |
|------|----------------|
| `foundation/serialization/reference.hpp` (modify) | Add `std::hash<AssetID>` (FNV-1a over the 16 id bytes). |
| `engine/assets/asset_metadata.hpp` (create) | `AssetMetadata` struct + its `reflect` overload. |
| `engine/assets/asset_registry.hpp` (create) | `AssetRegistry` + `AssetReferenceResolver`. Header-only. |
| `tests/unit/assets/asset_id_hash_test.cpp` (create) | Task 1 test. |
| `tests/unit/assets/asset_metadata_test.cpp` (create) | Task 2 test. |
| `tests/unit/assets/asset_registry_test.cpp` (create) | Task 3 test. |
| `tests/unit/assets/asset_resolver_test.cpp` (create) | Task 4 test. |
| `tests/CMakeLists.txt` (modify) | Register the four new test sources. |

No `CMakeLists.txt` library changes: `engine/assets` is header-only; the TOML backend the metadata test uses is in `mosaic_serialization`, already linked by the tests.

---

### Task 1: `std::hash<AssetID>`

**Files:**
- Modify: `foundation/serialization/reference.hpp`
- Test: `tests/unit/assets/asset_id_hash_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `std::hash<mosaic::foundation::serialization::AssetID>` — lets `AssetID` key `std::unordered_map`/`std::unordered_set`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/assets/asset_id_hash_test.cpp`:

```cpp
#include <functional>
#include <unordered_map>

#include <catch2/catch_test_macros.hpp>

#include "foundation/serialization/reference.hpp"

using mosaic::foundation::serialization::AssetID;

TEST_CASE("AssetID is usable as an unordered_map key", "[assets]")
{
    std::unordered_map<AssetID, int> map;
    const AssetID a = AssetID::from_string("00000000-0000-0000-0000-000000000001");
    const AssetID b = AssetID::from_string("00000000-0000-0000-0000-000000000002");
    map[a] = 1;
    map[b] = 2;

    REQUIRE(map.size() == 2);
    REQUIRE(map.at(a) == 1);
    REQUIRE(map.at(b) == 2);
    REQUIRE(std::hash<AssetID>{}(a) == std::hash<AssetID>{}(a));
    REQUIRE(std::hash<AssetID>{}(a) != std::hash<AssetID>{}(b));
}
```

Register in `tests/CMakeLists.txt` — add inside the `add_executable(mosaic_unit_tests ...)` source list, after `unit/scene/scene_load_test.cpp`:

```cmake
  unit/assets/asset_id_hash_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset debug`
Expected: FAIL — compile error, `static assertion failed: ... std::unordered_map ... hash` / `use of deleted function 'std::hash<...AssetID>'`.

- [ ] **Step 3: Write minimal implementation**

In `foundation/serialization/reference.hpp`:

(a) Add `<cstddef>` and `<functional>` to the includes (after `#include <cstdint>`):

```cpp
#include <cstddef>
#include <functional>
```

(b) At the very end of the file (after the closing `} // namespace mosaic::foundation::serialization`), add the specialization:

```cpp
template <>
struct std::hash<mosaic::foundation::serialization::AssetID>
{
    std::size_t operator()(const mosaic::foundation::serialization::AssetID& id) const noexcept
    {
        // FNV-1a over the 16 identity bytes.
        std::size_t h = 1469598103934665603ull;
        for (std::uint8_t byte : id.bytes())
        {
            h ^= byte;
            h *= 1099511628211ull;
        }
        return h;
    }
};
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[assets]"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add foundation/serialization/reference.hpp tests/unit/assets/asset_id_hash_test.cpp tests/CMakeLists.txt
git commit -m "Add std::hash<AssetID> (#20)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: `AssetMetadata` + TOML round-trip

**Files:**
- Create: `engine/assets/asset_metadata.hpp`
- Test: `tests/unit/assets/asset_metadata_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: the #10 reflection bridge (`to_value`/`from_value`), TOML backend (`to_toml`/`from_toml`), `AssetID`, `Value`.
- Produces:
  - `struct mosaic::engine::assets::AssetMetadata { AssetID uuid; std::string type; std::uint32_t version; std::string source; std::vector<AssetID> dependencies; Value metadata; Value import_settings; };`
  - `template<class V> void reflect(V&, AssetMetadata&)`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/assets/asset_metadata_test.cpp`:

```cpp
#include <cstdint>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "engine/assets/asset_metadata.hpp"
#include "foundation/serialization/reflect_value.hpp"
#include "foundation/serialization/toml_backend.hpp"
#include "foundation/serialization/value.hpp"

using mosaic::engine::assets::AssetMetadata;
using mosaic::foundation::serialization::AssetID;
using mosaic::foundation::serialization::from_toml;
using mosaic::foundation::serialization::from_value;
using mosaic::foundation::serialization::Table;
using mosaic::foundation::serialization::to_toml;
using mosaic::foundation::serialization::to_value;
using mosaic::foundation::serialization::Value;

TEST_CASE("AssetMetadata round-trips through TOML", "[assets]")
{
    AssetMetadata meta;
    meta.uuid = AssetID::from_string("00000000-0000-0000-0000-0000000000aa");
    meta.type = "mesh";
    meta.version = 3;
    meta.source = "models/robot.blend";
    meta.dependencies.push_back(AssetID::from_string("00000000-0000-0000-0000-0000000000bb"));

    Table extra;
    extra["author"] = Value(std::string("luci"));
    meta.metadata = Value(std::move(extra));

    Table imp;
    imp["scale"] = Value(std::int64_t{2});
    meta.import_settings = Value(std::move(imp));

    const std::string toml = to_toml(to_value(meta));

    AssetMetadata back;
    from_value(from_toml(toml), back);

    REQUIRE(back.uuid == meta.uuid);
    REQUIRE(back.type == "mesh");
    REQUIRE(back.version == 3u);
    REQUIRE(back.source == "models/robot.blend");
    REQUIRE(back.dependencies.size() == 1);
    REQUIRE(back.dependencies[0] == meta.dependencies[0]);
    REQUIRE(back.metadata == meta.metadata);
    REQUIRE(back.import_settings == meta.import_settings);
}
```

Register in `tests/CMakeLists.txt`, after the Task 1 line:

```cmake
  unit/assets/asset_metadata_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset debug`
Expected: FAIL — compile error, `fatal error: engine/assets/asset_metadata.hpp: No such file or directory`.

- [ ] **Step 3: Write minimal implementation**

Create `engine/assets/asset_metadata.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "foundation/serialization/reference.hpp"
#include "foundation/serialization/value.hpp"

namespace mosaic::engine::assets
{

using AssetID = mosaic::foundation::serialization::AssetID;
using Value = mosaic::foundation::serialization::Value;

// Normative asset metadata (RFC 0001 / data-model). Reflected, so it round-trips
// through the #10 serialization bridge in any backend (TOML for human-readable
// project metadata). `metadata` and `import_settings` are open-ended Value blobs.
struct AssetMetadata
{
    AssetID uuid;
    std::string type;
    std::uint32_t version = 0;
    std::string source;
    std::vector<AssetID> dependencies;
    Value metadata;
    Value import_settings;
};

template <class V>
void reflect(V& visitor, AssetMetadata& m)
{
    visitor.member("uuid", m.uuid);
    visitor.member("type", m.type);
    visitor.member("version", m.version);
    visitor.member("source", m.source);
    visitor.member("dependencies", m.dependencies);
    visitor.member("metadata", m.metadata);
    visitor.member("import_settings", m.import_settings);
}

} // namespace mosaic::engine::assets
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[assets]"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/assets/asset_metadata.hpp tests/unit/assets/asset_metadata_test.cpp tests/CMakeLists.txt
git commit -m "Add AssetMetadata with TOML round-trip (#20)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: `AssetRegistry` — register / metadata / load + cache

**Files:**
- Create: `engine/assets/asset_registry.hpp`
- Test: `tests/unit/assets/asset_registry_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `AssetMetadata` (Task 2), `std::hash<AssetID>` (Task 1), `AssetID`.
- Produces (on `mosaic::engine::assets::AssetRegistry`):
  - `using Loader = std::function<std::shared_ptr<void>()>;`
  - `void register_asset(AssetMetadata metadata, Loader loader);`
  - `template<class T> void register_resource(AssetMetadata metadata, std::shared_ptr<T> resource);`
  - `bool contains(AssetID id) const;`
  - `const AssetMetadata* metadata(AssetID id) const;`
  - `template<class T> std::shared_ptr<T> load(AssetID id);`

- [ ] **Step 1: Write the failing test**

Create `tests/unit/assets/asset_registry_test.cpp`:

```cpp
#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "engine/assets/asset_metadata.hpp"
#include "engine/assets/asset_registry.hpp"
#include "foundation/serialization/reference.hpp"

using mosaic::engine::assets::AssetMetadata;
using mosaic::engine::assets::AssetRegistry;
using mosaic::foundation::serialization::AssetID;

namespace
{
struct DummyResource
{
    int payload;
};
} // namespace

TEST_CASE("register + load returns the resource and caches it", "[assets]")
{
    AssetRegistry registry;
    AssetMetadata meta;
    meta.uuid = AssetID::from_string("00000000-0000-0000-0000-000000000101");
    meta.type = "dummy";

    int loader_calls = 0;
    auto resource = std::make_shared<DummyResource>(DummyResource{42});
    registry.register_asset(meta, [&loader_calls, resource]() -> std::shared_ptr<void>
    {
        ++loader_calls;
        return resource;
    });

    std::shared_ptr<DummyResource> first = registry.load<DummyResource>(meta.uuid);
    REQUIRE(first != nullptr);
    REQUIRE(first->payload == 42);

    std::shared_ptr<DummyResource> second = registry.load<DummyResource>(meta.uuid);
    REQUIRE(second == first);   // same cached instance
    REQUIRE(loader_calls == 1); // loader invoked exactly once
}

TEST_CASE("metadata is retrievable by AssetID", "[assets]")
{
    AssetRegistry registry;
    AssetMetadata meta;
    meta.uuid = AssetID::from_string("00000000-0000-0000-0000-000000000102");
    meta.type = "mesh";
    meta.version = 7;
    registry.register_asset(meta, []() -> std::shared_ptr<void> { return nullptr; });

    const AssetMetadata* got = registry.metadata(meta.uuid);
    REQUIRE(got != nullptr);
    REQUIRE(got->type == "mesh");
    REQUIRE(got->version == 7u);
    REQUIRE(registry.contains(meta.uuid));
}

TEST_CASE("register_resource stores an eager resource", "[assets]")
{
    AssetRegistry registry;
    AssetMetadata meta;
    meta.uuid = AssetID::from_string("00000000-0000-0000-0000-000000000103");
    auto resource = std::make_shared<DummyResource>(DummyResource{9});

    registry.register_resource(meta, resource);
    REQUIRE(registry.load<DummyResource>(meta.uuid) == resource);
}

TEST_CASE("unknown AssetID resolves to null/false", "[assets]")
{
    AssetRegistry registry;
    const AssetID unknown = AssetID::from_string("00000000-0000-0000-0000-0000000001ff");

    REQUIRE(registry.load<DummyResource>(unknown) == nullptr);
    REQUIRE(registry.metadata(unknown) == nullptr);
    REQUIRE_FALSE(registry.contains(unknown));
}
```

Register in `tests/CMakeLists.txt`, after the Task 2 line:

```cmake
  unit/assets/asset_registry_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset debug`
Expected: FAIL — compile error, `fatal error: engine/assets/asset_registry.hpp: No such file or directory`.

- [ ] **Step 3: Write minimal implementation**

Create `engine/assets/asset_registry.hpp`:

```cpp
#pragma once

#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>

#include "engine/assets/asset_metadata.hpp"
#include "foundation/serialization/reference.hpp"

namespace mosaic::engine::assets
{

// Runtime asset table: AssetID -> (metadata, lazily-loaded + cached resource).
// Resources are type-erased (shared_ptr<void>); load<T> static_pointer_casts. No
// concrete resource types exist yet (rendering), so loaders are caller-supplied.
class AssetRegistry
{
public:
    using Loader = std::function<std::shared_ptr<void>()>;

    // Register metadata + a lazy loader (invoked at most once, on first load).
    // Re-registering the same uuid overwrites its entry.
    void register_asset(AssetMetadata metadata, Loader loader)
    {
        const AssetID id = metadata.uuid;
        Entry entry;
        entry.metadata = std::move(metadata);
        entry.loader = std::move(loader);
        entries_[id] = std::move(entry);
    }

    // Eager convenience: register metadata + an already-built resource.
    template <class T>
    void register_resource(AssetMetadata metadata, std::shared_ptr<T> resource)
    {
        std::shared_ptr<void> erased = std::move(resource);
        register_asset(std::move(metadata), [erased]() { return erased; });
    }

    bool contains(AssetID id) const { return entries_.find(id) != entries_.end(); }

    // Metadata by id; nullptr if unregistered.
    const AssetMetadata* metadata(AssetID id) const
    {
        auto it = entries_.find(id);
        return it == entries_.end() ? nullptr : &it->second.metadata;
    }

    // Resolve + cache the resource. First call runs the loader and caches; later
    // calls return the cached instance. nullptr if the id is unregistered.
    template <class T>
    std::shared_ptr<T> load(AssetID id)
    {
        auto it = entries_.find(id);
        if (it == entries_.end())
        {
            return nullptr;
        }
        Entry& entry = it->second;
        if (!entry.loaded)
        {
            entry.cached = entry.loader ? entry.loader() : nullptr;
            entry.loaded = true;
        }
        return std::static_pointer_cast<T>(entry.cached);
    }

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

} // namespace mosaic::engine::assets
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[assets]"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/assets/asset_registry.hpp tests/unit/assets/asset_registry_test.cpp tests/CMakeLists.txt
git commit -m "Add AssetRegistry: register / metadata / cached load<T> (#20)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: `AssetReferenceResolver` — validate references on deserialize

**Files:**
- Modify: `engine/assets/asset_registry.hpp`
- Test: `tests/unit/assets/asset_resolver_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `AssetRegistry` (Task 3), `serialization::ReferenceResolver`, `Handle<T>`, `to_value`/`from_value`.
- Produces:
  - `class mosaic::engine::assets::AssetReferenceResolver : public serialization::ReferenceResolver` overriding `AssetID resolve_asset(AssetID) const` — returns the id if registered, else a nil `AssetID`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/assets/asset_resolver_test.cpp`:

```cpp
#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "engine/assets/asset_metadata.hpp"
#include "engine/assets/asset_registry.hpp"
#include "foundation/serialization/reference.hpp"
#include "foundation/serialization/reflect_value.hpp"
#include "foundation/serialization/value.hpp"

using mosaic::engine::assets::AssetMetadata;
using mosaic::engine::assets::AssetRegistry;
using mosaic::engine::assets::AssetReferenceResolver;
using mosaic::foundation::serialization::AssetID;
using mosaic::foundation::serialization::from_value;
using mosaic::foundation::serialization::Handle;
using mosaic::foundation::serialization::to_value;
using mosaic::foundation::serialization::Value;

namespace
{
struct DummyResource
{
    int payload;
};

struct MeshRef
{
    Handle<DummyResource> mesh;
};
template <class V> void reflect(V& visitor, MeshRef& m)
{
    visitor.member("mesh", m.mesh);
}
} // namespace

TEST_CASE("a registered Handle survives deserialize; an unknown one resolves to nil",
          "[assets]")
{
    AssetRegistry registry;
    const AssetID known = AssetID::from_string("00000000-0000-0000-0000-0000000000a1");
    AssetMetadata meta;
    meta.uuid = known;
    meta.type = "dummy";
    registry.register_resource(meta, std::make_shared<DummyResource>(DummyResource{1}));

    AssetReferenceResolver resolver(registry);

    // Known reference passes through validation.
    MeshRef ref;
    ref.mesh = Handle<DummyResource>(known);
    Value value = to_value(ref);
    MeshRef loaded;
    from_value(value, loaded, resolver);
    REQUIRE(loaded.mesh.asset_id() == known);

    // Unknown reference resolves to a nil handle.
    MeshRef ref2;
    ref2.mesh = Handle<DummyResource>(
        AssetID::from_string("00000000-0000-0000-0000-0000000000ff"));
    Value value2 = to_value(ref2);
    MeshRef loaded2;
    from_value(value2, loaded2, resolver);
    REQUIRE(loaded2.mesh.asset_id().is_nil());
}
```

Register in `tests/CMakeLists.txt`, after the Task 3 line:

```cmake
  unit/assets/asset_resolver_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset debug`
Expected: FAIL — compile error, `'AssetReferenceResolver' is not a member of 'mosaic::engine::assets'`.

- [ ] **Step 3: Write minimal implementation**

In `engine/assets/asset_registry.hpp`, add the resolver after the `AssetRegistry` class (before the closing namespace brace):

```cpp
// Validates AssetID/Handle references against a registry on deserialize: a
// registered id passes through; an unknown id becomes a nil AssetID, so the field
// lands as a nil handle. resolve_entity stays identity (entity remap is Scene's).
class AssetReferenceResolver final : public mosaic::foundation::serialization::ReferenceResolver
{
public:
    explicit AssetReferenceResolver(const AssetRegistry& registry) : registry_(registry) {}

    mosaic::foundation::serialization::AssetID resolve_asset(
        mosaic::foundation::serialization::AssetID serialized) const override
    {
        return registry_.contains(serialized) ? serialized
                                              : mosaic::foundation::serialization::AssetID();
    }

private:
    const AssetRegistry& registry_;
};
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[assets]"`
Expected: PASS (all `[assets]` cases). Then the full suite:
Run: `ctest --preset debug`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/assets/asset_registry.hpp tests/unit/assets/asset_resolver_test.cpp tests/CMakeLists.txt
git commit -m "Add AssetReferenceResolver: validate asset refs on deserialize (#20)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 5: Update the build handoff

**Files:**
- Modify: `docs/project/next-steps.md`

- [ ] **Step 1: Update next-steps**

In `docs/project/next-steps.md`:
- Add a "built and merged" paragraph for the asset registry (#20) under *Where we are*, mirroring the existing entries: note `engine/assets/asset_metadata.hpp` (reflected metadata, TOML round-trip) and `engine/assets/asset_registry.hpp` (`AssetRegistry` type-erased cache + `AssetReferenceResolver`), the `std::hash<AssetID>` addition, and that loaders/resource types and the importer/cooker remain future work.
- In *What to build next*, drop the asset registry and make **Transform system (#21)** the suggested next target — noting it blocks on foundation math types (file those first/with it). Keep the runtime loop (#22).
- Update the *Suggested sequence* diagram to mark `#20 asset registry` done (✓).
- Note the deferred **Scene ↔ registry integration** (a combined resolver) as a small follow-up.
- Bump **Last updated** to note asset registry #20 built.

- [ ] **Step 2: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "Update next-steps: asset registry built (#20)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Self-Review

**Spec coverage:**
- `std::hash<AssetID>` → Task 1.
- `AssetMetadata` (normative fields) + TOML round-trip → Task 2.
- `AssetRegistry` register/metadata/contains → Task 3.
- Lazy loader cached once (second load no reload) → Task 3 (loader-invocation-count test).
- `register_resource` eager convenience → Task 3.
- Missing id → null/nil non-throwing → Task 3 (registry) + Task 4 (resolver).
- `AssetReferenceResolver` validates `Handle`/`AssetID` on deserialize → Task 4.
- Out-of-scope (loaders/resource types, importer/cooker, filesystem, Scene integration) → not built; recorded in spec + Task 5 handoff.
- Unit tests under `tests/unit/assets/` → Tasks 1–4.

**Placeholder scan:** No TBD/TODO in production or test steps. Task 5 is prose documentation. Every error path (unknown id, nil resolution) has explicit code + a test.

**Type consistency:** `AssetMetadata` fields (Task 2) match their uses in Task 3 (`entry.metadata`, `meta.uuid`, `meta.type`, `meta.version`) and the tests. `AssetRegistry::Loader`/`register_asset`/`register_resource`/`contains`/`metadata`/`load` (Task 3) match Task 4's `registry.contains` and every test call site. `AssetReferenceResolver::resolve_asset` (Task 4) overrides the `serialization::ReferenceResolver` virtual with the exact signature (`AssetID resolve_asset(AssetID) const`). `std::hash<AssetID>` (Task 1) is what `std::unordered_map<AssetID, Entry>` (Task 3) requires. `AssetID`/`Handle`/`Value`/`to_value`/`from_value`/`to_toml`/`from_toml` match `foundation/serialization` headers.
</content>
