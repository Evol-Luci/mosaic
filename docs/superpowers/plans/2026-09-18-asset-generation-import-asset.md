# Asset Generation — `import_asset` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an 18th MCP tool, `import_asset`, to the existing `mosaic mcp` server: wrap `app/asset/asset_io.hpp::import_asset(project_dir, source_file)` as-is — the exact same cook-and-register pipeline `mosaic import` already uses — returning the new `AssetID` (or `null` on failure).

**Architecture:** `handle_tool_call` gains a second special-cased dispatch branch (alongside `set_script`'s existing one) plus a new trailing, defaulted `project_dir` parameter. `app/run/mcp.cpp` already has `project_dir` as a local from its own startup resolution — it just needs threading into the dispatch closure.

**Tech Stack:** C++20, `nlohmann_json`, the existing `import_asset()` pipeline (already a build dependency, header-only, already linked into `mosaic_app` via `run/import.cpp`). No new CMakeLists changes anywhere.

**Spec:** `docs/superpowers/specs/2026-09-18-asset-generation-import-asset-design.md`

## Global Constraints

- `import_asset` never returns a failure reason — `{"asset_id": null}` on any failure (unsupported format, missing source, stem collision). The actual reason is only in the server's own log output (stderr), exactly matching `mosaic import`'s own CLI UX. Not solved here.
- `handle_tool_call`'s new `project_dir` parameter is trailing and defaulted (`= std::string{}`) — every existing call site (every current test, `Fixture::call`) must keep compiling with zero changes.
- `McpToolError` stays reserved for malformed request parameters (`source` missing/mistyped) — a real import failure is graceful, never an error.
- No new CMakeLists changes — `app/asset/asset_io.hpp` is header-only and already linked into `mosaic_app`.

---

### Task 1: `import_asset` tool

**Files:**
- Modify: `app/mcp/mcp_tools.hpp` — `handle_tool_call`'s new defaulted `project_dir` parameter, doc-comment tool count 17 → 18.
- Modify: `app/mcp/mcp_tools.cpp` — new include, new `tool_import_asset`, the second `handle_tool_call` special-case, a new `tools_list_schema()` entry, a new `schema_source()` helper.
- Modify: `app/run/mcp.cpp` — thread the already-known `project_dir` into the dispatch closure.
- Test: `tests/unit/app/mcp_tools_test.cpp` (extended) — new `Fixture::call` overload, new test cases, existing tool-count assertion (17 → 18).
- Test: `tests/unit/app/mcp_protocol_test.cpp` (extended) — existing tool-count assertion (17 → 18).

**Interfaces:**
- Consumes: `mosaic::app::import_asset(const std::string& project_dir, const std::string& source_file) -> std::optional<AssetID>` (`app/asset/asset_io.hpp`, pre-existing), `mosaic::app::create_project`/`load_scene_file`/`save_scene_file` (pre-existing, test-only).
- Produces: `handle_tool_call`'s new 8th parameter (`const std::string& project_dir = std::string{}`) — no new public function name, just an extended signature. Consumed by `app/run/mcp.cpp` (this task) and the test file's new `Fixture::call` overload (this task).

- [ ] **Step 1: Write the failing tests**

Add these includes to `tests/unit/app/mcp_tools_test.cpp` (alongside the existing ones — `<filesystem>` is already present):

```cpp
#include "app/asset/asset_io.hpp"
#include "app/project/project_io.hpp"
#include "app/run/scene_io.hpp"
```

Add a third `Fixture::call` overload (alongside the existing two — do not remove or change them):

```cpp
    nlohmann::json call(const std::string& tool, const nlohmann::json& args, const std::string& project_dir)
    {
        return handle_tool_call(world, types, stack, "", tool, args, {}, project_dir);
    }
```

Update the existing `tools_list_schema` count test — both occurrences of `17` become `18`:

```cpp
TEST_CASE("mcp_tools: tools_list_schema lists all 18 v1 tools with a description and inputSchema", "[app][mcp]")
{
    const nlohmann::json schema = tools_list_schema();
    REQUIRE(schema.contains("tools"));
    const nlohmann::json tools = schema.at("tools");
    REQUIRE(tools.size() == 18);
```
(leave the rest of that test case's body unchanged).

In `tests/unit/app/mcp_protocol_test.cpp`, update the one existing count assertion:

```cpp
    REQUIRE(parsed.at("result").at("tools").size() == 18);
```

Append these test cases to `tests/unit/app/mcp_tools_test.cpp`:

```cpp
TEST_CASE("mcp_tools: import_asset cooks a PPM into the project and returns an asset_id", "[app][mcp]")
{
    Fixture f;
    const std::string proj = (std::filesystem::temp_directory_path() /
                              (std::string("mosaic_mcp_import_") + std::to_string(::getpid()))).string();
    std::filesystem::remove_all(proj);
    REQUIRE(mosaic::app::create_project(proj, f.types));

    const std::string src = (std::filesystem::temp_directory_path() /
                             (std::string("mosaic_mcp_import_src_") + std::to_string(::getpid()) + ".ppm")).string();
    std::string ppm = "P6\n2 1\n255\n";
    const unsigned char px[] = {255, 0, 0, 0, 255, 0};
    ppm.append(reinterpret_cast<const char*>(px), sizeof(px));
    REQUIRE(mosaic::app::save_scene_file(src, ppm));

    const nlohmann::json result = f.call("import_asset", {{"source", src}}, proj);
    REQUIRE_FALSE(result.at("asset_id").is_null());
    REQUIRE_FALSE(result.at("asset_id").get<std::string>().empty());

    const std::string stem = std::filesystem::path(src).stem().string();
    REQUIRE(std::filesystem::exists(std::filesystem::path(proj) / "assets" / (stem + ".image")));
    REQUIRE(std::filesystem::exists(std::filesystem::path(proj) / "assets" / (stem + ".toml")));

    std::filesystem::remove_all(proj);
    std::filesystem::remove(src);
}

TEST_CASE("mcp_tools: import_asset returns a null asset_id for a missing source file", "[app][mcp]")
{
    Fixture f;
    const std::string proj = (std::filesystem::temp_directory_path() /
                              (std::string("mosaic_mcp_import_missing_") + std::to_string(::getpid()))).string();
    std::filesystem::remove_all(proj);
    REQUIRE(mosaic::app::create_project(proj, f.types));

    const nlohmann::json result = f.call("import_asset", {{"source", "/no/such/file.ppm"}}, proj);
    REQUIRE(result.at("asset_id").is_null());

    std::filesystem::remove_all(proj);
}

TEST_CASE("mcp_tools: import_asset throws McpToolError for malformed arguments", "[app][mcp]")
{
    Fixture f;
    REQUIRE_THROWS_AS(f.call("import_asset", nlohmann::json::object(), std::string("/tmp")), McpToolError);
}

TEST_CASE("mcp_tools: import_asset refuses to overwrite on a stem collision", "[app][mcp]")
{
    Fixture f;
    const std::string proj = (std::filesystem::temp_directory_path() /
                              (std::string("mosaic_mcp_import_collide_") + std::to_string(::getpid()))).string();
    std::filesystem::remove_all(proj);
    REQUIRE(mosaic::app::create_project(proj, f.types));

    const std::string dir_a = (std::filesystem::temp_directory_path() /
                               (std::string("mosaic_mcp_import_cola_") + std::to_string(::getpid()))).string();
    const std::string dir_b = (std::filesystem::temp_directory_path() /
                               (std::string("mosaic_mcp_import_colb_") + std::to_string(::getpid()))).string();
    std::filesystem::remove_all(dir_a);
    std::filesystem::remove_all(dir_b);
    std::filesystem::create_directories(dir_a);
    std::filesystem::create_directories(dir_b);

    std::string ppm = "P6\n2 1\n255\n";
    const unsigned char px[] = {255, 0, 0, 0, 255, 0};
    ppm.append(reinterpret_cast<const char*>(px), sizeof(px));
    const std::string src_a = (std::filesystem::path(dir_a) / "tex.ppm").string();
    const std::string src_b = (std::filesystem::path(dir_b) / "tex.ppm").string();
    REQUIRE(mosaic::app::save_scene_file(src_a, ppm));
    REQUIRE(mosaic::app::save_scene_file(src_b, ppm));

    REQUIRE_FALSE(f.call("import_asset", {{"source", src_a}}, proj).at("asset_id").is_null());
    const std::filesystem::path meta = std::filesystem::path(proj) / "assets" / "tex.toml";
    const std::optional<std::string> before = mosaic::app::load_scene_file(meta.string());
    REQUIRE(before.has_value());

    REQUIRE(f.call("import_asset", {{"source", src_b}}, proj).at("asset_id").is_null());
    const std::optional<std::string> after = mosaic::app::load_scene_file(meta.string());
    REQUIRE(after.has_value());
    REQUIRE(*after == *before);

    std::filesystem::remove_all(proj);
    std::filesystem::remove_all(dir_a);
    std::filesystem::remove_all(dir_b);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build -j"$(nproc)"`
Expected: FAIL to build — `import_asset` isn't a registered MCP tool yet, and `handle_tool_call`'s 8th parameter doesn't exist.

- [ ] **Step 3: Update `mcp_tools.hpp`**

Change the `tools_list_schema()` doc comment:

```cpp
// The MCP `tools/list` response body: { "tools": [ { name, description, inputSchema }, ... ] }
// for all 18 v1 tools (entity/component CRUD + undo/redo/save_scene + describe_scene +
// set_script + import_asset).
nlohmann::json tools_list_schema();
```

Change `handle_tool_call`'s declaration to add the second trailing defaulted parameter:

```cpp
// Dispatches one tool call by name. `scene_path` is the manifest's default_scene path,
// used only by `save_scene`. `script_factory` is used only by `set_script`.
// `project_dir` is used only by `import_asset` (all three unused by every other tool,
// threaded through uniformly -- the same "every closure takes everything it might
// need" shape world_globals.cpp uses for world_ptr/types_ptr/stack_ptr). Both
// `script_factory` and `project_dir` default to empty so every existing call site
// keeps compiling unchanged; the affected tool reports a graceful failure (not a
// crash) if the relevant one wasn't actually supplied. Throws McpToolError for an
// unknown tool name or malformed arguments; otherwise never throws.
nlohmann::json handle_tool_call(mosaic::engine::World& world,
                                const mosaic::engine::reflection::ComponentReflection& types,
                                mosaic::foundation::commands::CommandStack& stack,
                                const std::string& scene_path, const std::string& tool_name,
                                const nlohmann::json& arguments,
                                const ScriptEngineFactory& script_factory = ScriptEngineFactory{},
                                const std::string& project_dir = std::string{});
```

- [ ] **Step 4: Update `mcp_tools.cpp`**

Add the include, alongside the existing ones:

```cpp
#include "app/asset/asset_io.hpp"
```

Add the new schema helper, right after `schema_entity_source()`:

```cpp
nlohmann::json schema_source()
{
    return {{"type", "object"}, {"properties", {{"source", {{"type", "string"}}}}}, {"required", {"source"}}};
}
```

Add `tool_import_asset`, placed after `tool_set_script` and before `tool_handlers()`. Note it, like `tool_set_script`, does NOT go into the `tool_handlers()` map:

```cpp
nlohmann::json tool_import_asset(const std::string& project_dir, const nlohmann::json& args)
{
    const std::string source = require_string(args, "source");
    const auto asset_id = mosaic::app::import_asset(project_dir, source);
    return {{"asset_id", asset_id ? nlohmann::json(asset_id->to_string()) : nlohmann::json(nullptr)}};
}
```

Add the `tools_list_schema()` entry, after the `set_script` entry:

```cpp
    tools.push_back({{"name", "import_asset"},
                     {"description", "Import a file into the project's asset pipeline (images, "
                                     ".py scripts, .wav audio, .ttf/.otf fonts, or a project's "
                                     "MOSAIC_IMPORTERS plugin extensions), cooked and registered "
                                     "exactly like `mosaic import`. asset_id is null on failure "
                                     "(unsupported format, missing source, or a stem collision) -- "
                                     "the specific reason is only visible in the server's own log "
                                     "output, not returned here."},
                     {"inputSchema", schema_source()}});
```

Change `handle_tool_call`'s definition to add the second special-case and the new parameter:

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
    const auto& handlers = tool_handlers();
    const auto it = handlers.find(tool_name);
    if (it == handlers.end())
    {
        throw McpToolError("unknown tool: " + tool_name);
    }
    return it->second(world, types, stack, scene_path, arguments);
}
```

- [ ] **Step 5: Update `app/run/mcp.cpp`**

`project_dir` is already a local variable in `run_mcp` (`const std::string& project_dir = *options.mcp_project;`). Replace the dispatch closure:

```cpp
    const mcp::ToolDispatch dispatch =
        [&](const std::string& tool_name, const nlohmann::json& arguments) -> nlohmann::json
    {
        return mcp::handle_tool_call(world, types, stack, scene_path, tool_name, arguments, script_factory);
    };
```

with:

```cpp
    const mcp::ToolDispatch dispatch =
        [&](const std::string& tool_name, const nlohmann::json& arguments) -> nlohmann::json
    {
        return mcp::handle_tool_call(world, types, stack, scene_path, tool_name, arguments,
                                     script_factory, project_dir);
    };
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build build -j"$(nproc)" --target mosaic_unit_tests && ./build/tests/mosaic_unit_tests "[mcp]"`
Expected: PASS — all 4 new `import_asset` cases, plus every pre-existing `[mcp]` case with the updated 18-tool counts. No failures.

- [ ] **Step 7: Commit**

```bash
git add app/mcp/mcp_tools.hpp app/mcp/mcp_tools.cpp app/run/mcp.cpp \
       tests/unit/app/mcp_tools_test.cpp tests/unit/app/mcp_protocol_test.cpp
git commit -m "$(cat <<'EOF'
feat(mcp): add import_asset, an 18th Agent API tool

Wraps app/asset/asset_io.hpp::import_asset() as-is -- the exact same
cook-and-register pipeline `mosaic import` already uses. asset_id is
null on any failure (unsupported format, missing source, stem
collision); the specific reason stays server-log-only, matching the
CLI's own existing UX. Phase 6 sub-project 4 of 7 (asset generation)
-- procedural asset generation and AssetID referencing via
set_component remain future expansion, documented but not built here.

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
- Consumes: `import_asset` via the real `mosaic mcp` CLI (Task 1).
- Produces: nothing new; verification + documentation only.

- [ ] **Step 1: Manually verify against a real project**

Run:

```bash
cmake --build build -j"$(nproc)"
rm -rf /tmp/import_asset_verify && ./build/app/mosaic create /tmp/import_asset_verify
printf 'P6\n2 1\n255\n' > /tmp/import_asset_verify_src.ppm
printf '\xff\x00\x00\x00\xff\x00' >> /tmp/import_asset_verify_src.ppm
printf '%s\n%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"import_asset","arguments":{"source":"/tmp/import_asset_verify_src.ppm"}}}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"import_asset","arguments":{"source":"/no/such/file.ppm"}}}' \
  | ./build/app/mosaic mcp /tmp/import_asset_verify
ls /tmp/import_asset_verify/assets/
```

Expected: response 1 shows a non-null `asset_id`; response 2 shows `{"asset_id":null}`; `ls` shows `import_asset_verify_src.image` and `import_asset_verify_src.toml` written to `assets/`. Inspect by eye — no crash.

- [ ] **Step 2: Run the full test suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: green aside from the two known pre-existing flakes. Zero new failures.

- [ ] **Step 3: Add the dated `next-steps.md` entry**

Prepend a new dated paragraph to the top of the changelog blob in `docs/project/next-steps.md` (immediately before the current most-recent `**Last updated:**` entry, matching the existing style), summarizing: `import_asset` ships as Phase 6 sub-project 4 of 7 (asset generation); an 18th MCP tool wrapping the existing `import_asset()` pipeline as-is; no inline failure reason (server-log-only, matching CLI UX); procedural asset generation and `AssetID` referencing via `set_component` are recorded as valuable future upgrades, not built here; next Phase 6 sub-project is automated testing (5 of 7). Link this plan and the spec.

- [ ] **Step 4: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "$(cat <<'EOF'
docs: record import_asset's next-steps.md entry

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01CEBXDEjzbGDpMyLrKCN4mY
EOF
)"
```

---

## Self-Review Notes

- **Spec coverage:** thin wrapper over `import_asset()` with no new asset logic (Task 1) ✓; `{"asset_id": null}` graceful failure, no inline reason (Task 1's 2nd/4th tests) ✓; second special-cased dispatch mirroring `set_script`'s precedent (Task 1 Step 4) ✓; trailing defaulted `project_dir`, zero disruption to existing call sites (Task 1, verified only the two count assertions plus the new tests change) ✓; zero CMakeLists changes (confirmed: no `target_link_libraries`/`add_library` edits anywhere) ✓; manual verification + next-steps.md (Task 2) ✓.
- **Placeholder scan:** no TBD/TODO; every step has real code or an exact command.
- **Type consistency:** `handle_tool_call`'s 8-parameter signature defined once in `mcp_tools.hpp` (Task 1 Step 3), used identically in `mcp_tools.cpp` (Step 4), `app/run/mcp.cpp` (Step 5), and the test file's new `Fixture::call` overload (Step 1) — same order, same defaults, everywhere.
