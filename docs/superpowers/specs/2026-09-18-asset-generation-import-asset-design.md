# Asset Generation — `import_asset` — Design

## Goal (Phase 6, Agent ecosystem pillar — sub-project 4 of 7)

Sub-projects 1-3 gave an agent a validated way to create/mutate entities and
components (Agent API), inspect a scene's structure (scene reasoning), and
author a gameplay script's source (code generation). None gives an agent a
way to bring an asset file (generated itself, or provided) into a project's
cooked-asset pipeline — today that means either raw file writes into
`assets/` (bypassing cooking, metadata registration, and every one of
`import_asset()`'s existing safety checks entirely) or falling back to the
`mosaic import` CLI outside MCP. This slice adds one new tool, `import_asset`,
that reuses the exact validated import pipeline `mosaic import` already
uses.

## Scope decision: importing provided files, not generating content

"Asset generation" bundles at least two independent things: importing a
file an agent already has (generated itself via its own file-editing tools,
or provided by the user) through the validated cook-and-register pipeline,
versus actually generating asset *content* from scratch inside Mosaic
(procedural textures, meshes) — a much bigger scope with no existing
machinery to reuse. This slice ships **only** the import wrapper.
Procedural generation, and confirming/documenting `AssetID` referencing via
the already-existing generic `set_component` tool, are recorded as future
expansion, not designed further here.

## `import_asset`: a thin wrapper over the existing pipeline

`app/asset/asset_io.hpp` already has:

```cpp
std::optional<mosaic::foundation::serialization::AssetID>
import_asset(const std::string& project_dir, const std::string& source_file);
```

Header-only, already linked into `mosaic_app` via `app/run/import.cpp` (the
existing `mosaic import` CLI command) — no new CMakeLists changes, the same
"reuse, zero new deps" pattern every prior sub-project 1-3 slice followed.
It resolves the source file's type by extension (`.py` → script, `.wav` →
audio, `.ttf`/`.otf` → font, a project's `MOSAIC_IMPORTERS` plugin
extensions, else a PPM-image fallback), cooks it, writes
`<assets>/<stem>.<ext>` + `<assets>/<stem>.toml` metadata, and returns a
fresh `AssetID` — refusing (returning `nullopt`) on a stem collision rather
than orphaning a prior asset, and never leaving a half-written asset behind
on a mid-write failure. All of this is already tested, shared behavior; this
slice adds no new asset logic.

The new tool:

```cpp
nlohmann::json tool_import_asset(const std::string& project_dir, const nlohmann::json& args)
{
    const std::string source = require_string(args, "source");
    const auto asset_id = import_asset(project_dir, source);
    return {{"asset_id", asset_id ? nlohmann::json(asset_id->to_string()) : nlohmann::json(nullptr)}};
}
```

`project_dir` is already known to the MCP session (the directory `mosaic
mcp` was launched against) — the tool's only JSON argument is `source`, the
file path to import. Result shape: `{"asset_id": "<uuid-string>"}` on
success (via `AssetID::to_string()`, the same string form already used
wherever an `AssetID` serializes into scene JSON), or `{"asset_id": null}`
on failure.

## Wiring: another special-cased dispatch, mirroring `set_script`

Like `set_script`, `import_asset` doesn't fit the uniform `ToolHandler`
shape used by the other 17 tools — it needs the session's `project_dir`,
which none of the World-mutating tools need (they only need `scene_path`,
already threaded through). Rather than widen every existing handler's
signature for one more unused parameter, `handle_tool_call` gains a second
special case, following the exact precedent `set_script` established:

```cpp
nlohmann::json handle_tool_call(World& world, const ComponentReflection& types, CommandStack& stack,
                                const std::string& scene_path, const std::string& tool_name,
                                const nlohmann::json& arguments, const ScriptEngineFactory& script_factory,
                                const std::string& project_dir)
{
    if (tool_name == "set_script") { return tool_set_script(world, types, arguments, script_factory); }
    if (tool_name == "import_asset") { return tool_import_asset(project_dir, arguments); }
    // ... existing dispatch unchanged
}
```

`project_dir` is a new trailing, **defaulted** parameter (`const
std::string& project_dir = std::string{}`), so every existing call site
(every current test, `Fixture::call`) compiles unchanged with zero
disruption — the same defaulting discipline `script_factory` already
established. `app/run/mcp.cpp` already has `project_dir` as a local
variable from its own startup resolution (`resolve_startup_scene`/
`load_manifest`); it just needs threading into the dispatch closure
alongside the other already-captured session state.

## Accepted limitation: no inline failure reason

`import_asset()`'s own return type is `std::optional<AssetID>` — every
failure reason (unsupported extension, a bad WAV/PPM/font, a stem
collision, a reserved-type-name plugin conflict) is reported only via
`mosaic::foundation::log::error`, which lands on the MCP server process's
**stderr**, not inside the JSON-RPC response. An agent calling
`import_asset` and getting `{"asset_id": null}` has no machine-readable
reason available through the tool call itself — exactly the same
experience a human already gets from `mosaic import`'s own CLI output
today (the log line is visible in the terminal, not returned as a string).
This is inherited as-is, not a new gap this slice introduces, and not
solved here — fixing it would mean changing `import_asset()`'s shared
return type, which the CLI depends on too, out of scope for an MCP-only
slice.

## Non-goals (explicit future expansion)

- **Procedural asset generation** — generating texture/mesh/audio content
  from scratch inside Mosaic rather than importing a provided file. A real,
  much larger capability with no existing machinery to build on; likely
  needs its own decomposition when it's actually designed.
- **`AssetID` referencing via `set_component`** — attaching an
  already-imported asset to a component field (e.g. a `MeshRenderer`'s
  material texture) is arguably already possible today through the
  existing generic `set_component` tool with a `Value` containing an
  `AssetID` string — worth confirming and documenting with a real test,
  but not designed further here.
- **Returning a structured failure reason from `import_asset`** — see
  Accepted limitation above.

## Testing

- **`tests/unit/app/mcp_tools_test.cpp`** (extended): a real project
  fixture (mirroring `project_create_test.cpp`'s temp-directory pattern —
  `create_project` into a `getpid()`-unique temp dir) with a real source
  file to import (a minimal valid PPM image is the simplest to
  hand-construct inline, matching `image_cook_test.cpp`'s own fixture
  approach): a successful import (`asset_id` is a non-empty, well-formed
  UUID string; the cooked blob + metadata files actually exist on disk
  afterward); an unsupported/missing source file (`asset_id: null`, no
  crash); malformed arguments (missing `source`) throwing `McpToolError`,
  same as every other tool. A stem-collision case (import the same file
  twice) asserting the second call returns `null` without disturbing the
  first import's `AssetID` — mirrors `import_asset()`'s own documented
  refuse-don't-overwrite behavior.
- **`tests/unit/app/mcp_protocol_test.cpp`**: the existing `tools/list`
  count assertion needs updating (17 → 18) — the exact class of gap
  self-caught twice already in this session's prior two sub-projects, now
  explicitly called out here so a third implementer doesn't miss it either.

## Files

- Modify: `app/mcp/mcp_tools.hpp` — `handle_tool_call`'s new defaulted
  `project_dir` parameter, doc-comment tool count 17 → 18.
- Modify: `app/mcp/mcp_tools.cpp` — `#include "app/asset/asset_io.hpp"`,
  new `tool_import_asset` function, the `handle_tool_call` special-case,
  a new `tools_list_schema()` entry, a new `schema_source()` helper.
- Modify: `app/run/mcp.cpp` — pass the already-known `project_dir` into the
  dispatch closure.
- Modify: `tests/unit/app/mcp_tools_test.cpp` — new test cases, the
  existing `tools_list_schema` count assertion (17 → 18), and a third
  `Fixture::call` overload taking a `project_dir` string as its 3rd
  parameter (distinguishable from the existing `ScriptEngineFactory`
  overload by parameter type, no ambiguity):
  `call(tool, args, project_dir) → handle_tool_call(world, types, stack,
  "", tool, args, {}, project_dir)`.
- Modify: `tests/unit/app/mcp_protocol_test.cpp` — the existing
  `tools/list` count assertion (17 → 18).
- Modify: `docs/project/next-steps.md` — new dated entry naming this as
  Phase 6 sub-project 4 of 7, with procedural asset generation and
  `AssetID` referencing named as what's next within this sub-project (or
  sub-project 5, automated testing, if the user prefers to move on).
