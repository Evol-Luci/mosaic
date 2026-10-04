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

TEST_CASE("raycast() returns a miss for a malformed sequence instead of raising", "[script]")
{
    // raycast read origin[0..2]/direction[0..2] with no length or type check, so one bad
    // argument raised out of pybind11 and -- uncaught -- aborted the whole tick. apply_impulse
    // has guarded its reads since it shipped; this brings raycast to the same convention.
    //
    // Each case asserts TWO things: the call reports a miss, and the script keeps running
    // afterwards. The second is the point -- a binding that raises would fail the run even if
    // the dict it never returned would have said hit == False.
    int  calls = 0;
    ScriptHost host;
    host.raycast = [&](Vec3, Vec3, float) -> ScriptRayHit
    {
        ++calls;
        ScriptRayHit h;
        h.hit = true; // deliberately a HIT: a guarded call must not reach the host at all
        return h;
    };

    auto engine = create_python_script_engine();
    engine->bind_host(host);

    SECTION("origin too short")
    {
        auto r = engine->run_string("h = raycast([0.0, 5.0], [0.0, -1.0, 0.0], 10.0)['hit']\n"
                                    "survived = True\n"
                                    "assert h is False\n"
                                    "assert survived is True\n");
        REQUIRE(r.ok);
        REQUIRE(calls == 0); // the host was never called with a half-built Vec3
    }

    SECTION("direction too short")
    {
        auto r = engine->run_string("h = raycast([0.0, 5.0, 0.0], [0.0, -1.0], 10.0)['hit']\n"
                                    "survived = True\n"
                                    "assert h is False\n"
                                    "assert survived is True\n");
        REQUIRE(r.ok);
        REQUIRE(calls == 0);
    }

    SECTION("non-numeric element")
    {
        auto r = engine->run_string("h = raycast([0.0, 'up', 0.0], [0.0, -1.0, 0.0], 10.0)['hit']\n"
                                    "survived = True\n"
                                    "assert h is False\n"
                                    "assert survived is True\n");
        REQUIRE(r.ok);
        REQUIRE(calls == 0);
    }
}

TEST_CASE("raycast() returns a miss for a non-sequence argument instead of raising", "[script]")
{
    // #214 made raycast degrade for malformations WITHIN a sequence. A non-sequence never
    // reached that guard at all: with py::sequence in the binding signature, pybind11's
    // dispatcher rejected the call before the body ran, so the TypeError aborted the tick.
    //
    // A dict is the likeliest mistake here, because component values on this very surface
    // ARE dicts -- tr['position']['x'] -- so a script author reaching for {'x':..,'y':..}
    // is making an understandable error, not a perverse one.
    int        calls = 0;
    ScriptHost host;
    host.raycast = [&](Vec3, Vec3, float) -> ScriptRayHit
    {
        ++calls;
        ScriptRayHit h;
        h.hit = true; // a guarded call must not reach the host at all
        return h;
    };

    auto engine = create_python_script_engine();
    engine->bind_host(host);

    SECTION("a dict for origin")
    {
        auto r = engine->run_string("h = raycast({'x': 0.0, 'y': 5.0, 'z': 0.0}, [0,-1,0], 10.0)['hit']\n"
                                    "survived = True\n"
                                    "assert h is False\n"
                                    "assert survived is True\n");
        REQUIRE(r.ok);
        REQUIRE(calls == 0);
    }

    SECTION("an int for direction")
    {
        auto r = engine->run_string("h = raycast([0,5,0], 7, 10.0)['hit']\n"
                                    "survived = True\n"
                                    "assert h is False\n"
                                    "assert survived is True\n");
        REQUIRE(r.ok);
        REQUIRE(calls == 0);
    }

    SECTION("None for origin")
    {
        auto r = engine->run_string("h = raycast(None, [0,-1,0], 10.0)['hit']\n"
                                    "survived = True\n"
                                    "assert h is False\n"
                                    "assert survived is True\n");
        REQUIRE(r.ok);
        REQUIRE(calls == 0);
    }

    SECTION("a string is a sequence, and still reports a miss on its elements")
    {
        // PySequence_Check is true for str, so this passes the new sequence guard and is
        // caught one layer deeper by the element casts. Pinned so the two layers stay honest.
        auto r = engine->run_string("h = raycast('abc', [0,-1,0], 10.0)['hit']\n"
                                    "assert h is False\n");
        REQUIRE(r.ok);
        REQUIRE(calls == 0);
    }
}
