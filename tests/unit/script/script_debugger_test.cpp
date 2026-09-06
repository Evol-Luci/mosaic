#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "engine/script/script_debugger.hpp"

using mosaic::engine::script::BreakpointHit;
using mosaic::engine::script::ScriptDebugger;
using mosaic::foundation::serialization::Value;

TEST_CASE("ScriptDebugger manages breakpoints and hits", "[script]")
{
    ScriptDebugger dbg;
    REQUIRE_FALSE(dbg.has_breakpoint(3));

    dbg.set_breakpoint(3);
    dbg.set_breakpoint(5);
    REQUIRE(dbg.has_breakpoint(3));
    REQUIRE(dbg.has_breakpoint(5));
    REQUIRE(dbg.breakpoints().size() == 2);

    dbg.clear_breakpoint(3);
    REQUIRE_FALSE(dbg.has_breakpoint(3));

    REQUIRE(dbg.hits().empty());
    BreakpointHit h;
    h.line = 5;
    h.locals.emplace_back("x", Value(static_cast<std::int64_t>(42)));
    dbg.record_hit(h);
    REQUIRE(dbg.hits().size() == 1);
    REQUIRE(dbg.hits()[0].line == 5);
    REQUIRE(dbg.hits()[0].locals[0].first == "x");

    dbg.clear_hits();
    REQUIRE(dbg.hits().empty());
    dbg.clear_breakpoints();
    REQUIRE(dbg.breakpoints().empty());
}
