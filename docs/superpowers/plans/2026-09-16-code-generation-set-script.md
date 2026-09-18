# Code Generation — `set_script` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a 17th MCP tool, `set_script`, to the existing `mosaic mcp` server: test-compile a Python script against the live `World` (reusing `studio::ScriptEditorModel::apply()` as-is) and write it into an entity's existing `ScriptComponent` only on success.

**Architecture:** `handle_tool_call` gains a new trailing, defaulted `ScriptEngineFactory` parameter and special-cases `"set_script"` before consulting the generic `tool_handlers()` dispatch table (it's the only tool needing an injected script-engine factory, which a plain function pointer can't capture). `app/run/mcp.cpp` constructs the real factory identically to how `mosaic editor` wires its own Script Editor.

**Tech Stack:** C++20, `nlohmann_json`, the existing embedded-Python `ScriptEngine`/`ScriptEditorModel` machinery (already a build dependency). No new CMakeLists changes anywhere — `studio/script_editor.hpp` is header-only and everything it needs is already linked into `mosaic_app`.

**Spec:** `docs/superpowers/specs/2026-09-16-code-generation-set-script-design.md`

## Global Constraints

- `set_script` never attaches a `ScriptComponent` itself — an agent calls the existing generic `add_component(e, "ScriptComponent")` first. A missing component is a graceful `{"ok": false, ...}`, not an error.
- A compile error is a graceful `{"ok": false, "message": "..."}` — never `McpToolError`. `McpToolError` is reserved for malformed request parameters (`entity`/`source` missing or mistyped) and stays consistent with every other tool.
- `handle_tool_call`'s new `script_factory` parameter is trailing and defaulted (`= ScriptEngineFactory{}`) — every existing call site (every current test, `Fixture::call`) must keep compiling with zero changes.
- The write is not undoable via `undo`/`redo` — inherited from `ScriptEditorModel::apply()` itself, not solved here.
- Uses the **unrestricted** `create_python_script_engine()` factory (gameplay scripting), not the sandboxed Studio-plugin one.

---

### Task 1: `set_script` tool

**Files:**
- Modify: `app/mcp/mcp_tools.hpp` — `ScriptEngineFactory` alias, `handle_tool_call`'s new defaulted parameter, doc-comment tool count 16 → 17.
- Modify: `app/mcp/mcp_tools.cpp` — new include, new `tool_set_script`, the `handle_tool_call` special-case, a new `tools_list_schema()` entry, and a new `schema_entity_source()` helper.
- Modify: `app/run/mcp.cpp` — construct the real factory and pass it into the dispatch closure.
- Test: `tests/unit/app/mcp_tools_test.cpp` (extended) — new test cases plus the existing tool-count assertion (16 → 17).
- Test: `tests/unit/app/mcp_protocol_test.cpp` (extended) — the existing tool-count assertion (16 → 17).

**Interfaces:**
- Consumes: `mosaic::studio::ScriptEditorModel`/`ScriptApplyResult` (`studio/script_editor.hpp`, pre-existing, header-only), `mosaic::script::python::create_python_script_engine()` (`modules/script_python/python_script_engine.hpp`, pre-existing).
- Produces: `using mosaic::app::mcp::ScriptEngineFactory = std::function<std::unique_ptr<mosaic::engine::script::ScriptEngine>()>` and `handle_tool_call`'s new 7th parameter, both declared in `app/mcp/mcp_tools.hpp`. Consumed by `app/run/mcp.cpp` (this task) and by the test file's `Fixture::call` overload (this task).

- [ ] **Step 1: Write the failing tests**

First, extend `Fixture` in `tests/unit/app/mcp_tools_test.cpp` with an overload of `call` that accepts a script factory (the existing zero-factory overload used by all current tests keeps working unchanged since the new parameter defaults):

```cpp
    nlohmann::json call(const std::string& tool, const nlohmann::json& args,
                        const mosaic::app::mcp::ScriptEngineFactory& script_factory)
    {
        return handle_tool_call(world, types, stack, "", tool, args, script_factory);
    }
```

Add this as a second overload of `call` inside the existing `Fixture` struct (alongside the current `call(tool, args = nlohmann::json::object())` — do not remove or change that one).

Add these includes to the top of the file (alongside the existing ones):

```cpp
#include "modules/script_python/python_script_engine.hpp" // ensure_interpreter, create_python_script_engine
```

Add this using-declaration alongside the file's existing ones:

```cpp
using mosaic::app::mcp::ScriptEngineFactory;
```

Append these test cases:

```cpp
TEST_CASE("mcp_tools: set_script test-compiles and writes a valid script into an existing ScriptComponent", "[app][mcp]")
{
    mosaic::script::python::ensure_interpreter();
    Fixture f;
    const std::uint64_t eid = f.call("create_entity").at("id").get<std::uint64_t>();
    f.call("add_component", {{"entity", eid}, {"component", "ScriptComponent"}});

    const ScriptEngineFactory factory = [] { return mosaic::script::python::create_python_script_engine(); };
    const nlohmann::json result =
        f.call("set_script", {{"entity", eid}, {"source", "def on_update(dt):\n    pass\n"}}, factory);
    REQUIRE(result.at("ok").get<bool>() == true);

    const nlohmann::json component = f.call("get_component", {{"entity", eid}, {"component", "ScriptComponent"}});
    REQUIRE(component.at("value").at("source").get<std::string>() == "def on_update(dt):\n    pass\n");
}

TEST_CASE("mcp_tools: set_script leaves the component unchanged on a compile error", "[app][mcp]")
{
    mosaic::script::python::ensure_interpreter();
    Fixture f;
    const std::uint64_t eid = f.call("create_entity").at("id").get<std::uint64_t>();
    f.call("add_component", {{"entity", eid}, {"component", "ScriptComponent"}});

    const ScriptEngineFactory factory = [] { return mosaic::script::python::create_python_script_engine(); };
    const nlohmann::json result = f.call("set_script", {{"entity", eid}, {"source", "def on_update(dt:\n"}}, factory);
    REQUIRE(result.at("ok").get<bool>() == false);
    REQUIRE_FALSE(result.at("message").get<std::string>().empty());

    const nlohmann::json component = f.call("get_component", {{"entity", eid}, {"component", "ScriptComponent"}});
    REQUIRE(component.at("value").at("source").get<std::string>().empty());
}

TEST_CASE("mcp_tools: set_script reports failure for an entity with no ScriptComponent", "[app][mcp]")
{
    mosaic::script::python::ensure_interpreter();
    Fixture f;
    const std::uint64_t eid = f.call("create_entity").at("id").get<std::uint64_t>();

    const ScriptEngineFactory factory = [] { return mosaic::script::python::create_python_script_engine(); };
    const nlohmann::json result = f.call("set_script", {{"entity", eid}, {"source", "pass"}}, factory);
    REQUIRE(result.at("ok").get<bool>() == false);
    REQUIRE(result.at("message").get<std::string>().find("ScriptComponent") != std::string::npos);
}

TEST_CASE("mcp_tools: set_script throws McpToolError for malformed arguments", "[app][mcp]")
{
    Fixture f;
    REQUIRE_THROWS_AS(f.call("set_script", {{"source", "pass"}}), McpToolError);
    REQUIRE_THROWS_AS(f.call("set_script", {{"entity", 1}}), McpToolError);
}
```

Update the existing `tools_list_schema` count test — change both occurrences of `16` to `17`:

```cpp
TEST_CASE("mcp_tools: tools_list_schema lists all 17 v1 tools with a description and inputSchema", "[app][mcp]")
{
    const nlohmann::json schema = tools_list_schema();
    REQUIRE(schema.contains("tools"));
    const nlohmann::json tools = schema.at("tools");
    REQUIRE(tools.size() == 17);
```
(leave the rest of that test case's body unchanged — only the title string and the `17`s change).

In `tests/unit/app/mcp_protocol_test.cpp`, update the one existing count assertion:

```cpp
    REQUIRE(parsed.at("result").at("tools").size() == 17);
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build -j"$(nproc)"`
Expected: FAIL to build — `ScriptEngineFactory` and `handle_tool_call`'s 7th parameter don't exist yet.

- [ ] **Step 3: Update `mcp_tools.hpp`**

Add includes:

```cpp
#include <functional>
#include <memory>
```

alongside the existing `#include <stdexcept>` etc. (check first — some may already be present via other includes; add whichever of `<functional>`/`<memory>` isn't already there).

Add this include:

```cpp
#include "engine/script/script_engine.hpp"
```

Add the type alias, right after the `McpToolError` struct definition:

```cpp
// Constructs a fresh, unbound ScriptEngine for set_script's throwaway compile-check --
// the exact same factory shape mosaic editor injects into its own Script Editor
// (app/run/editor.cpp), just threaded through the MCP dispatch instead of Studio's UI.
using ScriptEngineFactory = std::function<std::unique_ptr<mosaic::engine::script::ScriptEngine>()>;
```

Change the `tools_list_schema()` doc comment:

```cpp
// The MCP `tools/list` response body: { "tools": [ { name, description, inputSchema }, ... ] }
// for all 17 v1 tools (entity/component CRUD + undo/redo/save_scene + describe_scene + set_script).
nlohmann::json tools_list_schema();
```

Change `handle_tool_call`'s declaration to add the trailing defaulted parameter:

```cpp
// Dispatches one tool call by name. `scene_path` is the manifest's default_scene path,
// used only by `save_scene`. `script_factory` is used only by `set_script` (both unused
// by every other tool, threaded through uniformly -- the same "every closure takes
// everything it might need" shape world_globals.cpp uses for
// world_ptr/types_ptr/stack_ptr). Defaulted to an empty factory so every existing call
// site keeps compiling unchanged; `set_script` reports a graceful failure (not a crash)
// if no factory was actually supplied at the call site. Throws McpToolError for an
// unknown tool name or malformed arguments; otherwise never throws.
nlohmann::json handle_tool_call(mosaic::engine::World& world,
                                const mosaic::engine::reflection::ComponentReflection& types,
                                mosaic::foundation::commands::CommandStack& stack,
                                const std::string& scene_path, const std::string& tool_name,
                                const nlohmann::json& arguments,
                                const ScriptEngineFactory& script_factory = ScriptEngineFactory{});
```

- [ ] **Step 4: Update `mcp_tools.cpp`**

Add the include, alongside the existing engine includes:

```cpp
#include "studio/script_editor.hpp"
```

Add the new schema helper, right after `schema_component()`:

```cpp
nlohmann::json schema_entity_source()
{
    return {{"type", "object"},
           {"properties",
            {{"entity", {{"type", "integer"}, {"minimum", 0}}}, {"source", {{"type", "string"}}}}},
           {"required", {"entity", "source"}}};
}
```

Add `tool_set_script`, placed after `tool_describe_scene` and before `tool_handlers()`. Note it takes a **different** signature than the uniform `ToolHandler` shape (no `CommandStack&`, no `scene_path`, an extra `ScriptEngineFactory&`) — it is deliberately NOT added to the `tool_handlers()` map:

```cpp
nlohmann::json tool_set_script(World& world, const ComponentReflection& types,
                               const nlohmann::json& args, const ScriptEngineFactory& script_factory)
{
    const EntityID e(require_uint64(args, "entity"));
    const std::string source = require_string(args, "source");
    mosaic::studio::ScriptEditorModel model(script_factory);
    const mosaic::studio::ScriptApplyResult result = model.apply(source, world, types, e);
    return {{"ok", result.kind == mosaic::studio::ScriptApplyResult::Kind::Ok}, {"message", result.message}};
}
```

Add the `tools_list_schema()` entry, after the `describe_scene` entry:

```cpp
    tools.push_back({{"name", "set_script"},
                     {"description", "Test-compile a Python script against the live World and, on "
                                     "success, write it into the entity's existing ScriptComponent. "
                                     "Attach a ScriptComponent first via add_component. ok is false "
                                     "(not an error) on a compile failure or a missing component -- "
                                     "see message for details."},
                     {"inputSchema", schema_entity_source()}});
```

Change `handle_tool_call`'s definition to special-case `set_script` before the generic dispatch, and accept the new trailing parameter:

```cpp
nlohmann::json handle_tool_call(World& world, const ComponentReflection& types, CommandStack& stack,
                                const std::string& scene_path, const std::string& tool_name,
                                const nlohmann::json& arguments, const ScriptEngineFactory& script_factory)
{
    if (tool_name == "set_script")
    {
        return tool_set_script(world, types, arguments, script_factory);
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

Add the include:

```cpp
#include "modules/script_python/python_script_engine.hpp"
```

Construct the real factory and pass it into the dispatch closure — replace:

```cpp
    const mcp::ToolDispatch dispatch =
        [&](const std::string& tool_name, const nlohmann::json& arguments) -> nlohmann::json
    { return mcp::handle_tool_call(world, types, stack, scene_path, tool_name, arguments); };
```

with:

```cpp
    const mcp::ScriptEngineFactory script_factory =
        [] { return mosaic::script::python::create_python_script_engine(); };

    const mcp::ToolDispatch dispatch =
        [&](const std::string& tool_name, const nlohmann::json& arguments) -> nlohmann::json
    {
        return mcp::handle_tool_call(world, types, stack, scene_path, tool_name, arguments, script_factory);
    };
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build build -j"$(nproc)" --target mosaic_unit_tests && ./build/tests/mosaic_unit_tests "[mcp]"`
Expected: PASS — all 4 new `set_script` cases, plus every pre-existing `[mcp]` case with the updated 17-tool counts (31 total: 14 CRUD-and-scene-reasoning tools' tests + 11 protocol + 4 new `set_script` — exact case count isn't load-bearing, "no failures" is what matters).

- [ ] **Step 7: Commit**

```bash
git add app/mcp/mcp_tools.hpp app/mcp/mcp_tools.cpp app/run/mcp.cpp \
       tests/unit/app/mcp_tools_test.cpp tests/unit/app/mcp_protocol_test.cpp
git commit -m "$(cat <<'EOF'
feat(mcp): add set_script, a 17th Agent API tool

Reuses studio::ScriptEditorModel::apply() as-is: test-compiles a
Python script against the live World and writes it into an entity's
existing ScriptComponent only on success. A compile error or a
missing ScriptComponent is a graceful {"ok": false, ...}, never a
protocol error. Phase 6 sub-project 3 of 7 (code generation) -- Studio
plugin (.py file) authoring remains future expansion, documented but
not built here.

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
- Consumes: `set_script` via the real `mosaic mcp` CLI (Task 1).
- Produces: nothing new; verification + documentation only.

- [ ] **Step 1: Manually verify against a real project**

Run:

```bash
cmake --build build -j"$(nproc)"
rm -rf /tmp/set_script_verify && ./build/app/mosaic create /tmp/set_script_verify
printf '%s\n%s\n%s\n%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"create_entity","arguments":{}}}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"add_component","arguments":{"entity":4294967309,"component":"ScriptComponent"}}}' \
  '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"set_script","arguments":{"entity":4294967309,"source":"def on_update(dt):\n    pass\n"}}}' \
  '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"set_script","arguments":{"entity":4294967309,"source":"def on_update(dt:\n"}}}' \
  | ./build/app/mosaic mcp /tmp/set_script_verify
```

The `create_entity` id in the following lines assumes the starter template's next fresh id — inspect response `id:1`'s output first and substitute the actual returned id into the remaining three lines if it differs (the starter template's entity count can change; don't assume `4294967309` is stable).

Expected: response 3 shows `"ok":true`; response 4 shows `"ok":false` with a non-empty `message` (a Python syntax error). Inspect by eye — no crash.

- [ ] **Step 2: Run the full test suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: green aside from the two known pre-existing flakes. Zero new failures.

- [ ] **Step 3: Add the dated `next-steps.md` entry**

Prepend a new dated paragraph to the top of the changelog blob in `docs/project/next-steps.md` (immediately before the current most-recent `**Last updated:**` entry, matching the existing style), summarizing: `set_script` ships as Phase 6 sub-project 3 of 7 (code generation); a 17th MCP tool reusing `ScriptEditorModel::apply()` as-is to test-compile and write a `ScriptComponent`'s source; Studio plugin (`.py` file) authoring is recorded as a valuable future upgrade, not built here; next Phase 6 sub-project is asset generation (4 of 7). Link this plan and the spec.

- [ ] **Step 4: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "$(cat <<'EOF'
docs: record set_script's next-steps.md entry

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01CEBXDEjzbGDpMyLrKCN4mY
EOF
)"
```

---

## Self-Review Notes

- **Spec coverage:** reuses `ScriptEditorModel::apply()` as-is (Task 1) ✓; graceful `{"ok": false, ...}` for compile error / missing component, `McpToolError` only for malformed params (Task 1's 4 new tests) ✓; trailing defaulted `script_factory` parameter, zero disruption to existing call sites (Task 1, verified no other test needed changes beyond the two count assertions) ✓; unrestricted factory matching `mosaic editor`'s own wiring (Task 1 Step 5) ✓; zero CMakeLists changes (confirmed: no `target_link_libraries`/`add_library` edits anywhere in this plan) ✓; manual verification + next-steps.md (Task 2) ✓.
- **Placeholder scan:** no TBD/TODO; every step has real code or an exact command.
- **Type consistency:** `ScriptEngineFactory` defined once in `mcp_tools.hpp` (Task 1 Step 3), used identically in `mcp_tools.cpp` (Step 4), `app/run/mcp.cpp` (Step 5), and the test file's `Fixture::call` overload and new test cases (Step 1) — same alias, same signature everywhere.
