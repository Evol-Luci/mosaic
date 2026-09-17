# Scene Reasoning — `describe_scene` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a 16th MCP tool, `describe_scene`, to the existing `mosaic mcp` server: a parameterless snapshot of the scene's shape (entity count, a component-type histogram, and the `Transform` parent-child hierarchy as a tree).

**Architecture:** One new handler function in the existing `app/mcp/mcp_tools.cpp`, registered in the existing `tool_handlers()` map and `tools_list_schema()` — no new files, no protocol changes. Reads `Transform`/`Name` directly via `World::get_component<T>()` (both concrete, statically-known types), unlike the generic string-keyed CRUD tools which go through `ComponentReflection`.

**Tech Stack:** C++20, `nlohmann_json`, Catch2. No new dependencies.

**Spec:** `docs/superpowers/specs/2026-09-16-scene-reasoning-describe-scene-design.md`

## Global Constraints

- No parameters, no new files: `describe_scene` is added entirely inside `app/mcp/mcp_tools.cpp`/`.hpp` and its existing test file.
- `hierarchy` is rooted at every `Transform`-having entity whose `parent` is nil; entities with no `Transform` at all are excluded from `hierarchy` but still counted in `entity_count`/`component_counts`.
- A cycle among non-root entities must never hang or crash the tool — it's simply unreachable from any root and silently absent from `hierarchy`.
- `component_counts` omits a registered component name with zero entities (no zero-valued entries).
- `name` in a hierarchy node is the entity's `Name.value` if present, else JSON `null` — never a synthesized fallback string.

---

### Task 1: `describe_scene` tool

**Files:**
- Modify: `app/mcp/mcp_tools.hpp` — update `tools_list_schema()`'s doc comment from "15 v1 tools" to "16 v1 tools".
- Modify: `app/mcp/mcp_tools.cpp` — new handler, schema entry, registry entry.
- Test: `tests/unit/app/mcp_tools_test.cpp` (extended, not new).

**Interfaces:**
- Consumes: `mosaic::engine::World::get_component<T>(EntityID)` (`engine/world/world.hpp`, pre-existing), `mosaic::engine::transform::Transform{position, rotation, scale, parent}` (`engine/transform/transform.hpp`, pre-existing), `mosaic::engine::Name{value}` (`engine/world/name.hpp`, pre-existing), `mosaic::engine::reflection::ComponentReflection::components_on` (Task 1 of the prior plan, already in use by other handlers in this file).
- Produces: a new `"describe_scene"` entry in `handle_tool_call`'s dispatch table — no new public function signature; existing callers (`app/run/mcp.cpp`) need no changes at all, since they already call the tool by name through the existing `handle_tool_call` entry point.

- [ ] **Step 1: Write the failing tests**

`tests/unit/app/mcp_tools_test.cpp` already includes `engine/world/name.hpp` (used by an earlier test) — add `#include "engine/transform/transform.hpp"` alongside it (don't duplicate the `Name` include).

Append these 6 test cases:

```cpp
TEST_CASE("mcp_tools: describe_scene on an empty world", "[app][mcp]")
{
    Fixture f;
    const nlohmann::json result = f.call("describe_scene");
    REQUIRE(result.at("entity_count").get<std::uint64_t>() == 0);
    REQUIRE(result.at("component_counts").empty());
    REQUIRE(result.at("hierarchy").empty());
}

TEST_CASE("mcp_tools: describe_scene lists every Transform-having entity with no parent as a root", "[app][mcp]")
{
    Fixture f;
    using mosaic::engine::transform::Transform;
    const World::EntityID a = f.world.create_entity();
    const World::EntityID b = f.world.create_entity();
    f.world.add_component(a, Transform{});
    f.world.add_component(b, Transform{});

    const nlohmann::json result = f.call("describe_scene");
    REQUIRE(result.at("entity_count").get<std::uint64_t>() == 2);
    REQUIRE(result.at("component_counts").at("Transform").get<std::uint64_t>() == 2);
    REQUIRE(result.at("hierarchy").size() == 2);
    for (const auto& node : result.at("hierarchy"))
    {
        REQUIRE(node.at("children").empty());
        REQUIRE(node.at("name").is_null());
    }
}

TEST_CASE("mcp_tools: describe_scene nests a parent-child-grandchild Transform chain", "[app][mcp]")
{
    Fixture f;
    using mosaic::engine::transform::Transform;
    const World::EntityID root = f.world.create_entity();
    const World::EntityID child = f.world.create_entity();
    const World::EntityID grandchild = f.world.create_entity();

    Transform root_t;
    f.world.add_component(root, root_t);
    Transform child_t;
    child_t.parent = root;
    f.world.add_component(child, child_t);
    Transform grandchild_t;
    grandchild_t.parent = child;
    f.world.add_component(grandchild, grandchild_t);

    const nlohmann::json result = f.call("describe_scene");
    REQUIRE(result.at("hierarchy").size() == 1);
    const nlohmann::json& root_node = result.at("hierarchy").at(0);
    REQUIRE(root_node.at("entity").get<std::uint64_t>() == root.value());
    REQUIRE(root_node.at("children").size() == 1);
    const nlohmann::json& child_node = root_node.at("children").at(0);
    REQUIRE(child_node.at("entity").get<std::uint64_t>() == child.value());
    REQUIRE(child_node.at("children").size() == 1);
    REQUIRE(child_node.at("children").at(0).at("entity").get<std::uint64_t>() == grandchild.value());
}

TEST_CASE("mcp_tools: describe_scene reports a Name-labeled entity's name, null otherwise", "[app][mcp]")
{
    Fixture f;
    using mosaic::engine::transform::Transform;
    using mosaic::engine::Name;
    const World::EntityID labeled = f.world.create_entity();
    const World::EntityID unlabeled = f.world.create_entity();
    f.world.add_component(labeled, Transform{});
    f.world.add_component(labeled, Name{"Player"});
    f.world.add_component(unlabeled, Transform{});

    const nlohmann::json result = f.call("describe_scene");
    bool found_labeled = false;
    bool found_unlabeled = false;
    for (const auto& node : result.at("hierarchy"))
    {
        if (node.at("entity").get<std::uint64_t>() == labeled.value())
        {
            REQUIRE(node.at("name").get<std::string>() == "Player");
            found_labeled = true;
        }
        if (node.at("entity").get<std::uint64_t>() == unlabeled.value())
        {
            REQUIRE(node.at("name").is_null());
            found_unlabeled = true;
        }
    }
    REQUIRE(found_labeled);
    REQUIRE(found_unlabeled);
}

TEST_CASE("mcp_tools: describe_scene excludes Transform-less entities from hierarchy but counts them", "[app][mcp]")
{
    Fixture f;
    using mosaic::engine::Name;
    const World::EntityID loose = f.world.create_entity();
    f.world.add_component(loose, Name{"Loose"});

    const nlohmann::json result = f.call("describe_scene");
    REQUIRE(result.at("entity_count").get<std::uint64_t>() == 1);
    REQUIRE(result.at("component_counts").at("Name").get<std::uint64_t>() == 1);
    REQUIRE_FALSE(result.at("component_counts").contains("Transform"));
    REQUIRE(result.at("hierarchy").empty());
}

TEST_CASE("mcp_tools: describe_scene silently omits a Transform cycle from hierarchy without hanging", "[app][mcp]")
{
    Fixture f;
    using mosaic::engine::transform::Transform;
    const World::EntityID a = f.world.create_entity();
    const World::EntityID b = f.world.create_entity();
    Transform ta;
    ta.parent = b;
    Transform tb;
    tb.parent = a;
    f.world.add_component(a, ta);
    f.world.add_component(b, tb);

    const nlohmann::json result = f.call("describe_scene");
    REQUIRE(result.at("entity_count").get<std::uint64_t>() == 2);
    REQUIRE(result.at("component_counts").at("Transform").get<std::uint64_t>() == 2);
    REQUIRE(result.at("hierarchy").empty());
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build -j"$(nproc)" --target mosaic_unit_tests`
Expected: FAIL — `handle_tool_call` throws `McpToolError("unknown tool: describe_scene")` in every new case (`describe_scene` isn't registered yet).

- [ ] **Step 3: Add the handler, schema entry, and registry entry**

In `app/mcp/mcp_tools.cpp`, add two includes near the top (alongside the existing engine includes):

```cpp
#include "engine/transform/transform.hpp"
#include "engine/world/name.hpp"
```

Add `#include <functional>` alongside the existing `<memory>`/`<unordered_map>` includes (needed for the recursive tree-builder lambda below).

Add the handler function, placed after `tool_save_scene` and before `tool_handlers()`:

```cpp
nlohmann::json tool_describe_scene(World& world, const ComponentReflection& types, CommandStack&,
                                   const std::string&, const nlohmann::json&)
{
    // component_counts: one pass, tallying components_on() per entity -- avoids
    // O(registered_names x entities) that a per-name re-scan would cost.
    std::unordered_map<std::string, std::uint64_t> counts;
    world.for_each_entity(
        [&](EntityID e)
        {
            for (const std::string& name : types.components_on(world, e)) { ++counts[name]; }
        });
    nlohmann::json component_counts = nlohmann::json::object();
    for (const auto& [name, count] : counts) { component_counts[name] = count; }

    // hierarchy: one pass collecting a parent -> children map and the root set, then a
    // depth-first walk from roots only. A cycle among non-root entities is never
    // reached from any root, so it's simply absent here -- no depth guard needed for
    // this walk (unlike TransformSystem::compute_world's own kMaxDepth, which guards a
    // different code path: walking UP the parent chain to compute a world matrix).
    std::unordered_map<std::uint64_t, std::vector<EntityID>> children_by_parent;
    std::vector<EntityID> roots;
    world.for_each_entity(
        [&](EntityID e)
        {
            const auto* t = world.get_component<mosaic::engine::transform::Transform>(e);
            if (t == nullptr) { return; }
            if (t->parent) { children_by_parent[t->parent.value()].push_back(e); }
            else { roots.push_back(e); }
        });

    std::function<nlohmann::json(EntityID)> build_node = [&](EntityID e) -> nlohmann::json
    {
        nlohmann::json node;
        node["entity"] = e.value();
        const auto* name = world.get_component<mosaic::engine::Name>(e);
        node["name"] = name != nullptr ? nlohmann::json(name->value) : nlohmann::json(nullptr);
        nlohmann::json children = nlohmann::json::array();
        const auto it = children_by_parent.find(e.value());
        if (it != children_by_parent.end())
        {
            for (EntityID child : it->second) { children.push_back(build_node(child)); }
        }
        node["children"] = children;
        return node;
    };

    nlohmann::json hierarchy = nlohmann::json::array();
    for (EntityID root : roots) { hierarchy.push_back(build_node(root)); }

    return {{"entity_count", static_cast<std::uint64_t>(world.entity_count())},
           {"component_counts", component_counts},
           {"hierarchy", hierarchy}};
}
```

Add the registry entry to `tool_handlers()`'s initializer list (any position; grouped after `save_scene` reads cleanly):

```cpp
        {"save_scene", &tool_save_scene},
        {"describe_scene", &tool_describe_scene},
```

Add the schema entry to `tools_list_schema()` (any position; after `save_scene`'s `tools.push_back` reads cleanly), and change the function's total tool count in its enclosing doc comment (in `mcp_tools.hpp`) from 15 to 16:

```cpp
    tools.push_back({{"name", "describe_scene"},
                     {"description", "Return a snapshot of the scene's shape: entity count, a "
                                     "component-type histogram, and the Transform parent-child "
                                     "hierarchy as a tree."},
                     {"inputSchema", schema_none()}});
```

In `app/mcp/mcp_tools.hpp`, change:

```cpp
// The MCP `tools/list` response body: { "tools": [ { name, description, inputSchema }, ... ] }
// for all 15 v1 tools (entity/component CRUD + undo/redo/save_scene).
```

to:

```cpp
// The MCP `tools/list` response body: { "tools": [ { name, description, inputSchema }, ... ] }
// for all 16 v1 tools (entity/component CRUD + undo/redo/save_scene + describe_scene).
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build -j"$(nproc)" --target mosaic_unit_tests && ./build/tests/mosaic_unit_tests "[mcp]"`
Expected: PASS — all 6 new cases, plus every pre-existing `[mcp]` case (27 total: 10 CRUD + 11 protocol + 6 new `describe_scene`).

- [ ] **Step 5: Commit**

```bash
git add app/mcp/mcp_tools.hpp app/mcp/mcp_tools.cpp tests/unit/app/mcp_tools_test.cpp
git commit -m "$(cat <<'EOF'
feat(mcp): add describe_scene, a 16th Agent API tool

A parameterless snapshot of the scene's shape: entity count, a
component-type histogram, and the Transform parent-child hierarchy
as a tree. Phase 6 sub-project 2 of 7 (scene reasoning) -- spatial
queries, asset-usage queries, and generic criteria-based search
remain future expansion, documented but not built here.

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
- Consumes: `describe_scene` via the real `mosaic mcp` CLI (Task 1).
- Produces: nothing new; this task is verification + documentation only.

- [ ] **Step 1: Manually verify against a real project**

Run:

```bash
cmake --build build -j"$(nproc)"
rm -rf /tmp/describe_scene_verify && ./build/app/mosaic create /tmp/describe_scene_verify
printf '%s\n%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"describe_scene","arguments":{}}}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"create_entity","arguments":{}}}' \
  | ./build/app/mosaic mcp /tmp/describe_scene_verify
```

Expected: the first response's `result.content[0].text` (JSON-encoded) shows a non-zero `entity_count` matching the starter template's scene contents, a `component_counts` map, and a `hierarchy` array — inspect it by eye for sane shape (every hierarchy node has `entity`/`name`/`children`, no malformed nesting).

- [ ] **Step 2: Run the full test suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: green aside from the two known pre-existing flakes (`mosaic-threaded-script-test-flaky-segv`, the `--host`/`--connect` `ctest`-discovery quirk — see project memory). Zero new failures.

- [ ] **Step 3: Add the dated `next-steps.md` entry**

Prepend a new dated paragraph to the top of the changelog blob in `docs/project/next-steps.md` (immediately before the current most-recent `**Last updated:**` entry, matching the existing style), summarizing: `describe_scene` ships as Phase 6 sub-project 2 of 7 (scene reasoning); a 16th MCP tool returning entity/component counts + the Transform hierarchy tree; spatial queries, asset-usage queries, and generic criteria-based search are explicitly recorded as valuable future upgrades, not built here; next Phase 6 sub-project is code generation (3 of 7). Link this plan and the spec.

- [ ] **Step 4: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "$(cat <<'EOF'
docs: record describe_scene's next-steps.md entry

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01CEBXDEjzbGDpMyLrKCN4mY
EOF
)"
```

---

## Self-Review Notes

- **Spec coverage:** `entity_count`/`component_counts`/`hierarchy` shape (Task 1) ✓; `name` null-vs-string distinction (Task 1's 4th test) ✓; `Transform`-less entities excluded from `hierarchy` but counted (Task 1's 5th test) ✓; cycle safety (Task 1's 6th test) ✓; no new files/parameters (Task 1's Global Constraints) ✓; manual verification + next-steps.md entry (Task 2) ✓.
- **Placeholder scan:** no TBD/TODO; every step has real code or an exact command.
- **Type consistency:** `tool_describe_scene`'s signature matches the existing `ToolHandler` type alias (`World&, const ComponentReflection&, CommandStack&, const std::string&, const nlohmann::json&`) already defined in `mcp_tools.cpp`; no new public interface, so nothing downstream (Task 2, or `app/run/mcp.cpp`) needs any signature changes at all.
