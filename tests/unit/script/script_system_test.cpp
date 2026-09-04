#include <string>

#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/script/script_component.hpp"
#include "engine/script/script_host.hpp"
#include "engine/script/script_system.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "modules/script_python/python_script_engine.hpp"

#include "engine/assets/asset_registry.hpp"
#include "engine/assets/asset_metadata.hpp"
#include "engine/script/script_source.hpp"

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

TEST_CASE("ScriptSystem binds the host so a script can call stop()", "[script]")
{
    using mosaic::engine::script::ScriptHost;
    mosaic::foundation::reflection::TypeRegistry reg;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(reg, types);

    bool stopped = false;
    ScriptHost host;
    host.request_stop = [&] { stopped = true; };

    mosaic::engine::World world;
    world.add_system<mosaic::engine::script::ScriptSystem>(
        types, [] { return mosaic::script::python::create_python_script_engine(); }, host);

    const auto e = world.create_entity();
    world.add_component<mosaic::engine::transform::Transform>(
        e, mosaic::engine::transform::Transform{});
    world.add_component<mosaic::engine::script::ScriptComponent>(
        e, mosaic::engine::script::ScriptComponent{"def on_update(dt):\n    stop()\n"});

    world.tick(0.016f);
    REQUIRE(stopped);
}

TEST_CASE("ScriptSystem resolves a script asset by AssetID and runs it", "[script]")
{
    using mosaic::engine::assets::AssetMetadata;
    using mosaic::engine::assets::AssetRegistry;
    using mosaic::engine::script::ScriptSource;
    using mosaic::foundation::serialization::AssetID;

    mosaic::foundation::reflection::TypeRegistry reg;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(reg, types);

    AssetRegistry registry;
    const AssetID id = AssetID::from_string("00000000-0000-0000-0000-0000000000bb");
    AssetMetadata meta; meta.uuid = id; meta.type = "script"; meta.version = 1;
    registry.register_resource<ScriptSource>(
        meta, std::make_shared<ScriptSource>(ScriptSource{
            "def on_update(dt):\n"
            "    t = get_component(entity, 'Transform')\n"
            "    t['position']['x'] += dt\n"
            "    set_component(entity, 'Transform', t)\n"}));

    mosaic::engine::World world;
    world.add_system<mosaic::engine::script::ScriptSystem>(
        types, [] { return mosaic::script::python::create_python_script_engine(); },
        mosaic::engine::script::ScriptHost{}, &registry);

    const auto e = world.create_entity();
    world.add_component<mosaic::engine::transform::Transform>(
        e, mosaic::engine::transform::Transform{});
    mosaic::engine::script::ScriptComponent sc; sc.script = id; // no inline/path
    world.add_component<mosaic::engine::script::ScriptComponent>(e, sc);

    world.tick(0.5f);
    REQUIRE(world.get_component<mosaic::engine::transform::Transform>(e)->position.x == 0.5f);
}

TEST_CASE("threaded ScriptSystem applies script writes one tick later", "[script]")
{
    mosaic::foundation::reflection::TypeRegistry reg;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(reg, types);

    mosaic::engine::World world;
    // threaded = true (4th/5th ctor arg per the implemented signature)
    world.add_system<mosaic::engine::script::ScriptSystem>(
        types, [] { return mosaic::script::python::create_python_script_engine(); },
        mosaic::engine::script::ScriptHost{}, nullptr, /*threaded=*/true);

    const auto e = world.create_entity();
    world.add_component<mosaic::engine::transform::Transform>(
        e, mosaic::engine::transform::Transform{});
    world.add_component<mosaic::engine::script::ScriptComponent>(
        e, mosaic::engine::script::ScriptComponent{
               "def on_update(dt):\n"
               "    t = get_component(entity, 'Transform')\n"
               "    t['position']['x'] += dt\n"
               "    set_component(entity, 'Transform', t)\n"});

    // One-tick pipeline: after N ticks, x == (N-1)*dt.
    for (int i = 0; i < 4; ++i) { world.tick(0.25f); }
    REQUIRE(world.get_component<mosaic::engine::transform::Transform>(e)->position.x == 0.75f);
}

TEST_CASE("scripts communicate via emit + on_event, one tick later", "[script]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    world.add_system<ScriptSystem>(types, [] { return create_python_script_engine(); });

    // Emitter: fires 'ping' with a payload every tick.
    const auto a = world.create_entity();
    world.add_component<ScriptComponent>(a, ScriptComponent{
        "def on_update(dt):\n"
        "    emit('ping', {'v': 1.0})\n"});

    // Receiver: on 'ping', writes payload['v'] into its own Transform.x.
    const auto b = world.create_entity();
    world.add_component<Transform>(b, Transform{});
    world.add_component<ScriptComponent>(b, ScriptComponent{
        "def on_update(dt):\n"
        "    pass\n"
        "def on_event(name, payload):\n"
        "    if name == 'ping':\n"
        "        t = get_component(entity, 'Transform')\n"
        "        t['position']['x'] = payload['v']\n"
        "        set_component(entity, 'Transform', t)\n"});

    world.tick(0.016f); // A emits 'ping' (deferred); not delivered this tick
    REQUIRE(world.get_component<Transform>(b)->position.x == 0.0f);

    world.tick(0.016f); // drain delivers 'ping' -> B.on_event
    REQUIRE(world.get_component<Transform>(b)->position.x == 1.0f);
}
