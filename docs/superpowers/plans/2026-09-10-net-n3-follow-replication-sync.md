# Networking N3-follow — replication send-wiring (ReplicationSystem) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire N3's state replication over the live N2 session — a server `ReplicationSystem` broadcasts a snapshot each tick and a client `ReplicationSystem` applies received snapshots, so a client World mirrors the server World over the network.

**Architecture:** `NetworkSystem` becomes the single transport hub (it alone polls; it handles `Hello`/`Welcome`, buffers non-control frames for retrieval via `take_inbound()`, and exposes `broadcast()`). A new `ReplicationSystem` (added after `NetworkSystem`) uses N3's `snapshot()`/`apply_snapshot()`/`NetIdMap`: the server broadcasts; the client drains inbound and applies. Proven headless with two in-process Worlds linked by `make_loopback_pair()`.

**Tech Stack:** C++20, header-only engine headers under `engine/net/`, Catch2 tests, CMake presets (`cmake --preset debug` → `build/debug/`).

**Spec:** `docs/superpowers/specs/2026-09-10-net-n3-follow-replication-sync-design.md`

## Global Constraints

- C++20; headers use `#pragma once`; namespace `mosaic::engine::net`.
- Pure engine + headless: no sockets, no `setup_runtime`/CLI/demo wiring. Proven with two in-process Worlds linked by `make_loopback_pair()`.
- YAGNI: server broadcasts a **full** snapshot every tick (no delta/dirty tracking, no cadence throttling); **no** client→server send (client input is N5); snapshots go over `Channel::Reliable`.
- `ReplicationSystem` holds a raw `NetworkSystem*` + `const ComponentReflection*` + (client) its own `NetIdMap`. The World owns both systems; `NetworkSystem` is added first.
- Reuse N3: `snapshot(World&, const ComponentReflection&) → std::string`, `apply_snapshot(const std::string&, World&, const ComponentReflection&, NetIdMap&) → bool` (never throws), `NetIdMap` — all in `engine/net/replication.hpp`.
- Build: `cmake --build build/debug --target mosaic_unit_tests -j`. Run the net subset: `./build/debug/tests/mosaic_unit_tests "[net]"`.
- Branch: `claude/net-n3-follow-replication-sync` (already checked out off merged `main` `d9bc433`).
- Known pre-existing flaky test unrelated to this work: the threaded `script_system` test SIGSEGVs only under full-suite ordering (passes in isolation) — not a regression from this slice.

---

### Task 1: `MessageType::Snapshot` + decode

**Files:**
- Modify: `engine/net/message.hpp` (add enum value + extend `decode` guard)
- Test: `tests/unit/net/message_test.cpp` (add one case)

**Interfaces:**
- Consumes: existing `encode(MessageType, const std::string&)`, `decode(const std::string&, MessageType&, std::string&)`.
- Produces: `mosaic::engine::net::MessageType::Snapshot = 3`; `decode` now accepts a `Snapshot` frame.

- [ ] **Step 1: Write the failing test**

In `tests/unit/net/message_test.cpp`, add this test case at the end of the file (before any trailing namespace close, if present — otherwise at end of file):

```cpp
TEST_CASE("encode/decode round-trips a Snapshot frame", "[net]")
{
    using mosaic::engine::net::decode;
    using mosaic::engine::net::encode;
    using mosaic::engine::net::MessageType;

    const std::string blob = R"({"entities":[{"net":1,"components":{}}]})";
    MessageType type{};
    std::string payload;
    REQUIRE(decode(encode(MessageType::Snapshot, blob), type, payload));
    REQUIRE(type == MessageType::Snapshot);
    REQUIRE(payload == blob);

    // Empty payload also round-trips.
    REQUIRE(decode(encode(MessageType::Snapshot, ""), type, payload));
    REQUIRE(type == MessageType::Snapshot);
    REQUIRE(payload.empty());
}
```

Note: if `message_test.cpp` does not already `#include "engine/net/message.hpp"` and `<catch2/catch_test_macros.hpp>`, add those includes at the top (check the existing file first; it almost certainly has them).

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build/debug --target mosaic_unit_tests -j && ./build/debug/tests/mosaic_unit_tests "encode/decode round-trips a Snapshot frame"`
Expected: FAIL — `MessageType` has no member `Snapshot` (compile error), or the assertion fails because `decode` rejects the unknown type.

- [ ] **Step 3: Add the enum value**

In `engine/net/message.hpp`, change the `MessageType` enum to:

```cpp
enum class MessageType : std::uint8_t
{
    Hello    = 1, // client -> server on connect (payload: empty)
    Welcome  = 2, // server -> client (payload: u32 assigned session id)
    Snapshot = 3, // server -> client (payload: JSON replication snapshot from N3)
};
```

- [ ] **Step 4: Extend the decode guard**

In `engine/net/message.hpp`, in `decode`, change the unknown-type guard to also accept `Snapshot`:

```cpp
    if (raw != static_cast<std::uint8_t>(MessageType::Hello) &&
        raw != static_cast<std::uint8_t>(MessageType::Welcome) &&
        raw != static_cast<std::uint8_t>(MessageType::Snapshot))
    {
        return false;
    }
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake --build build/debug --target mosaic_unit_tests -j && ./build/debug/tests/mosaic_unit_tests "[net]"`
Expected: PASS — the new case plus all existing `[net]` cases (the existing `decode("\x99")`-style unknown-type rejection still holds, since `0x99` is none of the three known types).

- [ ] **Step 6: Commit**

```bash
git add engine/net/message.hpp tests/unit/net/message_test.cpp
git commit -m "feat(net): add MessageType::Snapshot + decode support (N3-follow T1)"
```

---

### Task 2: `NetworkSystem` transport hub (InboundMessage, buffering, `take_inbound`, `broadcast`)

**Files:**
- Modify: `engine/net/network_system.hpp` (add `InboundMessage`, inbound buffering in both handlers, `take_inbound()`, `broadcast()`)
- Test: `tests/unit/net/network_system_test.cpp` (add hub cases)

**Interfaces:**
- Consumes: existing `NetworkSystem(Role, std::shared_ptr<Transport>)`, `clients()`, the private `sessions_` map (`ConnectionId -> session id`), `MessageType`, `encode`/`decode` (Task 1's `Snapshot`).
- Produces:
  - `struct mosaic::engine::net::InboundMessage { std::uint32_t peer; MessageType type; std::string payload; };`
  - `std::vector<InboundMessage> NetworkSystem::take_inbound();` (drain-once — returns and clears the buffer).
  - `void NetworkSystem::broadcast(Channel channel, const std::string& bytes);` (server: sends to every session's `ConnectionId`; zero sessions = no-op).
  - Server buffers non-control frames from an established session as `{session_id, type, payload}`; client buffers non-control frames as `{0, type, payload}`.

- [ ] **Step 1: Write the failing tests**

In `tests/unit/net/network_system_test.cpp`, add `#include` for the inbound type if needed (it's in `network_system.hpp`, already included) and add a `using mosaic::engine::net::InboundMessage;` near the other usings. Then append these cases at the end of the file:

```cpp
TEST_CASE("server buffers a snapshot frame from an established session", "[net]")
{
    using mosaic::engine::net::InboundMessage;
    auto fake = std::make_shared<FakeTransport>();
    World w;
    auto& server = w.add_system<NetworkSystem>(Role::Server, fake);

    fake->inject_message(10u, encode(MessageType::Hello, ""));
    w.tick(0.016f); // establishes session 1 for conn 10

    fake->inject_message(10u, encode(MessageType::Snapshot, "BLOB"));
    fake->inject_message(99u, encode(MessageType::Snapshot, "NOPE")); // unknown conn
    w.tick(0.016f);

    std::vector<InboundMessage> got = server.take_inbound();
    REQUIRE(got.size() == 1);            // the unknown-conn frame was dropped
    REQUIRE(got[0].peer == 1u);          // sender's session id
    REQUIRE(got[0].type == MessageType::Snapshot);
    REQUIRE(got[0].payload == "BLOB");

    REQUIRE(server.take_inbound().empty()); // drain-once
}

TEST_CASE("client buffers a snapshot frame as peer 0", "[net]")
{
    using mosaic::engine::net::InboundMessage;
    auto fake = std::make_shared<FakeTransport>();
    World w;
    auto& client = w.add_system<NetworkSystem>(Role::Client, fake);

    fake->inject_message(1u, encode(MessageType::Snapshot, "BLOB"));
    w.tick(0.016f);

    std::vector<InboundMessage> got = client.take_inbound();
    REQUIRE(got.size() == 1);
    REQUIRE(got[0].peer == 0u);
    REQUIRE(got[0].type == MessageType::Snapshot);
    REQUIRE(got[0].payload == "BLOB");
}

TEST_CASE("server broadcast sends to every connected session", "[net]")
{
    auto fake = std::make_shared<FakeTransport>();
    World w;
    auto& server = w.add_system<NetworkSystem>(Role::Server, fake);

    fake->inject_message(10u, encode(MessageType::Hello, ""));
    fake->inject_message(11u, encode(MessageType::Hello, ""));
    w.tick(0.016f); // two sessions; fake->sent now holds two Welcomes

    const std::size_t before = fake->sent.size();
    server.broadcast(Channel::Reliable, encode(MessageType::Snapshot, "BLOB"));

    // Two new sends, one per connection, carrying the snapshot frame on Reliable.
    REQUIRE(fake->sent.size() == before + 2);
    std::vector<ConnectionId> targets{fake->sent[before].to, fake->sent[before + 1].to};
    std::sort(targets.begin(), targets.end());
    REQUIRE(targets == std::vector<ConnectionId>{10u, 11u});
    for (std::size_t i = before; i < fake->sent.size(); ++i)
    {
        REQUIRE(fake->sent[i].channel == Channel::Reliable);
        MessageType t{}; std::string p;
        REQUIRE(mosaic::engine::net::decode(fake->sent[i].bytes, t, p));
        REQUIRE(t == MessageType::Snapshot);
        REQUIRE(p == "BLOB");
    }
}

TEST_CASE("broadcast with no clients sends nothing", "[net]")
{
    auto fake = std::make_shared<FakeTransport>();
    World w;
    auto& server = w.add_system<NetworkSystem>(Role::Server, fake);

    server.broadcast(Channel::Reliable, encode(MessageType::Snapshot, "BLOB"));
    REQUIRE(fake->sent.empty()); // no-op, no crash
}
```

Add `#include <algorithm>` at the top of the test file if it is not already present (the broadcast test uses `std::sort`).

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build/debug --target mosaic_unit_tests -j 2>&1 | tail -20`
Expected: FAIL to compile — `NetworkSystem` has no `take_inbound`/`broadcast`, and `InboundMessage` is undeclared.

- [ ] **Step 3: Declare `InboundMessage`**

In `engine/net/network_system.hpp`, inside `namespace mosaic::engine::net`, immediately after the `enum class Role { Server, Client };` line, add:

```cpp
// A received application frame (non-control), surfaced to other systems via take_inbound().
struct InboundMessage
{
    std::uint32_t peer;      // server: sender's session id; client: 0 ("the server")
    MessageType   type;
    std::string   payload;
};
```

- [ ] **Step 4: Buffer non-control frames + add the hub methods**

In `engine/net/network_system.hpp`:

Replace the server `Message` branch in `handle_server` (currently rejects anything that is not `Hello`) with logic that keeps the `Hello` path and buffers other known frames:

```cpp
        if (ev.type == NetEvent::Type::Message)
        {
            MessageType type{}; std::string payload;
            if (!decode(ev.data, type, payload)) { return; }
            if (type == MessageType::Hello)
            {
                if (sessions_.count(ev.connection) != 0) { return; } // duplicate Hello
                const std::uint32_t id = next_session_id_++;
                sessions_.emplace(ev.connection, id);
                clients_.push_back(id);
                transport_->send(ev.connection, Channel::Reliable,
                                 encode(MessageType::Welcome, put_u32(id)));
                world.events().emit(NetworkEvent{NetworkEvent::Type::PeerJoined, id});
            }
            else
            {
                // Non-control frame: buffer only from an established session.
                auto it = sessions_.find(ev.connection);
                if (it != sessions_.end())
                {
                    inbound_.push_back({it->second, type, payload});
                }
            }
        }
        else if (ev.type == NetEvent::Type::Disconnected)
```

(The `Disconnected` branch and the rest of `handle_server` are unchanged — keep them exactly as they are.)

Replace the client `Message` branch in `handle_client` (currently rejects anything that is not `Welcome`) with:

```cpp
        else if (ev.type == NetEvent::Type::Message)
        {
            MessageType type{}; std::string payload;
            if (!decode(ev.data, type, payload)) { return; }
            if (type == MessageType::Welcome)
            {
                local_id_ = get_u32(payload);
                connected_ = true;
                world.events().emit(NetworkEvent{NetworkEvent::Type::SessionEstablished, local_id_});
            }
            else
            {
                inbound_.push_back({0u, type, payload});
            }
        }
```

(The client `Connected` and `Disconnected` branches are unchanged.)

Add the two public methods — put them right after the `clients()` accessor in the public section:

```cpp
    // Drain buffered non-control frames received since the last call (drain-once).
    std::vector<InboundMessage> take_inbound()
    {
        std::vector<InboundMessage> out = std::move(inbound_);
        inbound_.clear();
        return out;
    }

    // Server: send `bytes` to every connected session. Zero sessions -> no-op.
    void broadcast(Channel channel, const std::string& bytes)
    {
        if (!transport_) { return; }
        for (const auto& [conn, id] : sessions_)
        {
            (void)id;
            transport_->send(conn, channel, bytes);
        }
    }
```

Add the private member — next to the other server/client members:

```cpp
    std::vector<InboundMessage> inbound_;
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build/debug --target mosaic_unit_tests -j && ./build/debug/tests/mosaic_unit_tests "[net]"`
Expected: PASS — the four new hub cases plus every existing `[net]` case (the handshake, monotonic-ids, duplicate-Hello, disconnect, and no-traffic tests all still pass, since control-frame behavior is unchanged).

- [ ] **Step 6: Commit**

```bash
git add engine/net/network_system.hpp tests/unit/net/network_system_test.cpp
git commit -m "feat(net): NetworkSystem transport hub — inbound buffer + broadcast (N3-follow T2)"
```

---

### Task 3: `ReplicationSystem` + end-to-end replication over the loopback

**Files:**
- Create: `engine/net/replication_system.hpp`
- Create: `tests/unit/net/replication_system_test.cpp`
- Modify: `tests/CMakeLists.txt` (add the new test source)

**Interfaces:**
- Consumes:
  - `NetworkSystem` (Task 2): `broadcast(Channel, const std::string&)`, `take_inbound() → std::vector<InboundMessage>`, `clients() → const std::vector<std::uint32_t>&`, `Role`, `InboundMessage`.
  - N3: `snapshot(World&, const ComponentReflection&) → std::string`, `apply_snapshot(const std::string&, World&, const ComponentReflection&, NetIdMap&) → bool`, `NetIdMap`.
  - `message.hpp`: `encode`, `MessageType::Snapshot`, `Channel::Reliable`.
  - `World`: `add_system<S>(args…) → S&`, `get_component<T>`, `has_component<T>`, `for_each_entity`, `tick(float)`; `register_builtin_components`, `Transform`, `Material`, `NetReplicated`.
- Produces: `class mosaic::engine::net::ReplicationSystem : public ecs::System` with ctor `ReplicationSystem(Role, NetworkSystem*, const ComponentReflection*)`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/net/replication_system_test.cpp`:

```cpp
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/net/loopback_transport.hpp"
#include "engine/net/network_system.hpp"
#include "engine/net/net_replicated.hpp"
#include "engine/net/replication_system.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/render/material.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"

using mosaic::engine::World;
using mosaic::engine::net::make_loopback_pair;
using mosaic::engine::net::NetworkSystem;
using mosaic::engine::net::NetReplicated;
using mosaic::engine::net::ReplicationSystem;
using mosaic::engine::net::Role;
using mosaic::engine::render::Material;
using mosaic::engine::transform::Transform;
namespace refl = mosaic::engine::reflection;

TEST_CASE("client World mirrors the server over the session", "[net][replication]")
{
    // Shared component reflection (type metadata only — safe across both Worlds).
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

    // Server scene: one replicated entity + one unmarked entity.
    const auto se = server_world.create_entity();
    server_world.add_component<NetReplicated>(se, NetReplicated{1});
    Transform st; st.position = {1.0f, 2.0f, 3.0f};
    server_world.add_component<Transform>(se, st);
    Material sm; sm.base_color = {0.25f, 0.5f, 0.75f};
    server_world.add_component<Material>(se, sm);

    const auto unmarked = server_world.create_entity();
    server_world.add_component<Transform>(unmarked, Transform{});

    auto pump = [&]() {
        for (int i = 0; i < 8; ++i) { client_world.tick(0.016f); server_world.tick(0.016f); }
    };

    pump(); // handshake + first snapshot round-trip

    // Client mirrors exactly the one replicated entity.
    int count = 0;
    World::EntityID only;
    client_world.for_each_entity([&](World::EntityID e) { ++count; only = e; });
    REQUIRE(count == 1);

    const Transform* ct = client_world.get_component<Transform>(only);
    REQUIRE(ct != nullptr);
    REQUIRE(ct->position.x == 1.0f);
    REQUIRE(ct->position.y == 2.0f);
    REQUIRE(ct->position.z == 3.0f);
    const Material* cm = client_world.get_component<Material>(only);
    REQUIRE(cm != nullptr);
    REQUIRE(cm->base_color.x == 0.25f);
    REQUIRE_FALSE(client_world.has_component<NetReplicated>(only));

    // Move the server entity; the client updates in place, still exactly one entity.
    server_world.get_component<Transform>(se)->position = {5.0f, 6.0f, 7.0f};
    pump();

    count = 0;
    client_world.for_each_entity([&](World::EntityID e) { ++count; only = e; });
    REQUIRE(count == 1); // no duplicate from repeated per-tick snapshots
    REQUIRE(client_world.get_component<Transform>(only)->position.x == 5.0f);
}
```

- [ ] **Step 2: Add the test to the build**

In `tests/CMakeLists.txt`, in the `add_executable(mosaic_unit_tests …)` source list, immediately after the line `  unit/net/replication_test.cpp` add:

```cmake
  unit/net/replication_system_test.cpp
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `cmake --build build/debug --target mosaic_unit_tests -j 2>&1 | tail -20`
Expected: FAIL to compile — `engine/net/replication_system.hpp` does not exist.

- [ ] **Step 4: Create the ReplicationSystem header**

Create `engine/net/replication_system.hpp`:

```cpp
#pragma once

#include <string>
#include <vector>

#include "engine/ecs/system.hpp"
#include "engine/net/message.hpp"
#include "engine/net/network_system.hpp"
#include "engine/net/replication.hpp"
#include "engine/net/transport.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/world/world.hpp"

namespace mosaic::engine::net
{
// Wires N3 replication over the N2 session. Server: broadcast a full snapshot each tick
// (when clients are connected). Client: apply received snapshots into the local World.
// Added to the World AFTER NetworkSystem so inbound frames are available each tick.
class ReplicationSystem : public mosaic::engine::ecs::System
{
public:
    ReplicationSystem(Role role, NetworkSystem* net,
                      const mosaic::engine::reflection::ComponentReflection* types)
        : role_(role), net_(net), types_(types) {}

    void update(mosaic::engine::World& world, float /*dt*/) override
    {
        if (net_ == nullptr || types_ == nullptr) { return; }
        if (role_ == Role::Server)
        {
            if (net_->clients().empty()) { return; } // nothing to serialize/send
            net_->broadcast(Channel::Reliable,
                            encode(MessageType::Snapshot, snapshot(world, *types_)));
        }
        else
        {
            for (const InboundMessage& msg : net_->take_inbound())
            {
                if (msg.type == MessageType::Snapshot)
                {
                    apply_snapshot(msg.payload, world, *types_, ids_);
                }
            }
        }
    }

private:
    Role role_;
    NetworkSystem* net_;
    const mosaic::engine::reflection::ComponentReflection* types_;
    NetIdMap ids_; // client-held stable NetId -> local EntityID map
};
} // namespace mosaic::engine::net
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake --build build/debug --target mosaic_unit_tests -j && ./build/debug/tests/mosaic_unit_tests "client World mirrors the server over the session"`
Expected: PASS.

- [ ] **Step 6: Run the net + replication tags to confirm no regressions**

Run: `./build/debug/tests/mosaic_unit_tests "[net]" && ./build/debug/tests/mosaic_unit_tests "[replication]"`
Expected: PASS (all net + replication cases, including N3's and this slice's).

- [ ] **Step 7: Commit**

```bash
git add engine/net/replication_system.hpp tests/unit/net/replication_system_test.cpp tests/CMakeLists.txt
git commit -m "feat(net): ReplicationSystem — broadcast/apply snapshots over the session (N3-follow T3)"
```

---

### Task 4: Verify + next-steps handoff

**Files:**
- Modify: `docs/project/next-steps.md` (mark N3-follow done; note N4 as the next networking slice)

**Interfaces:**
- Consumes: nothing new. Verification + documentation only.
- Produces: an updated `next-steps.md` reflecting N3-follow complete.

- [ ] **Step 1: Run the full test suite**

Run: `cmake --build build/debug --target mosaic_unit_tests -j && ./build/debug/tests/mosaic_unit_tests`
Expected: PASS — full suite green, **except** the one pre-existing flaky `script_system` threaded-Python SIGSEGV noted in Global Constraints (it fails only under full-suite ordering, passes in isolation, and is unrelated to networking). If any **other** test fails, stop and investigate.

- [ ] **Step 2: Update next-steps**

In `docs/project/next-steps.md`, the newest entry is prepended to the single `**Last updated:**` line (line 7). Immediately after `**Last updated:** 2026-09-10 (` (or the current date token at the front of line 7), insert a new entry in the same dense style as the existing N3/N2 entries, stating: **Networking N3-follow (replication send-wiring) DONE** — `NetworkSystem` is now the single transport hub (`InboundMessage` + drain-once `take_inbound()` + server `broadcast()`, buffering non-control frames), a new `MessageType::Snapshot`, and a `ReplicationSystem` (`engine/net/replication_system.hpp`) that on the server broadcasts a full `snapshot()` each tick when clients are connected and on the client drains inbound and `apply_snapshot()`s — so a client World mirrors the server World over the live session, proven headless with two loopback-linked Worlds (client mirrors only the replicated entity, matches Transform/Material, no marker; moving the server entity updates the client in place with no duplicate). Channel::Reliable; no delta/throttling/client-input/despawn/`setup_runtime` wiring. Then record the next networking slices verbatim: **N4** entity despawn/removal replication + authoritative spawn/despawn + server-side `NetId` assignment policy; **N5** client→server input/commands (the `send_to_server` counterpart of `broadcast`); the real **ENet UDP backend** + `mosaic run --host`/`--connect` + `setup_runtime`/demo — later maintainer-verified slice. Add the spec/plan links: `[N3-follow spec](../superpowers/specs/2026-09-10-net-n3-follow-replication-sync-design.md)` · `[N3-follow plan](../superpowers/plans/2026-09-10-net-n3-follow-replication-sync.md)`. Match the existing markdown/link formatting.

- [ ] **Step 3: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "docs(net): mark N3-follow replication send-wiring done; N4 next"
```

---

## Self-Review

**1. Spec coverage:**
- `MessageType::Snapshot` + `decode` acceptance → Task 1. ✓
- `NetworkSystem` hub: `InboundMessage`, buffering (server: established-session only; client: peer 0), `take_inbound()` drain-once, `broadcast()` (server→all, zero = no-op) → Task 2. ✓
- `ReplicationSystem`: ctor `(Role, NetworkSystem*, const ComponentReflection*)`; server broadcasts full snapshot each tick when `clients()` non-empty; client drains inbound + `apply_snapshot` → Task 3 Step 4. ✓
- `Channel::Reliable` for snapshots → Task 3 Step 4. ✓
- Added after `NetworkSystem`; `ReplicationSystem` owns `NetIdMap` → Task 3 (test adds NetworkSystem first; header holds `ids_`). ✓
- Testing: message Snapshot round-trip (Task 1); hub broadcast/take_inbound/drain-once/zero-clients/unknown-conn (Task 2); end-to-end mirror + selectivity + no-`NetReplicated` + update-in-place + no-duplicate (Task 3). ✓
- New test source in `tests/CMakeLists.txt` → Task 3 Step 2. ✓
- next-steps handoff → Task 4. ✓

**2. Placeholder scan:** No TBD/TODO/"handle edge cases" — every code step has concrete code or an exact command. ✓

**3. Type consistency:** `InboundMessage { peer, type, payload }`, `take_inbound() → std::vector<InboundMessage>`, `broadcast(Channel, const std::string&)`, `ReplicationSystem(Role, NetworkSystem*, const ComponentReflection*)`, `snapshot(World&, const ComponentReflection&)`, `apply_snapshot(const std::string&, World&, const ComponentReflection&, NetIdMap&)`, `MessageType::Snapshot`, `Channel::Reliable` are consistent across Tasks 1–3 and the test code. `Transform.position` / `Material.base_color` match the confirmed structs. `add_system` returns `S&` (test takes `&server_net`). ✓
