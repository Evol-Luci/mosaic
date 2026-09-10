# Networking N3-follow — replication send-wiring (ReplicationSystem) — Design

## Goal (Phase 4, Networking pillar — slice N3-follow)

Wire N3's state replication **over the live session**: the server builds a snapshot each
tick and sends it to connected clients; the client receives snapshots and applies them, so
a client World comes to **mirror** the server World over the network. N3 gave the mechanism
(`snapshot()` / `apply_snapshot()` / `NetIdMap`); N2 gave the session (`NetworkSystem` +
`Transport` + handshake). N3-follow is the small system that joins them.

Everything stays **pure engine + headless**, proven deterministically with two in-process
Worlds linked by N1's `make_loopback_pair()`. **No** `setup_runtime`/CLI wiring, **no** real
backend, **no** demo — those land with the later real-ENet-backend slice (consistent with
N1/N2).

## Design

### Architecture

`NetworkSystem` becomes the **single transport hub**: it is the only thing that calls
`transport_->poll()`, it handles the `Hello`/`Welcome` control frames itself, it surfaces
non-control frames (snapshots) to other systems, and it exposes a send path. A new
`ReplicationSystem` layers replication policy on top, reusing N3's `snapshot()` /
`apply_snapshot()`. Both are plain `ecs::System`s on the same World; `NetworkSystem` is
added **first** so it polls before `ReplicationSystem` runs each tick.

### `engine/net/message.hpp` — add the Snapshot message type

Add a third `MessageType` and accept it in `decode()`:

```cpp
enum class MessageType : std::uint8_t
{
    Hello    = 1, // client -> server on connect (payload: empty)
    Welcome  = 2, // server -> client (payload: u32 assigned session id)
    Snapshot = 3, // server -> client (payload: JSON replication snapshot from N3)
};
```

`decode()`'s known-type guard is extended to include `Snapshot`, so a snapshot frame
decodes to `{Snapshot, payload}` instead of being rejected as unknown. `encode()` is
unchanged (type-agnostic).

### `engine/net/network_system.hpp` — extend the N2 system into the hub

- New inbound record:

  ```cpp
  struct InboundMessage
  {
      std::uint32_t peer;      // server: sender's session id; client: 0 ("the server")
      MessageType   type;
      std::string   payload;
  };
  ```

- Message handling (both roles): after `decode`, **control frames** (`Hello` on the server,
  `Welcome` on the client) are handled exactly as in N2. **Any other known type** is pushed
  onto a private `std::vector<InboundMessage> inbound_`:
  - Server: buffer only frames from an **established session** —
    `inbound_.push_back({sessions_[conn], type, payload})`; frames from an unknown
    connection are ignored.
  - Client: `inbound_.push_back({0, type, payload})`.

- `std::vector<InboundMessage> take_inbound()` — returns `std::move(inbound_)` and leaves
  the buffer empty (drain-once semantics).

- `void broadcast(Channel channel, const std::string& bytes)` — **server**: for each entry
  in `sessions_` (`ConnectionId -> session id`), `transport_->send(conn, channel, bytes)`.
  Zero sessions ⇒ no-op. (No client→server send in this slice; client input is N5.)

The existing N2 API (`role()`, `is_connected()`, `local_id()`, `clients()`) and the
handshake behavior are unchanged; `NetworkSystem` still writes no ECS state.

### `engine/net/replication_system.hpp` — the new system

```cpp
class ReplicationSystem : public mosaic::engine::ecs::System
{
public:
    ReplicationSystem(Role role, NetworkSystem* net,
                      const mosaic::engine::reflection::ComponentReflection* types);
    void update(mosaic::engine::World& world, float dt) override;

private:
    Role role_;
    NetworkSystem* net_;
    const mosaic::engine::reflection::ComponentReflection* types_;
    NetIdMap ids_; // client-held stable NetId -> local EntityID map
};
```

`update(world, dt)` by role (no-op if `net_` or `types_` is null):

- **Server**: if `net_->clients()` is non-empty, build and broadcast a full snapshot:
  `net_->broadcast(Channel::Reliable, encode(MessageType::Snapshot, snapshot(world, *types_)))`.
  Full snapshot **every tick** — no delta/dirty tracking (a later optimization). When no
  clients are connected, nothing is serialized or sent.
- **Client**: drain `net_->take_inbound()`; for each message with `type == Snapshot`, call
  `apply_snapshot(msg.payload, world, *types_, ids_)`. Non-snapshot messages are ignored.

`ReplicationSystem` holds a raw `NetworkSystem*` (the World owns both systems; the pointer
is obtained at construction after `add_system<NetworkSystem>()`, or via
`world.get_system<NetworkSystem>()`). It owns its own `NetIdMap` — client-held identity
state, exactly as N3 specified.

### Channel

Snapshots use **`Channel::Reliable`** — simplest and fully deterministic over the in-memory
loopback. Realistic unreliable / latest-wins delivery is an ENet-era concern, deferred.

### Data flow (loopback, after the N2 handshake)

```
N2 handshake completes        -> server.clients() == {1}
server tick: ReplicationSystem(Server) -> broadcast Snapshot(blob) to session 1's conn
client tick: NetworkSystem polls -> Message(Snapshot) buffered as InboundMessage{0,Snapshot,blob}
             ReplicationSystem(Client) -> take_inbound() -> apply_snapshot -> client mirrors server
server moves the entity; next server tick broadcasts a fresh snapshot
client tick: apply -> NetIdMap hit -> SAME entity updated, no duplicate
```

### Error handling

- `apply_snapshot` never throws (N3): a malformed/empty payload returns false and is
  ignored.
- `broadcast` to a stale/unknown `ConnectionId` is a safe no-op (N1 "drop-to-unknown").
- A garbage frame (unknown type) still fails `decode` in `NetworkSystem` and is dropped;
  only known non-control types are buffered.

## Testing (headless, deterministic)

New/extended unit tests under `tests/unit/net/`:

- **`message` Snapshot round-trip** (extend `message_test.cpp`) —
  `decode(encode(Snapshot, blob))` yields `{Snapshot, blob}` for a representative JSON blob
  (incl. an empty payload).
- **`NetworkSystem` hub** (new cases, via a `FakeTransport` and/or the loopback) — a server
  `broadcast(Reliable, frame)` reaches a connected client, surfacing in the client's
  `take_inbound()` as `{0, Snapshot, payload}`; a **second** `take_inbound()` is empty
  (drain-once); `broadcast` with **zero** connected clients sends nothing and does not
  crash; a frame from an **unknown** connection on the server is not buffered.
- **End-to-end replication over the loopback** (`replication_system_test.cpp`) — a server
  `World` with `NetworkSystem(Server)` + `ReplicationSystem(Server)` and a client `World`
  with the `Client` counterparts, linked by `make_loopback_pair()`; build a
  `TypeRegistry` + `ComponentReflection` via `register_builtin_components`. The server has a
  `NetReplicated{1}` entity with `Transform{pos=(1,2,3)}` + `Material` **and** an unmarked
  entity. Tick both Worlds enough times for the handshake plus a snapshot round-trip.
  Assert: the client has **exactly one** entity (the unmarked one is absent); its
  `Transform.position` and `Material.base_color` match the server's; it has **no**
  `NetReplicated`. Then move the server entity, tick again, and assert the client's entity
  **updates in place** and there is **still exactly one** (NetIdMap hit — repeated per-tick
  snapshots create no duplicate).

The `FakeTransport` (if used) lives in the test file, mirroring N2. All in-process, no
sockets, deterministic.

## Files

**New**
- `engine/net/replication_system.hpp` — `ReplicationSystem`.
- `tests/unit/net/replication_system_test.cpp`.

**Modified**
- `engine/net/message.hpp` — `MessageType::Snapshot` + `decode()` guard.
- `engine/net/network_system.hpp` — `InboundMessage`, inbound buffering, `take_inbound()`,
  `broadcast()`.
- `tests/unit/net/message_test.cpp` — Snapshot round-trip case.
- `tests/CMakeLists.txt` — new test source.

## Non-goals / follow-ups

- **Delta / dirty snapshots**, send-cadence throttling (every-N-ticks), and an **unreliable**
  snapshot channel — optimization slices once the real backend exists.
- **N4:** entity despawn/removal replication (server removal → client removal), authoritative
  spawn/despawn messages, server-side `NetId` assignment policy.
- **N5:** client → server input/commands (the `send_to_server` counterpart of `broadcast`).
- **Real ENet UDP backend + CLI** (`mosaic run --host`/`--connect`, module-walled,
  maintainer-verified) — the Transport seam makes the swap trivial; `setup_runtime` wiring
  and a runnable demo ride with it.
