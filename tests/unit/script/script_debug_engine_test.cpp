#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "engine/script/script_debugger.hpp"
#include "modules/script_python/python_script_engine.hpp"

using mosaic::engine::script::ScriptDebugger;
using mosaic::script::python::create_python_script_engine;

TEST_CASE("bind_debugger records a breakpoint hit with frame locals", "[script]")
{
    auto engine = create_python_script_engine();
    ScriptDebugger dbg;
    dbg.set_breakpoint(3); // the 'y = x + 1' line below
    engine->bind_debugger(&dbg);

    auto r = engine->run_string(
        "x = 10\n"    // line 1
        "z = 0\n"     // line 2
        "y = x + 1\n" // line 3  <-- breakpoint (x=10, z=0 already in locals)
        "z = y\n");   // line 4
    REQUIRE(r.ok);

    bool found = false;
    for (const auto& h : dbg.hits())
    {
        if (h.line != 3) { continue; }
        found = true;
        bool has_x = false;
        for (const auto& [k, v] : h.locals)
        {
            if (k == "x") { has_x = true; REQUIRE(v.as<std::int64_t>() == 10); }
        }
        REQUIRE(has_x);
    }
    REQUIRE(found);
}

TEST_CASE("no breakpoint yields no hits, and detaching stops tracing", "[script]")
{
    auto engine = create_python_script_engine();
    ScriptDebugger dbg; // no breakpoints set
    engine->bind_debugger(&dbg);
    REQUIRE(engine->run_string("a = 1\nb = 2\n").ok);
    REQUIRE(dbg.hits().empty());

    engine->bind_debugger(nullptr); // detach
    dbg.set_breakpoint(1);
    REQUIRE(engine->run_string("a = 1\n").ok);
    REQUIRE(dbg.hits().empty()); // detached: no trace installed
}
