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
#include "engine/transform/transform.hpp"
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

TEST_CASE("mcp_tools: tools_list_schema lists all 16 v1 tools with a description and inputSchema", "[app][mcp]")
{
    const nlohmann::json schema = tools_list_schema();
    REQUIRE(schema.contains("tools"));
    const nlohmann::json tools = schema.at("tools");
    REQUIRE(tools.size() == 16);
    for (const auto& tool : tools)
    {
        REQUIRE(tool.contains("name"));
        REQUIRE(tool.contains("description"));
        REQUIRE_FALSE(tool.at("description").get<std::string>().empty());
        REQUIRE(tool.contains("inputSchema"));
        REQUIRE(tool.at("inputSchema").at("type").get<std::string>() == "object");
    }
}

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
