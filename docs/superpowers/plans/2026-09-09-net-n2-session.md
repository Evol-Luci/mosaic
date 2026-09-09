# Networking N2 — session lifecycle + NetworkSystem Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A `NetworkSystem` (injected `Transport` + server/client `Role`) that runs a Hello/Welcome handshake, assigns a monotonic session id, and surfaces connect/disconnect on the World event bus — proven headless via the N1 loopback and an in-test fake transport.

**Architecture:** `engine/net/message.hpp` frames control messages (1 type byte + payload). `engine/net/network_event.hpp` is the bus event. `engine/net/network_system.hpp` is an `ecs::System` that each tick drains `transport->poll()` and, by role, drives the handshake, tracks sessions, and emits `NetworkEvent`s. Pure engine, header-only, no `setup_runtime`/CLI/real-backend wiring.

**Tech Stack:** C++20, Catch2, CMake. Builds on N1 (`engine/net/transport.hpp`, `loopback_transport.hpp`).

**Spec:** `docs/superpowers/specs/2026-09-09-net-n2-session-design.md`

**Base branch:** `claude/net-n2-session` (off `main` = N1 merged; `cmake --preset debug` configured).

## Global Constraints

- **Build convention:** presets into `build/debug/`. Build: `cmake --build --preset debug --target mosaic_unit_tests`. Run: `./build/debug/tests/mosaic_unit_tests "[net]"`. Full suite: `ctest --preset debug`. Do **not** use `cmake --build build`. **Transient link failures:** the ~200 MB test binary intermittently fails at LINK under memory pressure with no compiler error — `grep -iE "error:|error C[0-9]|undefined reference"`; if empty, re-run (up to ~3×); if it leaves a **0-byte** binary, `rm -f build/debug/tests/mosaic_unit_tests` and rebuild.
- **Namespace:** `mosaic::engine::net`. Pure engine, header-only, no external dep, no sockets.
- **Message frame:** `[1 byte MessageType][payload]`. `MessageType{ Hello=1, Welcome=2 }`. `Hello` payload empty; `Welcome` payload = `put_u32(session_id)` (little-endian).
- **Session ids:** server-assigned, **monotonic from 1**, decoupled from the transport `ConnectionId`. The client learns its id from `Welcome`.
- **World API:** `world.add_system<S>(args...) -> S&`; `world.get_system<S>() -> S*`; `world.tick(float)`; `world.events().emit(const E&)`; `world.events().subscribe<E>(std::function<void(const E&)>)` (sync — fires during the emitting tick).
- **N1 seam (do not change):** `Transport::{ send(ConnectionId, Channel, const std::string&), poll() -> vector<NetEvent>, disconnect(ConnectionId) }`; `NetEvent{ Type{Connected,Disconnected,Message}, connection, channel, data }`; `make_loopback_pair() -> pair<shared_ptr<Transport>, shared_ptr<Transport>>` (server id 1 / client id 2).

---

### Task 1: Message envelope

**Files:**
- Create: `engine/net/message.hpp`
- Create: `tests/unit/net/message_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `enum class mosaic::engine::net::MessageType : std::uint8_t { Hello = 1, Welcome = 2 };`
  - `std::string encode(MessageType, const std::string& payload);`
  - `bool decode(const std::string& frame, MessageType& type, std::string& payload);`
  - `std::string put_u32(std::uint32_t);` / `std::uint32_t get_u32(const std::string&, std::size_t off = 0);`

- [ ] **Step 1: Write the failing test**

Create `tests/unit/net/message_test.cpp`:

```cpp
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "engine/net/message.hpp"

using mosaic::engine::net::decode;
using mosaic::engine::net::encode;
using mosaic::engine::net::get_u32;
using mosaic::engine::net::MessageType;
using mosaic::engine::net::put_u32;

TEST_CASE("encode/decode round-trips Hello and Welcome", "[net]")
{
    MessageType t{};
    std::string payload;

    REQUIRE(decode(encode(MessageType::Hello, ""), t, payload));
    REQUIRE(t == MessageType::Hello);
    REQUIRE(payload.empty());

    REQUIRE(decode(encode(MessageType::Welcome, put_u32(7u)), t, payload));
    REQUIRE(t == MessageType::Welcome);
    REQUIRE(get_u32(payload) == 7u);
}

TEST_CASE("decode rejects empty and unknown-type frames", "[net]")
{
    MessageType t{};
    std::string payload;
    REQUIRE_FALSE(decode("", t, payload));
    REQUIRE_FALSE(decode(std::string(1, static_cast<char>(0x99)), t, payload)); // unknown type
}

TEST_CASE("put_u32/get_u32 round-trip (little-endian)", "[net]")
{
    REQUIRE(get_u32(put_u32(0xDEADBEEFu)) == 0xDEADBEEFu);
    REQUIRE(get_u32(put_u32(0u)) == 0u);
    REQUIRE(get_u32("abc") == 0u); // too short -> 0, no crash
}
```

Add to `tests/CMakeLists.txt` (next to `unit/net/loopback_transport_test.cpp`):

```cmake
  unit/net/message_test.cpp
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -20`
Expected: FAIL — `engine/net/message.hpp` not found.

- [ ] **Step 3: Write the implementation**

Create `engine/net/message.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <string>

namespace mosaic::engine::net
{
enum class MessageType : std::uint8_t
{
    Hello   = 1, // client -> server on connect (payload: empty)
    Welcome = 2, // server -> client (payload: u32 assigned session id)
};

// Frame = [1 byte type][payload bytes].
inline std::string encode(MessageType type, const std::string& payload)
{
    std::string out;
    out.push_back(static_cast<char>(static_cast<std::uint8_t>(type)));
    out += payload;
    return out;
}

// Decode a frame. false on an empty buffer or an unknown type.
inline bool decode(const std::string& frame, MessageType& type, std::string& payload)
{
    if (frame.empty()) { return false; }
    const auto raw = static_cast<std::uint8_t>(frame[0]);
    if (raw != static_cast<std::uint8_t>(MessageType::Hello) &&
        raw != static_cast<std::uint8_t>(MessageType::Welcome))
    {
        return false;
    }
    type = static_cast<MessageType>(raw);
    payload = frame.substr(1);
    return true;
}

inline std::string put_u32(std::uint32_t v)
{
    std::string s;
    s.push_back(static_cast<char>(v & 0xFF));
    s.push_back(static_cast<char>((v >> 8) & 0xFF));
    s.push_back(static_cast<char>((v >> 16) & 0xFF));
    s.push_back(static_cast<char>((v >> 24) & 0xFF));
    return s;
}
inline std::uint32_t get_u32(const std::string& s, std::size_t off = 0)
{
    if (off + 4 > s.size()) { return 0; }
    return static_cast<std::uint32_t>(static_cast<unsigned char>(s[off])) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(s[off + 1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(s[off + 2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(s[off + 3])) << 24);
}
} // namespace mosaic::engine::net
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -5 && ./build/debug/tests/mosaic_unit_tests "[net]" 2>&1 | tail -6`
Expected: the message `[net]` cases (plus the existing N1 loopback ones) PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/net/message.hpp tests/unit/net/message_test.cpp tests/CMakeLists.txt
git commit -m "feat(net): message envelope (Hello/Welcome frame + u32 helpers)"
```

---

### Task 2: `NetworkEvent` + `NetworkSystem`

**Files:**
- Create: `engine/net/network_event.hpp`
- Create: `engine/net/network_system.hpp`
- Create: `tests/unit/net/network_system_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Transport`/`NetEvent`/`Channel`/`ConnectionId` (N1), `make_loopback_pair` (N1), `MessageType`/`encode`/`decode`/`put_u32`/`get_u32` (Task 1), `World`/`ecs::System`.
- Produces:
  - `struct mosaic::engine::net::NetworkEvent { enum class Type { PeerJoined, PeerLeft, SessionEstablished, Disconnected } type; std::uint32_t peer; };`
  - `enum class mosaic::engine::net::Role { Server, Client };`
  - `class mosaic::engine::net::NetworkSystem : public ecs::System` with ctor `(Role, std::shared_ptr<Transport>)`, `update(World&, float)`, and accessors `role()`, `is_connected()`, `local_id()`, `clients()`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/net/network_system_test.cpp`:

```cpp
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/net/loopback_transport.hpp"
#include "engine/net/message.hpp"
#include "engine/net/network_event.hpp"
#include "engine/net/network_system.hpp"
#include "engine/net/transport.hpp"
#include "engine/world/world.hpp"

using mosaic::engine::World;
using mosaic::engine::net::Channel;
using mosaic::engine::net::ConnectionId;
using mosaic::engine::net::encode;
using mosaic::engine::net::get_u32;
using mosaic::engine::net::make_loopback_pair;
using mosaic::engine::net::MessageType;
using mosaic::engine::net::NetEvent;
using mosaic::engine::net::NetworkEvent;
using mosaic::engine::net::NetworkSystem;
using mosaic::engine::net::Role;
using mosaic::engine::net::Transport;

namespace
{
// A test-only Transport: poll() returns injected events; send()/disconnect() are captured.
class FakeTransport : public Transport
{
public:
    std::vector<NetEvent> inbox;
    struct Sent { ConnectionId to; Channel channel; std::string bytes; };
    std::vector<Sent> sent;
    std::vector<ConnectionId> disconnected;

    void send(ConnectionId to, Channel ch, const std::string& b) override
    { sent.push_back({to, ch, b}); }
    std::vector<NetEvent> poll() override
    { std::vector<NetEvent> out; out.swap(inbox); return out; }
    void disconnect(ConnectionId w) override { disconnected.push_back(w); }

    // Helper: queue an incoming Message event.
    void inject_message(ConnectionId from, const std::string& frame)
    {
        NetEvent e; e.type = NetEvent::Type::Message; e.connection = from;
        e.channel = Channel::Reliable; e.data = frame; inbox.push_back(e);
    }
    void inject_disconnect(ConnectionId from)
    {
        NetEvent e; e.type = NetEvent::Type::Disconnected; e.connection = from;
        inbox.push_back(e);
    }
};
} // namespace

TEST_CASE("handshake completes end-to-end over the loopback", "[net]")
{
    auto [server_tx, client_tx] = make_loopback_pair();

    World server_world;
    auto& server = server_world.add_system<NetworkSystem>(Role::Server, server_tx);
    std::vector<NetworkEvent> server_events;
    server_world.events().subscribe<NetworkEvent>(
        [&](const NetworkEvent& e) { server_events.push_back(e); });

    World client_world;
    auto& client = client_world.add_system<NetworkSystem>(Role::Client, client_tx);
    std::vector<NetworkEvent> client_events;
    client_world.events().subscribe<NetworkEvent>(
        [&](const NetworkEvent& e) { client_events.push_back(e); });

    // Tick both a few times so Hello -> Welcome -> established completes.
    for (int i = 0; i < 4; ++i) { client_world.tick(0.016f); server_world.tick(0.016f); }

    REQUIRE(client.is_connected());
    REQUIRE(client.local_id() == 1u);
    REQUIRE(server.clients() == std::vector<std::uint32_t>{1u});

    bool server_joined = false, client_established = false;
    for (const auto& e : server_events)
        if (e.type == NetworkEvent::Type::PeerJoined && e.peer == 1u) server_joined = true;
    for (const auto& e : client_events)
        if (e.type == NetworkEvent::Type::SessionEstablished && e.peer == 1u) client_established = true;
    REQUIRE(server_joined);
    REQUIRE(client_established);
}

TEST_CASE("server assigns monotonic ids and replies Welcome per client", "[net]")
{
    auto fake = std::make_shared<FakeTransport>();
    World w;
    auto& server = w.add_system<NetworkSystem>(Role::Server, fake);

    fake->inject_message(10u, encode(MessageType::Hello, ""));
    fake->inject_message(11u, encode(MessageType::Hello, ""));
    w.tick(0.016f);

    REQUIRE(server.clients() == std::vector<std::uint32_t>{1u, 2u});
    REQUIRE(fake->sent.size() == 2);
    // Welcome(1) -> conn 10, Welcome(2) -> conn 11, both Reliable.
    REQUIRE(fake->sent[0].to == 10u);
    REQUIRE(fake->sent[0].channel == Channel::Reliable);
    MessageType t{}; std::string p;
    REQUIRE(mosaic::engine::net::decode(fake->sent[0].bytes, t, p));
    REQUIRE(t == MessageType::Welcome);
    REQUIRE(get_u32(p) == 1u);
    REQUIRE(fake->sent[1].to == 11u);
    REQUIRE(mosaic::engine::net::decode(fake->sent[1].bytes, t, p));
    REQUIRE(get_u32(p) == 2u);
}

TEST_CASE("server ignores a duplicate Hello from an existing session", "[net]")
{
    auto fake = std::make_shared<FakeTransport>();
    World w;
    auto& server = w.add_system<NetworkSystem>(Role::Server, fake);

    fake->inject_message(10u, encode(MessageType::Hello, ""));
    w.tick(0.016f);
    fake->inject_message(10u, encode(MessageType::Hello, "")); // duplicate
    w.tick(0.016f);

    REQUIRE(server.clients() == std::vector<std::uint32_t>{1u}); // not double-added
    REQUIRE(fake->sent.size() == 1);                             // no second Welcome
}

TEST_CASE("server drops a session on disconnect and emits PeerLeft", "[net]")
{
    auto fake = std::make_shared<FakeTransport>();
    World w;
    auto& server = w.add_system<NetworkSystem>(Role::Server, fake);
    std::vector<NetworkEvent> events;
    w.events().subscribe<NetworkEvent>([&](const NetworkEvent& e) { events.push_back(e); });

    fake->inject_message(10u, encode(MessageType::Hello, ""));
    w.tick(0.016f);
    fake->inject_disconnect(10u);
    w.tick(0.016f);

    REQUIRE(server.clients().empty());
    bool left = false;
    for (const auto& e : events)
        if (e.type == NetworkEvent::Type::PeerLeft && e.peer == 1u) left = true;
    REQUIRE(left);
}

TEST_CASE("no traffic emits no events", "[net]")
{
    auto fake = std::make_shared<FakeTransport>();
    World w;
    w.add_system<NetworkSystem>(Role::Server, fake);
    std::vector<NetworkEvent> events;
    w.events().subscribe<NetworkEvent>([&](const NetworkEvent& e) { events.push_back(e); });
    w.tick(0.016f);
    REQUIRE(events.empty());
}
```

Add to `tests/CMakeLists.txt`:

```cmake
  unit/net/network_system_test.cpp
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -20`
Expected: FAIL — `engine/net/network_event.hpp` / `network_system.hpp` not found.

- [ ] **Step 3: Write the implementation**

Create `engine/net/network_event.hpp`:

```cpp
#pragma once

#include <cstdint>

namespace mosaic::engine::net
{
// Emitted on the World event bus by NetworkSystem. Server: PeerJoined/PeerLeft carry a
// client session id. Client: SessionEstablished carries its own assigned id; Disconnected
// on link loss.
struct NetworkEvent
{
    enum class Type { PeerJoined, PeerLeft, SessionEstablished, Disconnected };
    Type          type = Type::PeerJoined;
    std::uint32_t peer = 0;
};
} // namespace mosaic::engine::net
```

Create `engine/net/network_system.hpp`:

```cpp
#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine/ecs/system.hpp"
#include "engine/net/message.hpp"
#include "engine/net/network_event.hpp"
#include "engine/net/transport.hpp"
#include "engine/world/world.hpp"

namespace mosaic::engine::net
{
enum class Role { Server, Client };

// Drives the connection lifecycle over an injected Transport. Server: assigns a monotonic
// session id per client on Hello and replies Welcome; Client: sends Hello on connect and
// adopts the id from Welcome. Emits NetworkEvent on the World bus. Writes no ECS state.
class NetworkSystem : public mosaic::engine::ecs::System
{
public:
    NetworkSystem(Role role, std::shared_ptr<Transport> transport)
        : role_(role), transport_(std::move(transport)) {}

    void update(mosaic::engine::World& world, float /*dt*/) override
    {
        if (!transport_) { return; }
        for (const NetEvent& ev : transport_->poll())
        {
            if (role_ == Role::Server) { handle_server(world, ev); }
            else                       { handle_client(world, ev); }
        }
    }

    Role role() const { return role_; }
    bool is_connected() const { return connected_; }          // client
    std::uint32_t local_id() const { return local_id_; }      // client
    const std::vector<std::uint32_t>& clients() const { return clients_; } // server

private:
    void handle_server(mosaic::engine::World& world, const NetEvent& ev)
    {
        if (ev.type == NetEvent::Type::Message)
        {
            MessageType type{}; std::string payload;
            if (!decode(ev.data, type, payload) || type != MessageType::Hello) { return; }
            if (sessions_.count(ev.connection) != 0) { return; } // duplicate Hello -> ignore
            const std::uint32_t id = next_session_id_++;
            sessions_.emplace(ev.connection, id);
            clients_.push_back(id);
            transport_->send(ev.connection, Channel::Reliable, encode(MessageType::Welcome, put_u32(id)));
            world.events().emit(NetworkEvent{NetworkEvent::Type::PeerJoined, id});
        }
        else if (ev.type == NetEvent::Type::Disconnected)
        {
            auto it = sessions_.find(ev.connection);
            if (it == sessions_.end()) { return; }
            const std::uint32_t id = it->second;
            sessions_.erase(it);
            clients_.erase(std::remove(clients_.begin(), clients_.end(), id), clients_.end());
            world.events().emit(NetworkEvent{NetworkEvent::Type::PeerLeft, id});
        }
        // Connected on the server is a no-op: the session is created on Hello.
    }

    void handle_client(mosaic::engine::World& world, const NetEvent& ev)
    {
        if (ev.type == NetEvent::Type::Connected)
        {
            transport_->send(ev.connection, Channel::Reliable, encode(MessageType::Hello, ""));
        }
        else if (ev.type == NetEvent::Type::Message)
        {
            MessageType type{}; std::string payload;
            if (!decode(ev.data, type, payload) || type != MessageType::Welcome) { return; }
            local_id_ = get_u32(payload);
            connected_ = true;
            world.events().emit(NetworkEvent{NetworkEvent::Type::SessionEstablished, local_id_});
        }
        else if (ev.type == NetEvent::Type::Disconnected)
        {
            connected_ = false;
            world.events().emit(NetworkEvent{NetworkEvent::Type::Disconnected, local_id_});
        }
    }

    Role role_;
    std::shared_ptr<Transport> transport_;
    // server
    std::unordered_map<ConnectionId, std::uint32_t> sessions_;
    std::vector<std::uint32_t> clients_;
    std::uint32_t next_session_id_ = 1;
    // client
    bool connected_ = false;
    std::uint32_t local_id_ = 0;
};
} // namespace mosaic::engine::net
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -5 && ./build/debug/tests/mosaic_unit_tests "[net]" 2>&1 | tail -8`
Expected: all `[net]` cases PASS (N1 loopback + Task 1 message + these 5 session cases).

- [ ] **Step 5: Commit**

```bash
git add engine/net/network_event.hpp engine/net/network_system.hpp \
        tests/unit/net/network_system_test.cpp tests/CMakeLists.txt
git commit -m "feat(net): NetworkSystem session lifecycle + Hello/Welcome handshake (networking N2)"
```

---

### Task 3: Handoff update

**Files:**
- Modify: `docs/project/next-steps.md`

- [ ] **Step 1: Update the handoff**

In `docs/project/next-steps.md`:
1. Update the **Last updated** line (date `2026-09-09`) with a "Networking N2 (session lifecycle + NetworkSystem) DONE" summary: a message envelope (`engine/net/message.hpp`: type byte + payload, Hello/Welcome, u32 helpers), a `NetworkEvent` bus event (`network_event.hpp`), and a `NetworkSystem` (`network_system.hpp`, an `ecs::System` with an injected `Transport` + `Role`) that runs the Hello/Welcome handshake, assigns a **monotonic server-side session id** (decoupled from the transport ConnectionId), tracks `clients()`/`is_connected()`/`local_id()`, and emits `PeerJoined`/`PeerLeft`/`SessionEstablished`/`Disconnected` on the World bus. Headless-tested (message round-trip; end-to-end handshake over the N1 loopback with two Worlds; server multi-client + duplicate-Hello idempotence + disconnect via an in-test `FakeTransport`). Pure engine, no sockets/CLI. Networking decomposition: N1 done → **N2 done** → N3 state replication (reflection→snapshots keyed by session id) → N4 spawn/despawn + net-id ↔ EntityID map → N5 client input; the real ENet backend + `mosaic run --host`/`--connect` is a later maintainer-verified slice.
2. Link the N2 spec + this plan.

- [ ] **Step 2: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "docs: mark networking N2 (session + NetworkSystem) done; N3 next"
```

---

## Self-Review

**Spec coverage:**
- `message.hpp` envelope (`MessageType`, `encode`/`decode`, `put_u32`/`get_u32`) → Task 1. ✓
- `network_event.hpp` `NetworkEvent` → Task 2. ✓
- `network_system.hpp` `NetworkSystem` (Role, injected Transport, Hello/Welcome handshake, monotonic session id, `is_connected`/`local_id`/`clients`, bus events) → Task 2. ✓
- Tests: message round-trip + reject (Task 1); end-to-end loopback handshake, server multi-client monotonic ids + Welcome capture, duplicate-Hello idempotence, disconnect + PeerLeft, no-traffic-no-events via `FakeTransport` (Task 2). ✓
- No `setup_runtime`/CLI/real-backend/demo → documented non-goals, no task. ✓
- Handoff → Task 3. ✓

**Placeholder scan:** no TBD/TODO; every code step has full real code (envelope, event, system, and all tests incl. the `FakeTransport`). The one note ("add `<algorithm>`") is folded into the implementation instruction.

**Type consistency:** `MessageType{Hello=1,Welcome=2}`; `encode(MessageType,const std::string&)->string` / `decode(const string&,MessageType&,string&)->bool` / `put_u32`/`get_u32`; `NetworkEvent{Type{PeerJoined,PeerLeft,SessionEstablished,Disconnected}, peer}`; `Role{Server,Client}`; `NetworkSystem(Role, shared_ptr<Transport>)` + `role()/is_connected()/local_id()/clients()`. Server emits `PeerJoined/PeerLeft` with the session id; client emits `SessionEstablished(local_id)`/`Disconnected`. Matches the N1 `Transport`/`NetEvent` seam (poll/send/disconnect, Connected/Disconnected/Message) exactly. Consistent across message/event/system/tests.