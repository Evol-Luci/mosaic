#include <catch2/catch_test_macros.hpp>

#include "engine/script/script_worker.hpp"
#include "modules/script_python/python_script_engine.hpp"

using mosaic::engine::script::ScriptWorker;
using mosaic::foundation::serialization::Value;
using mosaic::script::python::create_python_script_engine;

TEST_CASE("ScriptWorker runs a job on its thread and returns the write batch", "[script]")
{
    ScriptWorker worker([] { return create_python_script_engine(); });

    ScriptWorker::Job job;
    job.dt = 0.5f;
    job.snapshot[1]["Score"] = Value(2.0);
    job.scripts.push_back(ScriptWorker::ScriptRef{
        1, "def on_update(dt):\n"
           "    v = get_component(entity, 'Score')\n"
           "    set_component(entity, 'Score', v + dt)\n"});

    worker.dispatch(std::move(job));
    const auto writes = worker.collect();

    REQUIRE(writes.size() == 1);
    REQUIRE(writes[0].entity == 1);
    REQUIRE(writes[0].comp == "Score");
    // value 2.0 + dt 0.5 = 2.5 — marshal back to double and check.
    REQUIRE(writes[0].value.as<double>() == 2.5);
}
