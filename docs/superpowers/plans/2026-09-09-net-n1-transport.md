# Networking N1 — neutral Transport + in-memory loopback Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A neutral `Transport` interface plus an in-memory loopback that links a server + client pair in-process, proven headless.

**Architecture:** `engine/net/transport.hpp` defines the backend-free seam (`send`/`poll`/`disconnect`, `NetEvent`, `Channel`, `ConnectionId`). `engine/net/loopback_transport.hpp` implements it with two linked in-process endpoints sharing per-direction queues (`send` enqueues to the peer's inbound; `poll` drains this endpoint's inbound). Header-only, no external dependency.

**Tech Stack:** C++20, Catch2, CMake.

**Spec:** `docs/superpowers/specs/2026-09-09-net-n1-transport-design.md`

**Base branch:** `claude/net-n1-transport` (off `main`; `cmake --preset debug` configured).

## Global Constraints

- **Build convention:** CMake presets into `build/debug/`. Build: `cmake --build --preset debug --target mosaic_unit_tests`. Run tags: `./build/debug/tests/mosaic_unit_tests "[net]"`. Full suite: `ctest --preset debug`. Do **not** use `cmake --build build` or `./build/app/...`. **Transient link failures:** the ~200 MB test binary intermittently fails at LINK under memory pressure with no compiler error — `grep -iE "error:|error C[0-9]|undefined reference"`; if empty, re-run (up to ~3×); if it leaves a **0-byte** `build/debug/tests/mosaic_unit_tests`, `rm -f` it and rebuild.
- **Namespace:** `mosaic::engine::net`. Backend-free (no sockets, no external dep) — pure in-engine, header-only.
- **Ids (fixed for determinism):** the loopback pair uses stable ids — on the **client** endpoint, the peer (server) is `ConnectionId 1`; on the **server** endpoint, the peer (client) is `ConnectionId 2`. Each side's `Connected` event carries the peer's id.
- **Channels:** `Channel::Reliable` and `Channel::Unreliable`. The loopback delivers both losslessly, in order, and preserves the tag (no loss simulation).
- **Opaque payloads:** messages are `std::string` byte buffers; no framing/serialization in N1.

---

### Task 1: `Transport` interface + in-memory loopback

**Files:**
- Create: `engine/net/transport.hpp`
- Create: `engine/net/loopback_transport.hpp`
- Create: `tests/unit/net/loopback_transport_test.cpp`
- Modify: `tests/CMakeLists.txt` (add the test source)

**Interfaces:**
- Produces:
  - `enum class mosaic::engine::net::Channel { Reliable, Unreliable };`
  - `struct mosaic::engine::net::NetEvent { enum class Type { Connected, Disconnected, Message } type; ConnectionId connection; Channel channel; std::string data; };`
  - `class mosaic::engine::net::Transport { virtual void send(ConnectionId, Channel, const std::string&) = 0; virtual std::vector<NetEvent> poll() = 0; virtual void disconnect(ConnectionId) = 0; };`
  - `std::pair<std::shared_ptr<Transport>, std::shared_ptr<Transport>> mosaic::engine::net::make_loopback_pair();`

- [ ] **Step 1: Write the failing test**

Create `tests/unit/net/loopback_transport_test.cpp`:

```cpp
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/net/loopback_transport.hpp"
#include "engine/net/transport.hpp"

using mosaic::engine::net::Channel;
using mosaic::engine::net::make_loopback_pair;
using mosaic::engine::net::NetEvent;
using mosaic::engine::net::Transport;

TEST_CASE("loopback pair sees a Connected event on the first poll", "[net]")
{
    auto [server, client] = make_loopback_pair();

    const auto se = server->poll();
    REQUIRE(se.size() == 1);
    REQUIRE(se[0].type == NetEvent::Type::Connected);
    REQUIRE(se[0].connection == 2u); // client id, from the server's side

    const auto ce = client->poll();
    REQUIRE(ce.size() == 1);
    REQUIRE(ce[0].type == NetEvent::Type::Connected);
    REQUIRE(ce[0].connection == 1u); // server id, from the client's side

    REQUIRE(server->poll().empty()); // no repeat, no traffic
    REQUIRE(client->poll().empty());
}

TEST_CASE("loopback delivers a reliable message to the peer, not the sender", "[net]")
{
    auto [server, client] = make_loopback_pair();
    server->poll(); client->poll(); // drain the Connected events

    client->send(/*to server*/ 1u, Channel::Reliable, "ping");

    const auto se = server->poll();
    REQUIRE(se.size() == 1);
    REQUIRE(se[0].type == NetEvent::Type::Message);
    REQUIRE(se[0].connection == 2u);           // from the client
    REQUIRE(se[0].channel == Channel::Reliable);
    REQUIRE(se[0].data == "ping");

    REQUIRE(client->poll().empty());           // sender does not receive its own message

    server->send(/*to client*/ 2u, Channel::Reliable, "pong");
    const auto ce = client->poll();
    REQUIRE(ce.size() == 1);
    REQUIRE(ce[0].data == "pong");
    REQUIRE(ce[0].connection == 1u);           // from the server
}

TEST_CASE("loopback preserves the unreliable channel tag", "[net]")
{
    auto [server, client] = make_loopback_pair();
    server->poll(); client->poll();

    client->send(1u, Channel::Unreliable, "u");
    const auto se = server->poll();
    REQUIRE(se.size() == 1);
    REQUIRE(se[0].channel == Channel::Unreliable);
    REQUIRE(se[0].data == "u");
}

TEST_CASE("loopback drains messages FIFO", "[net]")
{
    auto [server, client] = make_loopback_pair();
    server->poll(); client->poll();

    client->send(1u, Channel::Reliable, "a");
    client->send(1u, Channel::Reliable, "b");
    client->send(1u, Channel::Reliable, "c");

    const auto se = server->poll();
    REQUIRE(se.size() == 3);
    REQUIRE(se[0].data == "a");
    REQUIRE(se[1].data == "b");
    REQUIRE(se[2].data == "c");
}

TEST_CASE("loopback disconnect surfaces on the peer and drops later sends", "[net]")
{
    auto [server, client] = make_loopback_pair();
    server->poll(); client->poll();

    client->disconnect(1u); // client disconnects from the server

    const auto se = server->poll();
    REQUIRE(se.size() == 1);
    REQUIRE(se[0].type == NetEvent::Type::Disconnected);
    REQUIRE(se[0].connection == 2u);

    // Sends after disconnect are silently dropped (no throw, no delivery).
    server->send(2u, Channel::Reliable, "late");
    REQUIRE(client->poll().empty());
}

TEST_CASE("loopback drops a send to an unknown connection", "[net]")
{
    auto [server, client] = make_loopback_pair();
    server->poll(); client->poll();

    client->send(/*not the server*/ 99u, Channel::Reliable, "nowhere");
    REQUIRE(server->poll().empty());
}
```

Add to `tests/CMakeLists.txt` (in the `add_executable(mosaic_unit_tests ...)` source list;
put it near the end with the other subsystem tests, e.g. right before the `unit/vfx/*`
block):

```cmake
  unit/net/loopback_transport_test.cpp
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -20`
Expected: FAIL — `engine/net/transport.hpp` / `engine/net/loopback_transport.hpp` not found.

- [ ] **Step 3: Write the implementation**

Create `engine/net/transport.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mosaic::engine::net
{
using ConnectionId = std::uint32_t; // opaque; 0 == invalid/none

// Reliable-ordered vs UDP-style unreliable. The loopback delivers both losslessly and in
// order (tag preserved, no loss simulation); the tag matters for a real backend.
enum class Channel { Reliable, Unreliable };

// One event drained from an endpoint's poll(). data/channel are meaningful only for
// Message; connection identifies the peer for all three types.
struct NetEvent
{
    enum class Type { Connected, Disconnected, Message };
    Type         type = Type::Message;
    ConnectionId connection = 0;
    Channel      channel = Channel::Reliable;
    std::string  data;
};

// One network endpoint. Backend-free: concrete transports (loopback here; a real UDP
// backend later) live behind this seam and are created via factory functions. poll() is
// the per-tick, non-blocking event drain.
class Transport
{
public:
    virtual ~Transport() = default;
    virtual void send(ConnectionId to, Channel channel, const std::string& bytes) = 0;
    virtual std::vector<NetEvent> poll() = 0;
    virtual void disconnect(ConnectionId who) = 0;
};
} // namespace mosaic::engine::net
```

Create `engine/net/loopback_transport.hpp`:

```cpp
#pragma once

#include <deque>
#include <memory>
#include <utility>

#include "engine/net/transport.hpp"

namespace mosaic::engine::net
{
namespace detail
{
// Shared state for one loopback pair. Two endpoints reference the same Link. Each
// endpoint drains its own inbound queue; sending pushes onto the peer's inbound.
struct LoopLink
{
    std::deque<NetEvent> to_server; // messages/events the SERVER will poll()
    std::deque<NetEvent> to_client; // messages/events the CLIENT will poll()
    bool                 server_connected_pending = true;
    bool                 client_connected_pending = true;
    bool                 closed = false;
};
} // namespace detail

// One endpoint of a loopback pair. `is_server_` picks which queue is "mine" (inbound) vs
// the peer's (outbound), and which stable ids apply (server sees client=2; client sees
// server=1).
class LoopbackTransport : public Transport
{
public:
    LoopbackTransport(std::shared_ptr<detail::LoopLink> link, bool is_server)
        : link_(std::move(link)), is_server_(is_server) {}

    // self id = the id the PEER uses to address me; peer id = the id I use to address them.
    ConnectionId self_id() const { return is_server_ ? 1u : 2u; }
    ConnectionId peer_id() const { return is_server_ ? 2u : 1u; }

    void send(ConnectionId to, Channel channel, const std::string& bytes) override
    {
        if (link_->closed || to != peer_id()) { return; } // closed / unknown -> drop
        NetEvent ev;
        ev.type = NetEvent::Type::Message;
        ev.connection = self_id(); // the peer sees the message as coming from me
        ev.channel = channel;
        ev.data = bytes;
        peer_inbound().push_back(std::move(ev));
    }

    std::vector<NetEvent> poll() override
    {
        std::vector<NetEvent> out;
        // Latch the once-only Connected first.
        bool& connected_pending = is_server_ ? link_->server_connected_pending
                                             : link_->client_connected_pending;
        if (connected_pending)
        {
            connected_pending = false;
            NetEvent ev; ev.type = NetEvent::Type::Connected; ev.connection = peer_id();
            out.push_back(std::move(ev));
        }
        std::deque<NetEvent>& in = my_inbound();
        while (!in.empty()) { out.push_back(std::move(in.front())); in.pop_front(); }
        return out;
    }

    void disconnect(ConnectionId who) override
    {
        if (link_->closed || who != peer_id()) { return; }
        link_->closed = true;
        NetEvent ev; ev.type = NetEvent::Type::Disconnected; ev.connection = self_id();
        peer_inbound().push_back(std::move(ev)); // the peer learns I left
    }

private:
    std::deque<NetEvent>& my_inbound()
    { return is_server_ ? link_->to_server : link_->to_client; }
    std::deque<NetEvent>& peer_inbound()
    { return is_server_ ? link_->to_client : link_->to_server; }

    std::shared_ptr<detail::LoopLink> link_;
    bool                              is_server_;
};

// A connected server+client pair sharing one Link. Both first poll()s yield Connected.
inline std::pair<std::shared_ptr<Transport>, std::shared_ptr<Transport>> make_loopback_pair()
{
    auto link = std::make_shared<detail::LoopLink>();
    auto server = std::make_shared<LoopbackTransport>(link, /*is_server=*/true);
    auto client = std::make_shared<LoopbackTransport>(link, /*is_server=*/false);
    return {server, client};
}
} // namespace mosaic::engine::net
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -5 && ./build/debug/tests/mosaic_unit_tests "[net]" 2>&1 | tail -8`
Expected: the 6 `[net]` cases PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/net/transport.hpp engine/net/loopback_transport.hpp \
        tests/unit/net/loopback_transport_test.cpp tests/CMakeLists.txt
git commit -m "feat(net): neutral Transport seam + in-memory loopback (networking N1)"
```

---

### Task 2: Handoff update

**Files:**
- Modify: `docs/project/next-steps.md`

- [ ] **Step 1: Update the handoff**

In `docs/project/next-steps.md`:
1. Update the **Last updated** line (date `2026-09-09`) with a "Networking pillar STARTED — N1 (neutral Transport + in-memory loopback) DONE" summary: a backend-free `Transport` seam (`send`/`poll`/`disconnect`, `NetEvent`, `Channel` reliable/unreliable, opaque byte payloads) in `engine/net/transport.hpp` + a header-only in-process `make_loopback_pair()` (server+client linked, connect/disconnect events, FIFO delivery, channel tag preserved) in `engine/net/loopback_transport.hpp`; headless-tested (6 `[net]` cases), no sockets. The pillar targets **server-authoritative replication**. Networking decomposition: **N1 done** → N2 session/lifecycle + `NetworkSystem` → N3 state replication (reflection→snapshots) → N4 spawn/despawn + net-id map → N5 client input; a real ENet UDP backend is a later, maintainer-verified slice.
2. Note **UI pillar COMPLETE** (already recorded) and that **networking is the last Phase-4 subsystem, now underway**. Link the N1 spec + this plan.

- [ ] **Step 2: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "docs: mark networking N1 (Transport + loopback) done; N2 next"
```

---

## Self-Review

**Spec coverage:**
- `engine/net/transport.hpp` neutral `Transport` + `NetEvent`/`Channel`/`ConnectionId` → Task 1. ✓
- `engine/net/loopback_transport.hpp` `make_loopback_pair()` + loopback (connect/send/poll/disconnect, FIFO, channel tag, fixed ids server=1/client=2) → Task 1. ✓
- Tests: connect handshake, reliable both directions (+ sender doesn't self-receive), unreliable tag, FIFO, disconnect + drop-later, drop-to-unknown → Task 1. ✓
- Real ENet backend deferred → documented non-goal, no task. ✓
- Handoff → Task 2. ✓

**Placeholder scan:** no TBD/TODO; every code step has full, real code (interface + loopback impl + all six tests).

**Type consistency:** `Transport::send(ConnectionId, Channel, const std::string&)` / `poll() -> vector<NetEvent>` / `disconnect(ConnectionId)`; `NetEvent{Type,connection,channel,data}`; `Channel{Reliable,Unreliable}`; `make_loopback_pair() -> pair<shared_ptr<Transport>, shared_ptr<Transport>>`. Ids: server endpoint `self_id()=1`/`peer_id()=2`, client endpoint `self_id()=2`/`peer_id()=1` — so the server's `Connected`/`Message` events carry `connection=2` (client) and the client's carry `connection=1` (server), matching the tests. Consistent across the interface, impl, and tests.
