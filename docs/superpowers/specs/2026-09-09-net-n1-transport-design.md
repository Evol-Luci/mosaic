# Networking N1 — neutral Transport + in-memory loopback — Design

## Goal (Phase 4, Networking pillar — slice 1)

Start the **networking** pillar (the last Phase-4 subsystem) with its foundational seam:
a neutral `Transport` interface for moving opaque byte messages between endpoints, plus
an **in-memory loopback** implementation that links a server + client pair in-process.
Everything is proven **headless and deterministic** — the sandbox has no real sockets, so
(exactly like audio's null device and the render FakeDevice) the testable core is the
in-process loopback; a real UDP backend is a later, maintainer-verified slice.

The pillar targets **server-authoritative replication** (a server owns the World; clients
send input and receive serialized component-state snapshots). N1 is model-agnostic — it
just carries bytes — and is the seam that N2 (sessions) → N3 (state replication) → N4
(spawn/despawn + net-id map) → N5 (client input) build on.

**Scope (YAGNI):** move opaque bytes between two in-process endpoints, with connection
events and two channel tags. No real sockets, no serialization, no session/role logic,
no loss/latency simulation.

## Design

### `engine/net/transport.hpp` — neutral interface

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mosaic::engine::net
{
using ConnectionId = std::uint32_t;   // opaque; 0 == invalid/none

// Reliable-ordered vs UDP-style unreliable. The loopback delivers both losslessly and
// in order (it tags the channel but does not simulate loss); the tag matters for a real
// backend. Snapshots ride Unreliable; handshake/spawn ride Reliable (a later slice).
enum class Channel { Reliable, Unreliable };

// A single event drained from an endpoint's poll(). `data`/`channel` are set only for
// Message; `connection` identifies the peer for all three.
struct NetEvent
{
    enum class Type { Connected, Disconnected, Message };
    Type         type = Type::Message;
    ConnectionId connection = 0;
    Channel      channel = Channel::Reliable;
    std::string  data;                 // opaque bytes (Message only)
};

// One network endpoint (a server has many connections; a client has one). Backend-free:
// concrete transports (loopback here; a real UDP backend later) live behind this seam,
// created via factory functions. `poll()` is the per-tick, non-blocking event drain.
class Transport
{
public:
    virtual ~Transport() = default;
    virtual void send(ConnectionId to, Channel channel, const std::string& bytes) = 0;
    virtual std::vector<NetEvent> poll() = 0;   // drains events since the last poll()
    virtual void disconnect(ConnectionId who) = 0;
};
} // namespace mosaic::engine::net
```

Opaque `std::string` payloads (byte buffers); framing/serialization is N3+. `poll()`
returns and clears the pending events — callers service it once per tick, the same
discipline the audio/physics systems use for their backends.

### `engine/net/loopback_transport.hpp` — in-memory pair

A linked server+client pair in one process. Each endpoint owns an inbound queue; `send`
enqueues onto the **peer's** inbound tagged with the sender's `ConnectionId`; `poll`
drains this endpoint's inbound. A shared link struct holds both queues + the connected
flag so the two endpoints reference the same state.

```cpp
// Factory: a connected server+client pair. Both endpoints' FIRST poll() yields a
// Connected event (server sees the client's id, client sees the server's id). After a
// disconnect()/close, the peer's next poll() yields Disconnected.
std::pair<std::shared_ptr<Transport>, std::shared_ptr<Transport>> make_loopback_pair();
```

Behavior:
- **Connect:** on creation the link is "connected"; the first `poll()` on each side emits
  one `Connected{peer_id}` (a latched, once-only event).
- **Send:** `send(to, channel, bytes)` appends `Message{from=this_id, channel, bytes}` to
  the peer's inbound (in-order, FIFO). `to` is validated against the peer id; a `to` that
  isn't the peer is dropped (no throw).
- **Poll:** returns and clears this endpoint's inbound (Connected latched first, then
  Messages FIFO, then a Disconnected if the peer closed).
- **Disconnect:** `disconnect(peer)` (or destruction) marks the link closed and latches a
  `Disconnected` for the peer's next `poll()`; further sends are dropped.

Fixed ids for determinism: server `ConnectionId = 1` on the client side refers to the
server; client `ConnectionId = 2` on the server side refers to the client (the pair uses
two stable ids). Pure in-engine, header-only, no external dependency, no module wall.

### Deferred (a later slice) — real UDP backend

`modules/net_enet` implementing `Transport` on **ENet** (UDP reliability, MIT/C, via
FetchContent, `enet_*` walled in the `.cpp`), created via `create_enet_server(port)` /
`create_enet_client(host, port)` factories — mirroring `create_miniaudio_backend()`.
**Maintainer-verified on a real network** (no sockets in the sandbox). Not part of N1.

## Testing (headless, deterministic)

New unit tests under `tests/unit/net/`:

- **Connect handshake** — `make_loopback_pair()`; each endpoint's first `poll()` returns
  exactly one `Connected` event (with the peer's id); a second `poll()` (no traffic)
  returns empty.
- **Reliable message both directions** — client `send(server_id, Reliable, "ping")` →
  server `poll()` yields `Message{client_id, Reliable, "ping"}`; server →
  client likewise; the client's `poll()` does not also see its own sent message.
- **Unreliable tag preserved** — `send(..., Unreliable, ...)` arrives as
  `Message{..., Unreliable, ...}`.
- **FIFO ordering** — three sends drain in send order in one `poll()`.
- **Disconnect** — `disconnect(peer)` (or dropping one endpoint) → the other's `poll()`
  yields a `Disconnected{peer_id}`; subsequent sends to the closed peer are silently
  dropped (no throw, no crash).
- **Send to an unknown connection** — dropped, no throw.

All in-process, deterministic, no sockets. (No demo/CLI in N1 — networking has no
visual/audible output; the loopback tests are the proof. A `mosaic net-selftest` CLI can
come with N2 when there's a session to exercise.)

## Files

**New**
- `engine/net/transport.hpp` — `ConnectionId`, `Channel`, `NetEvent`, `Transport`.
- `engine/net/loopback_transport.hpp` — `make_loopback_pair()` + the loopback impl.
- `tests/unit/net/loopback_transport_test.cpp` — the tests above.

**Modified**
- `tests/CMakeLists.txt` — add the new test source.

## Non-goals / follow-ups

- **N1b/later:** the real ENet UDP backend (module-walled, maintainer-verified).
- **N2:** session/connection lifecycle + a `NetworkSystem` (client vs server role,
  handshake, `mosaic run --host` / `--connect`).
- **N3:** server→client state replication (reflection → `Value` snapshots).
- **N4:** entity spawn/despawn replication + net-id ↔ `EntityID` map.
- **N5:** client→server input/commands (via the `CommandStack`/event bus); later
  prediction/reconciliation, delta compression, loss/latency simulation on the loopback.
