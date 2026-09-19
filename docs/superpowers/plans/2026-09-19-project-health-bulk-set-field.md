# Project Health & Bulk Editing — `bulk_set_field` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a 25th MCP tool, `bulk_set_field`, to the existing `mosaic mcp` server: sets one field to a new value on every entity matching an optional filter (reusing `entities_where`'s exact query shape), applied as a single atomic undo step. This is sub-project 4 of 4 — the last one — in the project-health/bulk-editing arc (see `docs/project/deferred-work-backlog.md`).

**Architecture:** Two independently testable units. (1) `CompositeCommand`, a new generic `foundation/commands/` primitive wrapping several `Command`s as one atomic undo step — not MCP-specific, a foundation-tier addition alongside `Command` and `SetComponentCommand`. (2) `bulk_set_field` itself: a two-phase MCP tool that selects matching entities (reusing `entities_where`'s walk/`compare()`), builds a patched `Value` for each via a new write-side `json_set_at` (the counterpart to `entities_where`'s read-side `json_at_path`), and only once every patch is validated does it wrap them into one `CompositeCommand` and execute it. `bulk_set_field` fits the uniform `ToolHandler` shape directly (`World&`/`ComponentReflection&`/`CommandStack&`, all already parameters) — no `handle_tool_call` signature change, unlike `broken_references`/`orphaned_assets`.

**Tech Stack:** C++20, `nlohmann_json`, the existing `AnimationSystem::split_path()` helper, the existing `compare()`/`json_at_path()` helpers from `entities_where`, the existing `SetComponentCommand`. No new CMakeLists changes to `app/CMakeLists.txt` — only `tests/CMakeLists.txt` needs a new test file registered (explicit file list, not a glob).

**Spec:** `docs/superpowers/specs/2026-09-19-project-health-bulk-set-field-design.md`

## Global Constraints

- `json_set_at` requires every path segment (including the leaf) to already exist — it never creates new structure. A `set.field` that doesn't resolve on a matched entity is always `McpToolError`, thrown before any mutation happens.
- Two-phase execution: select + build every patch first; only wrap into one `CompositeCommand` and call `stack.execute()` once every patch is known to succeed. A malformed request must never partially mutate the world.
- Zero matches returns `{"updated": 0, "entities": []}` without touching `CommandStack` at all — no no-op command on the undo stack.
- `where` is optional and, when given, reuses `entities_where`'s exact operators (`eq`/`ne`/`gt`/`gte`/`lt`/`lte`) and its existing `compare()` helper verbatim.
- `CompositeCommand::undo()` reverses sub-commands in reverse order. A single `undo` call after a bulk edit must revert every entity it touched, not just the last one.
- No new CMakeLists changes to `app/CMakeLists.txt`; `tests/CMakeLists.txt` gets exactly one new line.

---

### Task 1: `CompositeCommand`

**Files:**
- Create: `foundation/commands/composite_command.hpp`
- Create: `tests/unit/commands/composite_command_test.cpp`
- Modify: `tests/CMakeLists.txt` — register the new test file.

**Interfaces:**
- Consumes: `mosaic::foundation::commands::Command` (pre-existing, `foundation/commands/command.hpp` — pure-virtual `id()`/`execute()`/`undo()`, with default `parameters()`/`required_permissions()`/`serialize()`), `mosaic::foundation::serialization::Value`/`Array`/`Table` (pre-existing, `foundation/serialization/value.hpp`).
- Produces: `mosaic::foundation::commands::CompositeCommand` — constructor `CompositeCommand(std::string id, std::vector<std::unique_ptr<Command>> commands)`; `execute()` runs every sub-command's `execute()` in order; `undo()` runs every sub-command's `undo()` in reverse order; `parameters()` returns a `serialization::Array` of every sub-command's own `serialize()`. Task 2 constructs one of these directly.

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/commands/composite_command_test.cpp`:

```cpp
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "foundation/commands/command.hpp"
#include "foundation/commands/composite_command.hpp"
#include "foundation/serialization/value.hpp"

using mosaic::foundation::commands::Command;
using mosaic::foundation::commands::CompositeCommand;
namespace ser = mosaic::foundation::serialization;

namespace
{
// A command that appends its tag to a shared trace on execute and removes it on
// undo, so a test can observe execution/undo order -- the same idiom
// tests/unit/commands/command_stack_test.cpp already uses for CommandStack itself.
class AppendCommand final : public Command
{
public:
    AppendCommand(std::vector<std::string>& trace, std::string tag)
        : trace_(trace), tag_(std::move(tag))
    {
    }

    std::string id() const override { return "test.append." + tag_; }
    void execute() override { trace_.push_back(tag_); }
    void undo() override { trace_.pop_back(); }

private:
    std::vector<std::string>& trace_;
    std::string tag_;
};
} // namespace

TEST_CASE("CompositeCommand::execute runs every sub-command in order", "[commands]")
{
    std::vector<std::string> trace;
    std::vector<std::unique_ptr<Command>> subs;
    subs.push_back(std::make_unique<AppendCommand>(trace, "a"));
    subs.push_back(std::make_unique<AppendCommand>(trace, "b"));
    subs.push_back(std::make_unique<AppendCommand>(trace, "c"));

    CompositeCommand composite("test.batch", std::move(subs));
    composite.execute();

    REQUIRE(trace == std::vector<std::string>{"a", "b", "c"});
}

TEST_CASE("CompositeCommand::undo reverses in reverse order", "[commands]")
{
    std::vector<std::string> trace;
    std::vector<std::unique_ptr<Command>> subs;
    subs.push_back(std::make_unique<AppendCommand>(trace, "a"));
    subs.push_back(std::make_unique<AppendCommand>(trace, "b"));
    subs.push_back(std::make_unique<AppendCommand>(trace, "c"));

    CompositeCommand composite("test.batch", std::move(subs));
    composite.execute();
    REQUIRE(trace == std::vector<std::string>{"a", "b", "c"});

    composite.undo();
    REQUIRE(trace.empty());
}

TEST_CASE("CompositeCommand::id returns the id given at construction", "[commands]")
{
    std::vector<std::unique_ptr<Command>> subs;
    CompositeCommand composite("mcp.bulk_set_field", std::move(subs));
    REQUIRE(composite.id() == "mcp.bulk_set_field");
}

TEST_CASE("CompositeCommand::parameters projects every sub-command's own serialize()", "[commands]")
{
    std::vector<std::string> trace;
    std::vector<std::unique_ptr<Command>> subs;
    subs.push_back(std::make_unique<AppendCommand>(trace, "a"));
    subs.push_back(std::make_unique<AppendCommand>(trace, "b"));

    CompositeCommand composite("test.batch", std::move(subs));
    ser::Value params = composite.parameters();

    REQUIRE(params.is<ser::Array>());
    const ser::Array& arr = params.as<ser::Array>();
    REQUIRE(arr.size() == 2);
    REQUIRE(arr[0].is<ser::Table>());
    const ser::Value* id0 = arr[0].as<ser::Table>().find("id");
    REQUIRE(id0 != nullptr);
    REQUIRE(id0->as<std::string>() == "test.append.a");
    const ser::Value* id1 = arr[1].as<ser::Table>().find("id");
    REQUIRE(id1 != nullptr);
    REQUIRE(id1->as<std::string>() == "test.append.b");
}

TEST_CASE("CompositeCommand with zero sub-commands is a safe no-op", "[commands]")
{
    std::vector<std::unique_ptr<Command>> subs;
    CompositeCommand composite("test.empty", std::move(subs));
    composite.execute(); // no crash
    composite.undo();    // no crash
    REQUIRE(composite.id() == "test.empty");
}
```

Register the new file in `tests/CMakeLists.txt`, right after the existing `unit/commands/set_component_command_test.cpp` line:

```cmake
  unit/commands/command_stack_test.cpp
  unit/commands/command_event_integration_test.cpp
  unit/commands/set_component_command_test.cpp
  unit/commands/composite_command_test.cpp
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build -j4`
Expected: FAIL to build — `foundation/commands/composite_command.hpp` doesn't exist yet.

- [ ] **Step 3: Create `foundation/commands/composite_command.hpp`**

```cpp
#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "foundation/commands/command.hpp"
#include "foundation/serialization/value.hpp"

namespace mosaic::foundation::commands
{

// Wraps several Commands as one atomic undo step: execute() runs each sub-command in
// order, undo() reverses in reverse order (undoing the last-applied change first). Not
// tool-specific -- any caller needing N mutations to revert as a single "undo" call
// (e.g. an MCP bulk-edit tool touching many entities) constructs one of these instead
// of pushing N separate commands onto the CommandStack.
class CompositeCommand final : public Command
{
public:
    CompositeCommand(std::string id, std::vector<std::unique_ptr<Command>> commands)
        : id_(std::move(id)), commands_(std::move(commands))
    {
    }

    std::string id() const override { return id_; }

    void execute() override
    {
        for (auto& c : commands_) { c->execute(); }
    }

    void undo() override
    {
        for (auto it = commands_.rbegin(); it != commands_.rend(); ++it) { (*it)->undo(); }
    }

    // Projects every sub-command's own serialize() into an array -- an agent/human
    // inspecting the transaction log sees the full batch, not an opaque blob.
    serialization::Value parameters() const override
    {
        serialization::Array arr;
        for (const auto& c : commands_) { arr.push_back(c->serialize()); }
        return serialization::Value{std::move(arr)};
    }

private:
    std::string id_;
    std::vector<std::unique_ptr<Command>> commands_;
};

} // namespace mosaic::foundation::commands
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build -j4 --target mosaic_unit_tests && ./build/tests/mosaic_unit_tests "CompositeCommand*"`
Expected: PASS — all 5 new test cases.

- [ ] **Step 5: Commit**

```bash
git add foundation/commands/composite_command.hpp tests/unit/commands/composite_command_test.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(commands): add CompositeCommand, an atomic multi-command batch

Wraps several Commands as one undo step: execute() runs each
sub-command in order, undo() reverses in reverse order. A generic
foundation/commands/ primitive, not MCP-specific -- any caller
needing N mutations to revert as a single undo call can reuse it
directly.

First consumer: the upcoming bulk_set_field MCP tool (sub-project 4
of 4 in the project-health/bulk-editing arc), which needs a bulk edit
touching many entities to revert atomically rather than requiring N
separate undo calls.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01CEBXDEjzbGDpMyLrKCN4mY
EOF
)"
```

---

### Task 2: `bulk_set_field` tool

**Files:**
- Modify: `app/mcp/mcp_tools.hpp` — doc-comment tool count 24 → 25. No signature changes.
- Modify: `app/mcp/mcp_tools.cpp` — new include, a new `json_set_at` helper, a new `tool_bulk_set_field`, a `tool_handlers()` entry, a `tools_list_schema()` entry, a new `schema_bulk_set_field()` helper.
- Test: `tests/unit/app/mcp_tools_test.cpp` (extended) — new test cases, existing tool-count assertion (24 → 25).
- Test: `tests/unit/app/mcp_protocol_test.cpp` (extended) — existing tool-count assertion (24 → 25).

**Interfaces:**
- Consumes: `mosaic::foundation::commands::CompositeCommand` (Task 1, `foundation/commands/composite_command.hpp` — constructor `CompositeCommand(std::string, std::vector<std::unique_ptr<Command>>)`), the existing `json_at_path`/`compare`/`require_string`/`value_to_json`/`json_to_value` helpers already in `mcp_tools.cpp` (from `entities_where`), the existing `mosaic::engine::commands::SetComponentCommand` (already included via `engine/commands/set_component_command.hpp`), `mosaic::engine::animation::split_path` (already included).
- Produces: no new public function signature or parameter — `bulk_set_field` fits the existing `ToolHandler` shape as-is, added to the existing `tool_handlers()` map.

- [ ] **Step 1: Write the failing tests**

Append these test cases to `tests/unit/app/mcp_tools_test.cpp` (uses the plain `Fixture`, not `RuntimeFixture` — no `AssetRegistry` involvement):

```cpp
TEST_CASE("mcp_tools: bulk_set_field updates only entities matching the where filter", "[app][mcp]")
{
    Fixture f;
    const std::uint64_t a = f.call("create_entity").at("id").get<std::uint64_t>();
    const std::uint64_t b = f.call("create_entity").at("id").get<std::uint64_t>();
    const std::uint64_t c = f.call("create_entity").at("id").get<std::uint64_t>();
    for (std::uint64_t eid : {a, b, c})
    {
        f.call("add_component", {{"entity", eid}, {"component", "Name"}});
    }
    f.call("set_component", {{"entity", a}, {"component", "Name"}, {"value", {{"value", "OldName"}}}});
    f.call("set_component", {{"entity", b}, {"component", "Name"}, {"value", {{"value", "OldName"}}}});
    f.call("set_component", {{"entity", c}, {"component", "Name"}, {"value", {{"value", "Untouched"}}}});

    const nlohmann::json result = f.call("bulk_set_field",
        {{"component", "Name"},
         {"where", {{"field", "value"}, {"op", "eq"}, {"value", "OldName"}}},
         {"set", {{"field", "value"}, {"value", "NewName"}}}});
    REQUIRE(result.at("updated").get<std::uint64_t>() == 2);
    const nlohmann::json entities = result.at("entities");
    REQUIRE(entities.size() == 2);

    REQUIRE(f.call("get_component", {{"entity", a}, {"component", "Name"}}).at("value").at("value").get<std::string>() == "NewName");
    REQUIRE(f.call("get_component", {{"entity", b}, {"component", "Name"}}).at("value").at("value").get<std::string>() == "NewName");
    REQUIRE(f.call("get_component", {{"entity", c}, {"component", "Name"}}).at("value").at("value").get<std::string>() == "Untouched");
}

TEST_CASE("mcp_tools: bulk_set_field with no where updates every entity with the component", "[app][mcp]")
{
    Fixture f;
    const std::uint64_t a = f.call("create_entity").at("id").get<std::uint64_t>();
    const std::uint64_t b = f.call("create_entity").at("id").get<std::uint64_t>();
    f.call("add_component", {{"entity", a}, {"component", "Name"}});
    f.call("add_component", {{"entity", b}, {"component", "Name"}});

    const nlohmann::json result = f.call("bulk_set_field",
        {{"component", "Name"}, {"set", {{"field", "value"}, {"value", "Renamed"}}}});
    REQUIRE(result.at("updated").get<std::uint64_t>() == 2);

    REQUIRE(f.call("get_component", {{"entity", a}, {"component", "Name"}}).at("value").at("value").get<std::string>() == "Renamed");
    REQUIRE(f.call("get_component", {{"entity", b}, {"component", "Name"}}).at("value").at("value").get<std::string>() == "Renamed");
}

TEST_CASE("mcp_tools: bulk_set_field with zero matches returns updated:0 and touches no undo stack", "[app][mcp]")
{
    Fixture f;
    const std::uint64_t eid = f.call("create_entity").at("id").get<std::uint64_t>();
    f.call("add_component", {{"entity", eid}, {"component", "Name"}});
    f.call("set_component", {{"entity", eid}, {"component", "Name"}, {"value", {{"value", "Something"}}}});
    REQUIRE(f.call("undo_depth").at("depth").get<std::uint64_t>() == 1);

    const nlohmann::json result = f.call("bulk_set_field",
        {{"component", "Name"},
         {"where", {{"field", "value"}, {"op", "eq"}, {"value", "NoSuchValue"}}},
         {"set", {{"field", "value"}, {"value", "NewName"}}}});
    REQUIRE(result.at("updated").get<std::uint64_t>() == 0);
    REQUIRE(result.at("entities").empty());
    REQUIRE(f.call("undo_depth").at("depth").get<std::uint64_t>() == 1); // unchanged -- no command pushed
}

TEST_CASE("mcp_tools: bulk_set_field throws for an unresolvable set.field and mutates nothing", "[app][mcp]")
{
    Fixture f;
    const std::uint64_t eid = f.call("create_entity").at("id").get<std::uint64_t>();
    f.call("add_component", {{"entity", eid}, {"component", "Name"}});
    f.call("set_component", {{"entity", eid}, {"component", "Name"}, {"value", {{"value", "Original"}}}});

    REQUIRE_THROWS_AS(f.call("bulk_set_field",
        {{"component", "Name"}, {"set", {{"field", "not_a_real_field"}, {"value", "X"}}}}), McpToolError);

    // Nothing mutated: value unchanged, no command pushed.
    REQUIRE(f.call("get_component", {{"entity", eid}, {"component", "Name"}}).at("value").at("value").get<std::string>() == "Original");
    REQUIRE(f.call("undo_depth").at("depth").get<std::uint64_t>() == 1); // only the earlier set_component
}

TEST_CASE("mcp_tools: bulk_set_field is a single atomic undo step across multiple entities", "[app][mcp]")
{
    Fixture f;
    const std::uint64_t a = f.call("create_entity").at("id").get<std::uint64_t>();
    const std::uint64_t b = f.call("create_entity").at("id").get<std::uint64_t>();
    const std::uint64_t c = f.call("create_entity").at("id").get<std::uint64_t>();
    for (std::uint64_t eid : {a, b, c})
    {
        f.call("add_component", {{"entity", eid}, {"component", "Name"}});
        f.call("set_component", {{"entity", eid}, {"component", "Name"}, {"value", {{"value", "Before"}}}});
    }
    REQUIRE(f.call("undo_depth").at("depth").get<std::uint64_t>() == 3);

    const nlohmann::json result = f.call("bulk_set_field",
        {{"component", "Name"}, {"set", {{"field", "value"}, {"value", "After"}}}});
    REQUIRE(result.at("updated").get<std::uint64_t>() == 3);
    REQUIRE(f.call("undo_depth").at("depth").get<std::uint64_t>() == 4); // 3 prior sets + 1 composite

    REQUIRE(f.call("undo").at("ok").get<bool>() == true); // one undo call
    for (std::uint64_t eid : {a, b, c})
    {
        REQUIRE(f.call("get_component", {{"entity", eid}, {"component", "Name"}}).at("value").at("value").get<std::string>() == "Before");
    }

    REQUIRE(f.call("redo").at("ok").get<bool>() == true); // one redo call re-applies the whole batch
    for (std::uint64_t eid : {a, b, c})
    {
        REQUIRE(f.call("get_component", {{"entity", eid}, {"component", "Name"}}).at("value").at("value").get<std::string>() == "After");
    }
}

TEST_CASE("mcp_tools: bulk_set_field throws McpToolError for missing required arguments", "[app][mcp]")
{
    Fixture f;
    REQUIRE_THROWS_AS(f.call("bulk_set_field", {{"set", {{"field", "value"}, {"value", "X"}}}}), McpToolError); // missing component
    REQUIRE_THROWS_AS(f.call("bulk_set_field", {{"component", "Name"}}), McpToolError); // missing set
    REQUIRE_THROWS_AS(f.call("bulk_set_field", {{"component", "Name"}, {"set", {{"value", "X"}}}}), McpToolError); // missing set.field
    REQUIRE_THROWS_AS(f.call("bulk_set_field", {{"component", "Name"}, {"set", {{"field", "value"}}}}), McpToolError); // missing set.value
    REQUIRE_THROWS_AS(f.call("bulk_set_field",
        {{"component", "Name"}, {"where", {{"field", "value"}, {"value", "X"}}}, {"set", {{"field", "value"}, {"value", "Y"}}}}),
        McpToolError); // missing where.op
    REQUIRE_THROWS_AS(f.call("bulk_set_field",
        {{"component", "Name"}, {"where", {{"field", "value"}, {"op", "regex"}, {"value", "X"}}}, {"set", {{"field", "value"}, {"value", "Y"}}}}),
        McpToolError); // unknown where.op
}
```

Update the existing `tools_list_schema` count test — both occurrences of `24` become `25`:

```cpp
TEST_CASE("mcp_tools: tools_list_schema lists all 25 v1 tools with a description and inputSchema", "[app][mcp]")
{
    const nlohmann::json schema = tools_list_schema();
    REQUIRE(schema.contains("tools"));
    const nlohmann::json tools = schema.at("tools");
    REQUIRE(tools.size() == 25);
```
(leave the rest of that test case's body unchanged).

In `tests/unit/app/mcp_protocol_test.cpp`, update the one existing count assertion:

```cpp
    REQUIRE(parsed.at("result").at("tools").size() == 25);
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build -j4`
Expected: FAIL to build — `bulk_set_field` isn't a registered MCP tool yet.

- [ ] **Step 3: Update `mcp_tools.hpp`**

Change the `tools_list_schema()` doc comment:

```cpp
// The MCP `tools/list` response body: { "tools": [ { name, description, inputSchema }, ... ] }
// for all 25 v1 tools (entity/component CRUD + undo/redo/save_scene + describe_scene +
// set_script + import_asset + tick + build_project + script_profile + entities_where +
// broken_references + orphaned_assets + bulk_set_field).
nlohmann::json tools_list_schema();
```

No other changes to this file — `handle_tool_call`'s declaration is unchanged (`bulk_set_field` needs no dependency beyond `World&`/`ComponentReflection&`/`CommandStack&`, all already parameters).

- [ ] **Step 4: Update `mcp_tools.cpp`**

Add the include, alongside the other `foundation/`-tier includes:

```cpp
#include "foundation/commands/composite_command.hpp"
```

Add usings, alongside the existing ones in the anonymous namespace:

```cpp
using mosaic::foundation::commands::Command;
using mosaic::foundation::commands::CompositeCommand;
```

Add `json_set_at`, placed right after `json_at_path` and before `compare`:

```cpp
bool json_set_at(nlohmann::json& root, const std::vector<std::string>& path, const nlohmann::json& value)
{
    nlohmann::json* cur = &root;
    for (std::size_t i = 0; i + 1 < path.size(); ++i)
    {
        if (!cur->is_object() || !cur->contains(path[i])) { return false; }
        cur = &cur->at(path[i]);
    }
    if (path.empty() || !cur->is_object() || !cur->contains(path.back())) { return false; }
    (*cur)[path.back()] = value;
    return true;
}
```

Add `tool_bulk_set_field`, placed right after `tool_orphaned_assets` and before `tool_handlers()`:

```cpp
nlohmann::json tool_bulk_set_field(World& world, const ComponentReflection& types, CommandStack& stack,
                                   const std::string&, const nlohmann::json& args)
{
    const std::string component = require_string(args, "component");
    if (!args.contains("set") || !args.at("set").is_object())
    {
        throw McpToolError("missing or invalid 'set' (expected an object with 'field' and 'value')");
    }
    const nlohmann::json& set_args = args.at("set");
    const std::string set_field = require_string(set_args, "field");
    if (!set_args.contains("value"))
    {
        throw McpToolError("missing 'set.value'");
    }
    const nlohmann::json set_value = set_args.at("value");
    const std::vector<std::string> set_path = mosaic::engine::animation::split_path(set_field, '.');

    std::optional<std::string> where_op;
    nlohmann::json where_value;
    std::vector<std::string> where_path;
    const bool has_where = args.contains("where");
    if (has_where)
    {
        if (!args.at("where").is_object())
        {
            throw McpToolError("invalid 'where' (expected an object with 'field', 'op', 'value')");
        }
        const nlohmann::json& where_args = args.at("where");
        const std::string where_field = require_string(where_args, "field");
        where_op = require_string(where_args, "op");
        if (!where_args.contains("value"))
        {
            throw McpToolError("missing 'where.value'");
        }
        static const std::unordered_set<std::string> kValidOps = {"eq", "ne", "gt", "gte", "lt", "lte"};
        if (kValidOps.find(*where_op) == kValidOps.end())
        {
            throw McpToolError("unknown where.op: " + *where_op + " (expected one of eq, ne, gt, gte, lt, lte)");
        }
        where_value = where_args.at("value");
        where_path = mosaic::engine::animation::split_path(where_field, '.');
    }

    std::vector<EntityID> matched;
    std::vector<Value> patched;
    world.for_each_entity(
        [&](EntityID e)
        {
            const auto v = types.component_value(world, e, component);
            if (!v) { return; }
            nlohmann::json component_json = value_to_json(*v);
            if (has_where)
            {
                const auto field_value = json_at_path(component_json, where_path);
                if (!field_value) { return; }
                if (!compare(*field_value, *where_op, where_value)) { return; }
            }
            if (!json_set_at(component_json, set_path, set_value))
            {
                throw McpToolError("'set.field' does not resolve on component '" + component + "': " + set_field);
            }
            matched.push_back(e);
            patched.push_back(json_to_value(component_json));
        });

    if (matched.empty())
    {
        return {{"updated", 0}, {"entities", nlohmann::json::array()}};
    }

    std::vector<std::unique_ptr<Command>> commands;
    nlohmann::json updated_entities = nlohmann::json::array();
    for (std::size_t i = 0; i < matched.size(); ++i)
    {
        commands.push_back(std::make_unique<mosaic::engine::commands::SetComponentCommand>(
            world, types, matched[i], component, patched[i]));
        updated_entities.push_back(matched[i].value());
    }
    stack.execute(std::make_unique<CompositeCommand>("mcp.bulk_set_field", std::move(commands)));

    return {{"updated", static_cast<std::uint64_t>(matched.size())}, {"entities", updated_entities}};
}
```

Add `"bulk_set_field"` to `tool_handlers()`'s initializer list:

```cpp
        {"script_profile", &tool_script_profile},
        {"bulk_set_field", &tool_bulk_set_field},
```

Add the new schema helper, right after `schema_entities_where()`:

```cpp
nlohmann::json schema_bulk_set_field()
{
    nlohmann::json where_schema = {
        {"type", "object"},
        {"properties",
         {{"field", {{"type", "string"}}},
          {"op", {{"type", "string"}, {"enum", {"eq", "ne", "gt", "gte", "lt", "lte"}}}},
          {"value", {{"description", "The value to compare against"}}}}},
        {"required", {"field", "op", "value"}}};
    nlohmann::json set_schema = {
        {"type", "object"},
        {"properties",
         {{"field", {{"type", "string"},
                    {"description", "Dotted path into the component's fields, e.g. \"value\" or \"position.x\""}}},
          {"value", {{"description", "The new value to write at that field"}}}}},
        {"required", {"field", "value"}}};
    return {{"type", "object"},
           {"properties",
            {{"component", {{"type", "string"}}},
             {"where", where_schema},
             {"set", set_schema}}},
           {"required", {"component", "set"}}};
}
```

Add the `tools_list_schema()` entry, after the `orphaned_assets` entry (last one, right before `return {{"tools", tools}};`):

```cpp
    tools.push_back({{"name", "bulk_set_field"},
                     {"description", "Set one field to a new value on every entity matching an "
                                     "optional filter (reusing entities_where's exact "
                                     "where.field/op/value shape; omit 'where' to target every "
                                     "entity with 'component'). Applied atomically as one "
                                     "undoable step -- a single undo call reverts every entity "
                                     "this touched, not just the last one. Covers bulk renames "
                                     "(e.g. set field \"value\" on Name) as the common case. A "
                                     "'set.field' that doesn't resolve on the matched component "
                                     "throws before anything is mutated; zero matches returns "
                                     "updated:0 without touching the undo stack."},
                     {"inputSchema", schema_bulk_set_field()}});
```

`handle_tool_call` itself needs no changes — `bulk_set_field` reaches it through the existing generic `tool_handlers()` lookup path.

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build -j4 --target mosaic_unit_tests && ./build/tests/mosaic_unit_tests "[mcp]"`
Expected: PASS — all 6 new `bulk_set_field` cases, plus every pre-existing `[mcp]` case with the updated 25-tool counts. No failures.

- [ ] **Step 6: Commit**

```bash
git add app/mcp/mcp_tools.hpp app/mcp/mcp_tools.cpp \
       tests/unit/app/mcp_tools_test.cpp tests/unit/app/mcp_protocol_test.cpp
git commit -m "$(cat <<'EOF'
feat(mcp): add bulk_set_field, a 25th Agent API tool

Sets one field to a new value on every entity matching an optional
filter, reusing entities_where's exact query shape and operators.
Two-phase execution -- select + build every patch first, only mutate
once every patch is validated -- means a malformed set.field throws
McpToolError before touching anything. Applied as one CompositeCommand
so a single undo call reverts the whole batch, not just the last
entity touched.

This is sub-project 4 of 4, the LAST one, in the project-health/
bulk-editing arc (see docs/project/deferred-work-backlog.md). "mass
rename" is covered as the flagship set-field use case (e.g. setting
Name.value); find-replace remains explicit future work.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01CEBXDEjzbGDpMyLrKCN4mY
EOF
)"
```

---

### Task 3: Manual verification + `next-steps.md` + backlog entry (arc completion)

**Files:**
- Modify: `docs/project/next-steps.md` — new dated entry.
- Modify: `docs/project/deferred-work-backlog.md` — mark the whole "Bulk refactoring / project health checks" item complete (all 4 sub-projects shipped).

**Interfaces:**
- Consumes: `bulk_set_field` via the real `mosaic mcp` CLI (Task 2).
- Produces: nothing new; verification + documentation only.

- [ ] **Step 1: Manually verify against a real project**

```bash
cmake --build build -j4 --target mosaic
rm -rf /tmp/bulk_set_field_verify && ./build/app/mosaic create /tmp/bulk_set_field_verify
```

Exercise the full round trip in one continuous `mosaic mcp` session: create two entities, add `Name` to both, set both to the same starting value, run `bulk_set_field` with a `where` filter matching that value to rename both at once, confirm the response's `updated`/`entities`, then `get_component` each to confirm the new value, then `undo` once and `get_component` each again to confirm BOTH entities reverted from a single `undo` call (not just one of them) -- this is the core behavior this whole sub-project exists to prove, so confirm it against the real CLI, not just the unit tests.

```bash
printf '%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"create_entity","arguments":{}}}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"create_entity","arguments":{}}}' \
  | ./build/app/mosaic mcp /tmp/bulk_set_field_verify
```

Note the returned entity ids from that call, then continue the same style of session (add_component, set_component, bulk_set_field, get_component, undo, get_component) substituting the real ids -- inspect every response by eye for a sane shape and no crash.

- [ ] **Step 2: Run the full test suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: green aside from the known pre-existing flakes (the `--host`/`--connect` `ctest`-discovery quirk, and the threaded-Python-teardown flake). Zero new failures.

- [ ] **Step 3: Add the dated `next-steps.md` entry**

Prepend a new dated paragraph to the top of the changelog blob in `docs/project/next-steps.md` (immediately before the current most-recent `**Last updated:**` entry, matching the existing style), summarizing: `bulk_set_field` ships as sub-project 4 of 4 -- the LAST one -- in the project-health/bulk-editing arc (from `docs/project/deferred-work-backlog.md`), closing the arc out entirely; a 25th MCP tool reusing `entities_where`'s query shape for selection; a new generic `CompositeCommand` foundation-tier primitive makes the bulk edit one atomic undo step; two-phase build-then-apply execution means a malformed request never partially mutates; "mass rename" covered as the flagship set-field use case, find-replace explicitly deferred as real future work. Link this plan and the spec, and note the arc (all 4 sub-projects) is now complete.

- [ ] **Step 4: Update `docs/project/deferred-work-backlog.md`**

Update the existing "(in progress: sub-project 3/4 shipped, ...)" annotations (both the Agent API section bullet and the Effort vs. impact table's callout) to mark the item fully shipped -- all 4 sub-projects complete -- pointing at this spec, e.g. `(shipped: all 4 sub-projects complete, see docs/superpowers/specs/2026-09-19-project-health-bulk-set-field-design.md)`.

- [ ] **Step 5: Commit**

```bash
git add docs/project/next-steps.md docs/project/deferred-work-backlog.md
git commit -m "$(cat <<'EOF'
docs: record bulk_set_field's next-steps.md entry, close out the arc

Sub-project 4 of 4 -- the last one -- in the project-health/
bulk-editing arc. All 4 sub-projects (entities_where,
broken_references, orphaned_assets, bulk_set_field) are now shipped.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01CEBXDEjzbGDpMyLrKCN4mY
EOF
)"
```

---

## Self-Review Notes

- **Spec coverage:** `CompositeCommand` as a standalone, generic foundation-tier primitive with its own test file (Task 1) ✓; `where`/`set` schema exactly as specified, `where` optional (Task 2 Step 4's `tool_bulk_set_field`, `has_where` branch) ✓; `json_set_at` requires the full path to already exist, never creates structure (Task 2 Step 4's `json_set_at`) ✓; two-phase build-then-apply, throwing before any mutation on an unresolvable `set.field` (Task 2 Step 4's `tool_bulk_set_field`: the `for_each_entity` loop only builds `matched`/`patched`, no command executes until after the loop) ✓; zero matches skips `CommandStack` entirely (Task 2 Step 4's early return before constructing any `Command`) ✓; single atomic undo step (Task 2's 5th test case, `CompositeCommand` from Task 1) ✓; fits `ToolHandler` directly, no `handle_tool_call` signature change (Task 2 Step 3 makes no signature edits) ✓; manual verification proves the atomic-undo behavior against the real CLI, not just unit tests (Task 3 Step 1) ✓; arc-completion backlog annotation (Task 3 Step 4) ✓.
- **Placeholder scan:** no TBD/TODO; every step has real code or an exact command.
- **Type consistency:** `CompositeCommand`'s constructor signature (`std::string, std::vector<std::unique_ptr<Command>>`) matches exactly between Task 1's implementation and Task 2's construction call (`CompositeCommand("mcp.bulk_set_field", std::move(commands))`); `tool_bulk_set_field`'s signature (`World&, const ComponentReflection&, CommandStack&, const std::string&, const nlohmann::json&`) matches the existing `ToolHandler` alias exactly (the same signature `tool_entities_where`/`tool_script_profile` already use, just with `CommandStack&` actually consumed this time instead of ignored); `json_set_at`'s signature and behavior match `json_at_path`'s existing established shape (dotted-path `std::vector<std::string>`, `nlohmann::json` in/out) with no invented conventions.
