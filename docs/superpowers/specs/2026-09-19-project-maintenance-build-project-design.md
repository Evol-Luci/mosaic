# Project Maintenance — `build_project` — Design

## Goal (Phase 6, Agent ecosystem pillar — sub-project 6 of 7)

Sub-projects 1-5 gave an agent a validated way to create/mutate entities and
components, inspect scene structure, author scripts, import assets, and
actually run the simulation via `tick`. None gives an agent a way to verify
the project as a whole still **packages** correctly after a round of
changes — the kind of health check a human would run before calling a
session of edits "done." This slice adds one new tool, `build_project`,
wrapping the exact same pure function `mosaic build` already uses.

## Scope decision: one tool, following the established pattern

"Project maintenance" (the roadmap's phrase for this sub-project) plausibly
covers several things: wrapping the still-unwrapped CLI verbs (`mosaic
build`, `mosaic docs`, `mosaic plugin {list,install,remove}`), or something
novel like bulk refactoring across entities/components or project
health/validation checks with no existing machinery to reuse. This slice
ships **only** `build_project`, following the one-tool-per-slice pattern
sub-projects 3-5 each established. Plugin management, docs regeneration,
and any novel refactoring/validation capability are recorded as future
expansion, not designed further here.

## `build_project`: a thin wrapper over the existing pipeline

`app/project/bundle.hpp` already has:

```cpp
bool build_bundle(const std::string& project_dir, const std::string& out_dir,
                  const std::string& runtime_binary,
                  const mosaic::engine::reflection::ComponentReflection& types);
```

Header-only, already linked into `mosaic_app` via `app/run/build.cpp` (the
existing `mosaic build` CLI command) — no new CMakeLists changes, the same
"reuse, zero new deps" pattern every prior Agent-API-extension slice
followed. It validates the project has a manifest, refuses a self-build
(`out_dir == project_dir`), regenerates a prior bundle in place or refuses a
non-bundle non-empty output directory, copies the runtime binary in, and
copies the project's scene/assets/manifest — returning `false` (and
logging) on any failure, never throwing.

The new tool:

```cpp
nlohmann::json tool_build_project(const ComponentReflection& types, const std::string& project_dir,
                                  const nlohmann::json& args)
{
    const std::string out_dir = optional_string(args, "out_dir",
                                                (std::filesystem::path(project_dir) / "dist").string());
    std::error_code ec;
    const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) { return {{"ok", false}}; }
    return {{"ok", mosaic::app::build_bundle(project_dir, out_dir, exe.string(), types)}};
}
```

`out_dir` is optional, defaulting to `<project_dir>/dist` — matching
`mosaic build`'s own CLI default exactly. `types` (`ComponentReflection`)
and `project_dir` are **already available** in `handle_tool_call`'s scope
from earlier sub-projects — this is the first of the three special-cased
tools (`set_script`, `import_asset`, `build_project`) that needs **no new
parameter added to `handle_tool_call`'s signature at all**, since
everything it needs is already threaded through.

## Failure contract: matching `import_asset`'s exact convention

An `/proc/self/exe` resolution failure is folded into the same `{"ok":
false}` path as every other `build_bundle` failure (not a project, a
self-build, a non-bundle non-empty output dir, a failed copy) — not a
separately special-cased `McpToolError`. This keeps the contract identical
to `import_asset`'s: **any** failure reports `{"ok": false}`, and the
specific reason is only visible in the server's own log output
(`mosaic::foundation::log::error`, stderr), never returned inline.
`McpToolError` stays reserved for malformed request parameters only (a
wrongly-typed `out_dir`).

## Wiring: a third special-cased dispatch

`build_project` doesn't fit the uniform `ToolHandler` shape (it needs
`project_dir`, which the 17 CRUD/`describe_scene`/`tick` tools don't) —
`handle_tool_call` gains a third special case, following the exact
precedent `set_script` and `import_asset` already established:

```cpp
if (tool_name == "build_project")
{
    return tool_build_project(types, project_dir, arguments);
}
```

No signature change to `handle_tool_call` itself — `types` and
`project_dir` are both already parameters. `app/run/mcp.cpp` needs no
changes at all beyond what sub-projects 3 and 4 already wired.

## Non-goals (explicit future expansion)

- **Plugin management** (`mosaic plugin {list, install, remove, source
  ...}`) — a real, valuable next slice with its own pure functions
  (`find_package`/`install_package`/etc.) already available to wrap, just
  a larger surface (multiple tools) than this one-tool slice takes on.
- **Docs regeneration** (`mosaic docs`) — wrapping the existing component/
  scripting-globals reference generator as a tool; not designed further
  here.
- **Bulk refactoring across entities/components** (e.g. rename a field
  project-wide) or **project health/validation checks** (orphaned asset
  detection, broken reference detection) — genuinely novel capabilities
  with no existing machinery to reuse, unlike every prior Phase 6 slice;
  real future work, not scoped here.

## Testing

- **`tests/unit/app/mcp_tools_test.cpp`** (extended): a successful build
  against a real temp project (mirroring `bundle_test.cpp`'s and
  `asset_import_test.cpp`'s existing temp-directory fixture pattern) —
  `ok: true`, `<project>/dist` actually exists afterward. A custom
  `out_dir` argument honored (`ok: true`, the custom path exists). A
  non-project directory — `ok: false`, no crash. Malformed arguments (a
  non-string `out_dir`) throwing `McpToolError`. The existing `Fixture`'s
  3-argument `call(tool, args, project_dir)` overload (added for
  `import_asset`'s tests) is reused as-is — no new fixture changes needed.
  `/proc/self/exe` resolves to the real `mosaic_unit_tests` binary during
  tests (the currently-running process), which `build_bundle` happily
  copies in as "the runtime" — no mocking needed, matching how
  `bundle_test.cpp` itself already exercises `build_bundle` against real
  temp projects.
- **`tests/unit/app/mcp_protocol_test.cpp`**: the existing `tools/list`
  count assertion needs updating (19 → 20) — the same class of gap
  self-caught in each of the prior three sub-projects, called out here
  again so it isn't missed a fifth time.

## Files

- Modify: `app/mcp/mcp_tools.hpp` — doc-comment tool count 19 → 20. No
  signature changes.
- Modify: `app/mcp/mcp_tools.cpp` — `#include <filesystem>`,
  `#include <system_error>`, `#include "app/project/bundle.hpp"`, a new
  `optional_string` helper alongside the existing `optional_uint64`/
  `optional_double`, a new `tool_build_project` function, the third
  `handle_tool_call` special-case, a new `tools_list_schema()` entry, a
  new `schema_build()` helper.
- Modify: `tests/unit/app/mcp_tools_test.cpp` — new test cases, the
  existing `tools_list_schema` count assertion (19 → 20).
- Modify: `tests/unit/app/mcp_protocol_test.cpp` — existing `tools/list`
  count assertion (19 → 20).
- Modify: `docs/project/next-steps.md` — new dated entry naming this as
  Phase 6 sub-project 6 of 7, with plugin management and docs regeneration
  named as what's next within this sub-project (or sub-project 7,
  optimization, if the user prefers to move on).
