# Blender Live-Link Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Blender addon streams live transform edits to Studio over a localhost TCP socket, updating the matching (by `Name`) entity's `Transform` in real time — the roadmap's "basic Blender bridge (live-link preview)".

**Architecture:** `studio/live_link.hpp` is pure parse+apply logic (no sockets, headlessly testable). `studio/live_link_server.hpp`/`.cpp` is a small non-blocking POSIX TCP listener, tested with a real localhost loopback pair in-process. `StudioApp` wires a toolbar toggle and a per-frame poll+apply loop. A separate, unbuilt Python file is the Blender-side addon.

**Tech Stack:** C++20, POSIX sockets (Linux only), the existing `foundation::serialization` JSON codec, Catch2. Blender addon: pure Python stdlib (`socket`, `bpy.app.handlers`), no C++ involvement.

**Spec:** `docs/superpowers/specs/2026-09-13-blender-live-link-design.md`

## Global Constraints

- One-way sync only: Blender → Studio. No Studio → Blender direction.
- Transform-only: position/rotation/scale. No mesh/material/geometry sync, no new-object creation.
- Linux-only socket layer for now — no Winsock.
- Wire values are already in Mosaic's convention (right-handed, Y-up) by the time Studio sees them; the Blender addon does the Z-up → Y-up conversion, never Studio.
- Single connection at a time; a new `accept()` replaces any prior peer.
- No `CommandStack` involvement — matches Studio's existing (command-less) live transform editing.
- `apply_live_link_update` never partially applies: zero or more-than-one `Name` match is a no-op, not a "first match wins".

---

### Task 1: `studio/live_link.hpp` — parse + apply (no sockets)

**Files:**
- Create: `studio/live_link.hpp`
- Create: `tests/unit/studio/live_link_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `mosaic::engine::Name` (`engine/world/name.hpp`), `mosaic::engine::transform::Transform` (`engine/transform/transform.hpp`), `mosaic::engine::World`, `foundation::serialization::from_json`/`Value`/`Table`/`Array` (all pre-existing).
- Produces: `mosaic::studio::LiveLinkUpdate` struct, `mosaic::studio::parse_live_link_message(const std::string&) -> std::optional<LiveLinkUpdate>`, `mosaic::studio::apply_live_link_update(World&, const LiveLinkUpdate&) -> bool`. Task 3 (Studio integration) calls both; Task 2 (socket layer) does not depend on this file at all.

- [ ] **Step 1: Register the new test file**

In `tests/CMakeLists.txt`, add `unit/studio/live_link_test.cpp` to the source list next to
the other `unit/studio/*.cpp` entries (e.g. right after `unit/studio/hierarchy_test.cpp`).

- [ ] **Step 2: Write the failing tests**

Create `tests/unit/studio/live_link_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "engine/transform/transform.hpp"
#include "engine/world/name.hpp"
#include "engine/world/world.hpp"
#include "studio/live_link.hpp"

using mosaic::engine::Name;
using mosaic::engine::World;
using mosaic::engine::transform::Transform;
using mosaic::studio::apply_live_link_update;
using mosaic::studio::LiveLinkUpdate;
using mosaic::studio::parse_live_link_message;

TEST_CASE("parse_live_link_message parses a valid line", "[studio]")
{
    const auto update = parse_live_link_message(
        R"({"name":"Cube","position":[1.0,2.0,3.0],"rotation":[0.0,0.0,0.0,1.0],"scale":[1.0,1.0,1.0]})");
    REQUIRE(update.has_value());
    REQUIRE(update->name == "Cube");
    REQUIRE(update->position.x == 1.0f);
    REQUIRE(update->position.y == 2.0f);
    REQUIRE(update->position.z == 3.0f);
    REQUIRE(update->rotation.w == 1.0f);
    REQUIRE(update->scale.x == 1.0f);
}

TEST_CASE("parse_live_link_message accepts a mix of int and float literals", "[studio]")
{
    const auto update = parse_live_link_message(
        R"({"name":"Cube","position":[1,2.5,3],"rotation":[0,0,0,1],"scale":[1,1,1]})");
    REQUIRE(update.has_value());
    REQUIRE(update->position.x == 1.0f);
    REQUIRE(update->position.y == 2.5f);
    REQUIRE(update->rotation.w == 1.0f);
}

TEST_CASE("parse_live_link_message rejects malformed JSON", "[studio]")
{
    REQUIRE_FALSE(parse_live_link_message("not json").has_value());
}

TEST_CASE("parse_live_link_message rejects a non-object root", "[studio]")
{
    REQUIRE_FALSE(parse_live_link_message("[1,2,3]").has_value());
}

TEST_CASE("parse_live_link_message rejects a missing name", "[studio]")
{
    REQUIRE_FALSE(parse_live_link_message(
        R"({"position":[1,2,3],"rotation":[0,0,0,1],"scale":[1,1,1]})").has_value());
}

TEST_CASE("parse_live_link_message rejects a missing position", "[studio]")
{
    REQUIRE_FALSE(parse_live_link_message(
        R"({"name":"Cube","rotation":[0,0,0,1],"scale":[1,1,1]})").has_value());
}

TEST_CASE("parse_live_link_message rejects a missing rotation", "[studio]")
{
    REQUIRE_FALSE(parse_live_link_message(
        R"({"name":"Cube","position":[1,2,3],"scale":[1,1,1]})").has_value());
}

TEST_CASE("parse_live_link_message rejects a missing scale", "[studio]")
{
    REQUIRE_FALSE(parse_live_link_message(
        R"({"name":"Cube","position":[1,2,3],"rotation":[0,0,0,1]})").has_value());
}

TEST_CASE("apply_live_link_update overwrites the matching entity's Transform", "[studio]")
{
    World world;
    const auto e = world.create_entity();
    world.add_component<Name>(e, Name{"Cube"});
    world.add_component<Transform>(e, Transform{});

    LiveLinkUpdate update;
    update.name = "Cube";
    update.position = {1.0f, 2.0f, 3.0f};
    update.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    update.scale = {2.0f, 2.0f, 2.0f};

    REQUIRE(apply_live_link_update(world, update));

    const Transform* t = world.get_component<Transform>(e);
    REQUIRE(t->position.x == 1.0f);
    REQUIRE(t->position.y == 2.0f);
    REQUIRE(t->position.z == 3.0f);
    REQUIRE(t->scale.x == 2.0f);
}

TEST_CASE("apply_live_link_update returns false when no entity matches", "[studio]")
{
    World world;
    const auto e = world.create_entity();
    world.add_component<Name>(e, Name{"Cube"});
    Transform before;
    before.position = {9.0f, 9.0f, 9.0f};
    world.add_component<Transform>(e, before);

    LiveLinkUpdate update;
    update.name = "DoesNotExist";
    REQUIRE_FALSE(apply_live_link_update(world, update));

    // Untouched.
    REQUIRE(world.get_component<Transform>(e)->position.x == 9.0f);
}

TEST_CASE("apply_live_link_update returns false and applies nothing when the name is ambiguous", "[studio]")
{
    World world;
    const auto a = world.create_entity();
    world.add_component<Name>(a, Name{"Cube"});
    Transform ta; ta.position = {1.0f, 0.0f, 0.0f};
    world.add_component<Transform>(a, ta);

    const auto b = world.create_entity();
    world.add_component<Name>(b, Name{"Cube"});
    Transform tb; tb.position = {2.0f, 0.0f, 0.0f};
    world.add_component<Transform>(b, tb);

    LiveLinkUpdate update;
    update.name = "Cube";
    update.position = {5.0f, 5.0f, 5.0f};

    REQUIRE_FALSE(apply_live_link_update(world, update));
    REQUIRE(world.get_component<Transform>(a)->position.x == 1.0f);
    REQUIRE(world.get_component<Transform>(b)->position.x == 2.0f);
}
```

- [ ] **Step 3: Run to verify it fails**

Run: `cmake --build build/debug --target mosaic_unit_tests -j4 && ./build/debug/tests/mosaic_unit_tests "[studio]"`
Expected: build failure — `studio/live_link.hpp` doesn't exist yet.

- [ ] **Step 4: Write `studio/live_link.hpp`**

```cpp
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "engine/transform/transform.hpp"
#include "engine/world/name.hpp"
#include "engine/world/world.hpp"
#include "foundation/math/quat.hpp"
#include "foundation/math/vec3.hpp"
#include "foundation/serialization/json_backend.hpp"
#include "foundation/serialization/value.hpp"

namespace mosaic::studio
{
struct LiveLinkUpdate
{
    std::string                    name;
    mosaic::foundation::math::Vec3 position;
    mosaic::foundation::math::Quat rotation;
    mosaic::foundation::math::Vec3 scale;
};

namespace detail
{
namespace ser = mosaic::foundation::serialization;

// A JSON number may come back as int64, uint64, or double depending on whether the
// source literal had a fractional part. Normalizes any of the three to float.
inline std::optional<float> as_float(const ser::Value& v)
{
    if (v.is<double>())        { return static_cast<float>(v.as<double>()); }
    if (v.is<std::int64_t>())  { return static_cast<float>(v.as<std::int64_t>()); }
    if (v.is<std::uint64_t>()) { return static_cast<float>(v.as<std::uint64_t>()); }
    return std::nullopt;
}

inline std::optional<mosaic::foundation::math::Vec3> as_vec3(const ser::Value* v)
{
    if (v == nullptr || !v->is<ser::Array>()) { return std::nullopt; }
    const ser::Array& a = v->as<ser::Array>();
    if (a.size() != 3) { return std::nullopt; }
    const auto x = as_float(a[0]);
    const auto y = as_float(a[1]);
    const auto z = as_float(a[2]);
    if (!x || !y || !z) { return std::nullopt; }
    return mosaic::foundation::math::Vec3{*x, *y, *z};
}

inline std::optional<mosaic::foundation::math::Quat> as_quat(const ser::Value* v)
{
    if (v == nullptr || !v->is<ser::Array>()) { return std::nullopt; }
    const ser::Array& a = v->as<ser::Array>();
    if (a.size() != 4) { return std::nullopt; }
    const auto x = as_float(a[0]);
    const auto y = as_float(a[1]);
    const auto z = as_float(a[2]);
    const auto w = as_float(a[3]);
    if (!x || !y || !z || !w) { return std::nullopt; }
    return mosaic::foundation::math::Quat{*x, *y, *z, *w};
}
} // namespace detail

// Parses one newline-delimited JSON line into a LiveLinkUpdate. Returns nullopt on
// malformed JSON, a non-object root, or a missing/wrong-typed field — never throws.
inline std::optional<LiveLinkUpdate> parse_live_link_message(const std::string& line)
{
    namespace ser = mosaic::foundation::serialization;
    ser::Value root;
    try
    {
        root = ser::from_json(line);
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
    if (!root.is<ser::Table>()) { return std::nullopt; }
    const ser::Table& t = root.as<ser::Table>();

    const ser::Value* name_v = t.find("name");
    if (name_v == nullptr || !name_v->is<std::string>()) { return std::nullopt; }

    const auto position = detail::as_vec3(t.find("position"));
    const auto rotation = detail::as_quat(t.find("rotation"));
    const auto scale = detail::as_vec3(t.find("scale"));
    if (!position || !rotation || !scale) { return std::nullopt; }

    LiveLinkUpdate update;
    update.name = name_v->as<std::string>();
    update.position = *position;
    update.rotation = *rotation;
    update.scale = *scale;
    return update;
}

// Finds the entity whose Name matches update.name and overwrites its Transform in
// place. Returns false if zero or more-than-one entity matches (ambiguous or
// unknown name) and touches nothing in that case.
inline bool apply_live_link_update(mosaic::engine::World& world, const LiveLinkUpdate& update)
{
    std::vector<mosaic::engine::World::EntityID> matches;
    world.for_each<mosaic::engine::Name>(
        [&](mosaic::engine::World::EntityID e, mosaic::engine::Name& n)
        {
            if (n.value == update.name) { matches.push_back(e); }
        });
    if (matches.size() != 1) { return false; }

    mosaic::engine::transform::Transform* t =
        world.get_component<mosaic::engine::transform::Transform>(matches[0]);
    if (t == nullptr) { return false; }

    t->position = update.position;
    t->rotation = update.rotation;
    t->scale = update.scale;
    return true;
}
} // namespace mosaic::studio
```

- [ ] **Step 5: Run to verify it passes**

Run: `cmake --build build/debug --target mosaic_unit_tests -j4 && ./build/debug/tests/mosaic_unit_tests "[studio]"`
Expected: PASS, all new cases plus every pre-existing `[studio]` case.

- [ ] **Step 6: Commit**

```bash
git add studio/live_link.hpp tests/unit/studio/live_link_test.cpp tests/CMakeLists.txt
git commit -m "feat(studio): live-link message parsing + Transform apply (Blender Live-Link)"
```

---

### Task 2: `studio/live_link_server.hpp`/`.cpp` — the socket layer

**Files:**
- Create: `studio/live_link_server.hpp`
- Create: `studio/live_link_server.cpp`
- Create: `tests/unit/studio/live_link_server_test.cpp`
- Modify: `studio/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing from Task 1 — this file has zero dependency on `live_link.hpp`.
- Produces: `mosaic::studio::LiveLinkState` (`Off`/`Listening`/`Connected`), `mosaic::studio::kDefaultLiveLinkPort` (`std::uint16_t`, value `6828`), and `mosaic::studio::LiveLinkServer` with `bool start(std::uint16_t port)`, `void stop()`, `LiveLinkState state() const`, `std::vector<std::string> poll()`. Task 3 (Studio integration) is the only consumer.

- [ ] **Step 1: Register the new test file and source file**

In `tests/CMakeLists.txt`, add `unit/studio/live_link_server_test.cpp` right after
`unit/studio/live_link_test.cpp`.

In `studio/CMakeLists.txt`, add `live_link_server.cpp` to the `add_library(mosaic_studio
STATIC ...)` source list.

- [ ] **Step 2: Write the failing tests**

Create `tests/unit/studio/live_link_server_test.cpp`:

```cpp
#include <chrono>
#include <cstring>
#include <thread>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <catch2/catch_test_macros.hpp>

#include "studio/live_link_server.hpp"

using mosaic::studio::LiveLinkServer;
using mosaic::studio::LiveLinkState;

namespace
{
// A minimal blocking client socket connected to 127.0.0.1:port, for test use only.
class TestClient
{
public:
    explicit TestClient(std::uint16_t port)
    {
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    }
    ~TestClient() { if (fd_ >= 0) { close(fd_); } }

    void send_text(const std::string& s) { send(fd_, s.data(), s.size(), 0); }
    void close_now() { close(fd_); fd_ = -1; }

private:
    int fd_ = -1;
};

// poll() is non-blocking; give the kernel a moment to deliver connect()/send()
// bytes on loopback before asserting. Loopback delivery is near-instant, unlike
// the cross-process real-UDP-over-a-real-network-stack delay networking N6/N7
// found in this sandbox — this is same-host TCP, no such margin needed, but a
// short bounded retry keeps the test robust against scheduling jitter.
template <class Pred>
void poll_until(LiveLinkServer& server, Pred pred, std::vector<std::string>* out = nullptr,
                int max_attempts = 200, int sleep_ms = 5)
{
    for (int i = 0; i < max_attempts; ++i)
    {
        auto lines = server.poll();
        if (out != nullptr)
        {
            for (auto& l : lines) { out->push_back(std::move(l)); }
        }
        if (pred()) { return; }
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }
}
} // namespace

TEST_CASE("LiveLinkServer starts Off and start() moves it to Listening", "[studio]")
{
    LiveLinkServer server;
    REQUIRE(server.state() == LiveLinkState::Off);
    REQUIRE(server.start(34901));
    REQUIRE(server.state() == LiveLinkState::Listening);
    server.stop();
    REQUIRE(server.state() == LiveLinkState::Off);
}

TEST_CASE("LiveLinkServer transitions to Connected when a client connects", "[studio]")
{
    LiveLinkServer server;
    REQUIRE(server.start(34902));

    TestClient client(34902);
    poll_until(server, [&] { return server.state() == LiveLinkState::Connected; });

    REQUIRE(server.state() == LiveLinkState::Connected);
}

TEST_CASE("LiveLinkServer poll() returns a complete line sent in one write", "[studio]")
{
    LiveLinkServer server;
    REQUIRE(server.start(34903));
    TestClient client(34903);
    poll_until(server, [&] { return server.state() == LiveLinkState::Connected; });

    client.send_text("hello\n");

    std::vector<std::string> received;
    poll_until(server, [&] { return !received.empty(); }, &received);

    REQUIRE(received.size() == 1);
    REQUIRE(received[0] == "hello");
}

TEST_CASE("LiveLinkServer buffers a line split across two sends", "[studio]")
{
    LiveLinkServer server;
    REQUIRE(server.start(34904));
    TestClient client(34904);
    poll_until(server, [&] { return server.state() == LiveLinkState::Connected; });

    client.send_text("hel");
    std::vector<std::string> received;
    poll_until(server, [&] { server.poll(); return true; }, nullptr, 20); // let the partial arrive
    client.send_text("lo\n");
    poll_until(server, [&] { return !received.empty(); }, &received);

    REQUIRE(received.size() == 1);
    REQUIRE(received[0] == "hello");
}

TEST_CASE("LiveLinkServer returns to Listening when the client disconnects", "[studio]")
{
    LiveLinkServer server;
    REQUIRE(server.start(34905));
    TestClient client(34905);
    poll_until(server, [&] { return server.state() == LiveLinkState::Connected; });

    client.close_now();
    poll_until(server, [&] { return server.state() == LiveLinkState::Listening; });

    REQUIRE(server.state() == LiveLinkState::Listening);
}

TEST_CASE("LiveLinkServer start() fails on an already-bound port", "[studio]")
{
    LiveLinkServer server1;
    REQUIRE(server1.start(34906));
    LiveLinkServer server2;
    REQUIRE_FALSE(server2.start(34906));
}
```

- [ ] **Step 3: Run to verify it fails**

Run: `cmake --build build/debug --target mosaic_unit_tests -j4 && ./build/debug/tests/mosaic_unit_tests "[studio]"`
Expected: build failure — `studio/live_link_server.hpp` doesn't exist yet.

- [ ] **Step 4: Write `studio/live_link_server.hpp`**

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mosaic::studio
{
enum class LiveLinkState { Off, Listening, Connected };

constexpr std::uint16_t kDefaultLiveLinkPort = 6828;

// A small, Linux-only, non-blocking POSIX TCP listener for Blender Live-Link. Single
// connection at a time — a new accept() replaces any prior peer. Socket internals
// (file descriptors, POSIX headers) stay entirely inside live_link_server.cpp.
class LiveLinkServer
{
public:
    LiveLinkServer() = default;
    ~LiveLinkServer();
    LiveLinkServer(const LiveLinkServer&) = delete;
    LiveLinkServer& operator=(const LiveLinkServer&) = delete;

    // Binds and listens on `port`. Returns false on bind failure (e.g. port in use).
    bool start(std::uint16_t port);

    // Closes any listening/connected socket. state() -> Off. Safe to call when
    // already Off.
    void stop();

    LiveLinkState state() const { return state_; }

    // Non-blocking: accepts a pending connection if any, drains available bytes,
    // and returns every complete newline-terminated line received this call (the
    // trailing newline is stripped; an incomplete trailing line is buffered for
    // the next poll()). Call once per Studio frame. Empty when state() == Off.
    std::vector<std::string> poll();

private:
    int listen_fd_ = -1;
    int peer_fd_ = -1;
    LiveLinkState state_ = LiveLinkState::Off;
    std::string recv_buffer_;
};
} // namespace mosaic::studio
```

- [ ] **Step 5: Write `studio/live_link_server.cpp`**

```cpp
#include "studio/live_link_server.hpp"

#include <cerrno>
#include <cstring>

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace mosaic::studio
{
namespace
{
void set_nonblocking(int fd)
{
    const int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
} // namespace

LiveLinkServer::~LiveLinkServer() { stop(); }

bool LiveLinkServer::start(std::uint16_t port)
{
    stop();

    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) { return false; }

    const int reuse = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    if (listen(listen_fd_, /*backlog=*/1) != 0)
    {
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    set_nonblocking(listen_fd_);

    state_ = LiveLinkState::Listening;
    return true;
}

void LiveLinkServer::stop()
{
    if (peer_fd_ >= 0) { close(peer_fd_); peer_fd_ = -1; }
    if (listen_fd_ >= 0) { close(listen_fd_); listen_fd_ = -1; }
    recv_buffer_.clear();
    state_ = LiveLinkState::Off;
}

std::vector<std::string> LiveLinkServer::poll()
{
    std::vector<std::string> out;
    if (state_ == LiveLinkState::Off) { return out; }

    // A new connection always replaces any prior peer.
    const int accepted = accept(listen_fd_, nullptr, nullptr);
    if (accepted >= 0)
    {
        if (peer_fd_ >= 0) { close(peer_fd_); }
        peer_fd_ = accepted;
        set_nonblocking(peer_fd_);
        recv_buffer_.clear();
        state_ = LiveLinkState::Connected;
    }

    if (peer_fd_ < 0) { return out; }

    char buf[4096];
    for (;;)
    {
        const ssize_t n = recv(peer_fd_, buf, sizeof(buf), 0);
        if (n > 0)
        {
            recv_buffer_.append(buf, static_cast<std::size_t>(n));
            continue;
        }
        if (n == 0 || (errno != EWOULDBLOCK && errno != EAGAIN))
        {
            close(peer_fd_);
            peer_fd_ = -1;
            recv_buffer_.clear();
            state_ = LiveLinkState::Listening;
        }
        break;
    }

    std::size_t start = 0;
    for (;;)
    {
        const std::size_t nl = recv_buffer_.find('\n', start);
        if (nl == std::string::npos) { break; }
        out.push_back(recv_buffer_.substr(start, nl - start));
        start = nl + 1;
    }
    recv_buffer_.erase(0, start);

    return out;
}
} // namespace mosaic::studio
```

- [ ] **Step 6: Run to verify it passes**

Run: `cmake --build build/debug --target mosaic_unit_tests -j4 && ./build/debug/tests/mosaic_unit_tests "[studio]"`
Expected: PASS, all new cases plus every pre-existing `[studio]` case.

- [ ] **Step 7: Commit**

```bash
git add studio/live_link_server.hpp studio/live_link_server.cpp tests/unit/studio/live_link_server_test.cpp studio/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(studio): non-blocking POSIX TCP listener for Blender Live-Link"
```

---

### Task 3: Studio integration

**Files:**
- Modify: `studio/studio_app.hpp`
- Modify: `studio/studio_app.cpp`
- Modify: `tests/unit/studio/studio_layout_test.cpp` (only if a test for `live_link_label` is added there — see Step 1; otherwise create `tests/unit/studio/live_link_label_test.cpp`)
- Modify: `tests/CMakeLists.txt` (only if the new test file above is created)

**Interfaces:**
- Consumes: `mosaic::studio::LiveLinkServer`, `LiveLinkState`, `kDefaultLiveLinkPort` (Task 2); `mosaic::studio::parse_live_link_message`, `apply_live_link_update` (Task 1).
- Produces: nothing further consumed by later tasks — this is the final wiring point.

- [ ] **Step 1: Write the failing test for the label helper**

Create `tests/unit/studio/live_link_label_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "studio/studio_app.hpp"

using mosaic::studio::live_link_label;
using mosaic::studio::LiveLinkState;

TEST_CASE("live_link_label describes each state", "[studio]")
{
    REQUIRE(std::string(live_link_label(LiveLinkState::Off)) == "Live Link: Off");
    REQUIRE(std::string(live_link_label(LiveLinkState::Listening)) == "Live Link: Listening");
    REQUIRE(std::string(live_link_label(LiveLinkState::Connected)) == "Live Link: Connected");
}
```

Add `unit/studio/live_link_label_test.cpp` to `tests/CMakeLists.txt`, next to the other
`unit/studio/*.cpp` entries.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build/debug --target mosaic_unit_tests -j4 && ./build/debug/tests/mosaic_unit_tests "[studio]"`
Expected: build failure — `live_link_label` doesn't exist yet, and `studio_app.hpp` doesn't
yet include what it needs for `LiveLinkState` to be visible to the test.

- [ ] **Step 3: Add includes and the `live_link_label` free function to `studio_app.hpp`**

Add near the top of `studio/studio_app.hpp` (alongside its other includes):

```cpp
#include "studio/live_link_server.hpp"
```

Add a free function (not a member — it has no state, matching the plan's testable-pure-helper
pattern), placed near the top of the `mosaic::studio` namespace block in the same header:

```cpp
inline const char* live_link_label(LiveLinkState s)
{
    switch (s)
    {
    case LiveLinkState::Off:       return "Live Link: Off";
    case LiveLinkState::Listening: return "Live Link: Listening";
    case LiveLinkState::Connected: return "Live Link: Connected";
    }
    return "Live Link";
}
```

Add a `LiveLinkServer live_link_;` private member to the `StudioApp` class declaration in the
same header (next to its other state members — check the file for where similar per-session
state like `play_`/`scene_file_` is declared and add it alongside them).

- [ ] **Step 4: Run to verify the label test passes**

Run: `cmake --build build/debug --target mosaic_unit_tests -j4 && ./build/debug/tests/mosaic_unit_tests "[studio]"`
Expected: PASS, including the new `live_link_label` case. (The toolbar/poll wiring below has
no direct unit test — ImGui-driven code in this codebase isn't unit-tested; this task's
verification is the label test plus a clean `mosaic_studio`/`mosaic` build in Step 6.)

- [ ] **Step 5: Wire the toolbar toggle and per-frame poll+apply loop in `studio_app.cpp`**

Add `#include "studio/live_link.hpp"` to `studio/studio_app.cpp`'s includes.

In `StudioApp::draw_ui`, in the toolbar section (the same `ImGui::BeginMainMenuBar()`/toolbar
block that already has the Play/Stop/Step buttons), add:

```cpp
        if (ImGui::Button(live_link_label(live_link_.state())))
        {
            if (live_link_.state() == LiveLinkState::Off) { live_link_.start(kDefaultLiveLinkPort); }
            else                                          { live_link_.stop(); }
        }
```

Then, once per `draw_ui` call — after the toolbar block, anywhere before the function returns
(placing it right after the panel-drawing loop is fine, since it doesn't depend on panel
state):

```cpp
    for (const std::string& line : live_link_.poll())
    {
        if (auto update = parse_live_link_message(line))
        {
            apply_live_link_update(world, *update);
        }
    }
```

- [ ] **Step 6: Build the app and studio targets cleanly**

Run: `cmake --build build/debug --target mosaic mosaic_studio -j4`
Expected: builds cleanly, no errors.

Then run the full test suite:

Run: `cmake --build build/debug --target mosaic_unit_tests -j4 && ./build/debug/tests/mosaic_unit_tests`
Expected: PASS except the pre-existing flaky `script_system` SIGSEGV.

- [ ] **Step 7: Commit**

```bash
git add studio/studio_app.hpp studio/studio_app.cpp tests/unit/studio/live_link_label_test.cpp tests/CMakeLists.txt
git commit -m "feat(studio): wire Blender Live-Link toolbar toggle + per-frame apply"
```

---

### Task 4: Blender addon

**Files:**
- Create: `tools/blender_live_link/mosaic_live_link.py`

**Interfaces:** none consumed from earlier tasks (this file is never built or imported by the
C++ toolchain — it runs only inside Blender's own Python). Produces the wire format Task 1's
`parse_live_link_message` already expects: one `\n`-terminated JSON object per line, with
`name`/`position`/`rotation`/`scale` fields already converted to Mosaic's Y-up convention.

- [ ] **Step 1: Write `tools/blender_live_link/mosaic_live_link.py`**

```python
"""Mosaic Live-Link — streams live transform edits from Blender to Mosaic Studio.

One-way (Blender -> Studio), transform-only. See
docs/superpowers/specs/2026-09-13-blender-live-link-design.md for the wire protocol
and axis-conversion rationale.
"""

bl_info = {
    "name": "Mosaic Live-Link",
    "author": "Mosaic",
    "version": (1, 0, 0),
    "blender": (4, 0, 0),
    "location": "View3D > Sidebar > Mosaic",
    "description": "Streams live transform edits to Mosaic Studio",
    "category": "Object",
}

import json
import socket

import bpy


def _to_mosaic_position(v):
    # Blender is right-handed Z-up; Mosaic is right-handed Y-up.
    return [v[0], v[2], -v[1]]


def _to_mosaic_rotation(q):
    # Same basis change applied to the quaternion (w unchanged).
    return [q[1], q[3], -q[2], q[0]]  # Blender Quaternion is (w, x, y, z)


class MosaicLiveLinkState:
    sock = None
    connected = False


_state = MosaicLiveLinkState()


def _send_line(obj):
    if _state.sock is None:
        return
    loc = obj.location
    rot = obj.rotation_quaternion if obj.rotation_mode == 'QUATERNION' else obj.rotation_euler.to_quaternion()
    scale = obj.scale
    message = {
        "name": obj.name,
        "position": _to_mosaic_position([loc.x, loc.y, loc.z]),
        "rotation": _to_mosaic_rotation([rot.w, rot.x, rot.y, rot.z]),
        "scale": [scale.x, scale.z, scale.y],
    }
    try:
        _state.sock.sendall((json.dumps(message) + "\n").encode("utf-8"))
    except OSError:
        _state.connected = False
        _state.sock = None


def _on_depsgraph_update(scene, depsgraph):
    for update in depsgraph.updates:
        obj = update.id
        if isinstance(obj, bpy.types.Object) and update.is_updated_transform:
            _send_line(obj)


class MOSAIC_OT_live_link_connect(bpy.types.Operator):
    bl_idname = "mosaic.live_link_connect"
    bl_label = "Connect"

    def execute(self, context):
        props = context.scene.mosaic_live_link
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((props.host, props.port))
            _state.sock = s
            _state.connected = True
            if _on_depsgraph_update not in bpy.app.handlers.depsgraph_update_post:
                bpy.app.handlers.depsgraph_update_post.append(_on_depsgraph_update)
        except OSError as e:
            self.report({'ERROR'}, f"Mosaic Live-Link: {e}")
            _state.connected = False
        return {'FINISHED'}


class MOSAIC_OT_live_link_disconnect(bpy.types.Operator):
    bl_idname = "mosaic.live_link_disconnect"
    bl_label = "Disconnect"

    def execute(self, context):
        if _on_depsgraph_update in bpy.app.handlers.depsgraph_update_post:
            bpy.app.handlers.depsgraph_update_post.remove(_on_depsgraph_update)
        if _state.sock is not None:
            _state.sock.close()
        _state.sock = None
        _state.connected = False
        return {'FINISHED'}


class MOSAIC_PT_live_link_panel(bpy.types.Panel):
    bl_label = "Mosaic Live-Link"
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = "Mosaic"

    def draw(self, context):
        layout = self.layout
        props = context.scene.mosaic_live_link
        layout.prop(props, "host")
        layout.prop(props, "port")
        if _state.connected:
            layout.label(text="Connected", icon='CHECKMARK')
            layout.operator(MOSAIC_OT_live_link_disconnect.bl_idname)
        else:
            layout.operator(MOSAIC_OT_live_link_connect.bl_idname)


class MosaicLiveLinkProperties(bpy.types.PropertyGroup):
    host: bpy.props.StringProperty(name="Host", default="127.0.0.1")
    port: bpy.props.IntProperty(name="Port", default=6828, min=1, max=65535)


_classes = (
    MosaicLiveLinkProperties,
    MOSAIC_OT_live_link_connect,
    MOSAIC_OT_live_link_disconnect,
    MOSAIC_PT_live_link_panel,
)


def register():
    for cls in _classes:
        bpy.utils.register_class(cls)
    bpy.types.Scene.mosaic_live_link = bpy.props.PointerProperty(type=MosaicLiveLinkProperties)


def unregister():
    if _on_depsgraph_update in bpy.app.handlers.depsgraph_update_post:
        bpy.app.handlers.depsgraph_update_post.remove(_on_depsgraph_update)
    if _state.sock is not None:
        _state.sock.close()
    del bpy.types.Scene.mosaic_live_link
    for cls in reversed(_classes):
        bpy.utils.unregister_class(cls)
```

- [ ] **Step 2: No automated verification — manual only**

This file is not part of the CMake build and has no C++ or Python test harness in this repo.
Verification is the manual pass described in Task 5.

- [ ] **Step 3: Commit**

```bash
git add tools/blender_live_link/mosaic_live_link.py
git commit -m "feat(tools): Blender Live-Link addon"
```

---

### Task 5: Manual verification + docs

**Files:**
- Modify: `docs/project/next-steps.md`

**Interfaces:** none (manual check + docs only).

- [ ] **Step 1: Manual verification**

Build and run `mosaic editor` (or `mosaic run --window` if Studio is launched that way in this
project — check `docs/project/next-steps.md`'s existing "maintainer's hardware check" entries
for the exact invocation this project uses). In Blender 4.x: install
`tools/blender_live_link/mosaic_live_link.py` as an addon (Edit > Preferences > Add-ons >
Install), enable it, open the "Mosaic" tab in the 3D viewport sidebar, click Connect. In
Studio: name an entity (via the Inspector, add a `Name` component with a value matching a
Blender object's name, e.g. `"Cube"`) and click "Live Link: Off" to start listening. Move,
rotate, and scale the matching object in Blender on each axis individually; confirm the
entity moves identically in Studio's viewport, including that a positive Blender Z move (up)
shows as a positive Mosaic Y move (up), not a horizontal shift (this is the concrete check for
the axis-conversion formula's correctness — record the result, since this is the one thing
this whole slice could not verify any other way).

- [ ] **Step 2: Update `docs/project/next-steps.md`**

Mark this slice done in the same style as prior entries: what was built (`Name` component
prerequisite already landed separately; `live_link.hpp` parse/apply; `LiveLinkServer` socket
layer; Studio toolbar toggle + per-frame poll; the Blender addon), what's automated
(`[studio]` tag — get the accurate count by running the suite) vs. the one manual check (axis
correctness in a real Blender, recorded in Step 1), and what remains for Phase 5 (Plugin SDK,
package manager, templates, project generators, documentation tooling — per
`docs/project/advancement-plans-and-evaluation.md`).

- [ ] **Step 3: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "docs(studio): mark Blender Live-Link preview slice done"
```
