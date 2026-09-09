# Networking N2 — session lifecycle + NetworkSystem — Design

## Goal (Phase 4, Networking pillar — slice 2)

Add the **session layer** on top of N1's `Transport`: a `NetworkSystem` (an `ecs::System`
with an injected `Transport` and a server/client `Role`) that drives connection lifecycle
each tick — a minimal **Hello/Welcome handshake** that establishes an app-level session and
hands the client a server-assigned **session id** — and surfaces connect/disconnect on the
World event bus. A tiny **type-tagged message envelope** (introduced here) frames control
messages and is the format N3+ (snapshots, input) extend.

Everything is **pure engine + headless**, proven deterministically via N1's
`make_loopback_pair()` (a server `World`+`NetworkSystem` and a client `World`+`NetworkSystem`
linked in-process). **No** `setup_runtime`/CLI wiring, **no** real backend, **no** demo —
those land with the later real-ENet-backend slice; the loopback test is the proof.

The pillar targets **server-authoritative replication**; N2 establishes the session +
client identity that N3 (state replication) and N4 (net-id ↔ EntityID map) build on.

## Design

### `engine/net/message.hpp` — message envelope

A minimal framing: a 1-byte message type followed by an opaque payload. Extended by
N3+ (Snapshot, Input, …).

```cpp
#pragma once

#include <cstdint>
#include <string>

namespace mosaic::engine::net
{
enum class MessageType : std::uint8_t
{
    Hello   = 1, // client -> server on connect (payload: empty; room for a protocol version)
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

// Decode a frame. Returns false on an empty/too-short buffer or an unknown type.
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

// Little-endian u32 payload helpers.
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

### `engine/net/network_event.hpp` — engine-level event

```cpp
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

The EventBus is generic (no registration); N3 and a later scripts bridge subscribe.

### `engine/net/network_system.hpp` — the session system

```cpp
enum class Role { Server, Client };

class NetworkSystem : public mosaic::engine::ecs::System
{
public:
    NetworkSystem(Role role, std::shared_ptr<Transport> transport);
    void update(mosaic::engine::World& world, float dt) override;

    Role role() const;
    bool is_connected() const;                          // client: session established
    std::uint32_t local_id() const;                     // client: assigned id (0 until Welcome)
    const std::vector<std::uint32_t>& clients() const;  // server: connected session ids
private:
    Role role_;
    std::shared_ptr<Transport> transport_;
    // server: transport ConnectionId -> assigned session id, + a monotonic next id (from 1)
    std::unordered_map<ConnectionId, std::uint32_t> sessions_;
    std::vector<std::uint32_t> clients_;
    std::uint32_t next_session_id_ = 1;
    // client:
    bool connected_ = false;
    std::uint32_t local_id_ = 0;
};
```

`update(world, dt)` drains `transport_->poll()` and, per event, by role:

- **Server**
  - `Connected(conn)` → nothing yet (await Hello).
  - `Message(conn, …)` → `decode`; if `Hello` and `conn` not already a session: assign
    `id = next_session_id_++`, `sessions_[conn]=id`, `clients_.push_back(id)`, send
    `encode(Welcome, put_u32(id))` on `Channel::Reliable` to `conn`, and
    `world.events().emit(NetworkEvent{PeerJoined, id})`.
  - `Disconnected(conn)` → if known: erase from `sessions_`/`clients_`, emit
    `NetworkEvent{PeerLeft, id}`.
- **Client**
  - `Connected(conn)` → `send(conn, Reliable, encode(Hello, ""))` (conn = the server, id 1).
  - `Message(conn, …)` → `decode`; if `Welcome`: `local_id_ = get_u32(payload)`,
    `connected_ = true`, emit `NetworkEvent{SessionEstablished, local_id_}`.
  - `Disconnected(conn)` → `connected_ = false`, emit `NetworkEvent{Disconnected, local_id_}`.

Session ids are **app-level and monotonic** (server-assigned, from 1), decoupled from the
transport `ConnectionId` (which is backend-specific) — N3/N4 key on the session id. The
`NetworkSystem` writes no ECS components (pure session state); it only reads the transport
and emits events + exposes queryable state.

### Data flow (loopback, ~3 ticks)

```
client tick: poll -> Connected            -> send Hello
server tick: poll -> Message(Hello)        -> id=1; send Welcome(1); emit PeerJoined(1)
client tick: poll -> Message(Welcome,1)    -> local_id=1, connected; emit SessionEstablished(1)
```

## Testing (headless, deterministic)

New unit tests under `tests/unit/net/`:

- **`message` round-trip** — `decode(encode(Hello, "")) == {Hello, ""}`;
  `decode(encode(Welcome, put_u32(7)))` → `Welcome`, `get_u32(payload)==7`; `decode("")`
  and `decode("\x99")` (unknown type) return false; `get_u32(put_u32(0xDEADBEEF))==0xDEADBEEF`.
- **Handshake end-to-end over the loopback** — `make_loopback_pair()`; a server `World`
  with `NetworkSystem(Server, server_tx)` and a client `World` with `NetworkSystem(Client,
  client_tx)`; sync `subscribe<NetworkEvent>` on each; tick both worlds ~4 times. Assert:
  `client.is_connected()`, `client.local_id()==1`, `server.clients()=={1}`; the server
  captured `PeerJoined(1)`, the client captured `SessionEstablished(1)`.
- **Server multi-client + fine-grained (via a `FakeTransport`)** — a small in-test
  `FakeTransport : Transport` whose `poll()` returns test-injected `NetEvent`s and whose
  `send()`/`disconnect()` are captured (the networking analog of `FakeRenderDevice`). With
  it, drive a server `NetworkSystem`:
  - inject `Message(conn=10, Hello)` then `Message(conn=11, Hello)` → `server.clients() ==
    {1, 2}` (**monotonic** ids), and the fake captured two `Welcome` sends carrying
    `put_u32(1)` and `put_u32(2)` on `Channel::Reliable` to conn 10 and 11.
  - a **duplicate** `Hello` from conn 10 does not double-add (idempotent) and sends no
    second Welcome.
  - inject `Disconnected(conn=10)` → `PeerLeft(1)`, `server.clients() == {2}`.
- **Disconnect (client side)** — over the loopback, `client_tx->disconnect(server_id)`;
  tick; the client system emits `NetworkEvent{Disconnected}` and `is_connected()` becomes
  false; the server emits `PeerLeft(1)` and `server.clients()` empties.
- **No traffic → no events** — ticking with an empty `poll()` emits nothing.

The `FakeTransport` lives in the test file (a test double, not shipped). All in-process,
no sockets, deterministic.

## Files

**New**
- `engine/net/message.hpp` — `MessageType`, `encode`/`decode`, `put_u32`/`get_u32`.
- `engine/net/network_event.hpp` — `NetworkEvent`.
- `engine/net/network_system.hpp` — `Role`, `NetworkSystem`.
- `tests/unit/net/message_test.cpp`, `tests/unit/net/network_system_test.cpp`.

**Modified**
- `tests/CMakeLists.txt` — new test sources.

## Non-goals / follow-ups

- **Real ENet backend + CLI** (`mosaic run --host`/`--connect`, module-walled,
  maintainer-verified) — a later isolated slice; the Transport seam makes the swap trivial.
- **N3:** server→client state replication (reflection → `Value` snapshots) over the
  session, keyed by session id.
- **N4:** entity spawn/despawn replication + net-id ↔ `EntityID` map.
- **N5:** client→server input/commands.
- Later: reconnect/timeout/heartbeat, protocol-version negotiation in Hello, a scripts
  bridge for `NetworkEvent`, multi-client loopback, `setup_runtime` wiring.
