# Blender Live-Link — Design

## Goal (Phase 5, Tool Ecosystem pillar — first slice)

Deliver the roadmap's "basic Blender bridge (live-link preview)": manipulating an object
already placed in Blender live-updates the matching Mosaic entity's `Transform` in Studio's
viewport, in real time. One-way (Blender → Studio), transform-only (position/rotation/scale —
no mesh/material/geometry sync), matching entities by the `Name` component
(`engine/world/name.hpp`, shipped as the prerequisite slice for exactly this). This is the
first Phase 5 (Tool Ecosystem) slice; Phase 4 (Production) is complete.

Per the architecture's Sacred Rule, Blender never touches Foundation or Engine — it is an
external DCC running out-of-process, talking to Studio only over IPC. Everything new here
lives in `studio/` (C++, Studio-only) and a new top-level `tools/` directory (the Blender
addon, pure Python, not part of the C++ build at all).

## Design

### Approach

A raw POSIX TCP socket (non-blocking accept/recv) on a fixed localhost port, carrying a
newline-delimited JSON protocol, parsed with the existing `foundation::serialization` JSON
codec — no new C++ dependency. The Blender side is a single-file Blender addon (pure Python
stdlib `socket`) that hooks `bpy.app.handlers.depsgraph_update_post` and pushes one JSON line
per changed object.

Rejected alternatives:
- **A vendored WebSocket library:** more code and build surface for a purely-localhost link
  that gains nothing from WebSocket framing over raw TCP.
- **A file-polling bridge** (Blender writes a JSON file periodically, Studio polls its
  mtime): far worse latency, and prone to partial-write races — not actually "live".

### Wire protocol

One JSON object per line (`\n`-terminated), Blender → Studio only:

```json
{"name": "Cube", "position": [1.0, 2.0, 3.0], "rotation": [0.0, 0.0, 0.0, 1.0], "scale": [1.0, 1.0, 1.0]}
```

`rotation` is `[x, y, z, w]`, matching `foundation::math::Quat`'s field order. All values are
already in **Mosaic's convention** (right-handed, Y-up, meters, radians) — the Blender addon
converts from Blender's right-handed Z-up before sending, never Studio. This mirrors how
asset cookers (e.g. the WAV/TTF importers) convert at the boundary rather than leaking a
foreign convention inward; Studio-side code only ever sees native Mosaic transforms.

**Axis conversion** (Blender `(x, y, z)` → Mosaic `(x, y, z)`):
```
mosaic.x =  blender.x
mosaic.y =  blender.z
mosaic.z = -blender.y
```
The same basis change applies to the quaternion (convert Blender's `(x, y, z, w)` the same
way: swap the `y`/`z` components and negate the new `z`, leaving `w` unchanged) — this is the
standard Z-up-to-Y-up conversion used by glTF-style exporters, and keeps handedness (no need
to negate `w` or flip winding). No numeric round-trip surprises are expected, but this is
explicitly flagged as the one piece of arithmetic in this slice most worth a dedicated
correctness test (see Testing) since a sign error here silently produces a mirrored/wrong
rotation rather than a crash.

`Value`'s JSON numbers may come back as `int64`, `uint64`, or `double` depending on whether
the source literal had a fractional part (the same dual-accept pattern N3's replication
parsing already established for `net`) — the parser normalizes any of the three into `float`.

### `studio/live_link.hpp` — pure parsing + application logic

No sockets here at all — this is the headlessly-testable half:

```cpp
#pragma once

#include <optional>
#include <string>

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

// Parses one newline-delimited JSON line into a LiveLinkUpdate. Returns nullopt on
// malformed JSON, a non-object root, or a missing/wrong-typed field — never throws.
std::optional<LiveLinkUpdate> parse_live_link_message(const std::string& line);

// Finds the entity whose Name matches update.name and overwrites its Transform in
// place (position/rotation/scale only — WorldTransform is derived, untouched here).
// Returns false if zero or more-than-one entity matches (ambiguous or unknown name;
// logged once by the caller, not here — this function stays silent/pure).
bool apply_live_link_update(mosaic::engine::World& world, const LiveLinkUpdate& update);
} // namespace mosaic::studio
```

### `studio/live_link_server.hpp`/`.cpp` — the socket layer

A small, Linux-only, non-blocking POSIX TCP listener. Single connection at a time — a new
`accept()` replaces any prior peer (this is a one-artist, one-Studio-instance dev tool, not a
multi-client server):

```cpp
namespace mosaic::studio
{
enum class LiveLinkState { Off, Listening, Connected };

class LiveLinkServer
{
public:
    // Binds and listens on `port` (default below). Returns false on bind failure.
    bool start(std::uint16_t port);
    void stop(); // closes any listening/connected socket; state() -> Off

    LiveLinkState state() const;

    // Non-blocking: accepts a pending connection if any, drains available bytes,
    // and returns every complete newline-terminated line received this call (an
    // incomplete trailing line is buffered for the next poll()). Call once per
    // Studio frame.
    std::vector<std::string> poll();

private:
    int listen_fd_ = -1;
    int peer_fd_ = -1;
    std::string recv_buffer_; // holds an incomplete trailing line across poll() calls
};

constexpr std::uint16_t kDefaultLiveLinkPort = 6828;
} // namespace mosaic::studio
```

`poll()`'s buffering handles TCP's stream semantics (a `recv()` call has no obligation to
return whole lines) — bytes are appended to `recv_buffer_`, complete `\n`-terminated lines are
split off and returned, and any remainder stays buffered. A `recv()` returning `0` or an error
other than `EWOULDBLOCK`/`EAGAIN` closes `peer_fd_` and drops back to `Listening` (still
`accept()`-able) — this is `LiveLinkServer`'s only externally-visible reaction to a disconnect.

### Studio integration (`studio/studio_app.hpp`/`.cpp`)

`StudioApp` gains a `LiveLinkServer live_link_;` member. In the toolbar (same row as the
existing Play/Stop/Step buttons):

```cpp
static const char* live_link_label(LiveLinkState s)
{
    switch (s)
    {
    case LiveLinkState::Off:       return "Live Link: Off";
    case LiveLinkState::Listening: return "Live Link: Listening";
    case LiveLinkState::Connected: return "Live Link: Connected";
    }
    return "Live Link";
}

if (ImGui::Button(live_link_label(live_link_.state())))
{
    if (live_link_.state() == LiveLinkState::Off) { live_link_.start(kDefaultLiveLinkPort); }
    else                                          { live_link_.stop(); }
}
```

Once per `draw_ui` call (unconditionally — `poll()` is a cheap no-op when `Off`):

```cpp
for (const std::string& line : live_link_.poll())
{
    if (auto update = parse_live_link_message(line))
    {
        apply_live_link_update(world, *update);
    }
}
```

No `CommandStack` involvement: Studio's existing viewport/inspector transform edits already
bypass `CommandStack` today (there is no undo integration for live transform drags yet), so
this matches established precedent rather than introducing a new pattern. A future undo
integration, if ever built, would cover both paths together.

### Blender addon (`tools/blender_live_link/mosaic_live_link.py`)

A single-file Blender addon (standard `bl_info` + `register()`/`unregister()`):
- A small panel (N-panel, "Mosaic" tab) with a "Connect" toggle and a host:port field
  (default `127.0.0.1:6828`).
- On connect: opens a `socket.socket(AF_INET, SOCK_STREAM)`, non-blocking, `connect_ex`.
- Registers a `bpy.app.handlers.depsgraph_update_post` handler that, for each `Object` update
  in the depsgraph, converts its `matrix_world`-derived location/rotation/scale through the
  axis conversion above and sends one JSON line (`socket.sendall`, best-effort — a failed
  send tears down the connection and the panel shows "Disconnected").
- On disconnect/unregister: closes the socket cleanly.

This file is never built, tested, or imported by the C++ toolchain — it is Blender-loaded
Python, entirely outside `CMakeLists.txt`. It is the one part of this slice that stays
genuinely manual/maintainer-verified, same carve-out N7 used for GLFW-driven input: there is
no way to exercise real Blender inside this test suite.

## Testing (headless & automated, except the addon itself)

- **`tests/unit/studio/live_link_test.cpp`** (new): `parse_live_link_message` — a valid line
  parses into the expected `LiveLinkUpdate`; malformed JSON, a non-object root, and each
  individually-missing field (`name`/`position`/`rotation`/`scale`) all return `nullopt`;
  a mix of int and float literals in the same array parses correctly (the dual-accept case).
  `apply_live_link_update` — a matching `Name` entity's `Transform` is overwritten exactly;
  zero matches returns `false` and touches nothing; two entities sharing the same `Name`
  returns `false` and touches neither (ambiguous, not "first match wins" — silent partial
  application would be worse than a no-op).
- **`tests/unit/studio/live_link_server_test.cpp`** (new): a real localhost TCP loopback pair
  in-process, the same way N6 tested `EnetTransport` — `start()` binds; a client `socket`
  connects and `state()` becomes `Connected`; sending a single line then a partial line then
  the rest of it across two separate `send()` calls yields the buffering behavior described
  above (one complete line from the first `poll()` that catches the boundary, the rest after
  the remainder arrives); closing the client socket brings `state()` back to `Listening`.
- **Axis-conversion correctness** is tested Python-side is not possible from this suite (no
  Blender in CI) — instead, `apply_live_link_update`'s test fixtures use pre-converted
  Mosaic-convention values only; the conversion formula itself is verified by hand/comment
  reference in the addon and re-verified manually against a real Blender scene during the
  manual verification pass (rotate/move an object on each single axis in Blender, confirm the
  matching axis moves in Studio).

**Manual (the one part this suite cannot automate):** install the addon in a real Blender,
connect, move/rotate/scale an object whose name matches a Mosaic entity, confirm Studio's
viewport updates live and on the correct axes.

## Files

**New**
- `studio/live_link.hpp` — pure parse/apply logic.
- `studio/live_link_server.hpp` / `studio/live_link_server.cpp` — POSIX socket layer.
- `tools/blender_live_link/mosaic_live_link.py` — the Blender addon.
- `tests/unit/studio/live_link_test.cpp`
- `tests/unit/studio/live_link_server_test.cpp`

**Modified**
- `studio/studio_app.hpp` / `studio/studio_app.cpp` — `LiveLinkServer` member, toolbar toggle,
  per-frame `poll()` + apply loop.
- `studio/CMakeLists.txt` — add `live_link_server.cpp` to `mosaic_studio`.
- `tests/CMakeLists.txt` — register the two new test files.
- `docs/project/next-steps.md` — mark this slice done.

## Non-goals / follow-ups

- **Studio → Blender direction** (editing in Studio pushing back to Blender) — explicitly
  out of scope; this is a one-way preview.
- **Mesh/material/geometry sync, new-object creation from Blender** — transform-only for this
  slice; a real asset still goes through `mosaic import`.
- **Windows support** (Winsock) — Linux-only for now; the socket layer is isolated behind
  `LiveLinkServer` specifically so a Windows backend could be added later without touching
  `live_link.hpp` or Studio's integration code.
- **Multiple simultaneous Blender connections** — one at a time, by design.
- **CommandStack / undo integration** for live-link edits — matches the rest of Studio's
  current (command-less) live transform editing; a later, broader undo pass would cover both.
- Further Phase 5 (Tool Ecosystem) work — Plugin SDK, package manager, templates, project
  generators, documentation tooling — remains scheduled after this slice, per
  `docs/project/advancement-plans-and-evaluation.md`.
