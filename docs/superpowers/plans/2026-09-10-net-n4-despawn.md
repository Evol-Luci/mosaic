# Networking N4 — despawn / removal replication Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `apply_snapshot` authoritative — a replicated entity removed on the server is removed on the client, by destroying any tracked `NetId` absent from the latest full snapshot.

**Architecture:** Snapshots are always the full authoritative set, so the newest snapshot is ground truth. `apply_snapshot` gains a reconcile step: after add/update, it destroys every `NetId` in the `NetIdMap` that is absent from the snapshot and erases it from the map. No new message, no new system — the client `ReplicationSystem` (N3-follow) gains despawn for free.

**Tech Stack:** C++20, header-only engine headers under `engine/net/`, Catch2 tests, CMake presets (`cmake --preset debug` → `build/debug/`).

**Spec:** `docs/superpowers/specs/2026-09-10-net-n4-despawn-design.md`

## Global Constraints

- C++20; headers use `#pragma once`; namespace `mosaic::engine::net`.
- Pure engine + headless: no sockets, no `setup_runtime`/CLI/demo. Proven with direct `apply_snapshot` calls and two loopback-linked Worlds.
- YAGNI: implicit despawn from the full snapshot — **no** new message type, **no** server-side change tracking, **no** explicit despawn messages, **no** delta snapshots.
- Removal happens **only on the success path**: a malformed/partial snapshot returns `false` and removes nothing (membership is only reconciled against a fully-parsed snapshot).
- `apply_snapshot` signature is unchanged: `bool apply_snapshot(const std::string&, World&, const ComponentReflection&, NetIdMap&)`.
- `World::destroy_entity` is **deferred** — committed at the end of `tick()`. The `ids.map` erase is **immediate**. A direct (no-system) test must `world.tick(...)` once after applying to commit the destruction before asserting entity counts; `ids.map` size can be asserted immediately.
- Field names: `Transform.position` (a `Vec3`), `Material.base_color` (a `Vec3`).
- Build: `cmake --build build/debug --target mosaic_unit_tests -j`. Run the subset: `./build/debug/tests/mosaic_unit_tests "[replication]"`.
- Branch: `claude/net-n4-despawn` (already checked out off merged `main` `b4be3c4`).
- Known pre-existing flaky test unrelated to this work: the threaded `script_system` test SIGSEGVs only under full-suite ordering (passes in isolation) — not a regression from this slice.

---

### Task 1: Removal logic in `apply_snapshot` + direct despawn tests

**Files:**
- Modify: `engine/net/replication.hpp` (add includes; add `seen` set, `seen.insert`, reconcile block in `apply_snapshot`)
- Test: `tests/unit/net/replication_test.cpp` (add two cases)

**Interfaces:**
- Consumes: existing `apply_snapshot`, `snapshot`, `NetIdMap`, `read_net_id`, `NetId`; `World::destroy_entity(EntityID)`, `World::create_entity`, `World::tick(float)`, `World::for_each_entity`, `World::get_component<T>`; N3 test helper `Types` (builds `TypeRegistry` + `ComponentReflection` via `register_builtin_components`).
- Produces: `apply_snapshot` now removes tracked `NetId`s absent from the snapshot (add + update + **remove**), erasing them from `ids.map`; signature unchanged.

- [ ] **Step 1: Write the failing tests**

In `tests/unit/net/replication_test.cpp`, append these two cases at the end of the file (the `Types` helper and all `using`s are already defined at the top of that file):

```cpp
TEST_CASE("absent NetId is despawned on the next snapshot", "[net][replication]")
{
    Types t;

    // Server with two replicated entities.
    World server;
    const auto a = server.create_entity();
    server.add_component<NetReplicated>(a, NetReplicated{1});
    Transform ta; ta.position = {1.0f, 0.0f, 0.0f};
    server.add_component<Transform>(a, ta);
    const auto b = server.create_entity();
    server.add_component<NetReplicated>(b, NetReplicated{2});
    server.add_component<Transform>(b, Transform{});

    World client;
    NetIdMap ids;
    REQUIRE(apply_snapshot(snapshot(server, t.types), client, t.types, ids));
    REQUIRE(ids.map.size() == 2u);

    // Server removes entity B; its destruction commits on the server tick.
    server.destroy_entity(b);
    server.tick(0.016f);

    // The new snapshot has only net 1. Applying it despawns net 2 on the client.
    REQUIRE(apply_snapshot(snapshot(server, t.types), client, t.types, ids));
    REQUIRE(ids.map.size() == 1u);       // map updated immediately (erase is not deferred)
    REQUIRE(ids.map.count(1u) == 1u);

    // destroy_entity is deferred: before a client tick, the entity is still present.
    int before = 0;
    client.for_each_entity([&](World::EntityID) { ++before; });
    REQUIRE(before == 2);

    // Committing the deferred destruction leaves exactly the net-1 entity.
    client.tick(0.016f);
    int after = 0;
    World::EntityID only;
    client.for_each_entity([&](World::EntityID e) { ++after; only = e; });
    REQUIRE(after == 1);
    REQUIRE(client.get_component<Transform>(only)->position.x == 1.0f); // net 1 survived
}

TEST_CASE("re-applying the same snapshot removes nothing", "[net][replication]")
{
    Types t;
    World server;
    const auto a = server.create_entity();
    server.add_component<NetReplicated>(a, NetReplicated{1});
    server.add_component<Transform>(a, Transform{});
    const auto b = server.create_entity();
    server.add_component<NetReplicated>(b, NetReplicated{2});
    server.add_component<Transform>(b, Transform{});

    World client;
    NetIdMap ids;
    const std::string blob = snapshot(server, t.types);
    REQUIRE(apply_snapshot(blob, client, t.types, ids));
    REQUIRE(apply_snapshot(blob, client, t.types, ids)); // re-apply the identical snapshot
    REQUIRE(ids.map.size() == 2u);

    client.tick(0.016f);
    int count = 0;
    client.for_each_entity([&](World::EntityID) { ++count; });
    REQUIRE(count == 2); // nothing despawned
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build/debug --target mosaic_unit_tests -j && ./build/debug/tests/mosaic_unit_tests "absent NetId is despawned on the next snapshot"`
Expected: FAIL — the client still has 2 entities after the tick (`after == 2`), because `apply_snapshot` does not yet remove absent ids, and `ids.map.size()` is still 2.

- [ ] **Step 3: Add the includes**

In `engine/net/replication.hpp`, add these two includes immediately after the existing `#include <unordered_map>` line:

```cpp
#include <unordered_set>
#include <vector>
```

- [ ] **Step 4: Record seen ids and reconcile**

In `engine/net/replication.hpp`, in `apply_snapshot`:

(a) Immediately **before** the `for (const Value& node_value : entities->as<ser::Array>())` loop, declare the seen-set:

```cpp
    std::unordered_set<NetId> seen;
```

(b) Inside the loop, immediately **after** the `read_net_id` success check (right after the block that does `if (!read_net_id(node, net)) { return false; }`), record the id:

```cpp
        seen.insert(net);
```

(c) Immediately **after** the node loop closes and **before** the final `return true;`, add the reconcile block:

```cpp
    // Remove tracked entities absent from this (full) snapshot: server-authoritative despawn.
    std::vector<NetId> to_remove;
    for (const auto& [net_id, local] : ids.map)
    {
        (void)local;
        if (seen.find(net_id) == seen.end())
        {
            to_remove.push_back(net_id);
        }
    }
    for (NetId net_id : to_remove)
    {
        world.destroy_entity(ids.map[net_id]);
        ids.map.erase(net_id);
    }
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build/debug --target mosaic_unit_tests -j && ./build/debug/tests/mosaic_unit_tests "[replication]"`
Expected: PASS — the two new cases plus every existing `[replication]` case (N3's single/idempotent/selectivity/malformed-empty all still pass, since a clean snapshot records every tracked id in `seen` and removes nothing).

- [ ] **Step 6: Commit**

```bash
git add engine/net/replication.hpp tests/unit/net/replication_test.cpp
git commit -m "feat(net): apply_snapshot despawns NetIds absent from the snapshot (N4 T1)"
```

---

### Task 2: End-to-end despawn over the loopback

**Files:**
- Test: `tests/unit/net/replication_system_test.cpp` (add one case)

**Interfaces:**
- Consumes: Task 1's authoritative `apply_snapshot` (through the client `ReplicationSystem`); `NetworkSystem`, `ReplicationSystem`, `Role`, `make_loopback_pair` (N2/N3-follow); `register_builtin_components`, `Transform`, `NetReplicated`, `World::destroy_entity`.
- Produces: integration proof that a server-side removal propagates to the client.

- [ ] **Step 1: Write the failing test**

In `tests/unit/net/replication_system_test.cpp`, append this case at the end of the file (the includes — `loopback_transport.hpp`, `network_system.hpp`, `net_replicated.hpp`, `replication_system.hpp`, `builtin_components.hpp`, `transform.hpp`, `world.hpp`, `type_registry.hpp` — and the `using`s / `refl` alias are already present from the N3-follow test):

```cpp
TEST_CASE("client despawns an entity removed on the server", "[net][replication]")
{
    mosaic::foundation::reflection::TypeRegistry registry;
    refl::ComponentReflection types;
    refl::register_builtin_components(registry, types);

    auto [server_tx, client_tx] = make_loopback_pair();

    World server_world;
    auto& server_net = server_world.add_system<NetworkSystem>(Role::Server, server_tx);
    server_world.add_system<ReplicationSystem>(Role::Server, &server_net, &types);

    World client_world;
    auto& client_net = client_world.add_system<NetworkSystem>(Role::Client, client_tx);
    client_world.add_system<ReplicationSystem>(Role::Client, &client_net, &types);

    // Two replicated entities on the server.
    const auto a = server_world.create_entity();
    server_world.add_component<NetReplicated>(a, NetReplicated{1});
    Transform ta; ta.position = {1.0f, 0.0f, 0.0f};
    server_world.add_component<Transform>(a, ta);
    const auto b = server_world.create_entity();
    server_world.add_component<NetReplicated>(b, NetReplicated{2});
    server_world.add_component<Transform>(b, Transform{});

    auto pump = [&]() {
        for (int i = 0; i < 8; ++i) { client_world.tick(0.016f); server_world.tick(0.016f); }
    };

    pump();
    int count = 0;
    client_world.for_each_entity([&](World::EntityID) { ++count; });
    REQUIRE(count == 2); // client mirrors both

    // Remove B on the server; the client should drop exactly it.
    server_world.destroy_entity(b);
    pump();

    count = 0;
    World::EntityID only;
    client_world.for_each_entity([&](World::EntityID e) { ++count; only = e; });
    REQUIRE(count == 1);
    REQUIRE(client_world.get_component<Transform>(only)->position.x == 1.0f); // net 1 survived
}
```

- [ ] **Step 2: Run the test to verify it passes**

Run: `cmake --build build/debug --target mosaic_unit_tests -j && ./build/debug/tests/mosaic_unit_tests "client despawns an entity removed on the server"`
Expected: PASS (Task 1 already implemented the behavior; this is the integration proof through `ReplicationSystem` + `tick`).

Note: if this test unexpectedly fails while Task 1's direct tests pass, the likely cause is too few `pump()` iterations for the removal to propagate — but 8 ticks per pump is ample (the handshake needs ~2 and each later server tick re-broadcasts the post-removal snapshot). Do not change Task 1's logic to satisfy this test.

- [ ] **Step 3: Run the replication + net tags to confirm no regressions**

Run: `./build/debug/tests/mosaic_unit_tests "[replication]" && ./build/debug/tests/mosaic_unit_tests "[net]"`
Expected: PASS.

- [ ] **Step 4: Commit**

```bash
git add tests/unit/net/replication_system_test.cpp
git commit -m "test(net): end-to-end despawn over the loopback (N4 T2)"
```

---

### Task 3: Verify + next-steps handoff

**Files:**
- Modify: `docs/project/next-steps.md` (mark N4 done; note N4b + N5 + ENet as next)

**Interfaces:**
- Consumes: nothing new. Verification + documentation only.
- Produces: an updated `next-steps.md` reflecting N4 complete.

- [ ] **Step 1: Run the full test suite**

Run: `cmake --build build/debug --target mosaic_unit_tests -j && ./build/debug/tests/mosaic_unit_tests`
Expected: PASS — full suite green, **except** the one pre-existing flaky `script_system` threaded-Python SIGSEGV noted in Global Constraints (fails only under full-suite ordering, passes in isolation, unrelated to networking). If any **other** test fails, stop and investigate.

- [ ] **Step 2: Update next-steps**

In `docs/project/next-steps.md`, the newest entry is prepended to the single `**Last updated:**` line (line 7). Immediately after `**Last updated:** 2026-09-10 (` at the front of line 7, insert a new entry in the same dense style as the existing N3-follow/N3 entries, stating: **Networking N4 (despawn / removal replication) DONE** — `apply_snapshot` (`engine/net/replication.hpp`) is now **authoritative**: beyond N3's create-on-first-sight + upsert, it records every `NetId` present in the (full) snapshot and, on the success path only, destroys every tracked `NetId` absent from it (`world.destroy_entity` + erase from `NetIdMap`) — so a server-side removal propagates to the client with **no new message type** (the full snapshot is ground truth). `destroy_entity` is deferred (commits on `tick`); the map erase is immediate. Proven headless (direct: a two-entity client, after a one-entity snapshot, drops to one entity on tick with the map shrinking immediately; re-applying an identical snapshot removes nothing; end-to-end over the loopback: server `destroy_entity` → client drops exactly that entity). Malformed/partial snapshots still return false and remove nothing. `[net]` + `[replication]` green; rest of suite green except the pre-existing flaky `script_system` threaded-Python SIGSEGV (full-suite-ordering only, networking-unrelated). Next networking slices: **N4b** server-side `NetId` assignment policy (stamp a unique monotonic id on `NetReplicated` entities created with `id == 0`, so game/script code need not hand-pick ids); **N5** client→server input/commands; the real **ENet UDP backend** + `mosaic run --host`/`--connect` + `setup_runtime`/demo — later maintainer-verified slice. Add the spec/plan links: `[N4 spec](../superpowers/specs/2026-09-10-net-n4-despawn-design.md)` · `[N4 plan](../superpowers/plans/2026-09-10-net-n4-despawn.md)`. Match the existing markdown/link formatting.

- [ ] **Step 3: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "docs(net): mark N4 despawn/removal replication done; N4b next"
```

---

## Self-Review

**1. Spec coverage:**
- `apply_snapshot` authoritative: record `seen`, reconcile on success path, `destroy_entity` + erase from map → Task 1 Step 4. ✓
- No new message/system; signature unchanged → Task 1 (edit-in-place). ✓
- Removal only on success path (malformed → false, no removal) → preserved (the reconcile runs only after the loop returns no `false`) → Task 1 Step 4(c). ✓
- Deferred-destroy / immediate-map-erase nuance → Task 1 test asserts `before == 2` then `after == 1`, and `ids.map.size()` immediately. ✓
- Tests: direct despawn, clean-snapshot-removes-nothing (Task 1); end-to-end despawn (Task 2). ✓
- next-steps handoff → Task 3. ✓

**2. Placeholder scan:** No TBD/TODO/"handle edge cases" — every code step has concrete code or an exact command. ✓

**3. Type consistency:** `apply_snapshot(const std::string&, World&, const ComponentReflection&, NetIdMap&)`, `NetIdMap::map`, `NetId`, `std::unordered_set<NetId> seen`, `world.destroy_entity(ids.map[net_id])`, `snapshot(World&, const ComponentReflection&)` match `replication.hpp` and the N3/N3-follow tests. `Transform.position` matches the struct. `Types` helper and `refl` alias exist in the respective test files. ✓
