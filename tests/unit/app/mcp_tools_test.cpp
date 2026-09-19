#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <unistd.h> // ::getpid (unique temp file under parallel ctest)

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "app/mcp/mcp_tools.hpp"
#include "app/asset/asset_io.hpp"
#include "app/project/project_io.hpp"
#include "app/run/scene_io.hpp"
#include "app/run/demo_scene.hpp"
#include "engine/assets/asset_registry.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/name.hpp"
#include "modules/script_python/python_script_engine.hpp" // ensure_interpreter, create_python_script_engine
#include "engine/world/world.hpp"
#include "foundation/commands/command_stack.hpp"
#include "foundation/reflection/type_registry.hpp"

using mosaic::app::mcp::handle_tool_call;
using mosaic::app::mcp::McpToolError;
using mosaic::app::mcp::ScriptEngineFactory;
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

    nlohmann::json call(const std::string& tool, const nlohmann::json& args,
                        const mosaic::app::mcp::ScriptEngineFactory& script_factory)
    {
        return handle_tool_call(world, types, stack, "", tool, args, script_factory);
    }

    nlohmann::json call(const std::string& tool, const nlohmann::json& args, const std::string& project_dir)
    {
        return handle_tool_call(world, types, stack, "", tool, args, {}, project_dir);
    }

    nlohmann::json call(const std::string& tool, const nlohmann::json& args, const std::string& project_dir,
                        mosaic::engine::assets::AssetRegistry& live_registry)
    {
        return handle_tool_call(world, types, stack, "", tool, args, {}, project_dir, live_registry, &live_registry);
    }
};

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
        return handle_tool_call(world, types, stack, "", tool, args, script_factory, "", asset_registry);
    }
};

// RuntimeFixture's setup_runtime() registers built-in meshes/a built-in font/demo textures into
// asset_registry that nothing in a fresh test World references via an AssetID field -- so those
// are legitimately orphaned by this tool's own definition, and orphaned_assets tests must check
// membership of the ids they care about rather than asserting the array's exact total size.
bool orphaned_contains(const nlohmann::json& orphaned, const std::string& asset_id)
{
    for (const auto& entry : orphaned)
    {
        if (entry.at("asset_id").get<std::string>() == asset_id) { return true; }
    }
    return false;
}
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

TEST_CASE("mcp_tools: tools_list_schema lists all 26 v1 tools with a description and inputSchema", "[app][mcp]")
{
    const nlohmann::json schema = tools_list_schema();
    REQUIRE(schema.contains("tools"));
    const nlohmann::json tools = schema.at("tools");
    REQUIRE(tools.size() == 26);
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

TEST_CASE("mcp_tools: set_script test-compiles and writes a valid script into an existing ScriptComponent", "[app][mcp]")
{
    mosaic::script::python::ensure_interpreter();
    Fixture f;
    const std::uint64_t eid = f.call("create_entity").at("id").get<std::uint64_t>();
    f.call("add_component", {{"entity", eid}, {"component", "ScriptComponent"}});

    const ScriptEngineFactory factory = [] { return mosaic::script::python::create_python_script_engine(); };
    const nlohmann::json result =
        f.call("set_script", {{"entity", eid}, {"source", "def on_update(dt):\n    pass\n"}}, factory);
    REQUIRE(result.at("ok").get<bool>() == true);

    const nlohmann::json component = f.call("get_component", {{"entity", eid}, {"component", "ScriptComponent"}});
    REQUIRE(component.at("value").at("source").get<std::string>() == "def on_update(dt):\n    pass\n");
}

TEST_CASE("mcp_tools: set_script leaves the component unchanged on a compile error", "[app][mcp]")
{
    mosaic::script::python::ensure_interpreter();
    Fixture f;
    const std::uint64_t eid = f.call("create_entity").at("id").get<std::uint64_t>();
    f.call("add_component", {{"entity", eid}, {"component", "ScriptComponent"}});

    const ScriptEngineFactory factory = [] { return mosaic::script::python::create_python_script_engine(); };
    const nlohmann::json result = f.call("set_script", {{"entity", eid}, {"source", "def on_update(dt:\n"}}, factory);
    REQUIRE(result.at("ok").get<bool>() == false);
    REQUIRE_FALSE(result.at("message").get<std::string>().empty());

    const nlohmann::json component = f.call("get_component", {{"entity", eid}, {"component", "ScriptComponent"}});
    REQUIRE(component.at("value").at("source").get<std::string>().empty());
}

TEST_CASE("mcp_tools: set_script reports failure for an entity with no ScriptComponent", "[app][mcp]")
{
    mosaic::script::python::ensure_interpreter();
    Fixture f;
    const std::uint64_t eid = f.call("create_entity").at("id").get<std::uint64_t>();

    const ScriptEngineFactory factory = [] { return mosaic::script::python::create_python_script_engine(); };
    const nlohmann::json result = f.call("set_script", {{"entity", eid}, {"source", "pass"}}, factory);
    REQUIRE(result.at("ok").get<bool>() == false);
    REQUIRE(result.at("message").get<std::string>().find("ScriptComponent") != std::string::npos);
}

TEST_CASE("mcp_tools: set_script throws McpToolError for malformed arguments", "[app][mcp]")
{
    Fixture f;
    REQUIRE_THROWS_AS(f.call("set_script", {{"source", "pass"}}), McpToolError);
    REQUIRE_THROWS_AS(f.call("set_script", {{"entity", 1}}), McpToolError);
}

TEST_CASE("mcp_tools: import_asset cooks a PPM into the project and returns an asset_id", "[app][mcp]")
{
    Fixture f;
    const std::string proj = (std::filesystem::temp_directory_path() /
                              (std::string("mosaic_mcp_import_") + std::to_string(::getpid()))).string();
    std::filesystem::remove_all(proj);
    REQUIRE(mosaic::app::create_project(proj, f.types));

    const std::string src = (std::filesystem::temp_directory_path() /
                             (std::string("mosaic_mcp_import_src_") + std::to_string(::getpid()) + ".ppm")).string();
    std::string ppm = "P6\n2 1\n255\n";
    const unsigned char px[] = {255, 0, 0, 0, 255, 0};
    ppm.append(reinterpret_cast<const char*>(px), sizeof(px));
    REQUIRE(mosaic::app::save_scene_file(src, ppm));

    const nlohmann::json result = f.call("import_asset", {{"source", src}}, proj);
    REQUIRE_FALSE(result.at("asset_id").is_null());
    REQUIRE_FALSE(result.at("asset_id").get<std::string>().empty());

    const std::string stem = std::filesystem::path(src).stem().string();
    REQUIRE(std::filesystem::exists(std::filesystem::path(proj) / "assets" / (stem + ".image")));
    REQUIRE(std::filesystem::exists(std::filesystem::path(proj) / "assets" / (stem + ".toml")));

    std::filesystem::remove_all(proj);
    std::filesystem::remove(src);
}

TEST_CASE("mcp_tools: import_asset returns a null asset_id for a missing source file", "[app][mcp]")
{
    Fixture f;
    const std::string proj = (std::filesystem::temp_directory_path() /
                              (std::string("mosaic_mcp_import_missing_") + std::to_string(::getpid()))).string();
    std::filesystem::remove_all(proj);
    REQUIRE(mosaic::app::create_project(proj, f.types));

    const nlohmann::json result = f.call("import_asset", {{"source", "/no/such/file.ppm"}}, proj);
    REQUIRE(result.at("asset_id").is_null());

    std::filesystem::remove_all(proj);
}

TEST_CASE("mcp_tools: import_asset throws McpToolError for malformed arguments", "[app][mcp]")
{
    Fixture f;
    REQUIRE_THROWS_AS(f.call("import_asset", nlohmann::json::object(), std::string("/tmp")), McpToolError);
}

TEST_CASE("mcp_tools: import_asset refuses to overwrite on a stem collision", "[app][mcp]")
{
    Fixture f;
    const std::string proj = (std::filesystem::temp_directory_path() /
                              (std::string("mosaic_mcp_import_collide_") + std::to_string(::getpid()))).string();
    std::filesystem::remove_all(proj);
    REQUIRE(mosaic::app::create_project(proj, f.types));

    const std::string dir_a = (std::filesystem::temp_directory_path() /
                               (std::string("mosaic_mcp_import_cola_") + std::to_string(::getpid()))).string();
    const std::string dir_b = (std::filesystem::temp_directory_path() /
                               (std::string("mosaic_mcp_import_colb_") + std::to_string(::getpid()))).string();
    std::filesystem::remove_all(dir_a);
    std::filesystem::remove_all(dir_b);
    std::filesystem::create_directories(dir_a);
    std::filesystem::create_directories(dir_b);

    std::string ppm = "P6\n2 1\n255\n";
    const unsigned char px[] = {255, 0, 0, 0, 255, 0};
    ppm.append(reinterpret_cast<const char*>(px), sizeof(px));
    const std::string src_a = (std::filesystem::path(dir_a) / "tex.ppm").string();
    const std::string src_b = (std::filesystem::path(dir_b) / "tex.ppm").string();
    REQUIRE(mosaic::app::save_scene_file(src_a, ppm));
    REQUIRE(mosaic::app::save_scene_file(src_b, ppm));

    REQUIRE_FALSE(f.call("import_asset", {{"source", src_a}}, proj).at("asset_id").is_null());
    const std::filesystem::path meta = std::filesystem::path(proj) / "assets" / "tex.toml";
    const std::optional<std::string> before = mosaic::app::load_scene_file(meta.string());
    REQUIRE(before.has_value());

    REQUIRE(f.call("import_asset", {{"source", src_b}}, proj).at("asset_id").is_null());
    const std::optional<std::string> after = mosaic::app::load_scene_file(meta.string());
    REQUIRE(after.has_value());
    REQUIRE(*after == *before);

    std::filesystem::remove_all(proj);
    std::filesystem::remove_all(dir_a);
    std::filesystem::remove_all(dir_b);
}

TEST_CASE("mcp_tools: import_asset hot-reloads the new asset into the live AssetRegistry", "[app][mcp]")
{
    Fixture f;
    mosaic::engine::assets::AssetRegistry registry;
    const std::string proj = (std::filesystem::temp_directory_path() /
                              (std::string("mosaic_mcp_import_hotreload_") + std::to_string(::getpid()))).string();
    std::filesystem::remove_all(proj);
    REQUIRE(mosaic::app::create_project(proj, f.types));

    const std::string src = (std::filesystem::temp_directory_path() /
                             (std::string("mosaic_mcp_import_hotreload_src_") + std::to_string(::getpid()) + ".ppm")).string();
    std::string ppm = "P6\n2 1\n255\n";
    const unsigned char px[] = {255, 0, 0, 0, 255, 0};
    ppm.append(reinterpret_cast<const char*>(px), sizeof(px));
    REQUIRE(mosaic::app::save_scene_file(src, ppm));

    const nlohmann::json result = f.call("import_asset", {{"source", src}}, proj, registry);
    REQUIRE_FALSE(result.at("asset_id").is_null());
    const auto asset_id = mosaic::foundation::serialization::AssetID::from_string(result.at("asset_id").get<std::string>());

    // Same session, no restart: the just-imported asset is immediately visible in the registry
    // that was passed in by reference -- this is the whole point of hot-reload.
    REQUIRE(registry.contains(asset_id));

    std::filesystem::remove_all(proj);
    std::filesystem::remove(src);
}

TEST_CASE("mcp_tools: import_asset without a live registry does not crash (default nullptr)", "[app][mcp]")
{
    Fixture f;
    const std::string proj = (std::filesystem::temp_directory_path() /
                              (std::string("mosaic_mcp_import_norefresh_") + std::to_string(::getpid()))).string();
    std::filesystem::remove_all(proj);
    REQUIRE(mosaic::app::create_project(proj, f.types));

    const std::string src = (std::filesystem::temp_directory_path() /
                             (std::string("mosaic_mcp_import_norefresh_src_") + std::to_string(::getpid()) + ".ppm")).string();
    std::string ppm = "P6\n2 1\n255\n";
    const unsigned char px[] = {255, 0, 0, 0, 255, 0};
    ppm.append(reinterpret_cast<const char*>(px), sizeof(px));
    REQUIRE(mosaic::app::save_scene_file(src, ppm));

    // No live registry supplied (matches every pre-existing import_asset call site) -- must
    // still succeed exactly as before, simply skipping the refresh.
    REQUIRE_FALSE(f.call("import_asset", {{"source", src}}, proj).at("asset_id").is_null());

    std::filesystem::remove_all(proj);
    std::filesystem::remove(src);
}

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

TEST_CASE("mcp_tools: build_project packages a project into a standalone bundle", "[app][mcp]")
{
    Fixture f;
    const std::string proj = (std::filesystem::temp_directory_path() /
                              (std::string("mosaic_mcp_build_") + std::to_string(::getpid()))).string();
    std::filesystem::remove_all(proj);
    REQUIRE(mosaic::app::create_project(proj, f.types));

    const nlohmann::json result = f.call("build_project", nlohmann::json::object(), proj);
    REQUIRE(result.at("ok").get<bool>() == true);
    REQUIRE(std::filesystem::exists(std::filesystem::path(proj) / "dist"));

    std::filesystem::remove_all(proj);
}

TEST_CASE("mcp_tools: build_project honors a custom out_dir argument", "[app][mcp]")
{
    Fixture f;
    const std::string proj = (std::filesystem::temp_directory_path() /
                              (std::string("mosaic_mcp_build_custom_") + std::to_string(::getpid()))).string();
    std::filesystem::remove_all(proj);
    REQUIRE(mosaic::app::create_project(proj, f.types));
    const std::string out = (std::filesystem::temp_directory_path() /
                             (std::string("mosaic_mcp_build_out_") + std::to_string(::getpid()))).string();
    std::filesystem::remove_all(out);

    const nlohmann::json result = f.call("build_project", {{"out_dir", out}}, proj);
    REQUIRE(result.at("ok").get<bool>() == true);
    REQUIRE(std::filesystem::exists(out));

    std::filesystem::remove_all(proj);
    std::filesystem::remove_all(out);
}

TEST_CASE("mcp_tools: build_project returns ok:false for a non-project directory", "[app][mcp]")
{
    Fixture f;
    const std::string notproj = (std::filesystem::temp_directory_path() /
                                 (std::string("mosaic_mcp_build_notproj_") + std::to_string(::getpid()))).string();
    std::filesystem::remove_all(notproj);
    std::filesystem::create_directories(notproj);

    const nlohmann::json result = f.call("build_project", nlohmann::json::object(), notproj);
    REQUIRE(result.at("ok").get<bool>() == false);

    std::filesystem::remove_all(notproj);
}

TEST_CASE("mcp_tools: build_project throws McpToolError for a malformed out_dir", "[app][mcp]")
{
    Fixture f;
    REQUIRE_THROWS_AS(f.call("build_project", {{"out_dir", 123}}, std::string("/tmp")), McpToolError);
}

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

TEST_CASE("mcp_tools: orphaned_assets reports a registered asset nothing references", "[app][mcp]")
{
    RuntimeFixture f;
    mosaic::engine::assets::AssetMetadata meta;
    meta.uuid = mosaic::app::random_asset_id();
    meta.type = "image";
    meta.source = "textures/unused.png";
    f.asset_registry.register_asset(meta, []() -> std::shared_ptr<void> { return nullptr; });

    const nlohmann::json result = f.call("orphaned_assets");
    REQUIRE(orphaned_contains(result.at("orphaned"), meta.uuid.to_string()));
    for (const auto& entry : result.at("orphaned"))
    {
        if (entry.at("asset_id").get<std::string>() == meta.uuid.to_string())
        {
            REQUIRE(entry.at("type").get<std::string>() == "image");
            REQUIRE(entry.at("source").get<std::string>() == "textures/unused.png");
        }
    }
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
    REQUIRE_FALSE(orphaned_contains(result.at("orphaned"), meta.uuid.to_string()));
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
    REQUIRE(orphaned_contains(result.at("orphaned"), meta.uuid.to_string()));
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
    REQUIRE(orphaned_contains(result.at("orphaned"), orphan_a.uuid.to_string()));
    REQUIRE(orphaned_contains(result.at("orphaned"), orphan_b.uuid.to_string()));
    REQUIRE_FALSE(orphaned_contains(result.at("orphaned"), referenced_meta.uuid.to_string()));
}

TEST_CASE("mcp_tools: orphaned_assets returns an empty array for an empty registry", "[app][mcp]")
{
    // A genuinely empty AssetRegistry, bypassing RuntimeFixture's own pre-populated one (which
    // always carries setup_runtime()'s built-in meshes/font/demo textures) -- this is the only
    // way to exercise a truly empty registry end to end.
    RuntimeFixture f;
    f.call("create_entity");
    mosaic::engine::assets::AssetRegistry empty_registry;
    const nlohmann::json result =
        handle_tool_call(f.world, f.types, f.stack, "", "orphaned_assets", nlohmann::json::object(),
                         f.script_factory, "", empty_registry);
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
    REQUIRE(orphaned_contains(result.at("orphaned"), orphan.uuid.to_string()));
}

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

TEST_CASE("mcp_tools: bulk_set_field throws for a set.value of the wrong type and mutates nothing", "[app][mcp]")
{
    Fixture f;
    const std::uint64_t eid = f.call("create_entity").at("id").get<std::uint64_t>();
    f.call("add_component", {{"entity", eid}, {"component", "Name"}});
    f.call("set_component", {{"entity", eid}, {"component", "Name"}, {"value", {{"value", "Original"}}}});

    // Name.value is a std::string; a JSON number doesn't deserialize into it.
    REQUIRE_THROWS_AS(f.call("bulk_set_field",
        {{"component", "Name"}, {"set", {{"field", "value"}, {"value", 42}}}}), McpToolError);

    // Nothing net-mutated: value unchanged, and the failed batch's undo left the stack as it was.
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

TEST_CASE("mcp_tools: entities_referencing reports an entity whose AssetID field matches", "[app][mcp]")
{
    Fixture f;
    const auto target = mosaic::app::random_asset_id();
    const std::uint64_t eid = f.call("create_entity").at("id").get<std::uint64_t>();
    f.call("add_component", {{"entity", eid}, {"component", "Material"}});
    nlohmann::json mat = f.call("get_component", {{"entity", eid}, {"component", "Material"}}).at("value");
    mat["texture"] = target.to_string();
    REQUIRE(f.call("set_component", {{"entity", eid}, {"component", "Material"}, {"value", mat}}).at("ok").get<bool>());

    const nlohmann::json result = f.call("entities_referencing", {{"asset_id", target.to_string()}});
    REQUIRE(result.at("matches").size() == 1);
    const nlohmann::json entry = result.at("matches").at(0);
    REQUIRE(entry.at("entity").get<std::uint64_t>() == eid);
    REQUIRE(entry.at("component").get<std::string>() == "Material");
    REQUIRE(entry.at("field").get<std::string>() == "texture");
}

TEST_CASE("mcp_tools: entities_referencing does not report a different or nil AssetID", "[app][mcp]")
{
    Fixture f;
    const auto target = mosaic::app::random_asset_id();
    const auto other = mosaic::app::random_asset_id();

    const std::uint64_t with_other = f.call("create_entity").at("id").get<std::uint64_t>();
    f.call("add_component", {{"entity", with_other}, {"component", "Material"}});
    nlohmann::json mat = f.call("get_component", {{"entity", with_other}, {"component", "Material"}}).at("value");
    mat["texture"] = other.to_string();
    REQUIRE(f.call("set_component", {{"entity", with_other}, {"component", "Material"}, {"value", mat}}).at("ok").get<bool>());

    const std::uint64_t with_nil = f.call("create_entity").at("id").get<std::uint64_t>();
    f.call("add_component", {{"entity", with_nil}, {"component", "Material"}}); // texture defaults to nil

    const nlohmann::json result = f.call("entities_referencing", {{"asset_id", target.to_string()}});
    REQUIRE(result.at("matches").empty());
}

TEST_CASE("mcp_tools: entities_referencing reports every match across multiple entities/components", "[app][mcp]")
{
    Fixture f;
    const auto target = mosaic::app::random_asset_id();

    const std::uint64_t a = f.call("create_entity").at("id").get<std::uint64_t>();
    const std::uint64_t b = f.call("create_entity").at("id").get<std::uint64_t>();
    for (std::uint64_t eid : {a, b})
    {
        f.call("add_component", {{"entity", eid}, {"component", "Material"}});
        nlohmann::json mat = f.call("get_component", {{"entity", eid}, {"component", "Material"}}).at("value");
        mat["texture"] = target.to_string();
        REQUIRE(f.call("set_component", {{"entity", eid}, {"component", "Material"}, {"value", mat}}).at("ok").get<bool>());
    }

    const nlohmann::json result = f.call("entities_referencing", {{"asset_id", target.to_string()}});
    REQUIRE(result.at("matches").size() == 2);
    std::vector<std::uint64_t> ids;
    for (const auto& entry : result.at("matches")) { ids.push_back(entry.at("entity").get<std::uint64_t>()); }
    REQUIRE(std::find(ids.begin(), ids.end(), a) != ids.end());
    REQUIRE(std::find(ids.begin(), ids.end(), b) != ids.end());
}

TEST_CASE("mcp_tools: entities_referencing returns an empty array for an asset id nothing references", "[app][mcp]")
{
    Fixture f;
    f.call("create_entity");
    const auto target = mosaic::app::random_asset_id();
    const nlohmann::json result = f.call("entities_referencing", {{"asset_id", target.to_string()}});
    REQUIRE(result.at("matches").empty());
}

TEST_CASE("mcp_tools: entities_referencing throws McpToolError for a malformed asset_id", "[app][mcp]")
{
    Fixture f;
    REQUIRE_THROWS_AS(f.call("entities_referencing", {{"asset_id", "not-a-real-uuid"}}), McpToolError);
}

TEST_CASE("mcp_tools: entities_referencing throws McpToolError for a missing asset_id", "[app][mcp]")
{
    Fixture f;
    REQUIRE_THROWS_AS(f.call("entities_referencing", nlohmann::json::object()), McpToolError);
}
