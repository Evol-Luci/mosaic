# Project Health & Bulk Editing — `entities_where` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a 22nd MCP tool, `entities_where`, to the existing `mosaic mcp` server: extends `entities_with(component)`'s has-component-only check with a field-value filter (`eq`/`ne`/`gt`/`gte`/`lt`/`lte`), returning matching entity ids and their actual field values. This is sub-project 1 of 4 in the project-health/bulk-editing arc (see `docs/project/deferred-work-backlog.md`).

**Architecture:** `entities_where` fits the uniform `ToolHandler` shape (only needs `World&`/`ComponentReflection&`) — added directly to `tool_handlers()`, no special-casing, mirroring `script_profile`'s simplicity. Field-path resolution reuses `AnimationSystem`'s existing `split_path()` helper; a small new `json_at_path` walker reads (rather than writes) a value at that path in the JSON tree `get_component` already produces.

**Tech Stack:** C++20, `nlohmann_json`, the existing `split_path()` helper (already a build dependency, already linked into `mosaic_app` via the animation system). No new CMakeLists changes.

**Spec:** `docs/superpowers/specs/2026-09-19-project-health-entities-where-design.md`

## Global Constraints

- Operators are exactly `eq`, `ne`, `gt`, `gte`, `lt`, `lte` — an unrecognized `op` string is `McpToolError`.
- A missing component, a field path that doesn't resolve, or a numeric operator against a non-numeric field value is a **graceful non-match** for that entity — never an error.
- `McpToolError` stays reserved for malformed request parameters: missing `component`/`field`/`op`/`value`, or an unrecognized `op`.
- No new CMakeLists changes — `animation_system.hpp` is already linked into `mosaic_app`.

---

### Task 1: `entities_where` tool

**Files:**
- Modify: `app/mcp/mcp_tools.hpp` — doc-comment tool count 21 → 22. No signature changes.
- Modify: `app/mcp/mcp_tools.cpp` — new includes, `json_at_path` helper, `compare` helper, `tool_entities_where`, a `tool_handlers()` entry, a `tools_list_schema()` entry, a `schema_entities_where()` helper.
- Test: `tests/unit/app/mcp_tools_test.cpp` (extended) — new test cases, existing tool-count assertion (21 → 22).
- Test: `tests/unit/app/mcp_protocol_test.cpp` (extended) — existing tool-count assertion (21 → 22).

**Interfaces:**
- Consumes: `mosaic::engine::animation::split_path(const std::string&, char) -> std::vector<std::string>` (`engine/animation/animation_system.hpp`, pre-existing), the existing `value_to_json`/`require_string` helpers already in `mcp_tools.cpp`.
- Produces: no new public function signature — `entities_where` uses the existing `ToolHandler` shape as-is, added to the existing `tool_handlers()` map.

- [ ] **Step 1: Write the failing tests**

Append these test cases to `tests/unit/app/mcp_tools_test.cpp`:

```cpp
TEST_CASE("mcp_tools: entities_where matches numeric fields with gt/gte/lt/lte", "[app][mcp]")
{
    Fixture f;
    const std::uint64_t low = f.call("create_entity").at("id").get<std::uint64_t>();
    const std::uint64_t mid = f.call("create_entity").at("id").get<std::uint64_t>();
    const std::uint64_t high = f.call("create_entity").at("id").get<std::uint64_t>();
    for (std::uint64_t eid : {low, mid, high})
    {
        f.call("add_component", {{"entity", eid}, {"component", "Transform"}});
    }
    // Directly set position.x on each via set_component with a full Transform value.
    auto set_x = [&](std::uint64_t eid, double x)
    {
        nlohmann::json t = f.call("get_component", {{"entity", eid}, {"component", "Transform"}}).at("value");
        t["position"]["x"] = x;
        REQUIRE(f.call("set_component", {{"entity", eid}, {"component", "Transform"}, {"value", t}}).at("ok").get<bool>());
    };
    set_x(low, 1.0);
    set_x(mid, 5.0);
    set_x(high, 9.0);

    const nlohmann::json gt = f.call("entities_where",
        {{"component", "Transform"}, {"field", "position.x"}, {"op", "gt"}, {"value", 5.0}});
    REQUIRE(gt.at("matches").size() == 1);
    REQUIRE(gt.at("matches").at(0).at("entity").get<std::uint64_t>() == high);
    REQUIRE(gt.at("matches").at(0).at("value").get<double>() == 9.0);

    const nlohmann::json gte = f.call("entities_where",
        {{"component", "Transform"}, {"field", "position.x"}, {"op", "gte"}, {"value", 5.0}});
    REQUIRE(gte.at("matches").size() == 2);

    const nlohmann::json lt = f.call("entities_where",
        {{"component", "Transform"}, {"field", "position.x"}, {"op", "lt"}, {"value", 5.0}});
    REQUIRE(lt.at("matches").size() == 1);
    REQUIRE(lt.at("matches").at(0).at("entity").get<std::uint64_t>() == low);

    const nlohmann::json lte = f.call("entities_where",
        {{"component", "Transform"}, {"field", "position.x"}, {"op", "lte"}, {"value", 5.0}});
    REQUIRE(lte.at("matches").size() == 2);
}

TEST_CASE("mcp_tools: entities_where matches any JSON type with eq/ne", "[app][mcp]")
{
    Fixture f;
    const std::uint64_t a = f.call("create_entity").at("id").get<std::uint64_t>();
    const std::uint64_t b = f.call("create_entity").at("id").get<std::uint64_t>();
    f.call("add_component", {{"entity", a}, {"component", "Name"}});
    f.call("add_component", {{"entity", b}, {"component", "Name"}});
    f.call("set_component", {{"entity", a}, {"component", "Name"}, {"value", {{"value", "Player"}}}});
    f.call("set_component", {{"entity", b}, {"component", "Name"}, {"value", {{"value", "Enemy"}}}});

    const nlohmann::json eq = f.call("entities_where",
        {{"component", "Name"}, {"field", "value"}, {"op", "eq"}, {"value", "Player"}});
    REQUIRE(eq.at("matches").size() == 1);
    REQUIRE(eq.at("matches").at(0).at("entity").get<std::uint64_t>() == a);

    const nlohmann::json ne = f.call("entities_where",
        {{"component", "Name"}, {"field", "value"}, {"op", "ne"}, {"value", "Player"}});
    REQUIRE(ne.at("matches").size() == 1);
    REQUIRE(ne.at("matches").at(0).at("entity").get<std::uint64_t>() == b);
}

TEST_CASE("mcp_tools: entities_where excludes an entity missing the component, gracefully", "[app][mcp]")
{
    Fixture f;
    f.call("create_entity"); // no Transform attached
    const nlohmann::json result = f.call("entities_where",
        {{"component", "Transform"}, {"field", "position.x"}, {"op", "gte"}, {"value", 0.0}});
    REQUIRE(result.at("matches").empty());
}

TEST_CASE("mcp_tools: entities_where excludes an entity whose field path doesn't resolve, gracefully", "[app][mcp]")
{
    Fixture f;
    const std::uint64_t eid = f.call("create_entity").at("id").get<std::uint64_t>();
    f.call("add_component", {{"entity", eid}, {"component", "Transform"}});
    const nlohmann::json result = f.call("entities_where",
        {{"component", "Transform"}, {"field", "position.not_a_real_field"}, {"op", "eq"}, {"value", 0.0}});
    REQUIRE(result.at("matches").empty());
}

TEST_CASE("mcp_tools: entities_where excludes a numeric op against a non-numeric field, gracefully", "[app][mcp]")
{
    Fixture f;
    const std::uint64_t eid = f.call("create_entity").at("id").get<std::uint64_t>();
    f.call("add_component", {{"entity", eid}, {"component", "Name"}});
    f.call("set_component", {{"entity", eid}, {"component", "Name"}, {"value", {{"value", "hi"}}}});
    const nlohmann::json result = f.call("entities_where",
        {{"component", "Name"}, {"field", "value"}, {"op", "gt"}, {"value", 0.0}});
    REQUIRE(result.at("matches").empty());
}

TEST_CASE("mcp_tools: entities_where throws McpToolError for an unknown op", "[app][mcp]")
{
    Fixture f;
    REQUIRE_THROWS_AS(f.call("entities_where",
        {{"component", "Transform"}, {"field", "position.x"}, {"op", "regex"}, {"value", 0.0}}), McpToolError);
}

TEST_CASE("mcp_tools: entities_where throws McpToolError for missing required arguments", "[app][mcp]")
{
    Fixture f;
    REQUIRE_THROWS_AS(f.call("entities_where", {{"field", "position.x"}, {"op", "eq"}, {"value", 0.0}}), McpToolError);
    REQUIRE_THROWS_AS(f.call("entities_where", {{"component", "Transform"}, {"op", "eq"}, {"value", 0.0}}), McpToolError);
    REQUIRE_THROWS_AS(f.call("entities_where", {{"component", "Transform"}, {"field", "position.x"}, {"value", 0.0}}), McpToolError);
    REQUIRE_THROWS_AS(f.call("entities_where", {{"component", "Transform"}, {"field", "position.x"}, {"op", "eq"}}), McpToolError);
}
```

Update the existing `tools_list_schema` count test — both occurrences of `21` become `22`:

```cpp
TEST_CASE("mcp_tools: tools_list_schema lists all 22 v1 tools with a description and inputSchema", "[app][mcp]")
{
    const nlohmann::json schema = tools_list_schema();
    REQUIRE(schema.contains("tools"));
    const nlohmann::json tools = schema.at("tools");
    REQUIRE(tools.size() == 22);
```
(leave the rest of that test case's body unchanged).

In `tests/unit/app/mcp_protocol_test.cpp`, update the one existing count assertion:

```cpp
    REQUIRE(parsed.at("result").at("tools").size() == 22);
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build -j4`
Expected: FAIL to build — `entities_where` isn't a registered MCP tool yet.

- [ ] **Step 3: Update `mcp_tools.hpp`**

Change the `tools_list_schema()` doc comment:

```cpp
// The MCP `tools/list` response body: { "tools": [ { name, description, inputSchema }, ... ] }
// for all 22 v1 tools (entity/component CRUD + undo/redo/save_scene + describe_scene +
// set_script + import_asset + tick + build_project + script_profile + entities_where).
nlohmann::json tools_list_schema();
```

No other changes to this file — `handle_tool_call`'s declaration is unchanged.

- [ ] **Step 4: Update `mcp_tools.cpp`**

Add the includes, alongside the existing ones:

```cpp
#include "engine/animation/animation_system.hpp"
```

alongside the other `engine/`-tier includes, and

```cpp
#include <unordered_set>
```

alongside the other standard-library includes.

Add `json_at_path` and `compare`, placed right after `tool_entities_with` and before `tool_entity_count`:

```cpp
std::optional<nlohmann::json> json_at_path(const nlohmann::json& root,
                                           const std::vector<std::string>& path)
{
    const nlohmann::json* cur = &root;
    for (const std::string& seg : path)
    {
        if (!cur->is_object() || !cur->contains(seg)) { return std::nullopt; }
        cur = &cur->at(seg);
    }
    return *cur;
}

bool compare(const nlohmann::json& lhs, const std::string& op, const nlohmann::json& rhs)
{
    if (op == "eq") { return lhs == rhs; }
    if (op == "ne") { return lhs != rhs; }
    if (!lhs.is_number() || !rhs.is_number()) { return false; }
    const double l = lhs.get<double>();
    const double r = rhs.get<double>();
    if (op == "gt") { return l > r; }
    if (op == "gte") { return l >= r; }
    if (op == "lt") { return l < r; }
    return l <= r; // "lte" -- op is already validated by the caller before compare() is called
}

nlohmann::json tool_entities_where(World& world, const ComponentReflection& types, CommandStack&,
                                   const std::string&, const nlohmann::json& args)
{
    const std::string component = require_string(args, "component");
    const std::string field = require_string(args, "field");
    const std::string op = require_string(args, "op");
    if (!args.contains("value"))
    {
        throw McpToolError("missing 'value'");
    }
    static const std::unordered_set<std::string> kValidOps = {"eq", "ne", "gt", "gte", "lt", "lte"};
    if (kValidOps.find(op) == kValidOps.end())
    {
        throw McpToolError("unknown op: " + op + " (expected one of eq, ne, gt, gte, lt, lte)");
    }
    const nlohmann::json target = args.at("value");
    const std::vector<std::string> path = mosaic::engine::animation::split_path(field, '.');

    nlohmann::json matches = nlohmann::json::array();
    world.for_each_entity(
        [&](EntityID e)
        {
            const auto v = types.component_value(world, e, component);
            if (!v) { return; }
            const nlohmann::json component_json = value_to_json(*v);
            const auto field_value = json_at_path(component_json, path);
            if (!field_value) { return; }
            if (compare(*field_value, op, target))
            {
                matches.push_back({{"entity", e.value()}, {"value", *field_value}});
            }
        });
    return {{"matches", matches}};
}
```

Add `"entities_where"` to `tool_handlers()`'s initializer list:

```cpp
        {"entities", &tool_entities},             {"entities_with", &tool_entities_with},
        {"entities_where", &tool_entities_where},
```

Add the new schema helper, right after `schema_component()`:

```cpp
nlohmann::json schema_entities_where()
{
    return {{"type", "object"},
           {"properties",
            {{"component", {{"type", "string"}}},
             {"field", {{"type", "string"},
                       {"description", "Dotted path into the component's fields, e.g. \"position.x\""}}},
             {"op", {{"type", "string"}, {"enum", {"eq", "ne", "gt", "gte", "lt", "lte"}}}},
             {"value", {{"description", "The value to compare against"}}}}},
           {"required", {"component", "field", "op", "value"}}};
}
```

Add the `tools_list_schema()` entry, after the `entities_with` entry:

```cpp
    tools.push_back({{"name", "entities_with"},
                     {"description", "Return the ids of every entity that currently has the named component."},
                     {"inputSchema", schema_component()}});
    tools.push_back({{"name", "entities_where"},
                     {"description", "Return the ids and actual field values of every entity whose "
                                     "component has a field matching a condition (op: eq, ne, gt, "
                                     "gte, lt, lte). A missing component, an unresolvable field path, "
                                     "or a numeric op against a non-numeric value gracefully excludes "
                                     "that entity rather than erroring."},
                     {"inputSchema", schema_entities_where()}});
```

`handle_tool_call` itself needs no changes — `entities_where` reaches it through the existing generic `tool_handlers()` lookup path.

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build -j4 --target mosaic_unit_tests && ./build/tests/mosaic_unit_tests "[mcp]"`
Expected: PASS — all 7 new `entities_where` cases, plus every pre-existing `[mcp]` case with the updated 22-tool counts. No failures.

- [ ] **Step 6: Commit**

```bash
git add app/mcp/mcp_tools.hpp app/mcp/mcp_tools.cpp \
       tests/unit/app/mcp_tools_test.cpp tests/unit/app/mcp_protocol_test.cpp
git commit -m "$(cat <<'EOF'
feat(mcp): add entities_where, a 22nd Agent API tool

Extends entities_with with a field-value filter (eq/ne/gt/gte/lt/lte),
returning matching entity ids and their actual field values. Reuses
AnimationSystem's split_path() for dotted-path resolution; a missing
component, unresolvable path, or numeric op against a non-numeric
value gracefully excludes that entity rather than erroring. Fits the
uniform ToolHandler shape directly -- no special-casing needed.

Sub-project 1 of 4 in the project-health/bulk-editing arc (see
docs/project/deferred-work-backlog.md): the query primitive broken
reference detection, orphaned asset detection, and bulk editing will
each build on.

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
- Consumes: `entities_where` via the real `mosaic mcp` CLI (Task 1).
- Produces: nothing new; verification + documentation only.

- [ ] **Step 1: Manually verify against a real project**

Run:

```bash
cmake --build build -j4
rm -rf /tmp/entities_where_verify && ./build/app/mosaic create /tmp/entities_where_verify
```

Note the starter template already has multiple entities with `Transform` components (13 entities total per the existing template — 11 with `Transform`, per `describe_scene`'s own manual-verification precedent). Run a query against it directly:

```bash
printf '%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"entities_where","arguments":{"component":"Transform","field":"position.x","op":"gte","value":0}}}' \
  | ./build/app/mosaic mcp /tmp/entities_where_verify
```

Expected: a `matches` array with entries for entities whose `Transform.position.x >= 0` (every entity in the default scene, most likely — the starter template's objects sit at or near the origin) — inspect by eye that the shape is sane (`entity`/`value` pairs) and no crash.

- [ ] **Step 2: Run the full test suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: green aside from the known pre-existing flakes (the `--host`/`--connect` `ctest`-discovery quirk, and the threaded-Python-teardown flake). Zero new failures.

- [ ] **Step 3: Add the dated `next-steps.md` entry**

Prepend a new dated paragraph to the top of the changelog blob in `docs/project/next-steps.md` (immediately before the current most-recent `**Last updated:**` entry, matching the existing style), summarizing: `entities_where` ships as sub-project 1 of 4 in the project-health/bulk-editing arc (from `docs/project/deferred-work-backlog.md`); a 22nd MCP tool extending `entities_with` with a field-value filter, reusing `AnimationSystem`'s `split_path()`; multi-condition filters, string ordering, and existence checks are explicitly deferred; next sub-project is broken reference detection (2 of 4). Link this plan and the spec.

- [ ] **Step 4: Update `docs/project/deferred-work-backlog.md`**

Append a short note after the "Bulk refactoring / project health checks" bullet in both the Agent API section and the Effort vs. impact callout at the bottom: `(in progress: sub-project 1/4 shipped, see docs/superpowers/specs/2026-09-19-project-health-entities-where-design.md)`.

- [ ] **Step 5: Commit**

```bash
git add docs/project/next-steps.md docs/project/deferred-work-backlog.md
git commit -m "$(cat <<'EOF'
docs: record entities_where's next-steps.md entry, annotate backlog

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01CEBXDEjzbGDpMyLrKCN4mY
EOF
)"
```

---

## Self-Review Notes

- **Spec coverage:** `eq`/`ne` any type, `gt`/`gte`/`lt`/`lte` numeric-only (Task 1's tests 1-2) ✓; graceful non-match for missing component/path/type-mismatch (Task 1's tests 3-5) ✓; `McpToolError` for unknown op and missing required args (Task 1's tests 6-7) ✓; reuses `split_path`, no new dotted-path logic invented (Task 1 Step 4) ✓; no special-casing, added directly to `tool_handlers()` (Task 1 Step 4) ✓; zero CMakeLists changes (confirmed: no `target_link_libraries`/`add_library` edits anywhere) ✓; manual verification + next-steps.md + backlog annotation (Task 2) ✓.
- **Placeholder scan:** no TBD/TODO; every step has real code or an exact command.
- **Type consistency:** `tool_entities_where`'s signature matches the existing `ToolHandler` alias exactly; `json_at_path`/`compare` are new file-local helpers with no naming collision against any existing `mcp_tools.cpp` symbol (verified: no prior `compare`/`json_at_path` in the file).
