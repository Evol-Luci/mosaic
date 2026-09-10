# Networking N3 — state replication (snapshot + client upsert) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add server-authoritative state replication — a `snapshot()` that serializes every `NetReplicated` entity's reflected components to JSON, and an `apply_snapshot()` that upserts them into a client World keyed by a stable `NetId` (create-on-first-sight).

**Architecture:** Reuse the existing reflection (`ComponentReflection`) + JSON (`to_json`/`from_json`) layers. A `NetReplicated { std::uint32_t id }` marker (reflected + registered "NetReplicated") tags server entities. `snapshot()` emits `{ "entities": [ { "net": <u>, "components": { "<Name>": <Value>, ... } } ] }`; `apply_snapshot()` decodes it, maps each `NetId` to a local `EntityID` via a client-held `NetIdMap` (create-on-first-sight), then `set_value()`s each component (upsert). Proven headless with two Worlds, bytes passed directly in-test.

**Tech Stack:** C++20, header-only engine headers under `engine/net/`, Catch2 tests, CMake presets (`cmake --preset debug` → `build/debug/`).

**Spec:** `docs/superpowers/specs/2026-09-09-net-n3-replication-design.md`

## Global Constraints

- C++20; headers use `#pragma once`; namespace `mosaic::engine::net`.
- Pure engine + headless: no sockets, no `NetworkSystem` wiring, no CLI/demo. Proven with two in-process Worlds and bytes passed directly.
- YAGNI: generic reflected-component snapshots for `NetReplicated` entities only, JSON-encoded, client upsert keyed by `NetId`. No despawn/removal, no delta/binary/compression, no interpolation/prediction.
- `apply_snapshot` must **never throw** — it returns `false` on malformed JSON or bad shape.
- The marker is **server-side only**: the client mirror entity is identified by the `NetIdMap` and does **not** receive a `NetReplicated` component.
- The `NetReplicated` marker is **excluded** from the components table in a snapshot (its net id is the key).
- Component field names in tests are exact: `Transform.position` (a `Vec3`), `Material.base_color` (a `Vec3`).
- Build: `cmake --build build/debug --target mosaic_unit_tests -j`. Run a tag subset: `./build/debug/tests/mosaic_unit_tests "[net]"`.
- Branch: `claude/net-n3-replication` (already checked out, spec committed at 58f4220).

---

### Task 1: `NetReplicated` marker + registration

**Files:**
- Create: `engine/net/net_replicated.hpp`
- Modify: `engine/reflection/builtin_components.hpp` (add include + one `register_component` line)
- Create: `tests/unit/net/net_replicated_test.cpp`
- Modify: `tests/CMakeLists.txt` (add the new test source)

**Interfaces:**
- Consumes: `mosaic::foundation::reflection::TypeRegistry`, `mosaic::engine::reflection::ComponentReflection`, `register_builtin_components`.
- Produces:
  - `mosaic::engine::net::NetId` = `std::uint32_t`.
  - `struct mosaic::engine::net::NetReplicated { std::uint32_t id = 0; };` with a `reflect(V&, NetReplicated&)` free function visiting member `"id"`.
  - `"NetReplicated"` registered in `register_builtin_components`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/net/net_replicated_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "engine/net/net_replicated.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/value.hpp"

using mosaic::engine::World;
using mosaic::engine::net::NetReplicated;
namespace refl = mosaic::engine::reflection;
namespace ser  = mosaic::foundation::serialization;

TEST_CASE("NetReplicated registers and reflects its id", "[net][replication]")
{
    mosaic::foundation::reflection::TypeRegistry registry;
    refl::ComponentReflection types;
    refl::register_builtin_components(registry, types);

    REQUIRE(types.has("NetReplicated"));

    World world;
    const auto e = world.create_entity();
    world.add_component<NetReplicated>(e, NetReplicated{42});

    const auto value = types.component_value(world, e, "NetReplicated");
    REQUIRE(value.has_value());
    const ser::Table& table = value->as<ser::Table>();
    const ser::Value* id = table.find("id");
    REQUIRE(id != nullptr);
    // Reflected uint32 surfaces as an unsigned integer in the Value DOM.
    REQUIRE(id->as<std::uint64_t>() == 42u);
}
```

- [ ] **Step 2: Add the test to the build**

In `tests/CMakeLists.txt`, in the `add_executable(mosaic_unit_tests ...)` source list, immediately after the line `  unit/net/network_system_test.cpp` add:

```cmake
  unit/net/net_replicated_test.cpp
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `cmake --build build/debug --target mosaic_unit_tests -j 2>&1 | tail -20`
Expected: FAIL to compile — `engine/net/net_replicated.hpp` does not exist.

- [ ] **Step 4: Create the header**

Create `engine/net/net_replicated.hpp`:

```cpp
#pragma once

#include <cstdint>

namespace mosaic::engine::net
{
// Stable network identity of a replicated entity (0 = unset).
using NetId = std::uint32_t;

// Server-side marker: tag an entity to replicate, with its stable net id.
// Reflected + registered as "NetReplicated" (authorable in scenes/inspector).
// The marker itself is NOT replicated — the net id is the snapshot key. N4
// formalizes net-id assignment; for now the id is set by whoever creates the
// entity.
struct NetReplicated
{
    std::uint32_t id = 0;
};

template <class V>
void reflect(V& visitor, NetReplicated& n)
{
    visitor.member("id", n.id);
}
} // namespace mosaic::engine::net
```

- [ ] **Step 5: Register the component**

In `engine/reflection/builtin_components.hpp`, add the include after line 27 (`#include "engine/ui/world_label.hpp"`):

```cpp
#include "engine/net/net_replicated.hpp"
```

Then, inside `register_builtin_components`, after the `"UiText"` line (currently the last registration, line 65), add:

```cpp
    types.register_component<mosaic::engine::net::NetReplicated>(registry, "NetReplicated");
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `cmake --build build/debug --target mosaic_unit_tests -j && ./build/debug/tests/mosaic_unit_tests "[replication]"`
Expected: PASS (1 test case).

- [ ] **Step 7: Commit**

```bash
git add engine/net/net_replicated.hpp engine/reflection/builtin_components.hpp tests/unit/net/net_replicated_test.cpp tests/CMakeLists.txt
git commit -m "feat(net): NetReplicated marker + builtin registration (N3 T1)"
```

---

### Task 2: `snapshot()` + `apply_snapshot()` + `NetIdMap`

**Files:**
- Create: `engine/net/replication.hpp`
- Create: `tests/unit/net/replication_test.cpp`
- Modify: `tests/CMakeLists.txt` (add the new test source)

**Interfaces:**
- Consumes:
  - `mosaic::engine::net::NetReplicated`, `NetId` (Task 1).
  - `World`: `create_entity()→EntityID`, `add_component<T>`, `get_component<T>→T*`, `has_component<T>`, `for_each<T>(fn)`, `for_each_entity(fn)`, `EntityID` (= `mosaic::foundation::EntityID`, has `.value()→std::uint64_t`).
  - `ComponentReflection`: `registered_names()→std::vector<std::string>`, `component_value(World&, EntityID, std::string_view)→std::optional<Value>`, `set_value(World&, EntityID, std::string_view, const Value&)→bool`.
  - `Value` DOM: `Value(std::uint64_t)`, `Value(ser::Table)`, `Value(ser::Array)`; `is<T>()`, `as<T>()`; `ser::Table` supports `operator[](const std::string&)`, `find(const std::string&)→const Value*`, and range-for yielding `(const std::string& key, const Value& value)`; `ser::Array` = `std::vector<Value>`.
  - `to_json(const Value&)→std::string`, `from_json(const std::string&)→Value` (throws `ser::SerializationError`).
- Produces:
  - `std::string snapshot(World&, const ComponentReflection&)`.
  - `struct NetIdMap { std::unordered_map<NetId, World::EntityID> map; };`
  - `bool apply_snapshot(const std::string& blob, World&, const ComponentReflection&, NetIdMap&)`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/net/replication_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "engine/net/net_replicated.hpp"
#include "engine/net/replication.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/render/material.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"

using mosaic::engine::World;
using mosaic::engine::net::apply_snapshot;
using mosaic::engine::net::NetIdMap;
using mosaic::engine::net::NetReplicated;
using mosaic::engine::net::snapshot;
using mosaic::engine::render::Material;
using mosaic::engine::transform::Transform;
namespace refl = mosaic::engine::reflection;

namespace
{
// Build a fresh TypeRegistry + ComponentReflection with all builtins registered.
struct Types
{
    mosaic::foundation::reflection::TypeRegistry registry;
    refl::ComponentReflection types;
    Types() { refl::register_builtin_components(registry, types); }
};
} // namespace

TEST_CASE("single NetReplicated entity replicates to a fresh client", "[net][replication]")
{
    Types t;

    World server;
    const auto se = server.create_entity();
    server.add_component<NetReplicated>(se, NetReplicated{1});
    Transform st;
    st.position = {1.0f, 2.0f, 3.0f};
    server.add_component<Transform>(se, st);
    Material sm;
    sm.base_color = {0.25f, 0.5f, 0.75f};
    server.add_component<Material>(se, sm);

    const std::string blob = snapshot(server, t.types);

    World client;
    NetIdMap ids;
    REQUIRE(apply_snapshot(blob, client, t.types, ids));

    // Exactly one client entity, matching Transform + Material, no NetReplicated.
    int count = 0;
    World::EntityID only;
    client.for_each_entity([&](World::EntityID e) { ++count; only = e; });
    REQUIRE(count == 1);

    const Transform* ct = client.get_component<Transform>(only);
    REQUIRE(ct != nullptr);
    REQUIRE(ct->position.x == 1.0f);
    REQUIRE(ct->position.y == 2.0f);
    REQUIRE(ct->position.z == 3.0f);

    const Material* cm = client.get_component<Material>(only);
    REQUIRE(cm != nullptr);
    REQUIRE(cm->base_color.x == 0.25f);

    REQUIRE_FALSE(client.has_component<NetReplicated>(only));
}

TEST_CASE("apply_snapshot is idempotent and upserts in place", "[net][replication]")
{
    Types t;

    World server;
    const auto se = server.create_entity();
    server.add_component<NetReplicated>(se, NetReplicated{7});
    Transform st;
    st.position = {0.0f, 0.0f, 0.0f};
    server.add_component<Transform>(se, st);

    World client;
    NetIdMap ids;

    const std::string blob1 = snapshot(server, t.types);
    REQUIRE(apply_snapshot(blob1, client, t.types, ids));
    // Re-apply the same blob: still one entity.
    REQUIRE(apply_snapshot(blob1, client, t.types, ids));

    int count = 0;
    World::EntityID only;
    client.for_each_entity([&](World::EntityID e) { ++count; only = e; });
    REQUIRE(count == 1);

    // Move the server entity, re-snapshot, re-apply: same entity updates.
    server.get_component<Transform>(se)->position = {5.0f, 6.0f, 7.0f};
    const std::string blob2 = snapshot(server, t.types);
    REQUIRE(apply_snapshot(blob2, client, t.types, ids));

    count = 0;
    client.for_each_entity([&](World::EntityID e) { ++count; only = e; });
    REQUIRE(count == 1); // still one — map hit, no duplicate
    REQUIRE(client.get_component<Transform>(only)->position.x == 5.0f);
}

TEST_CASE("only NetReplicated entities are replicated", "[net][replication]")
{
    Types t;

    World server;
    const auto a = server.create_entity();
    server.add_component<NetReplicated>(a, NetReplicated{1});
    server.add_component<Transform>(a, Transform{});
    const auto b = server.create_entity();
    server.add_component<NetReplicated>(b, NetReplicated{2});
    server.add_component<Transform>(b, Transform{});
    // An unmarked entity — must NOT be replicated.
    const auto c = server.create_entity();
    server.add_component<Transform>(c, Transform{});

    const std::string blob = snapshot(server, t.types);

    World client;
    NetIdMap ids;
    REQUIRE(apply_snapshot(blob, client, t.types, ids));

    int count = 0;
    client.for_each_entity([&](World::EntityID) { ++count; });
    REQUIRE(count == 2);
    REQUIRE(ids.map.size() == 2u);
}

TEST_CASE("apply_snapshot rejects malformed input without throwing", "[net][replication]")
{
    Types t;
    World client;
    NetIdMap ids;

    REQUIRE_FALSE(apply_snapshot("not json", client, t.types, ids));
    REQUIRE_FALSE(apply_snapshot("{}", client, t.types, ids)); // no "entities"

    // A snapshot of an empty server world applies cleanly (zero entities).
    World empty_server;
    const std::string blob = snapshot(empty_server, t.types);
    REQUIRE(apply_snapshot(blob, client, t.types, ids));
    int count = 0;
    client.for_each_entity([&](World::EntityID) { ++count; });
    REQUIRE(count == 0);
}
```

- [ ] **Step 2: Add the test to the build**

In `tests/CMakeLists.txt`, immediately after the `  unit/net/net_replicated_test.cpp` line added in Task 1, add:

```cmake
  unit/net/replication_test.cpp
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `cmake --build build/debug --target mosaic_unit_tests -j 2>&1 | tail -20`
Expected: FAIL to compile — `engine/net/replication.hpp` does not exist.

- [ ] **Step 4: Create the implementation**

Create `engine/net/replication.hpp`:

```cpp
#pragma once

#include <optional>
#include <string>
#include <unordered_map>

#include "engine/net/net_replicated.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/world/world.hpp"
#include "foundation/serialization/json_backend.hpp"
#include "foundation/serialization/value.hpp"

namespace mosaic::engine::net
{
namespace ser = mosaic::foundation::serialization;
using Value = ser::Value;

// Server: JSON snapshot of every NetReplicated entity's reflected components
// (the marker excluded — its net id is the key). Shape:
//   { "entities": [ { "net": <u>, "components": { "<Name>": <Value>, ... } }, ... ] }
inline std::string snapshot(World& world,
                            const mosaic::engine::reflection::ComponentReflection& types)
{
    const std::vector<std::string> names = types.registered_names();

    ser::Array entities;
    world.for_each<NetReplicated>([&](World::EntityID e, NetReplicated& nr)
    {
        ser::Table components;
        for (const std::string& name : names)
        {
            if (name == "NetReplicated")
            {
                continue; // the marker is the key, never replicated
            }
            std::optional<Value> value = types.component_value(world, e, name);
            if (value.has_value())
            {
                components[name] = *value;
            }
        }

        ser::Table node;
        node["net"] = Value(static_cast<std::uint64_t>(nr.id));
        node["components"] = Value(std::move(components));
        entities.push_back(Value(std::move(node)));
    });

    ser::Table root;
    root["entities"] = Value(std::move(entities));
    return ser::to_json(Value(std::move(root)));
}

// Client: stable NetId -> local EntityID map (create-on-first-sight).
struct NetIdMap
{
    std::unordered_map<NetId, World::EntityID> map;
};

// Read a "net" field as a NetId, accepting either an unsigned or signed integer
// (a JSON round-trip may narrow an unsigned to signed). Returns false if absent
// or not an integer.
inline bool read_net_id(const ser::Table& node, NetId& out)
{
    const Value* net = node.find("net");
    if (net == nullptr)
    {
        return false;
    }
    if (net->is<std::uint64_t>())
    {
        out = static_cast<NetId>(net->as<std::uint64_t>());
        return true;
    }
    if (net->is<std::int64_t>())
    {
        out = static_cast<NetId>(net->as<std::int64_t>());
        return true;
    }
    return false;
}

// Client: apply a snapshot — for each entity node, find-or-create its local
// entity via the map, then set_value() each component (upsert). Returns false on
// malformed JSON/shape; never throws.
inline bool apply_snapshot(const std::string& blob, World& world,
                           const mosaic::engine::reflection::ComponentReflection& types,
                           NetIdMap& ids)
{
    Value root;
    try
    {
        root = ser::from_json(blob);
    }
    catch (...)
    {
        return false; // malformed JSON throws SerializationError
    }

    if (!root.is<ser::Table>())
    {
        return false;
    }
    const ser::Table& root_table = root.as<ser::Table>();

    const Value* entities = root_table.find("entities");
    if (entities == nullptr || !entities->is<ser::Array>())
    {
        return false;
    }

    for (const Value& node_value : entities->as<ser::Array>())
    {
        if (!node_value.is<ser::Table>())
        {
            return false;
        }
        const ser::Table& node = node_value.as<ser::Table>();

        NetId net = 0;
        if (!read_net_id(node, net))
        {
            return false;
        }

        World::EntityID local;
        auto it = ids.map.find(net);
        if (it != ids.map.end())
        {
            local = it->second;
        }
        else
        {
            local = world.create_entity();
            ids.map.emplace(net, local);
        }

        const Value* components = node.find("components");
        if (components == nullptr || !components->is<ser::Table>())
        {
            continue; // no components to apply
        }
        for (const auto& [name, value] : components->as<ser::Table>())
        {
            types.set_value(world, local, name, value); // upsert (add or update)
        }
    }

    return true;
}
} // namespace mosaic::engine::net
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build/debug --target mosaic_unit_tests -j && ./build/debug/tests/mosaic_unit_tests "[replication]"`
Expected: PASS (all 5 `[replication]` cases — Task 1's plus the 4 here).

- [ ] **Step 6: Run the full net tag to confirm no regressions**

Run: `./build/debug/tests/mosaic_unit_tests "[net]"`
Expected: PASS (N1 + N2 + N3 net tests).

- [ ] **Step 7: Commit**

```bash
git add engine/net/replication.hpp tests/unit/net/replication_test.cpp tests/CMakeLists.txt
git commit -m "feat(net): snapshot + apply_snapshot with NetIdMap upsert (N3 T2)"
```

---

### Task 3: Verify + next-steps handoff

**Files:**
- Modify: `docs/project/next-steps.md` (mark N3 done, note N3-follow send-wiring as the next networking slice)

**Interfaces:**
- Consumes: nothing new. This task is verification + documentation only.
- Produces: an updated `next-steps.md` reflecting N3 complete.

- [ ] **Step 1: Run the full test suite**

Run: `cmake --build build/debug --target mosaic_unit_tests -j && ./build/debug/tests/mosaic_unit_tests`
Expected: PASS — full suite green (no regressions).

- [ ] **Step 2: Update next-steps**

In `docs/project/next-steps.md`, locate the Networking pillar section. Mark N3 (state replication) as complete, and record the follow-ups (verbatim from the spec's Non-goals):
- **N3-follow (send-wiring):** a `ReplicationSystem` where the server builds+sends a snapshot each tick over the N2 `NetworkSystem`/`Transport`, and the client applies received snapshots keyed by session.
- **N4:** entity despawn/removal + authoritative spawn/despawn + server-side `NetId` assignment policy.
- **N5:** client → server input/commands.
- **Real ENet backend + CLI** (`mosaic run --host`/`--connect`) — later maintainer-verified slice.

Match the existing formatting/checkbox style used for N1 and N2 in that file.

- [ ] **Step 3: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "docs(net): mark N3 replication done; record N3-follow/N4/N5 next steps"
```

---

## Self-Review

**1. Spec coverage:**
- `net_replicated.hpp` (NetId + reflected NetReplicated) → Task 1. ✓
- Registered "NetReplicated" in builtin_components → Task 1 Step 5. ✓
- `snapshot()` shape `{entities:[{net, components:{name:Value}}]}`, marker excluded → Task 2 Step 4. ✓
- `NetIdMap` → Task 2 Step 4. ✓
- `apply_snapshot()` try/catch → false, is<Table>/is<Array> guards, net as uint64 OR int64, create-on-first-sight + set_value upsert, never throws → Task 2 Step 4 (`read_net_id` handles the uint64/int64 dual-accept). ✓
- Tests: single replicate, idempotent upsert, multiple + selectivity, malformed/empty, reflection round-trip → Task 1 test (round-trip) + Task 2 tests (the other four). ✓
- New test sources in tests/CMakeLists.txt → Task 1 Step 2, Task 2 Step 2. ✓

**2. Placeholder scan:** No TBD/TODO/"handle edge cases" — every step has concrete code or an exact command. ✓

**3. Type consistency:** `NetId`, `NetReplicated{id}`, `NetIdMap{map}`, `snapshot(World&, const ComponentReflection&)→std::string`, `apply_snapshot(const std::string&, World&, const ComponentReflection&, NetIdMap&)→bool` are consistent across the header, tests, and the Interfaces blocks. `Transform.position`/`Material.base_color` field names match the confirmed structs. `component_value`/`set_value`/`registered_names` signatures match `component_reflection.hpp`. ✓
