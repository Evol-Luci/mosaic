#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/commands/command_stack.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/value.hpp"
#include "modules/script_python/python_script_engine.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::transform::Transform;
using mosaic::foundation::commands::CommandStack;
using mosaic::foundation::math::Vec3;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::foundation::serialization::Value;
using mosaic::script::python::create_python_script_engine;

TEST_CASE("a bound stack makes script set_component undoable", "[script]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    Transform t{}; t.position = Vec3{1, 2, 3};
    world.add_component<Transform>(e, t);

    CommandStack stack;
    auto engine = create_python_script_engine();
    engine->bind_world(world, types);
    engine->bind_commands(stack);
    engine->set_global("eid", Value(static_cast<std::uint64_t>(e.value())));

    auto r = engine->run_string(
        "t = get_component(eid, 'Transform')\n"
        "t['position']['x'] = 9.0\n"
        "assert set_component(eid, 'Transform', t)\n"
        "assert undo_depth() == 1\n");
    REQUIRE(r.ok);
    REQUIRE(world.get_component<Transform>(e)->position.x == 9.0f);

    r = engine->run_string("assert undo()\n");
    REQUIRE(r.ok);
    REQUIRE(world.get_component<Transform>(e)->position.x == 1.0f); // reverted

    r = engine->run_string("assert redo()\n");
    REQUIRE(r.ok);
    REQUIRE(world.get_component<Transform>(e)->position.x == 9.0f); // re-applied
}

TEST_CASE("a failed set through the stack is not recorded", "[script]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    CommandStack stack;
    auto engine = create_python_script_engine();
    engine->bind_world(world, types);
    engine->bind_commands(stack);
    engine->set_global("eid", Value(static_cast<std::uint64_t>(e.value())));

    const auto r = engine->run_string(
        "assert set_component(eid, 'Nope', {}) == False\n"
        "assert undo_depth() == 0\n");
    REQUIRE(r.ok);
}

TEST_CASE("without a bound stack, set_component still works and undo is a no-op", "[script]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    auto engine = create_python_script_engine();
    engine->bind_world(world, types);
    engine->set_global("eid", Value(static_cast<std::uint64_t>(e.value())));

    const auto r = engine->run_string(
        "t = get_component(eid, 'Transform')\n"
        "t['position']['y'] = 4.0\n"
        "assert set_component(eid, 'Transform', t)\n"  // S2 direct path
        "assert undo() == False\n");                    // no stack bound
    REQUIRE(r.ok);
    REQUIRE(world.get_component<Transform>(e)->position.y == 4.0f);
}
