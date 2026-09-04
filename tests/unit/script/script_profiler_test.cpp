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

TEST_CASE("ScriptSystem profiles per-entity on_update calls", "[script]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto& sys =
        world.add_system<ScriptSystem>(types, [] { return create_python_script_engine(); });

    REQUIRE(sys.profile().empty()); // nothing has run yet

    const auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    world.add_component<ScriptComponent>(e, ScriptComponent{
        "def on_update(dt):\n"
        "    s = 0\n"
        "    for i in range(50):\n"
        "        s += i\n"});

    for (int i = 0; i < 3; ++i) { world.tick(0.016f); }

    const auto prof = sys.profile();
    REQUIRE(prof.size() == 1);
    REQUIRE(prof[0].entity == e.value());
    REQUIRE(prof[0].calls == 3);              // deterministic: one call per tick
    REQUIRE(prof[0].total_ms >= 0.0);
    REQUIRE(prof[0].max_ms >= 0.0);
    REQUIRE(prof[0].total_ms >= prof[0].max_ms); // total is the sum of the per-tick samples
}
