# Plugin SDK — Custom Studio Panels Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A project-local `plugins/*.py` file can declare `MOSAIC_PANELS = {"My Panel": "draw_my_panel"}`; Studio adds "My Panel" to its dockable layout and calls `draw_my_panel()` once per frame while it's open, with real ImGui drawing power (text, buttons, inputs, checkboxes, scroll regions) available via nine new `imgui_*` globals.

**Architecture:** A new Studio-only module, `modules/script_python_studio`, provides a second `ScriptEngine` implementation whose globals are the existing gameplay set (extracted into a shared `install_world_globals` helper so it isn't duplicated) plus the new `imgui_*` set. `PluginHost` (already built in slice 1) switches to this engine for every plugin it loads and gains `MOSAIC_PANELS` parsing symmetric with `MOSAIC_MENU_ITEMS`. `StudioLayout` gains one new method to register a plugin panel into the existing dockable layout; `studio_app.cpp`'s existing panel-dispatch loop gains one new branch to call a plugin's draw function inside the panel's already-open `ImGui::Begin`/`End` block.

**Tech Stack:** C++20, the existing embedded CPython layer (pybind11), Dear ImGui (already vendored as `mosaic_imgui`).

**Spec:** `docs/superpowers/specs/2026-09-14-plugin-sdk-custom-panels-design.md`

## Global Constraints

- `studio/` must never depend on pybind11/Python.h directly (unchanged from slice 1) — the new module is the one place pybind11 *and* ImGui coexist, the same pattern `modules/render_vulkan` uses for Vulkan headers.
- `modules/script_python` (the gameplay-shared module) must never depend on ImGui — it stays usable outside the editor (`mosaic run`, headless tests, gameplay `ScriptComponent`s).
- `ScriptConsoleModel`/`ScriptEditorModel` (the Script Console/Editor) keep using the plain gameplay engine unchanged — the richer, ImGui-aware engine is only ever handed to `PluginHost`, the one place `imgui_*` calls are safe (inside an active panel draw).
- No automated test may call any `imgui_*` global — ImGui needs an active frame/context this suite doesn't have, consistent with every other ImGui-glue code in this project never being unit-tested.
- A malformed `MOSAIC_PANELS` (not a table, or a table with a non-string value) is a load error for the whole plugin file — same all-or-nothing semantics `MOSAIC_MENU_ITEMS` already has.

---

### Task 1: Extract `install_world_globals`; export `ensure_interpreter`

**Files:**
- Create: `modules/script_python/world_globals.hpp`
- Create: `modules/script_python/world_globals.cpp`
- Modify: `modules/script_python/python_script_engine.hpp`
- Modify: `modules/script_python/python_script_engine.cpp`
- Modify: `modules/script_python/CMakeLists.txt`
- Test: `tests/unit/script/world_globals_test.cpp` (new)
- Test: `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  namespace mosaic::script::python {
  void ensure_interpreter(); // now declared in python_script_engine.hpp, was file-local
  void install_world_globals(pybind11::dict& globals,
                             mosaic::engine::World** world_ptr,
                             const mosaic::engine::reflection::ComponentReflection** types_ptr,
                             mosaic::foundation::commands::CommandStack** stack_ptr);
  }
  ```
  Task 2 calls both of these from the new module.

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/script/world_globals_test.cpp`:

```cpp
#include <cstdint>

#include <catch2/catch_test_macros.hpp>
#include <pybind11/embed.h>

#include "engine/reflection/builtin_components.hpp"
#include "engine/world/name.hpp"
#include "engine/world/world.hpp"
#include "foundation/commands/command_stack.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "modules/script_python/python_script_engine.hpp" // ensure_interpreter
#include "modules/script_python/world_globals.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::foundation::commands::CommandStack;
using mosaic::foundation::reflection::TypeRegistry;

namespace py = pybind11;

TEST_CASE("install_world_globals exposes create_entity/is_valid/entity_count", "[script]")
{
    mosaic::script::python::ensure_interpreter();

    World world;
    TypeRegistry reg;
    ComponentReflection types;
    register_builtin_components(reg, types);

    World* world_ptr = &world;
    const ComponentReflection* types_ptr = &types;
    CommandStack* stack_ptr = nullptr;

    py::dict globals;
    globals["__builtins__"] = py::module_::import("builtins");
    mosaic::script::python::install_world_globals(globals, &world_ptr, &types_ptr, &stack_ptr);

    py::exec("eid = create_entity()", globals);
    py::exec("ok = is_valid(eid)", globals);
    REQUIRE(globals["ok"].cast<bool>());
    py::exec("count = entity_count()", globals);
    REQUIRE(globals["count"].cast<std::uint64_t>() == 1);
}

TEST_CASE("install_world_globals: set_component without a stack applies directly", "[script]")
{
    mosaic::script::python::ensure_interpreter();

    World world;
    TypeRegistry reg;
    ComponentReflection types;
    register_builtin_components(reg, types);

    const auto e = world.create_entity();
    world.add_component<mosaic::engine::Name>(e, mosaic::engine::Name{"before"});

    World* world_ptr = &world;
    const ComponentReflection* types_ptr = &types;
    CommandStack* stack_ptr = nullptr;

    py::dict globals;
    globals["__builtins__"] = py::module_::import("builtins");
    mosaic::script::python::install_world_globals(globals, &world_ptr, &types_ptr, &stack_ptr);

    globals["eid"] = py::int_(e.value());
    py::exec("ok = set_component(eid, 'Name', {'value': 'after'})", globals);
    REQUIRE(globals["ok"].cast<bool>());
    REQUIRE(world.get_component<mosaic::engine::Name>(e)->value == "after");
}

TEST_CASE("install_world_globals: undo/redo/undo_depth read *stack_ptr at call time", "[script]")
{
    mosaic::script::python::ensure_interpreter();

    World world;
    TypeRegistry reg;
    ComponentReflection types;
    register_builtin_components(reg, types);
    CommandStack commands;

    const auto e = world.create_entity();
    world.add_component<mosaic::engine::Name>(e, mosaic::engine::Name{"before"});

    World* world_ptr = &world;
    const ComponentReflection* types_ptr = &types;
    CommandStack* stack_ptr = nullptr; // deliberately unbound at install time

    py::dict globals;
    globals["__builtins__"] = py::module_::import("builtins");
    mosaic::script::python::install_world_globals(globals, &world_ptr, &types_ptr, &stack_ptr);

    py::exec("d0 = undo_depth()", globals);
    REQUIRE(globals["d0"].cast<std::uint64_t>() == 0);

    // Bind the stack AFTER installing globals -- proves the lambdas read *stack_ptr at
    // call time, not install time.
    stack_ptr = &commands;

    globals["eid"] = py::int_(e.value());
    py::exec("set_component(eid, 'Name', {'value': 'after'})", globals);
    py::exec("d1 = undo_depth()", globals);
    REQUIRE(globals["d1"].cast<std::uint64_t>() == 1);

    py::exec("undone = undo()", globals);
    REQUIRE(globals["undone"].cast<bool>());
    REQUIRE(world.get_component<mosaic::engine::Name>(e)->value == "before");
}
```

Add `unit/script/world_globals_test.cpp` to `tests/CMakeLists.txt`, right after the existing line `unit/script/script_engine_test.cpp`.

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --preset debug   # only if build/debug doesn't exist yet
cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)"
```
Expected: build fails — `modules/script_python/world_globals.hpp` does not exist yet.

- [ ] **Step 3: Implement**

Create `modules/script_python/world_globals.hpp`:

```cpp
#pragma once

#include <pybind11/pybind11.h>

#include "engine/reflection/component_reflection.hpp"
#include "engine/world/world.hpp"
#include "foundation/commands/command_stack.hpp"

namespace mosaic::script::python
{
namespace py = pybind11;

// Installs the shared gameplay-script globals (get_component, set_component,
// create_entity, destroy_entity, entity_count, entities, is_valid, add_component,
// remove_component, has_component, component_names, entities_with, emit, undo, redo,
// undo_depth) into `globals`. world_ptr/types_ptr/stack_ptr are pointers-to-pointer:
// the installed lambdas read *world_ptr/*types_ptr/*stack_ptr at CALL time, not
// install time, so a caller can install once in bind_world() and later just update
// *stack_ptr in bind_commands() without reinstalling anything -- undo/redo/undo_depth
// stay functional (returning false/0) even before *stack_ptr is set.
void install_world_globals(py::dict& globals,
                           mosaic::engine::World** world_ptr,
                           const mosaic::engine::reflection::ComponentReflection** types_ptr,
                           mosaic::foundation::commands::CommandStack** stack_ptr);
} // namespace mosaic::script::python
```

Create `modules/script_python/world_globals.cpp`:

```cpp
#include "modules/script_python/world_globals.hpp"

#include <memory>
#include <string>

#include "engine/commands/set_component_command.hpp"
#include "engine/script/script_event.hpp"
#include "foundation/core/entity_id.hpp"
#include "modules/script_python/value_marshal.hpp"

namespace mosaic::script::python
{
void install_world_globals(py::dict& globals,
                           mosaic::engine::World** world_ptr,
                           const mosaic::engine::reflection::ComponentReflection** types_ptr,
                           mosaic::foundation::commands::CommandStack** stack_ptr)
{
    globals["get_component"] = py::cpp_function(
        [world_ptr, types_ptr](std::uint64_t eid, const std::string& name) -> py::object
        {
            auto v = (*types_ptr)->component_value(**world_ptr, mosaic::foundation::EntityID(eid), name);
            return v ? value_to_py(*v) : py::none();
        });
    globals["set_component"] = py::cpp_function(
        [world_ptr, types_ptr, stack_ptr](std::uint64_t eid, const std::string& name, py::handle value) -> bool
        {
            const auto e = mosaic::foundation::EntityID(eid);
            const mosaic::foundation::serialization::Value v = py_to_value(value);
            if (*stack_ptr != nullptr)
            {
                auto cmd = std::make_unique<mosaic::engine::commands::SetComponentCommand>(
                    **world_ptr, **types_ptr, e, name, v);
                cmd->execute();
                if (!cmd->valid()) { return false; } // failed set: not recorded
                (*stack_ptr)->push_executed(std::move(cmd));
                return true;
            }
            return (*types_ptr)->set_value(**world_ptr, e, name, v);
        });
    globals["create_entity"] = py::cpp_function(
        [world_ptr]() -> std::uint64_t { return (*world_ptr)->create_entity().value(); });
    globals["destroy_entity"] = py::cpp_function(
        [world_ptr](std::uint64_t eid) -> bool
        {
            const auto e = mosaic::foundation::EntityID(eid);
            if (!(*world_ptr)->is_valid(e)) { return false; }
            (*world_ptr)->destroy_entity(e);
            return true;
        });
    globals["entity_count"] = py::cpp_function(
        [world_ptr]() -> std::uint64_t { return static_cast<std::uint64_t>((*world_ptr)->entity_count()); });
    globals["entities"] = py::cpp_function(
        [world_ptr]() -> py::list
        {
            py::list out;
            (*world_ptr)->for_each_entity([&](mosaic::foundation::EntityID e)
                                          { out.append(py::int_(e.value())); });
            return out;
        });
    globals["is_valid"] = py::cpp_function(
        [world_ptr](std::uint64_t eid) -> bool
        { return (*world_ptr)->is_valid(mosaic::foundation::EntityID(eid)); });
    globals["add_component"] = py::cpp_function(
        [world_ptr, types_ptr](std::uint64_t eid, const std::string& name) -> bool
        { return (*types_ptr)->add_default(**world_ptr, mosaic::foundation::EntityID(eid), name); });
    globals["remove_component"] = py::cpp_function(
        [world_ptr, types_ptr](std::uint64_t eid, const std::string& name) -> bool
        { return (*types_ptr)->remove_value(**world_ptr, mosaic::foundation::EntityID(eid), name); });
    globals["has_component"] = py::cpp_function(
        [world_ptr, types_ptr](std::uint64_t eid, const std::string& name) -> bool
        {
            const auto e = mosaic::foundation::EntityID(eid);
            for (const std::string& n : (*types_ptr)->components_on(**world_ptr, e))
            {
                if (n == name) { return true; }
            }
            return false;
        });
    globals["component_names"] = py::cpp_function(
        [world_ptr, types_ptr](std::uint64_t eid) -> py::list
        {
            py::list out;
            for (const std::string& n :
                 (*types_ptr)->components_on(**world_ptr, mosaic::foundation::EntityID(eid)))
            {
                out.append(py::str(n));
            }
            return out;
        });
    globals["entities_with"] = py::cpp_function(
        [world_ptr, types_ptr](const std::string& name) -> py::list
        {
            py::list out;
            (*world_ptr)->for_each_entity(
                [&](mosaic::foundation::EntityID e)
                {
                    for (const std::string& n : (*types_ptr)->components_on(**world_ptr, e))
                    {
                        if (n == name) { out.append(py::int_(e.value())); break; }
                    }
                });
            return out;
        });
    globals["emit"] = py::cpp_function(
        [world_ptr](const std::string& name, py::object payload)
        {
            (*world_ptr)->events().emit(
                mosaic::engine::script::ScriptEvent{name, py_to_value(payload)});
        },
        py::arg("name"), py::arg("payload") = py::none());
    globals["undo"] =
        py::cpp_function([stack_ptr]() -> bool { return *stack_ptr != nullptr && (*stack_ptr)->undo(); });
    globals["redo"] =
        py::cpp_function([stack_ptr]() -> bool { return *stack_ptr != nullptr && (*stack_ptr)->redo(); });
    globals["undo_depth"] = py::cpp_function(
        [stack_ptr]() -> std::uint64_t
        { return *stack_ptr != nullptr ? static_cast<std::uint64_t>((*stack_ptr)->undo_depth()) : 0; });
}
} // namespace mosaic::script::python
```

Add `world_globals.cpp` to `modules/script_python/CMakeLists.txt`'s `add_library` line, so it reads:
```cmake
add_library(mosaic_script_python STATIC python_script_engine.cpp world_globals.cpp)
```

In `modules/script_python/python_script_engine.hpp`, add the `ensure_interpreter` declaration right before the existing `create_python_script_engine` declaration:

```cpp
namespace mosaic::script::python
{
// Ensures the embedded CPython interpreter is initialized (idempotent, process-wide
// std::call_once guard -- safe to call from multiple translation units/modules).
void ensure_interpreter();

// Create a CPython-backed ScriptEngine. Initializes the embedded interpreter once per process
// (never finalized). Injected like create_box3d_backend.
std::unique_ptr<mosaic::engine::script::ScriptEngine> create_python_script_engine();
} // namespace mosaic::script::python
```

In `modules/script_python/python_script_engine.cpp`, find this exact block near the top:

```cpp
namespace
{
// CPython allows a single interpreter and is fragile across finalize/re-init, so initialize once
// per process and never finalize (the process reclaims it at exit). After init, the calling
// (main) thread holds the GIL; v0.1 scripting is single-threaded, so no explicit GIL handoff.
void ensure_interpreter()
{
    static std::once_flag flag;
    std::call_once(flag, [] {
#ifdef MOSAIC_PYTHON_HOME
        // Point CPython at its standard library: PyConfig.home defaults to the linked
        // Python's install prefix (baked in by CMake), overridable at runtime via the
        // PYTHONHOME env var. Without this, embedded CPython can't locate its stdlib
        // relative to the host binary — it warns and pure-Python imports fail.
        PyConfig config;
        PyConfig_InitPythonConfig(&config);
        const char* const env_home = std::getenv("PYTHONHOME");
        const std::string home =
            (env_home != nullptr && env_home[0] != '\0') ? env_home : MOSAIC_PYTHON_HOME;
        const PyStatus status = PyConfig_SetBytesString(&config, &config.home, home.c_str());
        if (PyStatus_Exception(status) == 0) { py::initialize_interpreter(&config); }
        else { py::initialize_interpreter(); } // bad home -> fall back to default init
        PyConfig_Clear(&config);
#else
        py::initialize_interpreter();
#endif
    });
}

// Helper base class: initializes CPython before any pybind11 member objects are
// constructed. py::dict's default constructor calls PyDict_New() immediately, so
// the interpreter must be running before the member initializers run.
// C++ initializes base classes (in declaration order) before member variables,
// so placing InterpreterInit first guarantees the interpreter is up in time.
struct InterpreterInit
{
    InterpreterInit() { ensure_interpreter(); }
};
```

Replace it with (moving `ensure_interpreter`'s *definition* out of the anonymous
namespace, into `mosaic::script::python` directly, so it matches the new public
declaration; `InterpreterInit` stays anonymous/file-local):

```cpp
// CPython allows a single interpreter and is fragile across finalize/re-init, so initialize once
// per process and never finalize (the process reclaims it at exit). After init, the calling
// (main) thread holds the GIL; v0.1 scripting is single-threaded, so no explicit GIL handoff.
// Exported (declared in python_script_engine.hpp) so modules/script_python_studio's
// StudioPluginScriptEngine can share the SAME process-wide std::call_once guard --
// calling py::initialize_interpreter() from two independent std::call_once flags would
// double-initialize CPython and assert.
void ensure_interpreter()
{
    static std::once_flag flag;
    std::call_once(flag, [] {
#ifdef MOSAIC_PYTHON_HOME
        // Point CPython at its standard library: PyConfig.home defaults to the linked
        // Python's install prefix (baked in by CMake), overridable at runtime via the
        // PYTHONHOME env var. Without this, embedded CPython can't locate its stdlib
        // relative to the host binary — it warns and pure-Python imports fail.
        PyConfig config;
        PyConfig_InitPythonConfig(&config);
        const char* const env_home = std::getenv("PYTHONHOME");
        const std::string home =
            (env_home != nullptr && env_home[0] != '\0') ? env_home : MOSAIC_PYTHON_HOME;
        const PyStatus status = PyConfig_SetBytesString(&config, &config.home, home.c_str());
        if (PyStatus_Exception(status) == 0) { py::initialize_interpreter(&config); }
        else { py::initialize_interpreter(); } // bad home -> fall back to default init
        PyConfig_Clear(&config);
#else
        py::initialize_interpreter();
#endif
    });
}

namespace
{
// Helper base class: initializes CPython before any pybind11 member objects are
// constructed. py::dict's default constructor calls PyDict_New() immediately, so
// the interpreter must be running before the member initializers run.
// C++ initializes base classes (in declaration order) before member variables,
// so placing InterpreterInit first guarantees the interpreter is up in time.
struct InterpreterInit
{
    InterpreterInit() { ensure_interpreter(); }
};
```

(This just moves the closing/reopening of `namespace { }` around the function; the
function body itself is unchanged character-for-character.)

Then find `PythonScriptEngine::bind_world`'s entire body (from `void bind_world(` through
its matching closing `}` — the long block installing `get_component` through
`undo_depth`) and replace the whole method with:

```cpp
    void bind_world(mosaic::engine::World& world,
                    const mosaic::engine::reflection::ComponentReflection& types) override
    {
        world_ = &world;
        types_ = &types;
        install_world_globals(globals_, &world_, &types_, &stack_);
    }
```

Add `#include "modules/script_python/world_globals.hpp"` to this file's includes.
Remove `#include "engine/commands/set_component_command.hpp"`, `#include
"engine/script/script_event.hpp"`, and `#include "foundation/core/entity_id.hpp"` from
this file's includes -- they are no longer used here (only `world_globals.cpp` needs
them now). Leave `bind_commands` exactly as it is (`stack_ = &stack;`).

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)"
./build/debug/tests/mosaic_unit_tests "[script]"
```
Expected: all `[script]` tests pass, including the 3 new `world_globals_test.cpp`
cases -- and, critically, every *pre-existing* `[script]` test (in
`script_engine_test.cpp`, `script_world_test.cpp`, `script_entities_test.cpp`,
`value_marshal_test.cpp`) must still pass completely unmodified. That proves the
`bind_world` refactor is behavior-preserving.

- [ ] **Step 5: Commit**

```bash
git add modules/script_python/world_globals.hpp modules/script_python/world_globals.cpp modules/script_python/python_script_engine.hpp modules/script_python/python_script_engine.cpp modules/script_python/CMakeLists.txt tests/unit/script/world_globals_test.cpp tests/CMakeLists.txt
git commit -m "refactor(script): extract install_world_globals; export ensure_interpreter"
```

---

### Task 2: `modules/script_python_studio` — the ImGui-aware plugin engine

**Files:**
- Create: `modules/script_python_studio/studio_plugin_script_engine.hpp`
- Create: `modules/script_python_studio/studio_plugin_script_engine.cpp`
- Create: `modules/script_python_studio/CMakeLists.txt`
- Modify: `CMakeLists.txt` (top-level)

**Interfaces:**
- Consumes: `mosaic::script::python::ensure_interpreter()`,
  `mosaic::script::python::install_world_globals(...)` (Task 1),
  `mosaic::script::python::value_to_py`/`py_to_value`
  (`modules/script_python/value_marshal.hpp`, pre-existing, header-only).
- Produces: `std::unique_ptr<mosaic::engine::script::ScriptEngine>
  mosaic::script::python_studio::create_studio_plugin_script_engine();` -- Task 5
  passes this as `StudioApp`'s new `plugin_factory` constructor argument.

This module has no dedicated unit tests -- it binds real ImGui calls, which need an
active frame/context this suite doesn't have (see Global Constraints). It is verified
by Task 5's build/ctest pass (proving it compiles and links cleanly and doesn't break
anything already covered) and by the manual verification at the end of Task 5.

- [ ] **Step 1: Create the module**

Create `modules/script_python_studio/studio_plugin_script_engine.hpp`:

```cpp
#pragma once

#include <memory>

#include "engine/script/script_engine.hpp"

namespace mosaic::script::python_studio
{
// Create a CPython-backed ScriptEngine for Studio plugins: every gameplay-script
// global (get_component, set_component, ...) plus imgui_* drawing globals for custom
// panels. Shares the SAME process-wide CPython interpreter modules/script_python
// initializes (see ensure_interpreter() in python_script_engine.hpp).
std::unique_ptr<mosaic::engine::script::ScriptEngine> create_studio_plugin_script_engine();
} // namespace mosaic::script::python_studio
```

Create `modules/script_python_studio/studio_plugin_script_engine.cpp`:

```cpp
#include "modules/script_python_studio/studio_plugin_script_engine.hpp"

#include <cstdio>
#include <memory>
#include <optional>
#include <string>

#include <imgui.h>
#include <pybind11/embed.h>

#include "engine/reflection/component_reflection.hpp"
#include "engine/world/world.hpp"
#include "foundation/commands/command_stack.hpp"
#include "modules/script_python/python_script_engine.hpp" // ensure_interpreter
#include "modules/script_python/value_marshal.hpp"
#include "modules/script_python/world_globals.hpp"

namespace py = pybind11;

namespace mosaic::script::python_studio
{
namespace
{
struct InterpreterInit
{
    InterpreterInit() { mosaic::script::python::ensure_interpreter(); }
};

class StudioPluginScriptEngine final
    : private InterpreterInit // must be first — initializes CPython before py::dict globals_
    , public mosaic::engine::script::ScriptEngine
{
public:
    StudioPluginScriptEngine()
    {
        globals_["__builtins__"] = py::module_::import("builtins");
    }

    mosaic::engine::script::ScriptResult run_string(const std::string& code) override
    {
        mosaic::engine::script::ScriptResult result;
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
        catch (const std::exception& e)
        {
            result.ok = false;
            result.error = e.what();
        }
        return result;
    }

    void bind_world(mosaic::engine::World& world,
                    const mosaic::engine::reflection::ComponentReflection& types) override
    {
        world_ = &world;
        types_ = &types;
        mosaic::script::python::install_world_globals(globals_, &world_, &types_, &stack_);
        install_imgui_globals();
    }

    void bind_commands(mosaic::foundation::commands::CommandStack& stack) override
    {
        stack_ = &stack;
    }

    void set_global(const std::string& name,
                    const mosaic::foundation::serialization::Value& value) override
    {
        globals_[py::str(name)] = mosaic::script::python::value_to_py(value);
    }

    std::optional<mosaic::foundation::serialization::Value>
    get_global(const std::string& name) override
    {
        if (!globals_.contains(name)) { return std::nullopt; }
        try
        {
            return mosaic::script::python::py_to_value(globals_[py::str(name)]);
        }
        catch (const std::exception&)
        {
            return std::nullopt;
        }
    }

    mosaic::engine::script::ScriptResult call_function(const std::string& name) override
    {
        mosaic::engine::script::ScriptResult result;
        if (!globals_.contains(name) || !py::isinstance<py::function>(globals_[py::str(name)]))
        {
            result.error = "'" + name + "' is not a defined function";
            return result;
        }
        child_depth_ = 0;
        try
        {
            py::module_ sys = py::module_::import("sys");
            py::module_ io = py::module_::import("io");
            py::object old_out = sys.attr("stdout");
            py::object old_err = sys.attr("stderr");
            py::object buffer = io.attr("StringIO")();
            sys.attr("stdout") = buffer;
            sys.attr("stderr") = buffer;

            struct StreamGuard
            {
                py::module_& sys_;
                py::object& old_out_;
                py::object& old_err_;
                ~StreamGuard() noexcept
                {
                    try { sys_.attr("stdout") = old_out_; sys_.attr("stderr") = old_err_; }
                    catch (...) {}
                }
            } guard{sys, old_out, old_err};

            try
            {
                globals_[py::str(name)]();
                result.ok = true;
            }
            catch (py::error_already_set& e)
            {
                result.ok = false;
                result.error = e.what();
            }
            result.output = buffer.attr("getvalue")().cast<std::string>();
        }
        catch (const std::exception& e)
        {
            result.ok = false;
            if (result.error.empty()) { result.error = e.what(); }
        }

        if (child_depth_ > 0)
        {
            const int unclosed = child_depth_;
            while (child_depth_ > 0) { ImGui::EndChild(); --child_depth_; }
            result.ok = false;
            result.error = (result.error.empty() ? std::string() : result.error + "; ") +
                           std::to_string(unclosed) +
                           " imgui_begin_child() call(s) were never matched by imgui_end_child()";
        }
        return result;
    }

private:
    void install_imgui_globals()
    {
        globals_["imgui_text"] = py::cpp_function(
            [](const std::string& text) { ImGui::TextUnformatted(text.c_str()); });
        globals_["imgui_separator"] = py::cpp_function([]() { ImGui::Separator(); });
        globals_["imgui_same_line"] = py::cpp_function([]() { ImGui::SameLine(); });
        globals_["imgui_spacing"] = py::cpp_function([]() { ImGui::Spacing(); });
        globals_["imgui_button"] = py::cpp_function(
            [](const std::string& label) -> bool { return ImGui::Button(label.c_str()); });
        globals_["imgui_checkbox"] = py::cpp_function(
            [](const std::string& label, bool value) -> bool
            {
                ImGui::Checkbox(label.c_str(), &value);
                return value;
            });
        globals_["imgui_input_text"] = py::cpp_function(
            [](const std::string& label, const std::string& value) -> std::string
            {
                char buf[1024];
                std::snprintf(buf, sizeof(buf), "%s", value.c_str());
                ImGui::InputText(label.c_str(), buf, sizeof(buf));
                return std::string(buf);
            });
        globals_["imgui_slider_float"] = py::cpp_function(
            [](const std::string& label, float value, float lo, float hi) -> float
            {
                ImGui::SliderFloat(label.c_str(), &value, lo, hi);
                return value;
            });
        globals_["imgui_begin_child"] = py::cpp_function(
            [this](const std::string& id, float height)
            {
                ImGui::BeginChild(id.c_str(), ImVec2(0.0f, height));
                ++child_depth_;
            });
        globals_["imgui_end_child"] = py::cpp_function(
            [this]()
            {
                if (child_depth_ > 0)
                {
                    ImGui::EndChild();
                    --child_depth_;
                }
            });
    }

    py::dict globals_;
    mosaic::engine::World* world_ = nullptr;
    const mosaic::engine::reflection::ComponentReflection* types_ = nullptr;
    mosaic::foundation::commands::CommandStack* stack_ = nullptr;
    int child_depth_ = 0;
};
} // namespace

std::unique_ptr<mosaic::engine::script::ScriptEngine> create_studio_plugin_script_engine()
{
    return std::make_unique<StudioPluginScriptEngine>();
}
} // namespace mosaic::script::python_studio
```

Create `modules/script_python_studio/CMakeLists.txt`:

```cmake
add_library(mosaic_script_python_studio STATIC studio_plugin_script_engine.cpp)
target_include_directories(mosaic_script_python_studio PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(mosaic_script_python_studio
  PUBLIC  mosaic_engine
  PRIVATE pybind11::embed mosaic_foundation mosaic_imgui mosaic_script_python)
target_compile_features(mosaic_script_python_studio PUBLIC cxx_std_20)
```

In the top-level `CMakeLists.txt`, find this exact line:

```cmake
add_subdirectory(modules/script_python)
```

and add a new line immediately after it:

```cmake
add_subdirectory(modules/script_python_studio)
```

- [ ] **Step 2: Build to verify it compiles**

```bash
cmake --preset debug   # only if build/debug doesn't exist yet, or CMakeLists.txt changed and needs a reconfigure
cmake --build build/debug --target mosaic_script_python_studio -j"$(nproc)"
```
Expected: builds cleanly. (There is no test to run for this step -- see the note above
this task's steps.)

- [ ] **Step 3: Commit**

```bash
git add modules/script_python_studio CMakeLists.txt
git commit -m "feat(script): add modules/script_python_studio (ImGui-aware plugin engine)"
```

---

### Task 3: `PluginHost` — `MOSAIC_PANELS` parsing and dispatch

**Files:**
- Modify: `studio/plugin_host.hpp`
- Modify: `studio/plugin_host.cpp`
- Test: `tests/unit/studio/plugin_host_test.cpp`

**Interfaces:**
- Consumes: nothing new from Task 1/2 (this task uses `PluginHost`'s existing
  `EngineFactory`/`ScriptEngine` interface exactly as slice 1 already did; it does not
  care which concrete engine the factory produces).
- Produces:
  ```cpp
  struct PluginPanel { std::string plugin_name; std::string label; std::string function_name; };
  const std::vector<PluginPanel>& PluginHost::panels() const;
  const PluginPanel* PluginHost::find_panel(const std::string& name) const;
  mosaic::engine::script::ScriptResult PluginHost::invoke_panel(const PluginPanel& item);
  ```
  Task 5 (`studio_app.cpp`) calls `panels()` after `load()` to register each one with
  `StudioLayout::add_panel` (Task 4), and calls `find_panel`/`invoke_panel` from the
  panel-dispatch loop.

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/studio/plugin_host_test.cpp` (it already has `make_plugin_dir`/
`write_plugin` helpers and the includes/usings this needs):

```cpp
TEST_CASE("PluginHost loads a plugin panel and invoke_panel mutates real World state", "[studio]")
{
    World world;
    TypeRegistry reg;
    ComponentReflection types;
    register_builtin_components(reg, types);
    CommandStack commands;

    const auto e = world.create_entity();
    world.add_component<mosaic::engine::Name>(e, mosaic::engine::Name{"before"});

    const fs::path dir = make_plugin_dir("panel");
    write_plugin(dir, "panel_plugin.py",
        "MOSAIC_PANELS = {\"My Panel\": \"draw_my_panel\"}\n\n"
        "def draw_my_panel():\n"
        "    set_component(" + std::to_string(e.value()) + ", \"Name\", {\"value\": \"drawn\"})\n");

    PluginHost host(create_python_script_engine);
    const auto errors = host.load(dir.string(), world, types, commands);
    REQUIRE(errors.empty());
    REQUIRE(host.panels().size() == 1);
    REQUIRE(host.panels()[0].label == "My Panel");
    REQUIRE(host.menu_items().empty());

    const auto* found = host.find_panel("My Panel");
    REQUIRE(found != nullptr);
    const auto result = host.invoke_panel(*found);
    REQUIRE(result.ok);
    REQUIRE(world.get_component<mosaic::engine::Name>(e)->value == "drawn");

    fs::remove_all(dir);
}

TEST_CASE("PluginHost treats a non-table MOSAIC_PANELS as a load error", "[studio]")
{
    World world;
    TypeRegistry reg;
    ComponentReflection types;
    register_builtin_components(reg, types);
    CommandStack commands;

    const fs::path dir = make_plugin_dir("badpanel");
    write_plugin(dir, "bad.py", "MOSAIC_PANELS = 5\n");

    PluginHost host(create_python_script_engine);
    const auto errors = host.load(dir.string(), world, types, commands);
    REQUIRE(errors.size() == 1);
    REQUIRE(host.panels().empty());

    fs::remove_all(dir);
}

TEST_CASE("PluginHost supports a plugin registering both menu items and panels", "[studio]")
{
    World world;
    TypeRegistry reg;
    ComponentReflection types;
    register_builtin_components(reg, types);
    CommandStack commands;

    const fs::path dir = make_plugin_dir("both");
    write_plugin(dir, "both.py",
        "MOSAIC_MENU_ITEMS = {\"Do Thing\": \"on_do_thing\"}\n"
        "MOSAIC_PANELS = {\"My Panel\": \"draw_my_panel\"}\n"
        "def on_do_thing():\n"
        "    pass\n"
        "def draw_my_panel():\n"
        "    pass\n");

    PluginHost host(create_python_script_engine);
    const auto errors = host.load(dir.string(), world, types, commands);
    REQUIRE(errors.empty());
    REQUIRE(host.menu_items().size() == 1);
    REQUIRE(host.panels().size() == 1);

    fs::remove_all(dir);
}

TEST_CASE("PluginHost::find_panel returns nullptr for an unknown name", "[studio]")
{
    PluginHost host(create_python_script_engine);
    REQUIRE(host.find_panel("Nope") == nullptr);
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)"
```
Expected: build fails -- `panels`/`find_panel`/`invoke_panel`/`PluginPanel` don't exist
on `PluginHost` yet.

- [ ] **Step 3: Implement**

In `studio/plugin_host.hpp`, add right after the existing `PluginMenuItem` struct:

```cpp
// One registered panel, read out of a plugin's MOSAIC_PANELS dict. Structurally
// identical to PluginMenuItem but kept as its own type: a menu item and a panel are
// different concepts today and may diverge later without disturbing menu-item call
// sites.
struct PluginPanel
{
    std::string plugin_name;
    std::string label;
    std::string function_name;
};
```

In the `PluginHost` class's public section, add right after `menu_items()`:

```cpp
    // Every registered panel across all successfully loaded plugins, in load order
    // then MOSAIC_PANELS declaration order.
    const std::vector<PluginPanel>& panels() const { return panels_; }

    // Finds a registered panel by its label (the name it was added to StudioLayout
    // under), or nullptr if none matches.
    const PluginPanel* find_panel(const std::string& name) const;

    // Invokes item's function on its plugin's engine.
    mosaic::engine::script::ScriptResult invoke_panel(const PluginPanel& item);
```

In the private section, add right after `invoke`'s declaration is removed (see below)
and before the `private:` members:

```cpp
private:
    mosaic::engine::script::ScriptResult call_by_name(const std::string& plugin_name,
                                                       const std::string& function_name);

    struct LoadedPlugin
    {
        std::string name;
        std::unique_ptr<mosaic::engine::script::ScriptEngine> engine;
    };

    EngineFactory               factory_;
    std::vector<LoadedPlugin>   plugins_;
    std::vector<PluginMenuItem> menu_items_;
    std::vector<PluginPanel>    panels_;
```

(`invoke`'s public declaration -- `mosaic::engine::script::ScriptResult invoke(const
PluginMenuItem& item);` -- stays exactly where it already is; only the private section
and member list change.)

In `studio/plugin_host.cpp`, replace the entire `PluginHost::load` function with:

```cpp
std::vector<std::string> PluginHost::load(const std::string& directory,
                                          mosaic::engine::World& world,
                                          const mosaic::engine::reflection::ComponentReflection& types,
                                          mosaic::foundation::commands::CommandStack& commands)
{
    plugins_.clear();
    menu_items_.clear();
    panels_.clear();
    std::vector<std::string> errors;

    std::error_code ec;
    if (!fs::exists(directory, ec) || !fs::is_directory(directory, ec)) { return errors; }

    std::error_code it_ec;
    for (auto it = fs::directory_iterator(directory, ec); it != fs::directory_iterator();
        it.increment(it_ec))
    {
        if (it_ec) { break; }
        const auto& entry = *it;
        std::error_code file_ec;
        if (!entry.is_regular_file(file_ec) || file_ec || entry.path().extension() != ".py") { continue; }

        const std::string name = entry.path().stem().string();
        if (!factory_)
        {
            errors.push_back(name + ": scripting is not available in this build");
            continue;
        }
        std::unique_ptr<mosaic::engine::script::ScriptEngine> engine = factory_();
        if (!engine)
        {
            errors.push_back(name + ": failed to create a script engine");
            continue;
        }
        engine->bind_world(world, types);
        engine->bind_commands(commands);

        const auto run = engine->run_string(read_file(entry.path()));
        if (!run.ok)
        {
            errors.push_back(name + ": " + run.error);
            continue;
        }

        bool item_error = false;

        std::vector<PluginMenuItem> plugin_items;
        const auto menu_dict = engine->get_global("MOSAIC_MENU_ITEMS");
        if (menu_dict.has_value())
        {
            if (!menu_dict->is<ser::Table>())
            {
                errors.push_back(name + ": MOSAIC_MENU_ITEMS must be a dict");
                item_error = true;
            }
            else
            {
                for (const auto& [label, value] : menu_dict->as<ser::Table>())
                {
                    if (!value.is<std::string>())
                    {
                        errors.push_back(name + ": MOSAIC_MENU_ITEMS['" + label +
                                         "'] must be a string function name");
                        item_error = true;
                        break;
                    }
                    plugin_items.push_back(PluginMenuItem{name, label, value.as<std::string>()});
                }
            }
        }

        std::vector<PluginPanel> plugin_panels;
        if (!item_error)
        {
            const auto panel_dict = engine->get_global("MOSAIC_PANELS");
            if (panel_dict.has_value())
            {
                if (!panel_dict->is<ser::Table>())
                {
                    errors.push_back(name + ": MOSAIC_PANELS must be a dict");
                    item_error = true;
                }
                else
                {
                    for (const auto& [label, value] : panel_dict->as<ser::Table>())
                    {
                        if (!value.is<std::string>())
                        {
                            errors.push_back(name + ": MOSAIC_PANELS['" + label +
                                             "'] must be a string function name");
                            item_error = true;
                            break;
                        }
                        plugin_panels.push_back(PluginPanel{name, label, value.as<std::string>()});
                    }
                }
            }
        }

        if (item_error) { continue; }

        plugins_.push_back(LoadedPlugin{name, std::move(engine)});
        for (auto& item : plugin_items) { menu_items_.push_back(std::move(item)); }
        for (auto& panel : plugin_panels) { panels_.push_back(std::move(panel)); }
    }

    return errors;
}
```

Replace the existing `PluginHost::invoke` function with these three functions:

```cpp
mosaic::engine::script::ScriptResult PluginHost::call_by_name(const std::string& plugin_name,
                                                               const std::string& function_name)
{
    for (const auto& plugin : plugins_)
    {
        if (plugin.name == plugin_name)
        {
            return plugin.engine->call_function(function_name);
        }
    }
    mosaic::engine::script::ScriptResult result;
    result.error = "plugin '" + plugin_name + "' is not loaded";
    return result;
}

mosaic::engine::script::ScriptResult PluginHost::invoke(const PluginMenuItem& item)
{
    return call_by_name(item.plugin_name, item.function_name);
}

mosaic::engine::script::ScriptResult PluginHost::invoke_panel(const PluginPanel& item)
{
    return call_by_name(item.plugin_name, item.function_name);
}

const PluginPanel* PluginHost::find_panel(const std::string& name) const
{
    for (const auto& panel : panels_)
    {
        if (panel.label == name) { return &panel; }
    }
    return nullptr;
}
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)"
./build/debug/tests/mosaic_unit_tests "PluginHost*"
./build/debug/tests/mosaic_unit_tests "[studio]"
```
Expected: `PluginHost*` shows all cases (the 6 from slice 1 plus the 4 new ones) passing;
`[studio]` shows zero regressions.

- [ ] **Step 5: Commit**

```bash
git add studio/plugin_host.hpp studio/plugin_host.cpp tests/unit/studio/plugin_host_test.cpp
git commit -m "feat(studio): add PluginHost panel registration (MOSAIC_PANELS)"
```

---

### Task 4: `StudioLayout::add_panel`

**Files:**
- Modify: `studio/studio_layout.hpp`
- Modify: `studio/studio_layout.cpp`
- Test: `tests/unit/studio/studio_layout_test.cpp`

**Interfaces:**
- Produces: `void StudioLayout::add_panel(std::string name);` -- idempotent (a no-op if
  a panel with that name already exists in `panels()`). Task 5 calls this once per
  `PluginPanel` after every `PluginHost::load()` call.

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/studio/studio_layout_test.cpp`:

```cpp
TEST_CASE("StudioLayout add_panel appends a new closed panel", "[studio]")
{
    StudioLayout layout;
    layout.add_panel("My Panel");
    REQUIRE(layout.panels().size() == 9);
    REQUIRE_FALSE(layout.is_open("My Panel"));
}

TEST_CASE("StudioLayout add_panel is idempotent", "[studio]")
{
    StudioLayout layout;
    layout.add_panel("My Panel");
    layout.add_panel("My Panel");
    REQUIRE(layout.panels().size() == 9);
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)"
```
Expected: build fails -- `StudioLayout::add_panel` doesn't exist yet.

- [ ] **Step 3: Implement**

In `studio/studio_layout.hpp`, add right after `toggle`'s declaration:

```cpp
    // Adds a new closed panel named `name` if one doesn't already exist (idempotent --
    // safe to call every time plugins reload). Used for plugin-registered panels.
    void add_panel(std::string name);
```

In `studio/studio_layout.cpp`, add right after `toggle`'s definition:

```cpp
void StudioLayout::add_panel(std::string name)
{
    for (const Panel& p : panels_)
    {
        if (p.name == name) { return; }
    }
    panels_.push_back(Panel{std::move(name), false});
}
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)"
./build/debug/tests/mosaic_unit_tests "[studio]"
```
Expected: all `[studio]` tests pass, including the 2 new ones.

- [ ] **Step 5: Commit**

```bash
git add studio/studio_layout.hpp studio/studio_layout.cpp tests/unit/studio/studio_layout_test.cpp
git commit -m "feat(studio): add StudioLayout::add_panel"
```

---

### Task 5: Studio integration, example plugin, docs

**Files:**
- Modify: `studio/studio_app.hpp`
- Modify: `studio/studio_app.cpp`
- Modify: `app/run/editor.cpp`
- Modify: `app/CMakeLists.txt`
- Create: `tools/example_plugins/world_inspector_panel.py`
- Modify: `tools/example_plugins/README.md`
- Modify: `docs/design/scripting.md`
- Modify: `docs/project/next-steps.md`

**Interfaces:**
- Consumes: `PluginHost::EngineFactory` (existing), `PluginHost::panels()`/
  `find_panel()`/`invoke_panel()` (Task 3), `StudioLayout::add_panel()` (Task 4),
  `mosaic::script::python_studio::create_studio_plugin_script_engine()` (Task 2).
- Produces: nothing consumed elsewhere in this plan -- final wiring point.

This is ImGui/`World` UI glue with no independent unit-test coverage for `draw_ui`
itself, consistent with slice 1's equivalent task and every other menu-bar/toolbar
wiring in this project.

- [ ] **Step 1: Wire the second factory into `StudioApp`**

In `studio/studio_app.hpp`, change the constructor to:

```cpp
    explicit StudioApp(std::string scene_path = "scene.json",
                       ScriptConsoleModel::EngineFactory script_factory = {},
                       PluginHost::EngineFactory plugin_factory = {})
        : scene_file_(std::move(scene_path)),
          script_console_(script_factory),
          script_editor_(script_factory),
          plugin_host_(plugin_factory)
    {
    }
```

- [ ] **Step 2: Register plugin panels with `StudioLayout` after every `load()`**

In `studio/studio_app.cpp`, find the existing lazy-first-frame plugin-load block:

```cpp
    if (!plugins_loaded_)
    {
        plugins_loaded_ = true;
        for (const std::string& err : plugin_host_.load("plugins", world, reflection, commands_))
        {
            mosaic::foundation::log::error("plugin", "{}", err);
        }
    }
```

and add a panel-registration loop right after the existing error-logging loop, inside
the same `if`:

```cpp
    if (!plugins_loaded_)
    {
        plugins_loaded_ = true;
        for (const std::string& err : plugin_host_.load("plugins", world, reflection, commands_))
        {
            mosaic::foundation::log::error("plugin", "{}", err);
        }
        for (const PluginPanel& panel : plugin_host_.panels())
        {
            layout_.add_panel(panel.label);
        }
    }
```

Then find the "Reload Plugins" `MenuItem` block:

```cpp
            if (ImGui::MenuItem("Reload Plugins"))
            {
                for (const std::string& err :
                    plugin_host_.load("plugins", world, reflection, commands_))
                {
                    mosaic::foundation::log::error("plugin", "{}", err);
                }
            }
```

and add the same panel-registration loop inside it:

```cpp
            if (ImGui::MenuItem("Reload Plugins"))
            {
                for (const std::string& err :
                    plugin_host_.load("plugins", world, reflection, commands_))
                {
                    mosaic::foundation::log::error("plugin", "{}", err);
                }
                for (const PluginPanel& panel : plugin_host_.panels())
                {
                    layout_.add_panel(panel.label);
                }
            }
```

- [ ] **Step 3: Dispatch plugin panels in the panel-drawing loop**

In `studio/studio_app.cpp`, find this exact block (the tail of the panel-dispatch
`if`/`else if` chain):

```cpp
            else if (panel.name == "Script Editor")
            {
                draw_script_editor(world, reflection);
            }
            else
            {
                ImGui::TextUnformatted("(empty)");
            }
```

and replace it with:

```cpp
            else if (panel.name == "Script Editor")
            {
                draw_script_editor(world, reflection);
            }
            else if (const PluginPanel* p = plugin_host_.find_panel(panel.name))
            {
                const auto result = plugin_host_.invoke_panel(*p);
                if (!result.ok)
                {
                    mosaic::foundation::log::error("plugin", "{}: {}", p->plugin_name, result.error);
                }
            }
            else
            {
                ImGui::TextUnformatted("(empty)");
            }
```

- [ ] **Step 4: Wire the new factory in `app/run/editor.cpp`**

Add this include alongside the existing `#include "modules/script_python/python_script_engine.hpp"`:

```cpp
#include "modules/script_python_studio/studio_plugin_script_engine.hpp"
```

Find the `StudioApp` construction:

```cpp
    mosaic::studio::StudioApp studio(
        options.scene.value_or("scene.json"),
        [] { return mosaic::script::python::create_python_script_engine(); });
```

and add the third argument:

```cpp
    mosaic::studio::StudioApp studio(
        options.scene.value_or("scene.json"),
        [] { return mosaic::script::python::create_python_script_engine(); },
        [] { return mosaic::script::python_studio::create_studio_plugin_script_engine(); });
```

- [ ] **Step 5: Link the new module into the app**

In `app/CMakeLists.txt`, find this exact line:

```cmake
target_link_libraries(mosaic_app PRIVATE mosaic_studio mosaic_script_python)
```

and change it to:

```cmake
target_link_libraries(mosaic_app PRIVATE mosaic_studio mosaic_script_python mosaic_script_python_studio)
```

- [ ] **Step 6: Build and run the full suite**

```bash
cmake --preset debug
cmake --build build/debug --target mosaic mosaic_studio mosaic_unit_tests -j"$(nproc)"
ctest --test-dir build/debug --output-on-failure
```
Expected: clean build; test failures limited to the pre-existing, unrelated `--host`/
`--connect` CLI Catch2 test-name-leading-dash discovery issue (and, if it reproduces
this run, the pre-existing flaky `threaded ScriptSystem` SIGSEGV) -- both documented in
project memory as predating this work.

- [ ] **Step 7: Add an example plugin using the panel API**

Create `tools/example_plugins/world_inspector_panel.py`:

```python
MOSAIC_PANELS = {"World Inspector": "draw_world_inspector"}


def draw_world_inspector():
    imgui_text("Entities: " + str(entity_count()))
    imgui_separator()
    imgui_begin_child("world_inspector_scroll", 300.0)
    for e in entities():
        if has_component(e, "Name"):
            label = get_component(e, "Name")["value"]
        else:
            label = "#" + str(e)
        names = component_names(e)
        imgui_text(label + ": [" + ",".join(names) + "]")
    imgui_end_child()
```

This is the panel-API counterpart to the existing `world_inspector.py` (which works
around the lack of real panels using stacked `UiText` rows and a fixed row cap) --
with a real panel, ImGui's own scroll region shows every entity, not just the first
few. Both files are kept: one demonstrates the menu-item technique, the other the
panel technique.

Add this row to the table in `tools/example_plugins/README.md`, right after the
`world_inspector.py` row:

```markdown
| `world_inspector_panel.py` | (registers a panel, not a menu item) "World Inspector" | `MOSAIC_PANELS`, `imgui_text`, `imgui_separator`, `imgui_begin_child`/`imgui_end_child` -- the panel-API counterpart to `world_inspector.py`, with real scrolling instead of a fixed row cap |
```

- [ ] **Step 8: Document `MOSAIC_PANELS` and the `imgui_*` surface**

In `docs/design/scripting.md`, find the existing "Studio plugins (Plugin SDK, Phase 5)"
section (added by the slice 1 docs PR) and add a new subsection right after its closing
paragraph (the one starting "This is slice 1 of the Plugin SDK..."):

```markdown
### Custom Studio panels (slice 2)

A plugin can also declare `MOSAIC_PANELS`, the panel-registration counterpart to
`MOSAIC_MENU_ITEMS`:

```python
MOSAIC_PANELS = {"My Panel": "draw_my_panel"}

def draw_my_panel():
    imgui_text("hello from a plugin panel")
```

Studio adds the panel to its dockable layout (`StudioLayout::add_panel`) and calls the
named function once per frame while the panel is open, inside the panel's own
`ImGui::Begin`/`End` — the function's `imgui_*` calls append to that window exactly
like native ImGui code would. Nine globals are available: `imgui_text`,
`imgui_separator`, `imgui_same_line`, `imgui_spacing`, `imgui_button`,
`imgui_checkbox`, `imgui_input_text`, `imgui_slider_float`, and
`imgui_begin_child`/`imgui_end_child` for scroll regions.

This needed a second, Studio-only `ScriptEngine` implementation
(`modules/script_python_studio`) — the one place pybind11 and ImGui coexist, the same
pattern `modules/render_vulkan` uses for Vulkan headers. `modules/script_python`
itself still has zero ImGui dependency, and `studio/` still has zero pybind11
dependency; every plugin engine `PluginHost` creates now comes from the richer module,
but the Script Console/Editor (used for ad-hoc gameplay scripting, not panel drawing)
keep using the plain one — calling an `imgui_*` global outside an active panel draw
would corrupt ImGui's internal state.
```

In `docs/project/next-steps.md`, find the "Last updated" line at the top of the file
and prepend a new clause (following the same pattern used for the slice-1 example-
plugins entry) describing this slice: `MOSAIC_PANELS` + the nine `imgui_*` globals +
the new `modules/script_python_studio` module + the `install_world_globals` extraction
+ the mismatched-child-region safety net; link this plan's spec
(`docs/superpowers/specs/2026-09-14-plugin-sdk-custom-panels-design.md`); note that
Plugin SDK slice 3 (custom asset importers) remains future work.

- [ ] **Step 9: Commit**

```bash
git add studio/studio_app.hpp studio/studio_app.cpp app/run/editor.cpp app/CMakeLists.txt tools/example_plugins/world_inspector_panel.py tools/example_plugins/README.md docs/design/scripting.md docs/project/next-steps.md
git commit -m "feat(studio): wire custom Studio panels into the editor"
```

- [ ] **Step 10: Manual verification (report results to your human partner)**

Run `mosaic editor` from inside a project directory containing
`tools/example_plugins/world_inspector_panel.py` copied (or symlinked) into its
`plugins/` directory. Confirm: the "World Inspector" panel appears in the dockable
layout after clicking **Reload Plugins**; opening it shows the entity count and a
scrollable list of every entity in the world (not capped at ~10 like the older
`world_inspector.py`); resizing the panel or scrolling the list works like any other
ImGui window. As a stress check, write a temporary throwaway plugin that calls
`imgui_begin_child(...)` without a matching `imgui_end_child()` and confirm Studio logs
an error to the Console panel instead of corrupting or breaking any other panel's
rendering afterward.
