#include <cstdint>
#include <string>

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
using mosaic::foundation::math::Vec3;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::foundation::serialization::Value;
using mosaic::script::python::create_python_script_engine;

TEST_CASE("a script mutates a component property and C++ observes it", "[script]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    Transform t{}; t.position = Vec3{1, 2, 3};
    world.add_component<Transform>(e, t);

    auto engine = create_python_script_engine();
    engine->bind_world(world, types);
    engine->set_global("eid", Value(static_cast<std::int64_t>(e.value())));

    const auto r = engine->run_string(
        "t = get_component(eid, 'Transform')\n"
        "t['position']['x'] = 9.0\n"
        "set_component(eid, 'Transform', t)\n");
    REQUIRE(r.ok);
    REQUIRE(world.get_component<Transform>(e)->position.x == 9.0f);
    REQUIRE(world.get_component<Transform>(e)->position.y == 2.0f); // untouched
}

TEST_CASE("get_component reads the current C++ values into the script", "[script]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    Transform t{}; t.position = Vec3{5, 0, 0};
    world.add_component<Transform>(e, t);

    auto engine = create_python_script_engine();
    engine->bind_world(world, types);
    engine->set_global("eid", Value(static_cast<std::int64_t>(e.value())));
    const auto r = engine->run_string("print(get_component(eid, 'Transform')['position']['x'])");
    REQUIRE(r.ok);
    REQUIRE(r.output.find("5.0") != std::string::npos);
}

TEST_CASE("script property errors return None/False, not a crash", "[script]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    auto engine = create_python_script_engine();
    engine->bind_world(world, types);
    engine->set_global("eid", Value(static_cast<std::int64_t>(e.value())));

    REQUIRE(engine->run_string("assert get_component(eid, 'Nope') is None").ok);      // unknown -> None
    REQUIRE(engine->run_string("assert set_component(eid, 'Nope', {}) == False").ok); // unknown -> False
}

TEST_CASE("set_global marshals scalars and lists into the script", "[script]")
{
    auto engine = create_python_script_engine();
    engine->set_global("n", Value(static_cast<std::int64_t>(42)));
    engine->set_global("s", Value(std::string("hi")));
    mosaic::foundation::serialization::Array arr;
    arr.push_back(Value(static_cast<std::int64_t>(1)));
    arr.push_back(Value(static_cast<std::int64_t>(2)));
    engine->set_global("xs", Value(std::move(arr)));
    const auto r = engine->run_string("print(n, s, xs[0] + xs[1])");
    REQUIRE(r.ok);
    REQUIRE(r.output.find("42 hi 3") != std::string::npos);
}
