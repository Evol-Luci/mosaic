# Agent API — MCP Server (`mosaic mcp`) — Design

## Goal (Phase 6, Agent Ecosystem pillar — sub-project 1 of 7)

Phase 6 of the roadmap ("agent API, scene reasoning, code generation, asset
generation, automated testing, project maintenance, optimization") is the
first work on top of a now-complete Phase 0-5: foundation, first world,
simulation, programming, production, and tool ecosystem are all shipped.
Phase 6 is dev-tooling for AI agents building games in Mosaic — not
in-game/gameplay AI — and is far too broad for one spec. It decomposes into
an ordered sequence of sub-projects, each roughly a dependency of the next:

1. **Agent API** (this spec) — the transport + command surface an external
   agent process uses to drive a Mosaic project.
2. Scene reasoning — semantic queries over a live/saved scene.
3. Code generation — agent-authored scripts/plugins.
4. Asset generation — agent-authored/imported assets.
5. Automated testing — agent-driven play-test verification.
6. Project maintenance — agent-assisted refactoring/upkeep.
7. Optimization — agent-assisted profiling/tuning.

This slice builds only #1: a new `mosaic mcp <project-dir>` subcommand
exposing a Model Context Protocol (MCP) server — the same protocol this very
development session already uses to reach other tools — so any MCP client
(Claude Code, or another) gets a validated, undoable, discoverable surface
for editing a Mosaic project's entities and components, instead of the two
options available today: raw CLI subcommands, or direct file edits that
bypass `ComponentReflection`/`CommandStack` entirely.

## Why MCP, not a script runner or a live IPC server

Two other transports were considered and rejected for this first slice:

- **Headless script runner** (`mosaic script <file.py>`) — minimal new code,
  reuses the existing `ScriptEngine`/`world_globals.cpp` Python surface
  directly. Rejected because it gives an agent raw code execution instead of
  a typed, discoverable, individually-permissioned tool surface — a step
  backward from what MCP already gives every other tool this session uses,
  and it duplicates rather than extends the existing Python scripting path.
- **Live IPC/socket server** attached to a running Studio session (mirrors
  the Blender Live-Link TCP server) — lets an agent drive and observe a
  *live* editing session in real time. Rejected for slice 1 as materially
  bigger scope (a new wire protocol, a new server embedded in the GUI
  process, a new client story) with no clear near-term need; live-session
  attachment is left as explicit future work, possibly its own later
  sub-project once slice 1's headless server proves the tool surface.

**MCP, headless, C++-native** was chosen: `mosaic mcp <project-dir>` is a new
CLI subcommand (not a GUI-attached server) implementing MCP's stdio
transport directly in C++, hand-rolling the JSON-RPC-over-stdio framing with
the already-vendored `nlohmann_json` — no new language/runtime dependency,
in the same "own small implementations, minimal deps" style as the vendored
ENet networking module. This directly reuses `World`/`ComponentReflection`/
`CommandStack` in-process; a Python-embedding alternative (running the
server as a script via the existing pybind11/CPython embedding) was
considered and rejected as an unnecessary extra marshaling layer and startup
cost for no benefit over binding the reflection/command core directly to
JSON-RPC the same way `world_globals.cpp` already binds it to pybind11.

## Session model: persistent World, explicit save

The server loads a project's scene into a live `World` **once**, at startup,
and holds it in memory across every tool call in the session — `create_entity`
then `set_component` compose, and `CommandStack` undo/redo work across
calls — mirroring how Studio itself works (open, edit, explicit Save). A
stateless per-call round-trip (load, mutate, save, on every single tool
call) was rejected: no cross-call undo/redo, and a full load+save on every
edit is wasted work for no benefit here.

## Startup

`mosaic mcp <project-dir>` resolves the project exactly as `mosaic editor`
does: `resolve_startup_scene`/`load_manifest` (`app/project/project_io.hpp`)
finds the manifest's `default_scene`; a directory with no `mosaic.toml`
fails immediately with a clear error (same as `resolve_startup_scene`'s
existing `nullopt` case), never falling back to a bare `scene.json` guess.
It then builds a `TypeRegistry` + `ComponentReflection`
(`register_builtin_components`), a `World`, and a `CommandStack`, and calls
the existing `populate_world` (`app/run/scene_load.hpp`) to load the scene —
which also runs `TransformSystem` once, matching `mosaic editor`'s own
startup sequence, so `WorldTransform` is valid from the first tool call.
No `RenderDevice`, GLFW, Vulkan, or `StudioApp` — fully headless. No
`AssetRegistry` either: `populate_world` takes no `AssetRegistry` parameter
(asset references are just `AssetID` values on components, resolved lazily
only when a resource is actually loaded for rendering — irrelevant to
headless entity/component editing), so building one for this slice would be
unused ceremony. Asset generation (sub-project 4) can add it when needed.

## Protocol

MCP's stdio transport: newline-delimited JSON-RPC 2.0 messages on
stdin/stdout, one message per line, UTF-8, no embedded newlines. A new
`app/mcp/` directory mirrors `app/docs/`'s existing split (a testable core
separate from its thin CLI wrapper, as `component_reference.hpp` already
does for `run/docs.cpp`):

- **`app/mcp/mcp_protocol.hpp`/`.cpp`** — the framing layer: reads
  newline-delimited JSON-RPC requests from an input stream, writes responses
  to an output stream, using `nlohmann::json` (already linked into
  `mosaic_app`) for the envelope (`jsonrpc`, `id`, `method`, `params`,
  `result`/`error`). Implements MCP's `initialize` handshake and
  `tools/list`/`tools/call` methods. Malformed JSON, an unknown method, or a
  request missing `id` (a notification) are handled per the JSON-RPC 2.0
  spec — a parse error or method-not-found response for requests, silent
  no-reply for notifications — and never crash the process.
- **`app/mcp/mcp_tools.hpp`/`.cpp`** — the tool-dispatch core: a function
  taking `World&`, `const ComponentReflection&`, `CommandStack&`, a tool
  name, and a `nlohmann::json` params object, returning a
  `nlohmann::json` result (or a typed error). No stdio, no JSON-RPC envelope
  concerns — independently unit-testable by constructing a `World` directly
  and calling it, the same way `world_globals_test.cpp` exercises
  `install_world_globals` without going through Python's `exec`.

**Value marshaling.** A component's field values cross the C++↔JSON boundary
via the engine's only public `Value`↔JSON conversion,
`foundation::serialization::to_json(const Value&) -> std::string` /
`from_json(const std::string&) -> Value` (`foundation/serialization/
json_backend.hpp`) — round-tripping through a string rather than exposing
the header-private `nlohmann::json`-direct helpers (`to_json_node`/
`from_json_node`) that already exist inside `json_backend.cpp` but aren't
declared in its header. This is a double string round-trip (`Value` →
`std::string` → parsed back into `nlohmann::json` for embedding in the
JSON-RPC result, and the reverse for `set_component`'s incoming payload) —
accepted as a non-issue at this call volume; exposing the internal
node-level helpers is a smaller, separate change if profiling ever shows
this matters.

## Tool set (v1 — entity/component CRUD only)

A near-exact mirror of `world_globals.cpp`'s existing Python surface, now
bound to MCP instead of pybind11 — the same architectural move
`install_imgui_globals` made for the Studio-panel transport, applied here to
JSON-RPC:

| Tool | Mirrors (`world_globals.cpp`) | Notes |
|---|---|---|
| `create_entity` | `create_entity()` | returns `{"id": <uint64>}` |
| `destroy_entity` | `destroy_entity(eid)` | deferred to next tick, per existing semantics |
| `get_component` | `get_component(eid, name)` | returns the component's fields as JSON, or `null` |
| `set_component` | `set_component(eid, name, value)` | routes through `CommandStack` (undoable), same as scripting |
| `add_component` | `add_component(eid, name)` | |
| `remove_component` | `remove_component(eid, name)` | |
| `has_component` | `has_component(eid, name)` | |
| `component_names` | `component_names(eid)` | |
| `entities` | `entities()` | |
| `entities_with` | `entities_with(name)` | |
| `entity_count` | `entity_count()` | |
| `undo` | `undo()` | |
| `redo` | `redo()` | |
| `undo_depth` | `undo_depth()` | |
| `save_scene` | — (new) | `scene::save_to_json` + `save_scene_file` to the manifest's `default_scene` path; returns the written path |

`emit` (gameplay `ScriptEvent` emission) is deliberately excluded — it's
runtime-tick/gameplay-event-specific, not meaningful for an agent editing
project state outside a running simulation.

Bad input (an unknown entity id, an unregistered component name) returns an
MCP tool error result (`isError: true` with a message) — never throws,
matching this codebase's consistent boundary-safety convention
(`ScriptEngine::run_string`, `PluginHost`, `ImportPluginHost` all follow the
same never-throw-across-the-boundary rule).

## Explicitly deferred (v1 scope boundary)

Documented here so a later expansion has a clear seam, not because these are
unwanted:

- **Wrapping existing CLI verbs as tools** (`create_project`,
  `build_project`, `import_asset`, `install_plugin`) — would let one MCP
  session scaffold/import/build, not just edit an already-open scene.
  Deferred to keep slice 1 tight; the tool-dispatch core's shape (a name +
  JSON params + JSON result, per tool) accepts new entries without
  redesign.
- **Live-Studio attachment** — an agent driving/observing a running GUI
  session in real time, per the rejected-approach discussion above.
- **Everything in sub-projects 2-7** (scene reasoning, code generation,
  asset generation, automated testing, project maintenance, optimization) —
  each gets its own spec once this slice's server exists to build on.

## Testing

- **`tests/unit/app/mcp_tools_test.cpp`** (new): the tool-dispatch core
  exercised directly — construct a `World`+`ComponentReflection`+
  `CommandStack`, call `handle_tool_call("create_entity", {})`, assert the
  JSON result; cover each of the 15 tools' success path, at least one
  failure path per tool that can fail (unknown entity/component), and that
  `set_component`'s undo/redo round-trips through the bound `CommandStack`
  exactly like the existing `world_globals_test.cpp` coverage for the same
  semantics.
- **`tests/unit/app/mcp_protocol_test.cpp`** (new): the framing layer in
  isolation — feed a `std::istringstream` of newline-delimited JSON-RPC
  requests, capture a `std::ostringstream` of responses; cover a well-formed
  `tools/call` round-trip, malformed JSON, an unknown method, and a
  notification (no `id`) producing no response line.
- **`tests/unit/app/options_test.cpp`** (extended): `mosaic mcp
  <project-dir>` parses into `Command::Mcp` with the project directory
  captured; missing the positional argument is rejected, matching `build`'s
  existing required-positional coverage.

## Files

- Modify: `app/cli/options.hpp`, `app/cli/options.cpp` — `Command::Mcp` +
  subcommand wiring, following `build`'s required-positional-argument
  pattern.
- Modify: `app/main.cpp` — dispatch `Command::Mcp` to `run_mcp`.
- Create: `app/run/mcp.hpp`, `app/run/mcp.cpp` — startup sequence (resolve
  project, build `World`/`ComponentReflection`/`CommandStack`,
  `populate_world`, run the protocol loop over stdin/stdout until EOF or a
  shutdown request).
- Create: `app/mcp/mcp_tools.hpp`, `app/mcp/mcp_tools.cpp` — the
  tool-dispatch core.
- Create: `app/mcp/mcp_protocol.hpp`, `app/mcp/mcp_protocol.cpp` — the
  JSON-RPC/stdio framing layer and MCP `initialize`/`tools/list`/
  `tools/call` handling.
- Modify: `app/CMakeLists.txt` — new sources.
- Create: `tests/unit/app/mcp_tools_test.cpp`,
  `tests/unit/app/mcp_protocol_test.cpp`.
- Modify: `tests/CMakeLists.txt`, `tests/unit/app/options_test.cpp`.
- Modify: `docs/project/next-steps.md` — new dated entry naming this as
  Phase 6 sub-project 1 of 7, with the remaining 6 named as what's next.
  `docs/project/roadmap.md` itself carries no in-progress/complete markers
  for any phase (confirmed: Phases 0-5 are described in plain prose with no
  status annotations even though all are done) — status tracking lives in
  `next-steps.md` alone, so `roadmap.md` needs no edit.

## Non-goals / follow-ups

- **Multi-scene projects** — `ProjectManifest` tracks one `default_scene`
  today; this slice doesn't change that. A project with multiple scene
  files has no MCP-exposed way to switch which one is loaded, matching
  `mosaic editor`'s own current single-scene-per-launch behavior.
- **Concurrent MCP sessions on the same project** — not addressed; two
  `mosaic mcp` processes against the same project directory could race on
  `save_scene`. No file locking is introduced here. Out of scope until a
  real multi-agent-session need appears.
- **Authentication/authorization** — MCP's stdio transport is local-process
  only (the client launches the server as a subprocess); no network
  exposure, so no auth story is needed at this scope.
