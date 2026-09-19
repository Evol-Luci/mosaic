# Project Health & Bulk Editing — `broken_references` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a 23rd MCP tool, `broken_references`, to the existing `mosaic mcp` server: scans every entity's every component for `AssetID`-typed fields whose value is non-nil but doesn't resolve in the live `AssetRegistry`, returning entity/component/field/asset_id for each. This is sub-project 2 of 4 in the project-health/bulk-editing arc (see `docs/project/deferred-work-backlog.md`).

**Architecture:** Unlike every prior MCP tool, `broken_references` needs a dependency no `ToolHandler` carries: `const AssetRegistry&`. It's threaded into `handle_tool_call` as a new parameter (mirroring how `script_factory`/`project_dir` were added for `set_script`/`import_asset`/`build_project`) and special-cased in `handle_tool_call`, not added to `tool_handlers()`. The scan itself drives `ComponentReflection::inspect()` with a small local `FieldVisitor` (`AssetRefCollector`) that only implements `field_asset` — the one callback the reflection system already dedicates to `AssetID` fields.

**Tech Stack:** C++20, `nlohmann_json`, `engine/reflection/field_visitor.hpp`'s existing `FieldVisitor` interface, `engine/assets/asset_registry.hpp`'s existing `AssetRegistry::contains(AssetID)`. No new CMakeLists changes — both headers are already linked into `mosaic_app`.

**Spec:** `docs/superpowers/specs/2026-09-19-project-health-broken-references-design.md`

## Global Constraints

- Scope is `AssetID` references only — no `EntityID` reference checking (needs a separate `FieldVisitor` extension, out of scope; see spec's Scope section).
- A nil `AssetID` (`value.is_nil()` true — no reference set) is normal, never reported.
- No arguments, no component filter — the tool always scans the whole world.
- The `AssetRegistry` snapshot is populated once at `mosaic mcp` startup and never refreshed; an asset imported via `import_asset` earlier in the same session will false-positive until restart. This is an accepted, already-tracked limitation (not fixed here) — the tool's `tools_list_schema()` description must say so explicitly.
- No new CMakeLists changes.

---

### Task 1: `broken_references` tool

**Files:**
- Modify: `app/mcp/mcp_tools.hpp` — new `#include "engine/assets/asset_registry.hpp"`, doc-comment tool count 22 → 23, new `const mosaic::engine::assets::AssetRegistry&` parameter on `handle_tool_call` (defaulted).
- Modify: `app/mcp/mcp_tools.cpp` — new includes/usings, new `AssetRefCollector` visitor, new `tool_broken_references`, a new special-case branch in `handle_tool_call`, a new `tools_list_schema()` entry.
- Modify: `app/run/mcp.cpp` — capture `asset_registry` in the `dispatch` lambda, pass it through to `handle_tool_call`.
- Test: `tests/unit/app/mcp_tools_test.cpp` (extended) — `RuntimeFixture::call` threads `asset_registry` through; new test cases; existing tool-count assertion (22 → 23).
- Test: `tests/unit/app/mcp_protocol_test.cpp` (extended) — existing tool-count assertion (22 → 23).

**Interfaces:**
- Consumes: `mosaic::engine::reflection::FieldVisitor` (`engine/reflection/field_visitor.hpp`, pre-existing — `field_asset(std::string_view, AssetID&)`, `field_float`, `field_readonly`, `begin_group`, `end_group`, all pure virtual except `field_asset`/`field_string` which have default bodies), `ComponentReflection::inspect(World&, EntityID, std::string_view, FieldVisitor&) const` and `ComponentReflection::components_on(World&, EntityID) const -> std::vector<std::string>` (both pre-existing, `engine/reflection/component_reflection.hpp`), `mosaic::engine::assets::AssetRegistry::contains(AssetID) const -> bool` (pre-existing, `engine/assets/asset_registry.hpp`).
- Produces: `handle_tool_call`'s new 5th parameter, `const mosaic::engine::assets::AssetRegistry& asset_registry = mosaic::engine::assets::AssetRegistry{}` — every existing call site that doesn't pass it keeps compiling unchanged, exactly like `script_factory`/`project_dir` before it.

- [ ] **Step 1: Write the failing tests**

First, update `RuntimeFixture` in `tests/unit/app/mcp_tools_test.cpp` so its `call` threads `asset_registry` through (it already threads `script_factory`):

```cpp
    nlohmann::json call(const std::string& tool, const nlohmann::json& args = nlohmann::json::object())
    {
        return handle_tool_call(world, types, stack, "", tool, args, script_factory, "", asset_registry);
    }
```

(replaces the existing `RuntimeFixture::call` body — same signature, now also passing `""` for `project_dir` and `asset_registry` for the new final parameter.)

Then append these test cases (uses `mosaic::app::random_asset_id()` from `app/asset/asset_io.hpp`, already included; uses `mosaic::engine::assets::AssetMetadata` from `engine/assets/asset_registry.hpp`'s own include of `asset_metadata.hpp`, already transitively available via the existing `#include "engine/assets/asset_registry.hpp"` in this test file):

```cpp
TEST_CASE("mcp_tools: broken_references reports a Material.texture AssetID that doesn't resolve", "[app][mcp]")
{
    RuntimeFixture f;
    const std::uint64_t eid = f.call("create_entity").at("id").get<std::uint64_t>();
    f.call("add_component", {{"entity", eid}, {"component", "Material"}});
    const auto stray_id = mosaic::app::random_asset_id();
    nlohmann::json mat = f.call("get_component", {{"entity", eid}, {"component", "Material"}}).at("value");
    mat["texture"] = stray_id.to_string();
    REQUIRE(f.call("set_component", {{"entity", eid}, {"component", "Material"}, {"value", mat}}).at("ok").get<bool>());

    const nlohmann::json result = f.call("broken_references");
    REQUIRE(result.at("broken").size() == 1);
    const nlohmann::json entry = result.at("broken").at(0);
    REQUIRE(entry.at("entity").get<std::uint64_t>() == eid);
    REQUIRE(entry.at("component").get<std::string>() == "Material");
    REQUIRE(entry.at("field").get<std::string>() == "texture");
    REQUIRE(entry.at("asset_id").get<std::string>() == stray_id.to_string());
}

TEST_CASE("mcp_tools: broken_references does not report an AssetID that resolves in the registry", "[app][mcp]")
{
    RuntimeFixture f;
    mosaic::engine::assets::AssetMetadata meta;
    meta.uuid = mosaic::app::random_asset_id();
    f.asset_registry.register_asset(meta, []() -> std::shared_ptr<void> { return nullptr; });

    const std::uint64_t eid = f.call("create_entity").at("id").get<std::uint64_t>();
    f.call("add_component", {{"entity", eid}, {"component", "Material"}});
    nlohmann::json mat = f.call("get_component", {{"entity", eid}, {"component", "Material"}}).at("value");
    mat["texture"] = meta.uuid.to_string();
    REQUIRE(f.call("set_component", {{"entity", eid}, {"component", "Material"}, {"value", mat}}).at("ok").get<bool>());

    const nlohmann::json result = f.call("broken_references");
    REQUIRE(result.at("broken").empty());
}

TEST_CASE("mcp_tools: broken_references does not report a nil (unset) AssetID field", "[app][mcp]")
{
    RuntimeFixture f;
    const std::uint64_t eid = f.call("create_entity").at("id").get<std::uint64_t>();
    f.call("add_component", {{"entity", eid}, {"component", "Material"}}); // texture defaults to nil
    const nlohmann::json result = f.call("broken_references");
    REQUIRE(result.at("broken").empty());
}

TEST_CASE("mcp_tools: broken_references reports every broken reference across multiple entities", "[app][mcp]")
{
    RuntimeFixture f;
    const std::uint64_t a = f.call("create_entity").at("id").get<std::uint64_t>();
    const std::uint64_t b = f.call("create_entity").at("id").get<std::uint64_t>();
    f.call("add_component", {{"entity", a}, {"component", "Material"}});
    f.call("add_component", {{"entity", b}, {"component", "Material"}});
    const auto stray_a = mosaic::app::random_asset_id();
    const auto stray_b = mosaic::app::random_asset_id();
    for (const auto& [eid, stray] : std::vector<std::pair<std::uint64_t, mosaic::foundation::serialization::AssetID>>{
             {a, stray_a}, {b, stray_b}})
    {
        nlohmann::json mat = f.call("get_component", {{"entity", eid}, {"component", "Material"}}).at("value");
        mat["texture"] = stray.to_string();
        REQUIRE(f.call("set_component", {{"entity", eid}, {"component", "Material"}, {"value", mat}}).at("ok").get<bool>());
    }

    const nlohmann::json result = f.call("broken_references");
    REQUIRE(result.at("broken").size() == 2);
}

TEST_CASE("mcp_tools: broken_references returns an empty array for a world with nothing to report", "[app][mcp]")
{
    RuntimeFixture f;
    f.call("create_entity");
    const nlohmann::json result = f.call("broken_references");
    REQUIRE(result.at("broken").empty());
}
```

Update the existing `tools_list_schema` count test — both occurrences of `22` become `23`:

```cpp
TEST_CASE("mcp_tools: tools_list_schema lists all 23 v1 tools with a description and inputSchema", "[app][mcp]")
{
    const nlohmann::json schema = tools_list_schema();
    REQUIRE(schema.contains("tools"));
    const nlohmann::json tools = schema.at("tools");
    REQUIRE(tools.size() == 23);
```
(leave the rest of that test case's body unchanged).

In `tests/unit/app/mcp_protocol_test.cpp`, update the one existing count assertion:

```cpp
    REQUIRE(parsed.at("result").at("tools").size() == 23);
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build -j4`
Expected: FAIL to build — `broken_references` isn't a registered MCP tool yet, `RuntimeFixture::call`'s new call shape doesn't match `handle_tool_call`'s current signature.

- [ ] **Step 3: Update `mcp_tools.hpp`**

Add the include, alongside the existing engine-tier includes:

```cpp
#include "engine/assets/asset_registry.hpp"
```

Change the `tools_list_schema()` doc comment:

```cpp
// The MCP `tools/list` response body: { "tools": [ { name, description, inputSchema }, ... ] }
// for all 23 v1 tools (entity/component CRUD + undo/redo/save_scene + describe_scene +
// set_script + import_asset + tick + build_project + script_profile + entities_where +
// broken_references).
nlohmann::json tools_list_schema();
```

Update `handle_tool_call`'s declaration and its doc comment:

```cpp
// Dispatches one tool call by name. `scene_path` is the manifest's default_scene path,
// used only by `save_scene`. `script_factory` is used only by `set_script`.
// `project_dir` is used only by `import_asset` and `build_project`. `asset_registry` is
// used only by `broken_references` (all four unused by every other tool, threaded
// through uniformly -- the same "every closure takes everything it might need" shape
// world_globals.cpp uses for world_ptr/types_ptr/stack_ptr). All four default so every
// existing call site keeps compiling unchanged; the affected tool reports a graceful
// failure (not a crash) if the relevant one wasn't actually supplied -- for
// `broken_references`, a default-constructed empty `AssetRegistry` simply makes every
// non-nil AssetID look unresolved. Throws McpToolError for an unknown tool name or
// malformed arguments; otherwise never throws.
nlohmann::json handle_tool_call(mosaic::engine::World& world,
                                const mosaic::engine::reflection::ComponentReflection& types,
                                mosaic::foundation::commands::CommandStack& stack,
                                const std::string& scene_path, const std::string& tool_name,
                                const nlohmann::json& arguments,
                                const ScriptEngineFactory& script_factory = ScriptEngineFactory{},
                                const std::string& project_dir = std::string{},
                                const mosaic::engine::assets::AssetRegistry& asset_registry =
                                    mosaic::engine::assets::AssetRegistry{});
```

- [ ] **Step 4: Update `mcp_tools.cpp`**

Add the include, alongside the other `engine/`-tier includes:

```cpp
#include "engine/assets/asset_registry.hpp"
```

Add usings, alongside the existing ones in the anonymous namespace:

```cpp
using mosaic::engine::assets::AssetRegistry;
using mosaic::foundation::serialization::AssetID;
```

Add `AssetRefCollector` and `tool_broken_references`, placed right after `tool_build_project` and before `tool_handlers()`:

```cpp
class AssetRefCollector final : public mosaic::engine::reflection::FieldVisitor
{
public:
    AssetRefCollector(const AssetRegistry& registry, nlohmann::json& out, EntityID entity,
                      const std::string& component)
        : registry_(registry), out_(out), entity_(entity), component_(component)
    {
    }

    void field_asset(std::string_view name, AssetID& value) override
    {
        if (value.is_nil()) { return; }
        if (registry_.contains(value)) { return; }
        out_.push_back({{"entity", entity_.value()},
                        {"component", component_},
                        {"field", path_with(name)},
                        {"asset_id", value.to_string()}});
    }

    void field_float(std::string_view, float&) override {}
    void field_readonly(std::string_view, const std::string&) override {}
    void begin_group(std::string_view name) override { path_.push_back(std::string(name)); }
    void end_group() override { path_.pop_back(); }

private:
    std::string path_with(std::string_view leaf) const
    {
        std::string result;
        for (const std::string& seg : path_) { result += seg + "."; }
        result += leaf;
        return result;
    }

    const AssetRegistry& registry_;
    nlohmann::json& out_;
    EntityID entity_;
    std::string component_;
    std::vector<std::string> path_;
};

nlohmann::json tool_broken_references(World& world, const ComponentReflection& types,
                                      const AssetRegistry& asset_registry)
{
    nlohmann::json broken = nlohmann::json::array();
    world.for_each_entity(
        [&](EntityID e)
        {
            for (const std::string& name : types.components_on(world, e))
            {
                AssetRefCollector collector(asset_registry, broken, e, name);
                types.inspect(world, e, name, collector);
            }
        });
    return {{"broken", broken}};
}
```

`broken_references` is **not** added to `tool_handlers()` — it needs `AssetRegistry`, which no plain `ToolHandler` carries. It's special-cased in `handle_tool_call` instead (Step 4 below).

Add the `tools_list_schema()` entry, after the `script_profile` entry (last one, right before `return {{"tools", tools}};`):

```cpp
    tools.push_back({{"name", "broken_references"},
                     {"description", "Scan every entity's every component for AssetID fields "
                                     "that are set (non-nil) but don't resolve in the live "
                                     "AssetRegistry. Only AssetID references are checked -- "
                                     "EntityID references aren't distinguishable from other "
                                     "scalar fields yet. Caveat: the AssetRegistry snapshot is "
                                     "loaded once at server startup and never refreshed, so an "
                                     "asset imported via import_asset earlier this session will "
                                     "false-positive here until the mosaic mcp process restarts."},
                     {"inputSchema", schema_none()}});
```

Update `handle_tool_call`'s definition — new parameter, new special-case branch:

```cpp
nlohmann::json handle_tool_call(World& world, const ComponentReflection& types, CommandStack& stack,
                                const std::string& scene_path, const std::string& tool_name,
                                const nlohmann::json& arguments, const ScriptEngineFactory& script_factory,
                                const std::string& project_dir, const AssetRegistry& asset_registry)
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
    if (tool_name == "broken_references")
    {
        return tool_broken_references(world, types, asset_registry);
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

Change the `dispatch` lambda to capture and pass `asset_registry`:

```cpp
        const mcp::ToolDispatch dispatch =
            [&](const std::string& tool_name, const nlohmann::json& arguments) -> nlohmann::json
        {
            return mcp::handle_tool_call(world, types, stack, scene_path, tool_name, arguments,
                                         script_factory, project_dir, asset_registry);
        };
```

(`asset_registry` is already captured by reference via the lambda's `[&]` — only the call itself needs the new trailing argument.)

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build build -j4 --target mosaic_unit_tests && ./build/tests/mosaic_unit_tests "[mcp]"`
Expected: PASS — all 5 new `broken_references` cases, plus every pre-existing `[mcp]` case with the updated 23-tool counts. No failures.

- [ ] **Step 7: Commit**

```bash
git add app/mcp/mcp_tools.hpp app/mcp/mcp_tools.cpp app/run/mcp.cpp \
       tests/unit/app/mcp_tools_test.cpp tests/unit/app/mcp_protocol_test.cpp
git commit -m "$(cat <<'EOF'
feat(mcp): add broken_references, a 23rd Agent API tool

Scans every entity's every component for AssetID fields that are set
but don't resolve in the live AssetRegistry, via the reflection
system's existing FieldVisitor::field_asset hook. Scoped to AssetID
references only -- EntityID references aren't distinguishable from
other scalar fields without a separate FieldVisitor extension.

AssetRegistry is threaded into handle_tool_call as a new parameter,
the same pattern set_script/import_asset/build_project already
established for script_factory/project_dir. The tool's schema
description calls out an accepted, already-tracked limitation: the
AssetRegistry snapshot is loaded once at mosaic mcp startup and never
refreshed, so an asset imported this session will false-positive
until restart.

Sub-project 2 of 4 in the project-health/bulk-editing arc (see
docs/project/deferred-work-backlog.md): orphaned asset detection and
bulk editing remain.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01CEBXDEjzbGDpMyLrKCN4mY
EOF
)"
```

---

### Task 2: Manual verification + `next-steps.md` + backlog entry

**Files:**
- Modify: `docs/project/next-steps.md` — new dated entry.
- Modify: `docs/project/deferred-work-backlog.md` — annotate the shipped item.

**Interfaces:**
- Consumes: `broken_references` via the real `mosaic mcp` CLI (Task 1).
- Produces: nothing new; verification + documentation only.

- [ ] **Step 1: Manually verify against a real project**

```bash
cmake --build build -j4
rm -rf /tmp/broken_references_verify && ./build/app/mosaic create /tmp/broken_references_verify
```

The starter template's entities don't set any `Material.texture` (default scene has no textured materials), so a clean scan should come back empty:

```bash
printf '%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"broken_references","arguments":{}}}' \
  | ./build/app/mosaic mcp /tmp/broken_references_verify
```

Expected: `{"broken": []}` (or equivalent nested under the JSON-RPC result envelope) — no crash, empty array.

Then, in the same session, exercise the actual broken-reference path end to end: create an entity, add a `Material`, `set_component` its `texture` to a made-up (non-existent) AssetID string, and confirm `broken_references` reports it. This can be done with a short sequence of JSON-RPC lines piped into the same `mosaic mcp` invocation (`create_entity`, `add_component`, `get_component`, `set_component` with a fabricated 36-character UUID string in `texture`, then `broken_references`) — inspect by eye that the reported `entity`/`component`/`field`/`asset_id` match what was set.

- [ ] **Step 2: Run the full test suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: green aside from the known pre-existing flakes (the `--host`/`--connect` `ctest`-discovery quirk, and the threaded-Python-teardown flake). Zero new failures.

- [ ] **Step 3: Add the dated `next-steps.md` entry**

Prepend a new dated paragraph to the top of the changelog blob in `docs/project/next-steps.md` (immediately before the current most-recent `**Last updated:**` entry, matching the existing style), summarizing: `broken_references` ships as sub-project 2 of 4 in the project-health/bulk-editing arc (from `docs/project/deferred-work-backlog.md`); a 23rd MCP tool scanning every entity's `AssetID` fields via `FieldVisitor::field_asset`; scoped to `AssetID` references only (`EntityID` reference checking deferred, needs a `FieldVisitor` extension); the `AssetRegistry` staleness caveat (false positives for assets imported earlier the same session, until restart) is called out explicitly in the tool's own schema description; next sub-project is orphaned asset detection (3 of 4). Link this plan and the spec.

- [ ] **Step 4: Update `docs/project/deferred-work-backlog.md`**

Update the existing "(in progress: sub-project 1/4 shipped, ...)" annotations (both the Agent API section bullet and the Effort vs. impact table's callout) to "sub-project 2/4 shipped, see `docs/superpowers/specs/2026-09-19-project-health-broken-references-design.md`".

- [ ] **Step 5: Commit**

```bash
git add docs/project/next-steps.md docs/project/deferred-work-backlog.md
git commit -m "$(cat <<'EOF'
docs: record broken_references's next-steps.md entry, annotate backlog

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01CEBXDEjzbGDpMyLrKCN4mY
EOF
)"
```

---

## Self-Review Notes

- **Spec coverage:** `AssetID`-only scope, `EntityID` explicitly out (Task 1 Step 4's `AssetRefCollector`, only `field_asset` does real work) ✓; no-arguments full-world scan (Task 1 Step 4's `tool_broken_references`, `schema_none()`) ✓; nil `AssetID` never reported (Task 1's 3rd test case, `field_asset`'s `is_nil()` guard) ✓; `AssetRegistry` threaded into `handle_tool_call` as a 4th special-cased tool (Task 1 Steps 3-4) ✓; staleness caveat surfaced in the schema description, not silently fixed (Task 1 Step 4's `tools_list_schema()` entry) ✓; zero CMakeLists changes (confirmed: `asset_registry.hpp` and `field_visitor.hpp` are already transitively linked into `mosaic_app` via existing includes — no `target_link_libraries`/`add_library` edits anywhere) ✓; manual verification + next-steps.md + backlog annotation (Task 2) ✓.
- **Placeholder scan:** no TBD/TODO; every step has real code or an exact command.
- **Type consistency:** `tool_broken_references`'s signature (`World&, const ComponentReflection&, const AssetRegistry&`) matches its two call sites (the new `handle_tool_call` branch) exactly; `AssetRefCollector`'s constructor args match how it's constructed inside `tool_broken_references`'s loop; `handle_tool_call`'s new 5th parameter name (`asset_registry`) is consistent across the `.hpp` declaration, the `.cpp` definition, and `mcp.cpp`'s call site; `RuntimeFixture::call`'s updated body passes arguments in the same order `handle_tool_call` now declares them (`script_factory, project_dir, asset_registry`).
