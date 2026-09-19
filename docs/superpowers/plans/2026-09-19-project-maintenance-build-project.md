# Project Maintenance — `build_project` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a 20th MCP tool, `build_project`, to the existing `mosaic mcp` server: wrap `app/project/bundle.hpp::build_bundle(project_dir, out_dir, runtime_binary, types)` as-is — the exact same pure function `mosaic build` already uses — returning `{"ok": bool}`.

**Architecture:** A third special-cased branch in `handle_tool_call` (alongside the existing `set_script`/`import_asset` ones) — but unlike those two, `build_project` needs **no new parameter** added to `handle_tool_call`'s signature at all, since `types` and `project_dir` are already threaded through from prior sub-projects.

**Tech Stack:** C++20, `nlohmann_json`, the existing `build_bundle()` pipeline (already a build dependency, header-only, already linked into `mosaic_app` via `run/build.cpp`). No new CMakeLists changes.

**Spec:** `docs/superpowers/specs/2026-09-19-project-maintenance-build-project-design.md`

## Global Constraints

- `build_project` never returns a failure reason — `{"ok": false}` on any failure (not a project, self-build, a non-bundle non-empty output dir, a failed copy, or a failed `/proc/self/exe` resolution). The actual reason is only in the server's own log output, exactly matching `import_asset`'s established convention.
- `out_dir` is optional, defaulting to `<project_dir>/dist`.
- `McpToolError` stays reserved for malformed request parameters (a wrongly-typed `out_dir`) — never for a real build failure.
- No new CMakeLists changes — `bundle.hpp` is header-only and already linked into `mosaic_app`.

---

### Task 1: `build_project` tool

**Files:**
- Modify: `app/mcp/mcp_tools.hpp` — doc-comment tool count 19 → 20. No signature changes.
- Modify: `app/mcp/mcp_tools.cpp` — new includes, `optional_string` helper, `tool_build_project`, the third `handle_tool_call` special-case, a new `tools_list_schema()` entry, a new `schema_build()` helper.
- Test: `tests/unit/app/mcp_tools_test.cpp` (extended) — new test cases, existing tool-count assertion (19 → 20).
- Test: `tests/unit/app/mcp_protocol_test.cpp` (extended) — existing tool-count assertion (19 → 20).

**Interfaces:**
- Consumes: `mosaic::app::build_bundle(const std::string&, const std::string&, const std::string&, const ComponentReflection&) -> bool` (`app/project/bundle.hpp`, pre-existing), `mosaic::app::create_project` (pre-existing, test-only).
- Produces: no new public function signature — `handle_tool_call`'s existing 8-parameter signature is unchanged; only its body gains a new `if` branch.

- [ ] **Step 1: Write the failing tests**

Append these test cases to `tests/unit/app/mcp_tools_test.cpp` (the existing 3-argument `Fixture::call(tool, args, project_dir)` overload, added for `import_asset`'s tests, is reused as-is — no fixture changes needed):

```cpp
TEST_CASE("mcp_tools: build_project packages a project into a standalone bundle", "[app][mcp]")
{
    Fixture f;
    const std::string proj = (std::filesystem::temp_directory_path() /
                              (std::string("mosaic_mcp_build_") + std::to_string(::getpid()))).string();
    std::filesystem::remove_all(proj);
    REQUIRE(mosaic::app::create_project(proj, f.types));

    const nlohmann::json result = f.call("build_project", nlohmann::json::object(), proj);
    REQUIRE(result.at("ok").get<bool>() == true);
    REQUIRE(std::filesystem::exists(std::filesystem::path(proj) / "dist"));

    std::filesystem::remove_all(proj);
}

TEST_CASE("mcp_tools: build_project honors a custom out_dir argument", "[app][mcp]")
{
    Fixture f;
    const std::string proj = (std::filesystem::temp_directory_path() /
                              (std::string("mosaic_mcp_build_custom_") + std::to_string(::getpid()))).string();
    std::filesystem::remove_all(proj);
    REQUIRE(mosaic::app::create_project(proj, f.types));
    const std::string out = (std::filesystem::temp_directory_path() /
                             (std::string("mosaic_mcp_build_out_") + std::to_string(::getpid()))).string();
    std::filesystem::remove_all(out);

    const nlohmann::json result = f.call("build_project", {{"out_dir", out}}, proj);
    REQUIRE(result.at("ok").get<bool>() == true);
    REQUIRE(std::filesystem::exists(out));

    std::filesystem::remove_all(proj);
    std::filesystem::remove_all(out);
}

TEST_CASE("mcp_tools: build_project returns ok:false for a non-project directory", "[app][mcp]")
{
    Fixture f;
    const std::string notproj = (std::filesystem::temp_directory_path() /
                                 (std::string("mosaic_mcp_build_notproj_") + std::to_string(::getpid()))).string();
    std::filesystem::remove_all(notproj);
    std::filesystem::create_directories(notproj);

    const nlohmann::json result = f.call("build_project", nlohmann::json::object(), notproj);
    REQUIRE(result.at("ok").get<bool>() == false);

    std::filesystem::remove_all(notproj);
}

TEST_CASE("mcp_tools: build_project throws McpToolError for a malformed out_dir", "[app][mcp]")
{
    Fixture f;
    REQUIRE_THROWS_AS(f.call("build_project", {{"out_dir", 123}}, std::string("/tmp")), McpToolError);
}
```

Update the existing `tools_list_schema` count test — both occurrences of `19` become `20`:

```cpp
TEST_CASE("mcp_tools: tools_list_schema lists all 20 v1 tools with a description and inputSchema", "[app][mcp]")
{
    const nlohmann::json schema = tools_list_schema();
    REQUIRE(schema.contains("tools"));
    const nlohmann::json tools = schema.at("tools");
    REQUIRE(tools.size() == 20);
```
(leave the rest of that test case's body unchanged).

In `tests/unit/app/mcp_protocol_test.cpp`, update the one existing count assertion:

```cpp
    REQUIRE(parsed.at("result").at("tools").size() == 20);
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build -j"$(nproc)"`
Expected: FAIL to build — `build_project` isn't a registered MCP tool yet.

- [ ] **Step 3: Update `mcp_tools.hpp`**

Change the `tools_list_schema()` doc comment:

```cpp
// The MCP `tools/list` response body: { "tools": [ { name, description, inputSchema }, ... ] }
// for all 20 v1 tools (entity/component CRUD + undo/redo/save_scene + describe_scene +
// set_script + import_asset + tick + build_project).
nlohmann::json tools_list_schema();
```

No other changes to this file — `handle_tool_call`'s declaration is unchanged, since `build_project` needs no new parameter.

- [ ] **Step 4: Update `mcp_tools.cpp`**

Add the includes, alongside the existing ones (right after `#include "app/mcp/mcp_tools.hpp"`'s blank line, grouped with the other standard-library includes):

```cpp
#include <filesystem>
#include <system_error>
```

Add this include alongside the other `app/`-tier includes:

```cpp
#include "app/project/bundle.hpp"
```

Add the new argument helper, right after `optional_double` (which currently ends just before `tool_create_entity`):

```cpp
std::string optional_string(const nlohmann::json& args, const char* key, const std::string& default_value)
{
    if (!args.contains(key)) { return default_value; }
    return require_string(args, key);
}
```

Add `tool_build_project`, placed after `tool_tick` and before `tool_handlers()`. Note it, like `tool_set_script`/`tool_import_asset`, does NOT go into the `tool_handlers()` map — it needs `project_dir`, which those tools' uniform signature doesn't carry:

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

Add the new schema helper, right after `schema_tick()` (the current last schema helper):

```cpp
nlohmann::json schema_build()
{
    return {{"type", "object"}, {"properties", {{"out_dir", {{"type", "string"}}}}}, {"required", nlohmann::json::array()}};
}
```

Add the `tools_list_schema()` entry, after the `tick` entry (the current last one):

```cpp
    tools.push_back({{"name", "build_project"},
                     {"description", "Package the project into a standalone bundle (default "
                                     "output <project>/dist, or a given out_dir), embedding this "
                                     "running mosaic binary as the runtime -- the same operation "
                                     "`mosaic build` performs. ok is false on any failure (the "
                                     "specific reason is only visible in the server's own log "
                                     "output, matching import_asset's same convention)."},
                     {"inputSchema", schema_build()}});
```

Change `handle_tool_call`'s definition to add the third special-case — its signature itself is unchanged:

```cpp
nlohmann::json handle_tool_call(World& world, const ComponentReflection& types, CommandStack& stack,
                                const std::string& scene_path, const std::string& tool_name,
                                const nlohmann::json& arguments, const ScriptEngineFactory& script_factory,
                                const std::string& project_dir)
{
    if (tool_name == "set_script")
    {
        return tool_set_script(world, types, arguments, script_factory);
    }
    if (tool_name == "import_asset")
    {
        return tool_import_asset(project_dir, arguments);
    }
    if (tool_name == "build_project")
    {
        return tool_build_project(types, project_dir, arguments);
    }
    const auto& handlers = tool_handlers();
    const auto it = handlers.find(tool_name);
    if (it == handlers.end())
    {
        throw McpToolError("unknown tool: " + tool_name);
    }
    return it->second(world, types, stack, scene_path, arguments);
}
```

`app/run/mcp.cpp` needs no changes at all — `types` and `project_dir` are already threaded into the dispatch closure from the `set_script`/`import_asset` work.

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build -j"$(nproc)" --target mosaic_unit_tests && ./build/tests/mosaic_unit_tests "[mcp]"`
Expected: PASS — all 4 new `build_project` cases, plus every pre-existing `[mcp]` case with the updated 20-tool counts. No failures.

- [ ] **Step 6: Commit**

```bash
git add app/mcp/mcp_tools.hpp app/mcp/mcp_tools.cpp \
       tests/unit/app/mcp_tools_test.cpp tests/unit/app/mcp_protocol_test.cpp
git commit -m "$(cat <<'EOF'
feat(mcp): add build_project, a 20th Agent API tool

Wraps app/project/bundle.hpp::build_bundle() as-is -- the exact same
pure function `mosaic build` already uses. ok is false on any failure
(not a project, self-build, a non-bundle non-empty output dir, a
failed copy, or a failed /proc/self/exe resolution); the specific
reason stays server-log-only, matching import_asset's exact
convention. Needs no new handle_tool_call parameter -- types and
project_dir were already threaded through from the set_script/
import_asset work. Phase 6 sub-project 6 of 7 (project maintenance)
-- plugin management and docs regeneration remain future expansion,
documented but not built here.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01CEBXDEjzbGDpMyLrKCN4mY
EOF
)"
```

---

### Task 2: Manual verification + `next-steps.md` entry

**Files:**
- Modify: `docs/project/next-steps.md` — new dated entry.

**Interfaces:**
- Consumes: `build_project` via the real `mosaic mcp` CLI (Task 1).
- Produces: nothing new; verification + documentation only.

- [ ] **Step 1: Manually verify against a real project**

Run:

```bash
cmake --build build -j"$(nproc)"
rm -rf /tmp/build_project_verify && ./build/app/mosaic create /tmp/build_project_verify
printf '%s\n%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"build_project","arguments":{}}}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"build_project","arguments":{"out_dir":"/no/such/parent/dir/wont/exist"}}}' \
  | ./build/app/mosaic mcp /tmp/build_project_verify
ls /tmp/build_project_verify/dist/
```

Expected: response 1 shows `{"ok":true}`; `ls` shows the bundle contents (`mosaic`, `run.sh`, the project's scene/assets); response 2's behavior depends on whether the parent path can be created — inspect it by eye, confirming no crash either way (the tool degrades to `{"ok":false}` gracefully on any real failure, per the accepted no-inline-reason limitation).

- [ ] **Step 2: Run the full test suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: green aside from the two known pre-existing flakes. Zero new failures.

- [ ] **Step 3: Add the dated `next-steps.md` entry**

Prepend a new dated paragraph to the top of the changelog blob in `docs/project/next-steps.md` (immediately before the current most-recent `**Last updated:**` entry, matching the existing style), summarizing: `build_project` ships as Phase 6 sub-project 6 of 7 (project maintenance); a 20th MCP tool wrapping `build_bundle()` as-is, letting an agent verify a project still packages correctly after a round of changes; no inline failure reason (server-log-only, matching `import_asset`'s convention); the first of the three special-cased tools needing no new `handle_tool_call` parameter, since `types`/`project_dir` were already threaded through; plugin management and docs regeneration are recorded as valuable future upgrades, not built here; next Phase 6 sub-project is optimization (7 of 7, the last one). Link this plan and the spec.

- [ ] **Step 4: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "$(cat <<'EOF'
docs: record build_project's next-steps.md entry

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01CEBXDEjzbGDpMyLrKCN4mY
EOF
)"
```

---

## Self-Review Notes

- **Spec coverage:** thin wrapper over `build_bundle()` with no new bundling logic (Task 1) ✓; `{"ok": false}` graceful failure, no inline reason, including a failed `/proc/self/exe` resolution folded into the same path (Task 1's 3rd test + Step 4's code) ✓; third special-cased dispatch needing no new `handle_tool_call` parameter (Task 1 Step 4) ✓; `out_dir` optional with a default matching the CLI (Task 1's 2nd test) ✓; zero CMakeLists changes (confirmed: no `target_link_libraries`/`add_library` edits anywhere) ✓; manual verification + next-steps.md (Task 2) ✓.
- **Placeholder scan:** no TBD/TODO; every step has real code or an exact command.
- **Type consistency:** `tool_build_project`'s signature (`const ComponentReflection&, const std::string&, const nlohmann::json&`) is defined once and called identically from `handle_tool_call`'s new branch; `optional_string` mirrors the existing `optional_uint64`/`optional_double` shape exactly (same default-value pattern, same `require_string` delegation for type validation when present).
