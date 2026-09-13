# Networking N6 — real ENet transport — Design

## Goal (Phase 4, Networking pillar — slice N6)

Give the neutral `Transport` seam (N1, `engine/net/transport.hpp`) a real backend. Everything
built on top of `Transport` — `NetworkSystem`, `ReplicationSystem`, `InputSystem` — depends
only on that interface, so none of it changes. N6 adds `modules/net_enet`: a real, UDP-based
`Transport` implementation over [ENet](https://github.com/lsalzman/enet), a small,
well-established reliable-UDP library that already models exactly the
`Channel::Reliable`/`Channel::Unreliable` split this codebase uses.

N6 is **transport only** — no CLI flags, no `setup_runtime` wiring, no demo scene. That's N7.
N6's own tests are still fully automated: real UDP over `127.0.0.1` in-process, no manual step,
no separate machines — the "later maintainer-verified slice" language in `next-steps.md`
refers to the eventual multi-process/`--host`/`--connect` demo (N7), not this module.

## Design

### Approach

Vendor ENet via `FetchContent` (it ships its own `CMakeLists.txt`, so `FetchContent_MakeAvailable`
works directly, mirroring how Box3D/miniaudio/GLFW are already pulled in), wrapped in a new
module `modules/net_enet` with a thin C++ facade — mirrors `modules/audio_miniaudio` exactly:
a header declaring only factory functions, `enet_*` types walled inside the `.cpp`.

Rejected alternatives:
- **Hand-rolled reliable UDP** (no external dependency): reinvents exactly what ENet already
  solves well (sequencing, ack/retransmit, connection lifecycle) — real risk for no benefit,
  against this project's reuse-over-reinvent bias.
- **A heavier library** (e.g. GameNetworkingSockets): drags in OpenSSL/protobuf for encryption
  and NAT traversal this project doesn't need yet. ENet is single-purpose, small (~15 files, C),
  and its two-channel-type model is already a near-exact match for `Channel`.

### `modules/net_enet/enet_transport.hpp`

```cpp
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "engine/net/transport.hpp"

namespace mosaic::net::enet
{
// Real UDP transport over ENet. Listens on `port` for up to `max_peers` clients.
// Returns nullptr if the host could not bind (e.g. port already in use).
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

### `modules/net_enet/enet_transport.cpp` — `EnetTransport`

A single `EnetTransport : public Transport` class serves both roles — once an `ENetHost*`
exists (server: created listening; client: created unbound, then `enet_host_connect` called
once toward the target), the two behave identically from `poll()`'s point of view: enumerate
whatever peers the host has. No `is_server_` bool is needed (unlike `LoopbackTransport`, which
needs it only to pick "mine" vs "peer" queues in its toy in-memory model — ENet's `ENetHost`
already tracks its own peers).

```cpp
class EnetTransport : public Transport
{
public:
    explicit EnetTransport(ENetHost* host) : host_(host) {}
    ~EnetTransport() override { enet_host_destroy(host_); release_enet(); }

    void send(ConnectionId to, Channel channel, const std::string& bytes) override
    {
        auto it = id_to_peer_.find(to);
        if (it == id_to_peer_.end()) { return; } // unknown/stale id: no-op, no crash
        const bool reliable = channel == Channel::Reliable;
        ENetPacket* packet = enet_packet_create(
            bytes.data(), bytes.size(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
        enet_peer_send(it->second, reliable ? 0 : 1, packet);
    }

    std::vector<NetEvent> poll() override
    {
        std::vector<NetEvent> out;
        ENetEvent ev;
        while (enet_host_service(host_, &ev, 0) > 0)
        {
            switch (ev.type)
            {
            case ENET_EVENT_TYPE_CONNECT:
            {
                const ConnectionId id = next_id_++;
                peer_to_id_[ev.peer] = id;
                id_to_peer_[id] = ev.peer;
                out.push_back({NetEvent::Type::Connected, id, Channel::Reliable, {}});
                break;
            }
            case ENET_EVENT_TYPE_RECEIVE:
            {
                auto it = peer_to_id_.find(ev.peer);
                if (it != peer_to_id_.end())
                {
                    out.push_back({NetEvent::Type::Message, it->second,
                                   ev.channelID == 0 ? Channel::Reliable : Channel::Unreliable,
                                   std::string(reinterpret_cast<char*>(ev.packet->data),
                                               ev.packet->dataLength)});
                }
                enet_packet_destroy(ev.packet);
                break;
            }
            case ENET_EVENT_TYPE_DISCONNECT:
            {
                auto it = peer_to_id_.find(ev.peer);
                if (it != peer_to_id_.end())
                {
                    out.push_back({NetEvent::Type::Disconnected, it->second, Channel::Reliable, {}});
                    id_to_peer_.erase(it->second);
                    peer_to_id_.erase(it);
                }
                break;
            }
            case ENET_EVENT_TYPE_NONE:
                break;
            }
        }
        return out;
    }

    void disconnect(ConnectionId who) override
    {
        auto it = id_to_peer_.find(who);
        if (it == id_to_peer_.end()) { return; }
        enet_peer_disconnect(it->second, 0); // graceful; DISCONNECT event follows via poll()
    }

private:
    ENetHost* host_;
    std::unordered_map<ENetPeer*, ConnectionId> peer_to_id_;
    std::unordered_map<ConnectionId, ENetPeer*> id_to_peer_;
    ConnectionId next_id_ = 1; // 0 is reserved invalid (transport.hpp)
};
```

`ENetPeer*` values are reused by ENet after a peer disconnects, so the stable `ConnectionId`
must come from a monotonic counter mapped through both directions — never from the pointer
itself.

Both channels are declared on every host (`channelLimit = 2`): channel 0 carries
`Channel::Reliable` (`ENET_PACKET_FLAG_RELIABLE`), channel 1 carries `Channel::Unreliable`
(no flag).

### `enet_initialize`/`enet_deinitialize`

ENet requires one process-wide `enet_initialize()` before any host is created and
`enet_deinitialize()` after the last one is destroyed. Since a single test process creates
multiple `EnetTransport`s (a server + a client per test case), this is refcounted with a
small static guard local to the `.cpp` (mirrors miniaudio's "no `ma_*` symbol above the
importer" wall — `enet_*` never appears outside this file):

```cpp
namespace
{
std::atomic<int> g_enet_refs{0};
void acquire_enet() { if (g_enet_refs.fetch_add(1) == 0) { enet_initialize(); } }
} // namespace
void release_enet() { if (g_enet_refs.fetch_sub(1) == 1) { enet_deinitialize(); } }
```

Both factory functions call `acquire_enet()` before creating their host; `EnetTransport`'s
destructor calls `release_enet()` after `enet_host_destroy`.

### CMake

`modules/net_enet/CMakeLists.txt` (mirrors `modules/audio_miniaudio/CMakeLists.txt`):

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

Top-level `CMakeLists.txt`: add an `enet` `FetchContent_Declare` (GitHub `lsalzman/enet`,
pinned to a tagged release) alongside the existing declares, `FetchContent_MakeAvailable(enet)`,
and `add_subdirectory(modules/net_enet)` next to the other `add_subdirectory(modules/...)` calls.

## Testing (headless, real UDP over 127.0.0.1 — no manual step, no separate machines)

Real sockets mean real scheduling latency: unlike every earlier `[net]` test (fixed
tick-count loops over the fully-deterministic in-memory `LoopbackTransport`), N6's tests need
a bounded wait. A small helper local to the test file:

```cpp
// Call pred() up to max_attempts times (sleeping sleep_ms between) until it returns true.
// pred is responsible for its own polling/event-collection — this helper only paces the
// retries; it must NEVER poll on its own behalf, since Transport::poll() is drain-once and
// a helper-side poll would race the caller's pred() and can silently discard an event the
// very iteration it arrives.
template <class Pred>
void poll_until(Pred pred, int max_attempts = 200, int sleep_ms = 5)
{
    for (int i = 0; i < max_attempts && !pred(); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }
}
```

Two related pitfalls surfaced while implementing this (both are load-bearing for every test
below, not just theoretical): first, `enet_host_connect` and `enet_peer_send` only *queue* —
actually transmitting requires the *initiator's own* subsequent `poll()` call, so any wait
loop after a `connect` or a `send` must keep polling the side that just acted, not only the
side waiting to receive. Second, the pinned ENet `v1.3.18` defines only
`ENET_EVENT_TYPE_DISCONNECT` (no separate `DISCONNECT_TIMEOUT` variant — that exists only in
newer/unreleased ENet branches).

(~1s worst case per wait; real runs resolve in a handful of iterations over loopback.)

Cases (`tests/unit/net/enet_transport_test.cpp`, tag `[net][enet]`; each test case picks its
own fixed port, e.g. `34001`, `34002`, ... — the transport is destroyed at scope-exit via
`unique_ptr`, releasing the port before the next case runs):

- **Connect handshake:** `create_enet_server(port, 4)` + `create_enet_client("127.0.0.1", port)`;
  poll both until each has seen exactly one `Connected` event.
- **Reliable send both directions:** after connecting, each side sends a distinct string on
  `Channel::Reliable`; poll the other until a `Message` event with matching `channel`/`data`
  arrives.
- **Unreliable send tag preserved:** same, with `Channel::Unreliable`; assert the received
  event's `channel == Unreliable`.
- **Disconnect surfaces an event:** client calls `disconnect()` on the server's `ConnectionId`
  (or vice versa); poll the peer until a `Disconnected` event with the matching id arrives.
- **Send to an unknown id is a no-op:** `send()` with a `ConnectionId` that was never seen —
  no crash, no event on either side (mirrors the existing `LoopbackTransport`
  drop-to-unknown-connection test).
- **Multiple clients get distinct ids:** one server, two clients connecting; the server's
  polled `Connected` events carry two different `ConnectionId`s.

## Files

**New**
- `modules/net_enet/CMakeLists.txt`
- `modules/net_enet/enet_transport.hpp`
- `modules/net_enet/enet_transport.cpp`
- `tests/unit/net/enet_transport_test.cpp`

**Modified**
- `CMakeLists.txt` — `FetchContent_Declare`/`MakeAvailable(enet)` + `add_subdirectory(modules/net_enet)`.
- `tests/CMakeLists.txt` — register the new test file + link `mosaic_net_enet`.
- `docs/project/next-steps.md` — mark N6 done.

## Non-goals / follow-ups

- **N7:** `mosaic run --host <port>` / `--connect <host:port>` CLI flags, `setup_runtime`
  wiring (inject an `EnetTransport` in place of the null/loopback path, symmetric with how
  `audio_backend` is injected), and a demo scene — genuinely needs two running processes,
  the first manual/maintainer-verified step in the whole networking pillar.
- NAT traversal, encryption, compression — ENet doesn't provide these either; out of scope
  for this project for now.
- Reconnection/backoff logic on link loss — `NetworkSystem` already reacts to `Disconnected`;
  automatic reconnect attempts are a later concern if ever needed.
- Anything requiring genuinely separate machines — N6's tests are all `127.0.0.1`, in-process.
