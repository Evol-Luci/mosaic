#include <catch2/catch_test_macros.hpp>

#include "engine/script/script_context.hpp"
#include "modules/script_python/python_script_engine.hpp"

using mosaic::engine::script::SnapshotContext;
using mosaic::foundation::serialization::Value;
using mosaic::script::python::create_python_script_engine;

TEST_CASE("bind_context routes get/set through the context, not World", "[script]")
{
    SnapshotContext::Snapshot snap;
    // A Transform-like value the script reads and writes back.
    snap[5]["Score"] = Value(1.0); // double

    SnapshotContext ctx(snap);
    auto engine = create_python_script_engine();
    engine->bind_context(ctx);
    engine->set_global("entity", Value(static_cast<std::uint64_t>(5)));

    auto r = engine->run_string(
        "v = get_component(entity, 'Score')\n"
        "set_component(entity, 'Score', v + 4.0)\n");
    REQUIRE(r.ok);
    REQUIRE(ctx.writes().size() == 1);
    REQUIRE(ctx.writes()[0].entity == 5);
    REQUIRE(ctx.writes()[0].comp == "Score");
}
