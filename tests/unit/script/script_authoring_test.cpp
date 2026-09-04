#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/value.hpp"
#include "modules/script_python/python_script_engine.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::transform::Transform;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::foundation::serialization::Value;
using mosaic::script::python::create_python_script_engine;

TEST_CASE("scripts can add / query / remove components", "[script]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    const auto e = world.create_entity();

    auto engine = create_python_script_engine();
    engine->bind_world(world, types);
    engine->set_global("e", Value(static_cast<std::uint64_t>(e.value())));

    // Add + query. Every line is an assertion; a failure makes run_string return ok=false.
    auto r = engine->run_string(
        "assert not has_component(e, 'Transform')\n"
        "assert add_component(e, 'Transform')\n"            // added
        "assert not add_component(e, 'Transform')\n"        // already present -> False
        "assert has_component(e, 'Transform')\n"
        "assert 'Transform' in component_names(e)\n"
        "assert e in entities_with('Transform')\n"
        "assert entities_with('RigidBody') == []\n");       // nothing has it yet
    REQUIRE(r.ok);
    REQUIRE(world.has_component<Transform>(e)); // C++ sees the script-added component

    // The script can now furnish the component it added.
    auto r2 = engine->run_string(
        "t = get_component(e, 'Transform')\n"
        "t['position']['x'] = 5.0\n"
        "assert set_component(e, 'Transform', t)\n");
    REQUIRE(r2.ok);
    REQUIRE(world.get_component<Transform>(e)->position.x == 5.0f);

    // Remove.
    auto r3 = engine->run_string(
        "assert remove_component(e, 'Transform')\n"
        "assert not remove_component(e, 'Transform')\n"     // already gone -> False
        "assert not has_component(e, 'Transform')\n");
    REQUIRE(r3.ok);
    REQUIRE_FALSE(world.has_component<Transform>(e));
}
