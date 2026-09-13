# Blender Live-Link — Bidirectional Transform Sync — Design

## Goal (Phase 5, Tool Ecosystem pillar — deepening the Live-Link preview slice)

The shipped Live-Link preview slice
([spec](2026-09-13-blender-live-link-design.md)) is one-way (Blender → Studio),
transform-only. Manual verification surfaced a gap: while Mosaic's physics simulation is
running, moving the linked Blender object updates the Mosaic entity's `Transform`
correctly, but the `PhysicsSystem` then immediately overwrites it again that same tick (or
the next), since nothing tells Blender what happened. This slice adds the missing
direction — Studio → Blender — so that while Mosaic is simulating, the physics-driven
`Transform` streams back and the Blender object visibly follows the simulation.

This is **not** two-way-at-once. The two directions are mutually exclusive, gated by
Studio's own `PlayState` (`studio/play_state.hpp`, already shipped as part of 37e):

- **Stopped** (`play_.is_playing() == false`): unchanged from the original slice — Blender
  is authoritative, edits in Blender flow to Studio, Studio sends nothing back.
- **Playing**: Studio is authoritative — Studio streams every named entity's `Transform`
  to Blender once per tick, and *ignores* (but still drains) incoming Blender messages.
  Note that `PlayState` defaults to `is_playing() == true` on Studio launch (this predates this slice — see `studio/play_state.hpp`), so a freshly opened Studio editor is Studio-authoritative until the user presses Stop; this is documented in `docs/project/next-steps.md`.

This single existing bool is the whole loop-prevention mechanism — no per-message
sequence numbers, timestamps, or "who sent this last" bookkeeping. Both sides can always
be sending; only one side's messages are ever *applied* at a time, and that side is
decided independently by each Studio frame reading its own `PlayState`, not negotiated
over the wire.

**Mesh/geometry sync remains explicitly out of scope for this slice** — that is a
separate follow-up slice (its own design), since it touches the asset/mesh pipeline
(`MeshCache`, `AssetRegistry`) rather than just the wire protocol.

## Design

### Wire protocol: adding `"type"`

Every message gains an explicit `"type"` field. This slice defines exactly one value,
`"transform"`, used identically in both directions:

```json
{"type":"transform","name":"Cube","position":[1.0,2.0,3.0],"rotation":[0.0,0.0,0.0,1.0],"scale":[1.0,1.0,1.0]}
```

`parse_live_link_message` treats a **missing** `"type"` field as `"transform"` (the
original slice's Blender addon never sent one) — pure backward compatibility, not a
protocol branch point yet. A `"type"` present with any other value is treated as
"not a transform message" and `parse_live_link_message` returns `nullopt` for it (the
follow-up mesh-sync slice will add a sibling parse function for `"type":"mesh"`; this
slice does not need to anticipate its shape beyond leaving the field free).

Values in the wire protocol are always in **Mosaic's own convention** (right-handed,
Y-up), regardless of direction — true for the original one-way slice, and unchanged
here. The Blender addon remains the **sole** place any axis conversion happens, in
either direction: its existing `_to_mosaic_position`/`_to_mosaic_rotation` convert
Blender's Z-up to Mosaic's Y-up before sending (unchanged by this slice), and new
`_from_mosaic_position`/`_from_mosaic_rotation` functions (addon-side, not Studio)
convert Mosaic's Y-up back to Blender's Z-up after receiving. **Studio's C++ code never
performs axis conversion in either direction** — `format_live_link_transform_message`
builds raw Mosaic-frame numbers, exactly as `parse_live_link_message` reads raw
Mosaic-frame numbers back out of an inbound message. This preserves the original
slice's documented boundary rule ("Studio-side code only ever sees Mosaic-convention
values") instead of splitting the conversion across two languages, which would require
both sides' conversions to stay exact inverses of each other forever with no test
surface spanning both languages in this repo (C++ tests cannot exercise Python; there
is no Blender in CI). An earlier draft of this design had Studio also convert, which —
combined with the addon's own conversion — silently doubled it; caught only by a
whole-branch review that traced a value across both languages, since every per-task
review checked only one side against its own (then-consistent) brief.

### `studio/live_link.hpp` additions

```cpp
// Builds one outgoing "type":"transform" JSON line (Studio -> Blender) for the given
// Name + Transform, converting Mosaic's Y-up convention to Blender's Z-up. The inverse
// of parse_live_link_message's reading of an inbound message.
std::string format_live_link_transform_message(const Name& name, const Transform& transform);
```

Pure, header-only (matches the existing file's style — no sockets, fully unit-testable).
`parse_live_link_message` gains the `"type"` handling described above; its existing
`LiveLinkUpdate`/`apply_live_link_update` are unchanged.

### `studio/live_link_server.hpp`/`.cpp` additions

```cpp
// Non-blocking best-effort send on the current connection (does nothing if state() !=
// Connected). A send that would block, or fails, is silently dropped — this is a
// continuous per-tick stream, so a dropped line is superseded by the next tick's send,
// never retried or queued.
void send(const std::string& line);
```

Implementation: append `'\n'`, `::send(peer_fd_, ..., MSG_NOSIGNAL)`; on `EWOULDBLOCK`/
`EAGAIN` or any other error, drop the bytes (no retry, no buffering) — mirrors `poll()`'s
already-established "best-effort, never blocks the Studio frame" contract.

### Studio integration (`studio/studio_app.cpp`)

The existing per-frame Live-Link block gains a play-state branch:

```cpp
if (play_.is_playing())
{
    world.for_each<mosaic::engine::Name>(
        [&](mosaic::engine::World::EntityID e, mosaic::engine::Name& name)
        {
            if (const auto* t = world.get_component<mosaic::engine::transform::Transform>(e))
            {
                live_link_.send(format_live_link_transform_message(name, *t));
            }
        });
    live_link_.poll(); // still drain the socket; inbound is ignored while playing
}
else
{
    for (const std::string& line : live_link_.poll())
    {
        if (auto update = parse_live_link_message(line))
        {
            apply_live_link_update(world, *update);
        }
    }
}
```

Sends happen unconditionally for every named entity, once per Studio frame, with no
change-detection or throttling — this matches the codebase's existing bias toward the
simplest correct thing (YAGNI); if per-entity-only-on-change filtering turns out to matter
for bandwidth with larger scenes, that is a follow-up, not a blocker for this slice.

### Blender addon (`tools/blender_live_link/mosaic_live_link.py`)

- Socket is set non-blocking (`sock.setblocking(False)`) immediately after `connect()`.
- A `bpy.app.timers.register` periodic callback (interval ~0.05s, registered on connect,
  unregistered on disconnect/unregister — same lifecycle as the existing depsgraph
  handler) does a newline-buffered non-blocking `recv()` loop: read what's available,
  split complete `\n`-terminated lines off an accumulating buffer, keep the remainder —
  the same buffering `LiveLinkServer::poll()` already does, mirrored in Python.
- Each complete line is parsed as JSON; a `"type":"transform"` message looks up
  `bpy.data.objects.get(name)` (Blender enforces globally unique object names, so no
  ambiguity case exists on this side) and, if found, applies the inverse-converted
  position/rotation (forcing `rotation_mode = 'QUATERNION'` first)/scale directly to the
  object's transform properties. An unknown name is a silent no-op.
- This write **does** re-trigger `depsgraph_update_post` and send a message back to
  Studio — that is expected and harmless: Studio ignores inbound messages whenever it is
  the one currently sending (i.e. whenever `is_playing()`), so the echo is received,
  parsed, and discarded, never re-applied. It does not compound into a loop because
  Studio's outgoing rate is driven by its own tick, not by what it receives.
- New `_from_mosaic_position`/`_from_mosaic_rotation` functions are the exact inverse of the existing `_to_mosaic_position`/`_to_mosaic_rotation` (see the original one-way slice's spec, `docs/superpowers/specs/2026-09-13-blender-live-link-design.md`, for the axis-conversion formula); `_to_mosaic_*` themselves are unchanged.

## Testing (headless & automated, except the addon itself)

- **`tests/unit/studio/live_link_test.cpp`** (extended):
  - `parse_live_link_message` accepts a message with `"type":"transform"` identically to
    one with `"type"` omitted; a message with an unrecognized `"type"` (e.g. `"mesh"`,
    ahead of the follow-up slice actually implementing it) returns `nullopt`.
  - `format_live_link_transform_message` — a known `Name`+`Transform` produces the exact
    expected JSON string, both for an axis-aligned identity case and a case with all three
    axes distinct and non-zero (catches a field-order mixup (e.g. position/scale swapped) that an identity-only test
    would miss).
  - **Round-trip identity test**: take a `Transform` with distinct non-zero position/rotation/scale on every axis, run it through `format_live_link_transform_message` then straight back through `parse_live_link_message` (no inverse-conversion step needed, since neither function performs any axis conversion) to confirm every field survives the JSON round trip unchanged. A second test confirms a name containing a quote and a backslash also survives the round trip (the JSON-escaping fix).
- **`tests/unit/studio/live_link_server_test.cpp`** (extended): `send()` on a
  `Connected` server delivers the exact bytes (plus trailing `\n`) to the loopback test
  client; `send()` while `Off` or `Listening` (no peer) is a silent no-op (no crash, no
  exception).
- **Manual** (the one part this suite cannot automate): connect Blender, press Play in
  Mosaic with a physics body linked by `Name`, confirm the Blender object visibly follows
  the falling/simulated body; press Stop, confirm moving the Blender object drives Studio
  again (original slice's behavior, unregressed); move the Blender object *while Playing*
  and confirm it has no effect (dropped, not fought/jittered).

## Files

**Modified**
- `studio/live_link.hpp` — `"type"` field handling in `parse_live_link_message`, new
  `format_live_link_transform_message`.
- `studio/live_link_server.hpp` / `studio/live_link_server.cpp` — new `send()`.
- `studio/studio_app.cpp` — play-state-gated send-vs-apply branch.
- `tools/blender_live_link/mosaic_live_link.py` — non-blocking recv timer, inverse axis
  conversion, apply-by-name.
- `tests/unit/studio/live_link_test.cpp`, `tests/unit/studio/live_link_server_test.cpp`
  — extended per Testing above.
- `docs/project/next-steps.md` — mark this slice done.

## Non-goals / follow-ups

- **Mesh/geometry sync** — separate follow-up slice (its own design), touching the asset
  pipeline rather than the wire protocol.
- **Per-entity change detection / send throttling** — every named entity is sent every
  frame while playing; deferred until it's shown to matter.
- **Windows support** — still Linux-only, unchanged from the original slice.
- **CommandStack / undo integration** — still out of scope, matches existing precedent.
