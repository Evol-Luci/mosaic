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
