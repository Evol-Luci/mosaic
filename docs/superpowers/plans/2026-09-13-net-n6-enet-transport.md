# Networking N6 (real ENet transport) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the neutral `Transport` seam (`engine/net/transport.hpp`) a real UDP backend via ENet, so `NetworkSystem`/`ReplicationSystem`/`InputSystem` can run over an actual network with zero changes to any of them.

**Architecture:** A new module `modules/net_enet` vendors ENet via `FetchContent` and wraps it in `EnetTransport : public Transport` plus two factory functions (`create_enet_server`, `create_enet_client`), mirroring `modules/audio_miniaudio`'s header/`.cpp` wall exactly. `ConnectionId`s are assigned by a monotonic counter mapped through `ENetPeer*` (never the pointer itself — ENet reuses peer pointers after disconnect).

**Tech Stack:** C++20, ENet (vendored, pinned tag), Catch2. Tests use real UDP over `127.0.0.1` in-process — no mocks, no separate machines.

**Spec:** `docs/superpowers/specs/2026-09-13-net-n6-enet-transport-design.md`

## Global Constraints

- No changes to `engine/net/transport.hpp`, `network_system.hpp`, `replication_system.hpp`, `input_system.hpp`, or `message.hpp` — `EnetTransport` implements the existing `Transport` interface exactly as-is.
- `enet_*` symbols (types, functions) never appear outside `modules/net_enet/enet_transport.cpp` — the header (`enet_transport.hpp`) declares only the two factory functions returning `std::unique_ptr<mosaic::engine::net::Transport>`. This mirrors `modules/audio_miniaudio/miniaudio_backend.hpp`'s existing wall.
- `ConnectionId` `0` stays reserved invalid/none (per `transport.hpp`) — the allocator starts at `1`.
- Tests are real UDP over `127.0.0.1`, fully automated (no manual step, no hardware, no separate machines) — use the `poll_until` bounded-wait helper below instead of a fixed tick count, since real sockets introduce real scheduling latency that the in-memory `LoopbackTransport` tests never had to account for.
- Each test case uses its own fixed port (assigned sequentially below) so cases never collide even though transports are destroyed and recreated across the file.

---

### Task 1: Vendor ENet + module scaffold + connect handshake

**Files:**
- Modify: `CMakeLists.txt` (top level)
- Create: `modules/net_enet/CMakeLists.txt`
- Create: `modules/net_enet/enet_transport.hpp`
- Create: `modules/net_enet/enet_transport.cpp`
- Modify: `tests/CMakeLists.txt`
- Create: `tests/unit/net/enet_transport_test.cpp`

**Interfaces:**
- Consumes: `mosaic::engine::net::Transport`, `NetEvent`, `Channel`, `ConnectionId` (all pre-existing, `engine/net/transport.hpp`) — unchanged by this task.
- Produces: `mosaic::net::enet::create_enet_server(std::uint16_t port, std::size_t max_peers) -> std::unique_ptr<Transport>` and `create_enet_client(const std::string& host, std::uint16_t port) -> std::unique_ptr<Transport>`. Task 2 and Task 3 extend the same `EnetTransport` class (adding `send`/message-receive and disconnect handling) — this task's `EnetTransport` handles construction, destruction, and `Connected` events only; `send()` and `disconnect()` exist as no-op stubs so the class compiles as a full `Transport` override set.

- [ ] **Step 1: Find ENet's exact CMake target name before writing the module's CMakeLists**

Add the `FetchContent_Declare` for ENet to the top-level `CMakeLists.txt`, right after the
miniaudio block (before the Dear ImGui block):

```cmake
# --- ENet (real UDP transport backend; pinned) --------------------------------
FetchContent_Declare(
  enet
  GIT_REPOSITORY https://github.com/lsalzman/enet.git
  GIT_TAG        v1.3.18
)
FetchContent_MakeAvailable(enet)
```

Then add `add_subdirectory(modules/net_enet)` to the `add_subdirectory(modules/...)` block,
right after `add_subdirectory(modules/audio_miniaudio)`.

Configure once to fetch the source and inspect what CMake target it actually exports (do not
assume — verify against the fetched source, since ENet's own `CMakeLists.txt` could rename or
namespace its target across versions):

```bash
cmake --preset debug 2>&1 | tail -30
grep -n "add_library" build/debug/_deps/enet-src/CMakeLists.txt
```

Expected: a line like `add_library(enet ...)`. Use whatever name this actually prints as the
link target in Step 3's `modules/net_enet/CMakeLists.txt` — if it differs from `enet`
(e.g. it's namespaced as `enet::enet`), use that exact name instead everywhere `enet` appears
as a link target below.

- [ ] **Step 2: Write the failing test**

Create `tests/unit/net/enet_transport_test.cpp`:

```cpp
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "engine/net/transport.hpp"
#include "modules/net_enet/enet_transport.hpp"

using mosaic::engine::net::Channel;
using mosaic::engine::net::NetEvent;
using mosaic::engine::net::Transport;
using mosaic::net::enet::create_enet_client;
using mosaic::net::enet::create_enet_server;

namespace
{
// Call pred() up to max_attempts times (sleeping sleep_ms between) until it returns true.
// pred is responsible for its own polling/event-collection — this helper only paces the
// retries. Real UDP has real scheduling latency, unlike the in-memory LoopbackTransport
// tests elsewhere, so a fixed tick count won't do; but poll() is drain-once, so this helper
// must NEVER poll on its own behalf, or it would steal events out from under pred() on the
// next iteration (this was tried and produced exactly that bug: the server's Connected
// event arrived one iteration after the client's, and a naive helper that also called
// `t.poll()` itself in its loop body silently discarded it before pred() could collect it).
template <class Pred>
void poll_until(Pred pred, int max_attempts = 200, int sleep_ms = 5)
{
    for (int i = 0; i < max_attempts && !pred(); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }
}
} // namespace

TEST_CASE("ENet transport completes a connect handshake over loopback", "[net][enet]")
{
    constexpr std::uint16_t port = 34001;
    auto server = create_enet_server(port, 4);
    auto client = create_enet_client("127.0.0.1", port);
    REQUIRE(server != nullptr);
    REQUIRE(client != nullptr);

    std::vector<NetEvent> server_events;
    std::vector<NetEvent> client_events;
    poll_until([&]
    {
        for (auto& e : server->poll()) { server_events.push_back(e); }
        for (auto& e : client->poll()) { client_events.push_back(e); }
        return !server_events.empty() && !client_events.empty();
    });

    REQUIRE(server_events.size() == 1);
    REQUIRE(server_events[0].type == NetEvent::Type::Connected);
    REQUIRE(server_events[0].connection != 0u);

    REQUIRE(client_events.size() == 1);
    REQUIRE(client_events[0].type == NetEvent::Type::Connected);
    REQUIRE(client_events[0].connection != 0u);
}
```

- [ ] **Step 3: Write `modules/net_enet/enet_transport.hpp`**

```cpp
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "engine/net/transport.hpp"

namespace mosaic::net::enet
{
// Real UDP transport over ENet. Listens on `port` for up to `max_peers` clients. Returns
// nullptr if the host could not bind (e.g. port already in use).
std::unique_ptr<mosaic::engine::net::Transport> create_enet_server(
    std::uint16_t port, std::size_t max_peers);

// Real UDP transport over ENet. Initiates a connection to `host:port`; the Connected
// NetEvent arrives from poll() once the handshake completes. Returns nullptr only if the
// local ENet host itself could not be created (never on connection failure/timeout — that
// surfaces later as an absent Connected event, mirroring how a real socket connect is
// asynchronous).
std::unique_ptr<mosaic::engine::net::Transport> create_enet_client(
    const std::string& host, std::uint16_t port);
} // namespace mosaic::net::enet
```

- [ ] **Step 4: Write `modules/net_enet/enet_transport.cpp`**

```cpp
#include "modules/net_enet/enet_transport.hpp"

#include <atomic>
#include <unordered_map>

#include <enet/enet.h>

namespace mosaic::net::enet
{
using mosaic::engine::net::Channel;
using mosaic::engine::net::ConnectionId;
using mosaic::engine::net::NetEvent;
using mosaic::engine::net::Transport;

namespace
{
std::atomic<int> g_enet_refs{0};
void acquire_enet()
{
    if (g_enet_refs.fetch_add(1) == 0) { enet_initialize(); }
}
void release_enet()
{
    if (g_enet_refs.fetch_sub(1) == 1) { enet_deinitialize(); }
}
} // namespace

class EnetTransport : public Transport
{
public:
    explicit EnetTransport(ENetHost* host) : host_(host) {}
    ~EnetTransport() override
    {
        enet_host_destroy(host_);
        release_enet();
    }

    void send(ConnectionId /*to*/, Channel /*channel*/, const std::string& /*bytes*/) override
    {
        // Implemented in Task 2.
    }

    std::vector<NetEvent> poll() override
    {
        std::vector<NetEvent> out;
        ENetEvent ev;
        while (enet_host_service(host_, &ev, 0) > 0)
        {
            if (ev.type == ENET_EVENT_TYPE_CONNECT)
            {
                const ConnectionId id = next_id_++;
                peer_to_id_[ev.peer] = id;
                id_to_peer_[id] = ev.peer;
                NetEvent out_ev;
                out_ev.type = NetEvent::Type::Connected;
                out_ev.connection = id;
                out.push_back(out_ev);
            }
            // RECEIVE/DISCONNECT handled in Task 2/Task 3.
        }
        return out;
    }

    void disconnect(ConnectionId /*who*/) override
    {
        // Implemented in Task 3.
    }

private:
    ENetHost* host_;
    std::unordered_map<ENetPeer*, ConnectionId> peer_to_id_;
    std::unordered_map<ConnectionId, ENetPeer*> id_to_peer_;
    ConnectionId next_id_ = 1; // 0 is reserved invalid (transport.hpp)
};

std::unique_ptr<Transport> create_enet_server(std::uint16_t port, std::size_t max_peers)
{
    acquire_enet();
    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = port;
    ENetHost* host = enet_host_create(&address, max_peers, /*channelLimit=*/2, 0, 0);
    if (host == nullptr)
    {
        release_enet();
        return nullptr;
    }
    return std::make_unique<EnetTransport>(host);
}

std::unique_ptr<Transport> create_enet_client(const std::string& host_str, std::uint16_t port)
{
    acquire_enet();
    ENetHost* host = enet_host_create(nullptr, 1, /*channelLimit=*/2, 0, 0);
    if (host == nullptr)
    {
        release_enet();
        return nullptr;
    }
    ENetAddress address;
    enet_address_set_host(&address, host_str.c_str());
    address.port = port;
    enet_host_connect(host, &address, /*channelCount=*/2, 0);
    return std::make_unique<EnetTransport>(host);
}
} // namespace mosaic::net::enet
```

- [ ] **Step 5: Write `modules/net_enet/CMakeLists.txt`**

Using the link-target name confirmed in Step 1 (written here as `enet`; substitute the actual
name if Step 1 found something different):

```cmake
add_library(mosaic_net_enet STATIC enet_transport.cpp)
target_include_directories(mosaic_net_enet
  PUBLIC  ${CMAKE_SOURCE_DIR}
  PRIVATE ${enet_SOURCE_DIR}/include)
target_link_libraries(mosaic_net_enet
  PUBLIC  mosaic_engine
  PRIVATE enet)
target_compile_features(mosaic_net_enet PUBLIC cxx_std_20)
```

- [ ] **Step 6: Register the test file and link the new module**

In `tests/CMakeLists.txt`, add `unit/net/enet_transport_test.cpp` to the source list right
after `unit/net/loopback_transport_test.cpp`, and add `mosaic_net_enet` to
`target_link_libraries(mosaic_unit_tests PRIVATE ...)` right after `mosaic_audio_miniaudio`.

- [ ] **Step 7: Build and run — verify RED, then GREEN**

```bash
cmake --preset debug
cmake --build build/debug --target mosaic_unit_tests -j4
```

Expected: this fails to build/link cleanly the first time only if Step 1's target name guess
was wrong — fix the `target_link_libraries` name in Step 5 against what Step 1 actually found,
then rebuild. Once it builds:

```bash
./build/debug/tests/mosaic_unit_tests "[enet]"
```

Expected: PASS — `All tests passed (N assertions in 1 test case)`.

- [ ] **Step 8: Commit**

```bash
git add CMakeLists.txt modules/net_enet tests/CMakeLists.txt tests/unit/net/enet_transport_test.cpp
git commit -m "feat(net): vendor ENet + connect handshake (networking N6)"
```

---

### Task 2: Reliable + unreliable message send/receive

**Files:**
- Modify: `modules/net_enet/enet_transport.cpp`
- Modify: `tests/unit/net/enet_transport_test.cpp`

**Interfaces:**
- Consumes: `EnetTransport` from Task 1 (its `peer_to_id_`/`id_to_peer_` maps, `poll_until` helper from the test file).
- Produces: working `send()` and the `ENET_EVENT_TYPE_RECEIVE` branch of `poll()`. Task 3 needs both untouched.

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/net/enet_transport_test.cpp`:

```cpp
TEST_CASE("ENet transport delivers a reliable message to the peer", "[net][enet]")
{
    constexpr std::uint16_t port = 34002;
    auto server = create_enet_server(port, 4);
    auto client = create_enet_client("127.0.0.1", port);

    ConnectionId server_side_client_id = 0;
    ConnectionId client_side_server_id = 0;
    poll_until([&]
    {
        for (auto& e : server->poll())
        {
            if (e.type == NetEvent::Type::Connected) { server_side_client_id = e.connection; }
        }
        for (auto& e : client->poll())
        {
            if (e.type == NetEvent::Type::Connected) { client_side_server_id = e.connection; }
        }
        return server_side_client_id != 0u && client_side_server_id != 0u;
    });

    client->send(client_side_server_id, Channel::Reliable, "ping");

    std::vector<NetEvent> received;
    poll_until([&]
    {
        client->poll(); // enet_peer_send only queues; the sender's own poll() flushes it
        for (auto& e : server->poll())
        {
            if (e.type == NetEvent::Type::Message) { received.push_back(e); }
        }
        return !received.empty();
    });

    REQUIRE(received.size() == 1);
    REQUIRE(received[0].connection == server_side_client_id);
    REQUIRE(received[0].channel == Channel::Reliable);
    REQUIRE(received[0].data == "ping");
}

TEST_CASE("ENet transport preserves the unreliable channel tag", "[net][enet]")
{
    constexpr std::uint16_t port = 34003;
    auto server = create_enet_server(port, 4);
    auto client = create_enet_client("127.0.0.1", port);

    ConnectionId client_side_server_id = 0;
    poll_until([&]
    {
        server->poll();
        for (auto& e : client->poll())
        {
            if (e.type == NetEvent::Type::Connected) { client_side_server_id = e.connection; }
        }
        return client_side_server_id != 0u;
    });

    client->send(client_side_server_id, Channel::Unreliable, "u");

    std::vector<NetEvent> received;
    poll_until([&]
    {
        client->poll();
        for (auto& e : server->poll())
        {
            if (e.type == NetEvent::Type::Message) { received.push_back(e); }
        }
        return !received.empty();
    });

    REQUIRE(received.size() == 1);
    REQUIRE(received[0].channel == Channel::Unreliable);
    REQUIRE(received[0].data == "u");
}
```

**Note (discovered while implementing):** `enet_peer_send` only queues a packet — actually
transmitting it requires the *sender's own* subsequent `poll()` call (`enet_host_service`
flushes queued sends). Every wait loop after a `send()` must keep polling the sender, not
just the recipient, or the packet never leaves the queue.

- [ ] **Step 2: Run to verify RED**

```bash
cmake --build build/debug --target mosaic_unit_tests -j4
./build/debug/tests/mosaic_unit_tests "[enet]"
```

Expected: the two new cases FAIL (time out with an empty `received`) — `send()` is still a
no-op stub and `poll()` doesn't yet surface `Message` events.

- [ ] **Step 3: Implement `send()` and the RECEIVE branch**

In `modules/net_enet/enet_transport.cpp`, replace the `send()` stub:

```cpp
    void send(ConnectionId to, Channel channel, const std::string& bytes) override
    {
        auto it = id_to_peer_.find(to);
        if (it == id_to_peer_.end()) { return; } // unknown/stale id: no-op, no crash
        const bool reliable = channel == Channel::Reliable;
        ENetPacket* packet = enet_packet_create(
            bytes.data(), bytes.size(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
        enet_peer_send(it->second, reliable ? 0 : 1, packet);
    }
```

And extend `poll()`'s event loop, adding the RECEIVE case alongside the existing CONNECT case:

```cpp
            if (ev.type == ENET_EVENT_TYPE_CONNECT)
            {
                const ConnectionId id = next_id_++;
                peer_to_id_[ev.peer] = id;
                id_to_peer_[id] = ev.peer;
                NetEvent out_ev;
                out_ev.type = NetEvent::Type::Connected;
                out_ev.connection = id;
                out.push_back(out_ev);
            }
            else if (ev.type == ENET_EVENT_TYPE_RECEIVE)
            {
                auto it = peer_to_id_.find(ev.peer);
                if (it != peer_to_id_.end())
                {
                    NetEvent out_ev;
                    out_ev.type = NetEvent::Type::Message;
                    out_ev.connection = it->second;
                    out_ev.channel = ev.channelID == 0 ? Channel::Reliable : Channel::Unreliable;
                    out_ev.data.assign(reinterpret_cast<char*>(ev.packet->data),
                                       ev.packet->dataLength);
                    out.push_back(out_ev);
                }
                enet_packet_destroy(ev.packet);
            }
```

- [ ] **Step 4: Run to verify GREEN**

```bash
cmake --build build/debug --target mosaic_unit_tests -j4
./build/debug/tests/mosaic_unit_tests "[enet]"
```

Expected: PASS — 3 test cases now (the Task 1 handshake case plus these two).

- [ ] **Step 5: Commit**

```bash
git add modules/net_enet/enet_transport.cpp tests/unit/net/enet_transport_test.cpp
git commit -m "feat(net): ENet reliable/unreliable message send+receive (networking N6)"
```

---

### Task 3: Disconnect handling, unknown-id no-op, multi-client ids

**Files:**
- Modify: `modules/net_enet/enet_transport.cpp`
- Modify: `tests/unit/net/enet_transport_test.cpp`

**Interfaces:**
- Consumes: `EnetTransport` from Tasks 1-2 (unchanged constructor/`poll`/`send` behavior for CONNECT/RECEIVE).
- Produces: working `disconnect()` and the `DISCONNECT` branch of `poll()`. Nothing later in this plan depends on this beyond the tests themselves — N7 will use the full `Transport` interface as-is.

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/net/enet_transport_test.cpp`:

```cpp
TEST_CASE("ENet transport surfaces a Disconnected event on the peer", "[net][enet]")
{
    constexpr std::uint16_t port = 34004;
    auto server = create_enet_server(port, 4);
    auto client = create_enet_client("127.0.0.1", port);

    ConnectionId server_side_client_id = 0;
    poll_until([&]
    {
        for (auto& e : server->poll())
        {
            if (e.type == NetEvent::Type::Connected) { server_side_client_id = e.connection; }
        }
        client->poll();
        return server_side_client_id != 0u;
    });

    server->disconnect(server_side_client_id);

    std::vector<NetEvent> client_events;
    poll_until([&]
    {
        server->poll(); // flush the queued disconnect command
        for (auto& e : client->poll()) { client_events.push_back(e); }
        return std::any_of(client_events.begin(), client_events.end(),
            [](const NetEvent& e) { return e.type == NetEvent::Type::Disconnected; });
    });

    REQUIRE(std::any_of(client_events.begin(), client_events.end(),
        [](const NetEvent& e) { return e.type == NetEvent::Type::Disconnected; }));
}

TEST_CASE("ENet transport drops a send to an unknown connection id", "[net][enet]")
{
    constexpr std::uint16_t port = 34005;
    auto server = create_enet_server(port, 4);
    auto client = create_enet_client("127.0.0.1", port);

    client->send(/*never-seen id*/ 999u, Channel::Reliable, "nowhere"); // no crash expected

    poll_until([&] { server->poll(); client->poll(); return true; }, /*max_attempts=*/40);
    SUCCEED("send to an unknown id did not crash");
}

TEST_CASE("ENet server assigns distinct ids to multiple clients", "[net][enet]")
{
    constexpr std::uint16_t port = 34006;
    auto server = create_enet_server(port, 4);
    auto client_a = create_enet_client("127.0.0.1", port);
    auto client_b = create_enet_client("127.0.0.1", port);

    std::vector<ConnectionId> ids;
    poll_until([&]
    {
        for (auto& e : server->poll())
        {
            if (e.type == NetEvent::Type::Connected) { ids.push_back(e.connection); }
        }
        client_a->poll();
        client_b->poll();
        return ids.size() == 2;
    }, /*max_attempts=*/400);

    REQUIRE(ids.size() == 2);
    REQUIRE(ids[0] != ids[1]);
}
```

Add `#include <algorithm>` to the test file's includes (needed for `std::any_of`).

- [ ] **Step 2: Run to verify RED**

```bash
cmake --build build/debug --target mosaic_unit_tests -j4
./build/debug/tests/mosaic_unit_tests "[enet]"
```

Expected: the disconnect case FAILS (times out, never sees `Disconnected`) — `disconnect()`
is still a no-op stub. The unknown-id and multi-client cases already PASS (send-to-unknown
was already safe from Task 2's `id_to_peer_.find` guard; multi-client id assignment already
works from Task 1's CONNECT handling) — that's expected, not a problem; the disconnect case
is this step's real target.

- [ ] **Step 3: Implement `disconnect()` and the DISCONNECT branch**

In `modules/net_enet/enet_transport.cpp`, replace the `disconnect()` stub:

```cpp
    void disconnect(ConnectionId who) override
    {
        auto it = id_to_peer_.find(who);
        if (it == id_to_peer_.end()) { return; }
        enet_peer_disconnect(it->second, 0); // graceful; DISCONNECT event follows via poll()
    }
```

And extend `poll()`'s event loop once more, adding the DISCONNECT case. Note: the pinned
ENet `v1.3.18` defines only `ENET_EVENT_TYPE_DISCONNECT` — no separate
`ENET_EVENT_TYPE_DISCONNECT_TIMEOUT` variant (that only exists in newer/unreleased ENet
branches; check `enet.h` in the fetched source if pinning a different tag):

```cpp
            else if (ev.type == ENET_EVENT_TYPE_DISCONNECT)
            {
                auto it = peer_to_id_.find(ev.peer);
                if (it != peer_to_id_.end())
                {
                    NetEvent out_ev;
                    out_ev.type = NetEvent::Type::Disconnected;
                    out_ev.connection = it->second;
                    out.push_back(out_ev);
                    id_to_peer_.erase(it->second);
                    peer_to_id_.erase(it);
                }
            }
```

- [ ] **Step 4: Run to verify GREEN**

```bash
cmake --build build/debug --target mosaic_unit_tests -j4
./build/debug/tests/mosaic_unit_tests "[enet]"
```

Expected: PASS — 6 test cases total.

Then run the full `[net]` tag to confirm no regressions in the existing in-memory-transport
tests (unaffected by this module, but confirms nothing else broke):

```bash
./build/debug/tests/mosaic_unit_tests "[net]"
```

Expected: PASS, all pre-existing `[net]` cases plus the 6 new `[enet]` ones.

- [ ] **Step 5: Commit**

```bash
git add modules/net_enet/enet_transport.cpp tests/unit/net/enet_transport_test.cpp
git commit -m "feat(net): ENet disconnect handling (networking N6)"
```

---

### Task 4: Update project docs

**Files:**
- Modify: `docs/project/next-steps.md`

**Interfaces:** none (docs only).

- [ ] **Step 1: Update `docs/project/next-steps.md`**

Replace the "Last updated" line and status paragraph to mark N6 done, in the same style as
the existing N4b/N5 entries. Summarize: `modules/net_enet` vendors ENet via `FetchContent`;
`EnetTransport` implements the existing `Transport` interface with zero changes to
`NetworkSystem`/`ReplicationSystem`/`InputSystem`; `ConnectionId`s come from a monotonic
counter mapped through `ENetPeer*` (never the pointer itself, since ENet reuses them);
`Channel::Reliable`/`Unreliable` map to ENet channels 0/1. Note that tests are real UDP over
`127.0.0.1`, fully automated via a bounded `poll_until` helper (no manual step, no separate
machines) — the first departure from the fixed-tick-count pattern every earlier `[net]` test
used, because real sockets have real scheduling latency. Get accurate test counts by running
`./build/debug/tests/mosaic_unit_tests "[enet]"` and `"[net]"` yourself rather than guessing.
Carry forward the one remaining follow-up: **N7** — `mosaic run --host`/`--connect` CLI flags,
`setup_runtime` wiring, and a demo scene, the first genuinely manual/maintainer-verified step
in the whole networking pillar (needs two running processes).

- [ ] **Step 2: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "docs(net): mark networking N6 (ENet transport) done; N7 next"
```
