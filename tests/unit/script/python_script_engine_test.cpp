#include <catch2/catch_test_macros.hpp>

#include <pybind11/embed.h>

#include "modules/script_python/python_script_engine.hpp" // ensure_interpreter
#include "modules/script_python/restricted_builtins.hpp"

namespace py = pybind11;

TEST_CASE("install_restricted_builtins removes dangerous builtins", "[script]")
{
    mosaic::script::python::ensure_interpreter();
    py::dict globals;
    globals["__builtins__"] = py::module_::import("builtins");
    mosaic::script::python::install_restricted_builtins(globals);

    for (const char* name : {"open", "eval", "exec", "compile", "input", "breakpoint",
                             "exit", "quit", "help", "vars", "globals", "locals"})
    {
        REQUIRE_THROWS(py::exec(std::string(name) + "()", globals));
    }
}

TEST_CASE("install_restricted_builtins keeps safe builtins and class definitions working", "[script]")
{
    mosaic::script::python::ensure_interpreter();
    py::dict globals;
    globals["__builtins__"] = py::module_::import("builtins");
    mosaic::script::python::install_restricted_builtins(globals);

    REQUIRE_NOTHROW(py::exec("print('hi')", globals));
    REQUIRE_NOTHROW(py::exec(
        "try:\n    1/0\nexcept ZeroDivisionError:\n    pass\n", globals));
    REQUIRE_NOTHROW(py::exec("class Foo:\n    pass\nFoo()", globals));
}

TEST_CASE("install_restricted_builtins allows the module allowlist", "[script]")
{
    mosaic::script::python::ensure_interpreter();
    py::dict globals;
    globals["__builtins__"] = py::module_::import("builtins");
    mosaic::script::python::install_restricted_builtins(globals);

    REQUIRE_NOTHROW(py::exec("import json\njson.dumps({'a': 1})", globals));
    REQUIRE_NOTHROW(py::exec("import math\nmath.sqrt(4)", globals));
    REQUIRE_NOTHROW(py::exec("from csv import reader", globals));
}

TEST_CASE("install_restricted_builtins blocks everything not on the module allowlist", "[script]")
{
    mosaic::script::python::ensure_interpreter();
    py::dict globals;
    globals["__builtins__"] = py::module_::import("builtins");
    mosaic::script::python::install_restricted_builtins(globals);

    for (const char* mod : {"os", "subprocess", "socket", "ctypes", "sys",
                            "shutil", "pathlib", "importlib"})
    {
        REQUIRE_THROWS(py::exec(std::string("import ") + mod, globals));
    }
}

TEST_CASE("install_restricted_builtins excludes modules that leak os/sys as an attribute", "[script]")
{
    // A real bug found during final review: random, collections, enum,
    // dataclasses, typing, and uuid each do `import os`/`import sys`
    // themselves, so once imported they expose the real, unrestricted os/sys
    // module as a plain attribute (uuid.os, random._os, collections._sys,
    // enum.sys, dataclasses.sys, typing.sys) -- reachable via attribute
    // access, which the __import__ restriction below can't intercept at all.
    // These are excluded from the allowlist entirely rather than trusted.
    mosaic::script::python::ensure_interpreter();
    py::dict globals;
    globals["__builtins__"] = py::module_::import("builtins");
    mosaic::script::python::install_restricted_builtins(globals);

    for (const char* mod : {"random", "collections", "enum", "dataclasses", "typing", "uuid"})
    {
        REQUIRE_THROWS(py::exec(std::string("import ") + mod, globals));
    }
}

TEST_CASE("install_restricted_builtins blocks open by default but allows it with allow_open", "[script]")
{
    mosaic::script::python::ensure_interpreter();

    py::dict blocked_globals;
    blocked_globals["__builtins__"] = py::module_::import("builtins");
    mosaic::script::python::install_restricted_builtins(blocked_globals);
    REQUIRE_THROWS(py::exec("open", blocked_globals));

    py::dict allowed_globals;
    allowed_globals["__builtins__"] = py::module_::import("builtins");
    mosaic::script::python::install_restricted_builtins(allowed_globals, /*allow_open=*/true);
    REQUIRE_NOTHROW(py::exec("open", allowed_globals)); // just references the name, doesn't call it
}

TEST_CASE("install_restricted_builtins does not mutate the real builtins module", "[script]")
{
    mosaic::script::python::ensure_interpreter();
    py::dict globals;
    globals["__builtins__"] = py::module_::import("builtins");
    mosaic::script::python::install_restricted_builtins(globals);

    // A fresh, unrestricted namespace must still have full access -- proves the
    // restriction copied builtins rather than mutating the shared module in place.
    py::dict unrestricted_globals;
    unrestricted_globals["__builtins__"] = py::module_::import("builtins");
    REQUIRE_NOTHROW(py::exec("open", unrestricted_globals));
    REQUIRE_NOTHROW(py::exec("import os", unrestricted_globals));
}

#include "engine/script/script_engine.hpp"

using mosaic::engine::script::ScriptResult;

TEST_CASE("create_python_script_engine() stays fully unrestricted", "[script]")
{
    auto engine = mosaic::script::python::create_python_script_engine();
    const ScriptResult r = engine->run_string("import os\nx = 1");
    REQUIRE(r.ok);
}

TEST_CASE("create_restricted_python_script_engine() blocks os/subprocess/socket/ctypes", "[script]")
{
    auto engine = mosaic::script::python::create_restricted_python_script_engine();
    for (const char* mod : {"os", "subprocess", "socket", "ctypes", "sys"})
    {
        const ScriptResult r = engine->run_string(std::string("import ") + mod);
        REQUIRE_FALSE(r.ok);
    }
}

TEST_CASE("create_restricted_python_script_engine() blocks eval/exec", "[script]")
{
    auto engine = mosaic::script::python::create_restricted_python_script_engine();
    REQUIRE_FALSE(engine->run_string("eval('1+1')").ok);
    REQUIRE_FALSE(engine->run_string("exec('x=1')").ok);
}

TEST_CASE("create_restricted_python_script_engine() allows open (asset importers read their source file)", "[script]")
{
    auto engine = mosaic::script::python::create_restricted_python_script_engine();
    const ScriptResult r = engine->run_string(
        "with open('/dev/null') as f:\n    f.read()\n");
    REQUIRE(r.ok);
}

TEST_CASE("create_restricted_python_script_engine() still runs safe code", "[script]")
{
    auto engine = mosaic::script::python::create_restricted_python_script_engine();
    ScriptResult r = engine->run_string("import json\nprint(json.dumps({'a': 1}))");
    REQUIRE(r.ok);
    REQUIRE(r.output.find("\"a\": 1") != std::string::npos);
}

TEST_CASE("create_restricted_python_script_engine() blocks the 'import builtins' bypass", "[script]")
{
    // A real gap found during implementation: CPython's own internals call
    // __import__('builtins') during some stdlib operations (observed via
    // json.dumps()), so "builtins" can't simply be treated as just another
    // disallowed module name -- it must resolve to *something*. But if that
    // something were the real, unrestricted builtins module, a plugin could
    // trivially defeat every other restriction via
    // `import builtins; builtins.eval(...)`. Confirm neither attribute nor
    // subscript access on whatever "import builtins" resolves to exposes eval.
    auto engine = mosaic::script::python::create_restricted_python_script_engine();
    REQUIRE_FALSE(engine->run_string("import builtins\nbuiltins.eval('1+1')").ok);
    REQUIRE_FALSE(engine->run_string("import builtins\nbuiltins['eval']('1+1')").ok);
}

