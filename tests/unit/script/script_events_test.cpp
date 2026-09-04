#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/script/script_event.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "modules/script_python/python_script_engine.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::script::ScriptEvent;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::script::python::create_python_script_engine;

TEST_CASE("emit publishes a named ScriptEvent on the world bus", "[script]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;

    std::vector<ScriptEvent> got;
    world.events().subscribe<ScriptEvent>([&](const ScriptEvent& e) { got.push_back(e); });

    auto engine = create_python_script_engine();
    engine->bind_world(world, types);

    REQUIRE(engine->run_string("emit('hit', {'dmg': 5})").ok);
    REQUIRE(got.size() == 1);
    REQUIRE(got[0].name == "hit");
    REQUIRE_FALSE(got[0].payload.is_null()); // carried a payload

    REQUIRE(engine->run_string("emit('bare')").ok); // payload defaults to None
    REQUIRE(got.size() == 2);
    REQUIRE(got[1].name == "bare");
    REQUIRE(got[1].payload.is_null());
}
