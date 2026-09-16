# Agent API — MCP Server Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `mosaic mcp <project-dir>`, a headless MCP (Model Context Protocol) server exposing entity/component CRUD + undo/redo/save over stdio JSON-RPC, so an external AI agent gets a validated, undoable editing surface instead of raw file edits.

**Architecture:** A `World`/`ComponentReflection`/`CommandStack`-driven tool-dispatch core (`app/mcp/mcp_tools.*`) mirrors `world_globals.cpp`'s existing Python surface but returns/accepts `nlohmann::json` instead of `py::object`; a thin, engine-agnostic JSON-RPC/stdio framing layer (`app/mcp/mcp_protocol.*`) drives it via an injected dispatch callback; a new CLI subcommand (`app/run/mcp.*`) resolves the project, builds the resident `World`, and runs the protocol loop over real stdin/stdout.

**Tech Stack:** C++20, `nlohmann_json` (already linked into `mosaic_app`), Catch2. No new dependencies — no pybind11/Python involvement anywhere in this feature.

**Spec:** `docs/superpowers/specs/2026-09-16-agent-api-mcp-server-design.md`

## Global Constraints

- v1 tool set is entity/component CRUD only: `create_entity`, `destroy_entity`, `get_component`, `set_component`, `add_component`, `remove_component`, `has_component`, `component_names`, `entities`, `entities_with`, `entity_count`, `undo`, `redo`, `undo_depth`, `save_scene` — no CLI-verb wrapping (`create_project`/`build_project`/etc.), no live-Studio attachment. Both are documented future expansion, not built here.
- Session model: one persistent `World` + `CommandStack` per `mosaic mcp` process, loaded once at startup, held across every tool call. No stateless per-call reload.
- An unknown entity id or unregistered component name is reported inside a successful tool result (`{"ok": false}` / `{"value": null}`) — **not** an MCP tool error. `isError: true` is reserved for malformed/missing request parameters and an unrecognized tool name.
- The server process must never crash on bad input — every exception is caught at the protocol boundary and turned into a tool-error result or a JSON-RPC error response.
- `mosaic mcp <project-dir>` requires an actual project directory with `mosaic.toml` — no bare `.json` scene file positional, unlike `run`/`editor`. Missing manifest fails immediately with a clear error.

---

### Task 1: `app/mcp/mcp_tools` — the tool-dispatch core

**Files:**
- Create: `app/mcp/mcp_tools.hpp`
- Create: `app/mcp/mcp_tools.cpp`
- Modify: `app/CMakeLists.txt` — add `mcp/mcp_tools.cpp` to `mosaic_app`'s sources.
- Test: `tests/unit/app/mcp_tools_test.cpp`
- Modify: `tests/CMakeLists.txt` — register the new test file.

**Interfaces:**
- Consumes: `mosaic::engine::World` (`create_entity`, `destroy_entity`, `is_valid`, `entity_count`, `for_each_entity`, `engine/world/world.hpp`), `mosaic::engine::reflection::ComponentReflection` (`component_value`, `set_value`, `add_default`, `remove_value`, `components_on`, `engine/reflection/component_reflection.hpp`), `mosaic::foundation::commands::CommandStack` (`undo`, `redo`, `undo_depth`, `push_executed`, `foundation/commands/command_stack.hpp`), `mosaic::engine::commands::SetComponentCommand` (`engine/commands/set_component_command.hpp`), `mosaic::foundation::serialization::to_json`/`from_json` (`foundation/serialization/json_backend.hpp`), `mosaic::engine::scene::save_to_json` (`engine/scene/scene.hpp`), `mosaic::app::save_scene_file` (`app/run/scene_io.hpp`).
- Produces: `struct mosaic::app::mcp::McpToolError : std::runtime_error`; `nlohmann::json mosaic::app::mcp::tools_list_schema()`; `nlohmann::json mosaic::app::mcp::handle_tool_call(mosaic::engine::World& world, const mosaic::engine::reflection::ComponentReflection& types, mosaic::foundation::commands::CommandStack& stack, const std::string& scene_path, const std::string& tool_name, const nlohmann::json& arguments)`. Consumed by Task 2 (`tools_list_schema`) and Task 3 (`handle_tool_call`, wired into `run/mcp.cpp`'s dispatch closure).

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/app/mcp_tools_test.cpp`:

```cpp
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <unistd.h> // ::getpid (unique temp file under parallel ctest)

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "app/mcp/mcp_tools.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/world/name.hpp"
#include "engine/world/world.hpp"
#include "foundation/commands/command_stack.hpp"
#include "foundation/reflection/type_registry.hpp"

using mosaic::app::mcp::handle_tool_call;
using mosaic::app::mcp::McpToolError;
using mosaic::app::mcp::tools_list_schema;
using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::foundation::commands::CommandStack;
using mosaic::foundation::reflection::TypeRegistry;

namespace
{
struct Fixture
{
    TypeRegistry     registry;
    ComponentReflection types;
    World            world;
    CommandStack     stack;

    Fixture() { register_builtin_components(registry, types); }

    nlohmann::json call(const std::string& tool, const nlohmann::json& args = nlohmann::json::object())
    {
        return handle_tool_call(world, types, stack, "", tool, args);
    }
};
} // namespace

TEST_CASE("mcp_tools: create_entity, entity_count, entities, destroy_entity", "[app][mcp]")
{
    Fixture f;
    const nlohmann::json created = f.call("create_entity");
    REQUIRE(created.contains("id"));
    const std::uint64_t eid = created.at("id").get<std::uint64_t>();

    REQUIRE(f.call("entity_count").at("count").get<std::uint64_t>() == 1);
    const nlohmann::json ids = f.call("entities").at("ids");
    REQUIRE(ids.size() == 1);
    REQUIRE(ids.at(0).get<std::uint64_t>() == eid);

    REQUIRE(f.call("destroy_entity", {{"entity", eid}}).at("ok").get<bool>() == true);
    // Deferred: still valid until the caller ticks the World (this server never ticks).
    REQUIRE(f.call("destroy_entity", {{"entity", eid + 1000}}).at("ok").get<bool>() == false);
}

TEST_CASE("mcp_tools: add/get/set/remove/has_component + component_names round-trip", "[app][mcp]")
{
    Fixture f;
    const std::uint64_t eid = f.call("create_entity").at("id").get<std::uint64_t>();

    REQUIRE(f.call("has_component", {{"entity", eid}, {"component", "Name"}}).at("ok").get<bool>() == false);
    REQUIRE(f.call("add_component", {{"entity", eid}, {"component", "Name"}}).at("ok").get<bool>() == true);
    REQUIRE(f.call("has_component", {{"entity", eid}, {"component", "Name"}}).at("ok").get<bool>() == true);

    const nlohmann::json names = f.call("component_names", {{"entity", eid}}).at("names");
    REQUIRE(names.size() == 1);
    REQUIRE(names.at(0).get<std::string>() == "Name");

    const nlohmann::json set_result = f.call(
        "set_component", {{"entity", eid}, {"component", "Name"}, {"value", {{"value", "hi"}}}});
    REQUIRE(set_result.at("ok").get<bool>() == true);

    const nlohmann::json got = f.call("get_component", {{"entity", eid}, {"component", "Name"}});
    REQUIRE(got.at("value").at("value").get<std::string>() == "hi");

    REQUIRE(f.call("remove_component", {{"entity", eid}, {"component", "Name"}}).at("ok").get<bool>() == true);
    REQUIRE(f.call("get_component", {{"entity", eid}, {"component", "Name"}}).at("value").is_null());
}

TEST_CASE("mcp_tools: unknown entity/component is a graceful result, not a tool error", "[app][mcp]")
{
    Fixture f;
    REQUIRE_NOTHROW(f.call("has_component", {{"entity", 999999}, {"component", "Name"}}));
    REQUIRE(f.call("has_component", {{"entity", 999999}, {"component", "Name"}}).at("ok").get<bool>() == false);
    REQUIRE(f.call("get_component", {{"entity", 999999}, {"component", "NotARealComponent"}}).at("value").is_null());
    REQUIRE(f.call("add_component", {{"entity", 999999}, {"component", "NotARealComponent"}}).at("ok").get<bool>() == false);
}

TEST_CASE("mcp_tools: entities_with selects only matching entities", "[app][mcp]")
{
    Fixture f;
    const std::uint64_t with_name = f.call("create_entity").at("id").get<std::uint64_t>();
    const std::uint64_t without_name = f.call("create_entity").at("id").get<std::uint64_t>();
    f.call("add_component", {{"entity", with_name}, {"component", "Name"}});

    const nlohmann::json ids = f.call("entities_with", {{"component", "Name"}}).at("ids");
    REQUIRE(ids.size() == 1);
    REQUIRE(ids.at(0).get<std::uint64_t>() == with_name);
    (void)without_name;
}

TEST_CASE("mcp_tools: set_component is undoable via undo/redo/undo_depth", "[app][mcp]")
{
    Fixture f;
    const std::uint64_t eid = f.call("create_entity").at("id").get<std::uint64_t>();
    f.call("add_component", {{"entity", eid}, {"component", "Name"}});
    f.call("set_component", {{"entity", eid}, {"component", "Name"}, {"value", {{"value", "before"}}}});

    REQUIRE(f.call("undo_depth").at("depth").get<std::uint64_t>() == 1);
    REQUIRE(f.call("undo").at("ok").get<bool>() == true);
    REQUIRE(f.call("get_component", {{"entity", eid}, {"component", "Name"}}).at("value").at("value").get<std::string>() == "");
    REQUIRE(f.call("redo").at("ok").get<bool>() == true);
    REQUIRE(f.call("get_component", {{"entity", eid}, {"component", "Name"}}).at("value").at("value").get<std::string>() == "before");
    REQUIRE(f.call("redo").at("ok").get<bool>() == false); // nothing left to redo
}

TEST_CASE("mcp_tools: malformed arguments throw McpToolError, not a crash", "[app][mcp]")
{
    Fixture f;
    REQUIRE_THROWS_AS(f.call("get_component", {{"component", "Name"}}), McpToolError); // missing entity
    REQUIRE_THROWS_AS(f.call("get_component", {{"entity", "not-a-number"}, {"component", "Name"}}), McpToolError);
    REQUIRE_THROWS_AS(f.call("set_component", {{"entity", 1}, {"component", "Name"}}), McpToolError); // missing value
}

TEST_CASE("mcp_tools: unknown tool name throws McpToolError", "[app][mcp]")
{
    Fixture f;
    REQUIRE_THROWS_AS(f.call("not_a_real_tool"), McpToolError);
}

TEST_CASE("mcp_tools: save_scene writes the current World to the given scene path", "[app][mcp]")
{
    Fixture f;
    const std::uint64_t eid = f.call("create_entity").at("id").get<std::uint64_t>();
    f.call("add_component", {{"entity", eid}, {"component", "Name"}});
    f.call("set_component", {{"entity", eid}, {"component", "Name"}, {"value", {{"value", "saved"}}}});

    const std::string path =
        (std::filesystem::temp_directory_path() /
         (std::string("mosaic_mcp_save_test_") + std::to_string(::getpid()) + ".json"))
            .string();
    std::filesystem::remove(path);

    const nlohmann::json result = handle_tool_call(f.world, f.types, f.stack, path, "save_scene", {});
    REQUIRE(result.at("path").get<std::string>() == path);
    REQUIRE(std::filesystem::exists(path));

    std::ifstream in(path);
    std::string written((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    REQUIRE(written.find("\"saved\"") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("mcp_tools: save_scene with an empty scene path is an McpToolError", "[app][mcp]")
{
    Fixture f;
    REQUIRE_THROWS_AS(handle_tool_call(f.world, f.types, f.stack, "", "save_scene", {}), McpToolError);
}

TEST_CASE("mcp_tools: tools_list_schema lists all 15 v1 tools with a description and inputSchema", "[app][mcp]")
{
    const nlohmann::json schema = tools_list_schema();
    REQUIRE(schema.contains("tools"));
    const nlohmann::json tools = schema.at("tools");
    REQUIRE(tools.size() == 15);
    for (const auto& tool : tools)
    {
        REQUIRE(tool.contains("name"));
        REQUIRE(tool.contains("description"));
        REQUIRE_FALSE(tool.at("description").get<std::string>().empty());
        REQUIRE(tool.contains("inputSchema"));
        REQUIRE(tool.at("inputSchema").at("type").get<std::string>() == "object");
    }
}
```

Add the new test file to `tests/CMakeLists.txt`, right after `unit/app/component_reference_test.cpp` (or `unit/app/scripting_globals_reference_test.cpp` if that landed first — either neighbor is fine):

```cmake
  unit/app/mcp_tools_test.cpp
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build -j"$(nproc)"`
Expected: FAIL to configure/build — `app/mcp/mcp_tools.hpp` doesn't exist yet.

- [ ] **Step 3: Create `mcp_tools.hpp`**

```cpp
#pragma once

#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/reflection/component_reflection.hpp"
#include "engine/world/world.hpp"
#include "foundation/commands/command_stack.hpp"

namespace mosaic::app::mcp
{
// Thrown for protocol-level misuse of a tool call: an unrecognized tool name, or a
// request missing/mistyping a required argument. NEVER thrown for an ordinary "not
// found" outcome (unknown entity id, unregistered component name) -- those come back
// inside a normal successful result (`{"ok": false}` / `{"value": null}`), exactly
// like the existing Python scripting globals (world_globals.cpp) already behave for
// the same cases. Caught at the protocol boundary (app/mcp/mcp_protocol.cpp) and
// turned into an MCP tool-error result -- never crashes the server process.
struct McpToolError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

// The MCP `tools/list` response body: { "tools": [ { name, description, inputSchema }, ... ] }
// for all 15 v1 tools (entity/component CRUD + undo/redo/save_scene).
nlohmann::json tools_list_schema();

// Dispatches one tool call by name. `scene_path` is the manifest's default_scene path,
// used only by `save_scene` (unused by every other tool, threaded through uniformly --
// the same "every closure takes everything it might need" shape world_globals.cpp uses
// for world_ptr/types_ptr/stack_ptr). Throws McpToolError for an unknown tool name or
// malformed arguments; otherwise never throws.
nlohmann::json handle_tool_call(mosaic::engine::World& world,
                                const mosaic::engine::reflection::ComponentReflection& types,
                                mosaic::foundation::commands::CommandStack& stack,
                                const std::string& scene_path, const std::string& tool_name,
                                const nlohmann::json& arguments);
} // namespace mosaic::app::mcp
```

- [ ] **Step 4: Create `mcp_tools.cpp`**

```cpp
#include "app/mcp/mcp_tools.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>

#include "app/run/scene_io.hpp"
#include "engine/commands/set_component_command.hpp"
#include "engine/scene/scene.hpp"
#include "foundation/core/entity_id.hpp"
#include "foundation/serialization/json_backend.hpp"

namespace mosaic::app::mcp
{
namespace
{
using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::foundation::EntityID;
using mosaic::foundation::commands::CommandStack;
using mosaic::foundation::serialization::Value;

nlohmann::json value_to_json(const Value& v)
{
    return nlohmann::json::parse(mosaic::foundation::serialization::to_json(v));
}

Value json_to_value(const nlohmann::json& j)
{
    return mosaic::foundation::serialization::from_json(j.dump());
}

std::uint64_t require_uint64(const nlohmann::json& args, const char* key)
{
    // nlohmann parses an ordinary non-negative JSON integer (e.g. from real wire input)
    // as number_integer (int64), NOT number_unsigned -- that type is only produced when
    // the C++ side constructs a json from an actual unsigned value, or the parsed value
    // overflows int64. So both number kinds must be accepted; number_integer additionally
    // needs an explicit non-negative check before the narrowing get<uint64_t>() below,
    // which would otherwise silently wrap a negative value into a huge one.
    if (!args.contains(key) || args.at(key).is_number_float() || !args.at(key).is_number())
    {
        throw McpToolError(std::string("missing or invalid '") + key + "' (expected a non-negative integer)");
    }
    if (args.at(key).is_number_integer() && args.at(key).get<std::int64_t>() < 0)
    {
        throw McpToolError(std::string("missing or invalid '") + key + "' (expected a non-negative integer)");
    }
    return args.at(key).get<std::uint64_t>();
}

std::string require_string(const nlohmann::json& args, const char* key)
{
    if (!args.contains(key) || !args.at(key).is_string())
    {
        throw McpToolError(std::string("missing or invalid '") + key + "' (expected a string)");
    }
    return args.at(key).get<std::string>();
}

using ToolHandler = nlohmann::json (*)(World&, const ComponentReflection&, CommandStack&,
                                       const std::string&, const nlohmann::json&);

nlohmann::json tool_create_entity(World& world, const ComponentReflection&, CommandStack&,
                                  const std::string&, const nlohmann::json&)
{
    return {{"id", world.create_entity().value()}};
}

nlohmann::json tool_destroy_entity(World& world, const ComponentReflection&, CommandStack&,
                                   const std::string&, const nlohmann::json& args)
{
    const EntityID e(require_uint64(args, "entity"));
    if (!world.is_valid(e)) { return {{"ok", false}}; }
    world.destroy_entity(e);
    return {{"ok", true}};
}

nlohmann::json tool_get_component(World& world, const ComponentReflection& types, CommandStack&,
                                  const std::string&, const nlohmann::json& args)
{
    const EntityID e(require_uint64(args, "entity"));
    const std::string name = require_string(args, "component");
    const auto v = types.component_value(world, e, name);
    return {{"value", v ? value_to_json(*v) : nlohmann::json(nullptr)}};
}

nlohmann::json tool_set_component(World& world, const ComponentReflection& types, CommandStack& stack,
                                  const std::string&, const nlohmann::json& args)
{
    const EntityID e(require_uint64(args, "entity"));
    const std::string name = require_string(args, "component");
    if (!args.contains("value"))
    {
        throw McpToolError("missing 'value'");
    }
    const Value next = json_to_value(args.at("value"));
    auto cmd = std::make_unique<mosaic::engine::commands::SetComponentCommand>(world, types, e, name, next);
    cmd->execute();
    if (!cmd->valid()) { return {{"ok", false}}; }
    stack.push_executed(std::move(cmd));
    return {{"ok", true}};
}

nlohmann::json tool_add_component(World& world, const ComponentReflection& types, CommandStack&,
                                  const std::string&, const nlohmann::json& args)
{
    const EntityID e(require_uint64(args, "entity"));
    const std::string name = require_string(args, "component");
    return {{"ok", types.add_default(world, e, name)}};
}

nlohmann::json tool_remove_component(World& world, const ComponentReflection& types, CommandStack&,
                                     const std::string&, const nlohmann::json& args)
{
    const EntityID e(require_uint64(args, "entity"));
    const std::string name = require_string(args, "component");
    return {{"ok", types.remove_value(world, e, name)}};
}

nlohmann::json tool_has_component(World& world, const ComponentReflection& types, CommandStack&,
                                  const std::string&, const nlohmann::json& args)
{
    const EntityID e(require_uint64(args, "entity"));
    const std::string name = require_string(args, "component");
    for (const std::string& n : types.components_on(world, e))
    {
        if (n == name) { return {{"ok", true}}; }
    }
    return {{"ok", false}};
}

nlohmann::json tool_component_names(World& world, const ComponentReflection& types, CommandStack&,
                                    const std::string&, const nlohmann::json& args)
{
    const EntityID e(require_uint64(args, "entity"));
    return {{"names", types.components_on(world, e)}};
}

nlohmann::json tool_entities(World& world, const ComponentReflection&, CommandStack&,
                             const std::string&, const nlohmann::json&)
{
    nlohmann::json ids = nlohmann::json::array();
    world.for_each_entity([&](EntityID e) { ids.push_back(e.value()); });
    return {{"ids", ids}};
}

nlohmann::json tool_entities_with(World& world, const ComponentReflection& types, CommandStack&,
                                  const std::string&, const nlohmann::json& args)
{
    const std::string name = require_string(args, "component");
    nlohmann::json ids = nlohmann::json::array();
    world.for_each_entity(
        [&](EntityID e)
        {
            for (const std::string& n : types.components_on(world, e))
            {
                if (n == name) { ids.push_back(e.value()); break; }
            }
        });
    return {{"ids", ids}};
}

nlohmann::json tool_entity_count(World& world, const ComponentReflection&, CommandStack&,
                                 const std::string&, const nlohmann::json&)
{
    return {{"count", static_cast<std::uint64_t>(world.entity_count())}};
}

nlohmann::json tool_undo(World&, const ComponentReflection&, CommandStack& stack,
                         const std::string&, const nlohmann::json&)
{
    return {{"ok", stack.undo()}};
}

nlohmann::json tool_redo(World&, const ComponentReflection&, CommandStack& stack,
                         const std::string&, const nlohmann::json&)
{
    return {{"ok", stack.redo()}};
}

nlohmann::json tool_undo_depth(World&, const ComponentReflection&, CommandStack& stack,
                               const std::string&, const nlohmann::json&)
{
    return {{"depth", static_cast<std::uint64_t>(stack.undo_depth())}};
}

nlohmann::json tool_save_scene(World& world, const ComponentReflection& types, CommandStack&,
                               const std::string& scene_path, const nlohmann::json&)
{
    if (scene_path.empty())
    {
        throw McpToolError("no scene path configured for this session");
    }
    const std::string json_text = mosaic::engine::scene::save_to_json(world, types);
    if (!mosaic::app::save_scene_file(scene_path, json_text))
    {
        throw McpToolError("failed to write scene file: " + scene_path);
    }
    return {{"path", scene_path}};
}

const std::unordered_map<std::string, ToolHandler>& tool_handlers()
{
    static const std::unordered_map<std::string, ToolHandler> handlers = {
        {"create_entity", &tool_create_entity},   {"destroy_entity", &tool_destroy_entity},
        {"get_component", &tool_get_component},   {"set_component", &tool_set_component},
        {"add_component", &tool_add_component},   {"remove_component", &tool_remove_component},
        {"has_component", &tool_has_component},   {"component_names", &tool_component_names},
        {"entities", &tool_entities},             {"entities_with", &tool_entities_with},
        {"entity_count", &tool_entity_count},     {"undo", &tool_undo},
        {"redo", &tool_redo},                     {"undo_depth", &tool_undo_depth},
        {"save_scene", &tool_save_scene},
    };
    return handlers;
}

nlohmann::json schema_none()
{
    return {{"type", "object"}, {"properties", nlohmann::json::object()}, {"required", nlohmann::json::array()}};
}

nlohmann::json schema_entity()
{
    return {{"type", "object"},
           {"properties", {{"entity", {{"type", "integer"}, {"minimum", 0}}}}},
           {"required", {"entity"}}};
}

nlohmann::json schema_entity_component()
{
    return {{"type", "object"},
           {"properties",
            {{"entity", {{"type", "integer"}, {"minimum", 0}}}, {"component", {{"type", "string"}}}}},
           {"required", {"entity", "component"}}};
}

nlohmann::json schema_entity_component_value()
{
    nlohmann::json s = schema_entity_component();
    s["properties"]["value"] = {{"description", "The component's fields as JSON"}};
    s["required"].push_back("value");
    return s;
}

nlohmann::json schema_component()
{
    return {{"type", "object"}, {"properties", {{"component", {{"type", "string"}}}}}, {"required", {"component"}}};
}
} // namespace

nlohmann::json tools_list_schema()
{
    nlohmann::json tools = nlohmann::json::array();
    tools.push_back({{"name", "create_entity"},
                     {"description", "Create a new entity and return its id."},
                     {"inputSchema", schema_none()}});
    tools.push_back({{"name", "destroy_entity"},
                     {"description", "Destroy an entity (deferred to the next World tick); "
                                     "ok is false if the id isn't valid."},
                     {"inputSchema", schema_entity()}});
    tools.push_back({{"name", "get_component"},
                     {"description", "Return a component's fields as JSON, or null if the "
                                     "entity doesn't have it."},
                     {"inputSchema", schema_entity_component()}});
    tools.push_back({{"name", "set_component"},
                     {"description", "Set a component's fields from JSON; routes through the "
                                     "undo stack. ok is false if the set failed."},
                     {"inputSchema", schema_entity_component_value()}});
    tools.push_back({{"name", "add_component"},
                     {"description", "Attach a default-constructed component to an entity by name."},
                     {"inputSchema", schema_entity_component()}});
    tools.push_back({{"name", "remove_component"},
                     {"description", "Remove a component from an entity by name."},
                     {"inputSchema", schema_entity_component()}});
    tools.push_back({{"name", "has_component"},
                     {"description", "Return whether an entity currently has a component by name."},
                     {"inputSchema", schema_entity_component()}});
    tools.push_back({{"name", "component_names"},
                     {"description", "Return the names of every component currently attached to an entity."},
                     {"inputSchema", schema_entity()}});
    tools.push_back({{"name", "entities"},
                     {"description", "Return the ids of every live entity."},
                     {"inputSchema", schema_none()}});
    tools.push_back({{"name", "entities_with"},
                     {"description", "Return the ids of every entity that currently has the named component."},
                     {"inputSchema", schema_component()}});
    tools.push_back({{"name", "entity_count"},
                     {"description", "Return the number of live entities."},
                     {"inputSchema", schema_none()}});
    tools.push_back({{"name", "undo"},
                     {"description", "Undo the last recorded command; ok is false if there's nothing to undo."},
                     {"inputSchema", schema_none()}});
    tools.push_back({{"name", "redo"},
                     {"description", "Redo the last undone command; ok is false if there's nothing to redo."},
                     {"inputSchema", schema_none()}});
    tools.push_back({{"name", "undo_depth"},
                     {"description", "Return how many commands are currently undoable."},
                     {"inputSchema", schema_none()}});
    tools.push_back({{"name", "save_scene"},
                     {"description", "Write the current World back to the project's scene file. "
                                     "Returns the path written."},
                     {"inputSchema", schema_none()}});
    return {{"tools", tools}};
}

nlohmann::json handle_tool_call(World& world, const ComponentReflection& types, CommandStack& stack,
                                const std::string& scene_path, const std::string& tool_name,
                                const nlohmann::json& arguments)
{
    const auto& handlers = tool_handlers();
    const auto it = handlers.find(tool_name);
    if (it == handlers.end())
    {
        throw McpToolError("unknown tool: " + tool_name);
    }
    return it->second(world, types, stack, scene_path, arguments);
}
} // namespace mosaic::app::mcp
```

- [ ] **Step 5: Register the new source file in CMake**

`app/CMakeLists.txt` — add `mcp/mcp_tools.cpp` to `mosaic_app`'s source list (any position; grouped near the other non-`run/` sources like `asset/`/`plugin/` is fine):

```cmake
add_library(mosaic_app STATIC
  cli/options.cpp
  run/status_reporter.cpp
  run/editor.cpp
  run/create.cpp
  run/docs.cpp
  run/build.cpp
  run/import.cpp
  run/plugin.cpp
  asset/font_cook.cpp
  asset/import_plugins.cpp
  plugin/git.cpp
  mcp/mcp_tools.cpp
)
```

No `target_link_libraries` changes needed — `mosaic_app` already links `mosaic_engine`, `mosaic_serialization`, and `nlohmann_json::nlohmann_json` (`PUBLIC`), which cover everything this file uses.

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build build -j"$(nproc)" && ./build/tests/mosaic_unit_tests "[mcp]"`
Expected: PASS — all 10 new cases.

- [ ] **Step 7: Commit**

```bash
git add app/mcp/mcp_tools.hpp app/mcp/mcp_tools.cpp app/CMakeLists.txt \
       tests/unit/app/mcp_tools_test.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(mcp): add the Agent API tool-dispatch core

app/mcp/mcp_tools mirrors world_globals.cpp's existing Python surface
(entity/component CRUD + undo/redo/save_scene) but returns/accepts
nlohmann::json instead of py::object, for the upcoming MCP server.
No Python involvement -- direct World/ComponentReflection/CommandStack
binding, same architectural move install_imgui_globals made for a
different transport.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01CEBXDEjzbGDpMyLrKCN4mY
EOF
)"
```

---

### Task 2: `app/mcp/mcp_protocol` — JSON-RPC/stdio framing

**Files:**
- Create: `app/mcp/mcp_protocol.hpp`
- Create: `app/mcp/mcp_protocol.cpp`
- Modify: `app/CMakeLists.txt` — add `mcp/mcp_protocol.cpp`.
- Test: `tests/unit/app/mcp_protocol_test.cpp`
- Modify: `tests/CMakeLists.txt` — register the new test file.

**Interfaces:**
- Consumes: `mosaic::app::mcp::tools_list_schema()` (Task 1, called directly — no injection needed, it takes no `World` and is pure).
- Produces: `using mosaic::app::mcp::ToolDispatch = std::function<nlohmann::json(const std::string&, const nlohmann::json&)>`; `std::string mosaic::app::mcp::handle_request_line(const std::string& line, const ToolDispatch& dispatch)`; `void mosaic::app::mcp::run_stdio_loop(std::istream& in, std::ostream& out, const ToolDispatch& dispatch)`. Consumed by Task 3 (`run/mcp.cpp` binds a dispatch closure over the real `World`/`ComponentReflection`/`CommandStack`/scene path and calls `run_stdio_loop(std::cin, std::cout, dispatch)`).

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/app/mcp_protocol_test.cpp`:

```cpp
#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "app/mcp/mcp_protocol.hpp"

using mosaic::app::mcp::handle_request_line;
using mosaic::app::mcp::run_stdio_loop;
using mosaic::app::mcp::ToolDispatch;

namespace
{
ToolDispatch echo_dispatch()
{
    return [](const std::string& tool_name, const nlohmann::json& arguments) -> nlohmann::json
    { return {{"called", tool_name}, {"arguments", arguments}}; };
}

ToolDispatch throwing_dispatch(const std::string& message)
{
    return [message](const std::string&, const nlohmann::json&) -> nlohmann::json
    { throw std::runtime_error(message); };
}
} // namespace

TEST_CASE("mcp_protocol: initialize returns server info", "[app][mcp]")
{
    const std::string response = handle_request_line(
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})", echo_dispatch());
    const nlohmann::json parsed = nlohmann::json::parse(response);
    REQUIRE(parsed.at("id").get<int>() == 1);
    REQUIRE(parsed.at("result").at("serverInfo").at("name").get<std::string>() == "mosaic-mcp");
}

TEST_CASE("mcp_protocol: notifications/initialized produces no response", "[app][mcp]")
{
    const std::string response =
        handle_request_line(R"({"jsonrpc":"2.0","method":"notifications/initialized"})", echo_dispatch());
    REQUIRE(response.empty());
}

TEST_CASE("mcp_protocol: tools/list returns the full tool schema", "[app][mcp]")
{
    const std::string response =
        handle_request_line(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})", echo_dispatch());
    const nlohmann::json parsed = nlohmann::json::parse(response);
    REQUIRE(parsed.at("result").at("tools").size() == 15);
}

TEST_CASE("mcp_protocol: tools/call dispatches and wraps a successful result", "[app][mcp]")
{
    const std::string response = handle_request_line(
        R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"create_entity","arguments":{}}})",
        echo_dispatch());
    const nlohmann::json parsed = nlohmann::json::parse(response);
    REQUIRE(parsed.at("result").at("isError").get<bool>() == false);
    const nlohmann::json inner = nlohmann::json::parse(
        parsed.at("result").at("content").at(0).at("text").get<std::string>());
    REQUIRE(inner.at("called").get<std::string>() == "create_entity");
}

TEST_CASE("mcp_protocol: tools/call wraps a dispatch exception as isError:true, not a crash", "[app][mcp]")
{
    const std::string response = handle_request_line(
        R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"anything","arguments":{}}})",
        throwing_dispatch("unknown tool: anything"));
    const nlohmann::json parsed = nlohmann::json::parse(response);
    REQUIRE(parsed.at("result").at("isError").get<bool>() == true);
    REQUIRE(parsed.at("result").at("content").at(0).at("text").get<std::string>() == "unknown tool: anything");
}

TEST_CASE("mcp_protocol: malformed JSON on the line yields a JSON-RPC parse error", "[app][mcp]")
{
    const std::string response = handle_request_line("{not json", echo_dispatch());
    const nlohmann::json parsed = nlohmann::json::parse(response);
    REQUIRE(parsed.at("error").at("code").get<int>() == -32700);
}

TEST_CASE("mcp_protocol: syntactically valid but non-object JSON yields Invalid Request, not a crash", "[app][mcp]")
{
    const std::string response = handle_request_line("42", echo_dispatch());
    const nlohmann::json parsed = nlohmann::json::parse(response);
    REQUIRE(parsed.at("error").at("code").get<int>() == -32600);
}

TEST_CASE("mcp_protocol: a non-object 'params' is treated as empty, not a crash", "[app][mcp]")
{
    const std::string response = handle_request_line(
        R"({"jsonrpc":"2.0","id":6,"method":"tools/call","params":42})", echo_dispatch());
    const nlohmann::json parsed = nlohmann::json::parse(response);
    REQUIRE(parsed.at("result").at("isError").get<bool>() == false);
}

TEST_CASE("mcp_protocol: unknown method yields a JSON-RPC method-not-found error", "[app][mcp]")
{
    const std::string response =
        handle_request_line(R"({"jsonrpc":"2.0","id":5,"method":"not/a/real/method"})", echo_dispatch());
    const nlohmann::json parsed = nlohmann::json::parse(response);
    REQUIRE(parsed.at("error").at("code").get<int>() == -32601);
}

TEST_CASE("mcp_protocol: an unknown method with no id is a silent notification, not an error", "[app][mcp]")
{
    const std::string response =
        handle_request_line(R"({"jsonrpc":"2.0","method":"not/a/real/method"})", echo_dispatch());
    REQUIRE(response.empty());
}

TEST_CASE("mcp_protocol: run_stdio_loop processes multiple lines and skips notification responses", "[app][mcp]")
{
    std::istringstream in(
        "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}\n");
    std::ostringstream out;
    run_stdio_loop(in, out, echo_dispatch());

    std::istringstream lines(out.str());
    std::string line;
    int count = 0;
    while (std::getline(lines, line))
    {
        REQUIRE_FALSE(line.empty());
        ++count;
    }
    REQUIRE(count == 2); // the notification produced no output line
}
```

Add the new test file to `tests/CMakeLists.txt`, right after `unit/app/mcp_tools_test.cpp`:

```cmake
  unit/app/mcp_protocol_test.cpp
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build -j"$(nproc)"`
Expected: FAIL to configure/build — `app/mcp/mcp_protocol.hpp` doesn't exist yet.

- [ ] **Step 3: Create `mcp_protocol.hpp`**

```cpp
#pragma once

#include <functional>
#include <istream>
#include <ostream>
#include <string>

#include <nlohmann/json.hpp>

namespace mosaic::app::mcp
{
// Invokes one MCP tool by name with its JSON arguments and returns its JSON result, or
// throws (any std::exception) to signal a tool-level failure -- caught here and reported
// as an MCP isError:true result, never propagated. Bound by app/run/mcp.cpp over the real
// World/ComponentReflection/CommandStack/scene path; a test can inject a stub.
using ToolDispatch = std::function<nlohmann::json(const std::string& tool_name, const nlohmann::json& arguments)>;

// Handles one JSON-RPC 2.0 request/notification line: routes "initialize", "tools/list",
// and "tools/call" (the latter through `dispatch`); an unknown method yields a JSON-RPC
// "method not found" error. Returns the response line to write (no trailing newline), or
// an empty string for a notification (no "id" field) or a blank input line -- a
// notification never produces a response, even for a recognized method. Never throws:
// malformed JSON on the line yields a JSON-RPC parse-error response instead of a crash.
std::string handle_request_line(const std::string& line, const ToolDispatch& dispatch);

// Reads newline-delimited JSON-RPC requests from `in` until EOF, writing each non-empty
// response line (terminated by '\n', flushed immediately) to `out`. Thin loop around
// handle_request_line -- the MCP stdio transport.
void run_stdio_loop(std::istream& in, std::ostream& out, const ToolDispatch& dispatch);
} // namespace mosaic::app::mcp
```

- [ ] **Step 4: Create `mcp_protocol.cpp`**

```cpp
#include "app/mcp/mcp_protocol.hpp"

#include <exception>
#include <string>

#include "app/mcp/mcp_tools.hpp"

namespace mosaic::app::mcp
{
namespace
{
nlohmann::json error_response(const nlohmann::json& id, int code, const std::string& message)
{
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}

nlohmann::json result_response(const nlohmann::json& id, const nlohmann::json& result)
{
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}
} // namespace

std::string handle_request_line(const std::string& line, const ToolDispatch& dispatch)
{
    if (line.empty())
    {
        return "";
    }

    nlohmann::json request;
    try
    {
        request = nlohmann::json::parse(line);
    }
    catch (const nlohmann::json::parse_error&)
    {
        return error_response(nullptr, -32700, "Parse error").dump();
    }

    // Syntactically valid JSON that isn't a request object (a bare number, string, or
    // array) -- .value()/.at() below require an object and would otherwise throw an
    // uncaught type_error. contains() itself is safe on any type (false for non-objects),
    // so this check must come first.
    if (!request.is_object())
    {
        return error_response(nullptr, -32600, "Invalid Request").dump();
    }

    const bool has_id = request.contains("id");
    const nlohmann::json id = has_id ? request.at("id") : nlohmann::json(nullptr);
    const std::string method = request.value("method", std::string());
    // Same non-object guard as above, one level deeper: a syntactically valid but
    // wrongly-typed "params" (e.g. a bare number) must not make the tools/call branch's
    // params.value(...) calls throw.
    nlohmann::json params = request.value("params", nlohmann::json::object());
    if (!params.is_object())
    {
        params = nlohmann::json::object();
    }

    nlohmann::json result;
    if (method == "initialize")
    {
        result = {{"protocolVersion", "2024-11-05"},
                 {"capabilities", {{"tools", nlohmann::json::object()}}},
                 {"serverInfo", {{"name", "mosaic-mcp"}, {"version", "0.1.0"}}}};
    }
    else if (method == "notifications/initialized")
    {
        return "";
    }
    else if (method == "tools/list")
    {
        result = tools_list_schema();
    }
    else if (method == "tools/call")
    {
        const std::string tool_name = params.value("name", std::string());
        const nlohmann::json arguments = params.value("arguments", nlohmann::json::object());
        try
        {
            const nlohmann::json tool_result = dispatch(tool_name, arguments);
            result = {{"content", {{{"type", "text"}, {"text", tool_result.dump()}}}}, {"isError", false}};
        }
        catch (const std::exception& e)
        {
            result = {{"content", {{{"type", "text"}, {"text", std::string(e.what())}}}}, {"isError", true}};
        }
    }
    else
    {
        if (!has_id) { return ""; }
        return error_response(id, -32601, "Method not found: " + method).dump();
    }

    if (!has_id)
    {
        return "";
    }
    return result_response(id, result).dump();
}

void run_stdio_loop(std::istream& in, std::ostream& out, const ToolDispatch& dispatch)
{
    std::string line;
    while (std::getline(in, line))
    {
        const std::string response = handle_request_line(line, dispatch);
        if (!response.empty())
        {
            out << response << '\n';
            out.flush();
        }
    }
}
} // namespace mosaic::app::mcp
```

- [ ] **Step 5: Register the new source file in CMake**

`app/CMakeLists.txt`:

```cmake
add_library(mosaic_app STATIC
  cli/options.cpp
  run/status_reporter.cpp
  run/editor.cpp
  run/create.cpp
  run/docs.cpp
  run/build.cpp
  run/import.cpp
  run/plugin.cpp
  asset/font_cook.cpp
  asset/import_plugins.cpp
  plugin/git.cpp
  mcp/mcp_tools.cpp
  mcp/mcp_protocol.cpp
)
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build build -j"$(nproc)" && ./build/tests/mosaic_unit_tests "[mcp]"`
Expected: PASS — all 10 Task 1 cases plus all 10 new Task 2 cases (20 total under `[mcp]`).

- [ ] **Step 7: Commit**

```bash
git add app/mcp/mcp_protocol.hpp app/mcp/mcp_protocol.cpp app/CMakeLists.txt \
       tests/unit/app/mcp_protocol_test.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(mcp): add the MCP JSON-RPC/stdio framing layer

app/mcp/mcp_protocol handles the newline-delimited JSON-RPC 2.0 stdio
transport (initialize, tools/list, tools/call) over an injected
ToolDispatch callback, kept independent of any World/engine state so
it's testable with a stub dispatch. Never throws across the boundary --
malformed JSON or a dispatch exception become a JSON-RPC/tool error
response instead of a crash.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01CEBXDEjzbGDpMyLrKCN4mY
EOF
)"
```

---

### Task 3: `mosaic mcp <project-dir>` CLI subcommand

**Files:**
- Modify: `app/cli/options.hpp` — `Command::Mcp`, `Options::mcp_project`.
- Modify: `app/cli/options.cpp` — subcommand wiring.
- Create: `app/run/mcp.hpp`, `app/run/mcp.cpp` — startup + protocol loop.
- Modify: `app/main.cpp` — dispatch `Command::Mcp` to `run_mcp`.
- Modify: `app/CMakeLists.txt` — add `run/mcp.cpp`.
- Test: `tests/unit/app/options_test.cpp` (extended).
- Modify: `docs/project/next-steps.md` — new dated entry.

**Interfaces:**
- Consumes: `mosaic::app::mcp::handle_tool_call` (Task 1), `mosaic::app::mcp::run_stdio_loop` (Task 2), `mosaic::app::load_manifest`/`ProjectManifest` (`app/project/project_io.hpp`, `app/project/project.hpp`, pre-existing), `mosaic::app::populate_world` (`app/run/scene_load.hpp`, pre-existing), `register_builtin_components` (`engine/reflection/builtin_components.hpp`, pre-existing).
- Produces: `int mosaic::app::run_mcp(const Options& options)`. Consumed by `main.cpp`'s dispatch only.

- [ ] **Step 1: Write the failing test**

Extend `tests/unit/app/options_test.cpp` — read the file first to match its existing style, then append (adjust the exact assertion helpers/macros to match whatever the file already uses for other subcommands with a required positional, e.g. `build`):

```cpp
TEST_CASE("mcp parses project directory into Command::Mcp", "[cli]")
{
    const char* argv[] = {"mosaic", "mcp", "/tmp/some-project"};
    ParseResult pr = parse(3, const_cast<char**>(argv));
    REQUIRE_FALSE(pr.exit_code.has_value());
    REQUIRE(pr.options.command == Command::Mcp);
    REQUIRE(pr.options.mcp_project.has_value());
    REQUIRE(*pr.options.mcp_project == "/tmp/some-project");
}

TEST_CASE("mcp without a project directory is rejected", "[cli]")
{
    const char* argv[] = {"mosaic", "mcp"};
    ParseResult pr = parse(2, const_cast<char**>(argv));
    REQUIRE(pr.exit_code.has_value());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build -j"$(nproc)"`
Expected: FAIL to build — `Command::Mcp` and `Options::mcp_project` don't exist yet.

- [ ] **Step 3: Add `Command::Mcp` and `Options::mcp_project`**

In `app/cli/options.hpp`, extend the enum:

```cpp
enum class Command { Run, Editor, Create, Build, Import, Docs, PluginList, PluginInstall, PluginRemove,
                    PluginSourceAdd, PluginSourceList, PluginSourceRemove, PluginSourcePull, Mcp };
```

Add a field to `Options` (near `build_project`, `import_project`):

```cpp
std::optional<std::string>   mcp_project;  // positional: project dir to serve (mcp)
```

- [ ] **Step 4: Wire the `mcp` subcommand in `options.cpp`**

Update line 15's comment to mention `mcp`:

```cpp
app.require_subcommand(1); // exactly one subcommand (`run`, `editor`, `create`, `build`, `import`, `docs`, `plugin`, or `mcp`)
```

Add the subcommand (placed after the `import` block, before `docs`):

```cpp
CLI::App* mcp = app.add_subcommand("mcp", "Run an MCP (Model Context Protocol) server for a project");
std::string mcp_project_name;
mcp->add_option("project", mcp_project_name, "Project directory to open")->required();
```

Add its dispatch, next to `import`'s (right after the `if (import->parsed()) { ... }` block):

```cpp
if (mcp->parsed())
{
    result.options.command = Command::Mcp;
}
if (!mcp_project_name.empty())
{
    result.options.mcp_project = mcp_project_name;
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build -j"$(nproc)" && ./build/tests/mosaic_unit_tests "[cli]"`
Expected: PASS — the 2 new cases plus every pre-existing `[cli]` case unaffected.

- [ ] **Step 6: Create `app/run/mcp.hpp`**

```cpp
#pragma once

#include "app/cli/options.hpp"

namespace mosaic::app
{
// Handle `mosaic mcp <project-dir>`: resolve the project's manifest + default scene,
// build a resident World/ComponentReflection/CommandStack, and run an MCP server over
// stdin/stdout until EOF. See
// docs/superpowers/specs/2026-09-16-agent-api-mcp-server-design.md.
int run_mcp(const Options& options);
} // namespace mosaic::app
```

- [ ] **Step 7: Create `app/run/mcp.cpp`**

```cpp
#include "app/run/mcp.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

#include "app/mcp/mcp_protocol.hpp"
#include "app/mcp/mcp_tools.hpp"
#include "app/project/project.hpp"
#include "app/project/project_io.hpp"
#include "app/run/scene_load.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/world/world.hpp"
#include "foundation/commands/command_stack.hpp"
#include "foundation/logging/log.hpp"
#include "foundation/reflection/type_registry.hpp"

namespace mosaic::app
{
int run_mcp(const Options& options)
{
    if (!options.mcp_project)
    {
        mosaic::foundation::log::error("mcp", "no project directory given");
        return 1;
    }
    const std::string& project_dir = *options.mcp_project;

    const std::optional<ProjectManifest> manifest = load_manifest(project_dir);
    if (!manifest)
    {
        mosaic::foundation::log::error("mcp", "not a Mosaic project (no mosaic.toml): {}", project_dir);
        return 1;
    }
    const std::string scene_path =
        (std::filesystem::path(project_dir) / manifest->default_scene).string();

    mosaic::foundation::reflection::TypeRegistry registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);

    mosaic::engine::World world;
    mosaic::foundation::commands::CommandStack stack;

    Options load_options;
    load_options.scene = scene_path;
    if (!populate_world(world, load_options, types))
    {
        mosaic::foundation::log::error("mcp", "failed to load scene: {}", scene_path);
        return 1;
    }

    const mcp::ToolDispatch dispatch =
        [&](const std::string& tool_name, const nlohmann::json& arguments) -> nlohmann::json
    { return mcp::handle_tool_call(world, types, stack, scene_path, tool_name, arguments); };

    mosaic::foundation::log::info("mcp", "serving project {} ({} entities)", project_dir,
                                  world.entity_count());
    mcp::run_stdio_loop(std::cin, std::cout, dispatch);
    return 0;
}
} // namespace mosaic::app
```

- [ ] **Step 8: Wire dispatch in `app/main.cpp`**

Add the include alongside the other `app/run/*.hpp` includes:

```cpp
#include "app/run/mcp.hpp"
```

Add the dispatch block alongside `Command::Import`'s (before the "smart positional" `open_path` resolution block, matching where `Docs`/`Import`/the plugin commands already return early):

```cpp
if (pr.options.command == Command::Mcp)
{
    return run_mcp(pr.options);
}
```

- [ ] **Step 9: Register the new source file in CMake**

`app/CMakeLists.txt`:

```cmake
add_library(mosaic_app STATIC
  cli/options.cpp
  run/status_reporter.cpp
  run/editor.cpp
  run/create.cpp
  run/docs.cpp
  run/build.cpp
  run/import.cpp
  run/plugin.cpp
  run/mcp.cpp
  asset/font_cook.cpp
  asset/import_plugins.cpp
  plugin/git.cpp
  mcp/mcp_tools.cpp
  mcp/mcp_protocol.cpp
)
```

- [ ] **Step 10: Build and manually verify against a real project**

Run:

```bash
cmake --build build -j"$(nproc)"
rm -rf /tmp/mcp_verify && ./build/app/mosaic create /tmp/mcp_verify
printf '%s\n%s\n%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/list"}' \
  '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"create_entity","arguments":{}}}' \
  | ./build/app/mosaic mcp /tmp/mcp_verify
```

Expected: three JSON-RPC response lines on stdout (server info, a 15-tool list, and `{"id":3,"result":{"content":[{"type":"text","text":"{\"id\":...}"}],"isError":false}}`), with `[INFO] mcp: serving project ...` on stderr. Inspect the output by eye — no crash, no malformed JSON.

- [ ] **Step 11: Run the full test suite**

Run: `cmake --build build -j"$(nproc)" && ctest --test-dir build --output-on-failure`
Expected: green aside from the known pre-existing flakes (the threaded-Python SIGSEGV under full-suite ordering, and the `--host`/`--connect` `ctest`-discovery quirk for `--`-prefixed `TEST_CASE` names) — see project memory `mosaic-threaded-script-test-flaky-segv` and `catch2-test-names-no-dashes`. Zero new failures.

- [ ] **Step 12: Add the dated `next-steps.md` entry**

Prepend a new dated paragraph to the top of the changelog blob in `docs/project/next-steps.md` (immediately before the current most-recent `**Last updated:**` entry, in the same style as the existing entries), summarizing: `mosaic mcp <project-dir>` ships as Phase 6 sub-project 1 of 7 (Agent API); a headless MCP server exposing 15 entity/component CRUD + undo/redo/save_scene tools over stdio JSON-RPC, backed directly by `World`/`ComponentReflection`/`CommandStack` with no Python involvement; v1 scope explicitly excludes CLI-verb wrapping and live-Studio attachment (documented future expansion); next Phase 6 sub-project is scene reasoning (2 of 7). Link this plan and the spec.

- [ ] **Step 13: Commit**

```bash
git add app/cli/options.hpp app/cli/options.cpp app/run/mcp.hpp app/run/mcp.cpp app/main.cpp \
       app/CMakeLists.txt tests/unit/app/options_test.cpp docs/project/next-steps.md
git commit -m "$(cat <<'EOF'
feat(cli): add mosaic mcp <project-dir> Agent API server

Wires the Task 1/2 tool-dispatch core and JSON-RPC framing into a new
CLI subcommand: resolves the project's manifest + default scene, loads
a resident World, and serves MCP over stdin/stdout until EOF. Closes
out Phase 6 sub-project 1 of 7 (Agent API).

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01CEBXDEjzbGDpMyLrKCN4mY
EOF
)"
```

---

## Self-Review Notes

- **Spec coverage:** headless startup via `load_manifest`/`populate_world` (Task 3) ✓; persistent `World`+`CommandStack` session model (Task 1's `handle_tool_call` signature takes them by reference, held open across calls by Task 3's loop) ✓; MCP stdio JSON-RPC framing with `initialize`/`tools/list`/`tools/call` (Task 2) ✓; all 15 v1 tools mirroring `world_globals.cpp` (Task 1) ✓; graceful not-found vs. `McpToolError` distinction (Task 1's handlers + tests) ✓; `Value`↔JSON via the public `to_json`/`from_json` string round-trip, not the private node helpers (Task 1) ✓; no `AssetRegistry` construction (Task 3's `run_mcp` never builds one) ✓; CLI wiring following `build`'s required-positional pattern (Task 3) ✓; manual verification + next-steps.md entry (Task 3) ✓.
- **Placeholder scan:** no TBD/TODO; every step has real code or an exact command.
- **Type consistency:** `mcp::ToolDispatch` is defined once in Task 2 and used identically in Task 2's tests and Task 3's `run_mcp`; `mcp::handle_tool_call`'s signature is defined in Task 1 and called unchanged in Task 3; `mcp::tools_list_schema()` is defined in Task 1 and called unchanged in Task 2.
