#include <string>

#include <catch2/catch_test_macros.hpp>

#include "modules/script_python/python_script_engine.hpp"

using mosaic::script::python::create_python_script_engine;

TEST_CASE("a Python script runs and its stdout is captured", "[script]")
{
    auto engine = create_python_script_engine();
    const auto r = engine->run_string("print('hello from python')");
    REQUIRE(r.ok);
    REQUIRE(r.error.empty());
    REQUIRE(r.output.find("hello from python") != std::string::npos);
}
