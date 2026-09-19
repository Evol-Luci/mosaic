# Project Health & Bulk Editing — `orphaned_assets` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a 24th MCP tool, `orphaned_assets`, to the existing `mosaic mcp` server: finds every registered asset that no entity's component field currently references. This is sub-project 3 of 4 in the project-health/bulk-editing arc (see `docs/project/deferred-work-backlog.md`).

**Architecture:** `orphaned_assets` is the mirror of `broken_references` (sub-project 2, already shipped): it walks the same `types.components_on()`/`types.inspect()` entity/component surface, but instead of checking each `AssetID` field against the registry immediately, it first gathers every non-nil `AssetID` referenced anywhere into a set, then diffs that set against every id `AssetRegistry::for_each()` enumerates. It reuses `broken_references`'s existing `AssetRegistry` threading through `handle_tool_call` with zero signature changes — `orphaned_assets` is a 5th special-cased branch alongside it.

**Tech Stack:** C++20, `nlohmann_json`, `engine/reflection/field_visitor.hpp`'s existing `FieldVisitor` interface, `engine/assets/asset_registry.hpp`'s existing `AssetRegistry::for_each`. `AssetID` already has a `std::hash` specialization (`foundation/serialization/reference.hpp:152`), so `std::unordered_set<AssetID>` needs no new infrastructure. No new CMakeLists changes.

**Spec:** `docs/superpowers/specs/2026-09-19-project-health-orphaned-assets-design.md`

## Global Constraints

- No arguments, no component filter — the tool always scans the whole project (entities and the asset registry both).
- `AssetMetadata.dependencies` is explicitly NOT consulted — out of scope, since it's never populated anywhere in production code today (see spec's Scope section).
- A nil `AssetID` field never counts as "referencing" anything.
- The `AssetRegistry` snapshot is populated once at `mosaic mcp` startup and never refreshed; an asset imported via `import_asset` earlier in the same session is invisible to `AssetRegistry::for_each()` entirely (neither a false orphan nor a false non-orphan) — restated in the tool's own `tools_list_schema()` description, matching `broken_references`'s precedent.
- No new CMakeLists changes.

---

### Task 1: `orphaned_assets` tool

**Files:**
- Modify: `app/mcp/mcp_tools.hpp` — doc-comment tool count 23 → 24. No signature changes (reuses the existing `asset_registry` parameter added for `broken_references`).
- Modify: `app/mcp/mcp_tools.cpp` — new `ReferencedAssetGatherer` visitor, new `tool_orphaned_assets` function, a new special-case branch in `handle_tool_call`, a new `tools_list_schema()` entry.
- Test: `tests/unit/app/mcp_tools_test.cpp` (extended) — new test cases, existing tool-count assertion (23 → 24).
- Test: `tests/unit/app/mcp_protocol_test.cpp` (extended) — existing tool-count assertion (23 → 24).

**Interfaces:**
- Consumes: `mosaic::engine::reflection::FieldVisitor` (`engine/reflection/field_visitor.hpp`, pre-existing — same interface `broken_references`'s `AssetRefCollector` already implements), `ComponentReflection::inspect`/`components_on` (pre-existing), `mosaic::engine::assets::AssetRegistry::for_each(F&&) const` (pre-existing, `engine/assets/asset_registry.hpp:53`, invokes `fn(const AssetMetadata&)` for every registered asset), `AssetMetadata`'s `uuid`/`type`/`source` fields (pre-existing, `engine/assets/asset_metadata.hpp`).
- Produces: no new public function signature or parameter — `orphaned_assets` reuses `handle_tool_call`'s existing `asset_registry` parameter exactly as `broken_references` already does; only a new `if (tool_name == "orphaned_assets")` branch is added to its body.

- [ ] **Step 1: Write the failing tests**

Append these test cases to `tests/unit/app/mcp_tools_test.cpp`, right after the existing `mcp_tools: broken_references returns an empty array for a world with nothing to report` test case (uses `mosaic::app::random_asset_id()` from `app/asset/asset_io.hpp` and `mosaic::engine::assets::AssetMetadata` from `engine/assets/asset_registry.hpp`, both already included in this test file from `broken_references`'s own tests):

```cpp
TEST_CASE("mcp_tools: orphaned_assets reports a registered asset nothing references", "[app][mcp]")
{
    RuntimeFixture f;
    mosaic::engine::assets::AssetMetadata meta;
    meta.uuid = mosaic::app::random_asset_id();
    meta.type = "image";
    meta.source = "textures/unused.png";
    f.asset_registry.register_asset(meta, []() -> std::shared_ptr<void> { return nullptr; });

    const nlohmann::json result = f.call("orphaned_assets");
    REQUIRE(result.at("orphaned").size() == 1);
    const nlohmann::json entry = result.at("orphaned").at(0);
    REQUIRE(entry.at("asset_id").get<std::string>() == meta.uuid.to_string());
    REQUIRE(entry.at("type").get<std::string>() == "image");
    REQUIRE(entry.at("source").get<std::string>() == "textures/unused.png");
}

TEST_CASE("mcp_tools: orphaned_assets does not report an asset referenced by an entity's field", "[app][mcp]")
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

    const nlohmann::json result = f.call("orphaned_assets");
    REQUIRE(result.at("orphaned").empty());
}

TEST_CASE("mcp_tools: orphaned_assets ignores nil AssetID fields as references", "[app][mcp]")
{
    RuntimeFixture f;
    mosaic::engine::assets::AssetMetadata meta;
    meta.uuid = mosaic::app::random_asset_id();
    f.asset_registry.register_asset(meta, []() -> std::shared_ptr<void> { return nullptr; });

    const std::uint64_t eid = f.call("create_entity").at("id").get<std::uint64_t>();
    f.call("add_component", {{"entity", eid}, {"component", "Material"}}); // texture defaults to nil

    const nlohmann::json result = f.call("orphaned_assets");
    REQUIRE(result.at("orphaned").size() == 1);
    REQUIRE(result.at("orphaned").at(0).at("asset_id").get<std::string>() == meta.uuid.to_string());
}

TEST_CASE("mcp_tools: orphaned_assets reports every orphan across a mixed registry", "[app][mcp]")
{
    RuntimeFixture f;
    mosaic::engine::assets::AssetMetadata referenced_meta;
    referenced_meta.uuid = mosaic::app::random_asset_id();
    f.asset_registry.register_asset(referenced_meta, []() -> std::shared_ptr<void> { return nullptr; });

    mosaic::engine::assets::AssetMetadata orphan_a;
    orphan_a.uuid = mosaic::app::random_asset_id();
    f.asset_registry.register_asset(orphan_a, []() -> std::shared_ptr<void> { return nullptr; });

    mosaic::engine::assets::AssetMetadata orphan_b;
    orphan_b.uuid = mosaic::app::random_asset_id();
    f.asset_registry.register_asset(orphan_b, []() -> std::shared_ptr<void> { return nullptr; });

    const std::uint64_t eid = f.call("create_entity").at("id").get<std::uint64_t>();
    f.call("add_component", {{"entity", eid}, {"component", "Material"}});
    nlohmann::json mat = f.call("get_component", {{"entity", eid}, {"component", "Material"}}).at("value");
    mat["texture"] = referenced_meta.uuid.to_string();
    REQUIRE(f.call("set_component", {{"entity", eid}, {"component", "Material"}, {"value", mat}}).at("ok").get<bool>());

    const nlohmann::json result = f.call("orphaned_assets");
    REQUIRE(result.at("orphaned").size() == 2);
    std::vector<std::string> ids;
    for (const auto& entry : result.at("orphaned")) { ids.push_back(entry.at("asset_id").get<std::string>()); }
    REQUIRE(std::find(ids.begin(), ids.end(), orphan_a.uuid.to_string()) != ids.end());
    REQUIRE(std::find(ids.begin(), ids.end(), orphan_b.uuid.to_string()) != ids.end());
}

TEST_CASE("mcp_tools: orphaned_assets returns an empty array for an empty registry", "[app][mcp]")
{
    RuntimeFixture f;
    f.call("create_entity");
    const nlohmann::json result = f.call("orphaned_assets");
    REQUIRE(result.at("orphaned").empty());
}

TEST_CASE("mcp_tools: orphaned_assets still reports an asset orphaned when a different asset id is referenced", "[app][mcp]")
{
    RuntimeFixture f;
    mosaic::engine::assets::AssetMetadata orphan;
    orphan.uuid = mosaic::app::random_asset_id();
    f.asset_registry.register_asset(orphan, []() -> std::shared_ptr<void> { return nullptr; });

    const auto unrelated_id = mosaic::app::random_asset_id(); // never registered, just referenced
    const std::uint64_t eid = f.call("create_entity").at("id").get<std::uint64_t>();
    f.call("add_component", {{"entity", eid}, {"component", "Material"}});
    nlohmann::json mat = f.call("get_component", {{"entity", eid}, {"component", "Material"}}).at("value");
    mat["texture"] = unrelated_id.to_string();
    REQUIRE(f.call("set_component", {{"entity", eid}, {"component", "Material"}, {"value", mat}}).at("ok").get<bool>());

    const nlohmann::json result = f.call("orphaned_assets");
    REQUIRE(result.at("orphaned").size() == 1);
    REQUIRE(result.at("orphaned").at(0).at("asset_id").get<std::string>() == orphan.uuid.to_string());
}
```

`std::find`/`std::vector` need `#include <algorithm>` and `#include <vector>` — neither is currently included at the top of `tests/unit/app/mcp_tools_test.cpp` (confirmed by inspection); add both alongside the other standard-library includes (`<cstdint>`, `<filesystem>`, etc.) before appending these test cases.

Update the existing `tools_list_schema` count test — both occurrences of `23` become `24`:

```cpp
TEST_CASE("mcp_tools: tools_list_schema lists all 24 v1 tools with a description and inputSchema", "[app][mcp]")
{
    const nlohmann::json schema = tools_list_schema();
    REQUIRE(schema.contains("tools"));
    const nlohmann::json tools = schema.at("tools");
    REQUIRE(tools.size() == 24);
```
(leave the rest of that test case's body unchanged).

In `tests/unit/app/mcp_protocol_test.cpp`, update the one existing count assertion:

```cpp
    REQUIRE(parsed.at("result").at("tools").size() == 24);
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build -j4`
Expected: FAIL to build — `orphaned_assets` isn't a registered MCP tool yet.

- [ ] **Step 3: Update `mcp_tools.hpp`**

Change the `tools_list_schema()` doc comment:

```cpp
// The MCP `tools/list` response body: { "tools": [ { name, description, inputSchema }, ... ] }
// for all 24 v1 tools (entity/component CRUD + undo/redo/save_scene + describe_scene +
// set_script + import_asset + tick + build_project + script_profile + entities_where +
// broken_references + orphaned_assets).
nlohmann::json tools_list_schema();
```

No other changes to this file — `handle_tool_call`'s declaration and its own doc comment are unchanged; `orphaned_assets` reuses the exact same `asset_registry` parameter `broken_references` already added.

- [ ] **Step 4: Update `mcp_tools.cpp`**

No new includes needed — `engine/assets/asset_registry.hpp` (which already transitively includes `asset_metadata.hpp`) is already included from `broken_references`'s own work, and `<unordered_set>` is already included from `entities_where`'s work.

Add `ReferencedAssetGatherer` and `tool_orphaned_assets`, placed right after `tool_broken_references` and before `tool_handlers()`:

```cpp
class ReferencedAssetGatherer final : public mosaic::engine::reflection::FieldVisitor
{
public:
    explicit ReferencedAssetGatherer(std::unordered_set<AssetID>& out) : out_(out) {}

    void field_asset(std::string_view, AssetID& value) override
    {
        if (!value.is_nil()) { out_.insert(value); }
    }

    void field_float(std::string_view, float&) override {}
    void field_readonly(std::string_view, const std::string&) override {}
    void begin_group(std::string_view) override {}
    void end_group() override {}

private:
    std::unordered_set<AssetID>& out_;
};

nlohmann::json tool_orphaned_assets(World& world, const ComponentReflection& types,
                                    const AssetRegistry& asset_registry)
{
    std::unordered_set<AssetID> referenced;
    world.for_each_entity(
        [&](EntityID e)
        {
            for (const std::string& name : types.components_on(world, e))
            {
                ReferencedAssetGatherer gatherer(referenced);
                types.inspect(world, e, name, gatherer);
            }
        });

    nlohmann::json orphaned = nlohmann::json::array();
    asset_registry.for_each(
        [&](const mosaic::engine::assets::AssetMetadata& meta)
        {
            if (referenced.find(meta.uuid) == referenced.end())
            {
                orphaned.push_back({{"asset_id", meta.uuid.to_string()},
                                    {"type", meta.type},
                                    {"source", meta.source}});
            }
        });
    return {{"orphaned", orphaned}};
}
```

`orphaned_assets` is **not** added to `tool_handlers()` — like `broken_references`, it needs `AssetRegistry`, which no plain `ToolHandler` carries. It's special-cased in `handle_tool_call` instead (below).

Add the `tools_list_schema()` entry, after the `broken_references` entry (last one, right before `return {{"tools", tools}};`):

```cpp
    tools.push_back({{"name", "orphaned_assets"},
                     {"description", "Return every registered asset that no entity's component "
                                     "field currently references (gathered via the same AssetID "
                                     "field walk broken_references uses). Caveat: the AssetRegistry "
                                     "snapshot is loaded once at server startup and never refreshed, "
                                     "so an asset imported via import_asset earlier this session is "
                                     "simply absent from this scan entirely -- neither falsely "
                                     "flagged nor falsely cleared -- until the mosaic mcp process "
                                     "restarts."},
                     {"inputSchema", schema_none()}});
```

Update `handle_tool_call`'s definition — one new special-case branch, no parameter changes:

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
    if (tool_name == "orphaned_assets")
    {
        return tool_orphaned_assets(world, types, asset_registry);
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

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build -j4 --target mosaic_unit_tests && ./build/tests/mosaic_unit_tests "[mcp]"`
Expected: PASS — all 6 new `orphaned_assets` cases, plus every pre-existing `[mcp]` case with the updated 24-tool counts. No failures.

- [ ] **Step 6: Commit**

```bash
git add app/mcp/mcp_tools.hpp app/mcp/mcp_tools.cpp \
       tests/unit/app/mcp_tools_test.cpp tests/unit/app/mcp_protocol_test.cpp
git commit -m "$(cat <<'EOF'
feat(mcp): add orphaned_assets, a 24th Agent API tool

Finds every registered asset that no entity's component field
currently references -- the mirror of broken_references, reusing the
same FieldVisitor::field_asset walk to gather every referenced
AssetID into a set, then diffing it against every id AssetRegistry
enumerates. Reuses handle_tool_call's existing asset_registry
parameter with zero signature changes.

AssetMetadata.dependencies (asset-to-asset references) is explicitly
not consulted -- it's never populated anywhere in production code
today. Same AssetRegistry staleness caveat as broken_references
applies and is restated in the tool's own schema description.

Sub-project 3 of 4 in the project-health/bulk-editing arc (see
docs/project/deferred-work-backlog.md): bulk editing remains.

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
- Consumes: `orphaned_assets` via the real `mosaic mcp` CLI (Task 1).
- Produces: nothing new; verification + documentation only.

- [ ] **Step 1: Manually verify against a real project**

```bash
cmake --build build -j4 --target mosaic
rm -rf /tmp/orphaned_assets_verify && ./build/app/mosaic create /tmp/orphaned_assets_verify
```

The starter template's own on-disk `assets/` directory (if any) plus whatever `import_asset` calls are made in this same session are the only things `orphaned_assets` can see. Run a clean scan first:

```bash
printf '%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"orphaned_assets","arguments":{}}}' \
  | ./build/app/mosaic mcp /tmp/orphaned_assets_verify
```

Expected: `{"orphaned": [...]}` with whatever shape the starter template's actual registered-but-unreferenced assets produce (inspect by eye — no crash, sane shape: `asset_id`/`type`/`source` per entry).

Then, in one continuous `mosaic mcp` session, exercise the full round trip: `import_asset` a real file (e.g. import a `.py` script or a PPM image via the same pattern `import_asset`'s own manual verification used in its sub-project), confirm the returned `asset_id` is present, then call `orphaned_assets` again in the SAME session and confirm that just-imported asset does NOT appear (it isn't referenced by any entity, but it's also invisible to `AssetRegistry::for_each()` since the registry snapshot was taken before this import) — this directly confirms the Accepted Limitation section of the spec by observation, not just by reading the code.

- [ ] **Step 2: Run the full test suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: green aside from the known pre-existing flakes (the `--host`/`--connect` `ctest`-discovery quirk, and the threaded-Python-teardown flake). Zero new failures.

- [ ] **Step 3: Add the dated `next-steps.md` entry**

Prepend a new dated paragraph to the top of the changelog blob in `docs/project/next-steps.md` (immediately before the current most-recent `**Last updated:**` entry, matching the existing style), summarizing: `orphaned_assets` ships as sub-project 3 of 4 in the project-health/bulk-editing arc (from `docs/project/deferred-work-backlog.md`); a 24th MCP tool, the mirror of `broken_references` -- gathers every referenced `AssetID` via the same `FieldVisitor::field_asset` walk, diffs against `AssetRegistry::for_each()`; `AssetMetadata.dependencies` explicitly not consulted (never populated in production code today, confirmed by grep); the same `AssetRegistry` staleness caveat as `broken_references` applies, this time meaning a same-session `import_asset`'d asset is invisible to the scan entirely rather than falsely flagged; next sub-project is bulk editing (4 of 4, the last one). Link this plan and the spec.

- [ ] **Step 4: Update `docs/project/deferred-work-backlog.md`**

Update the existing "(in progress: sub-project 2/4 shipped, ...)" annotations (both the Agent API section bullet and the Effort vs. impact table's callout) to "sub-project 3/4 shipped, see `docs/superpowers/specs/2026-09-19-project-health-orphaned-assets-design.md`".

- [ ] **Step 5: Commit**

```bash
git add docs/project/next-steps.md docs/project/deferred-work-backlog.md
git commit -m "$(cat <<'EOF'
docs: record orphaned_assets's next-steps.md entry, annotate backlog

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01CEBXDEjzbGDpMyLrKCN4mY
EOF
)"
```

---

## Self-Review Notes

- **Spec coverage:** two-pass gather-then-diff design (Task 1 Step 4's `tool_orphaned_assets`) ✓; `dependencies` explicitly not consulted (no code touches `meta.dependencies` anywhere in Task 1) ✓; nil `AssetID` never counts as a reference (`ReferencedAssetGatherer::field_asset`'s `is_nil()` guard, Task 1's 3rd test case) ✓; staleness caveat restated in the schema description (Task 1 Step 4's `tools_list_schema()` entry) and directly observed in Task 2 Step 1's manual verification (not just asserted) ✓; output includes `asset_id`/`type`/`source` (Task 1's 1st test case) ✓; reuses the existing `asset_registry` parameter with zero `handle_tool_call` signature changes (Task 1 Step 3 makes no signature edits) ✓; zero CMakeLists changes (confirmed: no new includes beyond what `broken_references`/`entities_where` already added, no `target_link_libraries`/`add_library` edits) ✓; manual verification + next-steps.md + backlog annotation (Task 2) ✓.
- **Placeholder scan:** no TBD/TODO; every step has real code or an exact command.
- **Type consistency:** `tool_orphaned_assets`'s signature (`World&, const ComponentReflection&, const AssetRegistry&`) matches its call site in the new `handle_tool_call` branch exactly, and matches `tool_broken_references`'s existing signature shape (same three parameters, same order) for consistency; `ReferencedAssetGatherer`'s constructor and `field_asset` override match `AssetRefCollector`'s existing `FieldVisitor` override signatures exactly (`std::string_view`, `AssetID&`); `AssetMetadata`'s `uuid`/`type`/`source` field names match `engine/assets/asset_metadata.hpp`'s actual definition, not invented names.
