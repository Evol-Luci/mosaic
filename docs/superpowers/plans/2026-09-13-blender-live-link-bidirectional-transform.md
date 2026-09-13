# Blender Live-Link — Bidirectional Transform Sync Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** While Mosaic is simulating (Play/Step), stream every named entity's physics-driven `Transform` from Studio back to the linked Blender object each tick; while stopped, Blender remains authoritative exactly as it is today. One existing bool (`PlayState::is_playing()`) is the entire authority switch — no per-message loop detection.

**Architecture:** Extend the existing newline-delimited JSON wire protocol with an explicit `"type"` field (`"transform"` for this slice, other values reserved for a later mesh-sync slice and rejected here). Add a pure `format_live_link_transform_message` (the send-side mirror of the existing `parse_live_link_message`) and a `LiveLinkServer::send()` method. Gate Studio's existing per-frame Live-Link block on `PlayState::is_playing()`: playing sends, stopped applies (as today). The Blender addon gains a non-blocking receive loop (`bpy.app.timers`) and the inverse (Mosaic→Blender) axis conversion.

**Tech Stack:** C++20, `foundation::serialization::Value`/`from_json` (existing, for parsing only — outgoing messages are hand-formatted to guarantee single-line output), POSIX sockets (existing `LiveLinkServer`), Python 3 / `bpy` (Blender addon, untested by this repo's suite).

**Spec:** `docs/superpowers/specs/2026-09-13-blender-live-link-bidirectional-transform-design.md`

## Global Constraints

- Wire protocol stays newline-delimited JSON, one object per line — `foundation::serialization::to_json` pretty-prints (multi-line) and **must not** be used for outgoing Live-Link messages; hand-format the JSON string instead (see Task 1).
- `"type"` field: missing = `"transform"` (backward compatible with the original one-way slice); any other value = reject (`nullopt`), reserved for a future mesh-sync message type.
- Loop prevention is entirely `PlayState`-based: no sequence numbers, timestamps, or wire-level "who sent this" bookkeeping.
- Linux-only (unchanged from the original slice) — no Windows socket work.
- No `CommandStack`/undo integration (matches existing precedent).
- The Blender addon (`tools/blender_live_link/mosaic_live_link.py`) is not built or tested by this repo's C++ toolchain or CI; its only available automated check is `python3 -m py_compile`. Functional correctness is manually verified in real Blender.

---

### Task 1: Wire protocol — `"type"` field + `format_live_link_transform_message`

**Files:**
- Modify: `studio/live_link.hpp`
- Test: `tests/unit/studio/live_link_test.cpp`

**Interfaces:**
- Consumes: `mosaic::engine::Name` (`engine/world/name.hpp`, field: `std::string value`), `mosaic::engine::transform::Transform` (`engine/transform/transform.hpp`, fields: `Vec3 position`, `Quat rotation` (`x,y,z,w`), `Vec3 scale`).
- Produces: `std::string format_live_link_transform_message(const mosaic::engine::Name& name, const mosaic::engine::transform::Transform& transform);` — a single JSON line, **no trailing newline** (the caller, `LiveLinkServer::send`, appends it). `parse_live_link_message` gains `"type"` handling; its signature and `LiveLinkUpdate` are unchanged.

The axis conversion (Mosaic Y-up → Blender Z-up, values embedded in the message so Blender can apply them directly) is the exact inverse of the addon's existing `_to_mosaic_position`/`_to_mosaic_rotation`:
```
blender.x =  mosaic.x
blender.y = -mosaic.z
blender.z =  mosaic.y
```
applied identically to position and to the rotation's `(x, y, z)` (the quaternion's `w` is unchanged); scale reorders the same way with no sign flip: Blender `(x, y, z)` = `(mosaic.x, mosaic.z, mosaic.y)`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/studio/live_link_test.cpp` (add `#include "engine/transform/transform.hpp"` is already present; no new includes needed since `Name`/`Transform` are already used in this file):

```cpp
TEST_CASE("parse_live_link_message accepts an explicit type:transform", "[studio]")
{
    const auto update = parse_live_link_message(
        R"({"type":"transform","name":"Cube","position":[1,2,3],"rotation":[0,0,0,1],"scale":[1,1,1]})");
    REQUIRE(update.has_value());
    REQUIRE(update->name == "Cube");
}

TEST_CASE("parse_live_link_message rejects an unrecognized type", "[studio]")
{
    REQUIRE_FALSE(parse_live_link_message(
        R"({"type":"mesh","name":"Cube","position":[1,2,3],"rotation":[0,0,0,1],"scale":[1,1,1]})").has_value());
}

TEST_CASE("format_live_link_transform_message formats an identity transform", "[studio]")
{
    Name name{"Cube"};
    Transform t; // default: position (0,0,0), rotation (0,0,0,1), scale (1,1,1)

    const std::string line = mosaic::studio::format_live_link_transform_message(name, t);

    REQUIRE(line ==
        R"({"type":"transform","name":"Cube","position":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]})");
}

TEST_CASE("format_live_link_transform_message formats distinct nonzero axes", "[studio]")
{
    Name name{"Cube"};
    Transform t;
    t.position = {2.0f, 3.0f, 4.0f};
    t.rotation = {0.25f, 0.5f, 0.75f, 1.0f};
    t.scale = {5.0f, 6.0f, 7.0f};

    const std::string line = mosaic::studio::format_live_link_transform_message(name, t);

    REQUIRE(line ==
        R"({"type":"transform","name":"Cube","position":[2,-4,3],"rotation":[0.25,-0.75,0.5,1],"scale":[5,7,6]})");
}

TEST_CASE("format_live_link_transform_message round-trips through the inverse conversion", "[studio]")
{
    Transform original;
    original.position = {2.0f, 3.0f, 4.0f};
    original.rotation = {0.25f, 0.5f, 0.75f, 1.0f};
    original.scale = {5.0f, 6.0f, 7.0f};

    const std::string line =
        mosaic::studio::format_live_link_transform_message(Name{"Cube"}, original);
    const auto blender_frame = parse_live_link_message(line); // raw numbers, Blender-frame
    REQUIRE(blender_frame.has_value());

    // Re-apply the known Blender -> Mosaic conversion (the addon's existing
    // _to_mosaic_position/_to_mosaic_rotation formula) to confirm losslessness.
    const auto& bp = blender_frame->position;
    const auto& bq = blender_frame->rotation;
    const auto& bs = blender_frame->scale;

    REQUIRE(bp.x == original.position.x);
    REQUIRE(-bp.y == original.position.z);
    REQUIRE(bp.z == original.position.y);

    REQUIRE(bq.x == original.rotation.x);
    REQUIRE(-bq.y == original.rotation.z);
    REQUIRE(bq.z == original.rotation.y);
    REQUIRE(bq.w == original.rotation.w);

    REQUIRE(bs.x == original.scale.x);
    REQUIRE(bs.y == original.scale.z);
    REQUIRE(bs.z == original.scale.y);
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --preset debug
cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)"
./build/debug/tests/mosaic_unit_tests "format_live_link_transform_message*"
```
Expected: build fails (`format_live_link_transform_message` is not declared) — that is the correct RED for a new-function test. Comment out the three `format_live_link_transform_message` test cases temporarily, rebuild, and confirm `parse_live_link_message rejects an unrecognized type` fails (it currently has no `"type"` handling at all, so it wrongly returns a value instead of `nullopt`). Then restore the commented-out cases before Step 3.

- [ ] **Step 3: Implement**

In `studio/live_link.hpp`, add near the top of the `mosaic::studio` namespace (after the `detail` namespace, before `parse_live_link_message`):

```cpp
namespace detail
{
// -0.0f prints as "-0" via ostream, which is ugly but harmless JSON; normalized to
// +0.0f here purely for clean, deterministic wire output.
inline float negate_clean(float v) { return v == 0.0f ? 0.0f : -v; }
} // namespace detail
```

Modify `parse_live_link_message` to add type handling right after the root/table check:

```cpp
    if (!root.is<ser::Table>()) { return std::nullopt; }
    const ser::Table& t = root.as<ser::Table>();

    const ser::Value* type_v = t.find("type");
    if (type_v != nullptr)
    {
        if (!type_v->is<std::string>() || type_v->as<std::string>() != "transform")
        {
            return std::nullopt;
        }
    }

    const ser::Value* name_v = t.find("name");
```

Add the new function after `parse_live_link_message`, before `apply_live_link_update`:

```cpp
// Builds one outgoing "type":"transform" JSON line (Studio -> Blender), converting
// Mosaic's Y-up convention to Blender's Z-up (the inverse of the Blender addon's
// _to_mosaic_position/_to_mosaic_rotation). No trailing newline — the caller
// (LiveLinkServer::send) appends it. Hand-formatted rather than routed through
// foundation::serialization::to_json, which pretty-prints across multiple lines and
// would break this protocol's one-line-per-message framing.
inline std::string format_live_link_transform_message(const Name& name,
                                                       const mosaic::engine::transform::Transform& transform)
{
    std::ostringstream out;
    out << std::setprecision(9);

    const auto& p = transform.position;
    const auto& q = transform.rotation;
    const auto& s = transform.scale;

    out << "{\"type\":\"transform\",\"name\":\"" << name.value << "\","
        << "\"position\":[" << p.x << "," << detail::negate_clean(p.z) << "," << p.y << "],"
        << "\"rotation\":[" << q.x << "," << detail::negate_clean(q.z) << "," << q.y << "," << q.w << "],"
        << "\"scale\":[" << s.x << "," << s.z << "," << s.y << "]}";
    return out.str();
}
```

Add `#include <iomanip>` and `#include <sstream>` to the top of `studio/live_link.hpp` (alongside the existing `#include <optional>` / `#include <string>` / `#include <vector>`).

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)"
./build/debug/tests/mosaic_unit_tests "[studio]"
```
Expected: all `[studio]` tests pass, including the six new/modified ones above.

- [ ] **Step 5: Commit**

```bash
git add studio/live_link.hpp tests/unit/studio/live_link_test.cpp
git commit -m "feat(studio): add type field + outgoing transform message formatting to Live-Link"
```

---

### Task 2: `LiveLinkServer::send()`

**Files:**
- Modify: `studio/live_link_server.hpp`, `studio/live_link_server.cpp`
- Test: `tests/unit/studio/live_link_server_test.cpp`

**Interfaces:**
- Consumes: nothing new (uses the class's existing `peer_fd_`/`state_`).
- Produces: `void LiveLinkServer::send(const std::string& line);` — appends `'\n'` and writes to the current peer; a no-op (no exception, no crash) when `state() != Connected`; a would-block or failed write is silently dropped (never retried/queued/buffered).

- [ ] **Step 1: Write the failing tests**

Add a `recv_text()` helper to the existing `TestClient` in `tests/unit/studio/live_link_server_test.cpp` (inside the anonymous namespace, alongside `send_text`/`close_now`):

```cpp
    std::string recv_text()
    {
        char buf[4096];
        const ssize_t n = recv(fd_, buf, sizeof(buf), MSG_DONTWAIT);
        if (n <= 0) { return {}; }
        return std::string(buf, static_cast<std::size_t>(n));
    }
```

Append these test cases at the end of the file:

```cpp
TEST_CASE("LiveLinkServer::send delivers a line to the connected client", "[studio]")
{
    LiveLinkServer server;
    REQUIRE(server.start(34907));
    TestClient client(34907);
    poll_until(server, [&] { return server.state() == LiveLinkState::Connected; });

    server.send("hello");

    std::string received;
    for (int i = 0; i < 200 && received.empty(); ++i)
    {
        received += client.recv_text();
        if (received.empty()) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    }
    REQUIRE(received == "hello\n");
}

TEST_CASE("LiveLinkServer::send is a silent no-op when there is no connected peer", "[studio]")
{
    LiveLinkServer server;
    server.send("hello"); // Off: must not throw or crash
    REQUIRE(server.start(34908));
    server.send("hello"); // Listening, no peer yet: must not throw or crash
    REQUIRE(server.state() == LiveLinkState::Listening);
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)"
```
Expected: build fails — `LiveLinkServer::send` is not declared yet.

- [ ] **Step 3: Implement**

In `studio/live_link_server.hpp`, add to the public section (after `poll()`):

```cpp
    // Best-effort non-blocking send on the current connection. A no-op if state()
    // != Connected. A send that would block, or fails, is silently dropped — this
    // is a continuous per-tick stream, so a dropped line is superseded by the next
    // tick's send, never retried or queued.
    void send(const std::string& line);
```

In `studio/live_link_server.cpp`, add after `poll()`'s closing brace:

```cpp
void LiveLinkServer::send(const std::string& line)
{
    if (peer_fd_ < 0) { return; }
    const std::string with_newline = line + "\n";
    ::send(peer_fd_, with_newline.data(), with_newline.size(), MSG_NOSIGNAL);
}
```

(`::send` disambiguates the POSIX socket function from the member function of the same name being defined.)

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)"
./build/debug/tests/mosaic_unit_tests "[studio]"
```
Expected: all `[studio]` tests pass.

- [ ] **Step 5: Commit**

```bash
git add studio/live_link_server.hpp studio/live_link_server.cpp tests/unit/studio/live_link_server_test.cpp
git commit -m "feat(studio): add LiveLinkServer::send for Studio -> Blender messages"
```

---

### Task 3: Studio integration — play-state-gated send/apply

**Files:**
- Modify: `studio/studio_app.cpp:86-92` (the existing Live-Link poll+apply block)

**Interfaces:**
- Consumes: `format_live_link_transform_message` and the extended `parse_live_link_message` (Task 1), `LiveLinkServer::send` (Task 2), `play_.is_playing()` (existing `PlayState`, `studio/play_state.hpp`), `world.for_each<mosaic::engine::Name>(...)` and `world.get_component<mosaic::engine::transform::Transform>(id)` (existing `World` API, already used identically in `studio/live_link.hpp`'s own `apply_live_link_update`).
- Produces: nothing new consumed by later tasks — this is the final wiring point.

This is ImGui/`World` UI glue with no existing unit-test coverage for `draw_ui` itself (the original slice's own toolbar/poll wiring wasn't unit-tested either — only `live_link.hpp`'s pure logic was). Verification here is build-clean plus the manual pass in Task 4/5.

- [ ] **Step 1: Replace the existing block**

In `studio/studio_app.cpp`, replace:

```cpp
    for (const std::string& line : live_link_.poll())
    {
        if (auto update = parse_live_link_message(line))
        {
            apply_live_link_update(world, *update);
        }
    }
```

with:

```cpp
    if (play_.is_playing())
    {
        // Studio is authoritative while simulating: push every named entity's
        // current (possibly physics-driven) Transform to Blender.
        world.for_each<mosaic::engine::Name>(
            [&](mosaic::engine::World::EntityID e, mosaic::engine::Name& name)
            {
                if (const auto* t = world.get_component<mosaic::engine::transform::Transform>(e))
                {
                    live_link_.send(format_live_link_transform_message(name, *t));
                }
            });
        live_link_.poll(); // drain the socket; inbound is ignored while playing
    }
    else
    {
        // Blender is authoritative while stopped (unchanged from the original slice).
        for (const std::string& line : live_link_.poll())
        {
            if (auto update = parse_live_link_message(line))
            {
                apply_live_link_update(world, *update);
            }
        }
    }
```

No new `#include`s are needed: `mosaic::engine::Name` and `mosaic::engine::transform::Transform` are already visible transitively via the existing `#include "studio/live_link.hpp"` at the top of this file.

- [ ] **Step 2: Build and run the full suite**

```bash
cmake --build build/debug --target mosaic mosaic_studio mosaic_unit_tests -j"$(nproc)"
ctest --test-dir build/debug --output-on-failure
```
Expected: clean build; the only test failures should be the six pre-existing, unrelated `--host`/`--connect` CLI Catch2 test-name-leading-dash discovery failures (documented in project memory) — nothing else.

- [ ] **Step 3: Commit**

```bash
git add studio/studio_app.cpp
git commit -m "feat(studio): gate Live-Link send/apply on PlayState (Studio authoritative while playing)"
```

---

### Task 4: Blender addon — non-blocking receive + inverse conversion + apply

**Files:**
- Modify: `tools/blender_live_link/mosaic_live_link.py`
- Modify: `docs/project/next-steps.md` (mark this slice done — folded into this task's commit per project convention: next-steps updates ride on the feature branch, never their own PR)

**Interfaces:**
- Consumes: the wire protocol from Task 1 (`"type":"transform"` messages, Blender-frame values).
- Produces: nothing consumed elsewhere in this plan — this is the addon-side terminus.

Python, not built or unit-tested by this repo. The only automated check available is a syntax compile; functional correctness is the manual pass at the end of this task.

- [ ] **Step 1: Add receive-buffer state and inverse conversion functions**

In `tools/blender_live_link/mosaic_live_link.py`, add after `_to_mosaic_rotation`:

```python
def _from_mosaic_position(v):
    # Inverse of _to_mosaic_position: Mosaic (x, y, z) -> Blender (x, y, z).
    return [v[0], -v[2], v[1]]


def _from_mosaic_rotation(q):
    # Inverse of _to_mosaic_rotation. q is Mosaic (x, y, z, w); returns Blender
    # Quaternion order (w, x, y, z).
    return [q[3], q[0], -q[2], q[1]]
```

Modify `MosaicLiveLinkState` to add a receive buffer:

```python
class MosaicLiveLinkState:
    sock = None
    connected = False
    recv_buffer = ""
```

- [ ] **Step 2: Add the apply function and the non-blocking poll timer**

Add after `_on_depsgraph_update`:

```python
def _apply_incoming_transform(msg):
    obj = bpy.data.objects.get(msg["name"])
    if obj is None:
        return
    pos = _from_mosaic_position(msg["position"])
    rot = _from_mosaic_rotation(msg["rotation"])
    scl = msg["scale"]
    obj.location = pos
    obj.rotation_mode = 'QUATERNION'
    obj.rotation_quaternion = rot
    obj.scale = [scl[0], scl[2], scl[1]]


def _poll_incoming():
    if _state.sock is None:
        return None
    try:
        while True:
            try:
                data = _state.sock.recv(4096)
            except BlockingIOError:
                break
            if not data:
                _state.connected = False
                _state.sock = None
                return None
            _state.recv_buffer += data.decode("utf-8", errors="ignore")
    except OSError:
        _state.connected = False
        _state.sock = None
        return None

    while "\n" in _state.recv_buffer:
        line, _state.recv_buffer = _state.recv_buffer.split("\n", 1)
        if not line:
            continue
        try:
            msg = json.loads(line)
        except json.JSONDecodeError:
            continue
        if msg.get("type", "transform") != "transform":
            continue
        try:
            _apply_incoming_transform(msg)
        except (KeyError, TypeError, IndexError):
            # A malformed message (missing/wrong-shaped field) must not kill this
            # timer permanently — skip it and keep polling.
            continue

    return 0.05
```

- [ ] **Step 3: Wire the timer into connect/disconnect/unregister**

Modify `MOSAIC_OT_live_link_connect.execute`:

```python
    def execute(self, context):
        props = context.scene.mosaic_live_link
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((props.host, props.port))
            s.setblocking(False)
            _state.sock = s
            _state.connected = True
            _state.recv_buffer = ""
            if _on_depsgraph_update not in bpy.app.handlers.depsgraph_update_post:
                bpy.app.handlers.depsgraph_update_post.append(_on_depsgraph_update)
            if not bpy.app.timers.is_registered(_poll_incoming):
                bpy.app.timers.register(_poll_incoming, first_interval=0.05)
        except OSError as e:
            self.report({'ERROR'}, f"Mosaic Live-Link: {e}")
            _state.connected = False
        return {'FINISHED'}
```

Modify `MOSAIC_OT_live_link_disconnect.execute`:

```python
    def execute(self, context):
        if _on_depsgraph_update in bpy.app.handlers.depsgraph_update_post:
            bpy.app.handlers.depsgraph_update_post.remove(_on_depsgraph_update)
        if bpy.app.timers.is_registered(_poll_incoming):
            bpy.app.timers.unregister(_poll_incoming)
        if _state.sock is not None:
            _state.sock.close()
        _state.sock = None
        _state.connected = False
        return {'FINISHED'}
```

Modify the module-level `unregister()`:

```python
def unregister():
    if _on_depsgraph_update in bpy.app.handlers.depsgraph_update_post:
        bpy.app.handlers.depsgraph_update_post.remove(_on_depsgraph_update)
    if bpy.app.timers.is_registered(_poll_incoming):
        bpy.app.timers.unregister(_poll_incoming)
    if _state.sock is not None:
        _state.sock.close()
    del bpy.types.Scene.mosaic_live_link
    for cls in reversed(_classes):
        bpy.utils.unregister_class(cls)
```

Also update the module docstring at the top of the file (currently "One-way (Blender -> Studio), transform-only...") to:

```python
"""Mosaic Live-Link — streams live transform edits between Blender and Mosaic Studio.

Bidirectional, transform-only: Blender drives Studio while Studio is stopped; Studio
drives Blender (e.g. physics results) while Studio is playing. See
docs/superpowers/specs/2026-09-13-blender-live-link-bidirectional-transform-design.md
for the wire protocol and axis-conversion rationale.
"""
```

- [ ] **Step 4: Verify the file is syntactically valid**

```bash
python3 -m py_compile tools/blender_live_link/mosaic_live_link.py
```
Expected: exits 0, no output. This is the only automated check available for this file — the sandbox's Blender install has a broken Python stdlib (documented project limitation), so `bpy`-dependent behavior cannot be exercised here.

- [ ] **Step 5: Mark the slice done in next-steps.md**

Open `docs/project/next-steps.md` and update its "Last updated" line (at the top) to describe this slice's completion, following the same style as the existing entry for the original one-way slice — state that Live-Link is now bidirectional (Studio -> Blender while playing, streaming physics-driven Transform; Blender -> Studio while stopped, unchanged), gated purely by `PlayState::is_playing()`, and link this plan's spec.

- [ ] **Step 6: Commit**

```bash
git add tools/blender_live_link/mosaic_live_link.py docs/project/next-steps.md
git commit -m "feat(tools): bidirectional Blender Live-Link (Studio -> Blender while playing)"
```

- [ ] **Step 7: Manual verification (report results to your human partner, do not mark this plan complete without it)**

With a real Blender instance, using the addon from Task 4:
1. Connect Blender to Studio (unchanged from before).
2. With Mosaic **stopped**, move/rotate/scale the linked Blender object — confirm Studio's viewport still follows (original slice's behavior, unregressed).
3. Press **Play** in Mosaic on a scene with a physics body linked by `Name` — confirm the Blender object visibly follows the simulated/falling body.
4. While **Playing**, try moving the Blender object manually — confirm it has no lasting effect (the next tick's Studio-authoritative update overwrites it; this is expected, not a bug).
5. Press **Stop** again — confirm Blender regains authority immediately (no stale state, no need to reconnect).

