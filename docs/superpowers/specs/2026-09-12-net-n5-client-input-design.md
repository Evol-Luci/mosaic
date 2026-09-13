# Networking N5 — client → server input — Design

## Goal (Phase 4, Networking pillar — slice N5)

Close the input loop: let a client send per-tick input to the server. N5 stays **neutral
plumbing** — the engine does not know what "input" means. A client sends an opaque string
payload; the server surfaces each one as a `NetworkInput{session, payload}` bus event. Nothing
in the engine decodes or applies it — that is game/script logic layered above, exactly like
`UiButton` emitting a `ScriptEvent` rather than mutating the World itself.

Everything stays **pure engine + headless**, proven deterministically with two in-process
Worlds linked by `make_loopback_pair()`.

## Design

### Boundary decision

- **N5 is neutral plumbing.** The client sends an opaque `std::string` payload (the
  app/scripts decide its meaning — a serialized command, a JSON blob, anything). The server
  does not decode it or mutate the World; it emits `NetworkInput{session, payload}` on the bus
  for game/script logic to consume.
- Rejected alternative: N5 also decodes a structured command schema and applies it
  authoritatively server-side. Rejected because it would bake gameplay input semantics
  (movement axes, button maps) into the engine, coupling networking to a specific game.

### Client input seam: `InputSystem`

One system, two roles, symmetric with `ReplicationSystem`/`NetworkSystem`:

- **Client role:** polls an injected provider `std::function<std::vector<std::string>()>`
  each tick and calls `NetworkSystem::send_to_server` once per returned payload. Mirrors how
  `UiSystem` takes a pointer provider and `main.cpp` fills it from GLFW — headless-testable
  with a fake provider, no GLFW dependency in the engine.
- **Server role:** drains `NetworkSystem::take_inbound()` each tick and emits one
  `NetworkInput{session, payload}` bus event per `MessageType::Input` frame. Non-`Input`
  frames in the drained batch are ignored (there are none today, but this keeps the system
  forward-compatible with future inbound message types).

Rejected alternative: no client-side system, just expose `send_to_server` on `NetworkSystem`
and let game/script code call it directly, with only a server-side drain+emit system. Rejected
because it gives the client no uniform per-tick send seam and leaves the send path untested by
a system (only by direct `NetworkSystem` calls).

### `engine/net/message.hpp` — new message type

Add `Input = 4` to `MessageType` (payload: opaque bytes, client -> server) and accept it in
`decode`'s validation list. No wire-format change beyond the existing `[1 byte type][payload]`
frame.

### `engine/net/network_system.hpp` — `send_to_server`

`NetworkSystem` already has `broadcast` (server -> all clients) but nothing for client ->
server. Add:

```cpp
// Client: send `bytes` to the server. No-op if not yet connected (no Connected event seen).
void send_to_server(Channel channel, const std::string& bytes)
{
    if (!transport_ || server_conn_ == 0) { return; }
    transport_->send(server_conn_, channel, bytes);
}
```

`server_conn_` is a new private member (`ConnectionId server_conn_ = 0;`), set in
`handle_client` on the `Connected` event (the same event that currently only triggers sending
`Hello`):

```cpp
if (ev.type == NetEvent::Type::Connected)
{
    server_conn_ = ev.connection;
    transport_->send(ev.connection, Channel::Reliable, encode(MessageType::Hello, ""));
}
```

`ConnectionId` is `std::uint32_t` with `0` reserved as invalid/none (per `transport.hpp`), so
the zero-check is the natural "not connected yet" guard — consistent with `is_connected()`
tracking a separate `connected_` bool for the *session* (post-Welcome), while `server_conn_`
tracks the *transport* link (post-Connected, which precedes the session handshake). Sending
before the session is established is harmless (the server drops non-Hello frames from
unestablished connections in `handle_server`), so no extra guard against pre-Welcome sends is
needed.

### `engine/net/network_event.hpp` — `NetworkInput`

Add alongside `NetworkEvent` (a distinct struct, not a new `NetworkEvent::Type`, since it
carries a payload rather than just a peer id):

```cpp
// Emitted on the World event bus by InputSystem (server role) for each received client
// input frame. Carries the sender's session id and the opaque payload — the engine does not
// interpret it.
struct NetworkInput
{
    std::uint32_t session = 0;
    std::string   payload;
};
```

### `engine/net/input_system.hpp` (new)

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
// server. Server role: drains inbound Input frames and emits NetworkInput on the bus. Added
// to the World AFTER NetworkSystem so inbound frames are available each tick (server role) /
// the session's transport link is established (client role).
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

No drain conflict: the client's `InputSystem` only sends and never calls `take_inbound()`
(that stays `ReplicationSystem`'s job, draining `Snapshot` frames on the client's own
`NetworkSystem`); the server's `InputSystem` drains `Input` frames from the server's own
`NetworkSystem` (the server's `ReplicationSystem` never drains — it only broadcasts). Client
and server each have their own `NetworkSystem` instance, so there is exactly one drainer of
`take_inbound()` per role, matching the existing N3-follow/N4 pattern.

### Data flow (loopback)

```
client: provider() -> ["move:1,0"]
        InputSystem.update -> net_.send_to_server(Reliable, encode(Input, "move:1,0"))
server: NetworkSystem buffers it as InboundMessage{peer=<session>, Input, "move:1,0"}
        InputSystem.update -> drains it -> world.events().emit(NetworkInput{session, "move:1,0"})
```

## Testing (headless, deterministic)

- **`message_test.cpp`** — extend: `encode`/`decode` round-trip for `MessageType::Input`;
  `decode` still rejects an unknown type byte.
- **`network_system_test.cpp`** — extend: a client `NetworkSystem` with a `FakeTransport`
  calls `send_to_server` after a `Connected` event has been polled, and the fake's `sent` list
  shows the frame addressed to the server's `ConnectionId`; `send_to_server` before any
  `Connected` event is a no-op (nothing appended to `sent`).
- **`input_system_test.cpp`** (new) —
  - Client role: a `NetworkSystem` + `InputSystem` with a fake provider returning a fixed
    payload list; after one `update`, the underlying `FakeTransport`'s `sent` shows one frame
    per payload, each decoding to `MessageType::Input` with the matching payload.
  - Client role, empty provider result: no frames sent.
  - Server role: inject a `MessageType::Input` frame into the server's `FakeTransport` inbox
    from an already-established session; after `NetworkSystem::update` then
    `InputSystem::update`, a subscribed `NetworkInput` handler recorded exactly one event with
    the correct session id and payload.
  - **End-to-end over the loopback:** client World with `NetworkSystem` + `InputSystem` (role
    Client, provider returning one payload per call), server World with `NetworkSystem` +
    `InputSystem` (role Server) subscribed to `NetworkInput`. Pump both worlds through the
    handshake, then a few more ticks; assert the server received `NetworkInput` events with
    the client's session id and the expected payload(s), in order.

All in-process, no sockets, deterministic.

## Files

**New**
- `engine/net/input_system.hpp`
- `tests/unit/net/input_system_test.cpp`

**Modified**
- `engine/net/message.hpp` — `MessageType::Input`.
- `engine/net/network_system.hpp` — `server_conn_` member + `send_to_server`.
- `engine/net/network_event.hpp` — `NetworkInput` struct.
- `tests/unit/net/message_test.cpp` — `Input` round-trip case.
- `tests/unit/net/network_system_test.cpp` — `send_to_server` cases.
- `tests/CMakeLists.txt` — register `unit/net/input_system_test.cpp`.

## Non-goals / follow-ups

- **N4b (still outstanding, DEFERRED):** server-side `NetId` assignment policy (stamp a unique
  monotonic id on `NetReplicated` entities created with `id == 0`). Not touched by N5.
- Applying input to the World (movement, actions) — game/script logic above the engine,
  subscribing to `NetworkInput` and doing its own decode. No schema is prescribed here.
- Input ordering/reliability guarantees beyond what `Channel::Reliable` already gives (no
  sequence numbers, no de-dup, no rate limiting) — later concern if it becomes a problem.
- Real **ENet UDP backend** + `mosaic run --host`/`--connect` + `setup_runtime`/demo — later
  maintainer-verified slice.
