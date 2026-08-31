#include <string>

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

namespace
{
const char* const kMover =
    "def on_update(dt):\n"
    "    t = get_component(entity, 'Transform')\n"
    "    t['position']['x'] += dt\n"
    "    set_component(entity, 'Transform', t)\n";

float mover_after_four_quarter_ticks()
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    world.add_component<ScriptComponent>(e, ScriptComponent{kMover});
    world.add_system<ScriptSystem>(types, [] { return create_python_script_engine(); });
    for (int i = 0; i < 4; ++i) { world.tick(0.25f); }
    return world.get_component<Transform>(e)->position.x;
}
} // namespace

TEST_CASE("ScriptSystem runs on_update each tick, deterministically", "[script]")
{
    const float x1 = mover_after_four_quarter_ticks();
    REQUIRE(x1 == 1.0f);                 // 0.25 * 4, binary-exact
    const float x2 = mover_after_four_quarter_ticks();
    REQUIRE(x1 == x2);                   // bit-identical across runs
}

TEST_CASE("ScriptSystem caches the per-entity engine so script state persists", "[script]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    world.add_component<ScriptComponent>(e, ScriptComponent{
        "count = 0\n"
        "def on_update(dt):\n"
        "    global count\n"
        "    count += 1\n"
        "    t = get_component(entity, 'Transform')\n"
        "    t['position']['x'] = float(count)\n"
        "    set_component(entity, 'Transform', t)\n"});
    world.add_system<ScriptSystem>(types, [] { return create_python_script_engine(); });
    world.tick(0.0f); world.tick(0.0f); world.tick(0.0f);
    REQUIRE(world.get_component<Transform>(e)->position.x == 3.0f); // counter persisted 1,2,3
}

TEST_CASE("editing the ScriptComponent source recompiles the behavior", "[script]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    world.add_component<ScriptComponent>(e, ScriptComponent{
        "def on_update(dt):\n"
        "    t = get_component(entity, 'Transform')\n"
        "    t['position']['x'] = 1.0\n"
        "    set_component(entity, 'Transform', t)\n"});
    world.add_system<ScriptSystem>(types, [] { return create_python_script_engine(); });
    world.tick(0.0f);
    REQUIRE(world.get_component<Transform>(e)->position.x == 1.0f);

    world.get_component<ScriptComponent>(e)->source =
        "def on_update(dt):\n"
        "    t = get_component(entity, 'Transform')\n"
        "    t['position']['x'] = 2.0\n"
        "    set_component(entity, 'Transform', t)\n";
    world.tick(0.0f);
    REQUIRE(world.get_component<Transform>(e)->position.x == 2.0f); // recompiled
}

TEST_CASE("a throwing on_update is non-fatal; other scripted entities still update", "[script]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto bad = world.create_entity();
    world.add_component<Transform>(bad, Transform{});
    world.add_component<ScriptComponent>(bad, ScriptComponent{
        "def on_update(dt):\n    raise ValueError('boom')\n"});
    auto good = world.create_entity();
    world.add_component<Transform>(good, Transform{});
    world.add_component<ScriptComponent>(good, ScriptComponent{kMover});
    world.add_system<ScriptSystem>(types, [] { return create_python_script_engine(); });

    world.tick(0.5f); // must not throw
    REQUIRE(world.get_component<Transform>(good)->position.x == 0.5f);
}

TEST_CASE("a ScriptComponent with a syntax error is non-fatal", "[script]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    world.add_component<ScriptComponent>(e, ScriptComponent{"this is not valid python !!\n"});
    world.add_system<ScriptSystem>(types, [] { return create_python_script_engine(); });

    world.tick(0.25f); // compile fails; on_update undefined -> captured, non-fatal
    world.tick(0.25f); // must not crash on subsequent ticks either
    REQUIRE(world.get_component<Transform>(e)->position.x == 0.0f); // unchanged
}
