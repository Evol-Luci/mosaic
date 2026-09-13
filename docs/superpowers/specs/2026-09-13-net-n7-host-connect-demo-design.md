# Networking N7 — `mosaic run --host`/`--connect` + demo — Design

## Goal (Phase 4, Networking pillar — slice N7, LAST)

Wire the six prior networking slices (N1-N6: neutral `Transport`, session lifecycle,
server->client replication, despawn, client->server input, and the real ENet backend) into
the actual CLI and a demo scene, so a maintainer can run two real `mosaic` processes and watch
networked multiplayer work. This closes the loop the roadmap has explicitly left open since
N5: "applying `NetworkInput` in game/script logic ... remains open work for whoever builds the
first networked gameplay demo." N7 is that demo.

This is the **first genuinely manual/maintainer-verified step** in the whole networking
pillar — every prior slice (N1-N6) was fully automated over in-memory or loopback-UDP
transports within a single test process. N7's own CLI-parsing and demo-glue logic stay
headless-testable (see Testing below); only the actual two-process `mosaic run --host` /
`mosaic run --connect` run is manual.

## Design

### Demo scope

**Full loop**, not a passive mirror: the host runs a shared `NetReplicated` orbiting cube
(exercises N1-N4, N6) and spawns a per-client avatar cube on connect, moved by that client's
WASD input via `NetworkInput` (exercises N5), despawned on disconnect (exercises N4's
despawn). The client is a pure mirror that sends its own input — nothing client-side decodes
or applies `NetworkInput`, keeping the engine-side neutrality N5 established; only this
app-level demo glue interprets the payload.

Rejected alternative: a minimal connectivity-only demo (host's existing default scene with
its orbiter marked `NetReplicated`; client just mirrors, no input wired in). Rejected because
it would leave N5's "first gameplay demo" gap explicitly still open, and the full-loop version
isn't meaningfully more code — it's the same systems, wired one step further.

### CLI (`app/cli/options.hpp` / `app/cli/options.cpp`)

Two new `run`-only options, mutually exclusive:

```cpp
std::optional<std::uint16_t> host_port;      // --host <port>
std::optional<std::string>   connect_address; // --connect <host:port>
```

```cpp
CLI::Option* host_opt = run->add_option("--host", host_port_raw, "Host a server on <port>");
CLI::Option* connect_opt = run->add_option("--connect", connect_address, "Connect to <host:port> as a client");
host_opt->excludes(connect_opt);
connect_opt->excludes(host_opt);
```

(`host_port_raw` is parsed as `int` via CLI11 then range-checked into `std::uint16_t` before
being stored in `Options`, since CLI11 doesn't natively bind `std::optional<std::uint16_t>`.)

A malformed `--connect` value (no `:`, unparsable port) or a `--host` bind failure
(`create_enet_server` returning `nullptr`, e.g. port already in use) prints an error via the
existing `reporter->error(...)` path and returns exit code `1` **before** entering the runtime
loop — mirrors how `--window` already handles `glfwInit`/`glfwCreateWindow`/render-device
failures in `main.cpp`.

`--connect` implies an **empty starting world**: when it's set, `main.cpp` skips
`populate_world`'s normal scene-loading path entirely (the client's world is populated
entirely by `ReplicationSystem` once connected) — passing `--scene`/a positional path
together with `--connect` is not an error, but the scene is ignored and a log line notes it
(consistent with this codebase's general non-fatal-warn-and-continue style elsewhere).

### `setup_runtime` (`app/run/demo_scene.hpp`)

Three more trailing-defaulted parameters, following the exact pattern `script_thread`,
`audio_backend`, and `pointer` were each added by in prior slices (append-only, so every
existing call site — tests included — keeps compiling unchanged):

```cpp
inline RuntimeScene setup_runtime(
    mosaic::engine::World&                                  world,
    mosaic::engine::assets::AssetRegistry&                  registry,
    const mosaic::engine::reflection::ComponentReflection&  reflection,
    mosaic::engine::script::ScriptSystem::EngineFactory     script_factory,
    mosaic::engine::script::ScriptHost                      host = {},
    bool                                                    script_thread = false,
    std::unique_ptr<mosaic::engine::audio::AudioBackend>    audio_backend = nullptr,
    std::function<mosaic::engine::ui::PointerState()>       pointer = {},
    mosaic::engine::net::Role                               net_role = mosaic::engine::net::Role::Client,
    std::shared_ptr<mosaic::engine::net::Transport>         net_transport = nullptr,
    std::function<std::vector<std::string>()>               input_provider = {})
```

Near the end of the function, after the existing audio/UI/physics wiring, only when a
transport was actually injected:

```cpp
if (net_transport != nullptr)
{
    auto& net = world.add_system<mosaic::engine::net::NetworkSystem>(net_role, net_transport);
    world.add_system<mosaic::engine::net::ReplicationSystem>(net_role, &net, &reflection);
    world.add_system<mosaic::engine::net::InputSystem>(net_role, &net, input_provider);
}
```

Both `ReplicationSystem` and `InputSystem` are added for **either** role — each already
branches internally on `Role` (server broadcasts/drains, client applies/sends), exactly as
designed in N3-follow and N5. No new field on `RuntimeScene` is needed: the host-demo glue
(below) only touches the World's event bus, never `NetworkSystem` directly.

### Host demo glue — new `app/run/net_host_demo.hpp`

A **standalone, testable function** — not folded into `setup_runtime` — because it is demo
game-logic, not runtime scaffolding (mirrors the existing HUD-click / contact-event
subscribers already living in `demo_scene.hpp`, but kept in its own file since it's
network-specific and only relevant to the `--host` path):

```cpp
#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "engine/net/net_replicated.hpp"
#include "engine/net/network_event.hpp"
#include "engine/render/material.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/logging/log.hpp"

namespace mosaic::app
{
// Host-only demo glue: spawns a NetReplicated avatar cube for each connecting client (id
// auto-assigned by N4b), moves it from that client's NetworkInput ("dx,dz" opaque payload —
// this is the one place that DECODES it; the engine itself never interprets NetworkInput),
// and despawns it on disconnect (N4's despawn then propagates the removal to every client).
// Pure World/event-bus logic — no CLI, no Transport — so it's headless-testable exactly like
// the N5/N6 integration tests, over two loopback-linked Worlds.
inline void wire_networked_demo_host(mosaic::engine::World& world)
{
    namespace net = mosaic::engine::net;
    using mosaic::engine::render::Material;
    using mosaic::engine::transform::Transform;

    auto avatars = std::make_shared<std::unordered_map<std::uint32_t, mosaic::foundation::EntityID>>();

    world.events().subscribe<net::NetworkEvent>(
        [&world, avatars](const net::NetworkEvent& e)
        {
            if (e.type == net::NetworkEvent::Type::PeerJoined)
            {
                const auto entity = world.create_entity();
                world.add_component<net::NetReplicated>(entity, net::NetReplicated{0});
                Transform t;
                t.position = {static_cast<float>((e.peer % 5)) - 2.0f, 1.0f, -3.0f};
                world.add_component<Transform>(entity, t);
                Material m;
                m.base_color = {0.2f, 0.6f, 0.9f};
                world.add_component<Material>(entity, m);
                (*avatars)[e.peer] = entity;
                mosaic::foundation::log::info("net", "peer {} joined; spawned avatar", e.peer);
            }
            else if (e.type == net::NetworkEvent::Type::PeerLeft)
            {
                auto it = avatars->find(e.peer);
                if (it != avatars->end())
                {
                    world.destroy_entity(it->second);
                    avatars->erase(it);
                    mosaic::foundation::log::info("net", "peer {} left; despawned avatar", e.peer);
                }
            }
        });

    world.events().subscribe<net::NetworkInput>(
        [&world, avatars](const net::NetworkInput& e)
        {
            auto it = avatars->find(e.session);
            if (it == avatars->end()) { return; }
            float dx = 0.0f, dz = 0.0f;
            if (std::sscanf(e.payload.c_str(), "%f,%f", &dx, &dz) != 2) { return; }
            if (Transform* t = world.get_component<Transform>(it->second))
            {
                t->position.x += dx;
                t->position.z += dz;
            }
        });
}
} // namespace mosaic::app
```

(`#include <cstdio>` for `std::sscanf`.)

A malformed/unparsable payload is silently ignored (matches N5's "the engine never
interprets the payload" boundary — this app-level glue is deliberately defensive, not
authoritative-strict, since it's demo code, not a protocol).

### Host's shared object (`main.cpp`, host path only)

One extra entity, created **after** `populate_world`/`setup_runtime` — not a modification to
`build_default_scene` (which several existing tests assert an exact entity count against; see
the project's own prior guidance on this), so nothing about the default scene changes:

```cpp
if (role == mosaic::engine::net::Role::Server && net_transport != nullptr)
{
    const auto e = world.create_entity();
    world.add_component<mosaic::engine::net::NetReplicated>(e, mosaic::engine::net::NetReplicated{0});
    mosaic::engine::transform::Transform t; t.position = {0.0f, 3.0f, 0.0f};
    world.add_component<mosaic::engine::transform::Transform>(e, t);
    mosaic::engine::render::Material m; m.base_color = {0.9f, 0.7f, 0.2f};
    world.add_component<mosaic::engine::render::Material>(e, m);
    world.add_component<mosaic::engine::script::ScriptComponent>(
        e, mosaic::engine::script::ScriptComponent{mosaic::app::kOrbitScript});
    mosaic::app::wire_networked_demo_host(world);
}
```

### Client input provider (`main.cpp`, client path only, mirrors the existing pointer provider)

```cpp
std::function<std::vector<std::string>()> input_provider;
if (net_role == mosaic::engine::net::Role::Client && net_transport != nullptr)
{
    input_provider = [glfw_window]() -> std::vector<std::string>
    {
        if (glfw_window == nullptr) { return {}; }
        float dx = 0.0f, dz = 0.0f;
        constexpr float speed = 0.05f;
        if (glfwGetKey(glfw_window, GLFW_KEY_D) == GLFW_PRESS) { dx += speed; }
        if (glfwGetKey(glfw_window, GLFW_KEY_A) == GLFW_PRESS) { dx -= speed; }
        if (glfwGetKey(glfw_window, GLFW_KEY_S) == GLFW_PRESS) { dz += speed; }
        if (glfwGetKey(glfw_window, GLFW_KEY_W) == GLFW_PRESS) { dz -= speed; }
        if (dx == 0.0f && dz == 0.0f) { return {}; } // nothing to send this tick
        return {std::to_string(dx) + "," + std::to_string(dz)};
    };
}
```

Sending nothing when idle keeps the demo from flooding the socket at the sim tick rate while
idle — a reasonable demo-level courtesy, not a protocol requirement.

### Data flow

```
mosaic run --host 7777 --window
  -> populate_world (default scene, unmarked) + one NetReplicated orbit cube + wire_networked_demo_host

mosaic run --connect 127.0.0.1:7777 --window
  -> empty world; NetworkSystem(Client) + ReplicationSystem(Client) + InputSystem(Client, WASD provider)
  -> connects; server's PeerJoined -> spawns an avatar; client's ReplicationSystem mirrors both
     the orbit cube AND the avatar cube (including its own)
  -> client holds D -> InputSystem sends "0.05,0.00" each tick -> server's InputSystem drains
     it -> NetworkInput{session, "0.05,0.00"} -> wire_networked_demo_host moves that avatar
  -> both processes' next snapshot carries the moved avatar -> client sees it move
  -> client process exits / connection drops -> server's PeerLeft -> avatar destroyed ->
     next snapshot omits it -> N4's despawn removes it from the client (had it still been open)
```

## Testing

**Headless & automated** (everything except the final manual step):
- `app/cli/options_test.cpp`: `--host <port>` parses into `Options::host_port`; `--connect
  host:port` parses and splits into host/port; `--host`/`--connect` together is a parse
  error (CLI11's `excludes`); an out-of-range `--host` port is rejected.
- `tests/unit/app/net_host_demo_test.cpp` (new): using two loopback-linked Worlds exactly
  like the N5/N6 integration tests — `wire_networked_demo_host` on the server World; assert
  a `PeerJoined` spawns exactly one new `NetReplicated` entity; a `NetworkInput` with a valid
  `"dx,dz"` payload moves that session's entity's `Transform` by the parsed delta; a
  malformed payload (`"garbage"`) is ignored (no crash, no change); a `PeerLeft` destroys the
  entity (`world.tick` commits the deferred destroy) and a subsequent `NetworkInput` for that
  now-gone session is a no-op.
- `setup_runtime`'s three new params default to their current no-op values, so every
  existing test (`headless_run_test.cpp`, `demo_scene_test.cpp`, etc.) is unaffected —
  confirm the full suite stays green.

**Manual (the one genuinely new kind of check in this whole pillar):** run
`mosaic run --host 7777 --window` and `mosaic run --connect 127.0.0.1:7777 --window` as two
separate processes on the same machine; verify visually: the shared orbit cube appears
identically in both windows, an avatar cube appears in both when the client connects, WASD in
the client window moves that avatar in both windows, and closing the client makes its avatar
disappear from the host's window.

## Files

**New**
- `app/run/net_host_demo.hpp`
- `tests/unit/app/net_host_demo_test.cpp`

**Modified**
- `app/cli/options.hpp` / `app/cli/options.cpp` — `--host`/`--connect` options.
- `app/run/demo_scene.hpp` — `setup_runtime`'s three new trailing params + net-system wiring.
- `app/main.cpp` — transport construction from parsed options, host's extra entity +
  `wire_networked_demo_host` call, client's input provider, `--connect`'s empty-world path.
- `tests/unit/app/options_test.cpp` — new CLI parsing cases.
- `tests/CMakeLists.txt` — register `unit/app/net_host_demo_test.cpp`.
- `docs/project/next-steps.md` — mark N7 (and the whole networking pillar) done.

## Non-goals / follow-ups

- Authoritative/anti-cheat input validation, rate limiting, sequence numbers — this demo's
  input handling is deliberately trusting, matching N5's stated non-goals.
- Any UI/HUD/audio/particle replication — only the two demo-specific `NetReplicated` entities
  (the shared orbit cube and per-client avatars) exist; the rest of the default scene's
  entities are simply never marked, so `snapshot()` never touches them.
- Reconnect-with-state / session resumption on drop.
- A real internet-facing dedicated-server deployment story (firewalling, NAT, matchmaking) —
  out of scope for this project for the foreseeable future; this is a LAN/loopback demo.
- With N7 done, the **networking pillar (N1-N7) is COMPLETE** — no further slices are
  currently planned for this subsystem.
