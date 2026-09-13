# Networking N5 (client → server input) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a client send opaque per-tick input payloads to the server, surfaced there as a `NetworkInput{session, payload}` bus event — no gameplay semantics baked into the engine.

**Architecture:** A new `MessageType::Input` frame; `NetworkSystem` grows a client-side `send_to_server`; a new `InputSystem` (client role: poll a provider, send each payload; server role: drain inbound `Input` frames, emit `NetworkInput` on the World bus). Mirrors the existing `ReplicationSystem`/`NetworkSystem` pairing and `UiSystem`'s injected-provider pattern.

**Tech Stack:** C++17, Catch2, the existing `engine/net` loopback transport for headless end-to-end tests. No new external dependencies.

**Spec:** `docs/superpowers/specs/2026-09-12-net-n5-client-input-design.md`

## Global Constraints

- No new message wire format beyond the existing `[1 byte type][payload]` frame (`engine/net/message.hpp`).
- The engine never decodes or interprets the input payload — it stays an opaque `std::string` end to end.
- Everything must be headless-testable: no sockets, no GLFW — tests use `make_loopback_pair()` and fake providers, matching every existing `engine/net` test.
- Follow existing code style exactly: `mosaic::engine::net` namespace, `#pragma once`, Catch2 `TEST_CASE`/`SECTION` with `"[net]"` tags (add `"[input]"` too where relevant), `using` declarations at test-file top rather than fully qualified names inline.

---

### Task 1: `MessageType::Input`

**Files:**
- Modify: `engine/net/message.hpp`
- Test: `tests/unit/net/message_test.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces: `mosaic::engine::net::MessageType::Input` (value `4`), accepted by `decode`. Later tasks encode/decode input frames with this.

- [ ] **Step 1: Write the failing test**

Look at the existing round-trip test(s) in `tests/unit/net/message_test.cpp` for the pattern (encode then decode, and the "unknown type is rejected" case), then add:

```cpp
TEST_CASE("Input frame round-trips through encode/decode", "[net]")
{
    const std::string frame = encode(MessageType::Input, "move:1,0");
    MessageType type{};
    std::string payload;
    REQUIRE(decode(frame, type, payload));
    REQUIRE(type == MessageType::Input);
    REQUIRE(payload == "move:1,0");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build/debug --target mosaic_unit_tests -j4 && ./build/debug/tests/mosaic_unit_tests "[net]"`
Expected: build failure — `MessageType::Input` does not exist yet.

- [ ] **Step 3: Add `Input` to `MessageType` and `decode`'s validation**

In `engine/net/message.hpp`, extend the enum:

```cpp
enum class MessageType : std::uint8_t
{
    Hello    = 1, // client -> server on connect (payload: empty)
    Welcome  = 2, // server -> client (payload: u32 assigned session id)
    Snapshot = 3, // server -> client (payload: JSON replication snapshot from N3)
    Input    = 4, // client -> server (payload: opaque, app-defined bytes; N5)
};
```

And extend `decode`'s validation check:

```cpp
inline bool decode(const std::string& frame, MessageType& type, std::string& payload)
{
    if (frame.empty()) { return false; }
    const auto raw = static_cast<std::uint8_t>(frame[0]);
    if (raw != static_cast<std::uint8_t>(MessageType::Hello) &&
        raw != static_cast<std::uint8_t>(MessageType::Welcome) &&
        raw != static_cast<std::uint8_t>(MessageType::Snapshot) &&
        raw != static_cast<std::uint8_t>(MessageType::Input))
    {
        return false;
    }
    type = static_cast<MessageType>(raw);
    payload = frame.substr(1);
    return true;
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build/debug --target mosaic_unit_tests -j4 && ./build/debug/tests/mosaic_unit_tests "[net]"`
Expected: PASS, including the new case and the pre-existing "unknown type rejected" case (still rejects e.g. byte `5`).

- [ ] **Step 5: Commit**

```bash
git add engine/net/message.hpp tests/unit/net/message_test.cpp
git commit -m "feat(net): add Input message type (networking N5)"
```

---

### Task 2: `NetworkSystem::send_to_server`

**Files:**
- Modify: `engine/net/network_system.hpp`
- Test: `tests/unit/net/network_system_test.cpp`

**Interfaces:**
- Consumes: `MessageType::Input` from Task 1 is not required here (this task only adds the send primitive; it works with any `Channel`/bytes).
- Produces: `NetworkSystem::send_to_server(Channel channel, const std::string& bytes)` — client role, no-op before a `Connected` event has been observed. `InputSystem` (Task 3) calls this.

- [ ] **Step 1: Write the failing tests**

Add to `tests/unit/net/network_system_test.cpp`, alongside the existing `FakeTransport`-based cases (reuse the `FakeTransport` class already defined in that file):

```cpp
TEST_CASE("client send_to_server sends to the server connection once connected", "[net]")
{
    auto transport = std::make_shared<FakeTransport>();
    World world;
    auto& client = world.add_system<NetworkSystem>(Role::Client, transport);

    NetEvent connected;
    connected.type = NetEvent::Type::Connected;
    connected.connection = 7;
    transport->inbox.push_back(connected);
    world.tick(0.016f); // processes Connected -> stores server_conn_, sends Hello

    client.send_to_server(Channel::Reliable, encode(MessageType::Input, "move:1,0"));

    REQUIRE(transport->sent.size() == 2u); // Hello (from Connected handling) + our Input send
    const auto& last = transport->sent.back();
    REQUIRE(last.to == 7u);
    REQUIRE(last.channel == Channel::Reliable);
    MessageType type{};
    std::string payload;
    REQUIRE(decode(last.bytes, type, payload));
    REQUIRE(type == MessageType::Input);
    REQUIRE(payload == "move:1,0");
}

TEST_CASE("client send_to_server before Connected is a no-op", "[net]")
{
    auto transport = std::make_shared<FakeTransport>();
    World world;
    auto& client = world.add_system<NetworkSystem>(Role::Client, transport);

    client.send_to_server(Channel::Reliable, encode(MessageType::Input, "x"));

    REQUIRE(transport->sent.empty());
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build/debug --target mosaic_unit_tests -j4 && ./build/debug/tests/mosaic_unit_tests "[net]"`
Expected: build failure — `send_to_server` does not exist yet.

- [ ] **Step 3: Implement `send_to_server`**

In `engine/net/network_system.hpp`, add a private member next to the other client-side state:

```cpp
    // client
    bool connected_ = false;
    std::uint32_t local_id_ = 0;
    ConnectionId server_conn_ = 0; // transport link to the server, set on Connected
```

Add the public method next to `broadcast`:

```cpp
    // Client: send `bytes` to the server. No-op if not yet connected at the transport level
    // (no Connected event observed) — distinct from is_connected(), which tracks the
    // post-Welcome session.
    void send_to_server(Channel channel, const std::string& bytes)
    {
        if (!transport_ || server_conn_ == 0) { return; }
        transport_->send(server_conn_, channel, bytes);
    }
```

And set `server_conn_` in `handle_client`'s `Connected` branch:

```cpp
    void handle_client(mosaic::engine::World& world, const NetEvent& ev)
    {
        if (ev.type == NetEvent::Type::Connected)
        {
            server_conn_ = ev.connection;
            transport_->send(ev.connection, Channel::Reliable, encode(MessageType::Hello, ""));
        }
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build/debug --target mosaic_unit_tests -j4 && ./build/debug/tests/mosaic_unit_tests "[net]"`
Expected: PASS, all `[net]` cases including the two new ones and all pre-existing handshake/replication cases (unaffected).

- [ ] **Step 5: Commit**

```bash
git add engine/net/network_system.hpp tests/unit/net/network_system_test.cpp
git commit -m "feat(net): NetworkSystem::send_to_server (networking N5)"
```

---

### Task 3: `NetworkInput` event + `InputSystem`

**Files:**
- Modify: `engine/net/network_event.hpp`
- Create: `engine/net/input_system.hpp`
- Create: `tests/unit/net/input_system_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `MessageType::Input` (Task 1), `NetworkSystem::send_to_server`, `NetworkSystem::take_inbound()`, `InboundMessage{peer, type, payload}` (all pre-existing/Task 2).
- Produces: `mosaic::engine::net::NetworkInput{std::uint32_t session, std::string payload}` (bus event); `mosaic::engine::net::InputSystem(Role role, NetworkSystem* net, std::function<std::vector<std::string>()> provider = nullptr)`. Nothing later in this plan consumes these, but game/script code will.

- [ ] **Step 1: Add the `NetworkInput` struct**

In `engine/net/network_event.hpp`, add below the existing `NetworkEvent` struct (needs `#include <string>`):

```cpp
#pragma once

#include <cstdint>
#include <string>

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

// Emitted on the World event bus by InputSystem (server role) for each received client
// input frame. Carries the sender's session id and the opaque payload — the engine does not
// interpret it; game/script logic subscribes and decodes it.
struct NetworkInput
{
    std::uint32_t session = 0;
    std::string   payload;
};
} // namespace mosaic::engine::net
```

- [ ] **Step 2: Write `engine/net/input_system.hpp`**

```cpp
#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "engine/ecs/system.hpp"
#include "engine/net/message.hpp"
#include "engine/net/network_event.hpp"
#include "engine/net/network_system.hpp"
#include "engine/world/world.hpp"

namespace mosaic::engine::net
{
// Client role: polls an injected provider each tick and sends each returned payload to the
// server as an Input frame. Server role: drains inbound Input frames and emits NetworkInput
// on the bus. Added to the World AFTER NetworkSystem so inbound frames are available each
// tick (server role) and the transport link to the server is established (client role).
class InputSystem : public mosaic::engine::ecs::System
{
public:
    InputSystem(Role role, NetworkSystem* net,
               std::function<std::vector<std::string>()> provider = nullptr)
        : role_(role), net_(net), provider_(std::move(provider)) {}

    void update(mosaic::engine::World& world, float /*dt*/) override
    {
        if (net_ == nullptr) { return; }
        if (role_ == Role::Client)
        {
            if (!provider_) { return; }
            for (const std::string& payload : provider_())
            {
                net_->send_to_server(Channel::Reliable, encode(MessageType::Input, payload));
            }
        }
        else
        {
            for (const InboundMessage& msg : net_->take_inbound())
            {
                if (msg.type == MessageType::Input)
                {
                    world.events().emit(NetworkInput{msg.peer, msg.payload});
                }
            }
        }
    }

private:
    Role role_;
    NetworkSystem* net_;
    std::function<std::vector<std::string>()> provider_;
};
} // namespace mosaic::engine::net
```

- [ ] **Step 3: Register the new test file**

In `tests/CMakeLists.txt`, add `unit/net/input_system_test.cpp` to the same list as
`unit/net/network_system_test.cpp` (alphabetically, right after it):

```
  unit/net/loopback_transport_test.cpp
  unit/net/message_test.cpp
  unit/net/network_system_test.cpp
  unit/net/input_system_test.cpp
  unit/net/net_replicated_test.cpp
  unit/net/replication_test.cpp
  unit/net/replication_system_test.cpp
```

(Match whatever ordering convention the file already uses — check it; alphabetical by
filename is fine either way as long as it's added to the same source list.)

- [ ] **Step 4: Write the failing tests**

Create `tests/unit/net/input_system_test.cpp`:

```cpp
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/net/input_system.hpp"
#include "engine/net/loopback_transport.hpp"
#include "engine/net/message.hpp"
#include "engine/net/network_event.hpp"
#include "engine/net/network_system.hpp"
#include "engine/net/transport.hpp"
#include "engine/world/world.hpp"

using mosaic::engine::World;
using mosaic::engine::net::Channel;
using mosaic::engine::net::ConnectionId;
using mosaic::engine::net::decode;
using mosaic::engine::net::encode;
using mosaic::engine::net::InputSystem;
using mosaic::engine::net::make_loopback_pair;
using mosaic::engine::net::MessageType;
using mosaic::engine::net::NetEvent;
using mosaic::engine::net::NetworkInput;
using mosaic::engine::net::NetworkSystem;
using mosaic::engine::net::Role;
using mosaic::engine::net::Transport;

namespace
{
// Mirrors the FakeTransport in network_system_test.cpp (kept local — no shared test header
// exists yet for this small helper).
class FakeTransport : public Transport
{
public:
    std::vector<NetEvent> inbox;
    struct Sent { ConnectionId to; Channel channel; std::string bytes; };
    std::vector<Sent> sent;

    void send(ConnectionId to, Channel ch, const std::string& b) override
    { sent.push_back({to, ch, b}); }
    std::vector<NetEvent> poll() override
    { std::vector<NetEvent> out; out.swap(inbox); return out; }
    void disconnect(ConnectionId) override {}
};
} // namespace

TEST_CASE("client InputSystem sends each provided payload as an Input frame", "[net][input]")
{
    auto transport = std::make_shared<FakeTransport>();
    World world;
    auto& net = world.add_system<NetworkSystem>(Role::Client, transport);
    world.add_system<InputSystem>(Role::Client, &net,
                                  []() { return std::vector<std::string>{"a", "b"}; });

    NetEvent connected;
    connected.type = NetEvent::Type::Connected;
    connected.connection = 9;
    transport->inbox.push_back(connected);

    world.tick(0.016f); // NetworkSystem handles Connected (sends Hello) + InputSystem sends a,b

    REQUIRE(transport->sent.size() == 3u); // Hello + "a" + "b"
    for (std::size_t i = 1; i < transport->sent.size(); ++i)
    {
        REQUIRE(transport->sent[i].to == 9u);
    }
    MessageType type{};
    std::string payload;
    REQUIRE(decode(transport->sent[1].bytes, type, payload));
    REQUIRE(type == MessageType::Input);
    REQUIRE(payload == "a");
    REQUIRE(decode(transport->sent[2].bytes, type, payload));
    REQUIRE(payload == "b");
}

TEST_CASE("client InputSystem with an empty provider result sends nothing", "[net][input]")
{
    auto transport = std::make_shared<FakeTransport>();
    World world;
    auto& net = world.add_system<NetworkSystem>(Role::Client, transport);
    world.add_system<InputSystem>(Role::Client, &net,
                                  []() { return std::vector<std::string>{}; });

    world.tick(0.016f);

    REQUIRE(transport->sent.empty());
}

TEST_CASE("server InputSystem emits NetworkInput for a received Input frame", "[net][input]")
{
    auto transport = std::make_shared<FakeTransport>();
    World world;
    auto& net = world.add_system<NetworkSystem>(Role::Server, transport);
    world.add_system<InputSystem>(Role::Server, &net);

    std::vector<NetworkInput> received;
    world.events().subscribe<NetworkInput>(
        [&](const NetworkInput& e) { received.push_back(e); });

    // Establish a session first (server assigns id 1 to connection 5 on Hello).
    NetEvent hello;
    hello.type = NetEvent::Type::Message;
    hello.connection = 5;
    hello.data = encode(MessageType::Hello, "");
    transport->inbox.push_back(hello);
    world.tick(0.016f);

    NetEvent input;
    input.type = NetEvent::Type::Message;
    input.connection = 5;
    input.data = encode(MessageType::Input, "move:1,0");
    transport->inbox.push_back(input);
    world.tick(0.016f);

    REQUIRE(received.size() == 1u);
    REQUIRE(received[0].session == 1u);
    REQUIRE(received[0].payload == "move:1,0");
}

TEST_CASE("input flows client to server over the loopback", "[net][input]")
{
    auto [server_tx, client_tx] = make_loopback_pair();

    World server_world;
    auto& server_net = server_world.add_system<NetworkSystem>(Role::Server, server_tx);
    server_world.add_system<InputSystem>(Role::Server, &server_net);
    std::vector<NetworkInput> received;
    server_world.events().subscribe<NetworkInput>(
        [&](const NetworkInput& e) { received.push_back(e); });

    World client_world;
    auto& client_net = client_world.add_system<NetworkSystem>(Role::Client, client_tx);
    int calls = 0;
    client_world.add_system<InputSystem>(Role::Client, &client_net,
        [&]() -> std::vector<std::string>
        {
            ++calls;
            return calls == 1 ? std::vector<std::string>{"move:1,0"}
                              : std::vector<std::string>{};
        });

    for (int i = 0; i < 4; ++i) { client_world.tick(0.016f); server_world.tick(0.016f); }

    REQUIRE(client_net.is_connected());
    REQUIRE(received.size() == 1u);
    REQUIRE(received[0].session == client_net.local_id());
    REQUIRE(received[0].payload == "move:1,0");
}
```

- [ ] **Step 5: Run tests to verify they fail**

Run: `cmake --build build/debug --target mosaic_unit_tests -j4 && ./build/debug/tests/mosaic_unit_tests "[input]"`
Expected: build failure — `input_system.hpp` / `InputSystem` / `NetworkInput` don't exist, and the CMake target won't include the new file until Step 3 above is applied (do Steps 1-3 before building).

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build build/debug --target mosaic_unit_tests -j4 && ./build/debug/tests/mosaic_unit_tests "[input]"`
Expected: PASS, all 4 new `[input]` cases.

Then run the full `[net]` tag to confirm no regressions:

Run: `./build/debug/tests/mosaic_unit_tests "[net]"`
Expected: PASS, every existing net/replication case plus the new ones.

- [ ] **Step 7: Commit**

```bash
git add engine/net/network_event.hpp engine/net/input_system.hpp tests/unit/net/input_system_test.cpp tests/CMakeLists.txt
git commit -m "feat(net): InputSystem — client send, server NetworkInput bus event (networking N5)"
```

---

### Task 4: Update project docs

**Files:**
- Modify: `docs/project/next-steps.md`

**Interfaces:** none (docs only).

- [ ] **Step 1: Update `docs/project/next-steps.md`**

Replace the "Last updated" line and status paragraph to mark N5 done, in the same style as
the existing N4 entry (summarize what was built, what's proven, current test tag counts —
run `./build/debug/tests/mosaic_unit_tests "[net]" --list-tests | wc -l` and similar to get
accurate counts rather than guessing). Carry forward, unchanged, the still-outstanding
follow-ups list:

- **N4b (still DEFERRED):** server-side `NetId` assignment policy.
- Real ENet UDP backend + `mosaic run --host`/`--connect` + `setup_runtime`/demo.

Add: applying `NetworkInput` in game/script logic (movement, actions) is an explicit
non-goal of N5 and remains open for whoever builds the first networked gameplay demo.

- [ ] **Step 2: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "docs(net): mark networking N5 (client input) done; N4b still deferred"
```
