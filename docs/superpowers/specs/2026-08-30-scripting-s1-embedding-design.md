# Scripting S1 — Python embedding + smoke test (design)

**Decomposition:** [scripting-decomposition](2026-08-30-scripting-decomposition.md) (slice S1).
**Builds on:** the module/neutral-backend pattern (physics/render), #35 app shell.
**Date:** 2026-08-30.

> **Note:** designed and built semi-autonomously at the user's request. The one foundational
> decision — CPython + pybind11 as the scripting runtime — is recorded here and in the
> decomposition, and lands in this slice's PR as the **veto point** before anything merges. A
> throwaway spike confirmed the toolchain: pybind11 v2.13.6 via FetchContent embeds the found
> CPython, runs `py::exec`, reads back a global, and captures a `SyntaxError` — all green.

## Summary

Embed **real CPython** behind a **neutral `ScriptEngine` interface**, so the engine can run a
Python script string and observe its output/errors without depending on Python. CPython +
**pybind11** live in `modules/script_python`, walled off exactly like Box3D/Vulkan. This slice is
the foundation the rest of the pillar (S2 reflection bindings, S3 world/commands, S4
`ScriptComponent`, S5 Studio console) builds on, and it lands the two build dependencies the
pillar necessarily adds.

## Scope

**In:**
- A neutral `ScriptEngine` interface (`engine/script/`) with `run_string` → a `ScriptResult`
  (ok / error text / captured stdout).
- `modules/script_python`: a pybind11/CPython implementation + a `create_python_script_engine()`
  factory (injected like `create_box3d_backend`).
- The CMake wiring: `find_package(Python3 Development)` + pybind11 via FetchContent; a
  `mosaic_script_python` static lib.
- Headless unit tests: a script runs and its `print` output is captured; a syntax/runtime error is
  captured (ok=false, non-empty error) rather than crashing.

**Deferred (later slices):** marshaling typed values / reflected components to Python (S2), world
& command access (S3), `ScriptComponent` (S4), the Studio console (S5). S1 observes execution via
captured **stdout** only — no value marshaling yet.

## Architecture

### 1. Neutral interface (`engine/script/script_engine.hpp`) — Python-free

```cpp
namespace mosaic::engine::script
{
// Result of running a script: ok=false carries a human-readable error; output is captured stdout.
struct ScriptResult
{
    bool        ok = false;
    std::string error;  // empty when ok
    std::string output; // captured sys.stdout
};

// A neutral embedded-scripting engine. The concrete backend (CPython/pybind11) lives in
// modules/script_python and never leaks its types above this interface — the engine/foundation
// tiers stay language-agnostic (design/scripting.md: "one reflected API, many languages").
class ScriptEngine
{
public:
    virtual ~ScriptEngine() = default;
    // Run a script in this engine's own global namespace, capturing stdout + errors. Never throws.
    virtual ScriptResult run_string(const std::string& code) = 0;
};
} // namespace mosaic::engine::script
```

`engine/script/` contains only this header (and later slices' neutral additions). No Python, no
pybind11 — the tier wall.

### 2. CPython/pybind11 backend (`modules/script_python/`)

- `python_script_engine.hpp` — declares `std::unique_ptr<mosaic::engine::script::ScriptEngine>
  create_python_script_engine();` (the injection seam; no pybind11 in this header).
- `python_script_engine.cpp` — the implementation:
  - **Interpreter lifetime: process-once, never finalized.** CPython allows one interpreter and
    is fragile across finalize/re-init, so a `std::once_flag` guards a single
    `py::initialize_interpreter()` (no `finalize`; the process reclaims it at exit). Every
    `ScriptEngine` instance shares that interpreter but holds its **own globals** (`py::dict` seeded
    from `__main__`'s builtins) for namespace isolation. (Verified pattern; the spike used a
    scoped interpreter, but process-once is the correct multi-instance/test-safe choice.)
  - `run_string(code)`:
    1. Redirect `sys.stdout`/`sys.stderr` to an `io.StringIO` for the duration of the call
       (save/restore around it), so `print` is captured into `ScriptResult::output`.
    2. `py::exec(code, globals_)` inside a `try`.
    3. On `py::error_already_set`: set `ok=false`, `error = e.what()` (the formatted traceback/type
       + message), and still return any captured output. On other `std::exception`: same, generic
       message. **Never propagates** — the C++ caller gets a `ScriptResult`, not an exception.
  - The GIL: pybind11's embedded interpreter holds the GIL on the calling thread; v0.1 scripting is
    single-threaded (runs on the main/tick thread), so no explicit GIL juggling. Documented as an
    assumption.

### 3. CMake

- Top-level `CMakeLists.txt`: `find_package(Python3 COMPONENTS Interpreter Development REQUIRED)`
  and a FetchContent block for **pybind11 v2.13.6** (pinned tag), `FetchContent_MakeAvailable`.
- `modules/script_python/CMakeLists.txt`: `add_library(mosaic_script_python STATIC
  python_script_engine.cpp)`, linking `mosaic_engine` (for the neutral interface header) PUBLIC and
  `pybind11::embed` PRIVATE (pybind11::embed brings the Python includes/libs). C++20.
- Tests link `mosaic_script_python`.

## Data flow

```
create_python_script_engine()  -> ensures the interpreter is initialized once; new namespace
engine->run_string("print(6*7)")
    -> redirect sys.stdout to StringIO
    -> py::exec(code, globals) [try/catch]
    -> ScriptResult{ ok=true, error="", output="42\n" }
engine->run_string("bad !!!")
    -> py::error_already_set caught
    -> ScriptResult{ ok=false, error="SyntaxError: invalid syntax ...", output="" }
```

## Error handling

- **Syntax / runtime errors** → captured into `ScriptResult{ ok=false, error }`; never thrown to
  C++, never crash the host.
- **Interpreter init failure** (e.g., a broken Python) → `create_python_script_engine` surfaces it
  as a failed engine (the factory logs + the first `run_string` returns ok=false); the app degrades
  gracefully rather than aborting.
- Captured stdout is returned even on error (a script may print before failing).

## Testability

Headless (`tests/unit/script/script_engine_test.cpp`, links `mosaic_script_python`):

- **Runs + captures stdout:** `run_string("print('hello')")` → `ok==true`, `output` contains
  `"hello"`.
- **Computes:** `run_string("print(6*7)")` → `output` contains `"42"` (proves real execution).
- **Namespace isolation:** two `run_string` calls on the same engine share state
  (`x = 5` then `print(x)` → `"5"`); a second engine does **not** see the first's `x` (separate
  globals).
- **Syntax error captured:** `run_string("this is not python !!")` → `ok==false`, `error` contains
  `"SyntaxError"`, no throw.
- **Runtime error captured:** `run_string("raise ValueError('boom')")` → `ok==false`, `error`
  contains `"ValueError"` and `"boom"`.

(Interpreter is process-once, so multiple test cases creating engines share it safely.)

## Ground rules honored

- **Tier contract:** `engine/script/` is Python-free (neutral interface only); CPython/pybind11 is
  confined to `modules/script_python`, injected via a factory — the same wall as
  `modules/physics_box3d`/`render_vulkan`. `foundation`/`engine` never include Python.
- **Dependencies via the project's standard mechanisms:** Python via `find_package`, pybind11 via
  FetchContent (pinned tag) — matching Vulkan/Threads (`find_package`) and Catch2/box3d/imgui
  (FetchContent). This is the one slice that adds them; it is the review/veto point.
- No engine/foundation API changes. Catch2 test names avoid a leading `--`.

## Acceptance

- [ ] `ScriptEngine` neutral interface exists in `engine/script/`, Python-free.
- [ ] `modules/script_python` embeds CPython via pybind11 and implements `run_string` with
      stdout + error capture; `create_python_script_engine()` is the injection seam.
- [ ] Build wires `find_package(Python3 Development)` + pybind11 (FetchContent); the full suite
      builds and links.
- [ ] Unit tests: script runs + stdout captured; compute proven; namespace isolation; syntax +
      runtime errors captured (no crash). Full suite green.

## Depends on / relates

- **Depends on:** the module/neutral-backend pattern, CMake FetchContent/find_package. **Blocks:**
  S2 (property bindings), S3 (world/commands), S4 (`ScriptComponent`), S5 (Studio console) — all
  run through this `ScriptEngine`.
- **Relates:** #18 reflection (S2 binds through it); the deferred automation/agent API targets the
  same runtime.
