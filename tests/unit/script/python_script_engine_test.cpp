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

TEST_CASE("install_restricted_builtins allows random/collections/enum/dataclasses/typing/uuid, but blocks their os/sys attribute leak", "[script]")
{
    // A real bug found during review: random, collections, enum,
    // dataclasses, typing, and uuid each do `import os`/`import sys`
    // themselves, so once imported they expose the real, unrestricted os/sys
    // module as a plain attribute (uuid.os, random._os, collections._sys,
    // enum.sys, dataclasses.sys, typing.sys). An earlier fix removed these
    // modules from the allowlist entirely; the actual fix (make_module_proxy,
    // see the next test) closes the whole class of leak instead, so these
    // modules were restored to the allowlist -- useful functionality that
    // was unnecessarily lost is now safe again.
    mosaic::script::python::ensure_interpreter();
    py::dict globals;
    globals["__builtins__"] = py::module_::import("builtins");
    mosaic::script::python::install_restricted_builtins(globals);

    REQUIRE_NOTHROW(py::exec("import uuid\nuuid.uuid4()", globals));
    REQUIRE_THROWS(py::exec("import uuid\nuuid.os", globals));
    REQUIRE_THROWS(py::exec("import random\nrandom._os", globals));
    REQUIRE_THROWS(py::exec("import collections\ncollections._sys", globals));
    REQUIRE_THROWS(py::exec("import enum\nenum.sys", globals));
    REQUIRE_THROWS(py::exec("import dataclasses\ndataclasses.sys", globals));
    REQUIRE_THROWS(py::exec("import typing\ntyping.sys", globals));
}

TEST_CASE("install_restricted_builtins closes the module-attribute-graph bug class at any depth", "[script]")
{
    // The actual fix for the deeper problem behind both prior findings: an
    // allowed module is wrapped so any attribute that resolves to another
    // module object is denied, not just the two specific paths already found
    // (json.codecs.sys, re.enum.sys). This closes one-hop, two-hop, and any
    // n-hop variant of the same bug class, since the FIRST hop into any
    // submodule is refused regardless of how deep a dangerous name sits
    // beyond it.
    mosaic::script::python::ensure_interpreter();
    py::dict globals;
    globals["__builtins__"] = py::module_::import("builtins");
    mosaic::script::python::install_restricted_builtins(globals);

    REQUIRE_THROWS(py::exec("import json\njson.codecs", globals));
    REQUIRE_THROWS(py::exec("import re\nre.enum", globals));
    // Legitimate, non-module attributes on the same modules must still work.
    REQUIRE_NOTHROW(py::exec("import json\njson.dumps({'a': 1})", globals));
    REQUIRE_NOTHROW(py::exec("import re\nre.compile('a+')", globals));
}

TEST_CASE("the module proxy itself does not leak the real module via its own storage", "[script]")
{
    // Two real bugs found during review of the proxy fix above, both fixed
    // by moving the wrapped module into actual C++ state instead of any
    // Python-visible attribute, dict, or closure (see the long comment on
    // ModuleProxy/make_module_proxy in restricted_builtins.cpp for the
    // full history): a first version stored `self._mod` as a plain
    // instance attribute (found via ordinary attribute lookup, since
    // __getattr__ only fires for attributes absent from the instance); a
    // second version moved the module into a module-level
    // WeakKeyDictionary referenced from the __getattr__ closure, but that
    // dict was still reachable via ordinary reflection independent of
    // storage scheme: type(proxy).__getattr__.__globals__['_module_store'].
    mosaic::script::python::ensure_interpreter();
    py::dict globals;
    globals["__builtins__"] = py::module_::import("builtins");
    mosaic::script::python::install_restricted_builtins(globals);

    REQUIRE_THROWS(py::exec("import json\njson._mod", globals));
    REQUIRE_THROWS(py::exec(
        "import json\nobject.__getattribute__(json, '_mod')", globals));
    // A pybind11-registered method has no __globals__ (that's a
    // CPython-Python-function-only attribute) -- confirm the attribute
    // itself is simply absent, not merely empty or unreadable some other
    // way.
    REQUIRE_THROWS(py::exec(
        "import json\ntype(json).__getattr__.__globals__", globals));
}

TEST_CASE("the module proxy blocks __dict__/__loader__/__spec__, which leak real submodules or code-execution capability",
         "[script]")
{
    // A third leak, found in the same review pass as the __globals__ one
    // above but distinct from it: __dict__ is not itself a module, so the
    // isinstance<py::module_> check let it through -- but it IS the
    // wrapped module's real namespace dict, and its values include every
    // submodule that module imported internally (json.__dict__['decoder']
    // reaches the real json.decoder module with plain subscripting, no
    // further attribute access needed). __loader__/__spec__ are real
    // importlib objects whose own methods (e.g. exec_module) can execute
    // arbitrary code -- a different kind of escape, not a module leak at
    // all. All three are denied by name now, before the isinstance<> check
    // even runs.
    mosaic::script::python::ensure_interpreter();
    py::dict globals;
    globals["__builtins__"] = py::module_::import("builtins");
    mosaic::script::python::install_restricted_builtins(globals);

    REQUIRE_THROWS(py::exec("import json\njson.__dict__", globals));
    REQUIRE_THROWS(py::exec("import json\njson.__loader__", globals));
    REQUIRE_THROWS(py::exec("import json\njson.__spec__", globals));
    // Legitimate use is still unaffected.
    REQUIRE_NOTHROW(py::exec("import json\njson.dumps({'a': 1})", globals));
}

TEST_CASE("functions returned from allowed modules don't leak the real module via __globals__/__code__",
         "[script]")
{
    // A fourth leak, found in the same review pass as the third: getattr()
    // only guards attribute access performed ON THE PROXY -- it never
    // touched the VALUES it legitimately returns. json.dumps is a real,
    // unwrapped CPython function (not a module, so it passed straight
    // through every prior check), and every plain Python function carries
    // __globals__ -- the REAL module's actual namespace dict, including
    // its real, unrestricted __builtins__ entry:
    //   json.dumps.__globals__['__builtins__']['eval'](...)
    // reached the genuine eval with none of __dict__/__loader__/__spec__/
    // __builtins__ involved at all. Closed by wrapping any callable value
    // returned from an allowed module (functions, bound methods) in
    // CallableProxy, which forwards __call__ but denies the same
    // reflection-attribute set on the callable itself.
    mosaic::script::python::ensure_interpreter();
    py::dict globals;
    globals["__builtins__"] = py::module_::import("builtins");
    mosaic::script::python::install_restricted_builtins(globals);

    REQUIRE_THROWS(py::exec("import json\njson.dumps.__globals__", globals));
    REQUIRE_THROWS(py::exec("import json\njson.dumps.__code__", globals));
    REQUIRE_THROWS(py::exec("import uuid\nuuid.uuid4.__globals__", globals));
    // Calling the wrapped function through the proxy must still work
    // exactly as before -- only attribute access on the function itself is
    // guarded, not invocation.
    py::exec("import json\nresult = json.dumps({'a': 1})", globals, globals);
    REQUIRE(globals["result"].cast<std::string>().find("\"a\": 1") != std::string::npos);
    REQUIRE_NOTHROW(py::exec("import uuid\nuuid.uuid4()", globals));
}

TEST_CASE("a callable's return value is guarded too, closing the factory-function shape of the same leak",
         "[script]")
{
    // A fifth leak, found in the same review pass as the fourth:
    // CallableProxy::call forwarded its return value completely
    // unguarded. functools.lru_cache(maxsize=1) is a real, allowed-module
    // function that returns a fresh, unwrapped closure -- that closure's
    // __globals__ reaches the real functools namespace exactly the way
    // json.dumps.__globals__ did before CallableProxy existed. Closed by
    // running call()'s return value through the same guard_returned_value
    // used for ordinary attribute access.
    //
    // NOT closed by this fix, and not attempted further (see the "honest,
    // deliberate stopping point" comment above kBlockedReflectionAttrs in
    // restricted_builtins.cpp): a function that returns a CLASS (e.g.
    // collections.namedtuple(...)) is unaffected, since a class has
    // neither __globals__ nor __code__ and so isn't detected as
    // needing a wrap. Proxying classes was investigated and rejected --
    // CPython's `except` semantics and operator dispatch make a correct
    // class/instance proxy impossible, not merely expensive.
    mosaic::script::python::ensure_interpreter();
    py::dict globals;
    globals["__builtins__"] = py::module_::import("builtins");
    mosaic::script::python::install_restricted_builtins(globals);

    REQUIRE_THROWS(py::exec(
        "import functools\n"
        "cached = functools.lru_cache(maxsize=1)\n"
        "cached.__globals__",
        globals));
    // The returned closure must still be usable as a decorator/callable.
    py::exec(
        "import functools\n"
        "@functools.lru_cache(maxsize=1)\n"
        "def add_one(x):\n"
        "    return x + 1\n"
        "result = add_one(41)\n",
        globals, globals);
    REQUIRE(globals["result"].cast<int>() == 42);
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

TEST_CASE("create_restricted_python_script_engine() blocks the built-in-function __self__ escape",
          "[script]")
{
    // Round 10: guard_returned_value() previously only wrapped a returned
    // callable in CallableProxy if it exposed __globals__ or __code__ --
    // true of ordinary Python functions, but not of C-implemented builtins
    // like math.sqrt, which carry neither attribute. That let a builtin
    // function sail through unwrapped, and its __self__ hands back the raw,
    // unproxied module object: math.sqrt.__self__.__loader__.load_module
    // ('sys').modules['os'].system is a clean escape to arbitrary command
    // execution. Fixed by wrapping anything callable (PyCallable_Check),
    // not just things that happen to expose __globals__/__code__.
    auto engine = mosaic::script::python::create_restricted_python_script_engine();
    REQUIRE_FALSE(engine->run_string(
                            "import math\n"
                            "m = math.sqrt.__self__\n"
                            "s = m.__loader__.load_module('sys')\n"
                            "osmod = s.modules['os']\n"
                            "osmod.getcwd()\n")
                      .ok);

    auto engine2 = mosaic::script::python::create_restricted_python_script_engine();
    REQUIRE_FALSE(
        engine2
            ->run_string(
                "import math\n"
                "g = math.sqrt.__self__.__loader__.load_module('sys').modules['os'].system\n")
            .ok);
}

