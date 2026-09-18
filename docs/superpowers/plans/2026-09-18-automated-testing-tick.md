# Automated Testing — `tick` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire `setup_runtime`'s full system set (`ScriptSystem`, `AnimationSystem`, `SequenceSystem`, `ParticleSystem`, `TrailSystem`, `AudioSystem`, `PhysicsSystem`) into `mosaic mcp` and add a 19th MCP tool, `tick`, that actually advances simulation time — the first MCP tool to call `World::tick()` at all, closing the "write a script → run it → check what happened" loop.

**Architecture:** `app/run/mcp.cpp` gains an `AssetRegistry` + `load_project_assets` + `setup_runtime` call at startup (mirroring `mosaic editor`'s own sequence, reusing the same `script_factory` already built for `set_script`). `tick` fits the existing uniform `ToolHandler` shape as-is — added directly to `tool_handlers()`, no special-casing needed (unlike `set_script`/`import_asset`).

**Tech Stack:** C++20, `nlohmann_json`, the existing `setup_runtime`/`World::tick` machinery (already a build dependency, already linked into `mosaic_app` via `run/editor.cpp`). No new CMakeLists changes.

**Spec:** `docs/superpowers/specs/2026-09-18-automated-testing-tick-design.md`

## Global Constraints

- `count` (default 1) is capped at **10,000 per call** — exceeding it is `McpToolError`, never a silent clamp.
- `dt` defaults to `1.0/60.0`.
- Adding systems at startup must not change behavior of any of the 18 existing tools — they never call `World::tick()`, so the added systems stay inert until `tick` is actually called.
- The ~30 existing tests using `Fixture` must not pay for `setup_runtime`'s cost (a real Box3D `PhysicsSystem`, etc.) — a new, separate `RuntimeFixture` is used only by `tick`'s own tests.
- No new CMakeLists changes — `demo_scene.hpp`/`AssetRegistry` are already linked into `mosaic_app`.

---

### Task 1: `tick` tool + `setup_runtime` wiring

**Files:**
- Modify: `app/run/mcp.cpp` — new `AssetRegistry`, `load_project_assets`, `setup_runtime` call.
- Modify: `app/mcp/mcp_tools.hpp` — doc-comment tool count 18 → 19.
- Modify: `app/mcp/mcp_tools.cpp` — new `optional_uint64`/`optional_double` helpers, `schema_tick`, `tool_tick`, a `tools_list_schema()` entry, a `tool_handlers()` entry.
- Test: `tests/unit/app/mcp_tools_test.cpp` (extended) — new `RuntimeFixture`, new test cases, existing tool-count assertion (18 → 19).
- Test: `tests/unit/app/mcp_protocol_test.cpp` (extended) — existing tool-count assertion (18 → 19).

**Interfaces:**
- Consumes: `mosaic::app::setup_runtime(World&, AssetRegistry&, const ComponentReflection&, ScriptSystem::EngineFactory, ...)` (`app/run/demo_scene.hpp`, pre-existing), `mosaic::app::load_project_assets(AssetRegistry&, const std::string&)` (`app/asset/asset_io.hpp`, pre-existing), `mosaic::engine::World::tick(float dt)` (`engine/world/world.hpp`, pre-existing).
- Produces: a new `"tick"` entry in `tool_handlers()`. No new public function signatures — `tick` uses the existing `ToolHandler` shape as-is.

- [ ] **Step 1: Write the failing tests**

Add these includes to `tests/unit/app/mcp_tools_test.cpp` (alongside the existing ones):

```cpp
#include "app/run/demo_scene.hpp"
#include "engine/assets/asset_registry.hpp"
```

Add a new `RuntimeFixture` struct, right after the existing `Fixture` struct (in the same anonymous namespace, do not modify `Fixture` itself):

```cpp
struct RuntimeFixture
{
    TypeRegistry        registry;
    ComponentReflection types;
    World                world;
    CommandStack          stack;
    mosaic::engine::assets::AssetRegistry asset_registry;
    ScriptEngineFactory   script_factory;

    RuntimeFixture()
        : script_factory([] { return mosaic::script::python::create_python_script_engine(); })
    {
        register_builtin_components(registry, types);
        mosaic::app::setup_runtime(world, asset_registry, types, script_factory);
    }

    nlohmann::json call(const std::string& tool, const nlohmann::json& args = nlohmann::json::object())
    {
        return handle_tool_call(world, types, stack, "", tool, args, script_factory);
    }
};
```

Update the existing `tools_list_schema` count test — both occurrences of `18` become `19`:

```cpp
TEST_CASE("mcp_tools: tools_list_schema lists all 19 v1 tools with a description and inputSchema", "[app][mcp]")
{
    const nlohmann::json schema = tools_list_schema();
    REQUIRE(schema.contains("tools"));
    const nlohmann::json tools = schema.at("tools");
    REQUIRE(tools.size() == 19);
```
(leave the rest of that test case's body unchanged).

In `tests/unit/app/mcp_protocol_test.cpp`, update the one existing count assertion:

```cpp
    REQUIRE(parsed.at("result").at("tools").size() == 19);
```

Append these test cases to `tests/unit/app/mcp_tools_test.cpp`:

```cpp
TEST_CASE("mcp_tools: tick advances a script's state across multiple ticks", "[app][mcp]")
{
    mosaic::script::python::ensure_interpreter();
    RuntimeFixture f;
    const std::uint64_t eid = f.call("create_entity").at("id").get<std::uint64_t>();
    f.call("add_component", {{"entity", eid}, {"component", "Transform"}});
    f.call("add_component", {{"entity", eid}, {"component", "ScriptComponent"}});

    const nlohmann::json set_result = f.call(
        "set_script",
        {{"entity", eid},
         {"source", "def on_update(dt):\n"
                    "    t = get_component(entity, \"Transform\")\n"
                    "    t[\"position\"][\"x\"] = t[\"position\"][\"x\"] + 1.0\n"
                    "    set_component(entity, \"Transform\", t)\n"}});
    REQUIRE(set_result.at("ok").get<bool>() == true);

    const nlohmann::json tick_result = f.call("tick", {{"count", 5}});
    REQUIRE(tick_result.at("ticks").get<std::uint64_t>() == 5);

    const nlohmann::json component = f.call("get_component", {{"entity", eid}, {"component", "Transform"}});
    REQUIRE(component.at("value").at("position").at("x").get<double>() == 5.0);
}

TEST_CASE("mcp_tools: tick with no arguments runs a single tick at the standard timestep", "[app][mcp]")
{
    RuntimeFixture f;
    const nlohmann::json result = f.call("tick");
    REQUIRE(result.at("ticks").get<std::uint64_t>() == 1);
}

TEST_CASE("mcp_tools: tick throws McpToolError when count exceeds the per-call limit", "[app][mcp]")
{
    RuntimeFixture f;
    REQUIRE_THROWS_AS(f.call("tick", {{"count", 10001}}), McpToolError);
}

TEST_CASE("mcp_tools: tick throws McpToolError for malformed count or dt", "[app][mcp]")
{
    RuntimeFixture f;
    REQUIRE_THROWS_AS(f.call("tick", {{"count", "not-a-number"}}), McpToolError);
    REQUIRE_THROWS_AS(f.call("tick", {{"dt", "not-a-number"}}), McpToolError);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build -j"$(nproc)"`
Expected: FAIL to build — `tick` isn't a registered MCP tool yet, and `RuntimeFixture`'s `mosaic::app::setup_runtime` call needs no new symbol but the test file itself won't build until `RuntimeFixture` compiles against real code paths exercised below.

- [ ] **Step 3: Update `mcp_tools.hpp`**

Change the `tools_list_schema()` doc comment:

```cpp
// The MCP `tools/list` response body: { "tools": [ { name, description, inputSchema }, ... ] }
// for all 19 v1 tools (entity/component CRUD + undo/redo/save_scene + describe_scene +
// set_script + import_asset + tick).
nlohmann::json tools_list_schema();
```

- [ ] **Step 4: Update `mcp_tools.cpp`**

Add the new argument helpers, right after `require_string`:

```cpp
std::uint64_t optional_uint64(const nlohmann::json& args, const char* key, std::uint64_t default_value)
{
    if (!args.contains(key)) { return default_value; }
    return require_uint64(args, key);
}

double optional_double(const nlohmann::json& args, const char* key, double default_value)
{
    if (!args.contains(key)) { return default_value; }
    if (!args.at(key).is_number())
    {
        throw McpToolError(std::string("invalid '") + key + "' (expected a number)");
    }
    return args.at(key).get<double>();
}
```

Add `tool_tick`, placed after `tool_describe_scene` and before `tool_handlers()` (note: unlike `tool_set_script`/`tool_import_asset`, this one matches the uniform `ToolHandler` signature exactly, so it DOES go into the `tool_handlers()` map):

```cpp
constexpr std::uint64_t kMaxTicksPerCall = 10000;

nlohmann::json tool_tick(World& world, const ComponentReflection&, CommandStack&,
                         const std::string&, const nlohmann::json& args)
{
    const std::uint64_t count = optional_uint64(args, "count", 1);
    const double dt = optional_double(args, "dt", 1.0 / 60.0);
    if (count > kMaxTicksPerCall)
    {
        throw McpToolError("count exceeds the per-call limit of " + std::to_string(kMaxTicksPerCall));
    }
    for (std::uint64_t i = 0; i < count; ++i)
    {
        world.tick(static_cast<float>(dt));
    }
    return {{"ticks", count}};
}
```

Add `"tick"` to `tool_handlers()`'s initializer list:

```cpp
        {"describe_scene", &tool_describe_scene},
        {"tick", &tool_tick},
```

Add the new schema helper, right after `schema_source()`:

```cpp
nlohmann::json schema_tick()
{
    return {{"type", "object"},
           {"properties",
            {{"count", {{"type", "integer"}, {"minimum", 0}}}, {"dt", {{"type", "number"}}}}},
           {"required", nlohmann::json::array()}};
}
```

Add the `tools_list_schema()` entry, after the `import_asset` entry:

```cpp
    tools.push_back({{"name", "tick"},
                     {"description", "Advance the simulation by `count` ticks (default 1, max "
                                     "10000 per call) of `dt` seconds each (default 1/60). Runs "
                                     "ScriptSystem, PhysicsSystem, and every other system "
                                     "mosaic run/editor use at play time. Returns the number of "
                                     "ticks actually run."},
                     {"inputSchema", schema_tick()}});
```

`handle_tool_call` itself needs no changes — `tick` reaches it through the existing generic `tool_handlers()` lookup path, same as the original 16 tools.

- [ ] **Step 5: Update `app/run/mcp.cpp`**

Add the includes:

```cpp
#include "app/asset/asset_io.hpp"
#include "app/run/demo_scene.hpp"
#include "engine/assets/asset_registry.hpp"
```

Insert, right after the existing `populate_world` block and before the `script_factory` construction:

```cpp
    mosaic::engine::assets::AssetRegistry asset_registry;
    load_project_assets(asset_registry, (std::filesystem::path(project_dir) / "assets").string());
```

Change the existing block:

```cpp
    const mcp::ScriptEngineFactory script_factory =
        [] { return mosaic::script::python::create_python_script_engine(); };

    const mcp::ToolDispatch dispatch =
```

to:

```cpp
    const mcp::ScriptEngineFactory script_factory =
        [] { return mosaic::script::python::create_python_script_engine(); };

    setup_runtime(world, asset_registry, types, script_factory);

    const mcp::ToolDispatch dispatch =
```

(`setup_runtime`'s returned `RuntimeScene` is intentionally discarded — its only field, `render_system`, is unused by the headless MCP server.)

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build build -j"$(nproc)" --target mosaic_unit_tests && ./build/tests/mosaic_unit_tests "[mcp]"`
Expected: PASS — all 4 new `tick` cases, plus every pre-existing `[mcp]` case with the updated 19-tool counts. No failures.

- [ ] **Step 7: Commit**

```bash
git add app/run/mcp.cpp app/mcp/mcp_tools.hpp app/mcp/mcp_tools.cpp \
       tests/unit/app/mcp_tools_test.cpp tests/unit/app/mcp_protocol_test.cpp
git commit -m "$(cat <<'EOF'
feat(mcp): add tick, a 19th Agent API tool

Wires setup_runtime's full system set (ScriptSystem, PhysicsSystem,
AnimationSystem, SequenceSystem, ParticleSystem, TrailSystem,
AudioSystem) into mosaic mcp at startup, reusing the same
script_factory set_script already built. tick is the first MCP tool
to actually call World::tick() -- closing the "write a script -> run
it -> check what happened" loop. count is capped at 10000/call.
Phase 6 sub-project 5 of 7 (automated testing) -- hot-reloading
imported assets into the live registry mid-session remains a real,
documented limitation, not solved here.

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
- Consumes: `tick` via the real `mosaic mcp` CLI (Task 1).
- Produces: nothing new; verification + documentation only.

- [ ] **Step 1: Manually verify against a real project**

Run:

```bash
cmake --build build -j"$(nproc)"
rm -rf /tmp/tick_verify && ./build/app/mosaic create /tmp/tick_verify
printf '%s\n%s\n%s\n%s\n%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"create_entity","arguments":{}}}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"add_component","arguments":{"entity":4294967309,"component":"Transform"}}}' \
  '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"add_component","arguments":{"entity":4294967309,"component":"ScriptComponent"}}}' \
  '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"set_script","arguments":{"entity":4294967309,"source":"def on_update(dt):\n    t = get_component(entity, \"Transform\")\n    t[\"position\"][\"x\"] = t[\"position\"][\"x\"] + 1.0\n    set_component(entity, \"Transform\", t)\n"}}}' \
  '{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"tick","arguments":{"count":3}}}' \
  | ./build/app/mosaic mcp /tmp/tick_verify
```

The `create_entity` id in requests 2-5 assumes the starter template's next fresh id — inspect response `id:1`'s output first and substitute the actual returned id into the remaining lines if it differs (don't assume `4294967309` is stable; the same caution the `set_script` sub-project's own plan already noted).

After the sequence, call `get_component` for the same entity's `Transform` and confirm `position.x == 3.0`:

```bash
printf '%s\n' \
  '{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"get_component","arguments":{"entity":4294967309,"component":"Transform"}}}' \
  | ./build/app/mosaic mcp /tmp/tick_verify
```

(Note: this is a **separate process** — the previous session's edits weren't saved, so this second invocation starts from the original unmodified scene. To verify within one continuous session, combine all 6 requests into a single `printf`/pipe into one `mosaic mcp` invocation instead of two.)

Expected: `tick_result.ticks == 3`; the combined-session `get_component` call shows `position.x == 3.0`. No crash.

- [ ] **Step 2: Run the full test suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: green aside from the two known pre-existing flakes. Zero new failures.

- [ ] **Step 3: Add the dated `next-steps.md` entry**

Prepend a new dated paragraph to the top of the changelog blob in `docs/project/next-steps.md` (immediately before the current most-recent `**Last updated:**` entry, matching the existing style), summarizing: `tick` ships as Phase 6 sub-project 5 of 7 (automated testing); a 19th MCP tool, the first to call `World::tick()`, closing the "write a script → run it → check what happened" loop; `setup_runtime`'s full system set (including `PhysicsSystem`) is now wired into every `mosaic mcp` session at startup; the asset-hot-reload limitation (imported assets don't appear in the live registry until restart) is recorded as a real, documented gap; next Phase 6 sub-project is project maintenance (6 of 7). Link this plan and the spec.

- [ ] **Step 4: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "$(cat <<'EOF'
docs: record tick's next-steps.md entry

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01CEBXDEjzbGDpMyLrKCN4mY
EOF
)"
```

---

## Self-Review Notes

- **Spec coverage:** `setup_runtime` wired at startup, reusing the existing `script_factory` (Task 1 Step 5) ✓; `tick` fits the uniform `ToolHandler` shape, no special-casing (Task 1 Step 4) ✓; `count`/`dt` optional with defaults, `count` capped at 10,000 (Task 1 Step 4, tested in Step 1) ✓; `RuntimeFixture` kept separate from `Fixture` so existing tests don't pay `setup_runtime`'s cost (Task 1 Step 1) ✓; the real "write a script, run it, check the result" loop tested end-to-end (Task 1's first new test case) ✓; asset-hot-reload limitation documented, not solved (spec's own non-goal, restated in Task 2's next-steps.md entry) ✓; manual verification + next-steps.md (Task 2) ✓.
- **Placeholder scan:** no TBD/TODO; every step has real code or an exact command.
- **Type consistency:** `tool_tick`'s signature matches the existing `ToolHandler` alias exactly (`World&, const ComponentReflection&, CommandStack&, const std::string&, const nlohmann::json&`) already defined in `mcp_tools.cpp`; `RuntimeFixture::script_factory` uses the same `ScriptEngineFactory` alias `Fixture`'s own `call` overload already uses — no new type introduced.
