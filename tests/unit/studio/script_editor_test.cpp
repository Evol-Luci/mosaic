#include <string>

#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/script/script_component.hpp"
#include "engine/script/script_debugger.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "modules/script_python/python_script_engine.hpp"
#include "studio/script_editor.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::script::ScriptComponent;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::studio::ScriptApplyResult;
using mosaic::studio::ScriptEditorModel;
using mosaic::script::python::create_python_script_engine;

namespace
{
ScriptEditorModel::EngineFactory py_factory()
{
    return [] { return create_python_script_engine(); };
}
} // namespace

TEST_CASE("script editor applies a valid source and writes it into the component", "[studio]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    world.add_component<ScriptComponent>(e, ScriptComponent{"def on_update(dt):\n    pass\n"});

    ScriptEditorModel editor(py_factory());
    const std::string src = "def on_update(dt):\n    x = 1\n";
    const ScriptApplyResult r = editor.apply(src, world, types, e);
    REQUIRE(r.kind == ScriptApplyResult::Kind::Ok);
    REQUIRE(world.get_component<ScriptComponent>(e)->source == src);
}

TEST_CASE("script editor rejects a syntax error and leaves the component unchanged", "[studio]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    const std::string original = "def on_update(dt):\n    pass\n";
    world.add_component<ScriptComponent>(e, ScriptComponent{original});

    ScriptEditorModel editor(py_factory());
    const ScriptApplyResult r = editor.apply("def on_update(dt) not python !!", world, types, e);
    REQUIRE(r.kind == ScriptApplyResult::Kind::Error);
    REQUIRE(r.message.find("SyntaxError") != std::string::npos);
    REQUIRE(world.get_component<ScriptComponent>(e)->source == original); // unchanged
}

TEST_CASE("script editor errors on an entity without a ScriptComponent", "[studio]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity(); // no ScriptComponent

    ScriptEditorModel editor(py_factory());
    const ScriptApplyResult r = editor.apply("def on_update(dt):\n    pass\n", world, types, e);
    REQUIRE(r.kind == ScriptApplyResult::Kind::Error);
}

TEST_CASE("script editor with no factory reports unavailable", "[studio]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    world.add_component<ScriptComponent>(e, ScriptComponent{"def on_update(dt):\n    pass\n"});

    ScriptEditorModel editor; // empty factory
    const ScriptApplyResult r = editor.apply("def on_update(dt):\n    pass\n", world, types, e);
    REQUIRE(r.kind == ScriptApplyResult::Kind::Error);
    REQUIRE(r.message.find("not available") != std::string::npos);
}

TEST_CASE("script editor debug() records a breakpoint hit inside on_update", "[studio]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    world.add_component<ScriptComponent>(e, ScriptComponent{"def on_update(dt):\n    pass\n"});

    ScriptEditorModel editor(py_factory());
    mosaic::engine::script::ScriptDebugger dbg;
    dbg.set_breakpoint(2); // 'q = 7' inside on_update

    const std::string src =
        "def on_update(dt):\n" // line 1
        "    q = 7\n"          // line 2  <-- breakpoint
        "    r = q + 1\n";     // line 3
    const ScriptApplyResult res = editor.debug(src, world, types, e, dbg);
    REQUIRE(res.kind == ScriptApplyResult::Kind::Ok);

    bool found = false;
    for (const auto& h : dbg.hits()) { if (h.line == 2) { found = true; } }
    REQUIRE(found); // on_update was exercised under the debugger and the breakpoint fired
}
