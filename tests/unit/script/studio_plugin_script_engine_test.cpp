#include <string>

#include <catch2/catch_test_macros.hpp>

#include "engine/script/script_engine.hpp"
#include "modules/script_python_studio/studio_plugin_script_engine.hpp"

using mosaic::engine::script::ScriptResult;
using mosaic::script::python_studio::create_studio_plugin_script_engine;

TEST_CASE("Studio plugin engine blocks os/subprocess/socket/ctypes", "[script]")
{
    auto engine = create_studio_plugin_script_engine();
    for (const char* mod : {"os", "subprocess", "socket", "ctypes", "sys"})
    {
        const ScriptResult r = engine->run_string(std::string("import ") + mod);
        REQUIRE_FALSE(r.ok);
    }
}

TEST_CASE("Studio plugin engine blocks open/eval/exec", "[script]")
{
    auto engine = create_studio_plugin_script_engine();
    REQUIRE_FALSE(engine->run_string("open('/etc/passwd')").ok);
    REQUIRE_FALSE(engine->run_string("eval('1+1')").ok);
    REQUIRE_FALSE(engine->run_string("exec('x=1')").ok);
}

TEST_CASE("Studio plugin engine still runs safe code and defines functions", "[script]")
{
    auto engine = create_studio_plugin_script_engine();
    ScriptResult define = engine->run_string("def on_click():\n    print('clicked')\n");
    REQUIRE(define.ok);
    ScriptResult call = engine->call_function("on_click");
    REQUIRE(call.ok);
    REQUIRE(call.output.find("clicked") != std::string::npos);
}
