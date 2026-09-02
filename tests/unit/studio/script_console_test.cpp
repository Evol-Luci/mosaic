#include <memory>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/commands/command_stack.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "modules/script_python/python_script_engine.hpp"
#include "studio/script_console.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::transform::Transform;
using mosaic::foundation::commands::CommandStack;
using mosaic::foundation::math::Vec3;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::studio::ConsoleEntry;
using mosaic::studio::ScriptConsoleModel;
using mosaic::script::python::create_python_script_engine;

namespace
{
ScriptConsoleModel::EngineFactory py_factory()
{
    return [] { return create_python_script_engine(); };
}

bool history_has(const ScriptConsoleModel& m, ConsoleEntry::Kind kind, const std::string& needle)
{
    for (const auto& e : m.history())
    {
        if (e.kind == kind && e.text.find(needle) != std::string::npos) { return true; }
    }
    return false;
}
} // namespace

TEST_CASE("script console runs code and captures output", "[studio]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world; CommandStack commands;
    ScriptConsoleModel console(py_factory());

    console.run("print(6*7)", world, types, commands);
    REQUIRE(history_has(console, ConsoleEntry::Kind::Input, "print(6*7)"));
    REQUIRE(history_has(console, ConsoleEntry::Kind::Output, "42"));
}

TEST_CASE("script console mutates the live world", "[studio]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world; CommandStack commands;
    ScriptConsoleModel console(py_factory());

    console.run("e = create_entity()", world, types, commands);
    REQUIRE(world.entity_count() == 1);
}

TEST_CASE("script console is a persistent REPL across runs", "[studio]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world; CommandStack commands;
    ScriptConsoleModel console(py_factory());

    console.run("x = 5", world, types, commands);
    console.run("print(x)", world, types, commands);
    REQUIRE(history_has(console, ConsoleEntry::Kind::Output, "5"));
}

TEST_CASE("script console surfaces errors without throwing", "[studio]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world; CommandStack commands;
    ScriptConsoleModel console(py_factory());

    console.run("raise ValueError('boom')", world, types, commands);
    REQUIRE(history_has(console, ConsoleEntry::Kind::Error, "boom"));
    // still usable afterwards
    console.run("print(1+1)", world, types, commands);
    REQUIRE(history_has(console, ConsoleEntry::Kind::Output, "2"));
}

TEST_CASE("script console routes mutations through the bound command stack", "[studio]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world; CommandStack commands;
    auto e = world.create_entity();
    Transform t{}; t.position = Vec3{1, 2, 3};
    world.add_component<Transform>(e, t);

    ScriptConsoleModel console(py_factory());
    // pass the entity id in via a run that reads it from a global set by a prior run
    console.run(std::string("eid = ") + std::to_string(e.value()), world, types, commands);
    console.run("t = get_component(eid, 'Transform')\n"
                "t['position']['x'] = 9.0\n"
                "set_component(eid, 'Transform', t)", world, types, commands);
    REQUIRE(world.get_component<Transform>(e)->position.x == 9.0f);
    console.run("undo()", world, types, commands);
    REQUIRE(world.get_component<Transform>(e)->position.x == 1.0f); // reverted via the stack
}

TEST_CASE("script console with no factory reports unavailable, no crash", "[studio]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world; CommandStack commands;
    ScriptConsoleModel console; // empty factory

    console.run("print(1)", world, types, commands);
    REQUIRE(history_has(console, ConsoleEntry::Kind::Error, "not available"));
}
