#include <catch2/catch_test_macros.hpp>

#include "app/run/demo_scene.hpp"
#include "engine/assets/asset_registry.hpp"
#include "engine/assets/asset_metadata.hpp"
#include "engine/script/script_source.hpp"
#include <memory>
#include "engine/reflection/builtin_components.hpp"
#include "engine/script/script_component.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "modules/script_python/python_script_engine.hpp"

using mosaic::app::setup_runtime;
using mosaic::engine::World;
using mosaic::engine::assets::AssetRegistry;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::script::ScriptComponent;
using mosaic::engine::transform::Transform;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::script::python::create_python_script_engine;

TEST_CASE("setup_runtime wires ScriptSystem so scripts tick in the runtime", "[app]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    world.add_component<ScriptComponent>(e, ScriptComponent{
        "def on_update(dt):\n"
        "    t = get_component(entity, 'Transform')\n"
        "    t['position']['x'] += dt\n"
        "    set_component(entity, 'Transform', t)\n"});

    AssetRegistry registry;
    setup_runtime(world, registry, types, [] { return create_python_script_engine(); });

    for (int i = 0; i < 4; ++i) { world.tick(0.25f); } // 0.25 * 4, binary-exact
    REQUIRE(world.get_component<Transform>(e)->position.x == 1.0f);
}

TEST_CASE("setup_runtime lets a scene reference a script by AssetID", "[app]")
{
    using mosaic::engine::assets::AssetMetadata;
    using mosaic::engine::script::ScriptSource;
    using mosaic::foundation::serialization::AssetID;

    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    AssetRegistry registry;

    const AssetID id = AssetID::from_string("00000000-0000-0000-0000-0000000000cc");
    AssetMetadata meta; meta.uuid = id; meta.type = "script"; meta.version = 1;
    registry.register_resource<ScriptSource>(meta, std::make_shared<ScriptSource>(ScriptSource{
        "def on_update(dt):\n"
        "    t = get_component(entity, 'Transform')\n"
        "    t['position']['x'] += dt\n"
        "    set_component(entity, 'Transform', t)\n"}));

    auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    ScriptComponent sc; sc.script = id;
    world.add_component<ScriptComponent>(e, sc);

    setup_runtime(world, registry, types, [] { return create_python_script_engine(); });

    for (int i = 0; i < 4; ++i) { world.tick(0.25f); }
    REQUIRE(world.get_component<Transform>(e)->position.x == 1.0f);
}
