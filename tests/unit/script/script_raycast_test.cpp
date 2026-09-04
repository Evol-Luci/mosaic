#include <catch2/catch_test_macros.hpp>

#include "engine/script/script_host.hpp"
#include "modules/script_python/python_script_engine.hpp"

using mosaic::engine::script::ScriptHost;
using mosaic::engine::script::ScriptRayHit;
using mosaic::foundation::math::Vec3;
using mosaic::script::python::create_python_script_engine;

TEST_CASE("bind_host exposes raycast() and marshals args + result", "[script]")
{
    Vec3  seen_origin{}, seen_dir{};
    float seen_max = 0.0f;

    ScriptHost host;
    host.raycast = [&](Vec3 o, Vec3 d, float max) -> ScriptRayHit
    {
        seen_origin = o; seen_dir = d; seen_max = max;
        ScriptRayHit h;
        h.hit = true; h.entity = 7; h.distance = 4.5f;
        h.point = Vec3{0.0f, 0.5f, 0.0f}; h.normal = Vec3{0.0f, 1.0f, 0.0f};
        return h;
    };

    auto engine = create_python_script_engine();
    engine->bind_host(host);

    auto r = engine->run_string(
        "r = raycast([0.0, 5.0, 0.0], [0.0, -1.0, 0.0], 10.0)\n"
        "assert r['hit'] is True\n"
        "assert r['entity'] == 7\n"
        "assert abs(r['distance'] - 4.5) < 1e-4\n"
        "assert abs(r['point']['y'] - 0.5) < 1e-4\n"
        "assert abs(r['normal']['y'] - 1.0) < 1e-4\n");
    REQUIRE(r.ok);

    // The C++ callback saw the marshaled args.
    REQUIRE(seen_origin.y == 5.0f);
    REQUIRE(seen_dir.y == -1.0f);
    REQUIRE(seen_max == 10.0f);
}

TEST_CASE("raycast() with no host callback returns a miss", "[script]")
{
    auto engine = create_python_script_engine();
    engine->bind_host(ScriptHost{}); // raycast unset
    auto r = engine->run_string("assert raycast([0,0,0], [0,0,-1], 1.0)['hit'] is False\n");
    REQUIRE(r.ok);
}
