#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/value.hpp"
#include "modules/script_python/python_script_engine.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::foundation::serialization::Value;
using mosaic::script::python::create_python_script_engine;

TEST_CASE("a script creates entities (immediate) and queues destruction (deferred)", "[script]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    // Pre-create a victim in C++ so the test holds its id to check after the tick commit.
    const auto victim = world.create_entity();

    auto engine = create_python_script_engine();
    engine->bind_world(world, types);
    engine->set_global("victim", Value(static_cast<std::uint64_t>(victim.value())));

    const auto r = engine->run_string(
        "a = create_entity()\n"
        "b = create_entity()\n"
        "assert entity_count() == 3\n"                 // victim + a + b, creation is immediate
        "assert len(entities()) == 3\n"
        "assert is_valid(a)\n"
        "assert destroy_entity(victim) == True\n"      // queued (deferred to tick)
        "assert destroy_entity(999999) == False\n"     // invalid id
        "assert entity_count() == 3\n"                 // still 3: destruction not committed yet
        "assert is_valid(victim) == True\n");
    REQUIRE(r.ok);
    REQUIRE(world.entity_count() == 3);

    world.tick(0.0f); // commit the deferred destruction
    REQUIRE(world.entity_count() == 2);
    REQUIRE_FALSE(world.is_valid(victim));
}
