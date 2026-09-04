#include <catch2/catch_test_macros.hpp>

#include "modules/script_python/python_script_engine.hpp"

using mosaic::script::python::create_python_script_engine;

// The embedded interpreter is configured (CMake bakes in the Python home) so it can
// locate its standard library — a pure-Python stdlib module must import and work.
// Before that fix this failed with ModuleNotFoundError (only built-in modules like
// `math` worked).
TEST_CASE("a script can import and use a pure-Python stdlib module", "[script]")
{
    auto engine = create_python_script_engine();

    auto r = engine->run_string(
        "import json\n"
        "print(json.dumps({'a': 1}))\n");

    REQUIRE(r.ok);                                  // import json succeeded
    REQUIRE(r.output.find("\"a\": 1") != std::string::npos); // and the module works
}
