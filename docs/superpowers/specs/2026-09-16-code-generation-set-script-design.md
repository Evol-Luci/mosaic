# Code Generation — `set_script` — Design

## Goal (Phase 6, Agent ecosystem pillar — sub-project 3 of 7)

Sub-projects 1 and 2 gave an agent a validated way to create/mutate entities
and components (Agent API) and to inspect a scene's structure (scene
reasoning). Neither gives an agent a way to *author code* — attaching a
gameplay `ScriptComponent` (`engine/script/script_component.hpp`, a
reflected `{std::string source;}` holding a Python script defining
`on_update(dt)`, run each tick by `ScriptSystem`) currently requires either
the fully-generic `set_component` tool (which writes any string as
`source` with zero validation — a typo-riddled script silently sits there
until the next tick fails) or a raw file edit outside MCP entirely. This
slice adds one new tool, `set_script`, that reuses the exact validation
Studio's own Script Editor panel already performs for a human.

## Scope decision: gameplay scripts now, plugin files later

"Code generation" bundles at least two independent things: gameplay
`ScriptComponent` authoring (operates on already-loaded `World` state, no
new file I/O) and Studio plugin (`.py` file under a project's `plugins/`)
authoring (a filesystem write outside the `World`, needing its own
validation approach via `PluginHost`-style discovery). This slice ships
**only** gameplay `ScriptComponent` authoring. Plugin-file authoring is
recorded as a valuable next slice, not designed further here.

## `set_script`: reuses `ScriptEditorModel::apply()` as-is

`studio/script_editor.hpp`'s `ScriptEditorModel` already does exactly what
this tool needs, and is pure logic with zero ImGui/GUI dependency (its own
doc comment: "Pure logic (no ImGui)"):

```cpp
ScriptApplyResult apply(const std::string& source, World& world,
                        const ComponentReflection& reflection, EntityID e);
```

It test-compiles `source` against a throwaway `ScriptEngine` bound to the
live `world` (via an injected `EngineFactory`), and on success writes
`source` into entity `e`'s existing `ScriptComponent`. It returns
`ScriptApplyResult{Kind::Ok|Error|None, message}` — `message` is captured
stdout on `Ok` (usually empty) or the compile/attachment error text on
`Error`; `Kind::None` only occurs if no factory was supplied. `Kind::Error`
covers **both** a genuine compile error and "entity has no
`ScriptComponent`" (the tool doesn't attach one — an agent calls the
existing generic `add_component(e, "ScriptComponent")` first, exactly the
same two-step flow Studio's own panel already assumes: attach, then edit).

The new tool wraps this directly:

```json
{"ok": true, "message": ""}
```
or
```json
{"ok": false, "message": "SyntaxError: invalid syntax (<string>, line 1)"}
```

A compile error or missing-`ScriptComponent` result is a **graceful**
`{"ok": false, ...}` — not an MCP tool error. An agent iterating on a
script expects failures sometimes, the same reasoning already established
for every other tool's "not found" outcomes in the Agent API spec.
`McpToolError` stays reserved for malformed request parameters (`entity`
missing/mistyped, `source` missing/mistyped).

## Wiring: a special-cased dispatch, not a 17th uniform handler

`set_script` doesn't fit the existing `ToolHandler` function-pointer shape
(`World&, const ComponentReflection&, CommandStack&, const std::string&,
const nlohmann::json&`) — it alone needs an injected script-engine
factory, which a plain function pointer can't capture. Rather than widen
every one of the other 16 handlers' signatures to carry an unused factory
parameter, `handle_tool_call` special-cases `"set_script"` before
consulting the generic `tool_handlers()` map:

```cpp
using ScriptEngineFactory = std::function<std::unique_ptr<mosaic::engine::script::ScriptEngine>()>;

nlohmann::json handle_tool_call(World& world, const ComponentReflection& types, CommandStack& stack,
                                const std::string& scene_path, const std::string& tool_name,
                                const nlohmann::json& arguments,
                                const ScriptEngineFactory& script_factory = ScriptEngineFactory{});
```

`script_factory` is a new **trailing, defaulted** parameter — every
existing call site (every current test, `Fixture::call`) compiles
unchanged with no factory supplied at all (an empty `std::function`, which
`ScriptEditorModel` itself already handles gracefully via `Kind::None`).
Only `app/run/mcp.cpp` (constructing the real factory) and the new
`set_script`-specific tests need to pass one explicitly.

`app/run/mcp.cpp` constructs the factory identically to how `mosaic
editor` wires its own Script Editor
(`app/run/editor.cpp`'s `StudioApp` construction):

```cpp
[] { return mosaic::script::python::create_python_script_engine(); }
```

The **unrestricted** gameplay factory, not the sandboxed
`create_restricted_python_script_engine()` used for Studio plugins — a
`ScriptComponent` is gameplay code, run the same way `ScriptSystem` runs it
at play time, not a marketplace-sourced plugin.

## Zero CMakeLists changes

`script_editor.hpp` is header-only (confirmed: no `script_editor.cpp`
exists). `mosaic_app` already has the include path
(`target_include_directories(mosaic_app PUBLIC ${CMAKE_SOURCE_DIR})`) and
already privately links `mosaic_script_python` (added for the prior Agent
API work) — everything `set_script` transitively needs is already present.
No new library targets, no new `target_link_libraries` entries anywhere.

## Known, accepted limitation: not undoable

`ScriptEditorModel::apply()` mutates `sc->source` directly, never through
`CommandStack` — so a `set_script` call is **not** reversible via the
existing `undo`/`redo` MCP tools, unlike `set_component`. This is inherited
as-is from Studio's own Script Editor panel (which has the same property
for a human editing the same field) — not a new inconsistency this slice
introduces, and not solved here.

## Non-goals (explicit future expansion)

- **Studio plugin (`.py` file) authoring** — writing a file into a
  project's `plugins/` directory declaring `MOSAIC_MENU_ITEMS`/
  `MOSAIC_PANELS`/`MOSAIC_IMPORTERS`, with its own validation approach
  (likely a throwaway `PluginHost`-style load rather than a plain
  compile-check, since a plugin's correctness partly depends on those
  dict-shaped module-level declarations, not just syntactic validity). A
  real, valuable next slice — not designed further here.
- **Attaching a `ScriptComponent` as part of `set_script`** — deliberately
  left to the existing generic `add_component` tool; `set_script` only
  writes into an already-attached component, exactly mirroring
  `ScriptEditorModel::apply()`'s own behavior.
- **Undo support for `set_script`** — see above; would require routing the
  write through `CommandStack`, a change to `ScriptEditorModel` itself
  (shared with Studio's UI), out of scope for an MCP-only slice.
- **The `debug()` half of `ScriptEditorModel`** (compile + run
  `on_update(0)` once with a debugger attached, without writing) — a real,
  separate capability (closer to "automated testing," sub-project 5, than
  "code generation") not built here.

## Testing

- **`tests/unit/app/mcp_tools_test.cpp`** (extended): a real
  `create_python_script_engine` factory (matching existing test precedent
  in `python_script_engine_test.cpp`/`world_globals_test.cpp` — call
  `ensure_interpreter()` first) exercising: a valid script written to an
  entity that already has a `ScriptComponent` (`ok: true`, and the
  component's `source` field actually changed, verified via the existing
  `get_component` tool); a syntactically invalid script (`ok: false`, a
  non-empty `message`, and the component's `source` **unchanged** from
  before the failed call); an entity with no `ScriptComponent` at all
  (`ok: false`, message mentions the missing component); and malformed
  arguments (missing `entity` or `source`) throwing `McpToolError`, same
  as every other tool.
- No changes needed to `mcp_protocol_test.cpp` — the protocol layer is
  generic over tool name and dispatch result shape; adding a 17th tool to
  `tools_list_schema()`'s count needs its own count-assertion update
  though, in both `mcp_tools_test.cpp`'s own `tools_list_schema` case
  **and** `mcp_protocol_test.cpp`'s `tools/list` case — the exact gap
  self-caught and fixed during the *previous* sub-project (scene
  reasoning), now explicitly listed here so it isn't missed again.

## Files

- Modify: `app/mcp/mcp_tools.hpp` — `ScriptEngineFactory` alias,
  `handle_tool_call`'s new defaulted parameter, doc-comment tool count
  16 → **17**.
- Modify: `app/mcp/mcp_tools.cpp` — `#include "studio/script_editor.hpp"`,
  new `tool_set_script` function, the `handle_tool_call` special-case
  branch, a new `tools_list_schema()` entry.
- Modify: `app/run/mcp.cpp` — construct the real
  `create_python_script_engine` factory and pass it into the dispatch
  closure.
- Modify: `tests/unit/app/mcp_tools_test.cpp` — new test cases, and the
  existing `tools_list_schema` count assertion (16→17).
- Modify: `tests/unit/app/mcp_protocol_test.cpp` — the existing
  `tools/list` count assertion (16→17).
- Modify: `docs/project/next-steps.md` — new dated entry naming this as
  Phase 6 sub-project 3 of 7, with plugin-file authoring named as what's
  next within this sub-project (or sub-project 4, asset generation, if the
  user prefers to move on).
