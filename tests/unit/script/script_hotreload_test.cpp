#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h> // ::getpid

#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/script/script_component.hpp"
#include "engine/script/script_system.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "modules/script_python/python_script_engine.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::script::ScriptComponent;
using mosaic::engine::script::ScriptSystem;
using mosaic::engine::transform::Transform;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::script::python::create_python_script_engine;

namespace fs = std::filesystem;

namespace
{
std::string mover_script(float x)
{
    return "def on_update(dt):\n"
           "    t = get_component(entity, 'Transform')\n"
           "    t['position']['x'] = " + std::to_string(x) + "\n"
           "    set_component(entity, 'Transform', t)\n";
}
fs::path tmp_py(const char* tag)
{
    return fs::temp_directory_path() /
           ("mosaic_hotreload_" + std::string(tag) + "_" + std::to_string(::getpid()) + ".py");
}
void write_file(const fs::path& p, const std::string& s)
{
    std::ofstream(p, std::ios::binary) << s;
}
} // namespace

TEST_CASE("ScriptComponent loads its on_update from a .py file", "[script]")
{
    const fs::path py = tmp_py("load");
    write_file(py, mover_script(1.0f));

    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    world.add_component<ScriptComponent>(e, ScriptComponent{"", py.string()}); // path set, no inline
    world.add_system<ScriptSystem>(types, [] { return create_python_script_engine(); });

    world.tick(0.1f);
    REQUIRE(world.get_component<Transform>(e)->position.x == 1.0f);
    fs::remove(py);
}

TEST_CASE("editing the .py file hot-reloads the script", "[script]")
{
    const fs::path py = tmp_py("reload");
    write_file(py, mover_script(1.0f));

    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    world.add_component<ScriptComponent>(e, ScriptComponent{"", py.string()});
    world.add_system<ScriptSystem>(types, [] { return create_python_script_engine(); });

    world.tick(0.1f);
    REQUIRE(world.get_component<Transform>(e)->position.x == 1.0f);

    // Edit the file + bump its mtime deterministically (independent of filesystem resolution).
    write_file(py, mover_script(2.0f));
    fs::last_write_time(py, fs::file_time_type::clock::now() + std::chrono::seconds(2));

    world.tick(0.1f);
    REQUIRE(world.get_component<Transform>(e)->position.x == 2.0f); // reloaded
    fs::remove(py);
}

TEST_CASE("a missing .py file is non-fatal", "[script]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    world.add_component<ScriptComponent>(e, ScriptComponent{"", "/no/such/mosaic_script_xyz.py"});
    world.add_system<ScriptSystem>(types, [] { return create_python_script_engine(); });

    world.tick(0.1f); // must not throw
    REQUIRE(world.get_component<Transform>(e)->position.x == 0.0f); // unchanged
}
