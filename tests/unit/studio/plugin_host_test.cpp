#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/world/name.hpp"
#include "engine/world/world.hpp"
#include "foundation/commands/command_stack.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "modules/script_python/python_script_engine.hpp"
#include "modules/script_python_studio/studio_plugin_script_engine.hpp"
#include "studio/plugin_host.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::foundation::commands::CommandStack;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::script::python::create_python_script_engine;
using mosaic::script::python_studio::create_studio_plugin_script_engine;
using mosaic::studio::PluginHost;

namespace
{
namespace fs = std::filesystem;

// A fresh, uniquely-named temp directory for one test's plugin files.
fs::path make_plugin_dir(const char* tag)
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path dir = fs::temp_directory_path() /
        (std::string("mosaic_plugin_test_") + tag + "_" + std::to_string(now));
    fs::create_directories(dir);
    return dir;
}

void write_plugin(const fs::path& dir, const std::string& filename, const std::string& source)
{
    std::ofstream out(dir / filename);
    out << source;
}
} // namespace

TEST_CASE("PluginHost loads a plugin and its menu item mutates real World state", "[studio]")
{
    World world;
    TypeRegistry reg;
    ComponentReflection types;
    register_builtin_components(reg, types);
    CommandStack commands;

    const auto e = world.create_entity();
    world.add_component<mosaic::engine::Name>(e, mosaic::engine::Name{"before"});

    const fs::path dir = make_plugin_dir("basic");
    write_plugin(dir, "hello.py",
        "MOSAIC_MENU_ITEMS = {\"Say Hello\": \"on_say_hello\"}\n\n"
        "def on_say_hello():\n"
        "    set_component(" + std::to_string(e.value()) + ", \"Name\", {\"value\": \"greeted\"})\n");

    PluginHost host(create_python_script_engine);
    const auto errors = host.load(dir.string(), world, types, commands);
    REQUIRE(errors.empty());
    REQUIRE(host.menu_items().size() == 1);
    REQUIRE(host.menu_items()[0].label == "Say Hello");

    const auto result = host.invoke(host.menu_items()[0]);
    REQUIRE(result.ok);
    REQUIRE(world.get_component<mosaic::engine::Name>(e)->value == "greeted");

    fs::remove_all(dir);
}

TEST_CASE("PluginHost keeps each plugin's globals isolated", "[studio]")
{
    World world;
    TypeRegistry reg;
    ComponentReflection types;
    register_builtin_components(reg, types);
    CommandStack commands;

    const fs::path dir = make_plugin_dir("isolated");
    write_plugin(dir, "a.py", "shared_name = 'from_a'\n");
    write_plugin(dir, "b.py",
        "MOSAIC_MENU_ITEMS = {\"Check\": \"on_check\"}\n"
        "def on_check():\n"
        "    try:\n"
        "        print(shared_name)\n"
        "    except NameError:\n"
        "        print('isolated')\n");

    PluginHost host(create_python_script_engine);
    REQUIRE(host.load(dir.string(), world, types, commands).empty());
    REQUIRE(host.menu_items().size() == 1);

    const auto result = host.invoke(host.menu_items()[0]);
    REQUIRE(result.ok);
    REQUIRE(result.output.find("isolated") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("PluginHost skips a broken plugin but still loads its sibling", "[studio]")
{
    World world;
    TypeRegistry reg;
    ComponentReflection types;
    register_builtin_components(reg, types);
    CommandStack commands;

    const fs::path dir = make_plugin_dir("broken");
    write_plugin(dir, "broken.py", "this is not valid python !!\n");
    write_plugin(dir, "good.py",
        "MOSAIC_MENU_ITEMS = {\"Good\": \"on_good\"}\ndef on_good():\n    print('ok')\n");

    PluginHost host(create_python_script_engine);
    const auto errors = host.load(dir.string(), world, types, commands);
    REQUIRE(errors.size() == 1);
    REQUIRE(errors[0].find("broken") != std::string::npos);
    REQUIRE(host.menu_items().size() == 1);
    REQUIRE(host.menu_items()[0].label == "Good");

    fs::remove_all(dir);
}

TEST_CASE("PluginHost loads a plugin with no menu items and no error", "[studio]")
{
    World world;
    TypeRegistry reg;
    ComponentReflection types;
    register_builtin_components(reg, types);
    CommandStack commands;

    const fs::path dir = make_plugin_dir("nomenu");
    write_plugin(dir, "quiet.py", "x = 1\n");

    PluginHost host(create_python_script_engine);
    const auto errors = host.load(dir.string(), world, types, commands);
    REQUIRE(errors.empty());
    REQUIRE(host.menu_items().empty());

    fs::remove_all(dir);
}

TEST_CASE("PluginHost treats a non-table MOSAIC_MENU_ITEMS as a load error", "[studio]")
{
    World world;
    TypeRegistry reg;
    ComponentReflection types;
    register_builtin_components(reg, types);
    CommandStack commands;

    const fs::path dir = make_plugin_dir("badshape");
    write_plugin(dir, "bad.py", "MOSAIC_MENU_ITEMS = 5\n");

    PluginHost host(create_python_script_engine);
    const auto errors = host.load(dir.string(), world, types, commands);
    REQUIRE(errors.size() == 1);
    REQUIRE(host.menu_items().empty());

    fs::remove_all(dir);
}

TEST_CASE("PluginHost.load a second time replaces the previous plugin set", "[studio]")
{
    World world;
    TypeRegistry reg;
    ComponentReflection types;
    register_builtin_components(reg, types);
    CommandStack commands;

    const fs::path dir = make_plugin_dir("reload");
    write_plugin(dir, "one.py",
        "MOSAIC_MENU_ITEMS = {\"One\": \"on_one\"}\ndef on_one():\n    pass\n");

    PluginHost host(create_python_script_engine);
    REQUIRE(host.load(dir.string(), world, types, commands).empty());
    REQUIRE(host.menu_items().size() == 1);

    fs::remove(dir / "one.py");
    write_plugin(dir, "two.py",
        "MOSAIC_MENU_ITEMS = {\"Two\": \"on_two\"}\ndef on_two():\n    pass\n");

    REQUIRE(host.load(dir.string(), world, types, commands).empty());
    REQUIRE(host.menu_items().size() == 1);
    REQUIRE(host.menu_items()[0].label == "Two");

    fs::remove_all(dir);
}

TEST_CASE("PluginHost loads a plugin panel and invoke_panel mutates real World state", "[studio]")
{
    World world;
    TypeRegistry reg;
    ComponentReflection types;
    register_builtin_components(reg, types);
    CommandStack commands;

    const auto e = world.create_entity();
    world.add_component<mosaic::engine::Name>(e, mosaic::engine::Name{"before"});

    const fs::path dir = make_plugin_dir("panel");
    write_plugin(dir, "panel_plugin.py",
        "MOSAIC_PANELS = {\"My Panel\": \"draw_my_panel\"}\n\n"
        "def draw_my_panel():\n"
        "    set_component(" + std::to_string(e.value()) + ", \"Name\", {\"value\": \"drawn\"})\n");

    PluginHost host(create_python_script_engine);
    const auto errors = host.load(dir.string(), world, types, commands);
    REQUIRE(errors.empty());
    REQUIRE(host.panels().size() == 1);
    REQUIRE(host.panels()[0].label == "My Panel");
    REQUIRE(host.menu_items().empty());

    const auto* found = host.find_panel("My Panel");
    REQUIRE(found != nullptr);
    const auto result = host.invoke_panel(*found);
    REQUIRE(result.ok);
    REQUIRE(world.get_component<mosaic::engine::Name>(e)->value == "drawn");

    fs::remove_all(dir);
}

TEST_CASE("PluginHost treats a non-table MOSAIC_PANELS as a load error", "[studio]")
{
    World world;
    TypeRegistry reg;
    ComponentReflection types;
    register_builtin_components(reg, types);
    CommandStack commands;

    const fs::path dir = make_plugin_dir("badpanel");
    write_plugin(dir, "bad.py", "MOSAIC_PANELS = 5\n");

    PluginHost host(create_python_script_engine);
    const auto errors = host.load(dir.string(), world, types, commands);
    REQUIRE(errors.size() == 1);
    REQUIRE(host.panels().empty());

    fs::remove_all(dir);
}

TEST_CASE("PluginHost supports a plugin registering both menu items and panels", "[studio]")
{
    World world;
    TypeRegistry reg;
    ComponentReflection types;
    register_builtin_components(reg, types);
    CommandStack commands;

    const fs::path dir = make_plugin_dir("both");
    write_plugin(dir, "both.py",
        "MOSAIC_MENU_ITEMS = {\"Do Thing\": \"on_do_thing\"}\n"
        "MOSAIC_PANELS = {\"My Panel\": \"draw_my_panel\"}\n"
        "def on_do_thing():\n"
        "    pass\n"
        "def draw_my_panel():\n"
        "    pass\n");

    PluginHost host(create_python_script_engine);
    const auto errors = host.load(dir.string(), world, types, commands);
    REQUIRE(errors.empty());
    REQUIRE(host.menu_items().size() == 1);
    REQUIRE(host.panels().size() == 1);

    fs::remove_all(dir);
}

TEST_CASE("PluginHost::find_panel returns nullptr for an unknown name", "[studio]")
{
    PluginHost host(create_python_script_engine);
    REQUIRE(host.find_panel("Nope") == nullptr);
}

TEST_CASE("PluginHost with the Studio plugin engine still gives panels the gameplay globals",
         "[studio]")
{
    // No ImGui frame is active in a unit test, so this exercises only the gameplay-global
    // half of StudioPluginScriptEngine (the same half PluginHost's other tests exercise via
    // create_python_script_engine) -- proving the switch to the ImGui-aware engine didn't
    // lose or break get_component/set_component/create_entity/etc. The imgui_* globals
    // themselves are exercised manually (see docs/design/scripting.md); calling one outside
    // a panel draw is guarded to fail loudly (require_drawing()) rather than segfault.
    World world;
    TypeRegistry reg;
    ComponentReflection types;
    register_builtin_components(reg, types);
    CommandStack commands;

    const auto e = world.create_entity();
    world.add_component<mosaic::engine::Name>(e, mosaic::engine::Name{"before"});

    const fs::path dir = make_plugin_dir("studio_engine_panel");
    write_plugin(dir, "panel_plugin.py",
        "MOSAIC_PANELS = {\"My Panel\": \"draw_my_panel\"}\n\n"
        "def draw_my_panel():\n"
        "    set_component(" + std::to_string(e.value()) + ", \"Name\", {\"value\": \"drawn\"})\n");

    PluginHost host(create_studio_plugin_script_engine);
    const auto errors = host.load(dir.string(), world, types, commands);
    REQUIRE(errors.empty());
    REQUIRE(host.panels().size() == 1);

    const auto* found = host.find_panel("My Panel");
    REQUIRE(found != nullptr);
    const auto result = host.invoke_panel(*found);
    REQUIRE(result.ok);
    REQUIRE(world.get_component<mosaic::engine::Name>(e)->value == "drawn");

    fs::remove_all(dir);
}

TEST_CASE("StudioPluginScriptEngine rejects an imgui_* call outside an active panel draw",
         "[studio]")
{
    // Regression test for the null-GetCurrentWindow() crash: a plugin that calls an
    // imgui_* global from module-level code (i.e. outside call_function's draw window)
    // must fail with a normal script error, not corrupt or crash the process.
    World world;
    TypeRegistry reg;
    ComponentReflection types;
    register_builtin_components(reg, types);
    CommandStack commands;

    const fs::path dir = make_plugin_dir("studio_engine_no_frame");
    write_plugin(dir, "bad.py", "imgui_text(\"module scope, no panel draw active\")\n");

    PluginHost host(create_studio_plugin_script_engine);
    const auto errors = host.load(dir.string(), world, types, commands);
    REQUIRE(errors.size() == 1);

    fs::remove_all(dir);
}

TEST_CASE("PluginHost.load skips a disabled plugin's file entirely -- no run, no error, no menu item",
         "[studio]")
{
    World world;
    TypeRegistry reg;
    ComponentReflection types;
    register_builtin_components(reg, types);
    CommandStack commands;

    const fs::path dir = make_plugin_dir("disabled_skip");
    write_plugin(dir, "enabled.py", "MOSAIC_MENU_ITEMS = {\"On\": \"on_click\"}\n\n"
                                    "def on_click(): pass\n");
    write_plugin(dir, "disabled.py", "this is not even valid python (");

    PluginHost host(create_python_script_engine);
    const auto errors = host.load(dir.string(), world, types, commands, {"disabled.py"});

    REQUIRE(errors.empty()); // the broken-but-disabled file was never run, so never errored
    REQUIRE(host.menu_items().size() == 1);
    REQUIRE(host.menu_items()[0].plugin_name == "enabled");

    fs::remove_all(dir);
}

TEST_CASE("PluginHost.load with an empty disabled list behaves exactly as before (default arg)",
         "[studio]")
{
    World world;
    TypeRegistry reg;
    ComponentReflection types;
    register_builtin_components(reg, types);
    CommandStack commands;

    const fs::path dir = make_plugin_dir("disabled_default");
    write_plugin(dir, "hello.py", "MOSAIC_MENU_ITEMS = {}\n");

    PluginHost host(create_python_script_engine);
    REQUIRE(host.load(dir.string(), world, types, commands).empty());
    REQUIRE(host.menu_items().empty());

    fs::remove_all(dir);
}

