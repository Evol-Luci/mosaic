#include <cstdint>

#include <catch2/catch_test_macros.hpp>
#include <pybind11/embed.h>

#include "engine/reflection/builtin_components.hpp"
#include "engine/world/name.hpp"
#include "engine/world/world.hpp"
#include "foundation/commands/command_stack.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "modules/script_python/python_script_engine.hpp" // ensure_interpreter
#include "modules/script_python/world_globals.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::foundation::commands::CommandStack;
using mosaic::foundation::reflection::TypeRegistry;

namespace py = pybind11;

TEST_CASE("install_world_globals exposes create_entity/is_valid/entity_count", "[script]")
{
    mosaic::script::python::ensure_interpreter();

    World world;
    TypeRegistry reg;
    ComponentReflection types;
    register_builtin_components(reg, types);

    World* world_ptr = &world;
    const ComponentReflection* types_ptr = &types;
    CommandStack* stack_ptr = nullptr;

    py::dict globals;
    globals["__builtins__"] = py::module_::import("builtins");
    mosaic::script::python::install_world_globals(globals, &world_ptr, &types_ptr, &stack_ptr);

    py::exec("eid = create_entity()", globals);
    py::exec("ok = is_valid(eid)", globals);
    REQUIRE(globals["ok"].cast<bool>());
    py::exec("count = entity_count()", globals);
    REQUIRE(globals["count"].cast<std::uint64_t>() == 1);
}

TEST_CASE("install_world_globals: set_component without a stack applies directly", "[script]")
{
    mosaic::script::python::ensure_interpreter();

    World world;
    TypeRegistry reg;
    ComponentReflection types;
    register_builtin_components(reg, types);

    const auto e = world.create_entity();
    world.add_component<mosaic::engine::Name>(e, mosaic::engine::Name{"before"});

    World* world_ptr = &world;
    const ComponentReflection* types_ptr = &types;
    CommandStack* stack_ptr = nullptr;

    py::dict globals;
    globals["__builtins__"] = py::module_::import("builtins");
    mosaic::script::python::install_world_globals(globals, &world_ptr, &types_ptr, &stack_ptr);

    globals["eid"] = py::int_(e.value());
    py::exec("ok = set_component(eid, 'Name', {'value': 'after'})", globals);
    REQUIRE(globals["ok"].cast<bool>());
    REQUIRE(world.get_component<mosaic::engine::Name>(e)->value == "after");
}

TEST_CASE("install_world_globals: undo/redo/undo_depth read *stack_ptr at call time", "[script]")
{
    mosaic::script::python::ensure_interpreter();

    World world;
    TypeRegistry reg;
    ComponentReflection types;
    register_builtin_components(reg, types);
    CommandStack commands;

    const auto e = world.create_entity();
    world.add_component<mosaic::engine::Name>(e, mosaic::engine::Name{"before"});

    World* world_ptr = &world;
    const ComponentReflection* types_ptr = &types;
    CommandStack* stack_ptr = nullptr; // deliberately unbound at install time

    py::dict globals;
    globals["__builtins__"] = py::module_::import("builtins");
    mosaic::script::python::install_world_globals(globals, &world_ptr, &types_ptr, &stack_ptr);

    py::exec("d0 = undo_depth()", globals);
    REQUIRE(globals["d0"].cast<std::uint64_t>() == 0);

    // Bind the stack AFTER installing globals -- proves the lambdas read *stack_ptr at
    // call time, not install time.
    stack_ptr = &commands;

    globals["eid"] = py::int_(e.value());
    py::exec("set_component(eid, 'Name', {'value': 'after'})", globals);
    py::exec("d1 = undo_depth()", globals);
    REQUIRE(globals["d1"].cast<std::uint64_t>() == 1);

    py::exec("undone = undo()", globals);
    REQUIRE(globals["undone"].cast<bool>());
    REQUIRE(world.get_component<mosaic::engine::Name>(e)->value == "before");
}
