# Optimization — `script_profile` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a 21st and final MCP tool, `script_profile`, to the existing `mosaic mcp` server: wrap `ScriptSystem::profile()` as-is — the exact machinery already backing `mosaic run --script-profile` — returning per-scripted-entity `on_update` timing. This closes out Phase 6 (Agent ecosystem) entirely.

**Architecture:** `script_profile` fits the uniform `ToolHandler` shape exactly (only needs `World&`) — added directly to `tool_handlers()`, no special-casing, no `handle_tool_call` changes at all, the simplest of the four thin-wrapper tools Phase 6's back half added.

**Tech Stack:** C++20, `nlohmann_json`, the existing `ScriptSystem::profile()`/`World::get_system<T>()` machinery (already a build dependency, already linked into `mosaic_app` via the `tick`/`setup_runtime` work). No new CMakeLists changes.

**Spec:** `docs/superpowers/specs/2026-09-19-optimization-script-profile-design.md`

## Global Constraints

- `script_profile` takes no parameters, reuses the existing `schema_none()` helper — no new schema helper needed.
- Retrieves the live `ScriptSystem` via `world.get_system<mosaic::engine::script::ScriptSystem>()`; a `nullptr` result (no `ScriptSystem` wired — e.g. a plain `Fixture` without `setup_runtime`) returns `{"entries": []}` gracefully, not a crash.
- No CMakeLists changes — `script_system.hpp` is already linked into `mosaic_app`.

---

### Task 1: `script_profile` tool

**Files:**
- Modify: `app/mcp/mcp_tools.hpp` — doc-comment tool count 20 → 21. No signature changes.
- Modify: `app/mcp/mcp_tools.cpp` — new include, `tool_script_profile`, a `tool_handlers()` entry, a `tools_list_schema()` entry (reusing `schema_none()`).
- Test: `tests/unit/app/mcp_tools_test.cpp` (extended) — new test cases, existing tool-count assertion (20 → 21).
- Test: `tests/unit/app/mcp_protocol_test.cpp` (extended) — existing tool-count assertion (20 → 21).

**Interfaces:**
- Consumes: `mosaic::engine::script::ScriptSystem::profile() -> std::vector<ScriptProfileEntry>` and `mosaic::engine::World::get_system<T>()` (`engine/script/script_system.hpp`, `engine/world/world.hpp`, pre-existing).
- Produces: no new public function signature — `script_profile` uses the existing `ToolHandler` shape as-is, added to the existing `tool_handlers()` map.

- [ ] **Step 1: Write the failing tests**

Append these test cases to `tests/unit/app/mcp_tools_test.cpp`:

```cpp
TEST_CASE("mcp_tools: script_profile reports per-entity on_update timing after ticking", "[app][mcp]")
{
    mosaic::script::python::ensure_interpreter();
    RuntimeFixture f;
    const std::uint64_t eid = f.call("create_entity").at("id").get<std::uint64_t>();
    f.call("add_component", {{"entity", eid}, {"component", "Transform"}});
    f.call("add_component", {{"entity", eid}, {"component", "ScriptComponent"}});
    REQUIRE(f.call("set_script", {{"entity", eid}, {"source", "def on_update(dt):\n    pass\n"}})
                .at("ok")
                .get<bool>());

    f.call("tick", {{"count", 3}});

    const nlohmann::json result = f.call("script_profile");
    const nlohmann::json entries = result.at("entries");
    bool found = false;
    for (const auto& entry : entries)
    {
        if (entry.at("entity").get<std::uint64_t>() == eid)
        {
            REQUIRE(entry.at("calls").get<std::uint64_t>() == 3);
            REQUIRE(entry.at("total_ms").get<double>() >= 0.0);
            REQUIRE(entry.at("max_ms").get<double>() >= 0.0);
            found = true;
        }
    }
    REQUIRE(found);
}

TEST_CASE("mcp_tools: script_profile is empty before any tick has run", "[app][mcp]")
{
    RuntimeFixture f;
    const nlohmann::json result = f.call("script_profile");
    REQUIRE(result.at("entries").empty());
}

TEST_CASE("mcp_tools: script_profile returns empty entries when no ScriptSystem is wired", "[app][mcp]")
{
    Fixture f;
    const nlohmann::json result = f.call("script_profile");
    REQUIRE(result.at("entries").empty());
}
```

Update the existing `tools_list_schema` count test — both occurrences of `20` become `21`:

```cpp
TEST_CASE("mcp_tools: tools_list_schema lists all 21 v1 tools with a description and inputSchema", "[app][mcp]")
{
    const nlohmann::json schema = tools_list_schema();
    REQUIRE(schema.contains("tools"));
    const nlohmann::json tools = schema.at("tools");
    REQUIRE(tools.size() == 21);
```
(leave the rest of that test case's body unchanged).

In `tests/unit/app/mcp_protocol_test.cpp`, update the one existing count assertion:

```cpp
    REQUIRE(parsed.at("result").at("tools").size() == 21);
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build -j4`
Expected: FAIL to build — `script_profile` isn't a registered MCP tool yet.

- [ ] **Step 3: Update `mcp_tools.hpp`**

Change the `tools_list_schema()` doc comment:

```cpp
// The MCP `tools/list` response body: { "tools": [ { name, description, inputSchema }, ... ] }
// for all 21 v1 tools (entity/component CRUD + undo/redo/save_scene + describe_scene +
// set_script + import_asset + tick + build_project + script_profile).
nlohmann::json tools_list_schema();
```

No other changes to this file — `handle_tool_call`'s declaration is unchanged.

- [ ] **Step 4: Update `mcp_tools.cpp`**

Add the include, alongside the other `engine/`-tier includes:

```cpp
#include "engine/script/script_system.hpp"
```

Add `tool_script_profile`, placed after `tool_tick` and before `tool_build_project`:

```cpp
nlohmann::json tool_script_profile(World& world, const ComponentReflection&, CommandStack&,
                                   const std::string&, const nlohmann::json&)
{
    auto* sys = world.get_system<mosaic::engine::script::ScriptSystem>();
    if (sys == nullptr) { return {{"entries", nlohmann::json::array()}}; }
    nlohmann::json entries = nlohmann::json::array();
    for (const auto& e : sys->profile())
    {
        entries.push_back({{"entity", e.entity}, {"calls", e.calls},
                           {"total_ms", e.total_ms}, {"max_ms", e.max_ms}});
    }
    return {{"entries", entries}};
}
```

Add `"script_profile"` to `tool_handlers()`'s initializer list:

```cpp
        {"describe_scene", &tool_describe_scene},
        {"tick", &tool_tick},
        {"script_profile", &tool_script_profile},
```

Add the `tools_list_schema()` entry, after the `build_project` entry:

```cpp
    tools.push_back({{"name", "script_profile"},
                     {"description", "Return per-scripted-entity on_update timing accumulated "
                                     "so far this session (via ScriptSystem::profile() -- the "
                                     "same machinery `mosaic run --script-profile` uses): calls, "
                                     "total_ms, and max_ms per entity. Empty if nothing has "
                                     "ticked yet, or if no ScriptSystem is wired."},
                     {"inputSchema", schema_none()}});
```

`handle_tool_call` itself needs no changes — `script_profile` reaches it through the existing generic `tool_handlers()` lookup path.

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build -j4 --target mosaic_unit_tests && ./build/tests/mosaic_unit_tests "[mcp]"`
Expected: PASS — all 3 new `script_profile` cases, plus every pre-existing `[mcp]` case with the updated 21-tool counts. No failures.

- [ ] **Step 6: Commit**

```bash
git add app/mcp/mcp_tools.hpp app/mcp/mcp_tools.cpp \
       tests/unit/app/mcp_tools_test.cpp tests/unit/app/mcp_protocol_test.cpp
git commit -m "$(cat <<'EOF'
feat(mcp): add script_profile, a 21st and final Agent API tool

Wraps ScriptSystem::profile() as-is -- the exact machinery already
backing `mosaic run --script-profile`. Returns per-scripted-entity
on_update timing (calls, total_ms, max_ms), letting an agent write a
script, tick it forward, then see exactly what's slow. Fits the
uniform ToolHandler shape directly -- no special-casing needed,
unlike set_script/import_asset/build_project. Phase 6 sub-project 7
of 7 (optimization) -- the last one. Closes out Phase 6 entirely.

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
- Consumes: `script_profile` via the real `mosaic mcp` CLI (Task 1).
- Produces: nothing new; verification + documentation only.

- [ ] **Step 1: Manually verify against a real project**

Run:

```bash
cmake --build build -j4
rm -rf /tmp/script_profile_verify && ./build/app/mosaic create /tmp/script_profile_verify
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"create_entity","arguments":{}}}' \
  | ./build/app/mosaic mcp /tmp/script_profile_verify
```

Note the returned entity id, then substitute it into the following combined session (a fresh
`mosaic mcp` invocation starts a new World, so run everything in one continuous pipe):

```bash
printf '%s\n%s\n%s\n%s\n%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"create_entity","arguments":{}}}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"add_component","arguments":{"entity":<ID>,"component":"Transform"}}}' \
  '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"add_component","arguments":{"entity":<ID>,"component":"ScriptComponent"}}}' \
  '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"set_script","arguments":{"entity":<ID>,"source":"def on_update(dt):\n    pass\n"}}}' \
  '{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"tick","arguments":{"count":10}}}' \
  '{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"script_profile","arguments":{}}}' \
  | ./build/app/mosaic mcp /tmp/script_profile_verify
```

(replace every `<ID>` with the actual id from response 1). Expected: response 6 shows `entries`
containing the entity with `calls: 10` and non-negative `total_ms`/`max_ms`. No crash.

- [ ] **Step 2: Run the full test suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: green aside from the known pre-existing flakes (the `--host`/`--connect` `ctest`-discovery quirk, and the threaded-Python-teardown flake). Zero new failures.

- [ ] **Step 3: Add the dated `next-steps.md` entry**

Prepend a new dated paragraph to the top of the changelog blob in `docs/project/next-steps.md`
(immediately before the current most-recent `**Last updated:**` entry, matching the existing
style), summarizing: `script_profile` ships as Phase 6 sub-project 7 of 7 (optimization) — **the
last one** — closing out Phase 6 (Agent ecosystem) entirely, and with it every phase
`docs/project/roadmap.md` currently defines; a 21st MCP tool wrapping `ScriptSystem::profile()`
as-is; broader per-system timing is recorded as the one clearly-scoped future upgrade within this
sub-project; what comes next is genuinely open, a call for the user/maintainer to make. Link this
plan and the spec.

- [ ] **Step 4: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "$(cat <<'EOF'
docs: record script_profile's next-steps.md entry, closing Phase 6

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01CEBXDEjzbGDpMyLrKCN4mY
EOF
)"
```

---

## Self-Review Notes

- **Spec coverage:** thin wrapper over `ScriptSystem::profile()` with no new profiling logic (Task 1) ✓; graceful `{"entries": []}` when no `ScriptSystem` is wired (Task 1's 3rd test) ✓; no special-casing, added directly to `tool_handlers()` (Task 1 Step 4) ✓; reuses `schema_none()`, no new schema helper (Task 1 Step 4) ✓; zero CMakeLists changes (confirmed: no `target_link_libraries`/`add_library` edits anywhere) ✓; manual verification + next-steps.md, explicitly noting this closes Phase 6 (Task 2) ✓.
- **Placeholder scan:** no TBD/TODO; every step has real code or an exact command (Task 2's `<ID>` substitution is an explicit, called-out manual step, not an unresolved placeholder in the code itself).
- **Type consistency:** `tool_script_profile`'s signature matches the existing `ToolHandler` alias exactly (`World&, const ComponentReflection&, CommandStack&, const std::string&, const nlohmann::json&`); `ScriptProfileEntry`'s fields (`entity`, `calls`, `total_ms`, `max_ms`) are read with the exact names the pre-existing `engine/script/script_system.hpp` struct already defines — no renaming.
