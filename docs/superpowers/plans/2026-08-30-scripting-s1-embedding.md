# Scripting S1 — Python embedding Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Embed real CPython behind a neutral `ScriptEngine` interface — run a Python script string and capture its stdout + errors — with CPython/pybind11 walled off in `modules/script_python`.

**Architecture:** `engine/script/script_engine.hpp` is a Python-free neutral interface; `modules/script_python` implements it with pybind11 (process-once interpreter, isolated globals per engine, stdout redirected to an `io.StringIO`, errors caught as `py::error_already_set`). Deps: `find_package(Python3 Development)` + pybind11 via FetchContent.

**Tech Stack:** C++20, CMake (FetchContent + find_package), Catch2, CPython, pybind11 v2.13.6.

**Spec:** `docs/superpowers/specs/2026-08-30-scripting-s1-embedding-design.md`

## Global Constraints

- **Tier wall:** `engine/script/` contains only the neutral interface (no Python, no pybind11). CPython + pybind11 live ONLY in `modules/script_python`, injected via a `create_python_script_engine()` factory — mirroring `modules/physics_box3d` (`create_box3d_backend`).
- **Dependencies via project standards:** Python via `find_package(Python3 COMPONENTS Interpreter Development REQUIRED)`; pybind11 via FetchContent (pinned `v2.13.6`) — matching the existing Vulkan/box3d/imgui pattern.
- **Interpreter lifetime: process-once, never finalized** (`std::once_flag` → `py::initialize_interpreter()`; no finalize). Single-threaded (runs on the main thread that holds the GIL after init) — no explicit GIL juggling in v0.1.
- **`run_string` never throws** — all Python errors become `ScriptResult{ ok=false, error }`.
- **Namespaces:** neutral interface `mosaic::engine::script`; backend factory `mosaic::script::python`.
- **Build:** `cmake --preset debug` (reconfigure — new deps) then `cmake --build build/debug -j`. **Test:** `ctest --test-dir build/debug`. Catch2 test names must not start with `--`.

---

### Task 1: Neutral interface + pybind11 backend + build wiring (+ smoke test)

Lands the runtime and the two build dependencies. Ends with a working embedded interpreter proven by one smoke test.

**Files:**
- Create: `engine/script/script_engine.hpp`
- Create: `modules/script_python/python_script_engine.hpp`
- Create: `modules/script_python/python_script_engine.cpp`
- Create: `modules/script_python/CMakeLists.txt`
- Modify: `CMakeLists.txt` (top-level: Python + pybind11 deps; `add_subdirectory`)
- Modify: `tests/CMakeLists.txt` (add the test source + link `mosaic_script_python`)
- Test: `tests/unit/script/script_engine_test.cpp`

**Interfaces:**
- Produces:
  - `struct mosaic::engine::script::ScriptResult { bool ok; std::string error; std::string output; };`
  - `class mosaic::engine::script::ScriptEngine { virtual ScriptResult run_string(const std::string&) = 0; };`
  - `std::unique_ptr<...ScriptEngine> mosaic::script::python::create_python_script_engine();`

- [ ] **Step 1: Write the neutral interface `engine/script/script_engine.hpp`**

```cpp
#pragma once

#include <string>

namespace mosaic::engine::script
{
// Result of running a script: ok=false carries a human-readable error; output is captured stdout.
struct ScriptResult
{
    bool        ok = false;
    std::string error;  // empty when ok
    std::string output; // captured sys.stdout/stderr
};

// A neutral embedded-scripting engine. The concrete backend (CPython/pybind11) lives in
// modules/script_python and never leaks its types above this interface, so engine/foundation
// stay language-agnostic.
class ScriptEngine
{
public:
    virtual ~ScriptEngine() = default;
    // Run a script in this engine's own global namespace, capturing stdout + errors. Never throws.
    virtual ScriptResult run_string(const std::string& code) = 0;
};
} // namespace mosaic::engine::script
```

- [ ] **Step 2: Write the backend header `modules/script_python/python_script_engine.hpp`** (no pybind11 here — keeps the seam clean)

```cpp
#pragma once

#include <memory>

#include "engine/script/script_engine.hpp"

namespace mosaic::script::python
{
// Create a CPython-backed ScriptEngine. Initializes the embedded interpreter once per process
// (never finalized). Injected like create_box3d_backend.
std::unique_ptr<mosaic::engine::script::ScriptEngine> create_python_script_engine();
} // namespace mosaic::script::python
```

- [ ] **Step 3: Write the failing smoke test `tests/unit/script/script_engine_test.cpp`**

```cpp
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
```

- [ ] **Step 4: Wire the module CMake `modules/script_python/CMakeLists.txt`** (mirrors `modules/physics_box3d/CMakeLists.txt`)

```cmake
add_library(mosaic_script_python STATIC python_script_engine.cpp)
target_include_directories(mosaic_script_python PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(mosaic_script_python
  PUBLIC  mosaic_engine
  PRIVATE pybind11::embed mosaic_foundation)
target_compile_features(mosaic_script_python PUBLIC cxx_std_20)
```

- [ ] **Step 5: Add the deps + subdir to the top-level `CMakeLists.txt`**

After the Dear ImGui block (the `mosaic_imgui` target, ~line 80) and before `add_subdirectory(foundation)`, insert:
```cmake
# --- Python scripting (CPython embedded via pybind11) -------------------------
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
FetchContent_Declare(
  pybind11
  GIT_REPOSITORY https://github.com/pybind/pybind11.git
  GIT_TAG        v2.13.6
)
FetchContent_MakeAvailable(pybind11)
```
And add the module subdirectory next to the other modules (after `add_subdirectory(modules/physics_box3d)`):
```cmake
add_subdirectory(modules/script_python)
```

- [ ] **Step 6: Register the test + link the module in `tests/CMakeLists.txt`**

Add the test source to the `mosaic_unit_tests` source list (after the last `unit/...` entry, e.g. the physics ones):
```cmake
  unit/script/script_engine_test.cpp
```
Add `mosaic_script_python` to the `target_link_libraries(mosaic_unit_tests PRIVATE ...)` list (next to `mosaic_physics_box3d`).

- [ ] **Step 7: Write the backend implementation `modules/script_python/python_script_engine.cpp`**

```cpp
#include "modules/script_python/python_script_engine.hpp"

#include <exception>
#include <mutex>
#include <string>

#include <pybind11/embed.h>

namespace py = pybind11;

namespace mosaic::script::python
{
namespace
{
// CPython allows a single interpreter and is fragile across finalize/re-init, so initialize once
// per process and never finalize (the process reclaims it at exit). After init, the calling
// (main) thread holds the GIL; v0.1 scripting is single-threaded, so no explicit GIL handoff.
void ensure_interpreter()
{
    static std::once_flag flag;
    std::call_once(flag, [] { py::initialize_interpreter(); });
}

class PythonScriptEngine final : public mosaic::engine::script::ScriptEngine
{
public:
    PythonScriptEngine()
    {
        ensure_interpreter();
        globals_ = py::dict();
        // Give the namespace access to builtins (print, etc.).
        globals_["__builtins__"] = py::module_::import("builtins");
    }

    mosaic::engine::script::ScriptResult run_string(const std::string& code) override
    {
        mosaic::engine::script::ScriptResult result;
        try
        {
            py::module_ sys = py::module_::import("sys");
            py::module_ io = py::module_::import("io");
            py::object old_out = sys.attr("stdout");
            py::object old_err = sys.attr("stderr");
            py::object buffer = io.attr("StringIO")();
            sys.attr("stdout") = buffer;
            sys.attr("stderr") = buffer;

            try
            {
                py::exec(code, globals_);
                result.ok = true;
            }
            catch (py::error_already_set& e)
            {
                result.ok = false;
                result.error = e.what();
            }

            result.output = buffer.attr("getvalue")().cast<std::string>();
            sys.attr("stdout") = old_out; // restore even on error
            sys.attr("stderr") = old_err;
        }
        catch (const std::exception& e)
        {
            result.ok = false;
            if (result.error.empty())
            {
                result.error = e.what();
            }
        }
        return result;
    }

private:
    py::dict globals_; // this engine's isolated namespace (interpreter outlives it)
};
} // namespace

std::unique_ptr<mosaic::engine::script::ScriptEngine> create_python_script_engine()
{
    return std::make_unique<PythonScriptEngine>();
}
} // namespace mosaic::script::python
```

- [ ] **Step 8: Configure (new deps), build, run the smoke test**

```bash
cmake --preset debug 2>&1 | tail -5   # reconfigure: fetches pybind11, finds Python3
cmake --build build/debug -j 2>&1 | tail -5
ctest --test-dir build/debug -R "script runs and its stdout" --output-on-failure
```
Expected: configure fetches pybind11 + finds Python3; build links `libpython`; the smoke test PASSES (`ok`, output contains "hello from python"). If configure cannot find Python3 Development, that is the environment gap to report — the spike confirmed it resolves here.

- [ ] **Step 9: Full suite green**

```bash
ctest --test-dir build/debug 2>&1 | tail -3
```
Expected: 100% pass (the embedded interpreter initializes once; no other test is affected).

- [ ] **Step 10: Commit**

```bash
git add engine/script/script_engine.hpp modules/script_python/ CMakeLists.txt tests/CMakeLists.txt tests/unit/script/script_engine_test.cpp
git commit -m "feat(script): embed CPython behind a neutral ScriptEngine (pybind11)"
```

---

### Task 2: Full smoke-test coverage

Rounds out the acceptance tests on the working module (pure test additions).

**Files:**
- Modify: `tests/unit/script/script_engine_test.cpp`

**Interfaces:**
- Consumes: `create_python_script_engine()` (Task 1).

- [ ] **Step 1: Add the remaining cases to `tests/unit/script/script_engine_test.cpp`**

```cpp
TEST_CASE("a Python script really executes (computes 6*7)", "[script]")
{
    auto engine = create_python_script_engine();
    const auto r = engine->run_string("print(6 * 7)");
    REQUIRE(r.ok);
    REQUIRE(r.output.find("42") != std::string::npos);
}

TEST_CASE("state persists across runs on one engine, isolated between engines", "[script]")
{
    auto a = create_python_script_engine();
    REQUIRE(a->run_string("x = 5").ok);
    const auto seen = a->run_string("print(x)");
    REQUIRE(seen.ok);
    REQUIRE(seen.output.find("5") != std::string::npos);

    // A second engine has its own namespace: it does NOT see `x`.
    auto b = create_python_script_engine();
    const auto missing = b->run_string("print(x)");
    REQUIRE_FALSE(missing.ok);
    REQUIRE(missing.error.find("NameError") != std::string::npos);
}

TEST_CASE("a syntax error is captured, not thrown", "[script]")
{
    auto engine = create_python_script_engine();
    const auto r = engine->run_string("this is not valid python !!");
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.error.find("SyntaxError") != std::string::npos);
}

TEST_CASE("a runtime error is captured with its type and message", "[script]")
{
    auto engine = create_python_script_engine();
    const auto r = engine->run_string("raise ValueError('boom')");
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.error.find("ValueError") != std::string::npos);
    REQUIRE(r.error.find("boom") != std::string::npos);
}
```

- [ ] **Step 2: Build + run the script tests, then the full suite**

```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "\[script\]" --output-on-failure
ctest --test-dir build/debug 2>&1 | tail -3
```
Expected: all `[script]` cases PASS; full suite green.

- [ ] **Step 3: Commit**

```bash
git add tests/unit/script/script_engine_test.cpp
git commit -m "test(script): compute, namespace isolation, syntax + runtime error capture"
```

---

## Notes for the executor

- **This slice adds two build dependencies** (a Python dev install via `find_package`, pybind11 via FetchContent). That is intentional and is the pillar's veto point — call it out in the PR.
- **`docs/project/next-steps.md`** — after Task 2, note Scripting S1 complete + point to S2 (reflection→Python property bindings). Commit on this branch (rides in the S1 PR).
- **Finishing:** use `superpowers:finishing-a-development-branch`; base branch is `main`. The PR is where the user approves the CPython+pybind11 dependency before merge.
- If `find_package(Python3 ... Development)` fails in CI/another machine, the fix is installing Python dev headers + a shared `libpython` (e.g. the distro's `python3-dev`/`python3-devel`); document this in the PR.
```
