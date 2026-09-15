# Next Steps — Build Handoff

This is the live "you are here" for a worker picking up Mosaic. It points at the
next buildable work and the decisions that constrain it. When the work below is
done, update this file to point at what comes next.

**Last updated:** 2026-09-14 (**Docs hub nav generator — `tools/docs/regenerate_nav.py`** — a Python script that regenerates 6 nav groups in `docs/index.html` from the actual files on disk, closing a real staleness gap: the hand-maintained nav previously listed only 84 files while `docs/superpowers` alone contained 183, missing every doc from recent sessions. The 6 auto-generated groups — Updates, Architecture, Design, RFCs, Specs, Plans — each map 1:1 to a `docs/` subdirectory with its own sort order: Updates is newest-first (descending filename), Architecture/Design/RFCs are alphabetical, and Specs/Plans are oldest-first (ascending filename), so the most actionable docs surface at the top of each group. The generator identifies its target blocks via a marker-comment convention: `<!-- AUTO-GENERATED:key -->` / `<!-- END-AUTO-GENERATED -->` pairs around each of the 6 groups — only the content between those markers is ever rewritten; the General and Project & Legal groups, which sit outside any marker pair, remain fully hand-curated and are never touched by the script. Title derivation reads the first `# ` heading from each file and falls back to the filename (with a warning) when no heading is found; the accepted trade-off is that some older specs and plans whose hand-picked nav labels were shorter now show more verbose titles drawn from their actual headings — a deliberate accuracy-over-brevity call. Reflection-driven API reference docs (part 2 of documentation tooling, per [`docs/design/documentation-tooling.md`](../design/documentation-tooling.md)) remain future work. spec: [`docs/superpowers/specs/2026-09-14-docs-hub-nav-generator-design.md`](../superpowers/specs/2026-09-14-docs-hub-nav-generator-design.md).) ¶ (**Project templates mechanism — `mosaic create --template <name>`** — the first of the remaining Phase 5 (Tool Ecosystem) sub-projects after the Plugin SDK: `create_project` now copies a named `templates/<name>/` directory into the new project instead of building a scene in-process, resolved via a new compile-time `MOSAIC_TEMPLATES_DIR` define mirroring the existing `MOSAIC_FONT_DIR` mechanism. `mosaic create <path> [--template <name>]` (default: `"default"`); an unknown name fails cleanly, naming every available template, with nothing written. `templates/default/scenes/main.json` ships as a one-time, checked-in snapshot of what `build_default_scene()` (`app/run/demo_scene.hpp`) produced at the moment this landed — `build_default_scene()` itself is untouched and keeps its own unrelated caller (`app/run/scene_load.hpp`'s no-scene-file ad-hoc-run fallback), but `create_project`'s `default` path no longer calls it at all, so the two can drift if `build_default_scene()` changes later; a contributor who wants new projects to match needs to regenerate the snapshot manually — documented, not automated. `create_project`'s `types` (`ComponentReflection&`) parameter is kept, unused, rather than touching the 29 other call sites across 7 files that construct and pass it for unrelated reasons; a future cleanup pass could remove it. Full design: `docs/superpowers/specs/2026-09-14-project-templates-mechanism-design.md`. A richer "3rd-person adventure" template (character movement, attack/sneak, health/death states, a basic enemy) remains explicit future work — needs a `Health` component and basic combat/AI scripting that don't exist yet, deliberately deferred to its own spec/plan so this mechanism could ship on its own first.) ¶ (**Plugin SDK slice 3 — custom asset importers (`MOSAIC_IMPORTERS`)** — a plugin can now declare `MOSAIC_IMPORTERS = {".ext": "import_fn"}`; `mosaic import <project> <file>` consults every loaded plugin's dict whenever the source extension isn't one of the four built-in kinds (script/audio/font/PPM-image fallback), invokes the matched one-argument function (receiving the source path, doing its own file I/O), and writes its string return value as the asset's cooked blob (`assets/<stem>.data`) -- register-only for now, loaded back via `AssetRegistry::load<std::string>(id)`, no new gameplay-script getter. New app-tier `ImportPluginHost` (`app/asset/import_plugins.hpp`/`.cpp`) mirrors `studio::PluginHost`'s discovery/all-or-nothing-load semantics but never binds a World, and is deliberately not built on top of `PluginHost` directly -- `mosaic_app` does privately link `mosaic_studio` for the editor launcher (`run/editor.cpp`), but `app/CMakeLists.txt` keeps that dependency (and ImGui/pybind11-studio with it) off every other app-tier header on purpose, and `asset_io.hpp` (this class's only caller) is included too widely to carry it. No `ScriptEngine` interface changes were needed: the return-value capture composes `set_global`/`run_string`/`get_global`, all pre-existing. `load_project_assets`'s previously-implicit "anything not script/audio/font is an image" assumption is now an explicit `texture` check plus a generic opaque-blob fallback, fixing what would otherwise have been a silent mis-decode for any plugin-imported type. A reserved-type collision guard refuses an importer extension that would produce one of the four built-in type names. Full design: `docs/superpowers/specs/2026-09-14-plugin-sdk-custom-importers-design.md`. Phase 5's three planned Plugin SDK extension points (menu items, panels, importers) are now all shipped; native `.so`/`.dll` plugins remain a much larger, still-later sub-project.) ¶ (**Plugin SDK slice 2 — custom Studio panels (`MOSAIC_PANELS`)** — a plugin can now declare `MOSAIC_PANELS = {"Label": "draw_fn"}` alongside (or instead of) `MOSAIC_MENU_ITEMS`; Studio registers each panel into its dockable `StudioLayout` and calls the named function once per frame while the panel is open, inside the panel's own ImGui window. Nine `imgui_*` globals are available to a panel-draw function: `imgui_text`, `imgui_separator`, `imgui_same_line`, `imgui_spacing`, `imgui_button`, `imgui_checkbox`, `imgui_input_text`, `imgui_slider_float`, and `imgui_begin_child`/`imgui_end_child` for scroll regions — a `child_depth_` safety net in the new engine force-closes any mismatched `imgui_begin_child()` and reports it as an error rather than corrupting ImGui's internal child-window stack. This needed a new module, `modules/script_python_studio`, the one deliberate place pybind11 and ImGui are allowed to coexist (mirroring how `modules/render_vulkan` is the one place Vulkan headers live); `modules/script_python` gained a shared `install_world_globals` free function (extracted from `PythonScriptEngine::bind_world`, taking pointer-to-pointer parameters so the globals still read the bound `World`/`ComponentReflection`/`CommandStack` at call time, preserving the pre-existing "undo/redo works even before `bind_commands`" property) so both the gameplay-script engine and the new Studio-panel engine share one implementation instead of duplicating the ~16 globals. `PluginHost` now discovers panels the same way it discovers menu items (all-or-nothing per-file load semantics preserved), and `world_inspector_panel.py` was added to `tools/example_plugins/` as the panel-API counterpart to the existing `world_inspector.py`, with real ImGui scrolling instead of a fixed row cap. Full design: `docs/superpowers/specs/2026-09-14-plugin-sdk-custom-panels-design.md`. Plugin SDK slice 3 (custom asset importers) remains future work. ¶ (**Plugin SDK slice 1 — example plugins tracked + the `plugins/` directory-resolution gap documented** — eight worked example plugins (one per major capability: entity lifecycle, component read/write, `entities_with`, undoable `set_component`/`undo`/`redo`/`undo_depth`, spawn/despawn, `add_component`/`remove_component` toggling, `emit`, and a deliberately-broken one demonstrating error isolation) now live in `tools/example_plugins/` (previously only in a developer's local test project, untracked). `docs/design/scripting.md` gained a "Studio plugins (Plugin SDK)" section plus a documented, not-yet-scheduled gap: `plugins/` resolves as a literal CWD-relative path today (matching how scenes/assets already work), which is fine for "run `mosaic editor` from inside your project directory" but not for an eventual installed/packaged distribution where CWD at launch isn't guaranteed to be the project directory — `mosaic editor <path>`'s existing `Options::open_path` argument doesn't currently `chdir()` or otherwise get threaded through to plugin/scene/asset resolution. Follow-up direction noted: resolve against an explicit tracked project root (the existing `mosaic.toml`/`load_manifest(project_dir)` machinery `mosaic create`/`mosaic build` already use) instead of CWD. ¶ (**Phase 5 (Tool Ecosystem) — Plugin SDK slice 1 (discovery/lifecycle + custom menu items) DONE** — the first Plugin SDK slice: a project-local `plugins/*.py` file declares a module-level `MOSAIC_MENU_ITEMS` dict mapping a menu label string to a zero-argument function name (e.g. `MOSAIC_MENU_ITEMS = {"Say Hello": "on_say_hello"}`), and Studio's new "Plugins" menu lists every registered label, dispatching a click to that function. Each plugin gets its own isolated `ScriptEngine` bound to the current `World` — the exact same `get_component`/`set_component`/`entities_with`/`undo`/`redo`/etc. globals gameplay `ScriptComponent`s already have (`studio/plugin_host.hpp`/`.cpp`, one fresh engine per discovered `.py` file, never shared). `ScriptEngine` gained two small, genuinely neutral additions to make this possible without `studio/` ever depending on pybind11/Python.h directly: `get_global(name)` (reads a global variable back out, marshaled through the same `Value` DOM `set_global` writes with) and `call_function(name)` (invokes an already-defined zero-argument function by name, capturing stdout/errors the same way `run_string` already does). A broken plugin — a file that fails to run, or whose `MOSAIC_MENU_ITEMS` is malformed (not a dict, or a non-string value) — is skipped and its error logged to the Console panel, never blocking sibling plugins in the same directory or crashing Studio; directory scanning itself uses the non-throwing `std::filesystem` overloads throughout (construction *and* iteration) for the same reason, found and fixed during review. No per-plugin enable/disable, manifest, or versioning yet — every `.py` file found loads unconditionally; no native `.so`/`.dll` dynamic-library plugins yet either — this slice is Python-only, the first stage of a "Python first, native later" plan. Plugin SDK slices 2 (custom Studio panels) and 3 (custom asset importers) remain future work, each their own spec/plan; after all three, the rest of Phase 5 (package manager, templates/project generators, documentation tooling) is next. spec: [Plugin SDK discovery + menu items spec](../superpowers/specs/2026-09-13-plugin-sdk-discovery-menu-items-design.md). ¶ (**Blender Live-Link is now BIDIRECTIONAL — Studio → Blender while playing DONE**, streaming physics-driven `Transform` updates from Studio to the Blender addon in real time; direction is gated purely by `PlayState::is_playing()` — Studio drives Blender while playing (e.g. a physics body falling under simulation pushes its transform to the matched Blender object every 50 ms via a `bpy.app.timers` poll), Blender drives Studio while stopped (unchanged from the original slice). Addon side: `_from_mosaic_position`/`_from_mosaic_rotation` inverse conversions (Mosaic Y-up → Blender Z-up), a `recv_buffer` field on `MosaicLiveLinkState`, `_apply_incoming_transform` (sets `location`/`rotation_quaternion`/`scale` on the matched object by name, no-op if absent), and `_poll_incoming` — a non-blocking `recv` loop (drains to `BlockingIOError`, buffers across calls, splits on `\n`, skips malformed/non-transform messages without killing the timer). Connect now calls `s.setblocking(False)` and registers `_poll_incoming`; disconnect and module `unregister()` both unregister it. **Caveat:** `PlayState` defaults to playing on launch, so a freshly opened Studio editor is Studio-authoritative for Live-Link immediately — press Stop to get the original Blender-authoritative behavior. spec: [Bidirectional Live-Link spec](../superpowers/specs/2026-09-13-blender-live-link-bidirectional-transform-design.md). ¶ (**Phase 5 (Tool Ecosystem) STARTED — Blender Live-Link (preview slice) DONE**, the roadmap's "basic Blender bridge (live-link preview)": a Blender object's live transform edits update the matching Mosaic entity's `Transform` in Studio's viewport in real time — one-way (Blender → Studio), transform-only, matched by name. Prerequisite (landed just before this, PR [#145](https://github.com/Evol-Luci/mosaic/pull/145)): a small reflected `Name { std::string value; }` component (`engine/world/name.hpp`) — entities had no stable human-readable identity before this; Studio's Hierarchy panel now shows it too, falling back to `"Entity <index>"` when unset. This slice itself: `studio/live_link.hpp` — pure `parse_live_link_message`/`apply_live_link_update` (no sockets; ambiguous or unmatched `Name` is a no-op, never a partial/first-match apply), reusing `foundation::serialization`'s JSON codec and its established int64/uint64/double dual-accept-as-float pattern. `studio/live_link_server.hpp`/`.cpp` — a small, Linux-only, non-blocking POSIX TCP `LiveLinkServer` (`Off`/`Listening`/`Connected`), single connection at a time, newline-buffered across `recv()` calls. Studio's toolbar gained a "Live Link: Off/Listening/Connected" toggle (starts `Off`, matching this project's explicit-opt-in stance for new background listeners) and a per-frame poll+apply loop in `draw_ui`; no `CommandStack` involvement, matching Studio's existing (command-less) live transform editing. A new `tools/` directory holds `tools/blender_live_link/mosaic_live_link.py`, a single-file Blender addon (pure stdlib `socket` + `bpy.app.handlers.depsgraph_update_post`) that does the Z-up→Y-up axis conversion **before** sending, so Studio-side code only ever sees Mosaic-convention values — mirrors how asset cookers convert at the boundary. Proven **headless & automated** for everything except real Blender: `[studio]` tag green (parse/apply edge cases incl. malformed JSON, missing fields, int/float-mixed arrays, ambiguous/unmatched names; the socket layer tested with a real localhost TCP loopback pair in-process, mirroring how N6 tested `EnetTransport` — connect, a line sent whole, a line split across two `send()` calls, disconnect-back-to-Listening, bind-conflict rejection). **Manual verification status, recorded honestly:** a real Blender install exists on this machine but its own bundled Python is broken (`ModuleNotFoundError: No module named 'math'` from Blender's *own* startup addons, even in a clean environment — see `sandbox-blender-broken-python-stdlib` in project memory), so the addon's actual `bpy`/UI behavior could not be exercised. What *was* verified instead: the addon's real axis-conversion formulas (copy-verified into a plain-`python3` script, since they're pure functions with no `bpy` dependency) produce mathematically correct output (Blender's 90° yaw about its own +Z-up maps to a 90° rotation about Mosaic's +Y-up, with the position axes swapped/negated as designed), and a real cross-process socket send using that script against a throwaway C++ harness wired to the actual `live_link.hpp`/`live_link_server.hpp` code succeeded end-to-end (received, parsed, and applied correctly) — confirming the wire protocol and Studio-side code are correct, just not exercised through real Blender's UI. That cross-process check also surfaced (and got documented, generalizing an earlier UDP-only finding) that this sandbox's cross-process loopback sockets — TCP included, not just ENet's UDP — have an unpredictable delivery delay (seconds to 20+s) that same-process tests never show; see `sandbox-cross-process-udp-connect-delay` in project memory. **Still open:** real Blender GUI verification (a working Blender install, or a different machine) — the one thing this whole slice could not close out itself. Next Phase 5 work: Plugin SDK, package manager, templates, project generators, documentation tooling, per `docs/project/advancement-plans-and-evaluation.md`. spec/plan: [Blender Live-Link spec](../superpowers/specs/2026-09-13-blender-live-link-design.md) · [Blender Live-Link plan](../superpowers/plans/2026-09-13-blender-live-link.md). **Networking N7 (--host/--connect + demo) DONE — the networking pillar (N1-N7) is COMPLETE.**
 N1-N6 are wired into an actual CLI and a full-loop demo: `mosaic run --host <port>` / `--connect <host>:<port>` (`app/cli/options.hpp`/`.cpp`, mutually exclusive, `--connect` validated as `<host>:<port>` at parse time via a custom CLI11 validator; `split_host_port` does the actual split). `setup_runtime` (`app/run/demo_scene.hpp`) grew three trailing-defaulted params (`net_role`, `net_transport`, `input_provider`, following the exact pattern `audio_backend`/`pointer` were each added by) — when a transport is injected it wires `NetworkSystem`+`ReplicationSystem`+`InputSystem` for that role; every pre-existing call site is unaffected. A new standalone, testable `wire_networked_demo_host` (`app/run/net_host_demo.hpp`) is the host-only demo glue: on `NetworkEvent::PeerJoined` it spawns a `NetReplicated{0}` avatar cube (id auto-assigned by N4b) mapped by session id; on `NetworkInput` it parses the opaque `"dx,dz"` payload (the one place that decodes it — the engine itself still never interprets `NetworkInput`, per N5) and moves that session's avatar; on `PeerLeft` it destroys the avatar (N4's despawn then propagates the removal to every client). `main.cpp` glues it together: constructs the real `EnetTransport` from the parsed options, skips normal scene population for `--connect` (starts from an empty world — populated entirely by replication), adds one extra shared `NetReplicated` orbiting cube (reusing `kOrbitScript`, a **fresh** entity — `build_default_scene` itself is untouched, since several tests assert its exact entity count) plus `wire_networked_demo_host` on the host path, and a WASD-driven client input provider (GLFW, mirrors the existing pointer-provider pattern) gated on `--window` (headless clients send nothing, since there's no keyboard to read). CLI parsing (28 `[cli]` cases) and `wire_networked_demo_host` (5 headless `[app][net]` cases over direct World event emission, no sockets) are fully automated, matching every prior networking slice. The manual/two-process part was verified **directly in this session** (not deferred to a maintainer) by running two real local `mosaic run --host`/`--connect` processes over loopback UDP: the client connects, the host logs `peer 1 joined; spawned avatar`, confirming the full N1-N6 chain (transport, session, replication, N4b id assignment) works end-to-end through the real CLI. Two things surfaced by that verification, now recorded for future sessions (see `sandbox-cross-process-udp-connect-delay` in project memory): (1) in **this sandbox specifically**, a genuinely separate `mosaic` server process can take several seconds to see a client's very first UDP packet — reproduced identically with a minimal two-process program using nothing but vendored ENet, so it is an environment characteristic, not an engine bug, and does not affect the fast in-process `[net]`/`[enet]` test suites at all; (2) the client does not currently send an explicit graceful `disconnect()` before exiting, so an killed/exited client's avatar is removed only once ENet's own (larger, default) peer timeout elapses, not immediately — a reasonable follow-up, not a defect. The one piece genuinely left to a maintainer with a real display is whether WASD in the client's window visibly moves that avatar in both windows — headless verification cannot exercise GLFW key state at all. `[cli]` (28 cases) + `[app][net]` (6 cases, including the earlier `setup_runtime` wiring case) + `[app]` (69 cases) green; rest of suite green except the pre-existing flaky `script_system` threaded-Python SIGSEGV (full-suite-ordering only, passes in isolation, networking-unrelated). spec/plan: [N7 spec](../superpowers/specs/2026-09-13-net-n7-host-connect-demo-design.md) · [N7 plan](../superpowers/plans/2026-09-13-net-n7-host-connect-demo.md). **Networking N6 (real ENet transport) DONE** — the neutral `Transport` seam (N1, `engine/net/transport.hpp`) has a real UDP backend: a new module `modules/net_enet` vendors [ENet](https://github.com/lsalzman/enet) (pinned `v1.3.18`) via `FetchContent` and wraps it in `EnetTransport : public Transport` behind two factory functions, `create_enet_server(port, max_peers)` and `create_enet_client(host, port)` — mirroring `modules/audio_miniaudio`'s header/`.cpp` wall exactly (`enet_*` symbols never appear outside `enet_transport.cpp`). `NetworkSystem`/`ReplicationSystem`/`InputSystem` needed **zero changes** — they depend only on the `Transport` interface. `ConnectionId`s come from a monotonic counter mapped through `ENetPeer*` in both directions (never the pointer itself, since ENet reuses peer pointers after disconnect); `Channel::Reliable`/`Unreliable` map to ENet channels 0/1; `enet_initialize`/`enet_deinitialize` are refcounted behind a small static guard so multiple transports coexist in one process. Proven **headless & fully automated** over real UDP on `127.0.0.1` — no manual step, no hardware, no separate machines (connect handshake; reliable send both directions; unreliable channel tag preserved; disconnect surfaces an event on the peer; send to an unknown id is a no-op; multiple clients get distinct ids). This slice surfaced and fixed a real concurrency bug worth recording: `Transport::poll()` is drain-once, so a naive `poll_until` test helper that *also* polls on its own behalf (in addition to the predicate polling) can silently discard an event the very iteration it arrives — the server's `Connected` event arrived one iteration after the client's and was lost by the helper's own redundant poll before the test's predicate ever got to collect it. Fixed by making `poll_until` a pure paced-retry helper with **all** polling living in the predicate. Relatedly, `enet_peer_send`/`enet_host_connect` only **queue** — actually transmitting requires the *sender's own* subsequent `poll()` (`enet_host_service` call), so every wait loop after a `send()` or `connect()` must keep polling the initiator, not just the recipient. `[net]` (46 cases) + `[enet]` (6 cases) green; rest of suite green except the pre-existing flaky `script_system` threaded-Python SIGSEGV (full-suite-ordering only, passes in isolation, networking-unrelated). Also fixed in passing: a stray `git log`-style fragment (`bb88b53 (feat(net): ...)`) had been accidentally spliced into this file's N4b entry during that slice's rebase conflict resolution — removed. Next and **last** networking slice: **N7** — `mosaic run --host <port>`/`--connect <host:port>` CLI flags, `setup_runtime` wiring (inject an `EnetTransport` the same way `audio_backend` is injected), and a demo scene. This is the **first genuinely manual/maintainer-verified step** in the whole networking pillar — it needs two actually-running processes, unlike every slice so far. spec/plan: [N6 spec](../superpowers/specs/2026-09-13-net-n6-enet-transport-design.md) · [N6 plan](../superpowers/plans/2026-09-13-net-n6-enet-transport.md). **Networking N4b (server-side NetId assignment) DONE** — the last open item from N4 is closed: `snapshot()` (`engine/net/replication.hpp`) now takes a `NetIdAllocator& alloc` (a monotonic `NetId next = 1` counter) and, while iterating `NetReplicated` entities, assigns `nr.id = alloc.next++` **in place** to any entity whose id is still `0` — game/script code no longer has to hand-pick a unique id when marking an entity replicated. An explicitly-set nonzero id is left untouched, and re-snapshotting an already-assigned entity never reassigns it (the allocator only fills gaps, one-way). `ReplicationSystem` owns the allocator as a new server-role-only member (`alloc_`, symmetric with the client-role-only `ids_` `NetIdMap`) and threads it into its per-tick `snapshot()` call — no protocol change, no new message type. Proven **headless & deterministic** (a zero-id entity gets a nonzero id after one `snapshot()` call; two zero-id entities in the same call get two distinct ids; a pre-set nonzero id is left alone; a second `snapshot()` call with the same allocator doesn't reassign; **end-to-end** over the loopback: a server entity created with `NetReplicated{0}` gets assigned in place and the client mirrors it under that id). `[net]` (33 cases) green; rest of suite green except the pre-existing flaky `script_system` threaded-Python SIGSEGV (full-suite-ordering only, passes in isolation, networking-unrelated). This lands right after N5 (client input, PR [#141](https://github.com/Evol-Luci/mosaic/pull/141)) merged just before it — the two touch disjoint files and needed only a routine rebase, no design change. With both merged, the only remaining Phase-4 networking work is the real **ENet UDP backend** + `mosaic run --host`/`--connect` + `setup_runtime`/demo — the last, maintainer-verified slice (mirrors audio A1→A2). **Networking N4 (despawn / removal replication) DONE** — `apply_snapshot` (`engine/net/replication.hpp`) is now **authoritative**: beyond N3's create-on-first-sight + `set_value` upsert, it records every `NetId` present in the (full) snapshot into a `std::unordered_set<NetId> seen` and, **on the success path only**, destroys every tracked `NetId` **absent** from it — `world.destroy_entity(ids.map[net])` + erase from the `NetIdMap` — so a server-side removal propagates to the client with **no new message type** (the full snapshot is ground truth; removal collects keys first, then erases, never mutating the map mid-iteration). `World::destroy_entity` is **deferred** (committed at end of `tick`); the map erase is **immediate**. A malformed/partial snapshot still returns `false` and removes nothing (membership reconciles only against a fully-parsed snapshot). Proven **headless & deterministic** (direct: a 2-entity client, after a 1-entity snapshot, has its `NetIdMap` shrink to 1 immediately and drops to 1 entity after a `tick` commits the deferred destroy, with the net-1 entity surviving; re-applying an identical snapshot removes nothing; **end-to-end** over the loopback: `server.destroy_entity(B)` → after pumping, the client drops exactly B and keeps net 1). `[net]` (28 cases) + `[replication]` (9 cases) green; rest of suite green except the pre-existing flaky `script_system` threaded-Python SIGSEGV (full-suite-ordering only, passes in isolation, networking-unrelated). Next networking slices: **N4b** server-side `NetId` assignment policy (stamp a unique monotonic id on `NetReplicated` entities created with `id == 0`, so game/script code need not hand-pick ids); **N5** client→server input/commands (the `send_to_server` counterpart of `broadcast`); the real **ENet UDP backend** + `mosaic run --host`/`--connect` + `setup_runtime`/demo — later maintainer-verified slice. spec/plan: [N4 spec](../superpowers/specs/2026-09-10-net-n4-despawn-design.md) · [N4 plan](../superpowers/plans/2026-09-10-net-n4-despawn.md). **Networking N3-follow (replication send-wiring) DONE** — N3's snapshot mechanism is now wired **over the live N2 session**, so a client World mirrors the server World across the network. `NetworkSystem` became the **single transport hub** (`engine/net/network_system.hpp`): it alone polls, still runs the Hello/Welcome handshake, and now buffers **non-control** frames into a private queue surfaced via drain-once `take_inbound() → std::vector<InboundMessage{peer,type,payload}>` (server tags `peer` = sender's session id and buffers only from an established session; client tags `peer = 0`), plus a server `broadcast(Channel, bytes)` that sends to every session's `ConnectionId` (zero clients = no-op). A new `MessageType::Snapshot = 3` (accepted by `decode`) frames the payload. A new `ReplicationSystem` (`engine/net/replication_system.hpp`, ctor `(Role, NetworkSystem*, const ComponentReflection*)`, added to the World **after** `NetworkSystem`) drives it: on the **server** it broadcasts `encode(Snapshot, snapshot(world, types))` each tick when `clients()` is non-empty (full snapshot, no delta); on the **client** it drains `take_inbound()` and `apply_snapshot()`s each Snapshot into its own `NetIdMap`. Channel::Reliable. Proven **headless & deterministic** with two loopback-linked Worlds (`message` Snapshot round-trip; hub buffering/`take_inbound` drain-once/`broadcast` fan-out + zero-client no-op + unknown-conn drop; **end-to-end**: client mirrors **only** the `NetReplicated` entity — Transform+Material match, no marker, unmarked entity absent — then moving the server entity updates the client in place with **no duplicate** across repeated per-tick snapshots). Pure engine, **no** client→server send (N5), despawn (N4), delta/throttling/unreliable, or `setup_runtime`/CLI/demo wiring (ENet slice). `[net]` (25 cases) + `[replication]` (6 cases) fully green; rest of suite green except the same pre-existing flaky `script_system` threaded-Python SIGSEGV (full-suite-ordering only, passes in isolation, networking-unrelated). Next networking slices: **N4** entity despawn/removal replication (server removal → client removal) + authoritative spawn/despawn messages + server-side `NetId` assignment policy; **N5** client→server input/commands (the `send_to_server` counterpart of `broadcast`); the real **ENet UDP backend** + `mosaic run --host`/`--connect` + `setup_runtime`/demo — later maintainer-verified slice. spec/plan: [N3-follow spec](../superpowers/specs/2026-09-10-net-n3-follow-replication-sync-design.md) · [N3-follow plan](../superpowers/plans/2026-09-10-net-n3-follow-replication-sync.md). **Networking N3 (server→client state replication) DONE** — the core of server-authoritative networking, reusing the reflection + JSON layers exactly as `reflection.md` anticipated. A reflected `NetReplicated { std::uint32_t id }` marker (`engine/net/net_replicated.hpp`, `NetId = uint32_t`, registered `"NetReplicated"`) tags server entities with a stable net id. `snapshot(World&, ComponentReflection&) → std::string` (`engine/net/replication.hpp`) JSON-encodes every `NetReplicated` entity as `{ "entities": [ { "net": <u>, "components": { "<Name>": <Value>, … } } ] }` — each entity's reflected components via `component_value`/`registered_names`, **excluding the marker itself** (its net id is the key). Client side: a `NetIdMap { unordered_map<NetId, EntityID> }` (client-held) + `apply_snapshot(blob, World&, ComponentReflection&, NetIdMap&) → bool` that **never throws** (malformed JSON/shape → `false` via `try{from_json}catch` + `is<Table>`/`is<Array>` guards), reads `net` accepting **either uint64 or int64** (a JSON round-trip may narrow), **creates-on-first-sight** (else map hit → same entity), then `set_value`-upserts each component — so re-applying is idempotent and moves update in place with no duplicates; the client mirror carries **no** `NetReplicated` component. Proven **headless & deterministic** with two in-process Worlds and bytes passed directly (5 `[replication]` cases: single-entity replicate w/ Transform+Material match + marker-absent, idempotent upsert + move-in-place, multi-entity selectivity — an unmarked entity is excluded, malformed/empty-`{}`/empty-world). Pure engine, no `NetworkSystem` wiring / despawn / delta / interpolation (all later). `[net]` + `[replication]` fully green (19 net cases / 26 replication assertions); rest of suite green **except one pre-existing flaky test** — `script_system_test.cpp` "threaded ScriptSystem applies script writes one tick later" SIGSEGVs only under full-suite ordering (embedded-Python GIL/interpreter-teardown under accumulated thread state), passes 3/3 in isolation, and **still fails with N3's registration stripped out** → N3-unrelated. Follow-ups: **N3-follow (send-wiring)** — a `ReplicationSystem` where the server builds+sends a snapshot each tick over the N2 `NetworkSystem`/`Transport` and the client applies received snapshots keyed by session (small, now that the mechanism exists); **N4** entity spawn/despawn replication + authoritative spawn/despawn messages + server-side `NetId` assignment policy; **N5** client→server input/commands; the real **ENet UDP backend** + `mosaic run --host`/`--connect` remains a later maintainer-verified slice. spec/plan: [N3 spec](../superpowers/specs/2026-09-09-net-n3-replication-design.md) · [N3 plan](../superpowers/plans/2026-09-09-net-n3-replication.md). **Networking N2 (session lifecycle + NetworkSystem) DONE** — the session layer on top of N1's transport: a message envelope (`engine/net/message.hpp`: a 1-byte `MessageType` + payload, `Hello`/`Welcome`, `put_u32`/`get_u32` LE helpers), a `NetworkEvent` bus event (`engine/net/network_event.hpp`: `PeerJoined`/`PeerLeft`/`SessionEstablished`/`Disconnected`), and a `NetworkSystem` (`engine/net/network_system.hpp`, an `ecs::System` with an injected `Transport` + server/client `Role`) that each tick drains `transport->poll()` and runs the **Hello/Welcome handshake** — the server assigns a **monotonic session id** (from 1, decoupled from the transport `ConnectionId`) on Hello and replies Welcome; the client sends Hello on connect and adopts its id from Welcome. Exposes `role()`/`is_connected()`/`local_id()`/`clients()` and emits the bus events; writes no ECS state. Headless-tested (message round-trip + reject; end-to-end handshake over the N1 loopback with two Worlds → `client.local_id()==1`, `server.clients()=={1}`, PeerJoined/SessionEstablished fired; server multi-client monotonic ids + `Welcome` capture + duplicate-Hello idempotence + disconnect→PeerLeft via an in-test `FakeTransport`). Pure engine, no sockets/CLI/`setup_runtime` wiring; full suite 585 green. Networking decomposition: N1 done → **N2 done** → **N3** server→client state replication (reflection → `Value` snapshots keyed by session id) → **N4** entity spawn/despawn replication + a net-id ↔ `EntityID` map → **N5** client→server input/commands; the real **ENet UDP backend** + `mosaic run --host`/`--connect` is a later, maintainer-verified slice (mirrors audio A1→A2). spec/plan: [N2 spec](../superpowers/specs/2026-09-09-net-n2-session-design.md) · [N2 plan](../superpowers/plans/2026-09-09-net-n2-session.md). **Networking N1 (neutral Transport + in-memory loopback) DONE** — the last Phase-4 subsystem is underway. A backend-free `Transport` seam (`engine/net/transport.hpp`: `send(ConnectionId, Channel, bytes)` / `poll() → vector<NetEvent>` / `disconnect`; `NetEvent` = Connected/Disconnected/Message; `Channel` = Reliable/Unreliable; opaque `std::string` payloads) + a header-only in-process `make_loopback_pair()` (`engine/net/loopback_transport.hpp`) that links a server+client sharing per-direction queues: `send` enqueues to the peer's inbound tagged with the sender id, `poll` drains this endpoint's inbound (Connected latched once, then Messages FIFO, then Disconnected on close); stable ids (server=1, client=2). Headless-tested (6 `[net]` cases: connect handshake, reliable both directions + no self-receive, unreliable tag preserved, FIFO, disconnect + drop-later, drop-to-unknown) — no sockets, fully deterministic. **The networking pillar targets server-authoritative replication** (a server owns the World; clients send input and receive serialized component-state snapshots). Networking decomposition: **N1 done** → **N2** session/connection lifecycle + a `NetworkSystem` (client vs server role, handshake, `mosaic run --host`/`--connect`) → **N3** server→client state replication (reflection → `Value` snapshots — `reflection.md` anticipates this) → **N4** entity spawn/despawn replication + a net-id ↔ `EntityID` map → **N5** client→server input/commands (via the `CommandStack`/event bus); a real **ENet UDP backend** (module-walled, maintainer-verified on a real network) is a later slice, mirroring audio A1→A2. spec/plan: [N1 spec](../superpowers/specs/2026-09-09-net-n1-transport-design.md) · [N1 plan](../superpowers/plans/2026-09-09-net-n1-transport.md). **UI pillar COMPLETE — U5b (world-space text labels) DONE** — floating 3D text: a reflected `WorldLabel` (`engine/ui/world_label.hpp`: font `AssetID` + text + world `size`/`color`/`alpha`/`offset`, registered `"WorldLabel"`, script-mutable) renders **camera-facing** at an entity's `WorldTransform` position — nameplates/labels. A pure `build_world_text` (`engine/ui/world_text.hpp`) reuses `layout_text` and places glyphs on a camera-facing billboard via the camera right/up axes (centered on the anchor), producing world-space `WorldGlyphVertex`es. `RenderSystem` runs a **world-label pass** (its own block deriving `cam_right`/`cam_up` from `inverse_affine(view)`, after the billboard section, **before** the HUD overlay) that resolves each font's atlas via `font_textures_` and submits through a new `draw_world_text` seam; a Vulkan pipeline modeled on the **ribbon** pipeline (CPU vertex stream + `view_proj` push) + the atlas descriptor draws them **depth-tested** (occluded by nearer geometry, write off). Demo: a floating "Orbiter" label tracking the orbiting cube. Headless-tested (`WorldLabel` reflection; `build_world_text` count/centering/empty; RenderSystem world-label pass incl. no-`WorldTransform`/empty skip); full suite 571 green; `mosaic run --window` exits 0; **the on-screen label is the maintainer's hardware check**. spec/plan: [U5b spec](../superpowers/specs/2026-09-09-ui-u5b-world-labels-design.md) · [U5b plan](../superpowers/plans/2026-09-09-ui-u5b-world-labels.md). **The UI pillar (U1 HUD panels → U2 text → U3 images → U4 click interaction → U5a layout/hierarchy → U5b world-space labels) is now COMPLETE.** **Remaining Phase-4 subsystem: networking.** **UI pillar — U5a (layout / hierarchy) DONE** — HUD elements **nest**: a child positions relative to its parent container's rect instead of the whole screen. `resolve_rect` generalized to `resolve_in(frame, anchor, pos, size)` (the screen is just the root frame `{0,0,1,1}`, and `resolve_rect` is the screen-frame case, bit-identical); a reflected `UiParent { EntityID }` (`engine/ui/ui_parent.hpp`, registered `"UiParent"`) names a child's container (an entity with a `UiPanel`); `engine/ui/ui_hierarchy.hpp` resolves each element's screen rect by walking the parent-`UiPanel` chain (`resolve_parent_frame`/`resolve_ui_rect`, depth-capped at 16 → cycle-safe, mirroring `TransformSystem`). **Every rect site routes through it** — the panel/image/text render passes and the `UiSystem` button hit-test — so a nested element (and `layout_text`, which gained a `frame` param so nested text scales with its parent) positions within its parent panel; nesting composes. Demo: a child `UiText` label parented to the camera's HUD bar (no `Transform`, so default-scene counts are unchanged). Headless-tested (`resolve_in`; frame resolution incl. no-parent/invalid/cyclic; `UiParent` reflection; a hierarchy render case; `layout_text` frame); full suite 565 green; `mosaic run --window` exits 0; **the on-screen nested layout is the maintainer's hardware check**. spec/plan: [U5a spec](../superpowers/specs/2026-09-08-ui-u5a-hierarchy-design.md) · [U5a plan](../superpowers/plans/2026-09-08-ui-u5a-hierarchy.md). **UI pillar — U4 (HUD interaction / click) DONE** — the HUD is now **clickable**: a neutral `PointerState` input seam (`engine/ui/pointer.hpp`: normalized x/y + `down`) + a reflected `UiButton` hit region (`engine/ui/ui_button.hpp`: anchor + normalized pos/size + an `event` name, registered `"UiButton"`) + the **first stateful `UiSystem`** (`engine/ui/ui_system.hpp`, an `ecs::System` with an injected `std::function<PointerState()>` provider) that detects a press-and-release **inside the same button** and emits a named `ScriptEvent{event, {entity, action:"click"}}` — delivered to `on_event(name, payload)` one tick later, exactly like the physics `'contact'` bridge. This is the **first runtime input path** (mouse never reached the World before). The engine stays windowing-agnostic: the pointer provider is filled from **GLFW only in `main.cpp`** (`glfwGetCursorPos`/`glfwGetMouseButton`, normalized by window size); headless/editor use a null provider (nothing clicks). `setup_runtime` gained a defaulted `pointer` param and adds `UiSystem`. Demo: a `UiButton` overlays the HUD bar and a `"hud_click"` log subscriber prints on click (no new entity, no script edits). Headless-tested (`UiButton` reflection + registration; `UiSystem` click semantics via a fake pointer provider — click, release-outside, press-outside, never-down); full suite 556 green; `mosaic run --window` exits 0; **the on-screen click is the maintainer's hardware check**. spec/plan: [U4 spec](../superpowers/specs/2026-09-08-ui-u4-interaction-design.md) · [U4 plan](../superpowers/plans/2026-09-08-ui-u4-interaction.md). **UI pillar — U3 (HUD images/sprites) DONE** — a reflected `UiImage` (`engine/ui/ui_image.hpp`: texture `AssetID` + anchor/normalized-pos/size + `tint`/`alpha`/`order`, registered `"UiImage"`, script-mutable) draws a textured, tinted, alpha-blended sprite stretched to an anchored screen rect — essentially U1's `UiPanel` plus a texture, reusing `resolve_rect`. `RenderSystem` runs an **image pass after the panel pass** (sprites over panels), resolving the texture through the existing `texture_cache_` and submitting `UiImageQuad`s via a new `draw_ui_image` seam; a Vulkan screen-space textured pipeline (`ui_image.vert/.frag`, push-constant rect+tint, samples the texture via `texture_set_layout_`, `out = texture.rgb*tint.rgb, texture.a*tint.a`, swapchain + viewport, depth off) draws them — modeled on the U1 `ui` + decal pipelines. Demo: a checker sprite top-right on the camera. Headless-tested (`UiImage` reflection + registration, RenderSystem image pass via `FakeRenderDevice` incl. skip-transparent + nil-texture); full suite 540 green; `mosaic run --window` exits 0 with no validation errors; **the on-screen sprite is the maintainer's hardware check**. spec/plan: [U3 spec](../superpowers/specs/2026-09-08-ui-u3-images-design.md) · [U3 plan](../superpowers/plans/2026-09-08-ui-u3-images.md). **UI pillar — U2b (HUD text rendering) DONE** (PR [#130](https://github.com/Evol-Luci/mosaic/pull/130)) — reflected `UiText` + a pure aspect-correct `layout_text` + a `draw_ui_text` seam + a RenderSystem text pass + a Vulkan textured glyph pipeline + a built-in demo font; completes **U2 (text)**. **UI pillar — U2a (FontAtlas cooked asset) DONE** — `mosaic import <proj> font.ttf` bakes a glyph atlas + per-codepoint metrics via **stb_truetype at cook time** (vendored single header, walled in `app/asset/font_cook.cpp` — no stb symbol above the importer, mirroring the WAV parser); a neutral `FontAtlas` (`engine/ui/font_atlas.hpp`: RGBA8 atlas with glyph coverage in the **alpha** channel + `Glyph` metrics + vertical metrics) is serialized as a `"font"` asset (`.font` `MOSAFNT1` blob, byte-exact round-trip) and registered by `load_project_assets` — mirrors the T5 image / A3 audio cook pipelines. Headless-tested (read_ttf metrics on a vendored Liberation/OFL fixture, cook/uncook round-trip + reject, import→load end-to-end); full suite green + real `mosaic import` CLI round-trip verified (`type = 'font'`). Nothing renders yet — **U2b** (UiText component + string layout + a glyph draw seam + a Vulkan text pipeline + demo text) is next. spec/plan: [U2a spec](../superpowers/specs/2026-09-08-ui-u2a-font-atlas-asset-design.md) · [U2a plan](../superpowers/plans/2026-09-08-ui-u2a-font-atlas-asset.md). **UI pillar — U1 (screen-space HUD foundation) DONE** (PR [#128](https://github.com/Evol-Luci/mosaic/pull/128)) — reflected `UiPanel` + a pure `resolve_rect` layout model + a `draw_ui` seam + a topmost RenderSystem HUD pass + a Vulkan screen-space quad pipeline + a demo HUD bar; headless-tested, GPU visual maintainer-verified. **UI decomposition COMPLETE:** U1 (HUD panels) → U2 (text) → U3 (images/sprites) → U4 (interaction/click) → U5a (layout/hierarchy) → **U5b (world-space text labels)** — all done. **The UI pillar is complete; the only remaining Phase-4 subsystem is networking.** UI follow-ups: hover/press events + visual feedback, keyboard/gamepad focus, text input fields, inherited alpha/visibility + clipping/scroll, flow/grid auto-layout, unifying panels/images/text under one `order`-sorted pass. **VFX pillar COMPLETE** — trails (billboard + ribbon), **decals** (textured world quads), and **screen effects** (fullscreen fade/tint/vignette overlay) all shipped, GPU visuals maintainer-verified; **VFX — V2 ribbon trails DONE** — trails render as a connected camera-facing ribbon via a new `draw_ribbons` seam + Vulkan triangle pipeline (visual is maintainer-verified); **VFX pillar started — V1 trails** — a `TrailRenderer`/`TrailSystem` fading tail; demo trail on the falling sphere; **Audio pillar COMPLETE (A1–A4)** — `AudioSystem` + a real miniaudio device wired into `setup_runtime`; `mosaic run --window` plays a 3D demo tone on the orbiter (audible check is the maintainer's); **Audio — A3** — cooked `.wav`→`AudioClip` asset (`mosaic import foo.wav`); 16-bit-PCM/float32 WAV parser, loads + plays through the null backend — done; **Audio pillar — A2** — a real miniaudio backend (`modules/audio_miniaudio`) implementing the neutral `AudioBackend`, proven headless on miniaudio's null device (no hardware in the sandbox) — done; **Audio pillar — A1** — neutral `AudioBackend` + reflected `AudioSource`/`AudioListener` + an `AudioSystem` driving voices from ECS state, headless via a fake backend (no device yet) — done; **particle cubic easing** — EaseCubicIn/Out/InOut added to the over-life easing set — done; **particle sub-frame spawn interpolation** — a moving emitter's continuous spawns spread along its swept path instead of clumping; orbiter trail demo — done; **script-driven particle emission** — a script bumps a scripted emitter's `burst_trigger` to fire bursts; demo puffer added — done; **particle triggered/re-armed bursts** — a `burst_trigger` edge counter re-fires a burst at runtime — done; **particle over-life easing** — per-property Linear/EaseIn/EaseOut/EaseInOut curves behind the over-life seam — done; **particle over-life curves** — color/size/alpha interpolate start→end over each particle's life — done; **particle rendering** — instanced billboards — done; **particles** Phase-4 subsystem done; v0.1 foundation + rendering + physics + Project & Tools Workflow **Networking N5 (client input) DONE** — a client can now send opaque per-tick input payloads to the server, surfaced there as a `NetworkInput` bus event. A new `MessageType::Input` frame was added, `NetworkSystem` gained a client-side `send_to_server` method, and a new `InputSystem` handles the logic: the client role polls an injected provider and sends each payload, while the server role drains inbound `Input` frames and emits `NetworkInput{session, payload}` on the World bus. The engine never interprets the payload; it remains an opaque `std::string`. Proven **headless & deterministic** (Input frame round-trips through encode/decode, client `send_to_server` flows to server connection, client `InputSystem` polls provider and sends, server `InputSystem` emits `NetworkInput` event for received frames; **end-to-end** over the loopback: `client_world` provider yields payload → `server_world` receives it mapped to the correct session id). `[net]` (35 cases) + `[input]` (8 cases) green; rest of suite green except the pre-existing flaky `script_system` threaded-Python SIGSEGV (full-suite-ordering only, passes in isolation, networking-unrelated). Next networking slices: **N4b (still DEFERRED):** server-side `NetId` assignment policy; the real **ENet UDP backend** + `mosaic run --host`/`--connect` + `setup_runtime`/demo. Note: applying `NetworkInput` in game/script logic (movement, actions) is an explicit non-goal of N5 and remains open work for whoever builds the first networked gameplay demo. spec/plan: [N5 spec](../superpowers/specs/2026-09-12-net-n5-client-input-design.md) · [N5 plan](../superpowers/plans/2026-09-12-net-n5-client-input.md). **Networking N4 (despawn / removal replication) DONE** — `apply_snapshot` (`engine/net/replication.hpp`) is now **authoritative**: beyond N3's create-on-first-sight + `set_value` upsert, it records every `NetId` present in the (full) snapshot into a `std::unordered_set<NetId> seen` and, **on the success path only**, destroys every tracked `NetId` **absent** from it — `world.destroy_entity(ids.map[net])` + erase from the `NetIdMap` — so a server-side removal propagates to the client with **no new message type** (the full snapshot is ground truth; removal collects keys first, then erases, never mutating the map mid-iteration). `World::destroy_entity` is **deferred** (committed at end of `tick`); the map erase is **immediate**. A malformed/partial snapshot still returns `false` and removes nothing (membership reconciles only against a fully-parsed snapshot). Proven **headless & deterministic** (direct: a 2-entity client, after a 1-entity snapshot, has its `NetIdMap` shrink to 1 immediately and drops to 1 entity after a `tick` commits the deferred destroy, with the net-1 entity surviving; re-applying an identical snapshot removes nothing; **end-to-end** over the loopback: `server.destroy_entity(B)` → after pumping, the client drops exactly B and keeps net 1). `[net]` (28 cases) + `[replication]` (9 cases) green; rest of suite green except the pre-existing flaky `script_system` threaded-Python SIGSEGV (full-suite-ordering only, passes in isolation, networking-unrelated). Next networking slices: **N4b** server-side `NetId` assignment policy (stamp a unique monotonic id on `NetReplicated` entities created with `id == 0`, so game/script code need not hand-pick ids); **N5** client→server input/commands (the `send_to_server` counterpart of `broadcast`); the real **ENet UDP backend** + `mosaic run --host`/`--connect` + `setup_runtime`/demo — later maintainer-verified slice. spec/plan: [N4 spec](../superpowers/specs/2026-09-10-net-n4-despawn-design.md) · [N4 plan](../superpowers/plans/2026-09-10-net-n4-despawn.md). **Networking N3-follow (replication send-wiring) DONE** — N3's snapshot mechanism is now wired **over the live N2 session**, so a client World mirrors the server World across the network. `NetworkSystem` became the **single transport hub** (`engine/net/network_system.hpp`): it alone polls, still runs the Hello/Welcome handshake, and now buffers **non-control** frames into a private queue surfaced via drain-once `take_inbound() → std::vector<InboundMessage{peer,type,payload}>` (server tags `peer` = sender's session id and buffers only from an established session; client tags `peer = 0`), plus a server `broadcast(Channel, bytes)` that sends to every session's `ConnectionId` (zero clients = no-op). A new `MessageType::Snapshot = 3` (accepted by `decode`) frames the payload. A new `ReplicationSystem` (`engine/net/replication_system.hpp`, ctor `(Role, NetworkSystem*, const ComponentReflection*)`, added to the World **after** `NetworkSystem`) drives it: on the **server** it broadcasts `encode(Snapshot, snapshot(world, types))` each tick when `clients()` is non-empty (full snapshot, no delta); on the **client** it drains `take_inbound()` and `apply_snapshot()`s each Snapshot into its own `NetIdMap`. Channel::Reliable. Proven **headless & deterministic** with two loopback-linked Worlds (`message` Snapshot round-trip; hub buffering/`take_inbound` drain-once/`broadcast` fan-out + zero-client no-op + unknown-conn drop; **end-to-end**: client mirrors **only** the `NetReplicated` entity — Transform+Material match, no marker, unmarked entity absent — then moving the server entity updates the client in place with **no duplicate** across repeated per-tick snapshots). Pure engine, **no** client→server send (N5), despawn (N4), delta/throttling/unreliable, or `setup_runtime`/CLI/demo wiring (ENet slice). `[net]` (25 cases) + `[replication]` (6 cases) fully green; rest of suite green except the same pre-existing flaky `script_system` threaded-Python SIGSEGV (full-suite-ordering only, passes in isolation, networking-unrelated). Next networking slices: **N4** entity despawn/removal replication (server removal → client removal) + authoritative spawn/despawn messages + server-side `NetId` assignment policy; **N5** client→server input/commands (the `send_to_server` counterpart of `broadcast`); the real **ENet UDP backend** + `mosaic run --host`/`--connect` + `setup_runtime`/demo — later maintainer-verified slice. spec/plan: [N3-follow spec](../superpowers/specs/2026-09-10-net-n3-follow-replication-sync-design.md) · [N3-follow plan](../superpowers/plans/2026-09-10-net-n3-follow-replication-sync.md). **Networking N3 (server→client state replication) DONE** — the core of server-authoritative networking, reusing the reflection + JSON layers exactly as `reflection.md` anticipated. A reflected `NetReplicated { std::uint32_t id }` marker (`engine/net/net_replicated.hpp`, `NetId = uint32_t`, registered `"NetReplicated"`) tags server entities with a stable net id. `snapshot(World&, ComponentReflection&) → std::string` (`engine/net/replication.hpp`) JSON-encodes every `NetReplicated` entity as `{ "entities": [ { "net": <u>, "components": { "<Name>": <Value>, … } } ] }` — each entity's reflected components via `component_value`/`registered_names`, **excluding the marker itself** (its net id is the key). Client side: a `NetIdMap { unordered_map<NetId, EntityID> }` (client-held) + `apply_snapshot(blob, World&, ComponentReflection&, NetIdMap&) → bool` that **never throws** (malformed JSON/shape → `false` via `try{from_json}catch` + `is<Table>`/`is<Array>` guards), reads `net` accepting **either uint64 or int64** (a JSON round-trip may narrow), **creates-on-first-sight** (else map hit → same entity), then `set_value`-upserts each component — so re-applying is idempotent and moves update in place with no duplicates; the client mirror carries **no** `NetReplicated` component. Proven **headless & deterministic** with two in-process Worlds and bytes passed directly (5 `[replication]` cases: single-entity replicate w/ Transform+Material match + marker-absent, idempotent upsert + move-in-place, multi-entity selectivity — an unmarked entity is excluded, malformed/empty-`{}`/empty-world). Pure engine, no `NetworkSystem` wiring / despawn / delta / interpolation (all later). `[net]` + `[replication]` fully green (19 net cases / 26 replication assertions); rest of suite green **except one pre-existing flaky test** — `script_system_test.cpp` "threaded ScriptSystem applies script writes one tick later" SIGSEGVs only under full-suite ordering (embedded-Python GIL/interpreter-teardown under accumulated thread state), passes 3/3 in isolation, and **still fails with N3's registration stripped out** → N3-unrelated. Follow-ups: **N3-follow (send-wiring)** — a `ReplicationSystem` where the server builds+sends a snapshot each tick over the N2 `NetworkSystem`/`Transport` and the client applies received snapshots keyed by session (small, now that the mechanism exists); **N4** entity spawn/despawn replication + authoritative spawn/despawn messages + server-side `NetId` assignment policy; **N5** client→server input/commands; the real **ENet UDP backend** + `mosaic run --host`/`--connect` remains a later maintainer-verified slice. spec/plan: [N3 spec](../superpowers/specs/2026-09-09-net-n3-replication-design.md) · [N3 plan](../superpowers/plans/2026-09-09-net-n3-replication.md). **Networking N2 (session lifecycle + NetworkSystem) DONE** — the session layer on top of N1's transport: a message envelope (`engine/net/message.hpp`: a 1-byte `MessageType` + payload, `Hello`/`Welcome`, `put_u32`/`get_u32` LE helpers), a `NetworkEvent` bus event (`engine/net/network_event.hpp`: `PeerJoined`/`PeerLeft`/`SessionEstablished`/`Disconnected`), and a `NetworkSystem` (`engine/net/network_system.hpp`, an `ecs::System` with an injected `Transport` + server/client `Role`) that each tick drains `transport->poll()` and runs the **Hello/Welcome handshake** — the server assigns a **monotonic session id** (from 1, decoupled from the transport `ConnectionId`) on Hello and replies Welcome; the client sends Hello on connect and adopts its id from Welcome. Exposes `role()`/`is_connected()`/`local_id()`/`clients()` and emits the bus events; writes no ECS state. Headless-tested (message round-trip + reject; end-to-end handshake over the N1 loopback with two Worlds → `client.local_id()==1`, `server.clients()=={1}`, PeerJoined/SessionEstablished fired; server multi-client monotonic ids + `Welcome` capture + duplicate-Hello idempotence + disconnect→PeerLeft via an in-test `FakeTransport`). Pure engine, no sockets/CLI/`setup_runtime` wiring; full suite 585 green. Networking decomposition: N1 done → **N2 done** → **N3** server→client state replication (reflection → `Value` snapshots keyed by session id) → **N4** entity spawn/despawn replication + a net-id ↔ `EntityID` map → **N5** client→server input/commands; the real **ENet UDP backend** + `mosaic run --host`/`--connect` is a later, maintainer-verified slice (mirrors audio A1→A2). spec/plan: [N2 spec](../superpowers/specs/2026-09-09-net-n2-session-design.md) · [N2 plan](../superpowers/plans/2026-09-09-net-n2-session.md). **Networking N1 (neutral Transport + in-memory loopback) DONE** — the last Phase-4 subsystem is underway. A backend-free `Transport` seam (`engine/net/transport.hpp`: `send(ConnectionId, Channel, bytes)` / `poll() → vector<NetEvent>` / `disconnect`; `NetEvent` = Connected/Disconnected/Message; `Channel` = Reliable/Unreliable; opaque `std::string` payloads) + a header-only in-process `make_loopback_pair()` (`engine/net/loopback_transport.hpp`) that links a server+client sharing per-direction queues: `send` enqueues to the peer's inbound tagged with the sender id, `poll` drains this endpoint's inbound (Connected latched once, then Messages FIFO, then Disconnected on close); stable ids (server=1, client=2). Headless-tested (6 `[net]` cases: connect handshake, reliable both directions + no self-receive, unreliable tag preserved, FIFO, disconnect + drop-later, drop-to-unknown) — no sockets, fully deterministic. **The networking pillar targets server-authoritative replication** (a server owns the World; clients send input and receive serialized component-state snapshots). Networking decomposition: **N1 done** → **N2** session/connection lifecycle + a `NetworkSystem` (client vs server role, handshake, `mosaic run --host`/`--connect`) → **N3** server→client state replication (reflection → `Value` snapshots — `reflection.md` anticipates this) → **N4** entity spawn/despawn replication + a net-id ↔ `EntityID` map → **N5** client→server input/commands (via the `CommandStack`/event bus); a real **ENet UDP backend** (module-walled, maintainer-verified on a real network) is a later slice, mirroring audio A1→A2. spec/plan: [N1 spec](../superpowers/specs/2026-09-09-net-n1-transport-design.md) · [N1 plan](../superpowers/plans/2026-09-09-net-n1-transport.md). **UI pillar COMPLETE — U5b (world-space text labels) DONE** — floating 3D text: a reflected `WorldLabel` (`engine/ui/world_label.hpp`: font `AssetID` + text + world `size`/`color`/`alpha`/`offset`, registered `"WorldLabel"`, script-mutable) renders **camera-facing** at an entity's `WorldTransform` position — nameplates/labels. A pure `build_world_text` (`engine/ui/world_text.hpp`) reuses `layout_text` and places glyphs on a camera-facing billboard via the camera right/up axes (centered on the anchor), producing world-space `WorldGlyphVertex`es. `RenderSystem` runs a **world-label pass** (its own block deriving `cam_right`/`cam_up` from `inverse_affine(view)`, after the billboard section, **before** the HUD overlay) that resolves each font's atlas via `font_textures_` and submits through a new `draw_world_text` seam; a Vulkan pipeline modeled on the **ribbon** pipeline (CPU vertex stream + `view_proj` push) + the atlas descriptor draws them **depth-tested** (occluded by nearer geometry, write off). Demo: a floating "Orbiter" label tracking the orbiting cube. Headless-tested (`WorldLabel` reflection; `build_world_text` count/centering/empty; RenderSystem world-label pass incl. no-`WorldTransform`/empty skip); full suite 571 green; `mosaic run --window` exits 0; **the on-screen label is the maintainer's hardware check**. spec/plan: [U5b spec](../superpowers/specs/2026-09-09-ui-u5b-world-labels-design.md) · [U5b plan](../superpowers/plans/2026-09-09-ui-u5b-world-labels.md). **The UI pillar (U1 HUD panels → U2 text → U3 images → U4 click interaction → U5a layout/hierarchy → U5b world-space labels) is now COMPLETE.** **Remaining Phase-4 subsystem: networking.** **UI pillar — U5a (layout / hierarchy) DONE** — HUD elements **nest**: a child positions relative to its parent container's rect instead of the whole screen. `resolve_rect` generalized to `resolve_in(frame, anchor, pos, size)` (the screen is just the root frame `{0,0,1,1}`, and `resolve_rect` is the screen-frame case, bit-identical); a reflected `UiParent { EntityID }` (`engine/ui/ui_parent.hpp`, registered `"UiParent"`) names a child's container (an entity with a `UiPanel`); `engine/ui/ui_hierarchy.hpp` resolves each element's screen rect by walking the parent-`UiPanel` chain (`resolve_parent_frame`/`resolve_ui_rect`, depth-capped at 16 → cycle-safe, mirroring `TransformSystem`). **Every rect site routes through it** — the panel/image/text render passes and the `UiSystem` button hit-test — so a nested element (and `layout_text`, which gained a `frame` param so nested text scales with its parent) positions within its parent panel; nesting composes. Demo: a child `UiText` label parented to the camera's HUD bar (no `Transform`, so default-scene counts are unchanged). Headless-tested (`resolve_in`; frame resolution incl. no-parent/invalid/cyclic; `UiParent` reflection; a hierarchy render case; `layout_text` frame); full suite 565 green; `mosaic run --window` exits 0; **the on-screen nested layout is the maintainer's hardware check**. spec/plan: [U5a spec](../superpowers/specs/2026-09-08-ui-u5a-hierarchy-design.md) · [U5a plan](../superpowers/plans/2026-09-08-ui-u5a-hierarchy.md). **UI pillar — U4 (HUD interaction / click) DONE** — the HUD is now **clickable**: a neutral `PointerState` input seam (`engine/ui/pointer.hpp`: normalized x/y + `down`) + a reflected `UiButton` hit region (`engine/ui/ui_button.hpp`: anchor + normalized pos/size + an `event` name, registered `"UiButton"`) + the **first stateful `UiSystem`** (`engine/ui/ui_system.hpp`, an `ecs::System` with an injected `std::function<PointerState()>` provider) that detects a press-and-release **inside the same button** and emits a named `ScriptEvent{event, {entity, action:"click"}}` — delivered to `on_event(name, payload)` one tick later, exactly like the physics `'contact'` bridge. This is the **first runtime input path** (mouse never reached the World before). The engine stays windowing-agnostic: the pointer provider is filled from **GLFW only in `main.cpp`** (`glfwGetCursorPos`/`glfwGetMouseButton`, normalized by window size); headless/editor use a null provider (nothing clicks). `setup_runtime` gained a defaulted `pointer` param and adds `UiSystem`. Demo: a `UiButton` overlays the HUD bar and a `"hud_click"` log subscriber prints on click (no new entity, no script edits). Headless-tested (`UiButton` reflection + registration; `UiSystem` click semantics via a fake pointer provider — click, release-outside, press-outside, never-down); full suite 556 green; `mosaic run --window` exits 0; **the on-screen click is the maintainer's hardware check**. spec/plan: [U4 spec](../superpowers/specs/2026-09-08-ui-u4-interaction-design.md) · [U4 plan](../superpowers/plans/2026-09-08-ui-u4-interaction.md). **UI pillar — U3 (HUD images/sprites) DONE** — a reflected `UiImage` (`engine/ui/ui_image.hpp`: texture `AssetID` + anchor/normalized-pos/size + `tint`/`alpha`/`order`, registered `"UiImage"`, script-mutable) draws a textured, tinted, alpha-blended sprite stretched to an anchored screen rect — essentially U1's `UiPanel` plus a texture, reusing `resolve_rect`. `RenderSystem` runs an **image pass after the panel pass** (sprites over panels), resolving the texture through the existing `texture_cache_` and submitting `UiImageQuad`s via a new `draw_ui_image` seam; a Vulkan screen-space textured pipeline (`ui_image.vert/.frag`, push-constant rect+tint, samples the texture via `texture_set_layout_`, `out = texture.rgb*tint.rgb, texture.a*tint.a`, swapchain + viewport, depth off) draws them — modeled on the U1 `ui` + decal pipelines. Demo: a checker sprite top-right on the camera. Headless-tested (`UiImage` reflection + registration, RenderSystem image pass via `FakeRenderDevice` incl. skip-transparent + nil-texture); full suite 540 green; `mosaic run --window` exits 0 with no validation errors; **the on-screen sprite is the maintainer's hardware check**. spec/plan: [U3 spec](../superpowers/specs/2026-09-08-ui-u3-images-design.md) · [U3 plan](../superpowers/plans/2026-09-08-ui-u3-images.md). **UI pillar — U2b (HUD text rendering) DONE** (PR [#130](https://github.com/Evol-Luci/mosaic/pull/130)) — reflected `UiText` + a pure aspect-correct `layout_text` + a `draw_ui_text` seam + a RenderSystem text pass + a Vulkan textured glyph pipeline + a built-in demo font; completes **U2 (text)**. **UI pillar — U2a (FontAtlas cooked asset) DONE** — `mosaic import <proj> font.ttf` bakes a glyph atlas + per-codepoint metrics via **stb_truetype at cook time** (vendored single header, walled in `app/asset/font_cook.cpp` — no stb symbol above the importer, mirroring the WAV parser); a neutral `FontAtlas` (`engine/ui/font_atlas.hpp`: RGBA8 atlas with glyph coverage in the **alpha** channel + `Glyph` metrics + vertical metrics) is serialized as a `"font"` asset (`.font` `MOSAFNT1` blob, byte-exact round-trip) and registered by `load_project_assets` — mirrors the T5 image / A3 audio cook pipelines. Headless-tested (read_ttf metrics on a vendored Liberation/OFL fixture, cook/uncook round-trip + reject, import→load end-to-end); full suite green + real `mosaic import` CLI round-trip verified (`type = 'font'`). Nothing renders yet — **U2b** (UiText component + string layout + a glyph draw seam + a Vulkan text pipeline + demo text) is next. spec/plan: [U2a spec](../superpowers/specs/2026-09-08-ui-u2a-font-atlas-asset-design.md) · [U2a plan](../superpowers/plans/2026-09-08-ui-u2a-font-atlas-asset.md). **UI pillar — U1 (screen-space HUD foundation) DONE** (PR [#128](https://github.com/Evol-Luci/mosaic/pull/128)) — reflected `UiPanel` + a pure `resolve_rect` layout model + a `draw_ui` seam + a topmost RenderSystem HUD pass + a Vulkan screen-space quad pipeline + a demo HUD bar; headless-tested, GPU visual maintainer-verified. **UI decomposition COMPLETE:** U1 (HUD panels) → U2 (text) → U3 (images/sprites) → U4 (interaction/click) → U5a (layout/hierarchy) → **U5b (world-space text labels)** — all done. **The UI pillar is complete; the only remaining Phase-4 subsystem is networking.** UI follow-ups: hover/press events + visual feedback, keyboard/gamepad focus, text input fields, inherited alpha/visibility + clipping/scroll, flow/grid auto-layout, unifying panels/images/text under one `order`-sorted pass. **VFX pillar COMPLETE** — trails (billboard + ribbon), **decals** (textured world quads), and **screen effects** (fullscreen fade/tint/vignette overlay) all shipped, GPU visuals maintainer-verified; **VFX — V2 ribbon trails DONE** — trails render as a connected camera-facing ribbon via a new `draw_ribbons` seam + Vulkan triangle pipeline (visual is maintainer-verified); **VFX pillar started — V1 trails** — a `TrailRenderer`/`TrailSystem` fading tail; demo trail on the falling sphere; **Audio pillar COMPLETE (A1–A4)** — `AudioSystem` + a real miniaudio device wired into `setup_runtime`; `mosaic run --window` plays a 3D demo tone on the orbiter (audible check is the maintainer's); **Audio — A3** — cooked `.wav`→`AudioClip` asset (`mosaic import foo.wav`); 16-bit-PCM/float32 WAV parser, loads + plays through the null backend — done; **Audio pillar — A2** — a real miniaudio backend (`modules/audio_miniaudio`) implementing the neutral `AudioBackend`, proven headless on miniaudio's null device (no hardware in the sandbox) — done; **Audio pillar — A1** — neutral `AudioBackend` + reflected `AudioSource`/`AudioListener` + an `AudioSystem` driving voices from ECS state, headless via a fake backend (no device yet) — done; **particle cubic easing** — EaseCubicIn/Out/InOut added to the over-life easing set — done; **particle sub-frame spawn interpolation** — a moving emitter's continuous spawns spread along its swept path instead of clumping; orbiter trail demo — done; **script-driven particle emission** — a script bumps a scripted emitter's `burst_trigger` to fire bursts; demo puffer added — done; **particle triggered/re-armed bursts** — a `burst_trigger` edge counter re-fires a burst at runtime — done; **particle over-life easing** — per-property Linear/EaseIn/EaseOut/EaseInOut curves behind the over-life seam — done; **particle over-life curves** — color/size/alpha interpolate start→end over each particle's life — done; **particle rendering** — instanced billboards — done; **particles** Phase-4 subsystem done; v0.1 foundation + rendering + physics + Project & Tools Workflow
(T1–T5) complete; **Scripting (Python) pillar COMPLETE — S1 (Python embedding), S2 (reflection→Python
property bindings), S3 (world/commands), S4 (ScriptComponent), AND S5 (Studio script console) ALL COMPLETE.
The v0.1 vertical slice is COMPLETE.**
The final v0.1 pillar was decomposed into five slices
([scripting decomposition](../superpowers/specs/2026-08-30-scripting-decomposition.md)):
**S1** embed → **S2** reflection→Python property bindings → **S3** world/commands → **S4**
`ScriptComponent` (attach a script, press Play) → **S5** Studio script console.
**S1** embeds **real CPython** behind a neutral, Python-free `ScriptEngine` interface
(`engine/script/script_engine.hpp`); the CPython + **pybind11** implementation is walled off in
`modules/script_python` (injected via `create_python_script_engine()`, exactly like the Box3D /
Vulkan backends). `run_string` runs a script in an isolated per-engine namespace, captures stdout
+ errors into a `ScriptResult` (never throws). **This slice adds the pillar's two build deps** —
a Python dev install (`find_package(Python3 Development)`) and pybind11 (FetchContent v2.13.6,
`PYBIND11_FINDPYTHON ON` to reconcile a machine's multiple Pythons); both are contained behind the
module wall. 335 tests green (5 script tests: run+capture, compute, namespace isolation, syntax +
runtime error capture, output-on-error). Runtime feasibility was confirmed by a throwaway spike
before committing. **Build requirement note:** building now needs an *embeddable* Python (headers
+ a shared `libpython`, e.g. distro `python3-dev`). S1 design/plan:
[S1 spec](../superpowers/specs/2026-08-30-scripting-s1-embedding-design.md) ·
[S1 plan](../superpowers/plans/2026-08-30-scripting-s1-embedding.md).
**S2** wires reflected component properties into the Python side via a `Value` DOM bridge
(`value_to_py`/`py_to_value` in `modules/script_python/value_marshal.hpp`). The engine's #18
`ComponentReflection` serializes a component to a `Value` tree; the bridge marshals it into a
Python dict; the script mutates it; the bridge converts back; and `ComponentReflection` applies the
updated `Value` tree. Two `cpp_function` globals (`get_component`/`set_component`) are injected
into each engine namespace by `bind_world`, keyed on entity `uint64` ID and reflected component
name. Unknown component names return `None`/`False` (never throw). 344 tests green (4 new script
property tests: mutate-and-observe, read-current-values, error-return, set_global marshal;
2 new marshal tests: round-trip every DOM scalar/container, uint64→Python int). S2 design/plan:
[S2 spec](../superpowers/specs/2026-08-30-scripting-s2-properties-design.md) ·
[S2 plan](../superpowers/plans/2026-08-30-scripting-s2-properties.md).
**S3** exposes entity lifecycle and undoable commands to Python. `bind_world` now installs six
entity builtins: `create_entity()` (immediate, returns uint64 id), `destroy_entity(eid)` (deferred
to `world.tick()`, returns bool), `entity_count()`, `entities()` (list of ids), `is_valid(eid)`,
and replaces the S2 direct `set_component` with a stack-aware version. A new neutral
`ScriptEngine::bind_commands(CommandStack&)` (pure-virtual, `engine/script/script_engine.hpp`;
only adds `#include "foundation/commands/command_stack.hpp"` — still no pybind11 above the module
wall) installs `undo()`/`redo()`/`undo_depth()` and routes `set_component` through a
`SetComponentCommand` when a stack is bound: `execute()` + `valid()` check + `push_executed` —
a rejected set is not recorded. Without a bound stack, `set_component` falls through to the S2
direct path (S2 tests unaffected). 355 tests green (4 new script tests: entity lifecycle
deferred-destroy, undoable set + undo + redo, failed-set not recorded, no-stack backward compat).
**S4** delivers the core payoff — "attach a Python script, press Play." A reflected
`ScriptComponent { std::string source; }` (`engine/script/script_component.hpp`, Python-free,
registered as `"ScriptComponent"`) holds a Python script defining `on_update(dt)`; a
`ScriptSystem` (`engine/script/script_system.hpp`) runs each scripted entity's `on_update(dt)` once
per `World::tick`. `ScriptSystem` is an `ecs::System` that drives the neutral `ScriptEngine` via an
injected factory (`std::function<unique_ptr<ScriptEngine>()>`), so the engine tier stays
Python-free; it caches one engine per scripted entity (compile source once, recompile on change,
prune the unseen), sets the `entity` global once and `dt` each tick, and ignores per-tick errors
(non-fatal — a throwing script skips its mutation without crashing the tick). Per-tick mutations use
the S2 direct path (no undo spam). v0.1 inlines the source in the component; a `.py`-asset reference
is a post-v0.1 refinement. 361 tests green (6 new: ScriptComponent reflection round-trip; ScriptSystem
determinism/bit-identical, per-entity state persistence, recompile-on-source-change, throwing-on_update
non-fatal, syntax-error-in-source non-fatal). S4 design/plan:
[S4 spec](../superpowers/specs/2026-08-30-scripting-s4-scriptcomponent-design.md) ·
[S4 plan](../superpowers/plans/2026-08-30-scripting-s4-scriptcomponent.md).
**S5** (Studio script console) is now **COMPLETE**. A "Script Console" panel in `mosaic editor`
hosts a multiline input area + Run/Clear buttons wired to `ScriptConsoleModel` (Task 2). The model
drives the persistent CPython REPL against the live editor `World` and a `CommandStack`; history is
colour-coded (input/output/error). The CPython backend (`mosaic_script_python`) is injected at the
app tier (`app/run/editor.cpp`) — `studio/` remains Python-free. 367 tests green (no new tests —
this task is GUI glue; the console logic is covered by S5 Task 2's headless tests).
**Deferred:** "Play-from-script" (running a script against the `PlayState` / `RuntimeLoop` from the
console) needs a neutral host-callback design; not part of v0.1. S5 design/plan:
[S5 spec](../superpowers/specs/2026-08-30-scripting-s5-studio-console/spec.md) ·
[S5 plan](../superpowers/plans/2026-08-30-scripting-s5-studio-console.md).
**Runtime integration (post-S5):** `ScriptSystem` is now wired into `setup_runtime` (before
`PhysicsSystem`) — the scaffolding both `mosaic editor` and the windowed `mosaic run` (a built
bundle's entry point) call — so a `ScriptComponent` **ticks on Play in the editor AND in a built
standalone**, closing the v0.1 "attach a script → Play → Build a standalone that plays" loop. The
default scene ships an orbiting scripted green cube (`ScriptComponent`, no RigidBody) that serializes
and survives `save → mosaic build → mosaic run`. An editor "Demo: physics" toolbar checkbox swaps
that object between script-driven (orbits) and physics-driven (falls) via a pure `set_demo_physics()`
(a script and a dynamic RigidBody can't both drive one Transform, so it swaps rather than layers).
371 tests green (2 new: the `setup_runtime` wiring tick-test + the toggle swap). `demo_scene.hpp` /
`studio/` stay Python-free (the CPython factory is injected at the app tier). **Deferred (roadmap):**
a dedicated slider / color-picker / Euler-rotation **controls panel** (editor-UX beyond v0.1's
"inspector" scope — the Inspector already edits position/rotation/scale/color as drag-fields);
**generic add/remove-component editing in the Inspector** for the selected entity — structural
composition (attach/detach whole components on the current selection, e.g. a `RigidBody` or a
`ScriptComponent`), of which the demo-physics toolbar toggle is a hardcoded, single-object precursor.
(Deliberately distinct from the controls-panel item above: that one is nicer *widgets for existing
field values*; this one is *adding and removing components*, which the Inspector cannot do at all
today.) Scripting in the **headless** `mosaic run` (no `--window`, which doesn't call
`setup_runtime`).
Design/plan: [spec](../superpowers/specs/2026-09-02-scripting-runtime-integration-design.md) ·
[plan](../superpowers/plans/2026-09-02-scripting-runtime-integration.md).
S3 design/plan:
[S3 spec](../superpowers/specs/2026-08-30-scripting-s3-world-commands-design.md) ·
[S3 plan](../superpowers/plans/2026-08-30-scripting-s3-world-commands.md).
**Post-integration fix (current tip of `main`):** `import_asset` now **refuses to overwrite on a
stem collision** — returns `nullopt` + logs instead of silently clobbering an existing asset and
orphaning its `AssetID` (`app/asset/asset_io.hpp`; a T5 follow-up, PR #83, salvaged from an orphaned
worktree). **372 tests green; `main` is clean, zero open PRs.**
**Phase 3 (Programming) — first hot-reload slice: the Studio Script Editor.** A new **Script Editor**
panel in `mosaic editor` edits the *selected* entity's `ScriptComponent` source: **Apply**
test-compiles the edit in a throwaway `ScriptEngine` (bound to the live world, exactly as
`ScriptSystem` compiles) and shows green "compiled OK" / a red error; on success it writes the source
into the component, so `ScriptSystem` **recompiles it live** — edit a script, watch the behavior
change on Play without a restart. A pure `ScriptEditorModel` (`studio/script_editor.hpp`) holds the
validate+apply logic (unit-tested); the ImGui panel is thin glue; `studio/` stays Python-free (the
CPython factory is injected at the app tier). 376 tests green (4 new: valid-apply-writes,
syntax-error-rejected-and-unchanged, no-`ScriptComponent`, null-factory). **Deferred (documented):**
external `.py`-file editing (edit in your own editor, reload on disk change — the same recompile
seam) and attaching a script to an unscripted entity (rides on the deferred generic add/remove-
component in the Inspector). Design/plan:
[spec](../superpowers/specs/2026-09-02-studio-script-editor-design.md) ·
[plan](../superpowers/plans/2026-09-02-studio-script-editor.md).

## Moving forward — the v0.1 vertical slice is DONE

v0.1 is **complete and shippable**: `mosaic create` → import an asset → open the Studio → move
objects → add physics → attach/write a Python script → **Play** → **Build a standalone that plays on
its own**. Foundation, rendering (#36), physics (P1–P5), tools (T1–T5), scripting (S1–S5), and the
runtime integration are all merged and verified (372 tests, functional + visual acceptance passed).

**Priority order (set by the maintainer, 2026-09-03):** **Editor UX → Scripting thread → More Phase 3
→ Physics / engine (fully headless)** — work the buckets below in that order (the Editor-UX
add/remove-component item, now done, was first because it unblocks attach-script-to-any-entity).
Each item still gets its own brainstorm → spec → plan → TDD cycle.

**A. Next roadmap phase** (see [roadmap.md](roadmap.md)):
- **Phase 3 — Programming** (deepen the layer just built): script **hot-reload — done** (Script
  Editor, external `.py` reload, `.py`-as-asset); **broader scripting API — first slice done**
  (`add_component`/`remove_component`/`has_component`/`component_names`/`entities_with` — scripts can
  now furnish `create_entity()` results and query composition by name; design:
  [spec](../superpowers/specs/2026-09-04-broader-script-api-design.md)). **`raycast()` — done**
  (scripts call `raycast(origin, dir, max)` → `{hit, entity, distance, point, normal}`, wired through
  a neutral `ScriptHost.raycast` seam so the script backend stays physics-agnostic; the runtime injects
  a `PhysicsSystem`-backed callback via `make_physics_raycast`; design:
  [spec](../superpowers/specs/2026-09-04-script-raycast-design.md)). **Events — done** (scripts
  `emit(name, payload)` and receive via an `on_event(name, payload)` hook; a neutral `ScriptEvent`
  rides the deferred bus, so `ScriptSystem` delivers it one tick later, FIFO; inline mode; design:
  [spec](../superpowers/specs/2026-09-04-script-events-design.md)). **Physics contact callbacks — done**
  (`setup_runtime` bridges each `ContactEvent` to a `'contact'` script event with
  `{phase, a, b, point, normal, separation, normal_impulse}`, delivered to `on_event` — scripts react
  to collisions/landings). **Broader-API bucket complete.** **Profiler — done** (`ScriptSystem` times
  each entity's `on_update`, exposes `profile()`; `mosaic run --script-profile` dumps the timing table
  sorted by cost; design: [spec](../superpowers/specs/2026-09-04-script-profiler-design.md)).
  **Debugger — done** (tracepoint inspection: a neutral `ScriptDebugger` holds breakpoints; the Python
  backend installs a non-blocking `sys.settrace` hook that records hits + local-variable snapshots; the
  Script Editor's `debug()` runs source + `on_update` once and shows hits/locals — no pause, so no
  UI-thread deadlock; design: [spec](../superpowers/specs/2026-09-05-script-debugger-design.md)).
  **✅ Phase 3 — Programming is complete.** Deferred Phase-3 niceties (own slices later): true
  **pause/step** debugging on the worker thread (the foundation is in place), richer math/input/time
  script helpers, events + profiling in threaded mode, and per-script-source profile aggregation.
  *(Watch: the threaded `ScriptWorker` unit test flaked once under heavy parallel `ctest` load —
  passed on rerun; likely contention, revisit if it recurs.)*
- **Phase 4 — Production:** animation, audio, particles, VFX, UI, navigation, networking, sequencer.
  **Animation — first slice done** (property-path keyframes: a neutral `AnimationClip`/`Track`/`Keyframe`
  model + `sample()` (linear/step, clamped); an `AnimationPlayer` component (reflected, serializes with
  the scene); a generic `AnimationSystem` that writes any reflected scalar by property path
  (`"Transform.position.x"`) via `component_value`/`set_value`; wired into `setup_runtime` after scripts,
  before physics; design: [spec](../superpowers/specs/2026-09-05-animation-system-design.md)).
  Animation follow-ups: clip-as-asset (`AssetID` import), cubic/bezier easing, per-component track
  grouping (perf), skeletal/blend trees, animation events. **Sequencer — done** (a director timeline:
  a `Sequence` component owns clip-tracks bound to target entities + start offsets, and event markers;
  `SequenceSystem` advances a master clock, samples active clips onto their targets via the shared
  `apply_clip_at`, and fires markers as `emit(name)` script events → scripts react through `on_event`;
  design: [spec](../superpowers/specs/2026-09-05-sequencer-design.md)). Sequencer follow-ups: auto-stop/
  clamp a non-looping sequence, marker payloads, clip blending, a timeline editor panel. **Navigation —
  done** (A\* pathfinding: a neutral `NavGrid` (2D, blocked cells) + `find_path` (8-connected, octile
  heuristic, no corner-cutting); a `NavAgent` component (goal cell/speed/active); a `NavigationSystem`
  that steers an agent's Transform toward its goal along the path, with a per-agent cached step-target
  so A\* only recomputes at cell centers (avoids re-rounding onto a blocked cell mid-move); grid injected
  into the system; design: [spec](../superpowers/specs/2026-09-05-navigation-design.md)). Navigation
  follow-ups: script `find_path()` seam, baking a grid from scene colliders, navmesh / 3D, dynamic
  obstacles, path smoothing, and `setup_runtime` wiring (a game supplies the grid). **Particles —
  done** (a neutral `Particle`/`Rng` (seeded splitmix64)/`integrate` model; a reflected
  `ParticleEmitter` component (emitting/rate/burst/lifetime/initial_velocity/spread/acceleration/
  seed/max_particles); a generic `ParticleSystem` that fires a burst on activation and runs
  rate-based continuous emission via a carried accumulator, seeds per-emitter deterministic velocity
  spread, applies constant acceleration, ages out expired particles, caps pools to `max_particles`,
  maintains transient per-emitter particle pools keyed by entity and pruned on destroy, exposes a
  `particles_for(EntityID)` query seam, and spawns at the entity's `Transform` origin; wired into
  `setup_runtime`; headless & bit-identical deterministic; design:
  [spec](../superpowers/specs/2026-09-06-particles-design.md)). **Particle rendering — DONE** (instanced camera-facing billboards via the neutral `draw_particles` seam, alpha-fade over life, per-emitter color/size, a Vulkan alpha-blended pipeline for swapchain + offscreen viewport, and a demo fountain in the default scene; design/plan: [spec](../superpowers/specs/2026-09-06-particle-rendering-design.md) · [plan](../superpowers/plans/2026-09-06-particle-rendering.md)). **Color/size/alpha over-life curves — DONE**
  (`ParticleEmitter`'s `color`/`size` are now the *start* values; added `end_color`/`end_size` plus an authored `alpha`/`end_alpha` pair, interpolated linearly by `t = age/lifetime` through a pure `color_at`/`size_at`/`alpha_at` seam the renderer consumes — defaults reproduce the previous constant-color, constant-size, 1→0 alpha fade; the demo fountain fades orange→magenta and shrinks. Easing is the documented next step behind the same seam; design/plan:
  [spec](../superpowers/specs/2026-09-06-particle-overlife-curves-design.md) · [plan](../superpowers/plans/2026-09-06-particle-overlife-curves.md)). **Per-property over-life easing — DONE**
  (added `color_easing`/`size_easing`/`alpha_easing` int fields (`enum Easing`: Linear/EaseIn/EaseOut/EaseInOut) and a pure `apply_easing(mode, t)` remap applied to `t` inside `color_at`/`size_at`/`alpha_at` before the lerp — every curve maps 0→0 and 1→1 so over-life endpoints stay exact, unknown modes fall back to linear; defaults (0=Linear) reproduce the prior linear interpolation, renderer unchanged; the demo fountain now holds opacity then fades fast (`alpha_easing = EaseOut`); design/plan:
  [spec](../superpowers/specs/2026-09-06-particle-easing-design.md) · [plan](../superpowers/plans/2026-09-06-particle-easing.md)). **Triggered / re-armed bursts — DONE**
  (a reflected `burst_trigger` int on `ParticleEmitter`; `ParticleSystem` tracks the last value it acted on per emitter and fires one burst per observed change — inequality, not delta — via a shared `fire_burst`, without mutating the component; the on-sight one-shot burst is preserved and default `0` is unchanged; renderer untouched. A default-scene demo is **deferred to the script-driven-emission slice**, which supplies the trigger stimulus; design/plan:
  [spec](../superpowers/specs/2026-09-06-particle-triggered-bursts-design.md) · [plan](../superpowers/plans/2026-09-06-particle-triggered-bursts.md)). **Script-driven emission — DONE**
  (needed **no new engine/binding code** — scripts already mutate any reflected component via the generic `get_component`/`set_component`, and `ScriptSystem` runs before `ParticleSystem`, so a scripted entity does `c = get_component(entity,'ParticleEmitter'); c['burst_trigger'] += 1; set_component(entity,'ParticleEmitter',c)` to drive bursts (or toggle `emitting`/`rate`). Shipped an end-to-end integration test (`tests/unit/app/script_runtime_test.cpp`) and the deferred **bursts demo**: a scripted cyan "puffer" in the default scene that bursts every ~1.2s (`app/run/demo_scene.hpp::kBurstScript`). Bounded slice — no separate spec/plan). **Sub-frame spawn interpolation — DONE**
  (a moving emitter's **continuous** spawns are spread along the path it swept this tick (`prev → curr`, fractions `(i+0.5)/n`) instead of clumping at the current position; bursts stay instantaneous; no component field, and a stationary emitter (`prev == curr`) is bit-identical to before; the demo orbiter cube now trails green→yellow particles smoothly; design/plan:
  [spec](../superpowers/specs/2026-09-06-particle-subframe-spawn-design.md) · [plan](../superpowers/plans/2026-09-06-particle-subframe-spawn.md)). **More easing curves (cubic) — DONE**
  (added `EaseCubicIn`/`EaseCubicOut`/`EaseCubicInOut` (modes 4/5/6) to `apply_easing` — monotonic, stay within [0,1]; bounded slice, no demo change. Elastic/bounce were deliberately deferred: they *overshoot* [0,1], so they need a range-handling design (esp. for alpha) before adding). Particles follow-ups:
  elastic/bounce easing (needs overshoot handling); deeper: sub-frame *age* integration (back-date a spawn's age to its sub-frame time).
  **Audio — A1 DONE** (the Audio pillar's architecture, headless: a neutral `AudioBackend` interface (play/stop/gain/pitch/position/is_playing/listener) behind the usual module wall; reflected `AudioSource` (clip/gain/pitch/looping/playing/spatial) + an `AudioListener` marker, registered as built-ins; an `AudioSystem` (injected backend) that edge-triggers play/stop on `playing`, pushes gain/pitch changes and spatial position from the entity's Transform, sets the listener pose from the `AudioListener` entity's Transform, and stops voices on prune — writing nothing back to components, deterministic (no `is_playing` polling). Proven headless with a recording `FakeAudioBackend` (10 tests); design/plan:
  [spec](../superpowers/specs/2026-09-06-audio-a1-design.md) · [plan](../superpowers/plans/2026-09-06-audio-a1.md)).
  **Audio — A2 DONE** (a real backend: `modules/audio_miniaudio` implements the neutral `AudioBackend` on **miniaudio** v0.11.21 (single header via FetchContent, `ma_*` walled in the `.cpp`). Two neutral-interface refinements landed: `play()` now carries `bool spatial` (backend disables 3D for UI sounds) and a neutral `register_clip(id, f32 PCM, frames, channels, rate)` seam supplies decoded audio. `register_clip` stores PCM per `AssetID`; `play` builds a per-voice `ma_audio_buffer`+`ma_sound` (volume/pitch/looping/spatialization) and returns a `VoiceId`; stop/gain/pitch/position/is_playing/listener map to `ma_*`. `create_miniaudio_backend(null_device)` factory. Since the sandbox has no audio device, **proven headless on miniaudio's null backend** (8 tests incl. `AudioSystem` driving the real backend); a feasibility spike validated the null-device path first. Audible playback on hardware is for the maintainer to confirm. design/plan:
  [spec](../superpowers/specs/2026-09-07-audio-a2-miniaudio-design.md) · [plan](../superpowers/plans/2026-09-07-audio-a2-miniaudio.md)).
  **Audio — A3 DONE** (cooked audio-clip asset, mirroring the T5 image pipeline: a neutral `AudioClip` resource (interleaved f32 PCM) + `app/asset/audio_cook.hpp` — `read_wav` parses uncompressed **16-bit-PCM / 32-bit-float** WAV (incl. `WAVE_FORMAT_EXTENSIBLE`) to f32 with our own minimal RIFF parser (no miniaudio in the importer — decode at cook), `cook_audio`/`uncook_audio` a compact `MOSAAUD1` blob. `import_asset` gained a `.wav` branch (→ `.audioclip` + `.toml`, overwrite-safe) and `load_project_assets` an `"audio"` branch that registers an `AudioClip` resource — so `mosaic import <proj> foo.wav` produces a loadable clip. Verified end-to-end headless incl. a loaded `AudioClip` playing through the miniaudio **null** backend; design/plan:
  [spec](../superpowers/specs/2026-09-07-audio-a3-clip-asset-design.md) · [plan](../superpowers/plans/2026-09-07-audio-a3-clip-asset.md)).
  **Audio — A4 DONE — the Audio pillar (A1–A4) is COMPLETE.** `setup_runtime` gained an injected (defaulted-null) `AudioBackend` and adds `AudioSystem`; when a real backend is present it registers a procedural demo tone (`kToneClipId`, `make_demo_tone`). The default scene now has an `AudioListener` on the camera and a looping **3D** `AudioSource` on the orbiter (pans as it circles). A testable `register_audio_clips(backend, registry)` hands cooked project clips to the backend. `mosaic run --window` creates a real miniaudio device (`create_miniaudio_backend()`; headless runs + the editor + all tests stay silent via the null default). Verified headless: the demo tone starts a voice through the runtime on the **null** device, a null backend is safe, `register_audio_clips` works, default-scene counts unchanged (audio attached to existing entities), full suite 507 green, `mosaic run --window` exits 0. **Audible hardware playback is the maintainer's check.** design/plan:
  [spec](../superpowers/specs/2026-09-07-audio-a4-runtime-wiring-design.md) · [plan](../superpowers/plans/2026-09-07-audio-a4-runtime-wiring.md)). **Audio follow-ups (post-pillar) are tracked as GitHub issues** ([#117](https://github.com/Evol-Luci/mosaic/issues/117) editor audio · [#118](https://github.com/Evol-Luci/mosaic/issues/118) listener orientation from camera rotation · [#119](https://github.com/Evol-Luci/mosaic/issues/119) attenuation/rolloff + doppler · [#120](https://github.com/Evol-Luci/mosaic/issues/120) one-shot cleanup + voice cap/stealing · [#121](https://github.com/Evol-Luci/mosaic/issues/121) 24-bit/`.mp3`/`.ogg` import · [#122](https://github.com/Evol-Luci/mosaic/issues/122) verify+demo script control of `AudioSource`) — all non-blocking polish; prioritize against Phase 4 as desired.
  **VFX — V1 (trails) DONE** (the VFX pillar's first slice: a reflected `TrailRenderer` component + a `TrailSystem` that samples each moving entity's `Transform.position` into a bounded per-entity history (transient state, seed-head-on-first-sight, one sample/`sample_interval`, ring-buffer `max_points`, prune on destroy, `points_for` query seam). Rendered as a fading **billboard chain** by appending tapered `ParticleInstance`s (per point: `frac=(i+1)/n` → size/alpha taper) into the existing `RenderSystem` billboard pass — **reusing `draw_particles`, no new device method or Vulkan pipeline**. Wired into `setup_runtime`; demo trail on the falling sphere (pale-blue tail). Headless-tested (sampling/cap/prune/determinism/reflection/registration + render taper via `FakeRenderDevice`); design/plan: [spec](../superpowers/specs/2026-09-07-vfx-trails-design.md) · [plan](../superpowers/plans/2026-09-07-vfx-trails.md)).
  **VFX — V2 (ribbon trails) DONE** — trails now render as a **connected camera-facing ribbon** instead of a billboard chain: the engine CPU-expands each trail's points into tapered world-space triangles (`engine/vfx/ribbon.hpp::build_ribbon`, `side = cross(seg_dir, dir_to_camera)` with fallbacks, width+alpha taper), submitted through a new neutral `draw_ribbons` seam (`RibbonVertex`/`RibbonDrawParams` on `RenderDevice`) and drawn by an alpha-blended Vulkan **triangle pipeline** (`ribbon.vert`/`ribbon.frag`, per-vertex pos/color/alpha, swapchain + viewport variants, modeled on the particle pipeline). Headless-tested (geometry + a `FakeRenderDevice` `draw_ribbons` call: 6 verts/segment, no trail in the particle batch); build + `mosaic run --window` exits 0 with no Vulkan validation errors; **the ribbon visual is maintainer-verified on hardware** (GPU output isn't observable in the sandbox). design/plan: [spec](../superpowers/specs/2026-09-07-vfx-ribbon-trails-design.md) · [plan](../superpowers/plans/2026-09-07-vfx-ribbon-trails.md)).
  **VFX — V3 (decals) DONE** — a reflected `Decal` component (texture/color/alpha) renders a world-oriented, alpha-blended textured quad placed by its `WorldTransform`; `RenderSystem` resolves the texture via the existing `TextureCache` and composes the MVP, submitting a new `draw_decals` seam; a Vulkan textured-quad pipeline reuses `texture_set_layout_` (shader-generated quad, no vertex buffer). Demo: a striped decal on the ground. design/plan: [spec](../superpowers/specs/2026-09-07-vfx-decals-design.md) · [plan](../superpowers/plans/2026-09-07-vfx-decals.md).
  **VFX — V4 (screen effects) DONE** — a reflected `ScreenEffect` component (tint/tint_strength/vignette) drives a fullscreen alpha **overlay** (fade/flash/color-grade + radial vignette) via a new `draw_screen_effect` seam + a fullscreen-triangle Vulkan pipeline (no scene sampling; composites tint + vignette in one blend); scripts can animate `tint_strength` for fades. Demo: a subtle vignette on the camera. design/plan: [spec](../superpowers/specs/2026-09-07-vfx-screen-effects-design.md) · [plan](../superpowers/plans/2026-09-07-vfx-screen-effects.md).
  **✅ VFX pillar COMPLETE (trails, decals, screen effects).** All GPU visuals maintainer-verified on hardware (not observable in the sandbox); headless geometry/wiring + build + `run --window` exit-0 verified. **VFX follow-ups:** scene-sampling post (bloom/blur/DOF — needs render-to-texture), projected-onto-geometry decals, ribbon texture / width- & color-over-length curves + miter joins.
  **UI pillar STARTED — U1 (screen-space HUD foundation) DONE** (runtime game UI, distinct from the
  editor's ImGui dev-tools): reflected `UiPanel` (anchor + normalized pos/size + color/alpha/order) +
  a pure `resolve_rect` layout model + a `draw_ui` seam + a topmost RenderSystem HUD pass + a Vulkan
  screen-space quad pipeline; demo HUD bar on the camera; headless-tested, GPU visual maintainer-verified.
  UI decomposition: **U1 done** → **U2** text/glyphs → **U3** images/sprites → **U4** interaction
  (input hit-test → script events) → **U5** layout/hierarchy + world-space panels + aspect-correct sizing.
  design/plan: [spec](../superpowers/specs/2026-09-08-ui-u1-screen-hud-design.md) ·
  [plan](../superpowers/plans/2026-09-08-ui-u1-screen-hud.md). **Remaining Phase-4 subsystems: UI (U2–U5), networking.**

**B. Accumulated v0.1 follow-ups** (smaller, well-scoped):
- **Editor UX:** **generic add/remove-component in the Inspector — DONE** (a `+ Add Component` combo +
  a per-component Remove button, generic over reflection via `registered_names()`/`add_default()`;
  unblocks attaching a `ScriptComponent` to any entity; design/plan:
  [spec](../superpowers/specs/2026-09-03-inspector-add-remove-component-design.md) ·
  [plan](../superpowers/plans/2026-09-03-inspector-add-remove-component.md)). **Viewport mouse-picking
  — DONE** (click an object to select it, consumes `PhysicsSystem::raycast`). **Controls panel — DONE**
  (a dedicated panel with position/scale drags, Euler-angle rotation **sliders in degrees**, and a
  base-color **picker** for the selected entity's `Transform`/`Material`; design/plan:
  [spec](../superpowers/specs/2026-09-03-transform-controls-panel-design.md) ·
  [plan](../superpowers/plans/2026-09-03-transform-controls-panel.md)). Still open: in-editor
  **File → Reload**; undoable add/remove & controls edits via a `CommandStack`.
- **Scripting:** **external `.py`-file hot-reload — DONE** (a `ScriptComponent.path` loads its script
  from a file and `ScriptSystem` reloads/recompiles it live on an on-disk edit, via mtime-polling;
  design/plan: [spec](../superpowers/specs/2026-09-03-script-file-hotreload-design.md) ·
  [plan](../superpowers/plans/2026-09-03-script-file-hotreload.md)). **Headless `mosaic run` scripting
  — DONE** (`setup_runtime` is hoisted out of the window block, so a windowless `mosaic run` ticks
  `ScriptSystem` + `PhysicsSystem`; verified by a headless frame-pump integration test and a live
  `--ticks` run emitting physics contacts; design/plan:
  [spec](../superpowers/specs/2026-09-03-headless-run-scripting-design.md) ·
  [plan](../superpowers/plans/2026-09-03-headless-run-scripting.md)). **Play-from-script — DONE** (a
  neutral `ScriptHost` host-callback bound into every engine; scripts call `stop()`/`pause()`/`play()`,
  wired in `main.cpp` so a headless script can end the run — verified by backend, system, and
  frame-pump integration tests; design/plan:
  [spec](../superpowers/specs/2026-09-03-play-from-script-design.md) ·
  [plan](../superpowers/plans/2026-09-03-play-from-script.md)). **`.py`-as-asset — DONE**
  (`mosaic import <proj> foo.py` cooks a `type="script"` asset (`.script` blob + `.toml`);
  `load_project_assets` registers it as a `ScriptSource`; `ScriptComponent.script` references it by
  `AssetID` and `ScriptSystem` resolves it via the registry (precedence asset-id → path → inline);
  design/plan: [spec](../superpowers/specs/2026-09-03-py-as-asset-design.md) ·
  [plan](../superpowers/plans/2026-09-03-py-as-asset.md)). **Script worker thread — DONE** (opt-in
  `mosaic run --script-thread`: a dedicated Python-owning worker thread runs `on_update`s against an
  immutable snapshot and emits a write batch the main thread applies one tick later, so a heavy
  script never stalls the frame; the `ScriptContext`/`SnapshotContext` sandbox seam + `bind_context`
  keep it race-free, inline stays the default; design/plan:
  [spec](../superpowers/specs/2026-09-03-script-worker-thread-design.md) ·
  [plan](../superpowers/plans/2026-09-03-script-worker-thread.md)). **Honest caveats:** CPython's
  single GIL means no *parallelism* across scripts (the win is decoupling, not speed); uniform
  ~1-frame result latency in threaded mode; entity-lifecycle (`create_entity`/`destroy_entity`) is
  unsupported in threaded mode (would race `World`). **This completes the scripting bucket** — future
  scripting work: per-interpreter GILs (PEP 684) for real parallelism, dirty/partial snapshots, an OS
  **file-watcher** for live re-import (replacing the `.py` mtime-poll), and a thin **editor
  `PlayState`-from-script** wiring (map the host to `studio.play_state()` in `editor.cpp`).
- **Studio (post-v0.1):** **Blender live-link** preview; asset-browser follow-ups (type filtering,
  thumbnails, disk import, clear-to-nil).
- **Physics:** **contact manifolds — DONE** (Begin `ContactEvent`s now carry world `point` / `normal`
  / `separation` / `normal_impulse` from the Box3D manifold via `b3Contact_GetData`, tier-wall-clean;
  design/plan: [spec](../superpowers/specs/2026-09-03-physics-contact-manifolds-design.md) ·
  [plan](../superpowers/plans/2026-09-03-physics-contact-manifolds.md)). Still open:
  record/replay-to-disk; the **hit-event** channel (above-threshold point/normal/approach-speed) +
  multi-point / friction impulses.
- **Engine:** wire the **job pool into `World::tick`** for parallel systems (honoring each system's
  `ComponentAccess`); a dense component id space ([#25](https://github.com/Evol-Luci/mosaic/issues/25)).

When the next slice is chosen, brainstorm it and update this file to point at it. The sections below
are the historical build record (most-recent-first).

Prior pillar — **T5** added `mosaic import <project> <file>` and the full asset-copy step in `build_bundle`.
`import_asset` (in `app/asset/asset_io.hpp`) accepts a PPM image, cooks it into a compact binary
blob (`.image`), writes `AssetMetadata` as a `.toml` sidecar (uuid, type, version, source), and
places both under `<project>/assets/`. `build_bundle` now copies the project's `assets/` tree
into the bundle (step 4.5, after manifest write, before binary copy) via `fs::copy` recursive;
`load_project_assets` scans bundle `assets/*.toml` at startup and registers each cooked image
into the `AssetRegistry` under its metadata uuid, so the bundle runs with its own assets from its
own copied binary — no source tree needed. 329 tests green; functionally, `mosaic create P &&
mosaic import P t5.ppm && mosaic build P -o B && B/mosaic run B --window --ticks 3` exits 0 with
no validation errors, and `B/assets/` contains the cooked `t5.image` + `t5.toml`. T5 design/plan:
[T5 spec](../superpowers/specs/2026-08-29-tools-t5-asset-importer-design.md) ·
[T5 plan](../superpowers/plans/2026-08-29-tools-t5-asset-importer.md).
**The Project & Tools Workflow pillar (T1–T5) is COMPLETE.**
**Next pillar: Scripting (Python)** — expose the ECS/world API to Python scripts.

Prior slice — **T4** added `mosaic build <project> [-o <out>]`: it packages a project into a
**self-contained, project-format bundle** — a copy of the running `mosaic` binary + the default
scene + the manifest + an executable `run.sh` — in `app/project/bundle.hpp` (`build_bundle`,
runtime-binary path injected for headless tests; runtime resolved via `/proc/self/exe`). Because
the bundle is project-format, the copied binary runs it through **T3's existing** `mosaic run
<dir>` path — so T4 only added the *build* side; running is free. Overwrite-safe: a dir is treated
as a regenerable prior bundle only if it carries the build artifacts (`mosaic` + `run.sh`), a
plain project or arbitrary `-o <path>` is refused untouched, and a self-build (`-o <project>`) is
refused — so no user data is clobbered; write failures roll back the bundle. Copying real
per-project `assets/` files was deferred to T5 (done). 319 tests green at the time. T4 design/plan:
[T4 spec](../superpowers/specs/2026-08-29-tools-t4-build-design.md) ·
[T4 plan](../superpowers/plans/2026-08-29-tools-t4-build.md).

Prior slice — **T3** introduced a **project** = a TOML `mosaic.toml` manifest (reflected `ProjectManifest{name,
default_scene}`) + a `scenes/` folder (a starter scene) + an `assets/` folder. `mosaic create
<name>` scaffolds one (starter scene = the T2 default demo serialized to `scenes/main.json`;
refuses to overwrite; rolls back a partial scaffold on failure). A "smart positional" arg on
`run`/`editor` — `resolve_startup_scene(path)` — maps a **project dir → its default scene** and a
**`.json` → itself**, then feeds T2's `populate_world` (so opening a project reuses all of T2's
load/render path; `editor.cpp` was untouched — resolution is centralized in `main()`). New
`app/project/` module (`project.hpp` + `project_io.hpp`), a `create` subcommand, and `Command`/
`Options` additions. Reading per-project `assets/` is deferred to **T5** (importer). 313 tests
green; `mosaic create X && mosaic editor X` / `mosaic run X --window` open + render the scaffolded
project. **Next: T4** (`mosaic build` — package a project's scene(s) + referenced assets into a
runnable standalone bundle). T3 design/plan:
[T3 spec](../superpowers/specs/2026-08-29-tools-t3-project-model-design.md) ·
[T3 plan](../superpowers/plans/2026-08-29-tools-t3-project-model.md).

Prior slice — **T2** split the hardcoded `build_demo_scene` into `setup_runtime` (non-serialized scaffolding:
built-in meshes + demo textures + `PhysicsSystem` + `RenderSystem`) and `build_default_scene`
(pure entity data), added a `scene_io` filesystem seam and a shared `populate_world` (load
`--scene` or the default demo, then derive `WorldTransform` so a loaded scene renders), wired
both `mosaic run` and `mosaic editor` to that path (and gave `editor` a `--scene` flag, fixing
the old `run --window --scene` double-populate), and added a headless `SceneFileModel` driving
**editor File → Save** (writes the current scene JSON; menu shows the path + a `*` when dirty).
The Studio now edits **real scene files**: open one via `--scene`, edit, Save, relaunch loads it.
**In-editor File → Reload is a deferred follow-up** (it needs a fresh-`World` rebuild + a
`FramePump` world-provider indirection — `PhysicsSystem` has no teardown and the pump ticks a
fixed `World&`). 302 tests green; `mosaic run --window` / `mosaic editor` exit 0, 0 validation
errors. **Next: T3** (`mosaic create` + project model — a TOML manifest + `scenes/`/`assets/`
folders; the project's default scene opens through T2's load path). T2 design/plan:
[T2 spec](../superpowers/specs/2026-08-29-tools-t2-scene-load-save-design.md) ·
[T2 plan](../superpowers/plans/2026-08-29-tools-t2-scene-load-save.md).

Prior slice — **T1** makes `MeshRenderer` reference a mesh **`AssetID`** (reflected,
serialized) resolved to a GPU mesh through a `MeshCache` — mirroring the 36d-2 texture pattern
(`Image`+`TextureCache`+`Material.texture`). A new `Mesh` resource + `MeshCache` (upload-once,
`AssetID → MeshHandle`), the built-in cube registered as a mesh asset under `kCubeMeshId`
(`register_builtin_meshes`), and `RenderSystem` resolving each entity's mesh via the cache mean
**scenes now round-trip renderable entities** — the enabler T2–T5 need. Headless tests cover the
cache (upload-once/nil/unregistered), the `MeshRenderer` reflection + scene round-trip, and the
updated render-system resolution; `mosaic run --window` still renders (0 validation errors, 287
tests green). **Next: T2** (scene load/save wired into the Studio — `mosaic editor --scene`, File
→ Open/Save). Design/plan:
[tools decomposition](../superpowers/specs/2026-08-29-tools-workflow-decomposition.md) ·
[T1 spec](../superpowers/specs/2026-08-29-tools-t1-meshes-as-assets-design.md) ·
[T1 plan](../superpowers/plans/2026-08-29-tools-t1-meshes-as-assets.md).
Prior state: v0.1 foundation + rendering complete; **Studio #37 v0.1 lean
core COMPLETE** — 37a–37e all built. `mosaic editor` opens an ImGui docking window with a
scene **hierarchy** (click to select), a **reflection-driven inspector** that edits a
selected entity's components live (the #18 payoff), a **viewport** panel showing the live 3D
scene (spinning, textured, lit cube) via a panel-sized offscreen target sampled by ImGui, and
a **toolbar play/stop/step + Console panel** — play/stop drives the `RuntimeLoop` (pause =
`stop()`, resume = `start()`; a Step ticks once while paused), and the Console surfaces #33
logging live via a capturing `Sink` + thread-safe ring buffer. Rendering 36a–36e-1 remains
complete (`mosaic run --window` draws the same cube directly). **RenderGraph** stays deferred
(37d chose direct two-pass submission; re-filed to the first dynamic/≥3-pass slice). The
**logging-sink gap** surfaced by 37e is now **closed** — `app/main.cpp` installs a stderr
`ConsoleSink` for every command, so `mosaic run`/headless runs emit their `log::` output and
Vulkan validation messages surface (the JSON reporter still writes structured output to stdout,
so stdout stays pure JSON). **Validation-layer fix:** with `VK_LAYER_KHRONOS_validation` now
installed, a pre-existing swapchain-semaphore-reuse bug (latent since the 36a swapchain work)
surfaced and is fixed — `render_finished_` is now per swapchain image, indexed by
`image_index_`, so a present-wait semaphore is never reused while its present is still pending
(`editor --ticks 180` now emits 0 `pSignalSemaphores` warnings). **Post-v0.1 Studio asset
browser is built:** an **Assets** panel lists registered assets (via new `AssetRegistry`
enumeration) and assigns a selected asset to a component's `AssetID` field live — e.g. swap
the cube's `Material.texture` and watch the viewport update — through the reflected-property
surface (a new mutable `FieldVisitor::field_asset`, so humans and the future agent API share
one path), with a headless `AssetBrowserModel` and three procedural demo textures.
**Two visual bugs from acceptance testing were fixed** (PR #61): inside-out culling (a
negative-height viewport reverses winding, so `frontFace` is now `CLOCKWISE`; pre-existing
since 36b) and paused inspector edits not refreshing the viewport (the editor now runs
`TransformSystem` every frame as derived state, gating only the spin behind play/stop).
Design/plan for the asset browser:
[spec](../superpowers/specs/2026-08-23-studio-asset-browser-design.md) ·
[plan](../superpowers/plans/2026-08-23-studio-asset-browser.md).
**Physics (Phase 2) is now underway — P1 is built.** The physics pillar was decomposed
into five slices (P1–P5) behind a neutral `PhysicsBackend` wall, with Box3D
([erincatto/box3d](https://github.com/erincatto/box3d), MIT, C17) as the backend. **P1**:
`engine/physics/physics_backend.hpp` (neutral `PhysicsBackend`/`BodyDef`/`BoxShape`/`Pose`/
`WorldDef`) + `modules/physics_box3d` (Box3D via FetchContent, pinned to `30c67b5`,
implementing the interface; `b3*` never leaves the module), proven by a **headless
deterministic** falling-box unit test — a box drops and rests on a ground plane at y≈0.5,
bit-identically across runs. **P2 is now built too** — reflected `RigidBody`/`Collider`
components + a `PhysicsSystem` that owns an **injected** backend, lazily creates bodies from
`RigidBody`+`Collider`+`Transform`, steps once per `World::tick`, and writes dynamic poses
back to `Transform` (position + real rotation — completing P1's rotation read-back). The
editor/`mosaic run --window` demo is now a **textured box dropping onto a ground slab** —
**Play finally simulates**, and Pause/Step single-step the fall. Tested headlessly: a
fake-backend sync test (one body per physics entity, dynamic write-back, static untouched) +
a real-backend ECS integration test (box falls + rests at y≈1.0, deterministic).
**Editor-interaction fix (found in acceptance testing):** the `PhysicsSystem` now syncs
**both ways** — a new `PhysicsBackend::set_body_transform` (teleport + zero velocity) plus a
per-tick divergence check push external `Transform` edits (inspector drags) into the
simulation, so a physics object can be moved while playing and edits survive Pause→Play
(previously the one-way body→Transform write-back clobbered edits / jumped the object back).
**P3 is now built too** — a tagged neutral `Shape` (box/sphere/capsule) mapped to Box3D
shapes, reflected `SphereCollider`/`CapsuleCollider` components (P2's `Collider` stays box),
and a `PhysicsSystem` that selects the shape per entity. The demo drops a **sphere, capsule,
and a box stack** onto the ground; headless tests cover sphere-rest, box-stack stability, and
capsule-settle, all deterministic. **"Plane" = flat box** (Box3D has no plane primitive).
**P4 is now built too** — a neutral `RaycastResult` + `PhysicsBackend::raycast` (closest-hit
via Box3D's `b3World_CastRayClosest`, body identified through userData) and an ECS-level
`PhysicsSystem::raycast` returning the hit `EntityID` (via a `BodyHandle → EntityID` reverse
map). Headless deterministic hit/miss/nearest-of-two tests at both the backend and ECS levels
(0 validation errors). No visuals — viewport mouse-picking is a deferred editor feature.
**P5 is now built — the physics pillar (P1–P5) is COMPLETE.** Box3D touch events map to a
neutral `BodyContact`, surfaced as a public `ContactEvent { phase; EntityID a; EntityID b; }`
emitted on the #11 `EventBus` by `PhysicsSystem` each tick; a contact-sequence determinism test
records the event stream over a scenario and asserts it identical across runs (the record/replay
guarantee applied to events); and the demo **logs contacts** so `mosaic run`/the editor Console
show `contact begin: entity … <-> entity …` as the bodies land (282 tests, 0 validation errors).
**What remains for the v0.1 vertical slice:** **Scripting (Python)**. The **Project & Tools
Workflow pillar (T1–T5) is now COMPLETE** — T1 (meshes as assets), T2 (scene load/save), T3
(`mosaic create` + project model), T4 (`mosaic build` standalone bundle), T5 (asset
importer/cooker + `build_bundle` copies assets) are all done. **Physics follow-ups:**
sphere/capsule demo meshes (done), **contact manifolds — DONE** (point/normal/separation/impulse on
Begin `ContactEvent`s); **viewport mouse-picking — DONE** (click an object in the viewport to select
it, via `PhysicsSystem::raycast` + a camera unproject; spec/plan:
[spec](../superpowers/specs/2026-09-03-viewport-mouse-picking-design.md) ·
[plan](../superpowers/plans/2026-09-03-viewport-mouse-picking.md)). Still-deferred: full
record/replay-to-disk, hit-event contacts + multi-point/friction impulses, collider-less picking + gizmos.
Physics design/plan:
[decomposition](../superpowers/specs/2026-08-24-physics-decomposition.md) ·
[P1 spec](../superpowers/specs/2026-08-24-physics-p1-backend-falling-box-design.md) · [P1 plan](../superpowers/plans/2026-08-24-physics-p1-backend-falling-box.md) ·
[P2 spec](../superpowers/specs/2026-08-24-physics-p2-system-design.md) · [P2 plan](../superpowers/plans/2026-08-24-physics-p2-system.md) ·
[P3 spec](../superpowers/specs/2026-08-24-physics-p3-shapes-design.md) · [P3 plan](../superpowers/plans/2026-08-24-physics-p3-shapes.md) ·
[P4 spec](../superpowers/specs/2026-08-24-physics-p4-raycast-design.md) · [P4 plan](../superpowers/plans/2026-08-24-physics-p4-raycast.md) ·
[P5 spec](../superpowers/specs/2026-08-24-physics-p5-contacts-design.md) · [P5 plan](../superpowers/plans/2026-08-24-physics-p5-contacts.md).
Also still open: post-v0.1 Studio work (Blender live-link; asset-browser follow-ups —
type-aware filtering, thumbnails, disk import, clear-to-nil))

## Where we are

- Docs, architecture, and the licensing/CLA groundwork are in place.
- **[RFC 0001: Core Data Model](../rfcs/0001-core-data-model.md) is Accepted.** The
  four questions that gated the foundation are settled (see the RFC's *Resolved
  decisions*), so the data-model portion of **Phase 0 — Foundation** is unblocked
  and ready to implement.

**ECS core ([#9](https://github.com/Evol-Luci/mosaic/issues/9)) is built and
merged.** The C++ toolchain is stood up (CMake + CMakePresets, C++20, Catch2 via
FetchContent), and the first foundation code is in place:

- `foundation/core/entity_id.hpp` — `EntityID`, the **32 index / 32 generation**
  handle; `EntityID(0)` null; slot **retired** (not recycled) on generation exhaustion.
- `engine/ecs/` — `EntityRegistry` (slot table, free-list recycle, stale-handle
  detection, retirement), sparse-set component storage (`ComponentRegistry` +
  a minimal `ComponentTypeID` shim), and the declaration-only `System` contract.
- `tests/unit/ecs/` — unit tests, all green.

**Serialization ([#10](https://github.com/Evol-Luci/mosaic/issues/10)) is built and
merged.** Reflection-driven, lossless cross-format projection; normative stance in
[RFC 0001 *Serialization stance*](../rfcs/0001-core-data-model.md#serialization-stance):

- `foundation/serialization/` — `Value` DOM, `AssetID`/`Handle<T>`/`ReferenceResolver`,
  reflection bridge (`to_value`/`from_value`), JSON backend (nlohmann), TOML backend
  (toml++), and error handling via `SerializationError`. `from_json`/`from_toml`
  guard against adversarial deeply-nested input with a configurable
  nesting-depth cap (default 128) that throws `SerializationError` rather than
  overflowing the stack — the non-blocking hardening follow-up from the #10 review. ✓
- `tests/unit/serialization/` — unit tests for every tag: `[value]`, `[reference]`,
  `[reflect]`, `[refenc]`, `[json]`, `[toml]`, `[crossfmt]` — 66 tests, all green.

**Command / event system ([#11](https://github.com/Evol-Luci/mosaic/issues/11)) is
built and merged.** The last Phase 0 foundation primitive; normative semantics live
in [design/command-event-system.md](../design/command-event-system.md), and
`required_permissions` ties into
[design/automation-and-agents.md](../design/automation-and-agents.md):

- `foundation/commands/` — `Command` (the `id, parameters, required_permissions,
  execute(), undo(), serialize()` contract, with a default `serialize()` that
  projects `{ id, parameters, required_permissions }` through the serialization
  `Value` DOM) and `CommandStack`, the undoable transaction log (execute pushes +
  clears redo; undo inverts LIFO; redo re-executes).
- `foundation/events/` — `EventBus`: synchronous subscribers fire in-tick before
  `emit` returns; deferred subscribers are served from a **double-buffered queue**
  drained once per tick, FIFO in global emit order across all event types, never
  mid-tick. A re-emit from inside a drain defers to the next tick.
- `tests/unit/events/` + `tests/unit/commands/` — `[events]`, `[commands]` unit
  tests plus a command/event integration test (`[commands][events]`): a deferred
  event emitted by a command announces on the next drain, and **undo announces
  nothing** — the "enqueue on execute, not undo" acceptance. 80 tests, all green.
  - *Interpretation note for the World layer:* the RFC says a deferred event
    enqueues "on execute, not when undone/redone." `CommandStack::redo()` re-runs
    the command's `execute()`, so a redo **does** re-announce — events live in the
    command body, not the log, and the UI must observe a redo. This resolves the
    RFC's ambiguity in the natural direction (redo == re-execute); flag it if the
    World/UI layer needs redo to be silent.

The three Phase 0 foundation implementation issues (#9, #10, #11) are now complete.

**The runtime `World` (Engine layer) is built and merged.** The first Engine unit
after the foundation primitives, designed and planned in
[specs/2026-08-16-engine-world-runtime-design.md](../superpowers/specs/2026-08-16-engine-world-runtime-design.md)
and [plans/2026-08-16-engine-world-runtime.md](../superpowers/plans/2026-08-16-engine-world-runtime.md):

- `engine/world/` — `World`: owns the `EntityRegistry`, `ComponentRegistry`, and a
  per-world `EventBus`; delegates entity/component ops; runs registered systems.
  `tick(dt)` does exactly three things in order: **drain deferred events** (the
  #11 fixed sync point), **run systems** in registration order, **commit deferred
  destructions** (`on_destroy → destroy`). `destroy_entity` is deferred so systems
  never have entities vanish mid-`for_each`.
- `engine/ecs/` additions — `SparseSet::each` / `ComponentRegistry::for_each<T>`
  single-component iteration, and `System::update(World&, dt)` (default no-op).
- `tests/unit/world/` + `tests/unit/ecs/` — `[world]` cases plus the interleaved
  create/add/destroy/recycle integration test the #9 handoff asked for. Full suite
  95 tests, all green.

Deliberately **out of scope** (their own future specs): Scene load/save, concrete
systems (e.g. transform), and the outer runtime loop. `World` exposes `tick(dt)`;
nothing calls it in a loop yet.

**Reflection system ([#18](https://github.com/Evol-Luci/mosaic/issues/18)) is built
and merged.** The gap `World` deliberately left open — reflection-driven *generic*
component construction — is now closed. Designed and planned in
[specs/2026-08-16-reflection-system-design.md](../superpowers/specs/2026-08-16-reflection-system-design.md)
and [plans/2026-08-16-reflection-system.md](../superpowers/plans/2026-08-16-reflection-system.md);
normative intent in [design/reflection.md](../design/reflection.md):

- `foundation/reflection/` — `type_id.hpp` (`TypeID` + `type_id<T>()`, the stable
  within-run type handle) and `type_registry.hpp` (`TypeRegistry`: register a
  reflected type by stable name, look it up by name **or** `TypeID`, enumerate its
  fields via the existing `reflect()` visitor). Pure foundation — no `World`.
- `engine/reflection/component_reflection.hpp` — `ComponentReflection`: the
  engine-layer bridge. `register_component<T>(registry, "Name")` records metadata
  **and** an attach thunk; `construct(world, e, name, value)` deserializes a
  `Value` into a fresh `T` (via the #10 `from_value`) and attaches it to an entity
  — **no concrete C++ type at the call site**. This is the path Scene load/save
  will call.
- The #9 `ComponentTypeID` shim is now a **re-export** of
  `foundation::reflection::TypeID`: one identity source, no drift. `ComponentRegistry`
  is unchanged and now shares its per-type integer with `TypeInfo::id`.
- `tests/unit/reflection/` — `[reflection]` cases: register/lookup by name & id,
  field enumeration, generic construct-and-attach into a `World`, the
  reflect → `to_value` → construct round-trip (nested + vector fields), and
  unknown-name / invalid-entity rejection. Full suite 107 tests, all green.
  - *Durability rule for future workers:* the **persisted** identity of a type is
    its **name** (a string) — Scene files, replication, any on-disk form key on it.
    `TypeID` is a runtime-only handle and may vary in assignment order across runs;
    never persist it.

**Scene load/save ([#19](https://github.com/Evol-Luci/mosaic/issues/19)) is built
and merged.** The first persistence path — a `World`'s entity+component graph to
JSON and back. Designed and planned in
[specs/2026-08-16-scene-load-save-design.md](../superpowers/specs/2026-08-16-scene-load-save-design.md)
and [plans/2026-08-16-scene-load-save.md](../superpowers/plans/2026-08-16-scene-load-save.md):

- `engine/scene/scene.hpp` — `to_value`/`from_value` (World ↔ scene `Value`) and
  `save_to_json`/`load_from_json` (via the #10 backend). Header-only, in-memory
  only; filesystem I/O is a caller / asset-registry concern.
- `engine/ecs/` + `engine/world/` — `EntityRegistry::for_each` /
  `World::for_each_entity` live-entity enumeration (also feeds the future editor
  hierarchy), so component-less entities round-trip faithfully.
- `engine/reflection/` — `ComponentReflection` gained the **save** direction
  (`save`/`has`) symmetric to `construct`; per-entity component order is
  deterministic (sorted type name).
- **Load is two-pass:** create every entity + build a serialized-id → new-handle
  remap, then generically construct components through a `SceneReferenceResolver`
  so intra-scene `EntityID` references remap to the loaded handles. An unknown
  component type name throws `SerializationError`; a reference outside the scene
  resolves to null. `AssetID`/`Handle` ids round-trip now; *resolving* them to
  live resources is the asset registry (#20).
- `tests/unit/scene/` — `[scene]` cases: populated round-trip (empties preserved),
  non-trivial reference remap (bumped generation), broken-ref → null, unknown-type
  throw, malformed-JSON throw, JSON convenience round-trip. Full suite 120 tests,
  all green.

**Asset registry ([#20](https://github.com/Evol-Luci/mosaic/issues/20)) is built
and merged.** The runtime-side registry — resolve/cache `AssetID` → loaded
resources, validate references on deserialize. Designed and planned in
[specs/2026-08-16-asset-registry-design.md](../superpowers/specs/2026-08-16-asset-registry-design.md)
and [plans/2026-08-16-asset-registry.md](../superpowers/plans/2026-08-16-asset-registry.md):

- `engine/assets/asset_metadata.hpp` — `AssetMetadata` (uuid, type, version, source,
  dependencies, open-ended metadata/import_settings), reflected, round-tripping
  through TOML via #10.
- `engine/assets/asset_registry.hpp` — `AssetRegistry`: `register_asset(meta, loader)`
  (lazy) / `register_resource<T>` (eager), `load<T>(id)` resolves + caches
  (loader invoked once), `metadata`/`contains`. Resources are type-erased
  (`shared_ptr<void>` + `load<T>` cast) — no `Resource` base class, since no concrete
  resource types exist until rendering. Missing id → null/nil, non-throwing.
- `AssetReferenceResolver` (same header) — the #10 `ReferenceResolver` hook: on
  deserialize a registered `AssetID`/`Handle` passes through, an unknown one becomes
  a nil handle.
- `foundation/serialization/reference.hpp` gained `std::hash<AssetID>` (mirrors
  `EntityID`), the natural cross-reference key.
- `tests/unit/assets/` — `[assets]` cases: hash-as-map-key, metadata TOML round-trip,
  register/cached-load (loader-once), eager resource, unknown → null, Handle
  validate/nil on deserialize. Full suite 127 tests, all green.
  - **Deferred (small follow-up):** wiring `AssetReferenceResolver` *into* Scene's
    load path (a combined entity-remap + asset-validation resolver), and any real
    loader/resource type (rendering), importer, and cooker.

**Foundation math + transform system ([#21](https://github.com/Evol-Luci/mosaic/issues/21),
[#26](https://github.com/Evol-Luci/mosaic/issues/26)) is built and merged.** The
first concrete `System`, and the math layer the rest of the slice inherits. Designed
and planned in
[specs/2026-08-16-transform-system-design.md](../superpowers/specs/2026-08-16-transform-system-design.md)
and [plans/2026-08-16-transform-system.md](../superpowers/plans/2026-08-16-transform-system.md):

- `foundation/math/{vec3,quat,mat4}.hpp` — minimal, hand-rolled (no GLM), header-only.
  **Conventions now fixed for rendering/physics:** column-major storage, column
  vectors, right-handed, Y-up, radians; `world = parent * local`, `local = T*R*S`.
  `Vec3`/`Quat` are reflected so transforms serialize.
- `engine/transform/transform.hpp` — reflected `Transform` (authored local
  position/rotation/scale + optional `parent` EntityID, nil = root) and the derived
  `WorldTransform{ Mat4 }` (system-written, not serialized).
- `engine/transform/transform_system.hpp` — `TransformSystem`, the first concrete
  `System`. `update` walks each entity's parent chain (`world(e) = world(parent) *
  local(e)`) and writes its `WorldTransform`; a depth guard bounds malformed cycles.
- `tests/unit/math/` + `tests/unit/transform/` — `[math]` (Vec3/Quat/Mat4 ops, TRS,
  transform_point) and `[transform]` (single→world, parent compose, rotated parent,
  moving a parent updates children next tick, reflection round-trip). Full suite 142
  tests, all green.
  - *Perf follow-up:* `update` recomputes each entity by re-walking ancestors —
    memoize per tick or process parent-first when scenes grow.

**Runtime loop ([#22](https://github.com/Evol-Luci/mosaic/issues/22)) is built and
merged.** The outer driver that finally *runs* the engine. Designed and planned in
[specs/2026-08-16-runtime-loop-design.md](../superpowers/specs/2026-08-16-runtime-loop-design.md)
and [plans/2026-08-16-runtime-loop.md](../superpowers/plans/2026-08-16-runtime-loop.md):

- `engine/runtime/runtime_loop.hpp` — `RuntimeLoop`: a deterministic fixed-timestep
  accumulator. `advance(world, real_dt)` banks externally-supplied elapsed time and
  dispatches whole `World::tick(fixed_dt)` calls; `step` is manual single-step;
  `start`/`stop` pause/resume; a `max_steps_per_advance` cap guards the spiral of
  death. Owns pacing **only** — the per-tick order stays in `World::tick`.
- The real `std::chrono` measure-and-sleep frame pump is deliberately **not** here —
  it belongs to the app/CLI (`mosaic run`) that owns the process main loop; the loop
  consumes an injected `real_dt` so it stays deterministic and testable.
- `tests/unit/runtime/` — `[runtime]` cases: fixed dispatch + remainder carry,
  start/stop/single-step, determinism (same schedule → equal tick count),
  spiral-of-death cap, and a populated-`World` + `TransformSystem` run stable over
  1000 ticks. Full suite 149 tests, all green.

**The headless engine core of the v0.1 vertical slice is now complete** (#9 ECS,
#10 serialization, #11 commands/events, the runtime `World`, #18 reflection, #19
Scene, #20 assets, #21 transform + #26 math, #22 runtime loop).

**Logging ([#33](https://github.com/Evol-Luci/mosaic/issues/33)) is built** (in
[PR #39](https://github.com/Evol-Luci/mosaic/pull/39)). The nearest of the remaining
foundation pieces — a leveled, categorized, thread-safe logging facility every later
subsystem (app/CLI, rendering, assets, jobs) consumes. Designed and planned in
[specs/2026-08-16-logging-design.md](../superpowers/specs/2026-08-16-logging-design.md)
and [plans/2026-08-16-logging.md](../superpowers/plans/2026-08-16-logging.md):

- `foundation/logging/log.hpp` — `Level` (trace/debug/info/warn/error), `Record`, a
  `noexcept` `Sink` + `ConsoleSink`, and a `Logger` (sink list + `std::atomic`
  min-level + a `std::mutex` serializing dispatch; below-threshold messages drop
  without taking the lock). A process-global `std::format`-style free-function
  surface (`log::info(cat, fmt, ...)`) forwards to a function-local-static default
  `Logger`, so promoting `Logger` to a public *injectable* API later is additive.
- Header-only and stdlib-only — honors the foundation tier contract.
- `tests/unit/logging/` — `[logging]` cases: level routing, min-level filter, the
  default `Info` level, multiple sinks, `std::format` substitution, exact
  `ConsoleSink` output, and a concurrent 8×500 no-lost/no-torn test whose sink is
  **deliberately unguarded** so the `Logger` mutex is the real subject under test
  (proven a genuine detector: removing the lock aborts the test). Full suite 160
  tests, all green.
  - *Deferred (post-v0.1):* lazy-eval logging macros with `source_location` capture,
    and file/rotating/JSON sinks + async queue (all named out of scope in #33).

**App / CLI shell ([#35](https://github.com/Evol-Luci/mosaic/issues/35)) is built** —
the engine now *runs a world outside a test*. `mosaic run` owns a `main` and the
real-time frame pump the runtime loop deliberately left out. Designed and planned in
[specs/2026-08-17-app-cli-shell-design.md](../superpowers/specs/2026-08-17-app-cli-shell-design.md)
and [plans/2026-08-17-app-cli-shell.md](../superpowers/plans/2026-08-17-app-cli-shell.md):

- New top-level `app/` tier: a `mosaic_app` library holding all logic behind test
  seams + a thin `mosaic` executable. `app/cli/` (CLI11 arg parsing → a plain
  `Options`; CLI11 confined to the app tier), `app/run/frame_pump.hpp` (the
  `FramePump` — measures wall-clock dt, drives `RuntimeLoop::advance`, sleeps to a
  target rate, stops on SIGINT / `--duration` / `--ticks`; clock/sleep/stop are
  **injected** as plain-`double`-seconds seams so tests run with no real time),
  `app/run/status_reporter.*` (`--json` periodic + summary lines, human-readable via
  logging #33), and `app/main.cpp` (real `std::chrono` + SIGINT wiring).
- `engine/reflection/builtin_components.hpp` — a shared `register_builtin_components`
  (registers `Transform`) the app and the future editor both call, so their component
  sets can't drift.
- `tests/unit/app/` — `[cli]`/`[app]` cases: arg parsing, the injected-clock pump
  (exact `--ticks` count, duration/signal stop, progress cadence — the pump is the
  real subject, `RuntimeLoop(fixed_dt, 1)` makes counts exact), non-positive
  `--fps`/`--fixed-hz` rejected, and the JSON reporter shapes. Full suite 175, all
  green; `mosaic run --ticks 10 --json` emits a summary and exits 0.
  - *Deferred (future Tools issues):* `mosaic create/editor/build`, a project
    generator, and the asset compiler — the CLI is structured for those subcommands.

**Jobs ([#34](https://github.com/Evol-Luci/mosaic/issues/34)) is built** — the last
remaining v0.1 foundation piece. A minimal task system for parallel work. Designed and
planned in
[specs/2026-08-17-jobs-design.md](../superpowers/specs/2026-08-17-jobs-design.md)
and [plans/2026-08-17-jobs.md](../superpowers/plans/2026-08-17-jobs.md):

- `foundation/jobs/thread_pool.{hpp,cpp}` — `ThreadPool`: N worker threads pulling FIFO
  from a mutex/condvar queue. `submit(task)` returns `std::future<void>` (a throwing
  task surfaces on `.get()`); `parallel_for(begin, end, body, grain)` splits a range
  into up to `thread_count` chunks, **blocks on an internal latch** until all complete,
  and rethrows the first body exception from the calling thread. The destructor
  **drains the queue then joins** — no leaked/detached threads, no unfulfilled future.
- Compiled `mosaic_jobs` STATIC lib (stdlib threads only, no third-party dep), wired via
  `find_package(Threads)` — honors the foundation tier contract.
- `tests/unit/jobs/` — `[jobs]` cases: submit-runs-all, future exception propagation,
  drain-on-shutdown (1000 tasks), single-thread pool, `parallel_for` exact coverage,
  empty range, body-exception rethrow, custom grain. Full suite 186, all green.
  - *Deferred (post-v0.1, per #34):* work-stealing, job dependencies/graphs, priorities,
    fibers, and **wiring the pool into `World::tick`** for parallel system execution
    (honoring each `System`'s `ComponentAccess`) — a separate engine follow-up; today
    ticking is single-threaded by design.

## What to build next

The engine is now **visible and operable**: `mosaic run --window` renders, and `mosaic
editor` opens the Studio with a hierarchy + a live reflection inspector. The v0.1 foundation
(logging #33, app/CLI #35, jobs #34) and rendering (#36 / 36a–36e-1) are complete; the
frontier is the remainder of the **Studio (#37)** — the viewport, then play/stop + console.
Per the [v0.1 scope](roadmap.md#the-v01-vertical-slice--the-first-mountain), each sub-issue
gets its own brainstorm → spec → plan → TDD cycle:

- **First *visible* subsystem** — rendering [#36](https://github.com/Evol-Luci/mosaic/issues/36)
  (Vulkan, **complete for v0.1**) then the Studio shell [#37](https://github.com/Evol-Luci/mosaic/issues/37)
  (**underway** — see the Studio decomposition below); the Studio depends on rendering.
  **#36 was decomposed** (design:
  [specs/2026-08-17-rendering-decomposition.md](../superpowers/specs/2026-08-17-rendering-decomposition.md))
  into five linear sub-issues:
  1. ~~[#42](https://github.com/Evol-Luci/mosaic/issues/42) 36a — device + window +
     swapchain (clear-screen)~~ ✓ **built.** The backend-neutral `RenderDevice`
     interface + `modules/render_vulkan` (GLFW, swapchain + resize recreation, per-frame
     sync, debug validation → logging) are in place; `mosaic run --window` clears the
     window each frame. Design/plan: [spec](../superpowers/specs/2026-08-17-render-36a-clear-screen-design.md) · [plan](../superpowers/plans/2026-08-17-render-36a-clear-screen.md).
  2. ~~[#43](https://github.com/Evol-Luci/mosaic/issues/43) 36b — mesh draw at `WorldTransform`~~ ✓ **built.** A `RenderSystem` (render-time traversal, fixed camera) draws entities with `WorldTransform` + `MeshRenderer` via a Vulkan graphics pipeline (glslc SPIR-V, depth buffer); `mosaic run --window` shows a spinning cube. `foundation/math` gained `perspective`/`look_at`. Design/plan: [spec](../superpowers/specs/2026-08-18-render-36b-mesh-draw-design.md) · [plan](../superpowers/plans/2026-08-18-render-36b-mesh-draw.md).
  3. ~~[#44](https://github.com/Evol-Luci/mosaic/issues/44) 36c — camera + view/projection~~ ✓ **built.** A reflected `Camera` component (lens: fov/near/far) placed via a `Transform`; the `RenderSystem` derives `view = inverse_affine(camera WorldTransform)` from the active camera (fallback if none). `foundation/math` gained `inverse_affine`. Design/plan: [spec](../superpowers/specs/2026-08-18-render-36c-camera-design.md) · [plan](../superpowers/plans/2026-08-18-render-36c-camera.md).
  4. [#45](https://github.com/Evol-Luci/mosaic/issues/45) 36d — material + texture + directional light — **split into two**:
     - ~~36d-1 (lighting)~~ ✓ **built.** Reflected `Material{base_color}` + `DirectionalLight` components; `Vertex` gained a normal; Lambert + ambient shading via object-space light in push constants (no descriptor sets). `foundation/math` gained `transform_direction`. Design/plan: [spec](../superpowers/specs/2026-08-18-render-36d1-lighting-design.md) · [plan](../superpowers/plans/2026-08-18-render-36d1-lighting.md).
     - ~~36d-2 (texture)~~ ✓ **built.** UVs on `Vertex`; `create_texture` + a Vulkan sampler + **descriptor sets**; an `Image` asset resource + a `TextureCache` resolving `Material.texture` (`AssetID`) through the asset registry (#20, cached/uploaded once); a procedural checkerboard demo. `foundation/math` gained `Vec2`. Design/plan: [spec](../superpowers/specs/2026-08-22-render-36d2-texture-design.md) · [plan](../superpowers/plans/2026-08-22-render-36d2-texture.md).
  5. [#46](https://github.com/Evol-Luci/mosaic/issues/46) 36e — point light + minimal render graph — **split into two**:
     - ~~36e-1 (point light)~~ ✓ **built.** A reflected `PointLight` component (`color`, `intensity`, `range`; world position derived from its entity's `WorldTransform`, like `Camera`); object-space Lambert point term with windowed inverse-square attenuation added to the fragment shader; the vertex shader now passes object-space fragment position; push constants grew 112→144 bytes (device `maxPushConstantsSize` assert). `mosaic run --window` now shows point-light distance falloff across the spinning cube alongside the existing directional light. Known limitation: attenuation distance is measured in object space, so non-uniform mesh scale warps it (exact for the unit-scale demo cube). Design/plan: [spec](../superpowers/specs/2026-08-22-render-36e1-point-light-design.md) · [plan](../superpowers/plans/2026-08-22-render-36e1-point-light.md).
     - **Render graph deferred** — a point light does not create a second pass, so the "≥2 passes justify a graph" bar is not met; re-filed to the first slice that genuinely needs ≥2 passes (a shadow-map depth pre-pass, or the Studio's offscreen-viewport-to-texture in #37). Not dropped. **Update: 37d met the ≥2-pass bar but still chose direct two-pass submission** (see 37d below); the RenderGraph is now re-filed to the first *dynamic* or ≥3-pass slice.

  Decisions fixed in the decomposition: **GLFW** windowing (backend/app only, never
  engine/foundation); the **RenderGraph is deferred past 36e** (direct submission behind
  the neutral device interface until ≥2 passes justify a graph — 36e-1 did not meet
  that bar; deferred to the next ≥2-pass slice, which is **37d**). Each sub-issue gets its
  own brainstorm → spec → plan → TDD cycle.

- **Studio shell** [#37](https://github.com/Evol-Luci/mosaic/issues/37) — the editor
  front-end, and where reflection (#18) becomes user-visible. **Decomposed** (design:
  [specs/2026-08-22-studio-decomposition.md](../superpowers/specs/2026-08-22-studio-decomposition.md))
  into five slices — v0.1 scope is the **lean core + console** (asset browser + Blender
  live-link deferred), GUI is **Dear ImGui (docking)**:
  1. ~~37a — editor shell~~ ✓ **built.** New `studio/` tier + `mosaic_studio`; a `mosaic
     editor` subcommand opens an ImGui docking window (dockspace + menu bar + placeholder
     panels); three default-no-op UI hooks on `RenderDevice` driven by the Vulkan backend.
     Design/plan: [spec](../superpowers/specs/2026-08-22-studio-37a-editor-shell-design.md) · [plan](../superpowers/plans/2026-08-22-studio-37a-editor-shell.md).
  2. ~~37b — scene hierarchy~~ ✓ **built.** A `SelectionModel` + `build_hierarchy(World)`
     seam; the Hierarchy panel lists entities (via `World::for_each_entity`) with
     click-to-select. Folded in two 37a review follow-ups. Design/plan:
     [spec](../superpowers/specs/2026-08-23-studio-37b-hierarchy-design.md) · [plan](../superpowers/plans/2026-08-23-studio-37b-hierarchy.md).
  3. ~~37c — reflection inspector~~ ✓ **built** (the #18 payoff). A mutable `FieldVisitor`
     + a `reflect()`-bridging adapter (the shared reflected-property API) + `ComponentReflection::components_on`/`inspect`;
     the Inspector edits a selected entity's component fields **live** (float `DragFloat`,
     others read-only). Design/plan: [spec](../superpowers/specs/2026-08-23-studio-37c-inspector-design.md) · [plan](../superpowers/plans/2026-08-23-studio-37c-inspector.md).
  4. ~~37d — viewport~~ ✓ **built.** A dockable **Viewport** panel shows the live 3D scene
     rendered into a panel-sized **offscreen** color+depth target and sampled by ImGui
     (`ImGui::Image`). The neutral `RenderDevice` gained four default-no-op viewport hooks
     (`set_viewport_size`/`begin_viewport_pass`/`end_viewport_pass`/`viewport_texture`); the
     Vulkan backend implements them behind a **lazy swapchain-pass** refactor so `mosaic run
     --window` and `mosaic editor` share one device unchanged. A headless `ViewportPanel`
     model + a `render_scene_to_viewport` seam carry the testable sizing/ordering; `mosaic
     editor` now renders the shared demo scene each frame. **RenderGraph decision:** minimal
     **direct two-pass submission** (one command buffer, one submit; inter-pass hazard via the
     offscreen pass `finalLayout` + subpass dependencies) — the graph stays deferred to the
     first dynamic/≥3-pass slice. Design/plan: [spec](../superpowers/specs/2026-08-23-studio-37d-viewport-design.md) · [plan](../superpowers/plans/2026-08-23-studio-37d-viewport.md).
  5. ~~37e — play/stop + console~~ ✓ **built.** A headless `PlayState` drives the
     `RuntimeLoop` from the editor frame loop (pause = `stop()`, resume = `start()`, Step =
     one `RuntimeLoop::step()` while paused); Play/Stop/Step live in the menu bar. A
     thread-safe `ConsoleBuffer` (capacity-capped ring, owned copies of the transient
     `Record` strings) + a `ConsoleSink : log::Sink` registered on the process-global logger
     capture #33 log output; the Console panel renders it. **Surfaced a gap:** the app
     installs no sink outside the editor, so `mosaic run` drops all logging — filed as a
     follow-up (install a stderr `ConsoleSink` in the CLI entry path). Design/plan:
     [spec](../superpowers/specs/2026-08-23-studio-37e-play-stop-console-design.md) · [plan](../superpowers/plans/2026-08-23-studio-37e-play-stop-console.md).

**Suggested order (rendering):** ~~#42 (36a)~~ ✓ → ~~#43 (36b)~~ ✓ → ~~#44 (36c)~~ ✓ →
~~36d-1 (lighting)~~ ✓ → ~~36d-2 (texture)~~ ✓ → ~~36e-1 (point light)~~ ✓ → **rendering complete.**

**Suggested order (Studio #37):** ~~37a (shell)~~ ✓ → ~~37b (hierarchy)~~ ✓ →
~~37c (inspector)~~ ✓ → ~~37d (viewport)~~ ✓ → ~~37e (play/stop + console)~~ ✓ →
**Studio #37 v0.1 lean core complete.**

**Full v0.1 order:** ~~#33 logging~~ ✓ → ~~#35 app/CLI shell~~ ✓ → ~~#34 jobs~~ ✓ →
~~#36 decompose~~ ✓ → ~~#42–#46 rendering (36a–36e-1)~~ ✓ → ~~37a (shell)~~ ✓ →
~~37b (hierarchy)~~ ✓ → ~~37c (inspector)~~ ✓ → ~~37d (viewport)~~ ✓ → ~~37e (play/stop + console)~~ ✓ →
**v0.1 Studio (#37) lean core complete.**

## Suggested sequence

```
#9 ECS core ✓ ──┬── #10 Serialization ✓
                └── #11 Command/event system ✓
                        │
                        └── World runtime ✓ ──┬── #18 reflection ✓ ── #19 Scene load/save ✓
                                              ├── #20 asset registry ✓
                                              ├── #21 transform system ✓ (+ foundation math #26)
                                              └── #22 runtime loop ✓
```

**The v0.1 headless engine core is complete.** The next frontier is the app/CLI
shell, the first visible subsystem (rendering / studio), and the remaining
foundation pieces (logging, jobs) — see *What to build next*.

#9, #10, #11, the World runtime, #18 reflection, #19 Scene load/save, #20 asset
registry, #21 transform system (+ foundation math #26), and #22 runtime loop are all
done. **The v0.1 headless engine core is complete** — the frontier moves off this
tree to the app/CLI shell, the first visible subsystem, and the remaining foundation
pieces (see *What to build next*).

## Ground rules for the worker

- Honor the tier/dependency contract in
  [ARCHITECTURE.md](../architecture/ARCHITECTURE.md): `foundation/` knows nothing
  above it; `engine/` depends only on `foundation/`.
- Each issue lists acceptance criteria and expects unit tests under `tests/unit/`.
- ~~Reflection metadata is a soft dependency for #10; #9 shipped a minimal
  `ComponentTypeID` shim (type identity only). The reflection system reconciles
  it — and the `World` needs its *generic* construction path before Scene
  load/save is buildable.~~ **Done by #18:** `ComponentTypeID` is now a re-export
  of `foundation::reflection::TypeID` (one identity source), and
  `engine/reflection/component_reflection.hpp` supplies the generic construction
  path Scene load/save calls.
- ~~ECS handoff: own the `on_destroy(e)` → `EntityRegistry::destroy(e)` sequencing
  and add an interleaved create/add/destroy/recycle integration test.~~ **Done by
  the `World`:** `tick` commits deferred destructions in that order, and
  `tests/unit/world/world_integration_test.cpp` covers the interleaved cycle.
- Event-drain handoff: the `World::tick` owns the once-per-tick
  `EventBus::drain_deferred()` at the tick's start. The still-open piece is the
  **runtime loop** that calls `World::tick(dt)` each frame — there is no scheduler
  yet, by design.
- Known tech debt ([#25](https://github.com/Evol-Luci/mosaic/issues/25), post-v0.1):
  `type_id<T>()` shares one global counter across all reflected types, so
  `ComponentRegistry::storages_` is sized by the max id of *any* reflected type
  (null gaps for non-components). Accepted, correctness-safe for v0.1; give
  components a dense id space later without reintroducing the #9 identity drift.
- If a decision in RFC 0001 turns out to be wrong under implementation, don't
  silently deviate — open an issue proposing an amendment and link the RFC. (See
  the redo/announce interpretation note under *Where we are* — a resolved
  ambiguity, not a deviation, but worth revisiting at the World/UI layer.)

## After the engine core

The **v0.1 headless engine core is complete**: entities, serialization,
commands/events, the runtime `World`, reflection, Scene load/save, the asset
registry, foundation math, the transform system, and the runtime loop. Everything
composes into a world that can be populated, serialized, reloaded, and ticked
deterministically, and **`mosaic run` now runs it in real time outside a test** — but
nothing is *visible* yet.

The **v0.1 foundation is now complete** too — logging
[#33](https://github.com/Evol-Luci/mosaic/issues/33), the app/CLI shell
[#35](https://github.com/Evol-Luci/mosaic/issues/35), and jobs
[#34](https://github.com/Evol-Luci/mosaic/issues/34) are all built. The one remaining
on-ramp is the first *visible* subsystem (its own future spec):
- **First visible subsystem** — Vulkan rendering
  [#36](https://github.com/Evol-Luci/mosaic/issues/36) (decomposed into #42–#46;
  see [the decomposition](../superpowers/specs/2026-08-17-rendering-decomposition.md))
  then the Studio shell [#37](https://github.com/Evol-Luci/mosaic/issues/37). The
  milestone that makes the engine something you can look at and operate (Studio depends on
  rendering). **Rendering 36a–36e-1 is complete** (`mosaic run --window` draws a spinning
  cube, viewed through a `Camera`, lit by a directional + point light and textured with a
  checkerboard resolved through the asset registry); the render-graph half of #46 remains
  deferred (not dropped) — 37d met the ≥2-pass bar but chose direct two-pass submission, so
  the graph is re-filed to the first dynamic/≥3-pass slice. **Studio #37 is underway**
  (decomposed into 37a–37e; see [the decomposition](../superpowers/specs/2026-08-22-studio-decomposition.md)):
  **all five slices — 37a (editor shell), 37b (hierarchy), 37c (reflection inspector), 37d
  (viewport), 37e (play/stop + console) — are built.** `mosaic editor` opens a docking window
  with a scene hierarchy, a live reflection inspector, a viewport showing the live 3D scene via
  an offscreen render target, and a play/stop/step toolbar + Console panel. **The v0.1 Studio
  (#37) lean core is complete.** Deferred post-v0.1 (their own future work): the asset browser
  and the Blender live-link preview. A small follow-up also stands: install a stderr
  `ConsoleSink` in the CLI entry path so `mosaic run` output (currently dropped) is visible.

Reassess this file as each lands.
