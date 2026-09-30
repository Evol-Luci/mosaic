#include <catch2/catch_test_macros.hpp>

#include "engine/script/script_host.hpp"
#include "modules/script_python/python_script_engine.hpp"

using mosaic::engine::script::ScriptHost;
using mosaic::foundation::math::Vec3;
using mosaic::script::python::create_python_script_engine;

TEST_CASE("bind_host exposes apply_impulse and marshals its arguments", "[script]")
{
    std::uint64_t seen_entity = 0;
    Vec3          seen_impulse{}, seen_point{};

    ScriptHost host;
    host.apply_impulse = [&](std::uint64_t e, Vec3 impulse, Vec3 point) -> bool
    {
        seen_entity = e; seen_impulse = impulse; seen_point = point;
        return true;
    };

    auto engine = create_python_script_engine();
    engine->bind_host(host);

    auto r = engine->run_string(
        "ok = apply_impulse(7, [2.0, 0.0, -1.0], [0.0, 0.25, 0.0])\n"
        "assert ok is True\n");
    REQUIRE(r.ok);

    REQUIRE(seen_entity == 7);
    REQUIRE(seen_impulse.x == 2.0f);
    REQUIRE(seen_impulse.z == -1.0f);
    REQUIRE(seen_point.y == 0.25f);
}

TEST_CASE("apply_impulse with no host callback returns False", "[script]")
{
    // ScriptHost's contract: an unset hook is a silent no-op.
    ScriptHost host; // apply_impulse deliberately unset
    auto engine = create_python_script_engine();
    engine->bind_host(host);

    auto r = engine->run_string(
        "ok = apply_impulse(1, [1.0, 0.0, 0.0], [0.0, 0.0, 0.0])\n"
        "assert ok is False\n");
    REQUIRE(r.ok);
}

TEST_CASE("apply_impulse returns False for a too-short sequence", "[script]")
{
    // Review Focus 1: the likeliest hand-written typo. It must not raise out of
    // pybind11, which would abort the whole tick rather than this one call.
    bool called = false;
    ScriptHost host;
    host.apply_impulse = [&](std::uint64_t, Vec3, Vec3) -> bool { called = true; return true; };

    auto engine = create_python_script_engine();
    engine->bind_host(host);

    auto r = engine->run_string(
        "ok = apply_impulse(1, [1.0, 0.0], [0.0, 0.0, 0.0])\n"
        "assert ok is False\n"
        "after = 42\n"); // the script keeps running past the failed call
    REQUIRE(r.ok);
    REQUIRE_FALSE(called); // never reached the host
}

TEST_CASE("apply_impulse returns False for a non-numeric element", "[script]")
{
    // Review Focus 2: well-formed length, bad contents. This fails in the cast
    // rather than the index, so it is a genuinely different path from the test above.
    bool called = false;
    ScriptHost host;
    host.apply_impulse = [&](std::uint64_t, Vec3, Vec3) -> bool { called = true; return true; };

    auto engine = create_python_script_engine();
    engine->bind_host(host);

    auto r = engine->run_string(
        "ok = apply_impulse(1, [1.0, 'nope', 0.0], [0.0, 0.0, 0.0])\n"
        "assert ok is False\n");
    REQUIRE(r.ok);
    REQUIRE_FALSE(called);
}

TEST_CASE("apply_impulse propagates a False result from the host", "[script]")
{
    // The host returning false (no body, no physics system) must reach the script
    // as False rather than being swallowed into True.
    ScriptHost host;
    host.apply_impulse = [](std::uint64_t, Vec3, Vec3) -> bool { return false; };

    auto engine = create_python_script_engine();
    engine->bind_host(host);

    auto r = engine->run_string(
        "ok = apply_impulse(1, [1.0, 0.0, 0.0], [0.0, 0.0, 0.0])\n"
        "assert ok is False\n");
    REQUIRE(r.ok);
}
