# Plugin Runtime Sandboxing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restrict what Studio plugins and asset importers can do at
runtime (block `os`/`subprocess`/`socket`/`ctypes`/`eval`/`exec`/
`compile`/`__import__` and everything but a small module allowlist,
plus `open` for Studio plugins specifically — asset importers keep it,
since reading their source file is their whole job), while leaving
gameplay `ScriptComponent` execution completely untouched.

**Architecture:** One new shared function,
`install_restricted_builtins(pybind11::dict&)`, replaces a copy of the
real builtins with the dangerous names removed and a re-import-checking
`__import__`. `PythonScriptEngine` gains a private constructor flag and a
new sibling factory (`create_restricted_python_script_engine`) used only
by `ImportPluginHost`'s production construction site;
`StudioPluginScriptEngine` is restricted unconditionally in place (every
one of its callers is already plugin code).

**Tech Stack:** C++20, pybind11 (embedded CPython), Catch2.

**Spec:** `docs/superpowers/specs/2026-09-15-plugin-runtime-sandboxing-design.md`

## Global Constraints

- `create_python_script_engine()`'s signature and behavior are **unchanged** —
  gameplay `ScriptComponent` scripts stay fully unrestricted. Never touch
  its ~25 existing call sites.
- This is defense-in-depth, not a hard security boundary — the spec's
  honest-limitation framing must be preserved verbatim in
  `docs/design/plugin-sandboxing.md` (Task 4), not softened.
- Blocklist for builtins (`eval`, `exec`, `compile`, `__import__`,
  `input`, `breakpoint`, `exit`, `quit`, `help`, `vars`, `globals`,
  `locals`, plus `open` unless `allow_open = true` — Studio plugins get
  `allow_open = false`, asset importers get `allow_open = true`, since
  reading their source file is their whole job), allowlist for modules
  (`json`, `csv`, `math`, `re`, `time`, `datetime`, `itertools`,
  `functools`, `string`, `copy`, `textwrap` — final review found and
  fixed a real bypass where `random`/`collections`/`enum`/`dataclasses`/
  `typing`/`uuid` each leak the real `os`/`sys` module as a direct
  attribute, e.g. `uuid.os`; those six were removed from the allowlist
  entirely, see the spec's final section for the empirical audit) —
  exact values from the spec, copied verbatim into code, not re-derived.

---

### Task 1: `install_restricted_builtins`

**Files:**
- Create: `modules/script_python/restricted_builtins.hpp`
- Create: `modules/script_python/restricted_builtins.cpp`
- Modify: `modules/script_python/CMakeLists.txt`
- Create: `tests/unit/script/python_script_engine_test.cpp` (this task
  writes only the allowlist/blocklist-shape assertions that don't need a
  full `PythonScriptEngine`; Task 2 extends this same file with the
  factory-level tests)
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `mosaic::script::python::install_restricted_builtins(
  pybind11::dict& globals, bool allow_open = false)` — replaces
  `globals["__builtins__"]` in place. Task 2 (`PythonScriptEngine`, asset
  importers) calls this with `allow_open = true`; Task 3
  (`StudioPluginScriptEngine`) calls it with the default `allow_open =
  false`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/script/python_script_engine_test.cpp`:

```cpp
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
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --preset debug && cmake --build build/debug --target mosaic_unit_tests -- -j2 2>&1 | tail -40`

Expected: FAIL — `modules/script_python/restricted_builtins.hpp` doesn't
exist. Add the new test file to `tests/CMakeLists.txt`, directly after
`unit/script/script_debug_engine_test.cpp` (the last entry in that
`unit/script/*.cpp` block):

```
  unit/script/python_script_engine_test.cpp
```

Re-run. Expected: FAIL with a missing-header compile error.

- [ ] **Step 3: Write the minimal implementation**

Create `modules/script_python/restricted_builtins.hpp`:

```cpp
#pragma once

#include <pybind11/embed.h>

namespace mosaic::script::python
{
// Replaces globals["__builtins__"] with a restricted builtins namespace for
// less-trusted code (Studio plugins, asset importers). Defense-in-depth: this
// blocks the obvious attack surface (eval/exec/compile/__import__ of anything
// not on a small allowlist, plus open() unless allow_open is set) but does NOT
// claim to be a hard security boundary -- CPython has no supported way to fully
// sandbox arbitrary code running in-process (a determined attacker can still
// escape via object introspection, e.g.
// ().__class__.__bases__[0].__subclasses__()). Real isolation for genuinely
// untrusted code needs OS-level separation; see docs/design/plugin-sandboxing.md.
//
// allow_open exists because asset importers' entire purpose is reading their
// source file (the real, shipped tools/example_plugins/csv_importer.py does
// `with open(source_path) as f: ...`) -- blocking open() for them would break
// the feature, not secure it. Studio plugins have no such need, so their caller
// leaves allow_open at its default false.
void install_restricted_builtins(pybind11::dict& globals, bool allow_open = false);
} // namespace mosaic::script::python
```

Create `modules/script_python/restricted_builtins.cpp`:

```cpp
#include "modules/script_python/restricted_builtins.hpp"

#include <string>
#include <unordered_set>

namespace py = pybind11;

namespace mosaic::script::python
{
namespace
{
// Genuinely dangerous builtins: dynamic code execution, interactive/debugger
// hooks, and namespace introspection that could be chained toward an escape.
// "open" is deliberately NOT in this fixed list -- whether it's blocked depends
// on install_restricted_builtins's allow_open parameter (see its doc comment).
// A blocklist (not a hand-curated allowlist of the ~150 safe names) so a
// legitimate plugin never silently loses something it needs.
constexpr const char* kBlockedBuiltins[] = {
    "eval", "exec", "compile", "__import__", "input",
    "breakpoint", "exit", "quit", "help", "vars", "globals", "locals",
};

// A small, conservative set of stdlib modules with no meaningful attack surface
// on their own. Extend this list (see docs/design/plugin-sandboxing.md) if a real
// plugin needs something more -- don't work around it by widening the blocklist
// approach above instead.
const std::unordered_set<std::string>& allowed_modules()
{
    static const std::unordered_set<std::string> kAllowed = {
        "json",       "csv",        "math",     "re",     "random",
        "time",       "datetime",   "collections", "itertools", "functools",
        "string",     "copy",       "enum",     "dataclasses", "typing",
        "textwrap",   "uuid",
    };
    return kAllowed;
}
} // namespace

void install_restricted_builtins(py::dict& globals, bool allow_open)
{
    py::module_ builtins_mod = py::module_::import("builtins");
    // .copy() (Python's own dict method), never alias builtins.__dict__ directly --
    // popping names from an alias would mutate the real, process-wide builtins
    // module and break every other engine, including unrestricted gameplay scripts.
    py::dict restricted = builtins_mod.attr("__dict__").attr("copy")().cast<py::dict>();

    for (const char* name : kBlockedBuiltins)
    {
        restricted.attr("pop")(name, py::none());
    }
    if (!allow_open)
    {
        restricted.attr("pop")("open", py::none());
    }

    restricted["__import__"] = py::cpp_function(
        [](py::args args, py::kwargs kwargs) -> py::object
        {
            const std::string name = args.size() > 0 ? args[0].cast<std::string>() : std::string();
            const std::string top_level = name.substr(0, name.find('.'));
            if (allowed_modules().count(top_level) == 0)
            {
                throw py::import_error("import of '" + name + "' is not permitted here");
            }
            py::module_ builtins_mod = py::module_::import("builtins");
            py::object  real_import  = builtins_mod.attr("__import__");
            return real_import(*args, **kwargs);
        });

    globals["__builtins__"] = restricted;
}
} // namespace mosaic::script::python
```

- [ ] **Step 4: Wire `modules/script_python/CMakeLists.txt`**

Add `restricted_builtins.cpp` to the existing source list (find the
`add_library(mosaic_script_python ...)` call and add the new file
alongside `python_script_engine.cpp`/`world_globals.cpp`/`value_marshal.cpp`
— match whatever ordering those already use).

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build/debug --target mosaic_unit_tests -- -j2 && ./build/debug/tests/mosaic_unit_tests "[script]"`

Expected: PASS — all 5 new cases green.

- [ ] **Step 6: Commit**

```bash
git add modules/script_python/restricted_builtins.hpp modules/script_python/restricted_builtins.cpp modules/script_python/CMakeLists.txt tests/unit/script/python_script_engine_test.cpp tests/CMakeLists.txt
git commit -m "feat(script): add install_restricted_builtins"
```

---

### Task 2: `create_restricted_python_script_engine`

**Files:**
- Modify: `modules/script_python/python_script_engine.hpp`
- Modify: `modules/script_python/python_script_engine.cpp`
- Modify: `tests/unit/script/python_script_engine_test.cpp` (extends
  Task 1's file with factory-level tests)

**Interfaces:**
- Consumes: `mosaic::script::python::install_restricted_builtins` (Task 1).
- Produces: `mosaic::script::python::create_restricted_python_script_engine()
  -> std::unique_ptr<mosaic::engine::script::ScriptEngine>` — same
  zero-argument signature as the existing `create_python_script_engine()`,
  so it's a drop-in `EngineFactory` (`std::function<std::unique_ptr<
  ScriptEngine>()>`). Task 5 (`app/asset/asset_io.hpp`) consumes this
  verbatim.

- [ ] **Step 1: Write the failing tests**

Add to `tests/unit/script/python_script_engine_test.cpp` (after the 5
cases from Task 1):

```cpp
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
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build/debug --target mosaic_unit_tests -- -j2 2>&1 | tail -40`

Expected: FAIL — `create_restricted_python_script_engine` is not declared.

- [ ] **Step 3: Modify `modules/script_python/python_script_engine.hpp`**

Change:

```cpp
// Create a CPython-backed ScriptEngine. Initializes the embedded interpreter once per process
// (never finalized). Injected like create_box3d_backend.
std::unique_ptr<mosaic::engine::script::ScriptEngine> create_python_script_engine();
```

to:

```cpp
// Create a CPython-backed ScriptEngine. Initializes the embedded interpreter once per process
// (never finalized). Injected like create_box3d_backend.
std::unique_ptr<mosaic::engine::script::ScriptEngine> create_python_script_engine();

// Same as create_python_script_engine(), except the builtins namespace is restricted
// (see restricted_builtins.hpp) -- for less-trusted code (asset importer plugins),
// never for gameplay ScriptComponent scripts. Same zero-argument signature, so it's
// a drop-in EngineFactory wherever create_python_script_engine() is used today.
std::unique_ptr<mosaic::engine::script::ScriptEngine> create_restricted_python_script_engine();
```

- [ ] **Step 4: Modify `modules/script_python/python_script_engine.cpp`**

Add the include:

```cpp
#include "modules/script_python/restricted_builtins.hpp"
```

Change the `PythonScriptEngine` constructor from:

```cpp
    PythonScriptEngine()
    {
        // Interpreter is already running (InterpreterInit ran first).
        // Give the namespace access to builtins (print, etc.).
        globals_["__builtins__"] = py::module_::import("builtins");
    }
```

to:

```cpp
    explicit PythonScriptEngine(bool restricted = false)
    {
        // Interpreter is already running (InterpreterInit ran first).
        if (restricted)
        {
            // allow_open=true: this constructor path is only ever reached via
            // create_restricted_python_script_engine(), used exclusively for asset
            // importers, whose entire purpose is reading their own source file.
            install_restricted_builtins(globals_, /*allow_open=*/true);
        }
        else
        {
            // Give the namespace access to builtins (print, etc.).
            globals_["__builtins__"] = py::module_::import("builtins");
        }
    }
```

Change the file's bottom from:

```cpp
std::unique_ptr<mosaic::engine::script::ScriptEngine> create_python_script_engine()
{
    return std::make_unique<PythonScriptEngine>();
}
} // namespace mosaic::script::python
```

to:

```cpp
std::unique_ptr<mosaic::engine::script::ScriptEngine> create_python_script_engine()
{
    return std::make_unique<PythonScriptEngine>();
}

std::unique_ptr<mosaic::engine::script::ScriptEngine> create_restricted_python_script_engine()
{
    return std::make_unique<PythonScriptEngine>(/*restricted=*/true);
}
} // namespace mosaic::script::python
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build/debug --target mosaic_unit_tests -- -j2 && ./build/debug/tests/mosaic_unit_tests "[script]"`

Expected: PASS — the 4 new cases plus Task 1's 5 cases, all green. Also
run the full existing script suite to confirm
`create_python_script_engine()`'s ~25 existing call sites are unaffected:
`./build/debug/tests/mosaic_unit_tests` (full suite) should show no new
failures beyond the pre-existing, already-documented flaky
`threaded ScriptSystem applies script writes one tick later` SIGSEGV.

- [ ] **Step 6: Commit**

```bash
git add modules/script_python/python_script_engine.hpp modules/script_python/python_script_engine.cpp tests/unit/script/python_script_engine_test.cpp
git commit -m "feat(script): add create_restricted_python_script_engine"
```

---

### Task 3: Restrict `StudioPluginScriptEngine`

**Files:**
- Modify: `modules/script_python_studio/studio_plugin_script_engine.cpp`
- Create: `tests/unit/script/studio_plugin_script_engine_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `mosaic::script::python::install_restricted_builtins` (Task 1).
- Produces: nothing new — `create_studio_plugin_script_engine()`'s
  signature is unchanged; every existing caller gets the restriction for
  free.

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/script/studio_plugin_script_engine_test.cpp`:

```cpp
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
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build/debug --target mosaic_unit_tests -- -j2 2>&1 | tail -40`

Expected: FAIL to compile — the new test file isn't in
`tests/CMakeLists.txt`. Add it, directly after
`unit/script/python_script_engine_test.cpp` and
`unit/script/studio_plugin_script_engine_test.cpp` alphabetically fits
right after that entry:

```
  unit/script/studio_plugin_script_engine_test.cpp
```

Re-run. Expected: the two `os`/`open` cases FAIL (assert `ok` but get
`true`, since `StudioPluginScriptEngine` isn't restricted yet); the third
case already passes (existing behavior).

- [ ] **Step 3: Modify `modules/script_python_studio/studio_plugin_script_engine.cpp`**

Add the include:

```cpp
#include "modules/script_python/restricted_builtins.hpp"
```

Change the `StudioPluginScriptEngine` constructor from:

```cpp
    StudioPluginScriptEngine()
    {
        globals_["__builtins__"] = py::module_::import("builtins");
    }
```

to:

```cpp
    StudioPluginScriptEngine()
    {
        mosaic::script::python::install_restricted_builtins(globals_);
    }
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build/debug --target mosaic_unit_tests -- -j2 && ./build/debug/tests/mosaic_unit_tests "[script]"`

Expected: PASS — the 3 new cases green. Also run the full suite (no new
failures beyond the documented flaky test) and specifically
`./build/debug/tests/mosaic_unit_tests "[studio]"` to confirm
`tests/unit/studio/plugin_host_test.cpp` (which uses
`create_studio_plugin_script_engine` extensively) still passes — its
existing test plugins don't use anything on the new blocklist, per a
repo-wide grep during spec-writing that found no example plugin importing
anything beyond `csv`/`json`.

- [ ] **Step 5: Commit**

```bash
git add modules/script_python_studio/studio_plugin_script_engine.cpp tests/unit/script/studio_plugin_script_engine_test.cpp tests/CMakeLists.txt
git commit -m "feat(script): restrict StudioPluginScriptEngine's builtins"
```

---

### Task 4: Restrict `ImportPluginHost` + docs

**Files:**
- Modify: `app/asset/asset_io.hpp`
- Modify: `tests/unit/app/import_plugins_test.cpp`
- Create: `docs/design/plugin-sandboxing.md`
- Modify: `docs/project/next-steps.md`

**Interfaces:**
- Consumes: `mosaic::script::python::create_restricted_python_script_engine`
  (Task 2).
- Produces: nothing further downstream — this is the final task.

- [ ] **Step 1: Write the failing tests**

`tests/unit/app/import_plugins_test.cpp` already has two file-local
helpers used by every existing case: `fs::path make_plugin_dir(const
char* tag)` (a fresh, uniquely-named temp directory) and `void
write_file(const fs::path& path, const std::string& content)`. Add two
new cases using them, constructing `ImportPluginHost` with
`create_restricted_python_script_engine` (every existing case in this
file uses `create_python_script_engine` instead — those are unaffected by
this task and stay as-is):

```cpp
TEST_CASE("ImportPluginHost with the restricted factory rejects os but still allows reading its own source file",
         "[app]")
{
    const fs::path dir = make_plugin_dir("restricted_ok");
    write_file(dir / "csv_importer.py",
        "MOSAIC_IMPORTERS = {\".csv\": \"import_csv\"}\n\n"
        "def import_csv(source_path):\n"
        "    with open(source_path) as f:\n"
        "        return f.read().upper()\n");
    const fs::path source = make_plugin_dir("restricted_ok_src") / "data.csv";
    write_file(source, "a,b,c\n");

    ImportPluginHost host(create_restricted_python_script_engine);
    REQUIRE(host.load(dir.string()).empty());
    const auto* found = host.find_importer(".csv");
    REQUIRE(found != nullptr);
    const auto result = host.invoke(*found, source.string());
    REQUIRE(result.blob.has_value());
    REQUIRE(*result.blob == "A,B,C\n");
}

TEST_CASE("ImportPluginHost with the restricted factory rejects an os import", "[app]")
{
    const fs::path dir = make_plugin_dir("restricted_evil");
    write_file(dir / "evil.py",
        "MOSAIC_IMPORTERS = {\".evil\": \"do_evil\"}\n\n"
        "def do_evil(source_path):\n"
        "    import os\n"
        "    return os.listdir('.')\n");
    const fs::path source = make_plugin_dir("restricted_evil_src") / "data.evil";
    write_file(source, "x\n");

    ImportPluginHost host(create_restricted_python_script_engine);
    REQUIRE(host.load(dir.string()).empty()); // the file itself loads fine -- only the os usage fails
    const auto* found = host.find_importer(".evil");
    REQUIRE(found != nullptr);
    const auto result = host.invoke(*found, source.string());
    REQUIRE_FALSE(result.blob.has_value());
    REQUIRE_FALSE(result.error.empty());
}
```

Add the include and using-declaration these need, alongside the file's
existing `#include "modules/script_python/python_script_engine.hpp"` and
`using mosaic::script::python::create_python_script_engine;`:

```cpp
using mosaic::script::python::create_restricted_python_script_engine;
```

(`create_restricted_python_script_engine` is declared in the same
`modules/script_python/python_script_engine.hpp` header this file already
includes — no new `#include` needed.)

- [ ] **Step 2: Run the tests**

Run: `cmake --build build/debug --target mosaic_unit_tests -- -j2 && ./build/debug/tests/mosaic_unit_tests "[app]" 2>&1 | tail -20`

Expected: **PASS immediately, not a RED step.** Both new cases construct
`ImportPluginHost` directly with `create_restricted_python_script_engine`,
which Task 2 already fully implemented and tested at the bare
`ScriptEngine` level — there's no new engine behavior for this task to
make pass. What these two cases add is integration-level coverage that
the restriction behaves correctly when driven *through*
`ImportPluginHost` specifically (real plugin file discovery + `invoke`,
not just `run_string` directly), which Task 2's tests didn't exercise.
This task's actual new deliverable is Step 3 below (the production
`asset_io.hpp` call site) — that line isn't independently unit-testable,
so these two cases are the regression coverage that backs it.

- [ ] **Step 3: Modify `app/asset/asset_io.hpp`**

Change:

```cpp
        ImportPluginHost import_plugins(mosaic::script::python::create_python_script_engine);
```

to:

```cpp
        ImportPluginHost import_plugins(mosaic::script::python::create_restricted_python_script_engine);
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build/debug --target mosaic_unit_tests -- -j2 && ./build/debug/tests/mosaic_unit_tests "[app]"`

Expected: PASS — the new case plus every existing `[app]`-tagged test
still green (the other `ImportPluginHost` tests in this file construct it
directly with `create_python_script_engine`, testing importer *logic*
rather than the security boundary, and are unaffected by this change
since they don't touch `asset_io.hpp`'s production construction site).

- [ ] **Step 5: Write `docs/design/plugin-sandboxing.md`**

Create `docs/design/plugin-sandboxing.md`:

```markdown
# Plugin runtime sandboxing

Studio plugins (`MOSAIC_MENU_ITEMS`/`MOSAIC_PANELS`) and asset importers
(`MOSAIC_IMPORTERS`) run with a restricted set of Python builtins and a
restricted `__import__` — gameplay `ScriptComponent` scripts do not
(they're first-party code, same trust level as the rest of a project).

## An honest limitation

**This is a defense-in-depth deterrent, not a hard security boundary.**
CPython has no supported way to fully sandbox arbitrary code running
in-process: a determined attacker with knowledge of Python internals can
still escape via object introspection (e.g.
`().__class__.__bases__[0].__subclasses__()`-style chains reach loaded
classes without ever calling a blocked builtin or `import` statement).
Real isolation for genuinely untrusted third-party code needs OS-level
separation (a container, a restricted process, or similar) — see the
"dynamic sandboxed execution" layer named in `docs/design/marketplace.md`,
a distinct, harder, still-unscheduled piece of a future marketplace review
pipeline. This slice raises the bar for the common case — blocks
`import os`/`subprocess`/`socket`/`ctypes`, blocks `eval()`/`exec()`/
`compile()` outright, and blocks `open()` too for Studio plugins
specifically — without claiming more than that.

**A second, concrete instance of this limitation was found during
review, then actually closed (not just documented):** several stdlib
modules reach the real `os`/`sys` module via their own attribute
graph with zero further import statements — `uuid.os`, `json.codecs.sys`,
`re.enum.sys` all worked before the fix below. Rather than keep removing
individually-discovered leaky modules from the allowlist (two rounds
already failed to converge — each "clean" subset turned out to have a
leak one hop deeper), the real fix wraps every module the restricted
`__import__` hands back in a proxy (`make_module_proxy`) that denies any
attribute resolving to another module object, at any depth. This closes
the whole class of bug in one change: all six originally-removed modules
(`random`, `collections`, `enum`, `dataclasses`, `typing`, `uuid`) were
restored to the allowlist, since the specific leaky attribute they each
had is now blocked by the proxy regardless of which module carries it.
See `docs/design/plugin-sandboxing.md`'s extended treatment below.

## What's restricted, and where

`install_restricted_builtins()` (`modules/script_python/
restricted_builtins.hpp`) removes these builtins entirely: `eval`,
`exec`, `compile`, `__import__`, `input`, `breakpoint`, `exit`, `quit`,
`help`, `vars`, `globals`, `locals` — plus `open`, unless the caller
passes `allow_open = true`. It then reinstalls `__import__` as a wrapper
checking the requested module against an allowlist — `json`, `csv`,
`math`, `re`, `random`, `time`, `datetime`, `collections`, `itertools`,
`functools`, `string`, `copy`, `enum`, `dataclasses`, `typing`,
`textwrap`, `uuid` — raising `ImportError` for anything else, including
`sys` (deliberately excluded — powerful enough on its own, via
`sys.modules`/`sys._getframe`, to defeat the point). Every allowed
module is also wrapped in a proxy (`make_module_proxy`) before being
handed back to the plugin: any attribute access that would resolve to
another module object is denied, closing the "reach a dangerous module
via an allowed module's own attribute" bug class at any depth, not just
the specific paths already found.

This is applied to:
- **Studio plugins**, unconditionally, `open` blocked
  (`create_studio_plugin_script_engine()` — every caller is already
  plugin code, and a Studio plugin's job is UI/gameplay manipulation via
  `get_component`/`set_component`/`imgui_*`, never reading arbitrary
  files).
- **Asset importers**, via a new, separate factory
  (`create_restricted_python_script_engine()`, `allow_open = true`),
  used only by `ImportPluginHost`'s production construction site
  (`app/asset/asset_io.hpp`). `open` stays available here deliberately —
  an asset importer's entire purpose is reading its source file (the
  real, shipped `tools/example_plugins/csv_importer.py` does exactly
  `with open(source_path) as f: ...`); blocking it would break the
  feature, not secure it. This isn't a new gap: import plugins could
  already read arbitrary files before this slice (fully unrestricted);
  what this slice actually adds for that path is blocking
  `os`/`subprocess`/`socket`/`ctypes`/`eval`/`exec`.

**Not** applied to gameplay `ScriptComponent` scripts
(`create_python_script_engine()` — unchanged, still fully unrestricted).
Restricting first-party project code would block legitimate needs (a save
system wanting real file I/O, say) for no security benefit, since that
code was never less-trusted to begin with.

## Extending the module allowlist

If a real, legitimate plugin need shows up for a module not on the list,
add it to `kAllowed` in `restricted_builtins.cpp` deliberately (weigh what
that module can actually do) — don't work around the restriction by
widening the builtins blocklist logic instead; the module allowlist is
the intended, auditable extension point.

Full design: `docs/superpowers/specs/2026-09-15-plugin-runtime-sandboxing-design.md`.
```

- [ ] **Step 6: Add the `docs/project/next-steps.md` entry**

Prepend a new entry to the `**Last updated:**` line (following the
file's existing convention: each entry is `(...)` joined by ` ¶ ` to the
previous one), dated today, summarizing: Studio plugins and asset
importers now run with a restricted builtins/import set
(`install_restricted_builtins`, blocklist for builtins, allowlist for
modules); gameplay `ScriptComponent` scripts are explicitly untouched;
the honest defense-in-depth-not-a-hard-boundary framing; link the spec:
`docs/superpowers/specs/2026-09-15-plugin-runtime-sandboxing-design.md`.

- [ ] **Step 7: Commit**

```bash
git add app/asset/asset_io.hpp tests/unit/app/import_plugins_test.cpp docs/design/plugin-sandboxing.md docs/project/next-steps.md
git commit -m "feat(app): restrict ImportPluginHost's engine, document plugin sandboxing"
```
