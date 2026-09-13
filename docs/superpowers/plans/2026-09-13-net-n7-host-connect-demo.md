# Networking N7 (--host/--connect + demo) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire N1-N6 into `mosaic run --host <port>` / `--connect <host:port>` and a full-loop demo (shared orbiting cube + per-client avatar cubes moved by WASD), closing the loop N5 explicitly left open. This is the last Phase-4 networking slice.

**Architecture:** Two new CLI options construct a real `EnetTransport` and a `Role`; `setup_runtime` grows three trailing-defaulted params to wire `NetworkSystem`/`ReplicationSystem`/`InputSystem` when a transport is given; a standalone, headlessly-testable `wire_networked_demo_host` subscribes to the World's event bus for the host-only avatar spawn/move/despawn glue; `main.cpp` glues it all together.

**Tech Stack:** C++20, CLI11, Catch2, `engine/net/*` (N1-N5), `modules/net_enet` (N6). No new external dependencies.

**Spec:** `docs/superpowers/specs/2026-09-13-net-n7-host-connect-demo-design.md`

## Global Constraints

- `setup_runtime`'s three new parameters are trailing-defaulted — every existing call site (all current tests) must compile and behave identically unchanged.
- `wire_networked_demo_host` touches only the World/event-bus API (no CLI, no `Transport`) — it must stay headless-testable over two loopback-linked Worlds, exactly like the existing N5/N6 integration tests.
- The engine itself never interprets `NetworkInput`'s payload (N5's boundary) — only this app-level demo glue decodes the `"dx,dz"` string, and it does so defensively (a malformed payload is silently ignored, never a crash).
- `build_default_scene` is never modified — several existing tests assert an exact entity count against it (`demo_scene_test.cpp`); the host's extra networked entity is created separately in `main.cpp`.
- Follow existing code style: `mosaic::app`/`mosaic::engine::net` namespaces, `#pragma once`, Catch2 `TEST_CASE` with the file's established tags (`[cli]` for `options_test.cpp`, `[app]` for `demo_scene_test.cpp`/the new host-demo test).

---

### Task 1: CLI `--host`/`--connect` parsing

**Files:**
- Modify: `app/cli/options.hpp`
- Modify: `app/cli/options.cpp`
- Modify: `tests/unit/app/options_test.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces: `Options::host_port` (`std::optional<std::uint16_t>`), `Options::connect_address` (`std::optional<std::string>`, pre-validated as `<host>:<port>`), and a free function `split_host_port(const std::string& address) -> std::pair<std::string, std::uint16_t>` (declared in `options.hpp`, defined in `options.cpp`) that Task 4 (`main.cpp`) uses to build the actual `EnetTransport`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/app/options_test.cpp`:

```cpp
TEST_CASE("--host parses into a port", "[cli]")
{
    auto result = parse_args({"mosaic", "run", "--host", "7777"});
    REQUIRE_FALSE(result.exit_code.has_value());
    REQUIRE(result.options.host_port.has_value());
    REQUIRE(*result.options.host_port == std::uint16_t{7777});
    REQUIRE_FALSE(result.options.connect_address.has_value());
}

TEST_CASE("--connect parses a valid host:port", "[cli]")
{
    auto result = parse_args({"mosaic", "run", "--connect", "127.0.0.1:7777"});
    REQUIRE_FALSE(result.exit_code.has_value());
    REQUIRE(result.options.connect_address.has_value());
    REQUIRE(*result.options.connect_address == "127.0.0.1:7777");
    REQUIRE_FALSE(result.options.host_port.has_value());
}

TEST_CASE("--connect without a port is rejected", "[cli]")
{
    auto result = parse_args({"mosaic", "run", "--connect", "127.0.0.1"});
    REQUIRE(result.exit_code.has_value());
    REQUIRE(*result.exit_code != 0);
}

TEST_CASE("--connect with a non-numeric port is rejected", "[cli]")
{
    auto result = parse_args({"mosaic", "run", "--connect", "127.0.0.1:abc"});
    REQUIRE(result.exit_code.has_value());
    REQUIRE(*result.exit_code != 0);
}

TEST_CASE("--host and --connect together are rejected", "[cli]")
{
    auto result = parse_args({"mosaic", "run", "--host", "7777", "--connect", "127.0.0.1:7778"});
    REQUIRE(result.exit_code.has_value());
    REQUIRE(*result.exit_code != 0);
}

TEST_CASE("--host port zero is rejected", "[cli]")
{
    auto result = parse_args({"mosaic", "run", "--host", "0"});
    REQUIRE(result.exit_code.has_value());
    REQUIRE(*result.exit_code != 0);
}

TEST_CASE("split_host_port splits a valid address", "[cli]")
{
    const auto [host, port] = mosaic::app::split_host_port("127.0.0.1:7777");
    REQUIRE(host == "127.0.0.1");
    REQUIRE(port == std::uint16_t{7777});
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build/debug --target mosaic_unit_tests -j4 && ./build/debug/tests/mosaic_unit_tests "[cli]"`
Expected: build failure — `Options::host_port`/`connect_address`/`split_host_port` don't exist yet.

- [ ] **Step 3: Add the fields and `split_host_port` declaration to `options.hpp`**

In `app/cli/options.hpp`, add to the `Options` struct (near the other `run`-only fields, after `open_path`):

```cpp
    std::optional<std::uint16_t>  host_port;      // --host <port>
    std::optional<std::string>    connect_address; // --connect <host>:<port>
```

And, after the `Options`/`ParseResult` structs, before `parse`'s declaration, add:

```cpp
// Split a validated "<host>:<port>" address (as produced by --connect) into its parts.
// Only called on addresses that already passed the CLI validator, so the split always
// succeeds — this is not a general-purpose parser.
std::pair<std::string, std::uint16_t> split_host_port(const std::string& address);
```

(Add `#include <utility>` to `options.hpp` for `std::pair`.)

- [ ] **Step 4: Implement in `options.cpp`**

In `app/cli/options.cpp`, add the two options right after the existing `run_scene`/`run_path` block (before `run->add_option("--duration", ...)`):

```cpp
    std::uint16_t host_port = 0;
    std::string   connect_address;
    CLI::Option* host_opt = run->add_option(
        "--host", host_port, "Host a server on <port>");
    host_opt->check(CLI::Range(std::uint16_t{1}, std::uint16_t{65535}));
    CLI::Option* connect_opt = run->add_option(
        "--connect", connect_address, "Connect to <host>:<port> as a client");
    connect_opt->check(
        [](const std::string& val) -> std::string
        {
            const auto colon = val.rfind(':');
            if (colon == std::string::npos || colon + 1 == val.size())
            {
                return "expected <host>:<port>";
            }
            try
            {
                const int port = std::stoi(val.substr(colon + 1));
                if (port < 1 || port > 65535) { return "port out of range in <host>:<port>"; }
            }
            catch (const std::exception&)
            {
                return "invalid port in <host>:<port>";
            }
            return "";
        });
    host_opt->excludes(connect_opt);
    connect_opt->excludes(host_opt);
```

And add `#include <stdexcept>` to `options.cpp` for the `try`/`catch` (`std::stoi` throws `std::invalid_argument`/`std::out_of_range`, both derived from it).

At the end of `parse` (alongside the existing `if (!scene.empty()) { ... }` block, before `return result;`), add:

```cpp
    if (host_opt->count() > 0)
    {
        result.options.host_port = host_port;
    }
    if (connect_opt->count() > 0)
    {
        result.options.connect_address = connect_address;
    }
```

At the end of the file (after `parse`'s closing brace, still inside `namespace mosaic::app`), add:

```cpp
std::pair<std::string, std::uint16_t> split_host_port(const std::string& address)
{
    const auto colon = address.rfind(':');
    const std::string host = address.substr(0, colon);
    const int port = std::stoi(address.substr(colon + 1));
    return {host, static_cast<std::uint16_t>(port)};
}
```

- [ ] **Step 5: Run to verify it passes**

Run: `cmake --build build/debug --target mosaic_unit_tests -j4 && ./build/debug/tests/mosaic_unit_tests "[cli]"`
Expected: PASS, all `[cli]` cases including the 7 new ones.

- [ ] **Step 6: Commit**

```bash
git add app/cli/options.hpp app/cli/options.cpp tests/unit/app/options_test.cpp
git commit -m "feat(cli): --host/--connect options + split_host_port (networking N7)"
```

---

### Task 2: Host demo glue (`wire_networked_demo_host`)

**Files:**
- Create: `app/run/net_host_demo.hpp`
- Create: `tests/unit/app/net_host_demo_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `mosaic::engine::net::NetworkEvent`, `mosaic::engine::net::NetworkInput` (pre-existing bus events), `mosaic::engine::net::NetReplicated`, `mosaic::engine::transform::Transform`, `mosaic::engine::render::Material` (all pre-existing).
- Produces: `mosaic::app::wire_networked_demo_host(mosaic::engine::World& world)`. Task 4 (`main.cpp`) calls this once, only on the host path.

- [ ] **Step 1: Register the new test file**

In `tests/CMakeLists.txt`, add `unit/app/net_host_demo_test.cpp` to the source list next to the other `unit/app/*.cpp` entries (alphabetically, near `unit/app/net_replicated_test.cpp` if present, otherwise anywhere in that block — match the file's existing ordering convention).

- [ ] **Step 2: Write the failing tests**

Create `tests/unit/app/net_host_demo_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "app/run/net_host_demo.hpp"
#include "engine/net/net_replicated.hpp"
#include "engine/net/network_event.hpp"
#include "engine/render/material.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"

using mosaic::app::wire_networked_demo_host;
using mosaic::engine::World;
using mosaic::engine::net::NetReplicated;
using mosaic::engine::net::NetworkEvent;
using mosaic::engine::net::NetworkInput;
using mosaic::engine::transform::Transform;

TEST_CASE("PeerJoined spawns exactly one NetReplicated avatar", "[app][net]")
{
    World world;
    wire_networked_demo_host(world);

    int before = 0;
    world.for_each<NetReplicated>([&](World::EntityID, NetReplicated&) { ++before; });
    REQUIRE(before == 0);

    world.events().emit(NetworkEvent{NetworkEvent::Type::PeerJoined, 1u});

    int after = 0;
    world.for_each<NetReplicated>([&](World::EntityID, NetReplicated&) { ++after; });
    REQUIRE(after == 1);
}

TEST_CASE("NetworkInput moves the sender's avatar by the parsed delta", "[app][net]")
{
    World world;
    wire_networked_demo_host(world);
    world.events().emit(NetworkEvent{NetworkEvent::Type::PeerJoined, 1u});

    World::EntityID avatar;
    Transform before;
    world.for_each<NetReplicated>([&](World::EntityID e, NetReplicated&)
    {
        avatar = e;
        before = *world.get_component<Transform>(e);
    });

    world.events().emit(NetworkInput{1u, "0.5,-1.5"});

    const Transform* after = world.get_component<Transform>(avatar);
    REQUIRE(after->position.x == before.position.x + 0.5f);
    REQUIRE(after->position.z == before.position.z - 1.5f);
}

TEST_CASE("a malformed NetworkInput payload is silently ignored", "[app][net]")
{
    World world;
    wire_networked_demo_host(world);
    world.events().emit(NetworkEvent{NetworkEvent::Type::PeerJoined, 1u});

    World::EntityID avatar;
    Transform before;
    world.for_each<NetReplicated>([&](World::EntityID e, NetReplicated&)
    {
        avatar = e;
        before = *world.get_component<Transform>(e);
    });

    world.events().emit(NetworkInput{1u, "not-a-delta"});

    const Transform* after = world.get_component<Transform>(avatar);
    REQUIRE(after->position.x == before.position.x);
    REQUIRE(after->position.z == before.position.z);
}

TEST_CASE("PeerLeft despawns that session's avatar", "[app][net]")
{
    World world;
    wire_networked_demo_host(world);
    world.events().emit(NetworkEvent{NetworkEvent::Type::PeerJoined, 1u});

    int before = 0;
    world.for_each<NetReplicated>([&](World::EntityID, NetReplicated&) { ++before; });
    REQUIRE(before == 1);

    world.events().emit(NetworkEvent{NetworkEvent::Type::PeerLeft, 1u});
    world.tick(0.016f); // destroy_entity is deferred; commit it

    int after = 0;
    world.for_each<NetReplicated>([&](World::EntityID, NetReplicated&) { ++after; });
    REQUIRE(after == 0);
}

TEST_CASE("NetworkInput for a session with no avatar (e.g. after PeerLeft) is a no-op", "[app][net]")
{
    World world;
    wire_networked_demo_host(world);
    world.events().emit(NetworkEvent{NetworkEvent::Type::PeerJoined, 1u});
    world.events().emit(NetworkEvent{NetworkEvent::Type::PeerLeft, 1u});
    world.tick(0.016f);

    // No crash, and no entity springs into existence.
    world.events().emit(NetworkInput{1u, "1.0,1.0"});
    int count = 0;
    world.for_each<NetReplicated>([&](World::EntityID, NetReplicated&) { ++count; });
    REQUIRE(count == 0);
}
```

- [ ] **Step 3: Run to verify it fails**

Run: `cmake --build build/debug --target mosaic_unit_tests -j4 && ./build/debug/tests/mosaic_unit_tests "[app][net]"`
Expected: build failure — `app/run/net_host_demo.hpp` doesn't exist yet.

- [ ] **Step 4: Write `app/run/net_host_demo.hpp`**

```cpp
#pragma once

#include <cstdio>
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
// the N5/N6 integration tests, over two loopback-linked Worlds (or, as here, a single World
// with events emitted directly).
inline void wire_networked_demo_host(mosaic::engine::World& world)
{
    namespace net = mosaic::engine::net;
    using mosaic::engine::render::Material;
    using mosaic::engine::transform::Transform;

    auto avatars = std::make_shared<
        std::unordered_map<std::uint32_t, mosaic::engine::World::EntityID>>();

    world.events().subscribe<net::NetworkEvent>(
        [&world, avatars](const net::NetworkEvent& e)
        {
            if (e.type == net::NetworkEvent::Type::PeerJoined)
            {
                const auto entity = world.create_entity();
                world.add_component<net::NetReplicated>(entity, net::NetReplicated{0});
                Transform t;
                t.position = {static_cast<float>(e.peer % 5) - 2.0f, 1.0f, -3.0f};
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

- [ ] **Step 5: Run to verify it passes**

Run: `cmake --build build/debug --target mosaic_unit_tests -j4 && ./build/debug/tests/mosaic_unit_tests "[app][net]"`
Expected: PASS, all 5 new cases.

- [ ] **Step 6: Commit**

```bash
git add app/run/net_host_demo.hpp tests/unit/app/net_host_demo_test.cpp tests/CMakeLists.txt
git commit -m "feat(net): host demo glue — avatar spawn/move/despawn (networking N7)"
```

---

### Task 3: `setup_runtime` network wiring

**Files:**
- Modify: `app/run/demo_scene.hpp`
- Modify: `tests/unit/app/demo_scene_test.cpp`

**Interfaces:**
- Consumes: `mosaic::engine::net::Role`, `mosaic::engine::net::Transport`, `mosaic::engine::net::NetworkSystem`, `mosaic::engine::net::ReplicationSystem`, `mosaic::engine::net::InputSystem` (all pre-existing, N2/N3-follow/N5).
- Produces: `setup_runtime`'s three new trailing params. Task 4 (`main.cpp`) is the only caller that passes non-default values for them.

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/app/demo_scene_test.cpp`:

```cpp
TEST_CASE("setup_runtime wires NetworkSystem/ReplicationSystem/InputSystem when given a transport",
          "[app][net]")
{
    World world;
    AssetRegistry registry;
    mosaic::foundation::reflection::TypeRegistry     reg;
    mosaic::engine::reflection::ComponentReflection  types;
    mosaic::engine::reflection::register_builtin_components(reg, types);

    auto [server_tx, client_tx] = mosaic::engine::net::make_loopback_pair();

    setup_runtime(
        world, registry, types,
        [] { return mosaic::script::python::create_python_script_engine(); },
        mosaic::engine::script::ScriptHost{}, false, nullptr, {},
        mosaic::engine::net::Role::Server, server_tx);

    REQUIRE(world.get_system<mosaic::engine::net::NetworkSystem>() != nullptr);
    REQUIRE(world.get_system<mosaic::engine::net::ReplicationSystem>() != nullptr);
    REQUIRE(world.get_system<mosaic::engine::net::InputSystem>() != nullptr);
}

TEST_CASE("setup_runtime without a transport wires no networking", "[app]")
{
    World world;
    AssetRegistry registry;
    mosaic::foundation::reflection::TypeRegistry     reg;
    mosaic::engine::reflection::ComponentReflection  types;
    mosaic::engine::reflection::register_builtin_components(reg, types);

    setup_runtime(
        world, registry, types,
        [] { return mosaic::script::python::create_python_script_engine(); });

    REQUIRE(world.get_system<mosaic::engine::net::NetworkSystem>() == nullptr);
}
```

Add these includes to `tests/unit/app/demo_scene_test.cpp`: `"engine/net/loopback_transport.hpp"`,
`"engine/net/network_system.hpp"`, `"engine/net/replication_system.hpp"`,
`"engine/net/input_system.hpp"`, `"engine/script/script_host.hpp"`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build/debug --target mosaic_unit_tests -j4 && ./build/debug/tests/mosaic_unit_tests "[app]"`
Expected: build failure — `setup_runtime` doesn't accept the extra arguments yet.

- [ ] **Step 3: Extend `setup_runtime`'s signature and body**

In `app/run/demo_scene.hpp`, add includes near the top (alongside the other `engine/*` includes):

```cpp
#include "engine/net/input_system.hpp"
#include "engine/net/network_system.hpp"
#include "engine/net/replication_system.hpp"
#include "engine/net/transport.hpp"
```

Change `setup_runtime`'s signature:

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

Immediately before the final `return RuntimeScene{...}`, add:

```cpp
    // Networking (N7): only wired when a transport is actually injected — headless/non-
    // networked runs and every pre-existing call site are unaffected.
    if (net_transport != nullptr)
    {
        auto& net = world.add_system<mosaic::engine::net::NetworkSystem>(net_role, net_transport);
        world.add_system<mosaic::engine::net::ReplicationSystem>(net_role, &net, &reflection);
        world.add_system<mosaic::engine::net::InputSystem>(net_role, &net, input_provider);
    }
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build/debug --target mosaic_unit_tests -j4 && ./build/debug/tests/mosaic_unit_tests "[app]"`
Expected: PASS, all `[app]` cases including the 2 new ones.

Then run the full suite to confirm no regressions from the signature change:

Run: `./build/debug/tests/mosaic_unit_tests`
Expected: PASS except the pre-existing flaky `script_system` SIGSEGV.

- [ ] **Step 5: Commit**

```bash
git add app/run/demo_scene.hpp tests/unit/app/demo_scene_test.cpp
git commit -m "feat(net): setup_runtime wires NetworkSystem/ReplicationSystem/InputSystem (networking N7)"
```

---

### Task 4: `main.cpp` integration

**Files:**
- Modify: `app/main.cpp`

**Interfaces:**
- Consumes: `Options::host_port`/`connect_address` (Task 1), `split_host_port` (Task 1), `wire_networked_demo_host` (Task 2), `setup_runtime`'s new params (Task 3), `mosaic::net::enet::create_enet_server`/`create_enet_client` (N6, `modules/net_enet/enet_transport.hpp`).
- Produces: the actual `mosaic run --host`/`--connect` behavior. Nothing later in this plan depends on this beyond the manual verification.

- [ ] **Step 1: Add includes**

In `app/main.cpp`, add:

```cpp
#include "app/run/net_host_demo.hpp"
#include "engine/net/network_event.hpp"
#include "engine/script/script_component.hpp"
#include "modules/net_enet/enet_transport.hpp"
```

(`engine/script/script_component.hpp` is for `ScriptComponent` — check whether it's already included transitively via `demo_scene.hpp`; if the build already compiles without it, skip adding it. `mosaic::app::kOrbitScript` is already visible via `demo_scene.hpp`, already included.)

- [ ] **Step 2: Construct the transport and role after parsing options**

In `main.cpp`, right after `const Options& options = pr.options;` (before `engine::World world;`), add:

```cpp
    mosaic::engine::net::Role net_role = mosaic::engine::net::Role::Client;
    std::shared_ptr<mosaic::engine::net::Transport> net_transport;
    if (options.host_port)
    {
        net_role = mosaic::engine::net::Role::Server;
        net_transport = mosaic::net::enet::create_enet_server(*options.host_port, /*max_peers=*/8);
        if (net_transport == nullptr)
        {
            reporter->error("failed to host on port " + std::to_string(*options.host_port));
            return 1;
        }
    }
    else if (options.connect_address)
    {
        const auto [host, port] = split_host_port(*options.connect_address);
        net_transport = mosaic::net::enet::create_enet_client(host, port);
        if (net_transport == nullptr)
        {
            reporter->error("failed to connect to " + *options.connect_address);
            return 1;
        }
    }
```

- [ ] **Step 3: Skip normal scene population when connecting as a client**

Find the existing `if (!populate_world(world, options, types))` block and wrap it:

```cpp
    if (!options.connect_address)
    {
        if (!populate_world(world, options, types))
        {
            reporter->error("failed to load scene: " + options.scene.value_or(std::string{}));
            return 1;
        }
    }
    else if (options.scene || options.open_path)
    {
        mosaic::foundation::log::info(
            "net", "--connect starts from an empty world; ignoring the given scene/path");
    }
```

- [ ] **Step 4: Build the client's input provider (before the `setup_runtime` call)**

Right before the `setup_runtime(...)` call, add:

```cpp
    std::function<std::vector<std::string>()> net_input_provider;
    if (net_role == mosaic::engine::net::Role::Client && net_transport != nullptr)
    {
        net_input_provider = [&glfw_window]() -> std::vector<std::string>
        {
            if (glfw_window == nullptr) { return {}; }
            float dx = 0.0f, dz = 0.0f;
            constexpr float speed = 0.05f;
            if (glfwGetKey(glfw_window, GLFW_KEY_D) == GLFW_PRESS) { dx += speed; }
            if (glfwGetKey(glfw_window, GLFW_KEY_A) == GLFW_PRESS) { dx -= speed; }
            if (glfwGetKey(glfw_window, GLFW_KEY_S) == GLFW_PRESS) { dz += speed; }
            if (glfwGetKey(glfw_window, GLFW_KEY_W) == GLFW_PRESS) { dz -= speed; }
            if (dx == 0.0f && dz == 0.0f) { return {}; }
            return {std::to_string(dx) + "," + std::to_string(dz)};
        };
    }
```

(`glfw_window` is already declared earlier in `main.cpp`, as `GLFWwindow* glfw_window =
nullptr;`, right before `render_device`/`asset_registry` and well before the existing
`setup_runtime` call this step's code precedes — no reordering needed, this lambda simply
captures it by reference like the existing pointer-provider lambda a few lines below does.)

- [ ] **Step 5: Pass the new arguments to `setup_runtime`**

Extend the existing `setup_runtime(...)` call (the one assigned to `render_system`) with the
three new trailing arguments:

```cpp
    auto render_system =
        setup_runtime(world, asset_registry, types,
                      [] { return mosaic::script::python::create_python_script_engine(); },
                      script_host, options.script_thread, std::move(audio),
                      [&glfw_window]() -> mosaic::engine::ui::PointerState
                      {
                          if (glfw_window == nullptr) { return {}; }
                          int ww = 0, wh = 0;
                          glfwGetWindowSize(glfw_window, &ww, &wh);
                          if (ww <= 0 || wh <= 0) { return {}; }
                          double mx = 0.0, my = 0.0;
                          glfwGetCursorPos(glfw_window, &mx, &my);
                          mosaic::engine::ui::PointerState p;
                          p.x = std::clamp(static_cast<float>(mx / ww), 0.0f, 1.0f);
                          p.y = std::clamp(static_cast<float>(my / wh), 0.0f, 1.0f);
                          p.down = glfwGetMouseButton(glfw_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
                          return p;
                      },
                      net_role, net_transport, net_input_provider)
            .render_system;
```

- [ ] **Step 6: Add the host's shared entity + demo glue, after `setup_runtime`**

Right after the `setup_runtime(...)` call above (still before the `if (options.assets_dir)`
block), add:

```cpp
    if (net_role == mosaic::engine::net::Role::Server && net_transport != nullptr)
    {
        const auto e = world.create_entity();
        world.add_component<mosaic::engine::net::NetReplicated>(e, mosaic::engine::net::NetReplicated{0});
        mosaic::engine::transform::Transform t;
        t.position = {0.0f, 3.0f, 0.0f};
        world.add_component<mosaic::engine::transform::Transform>(e, t);
        mosaic::engine::render::Material m;
        m.base_color = {0.9f, 0.7f, 0.2f};
        world.add_component<mosaic::engine::render::Material>(e, m);
        world.add_component<mosaic::engine::script::ScriptComponent>(
            e, mosaic::engine::script::ScriptComponent{mosaic::app::kOrbitScript});
        mosaic::app::wire_networked_demo_host(world);
    }
```

(`#include "engine/net/net_replicated.hpp"` if not already transitively available — check by
building; `demo_scene.hpp`'s own new includes from Task 3 already pull in
`engine/net/transport.hpp` etc., but `NetReplicated` itself may need an explicit include here.)

- [ ] **Step 7: Build**

Run: `cmake --build build/debug --target mosaic_app -j4`
Expected: builds cleanly. Fix any missing-include errors by adding the specific header the
compiler names (do not guess — the exact error names the missing symbol and its header).

Run the full test suite too, to make sure nothing else regressed:

Run: `cmake --build build/debug --target mosaic_unit_tests -j4 && ./build/debug/tests/mosaic_unit_tests`
Expected: PASS except the pre-existing flaky `script_system` SIGSEGV.

- [ ] **Step 8: Manual smoke verification (host+client lifecycle, headless — no window needed)**

This exercises connect/avatar-spawn/avatar-despawn end to end over real loopback UDP without
needing a display (WASD movement itself needs `--window`+a real display, which is the
genuinely manual/maintainer-only part — this step is not that, it's an automated-by-you
sanity check before the real manual step):

```bash
./build/debug/app/mosaic run --host 17777 --json --duration 3 &
HOST_PID=$!
sleep 0.3
./build/debug/app/mosaic run --connect 127.0.0.1:17777 --json --duration 1
wait $HOST_PID
```

Expected: the client process exits cleanly (code 0) after ~1s; the host process's stderr log
(from `foundation::log`) shows `peer 1 joined; spawned avatar` shortly after the client
connects, and `peer 1 left; despawned avatar` shortly after the client exits; the host process
itself then exits cleanly (code 0) after its own `--duration 3`.

- [ ] **Step 9: Commit**

```bash
git add app/main.cpp
git commit -m "feat(net): wire --host/--connect into main.cpp (networking N7)"
```

---

### Task 5: Update project docs

**Files:**
- Modify: `docs/project/next-steps.md`

**Interfaces:** none (docs only).

- [ ] **Step 1: Update `docs/project/next-steps.md`**

Replace the "Last updated" line and status paragraph to mark N7 done, in the same style as
the existing N4b/N5/N6 entries. Summarize: `--host <port>`/`--connect <host:port>` CLI flags;
`setup_runtime`'s three new trailing params; `wire_networked_demo_host`'s spawn/move/despawn
glue; the shared orbiting cube + per-client avatar demo. Note explicitly which parts were
verified how: CLI parsing + `wire_networked_demo_host` are headless/automated (Catch2); the
host+client connect/avatar-lifecycle was verified headlessly via two real local processes over
loopback UDP (no display needed); the actual WASD-driven avatar movement needs `--window` and
a real display, and is the one part that stayed genuinely maintainer-verified-only in this
whole pillar. Get accurate test counts by running the actual suite rather than guessing.
State plainly: **the networking pillar (N1-N7) is now COMPLETE.**

- [ ] **Step 2: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "docs(net): mark networking N7 done; networking pillar (N1-N7) COMPLETE"
```
