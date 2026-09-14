# Plugin SDK — Discovery/Lifecycle + Custom Menu Items Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A project-local `plugins/*.py` file can register a Studio "Plugins" menu item that, when clicked, runs plugin code with the same World access gameplay `ScriptComponent`s already have (get/set component, entity create/destroy/query, undo/redo).

**Architecture:** Two small, genuinely neutral additions to the existing `ScriptEngine` interface (`get_global`, `call_function`) let a new Studio-tier `PluginHost` read a plugin's declared `MOSAIC_MENU_ITEMS` dict and dispatch clicks by function name — without `studio/` ever touching pybind11 (it never has, and this preserves that). `PluginHost` constructs one `ScriptEngine` per discovered `.py` file (via the same injected `EngineFactory` pattern `ScriptConsoleModel`/`ScriptEditorModel` already use), isolating each plugin's globals.

**Tech Stack:** C++20, the existing embedded CPython layer (`modules/script_python`, pybind11) — only `PythonScriptEngine`'s implementation touches pybind11 directly; `studio/plugin_host.*` only ever sees the neutral `ScriptEngine` interface and the `foundation::serialization::Value` DOM.

**Spec:** `docs/superpowers/specs/2026-09-13-plugin-sdk-discovery-menu-items-design.md`

## Global Constraints

- `studio/` must never depend on pybind11/`Python.h` directly (existing project rule, unbroken by this plan — see the spec's "Why not store a Python callable in C++" section).
- Native `.so`/`.dll` dynamic-library plugins are explicitly out of scope for this plan.
- No per-plugin enable/disable, manifest, or versioning in this slice — every `.py` file found in the plugins directory loads unconditionally.
- Plugins are project-local only (`plugins/`, resolved relative to the process's current working directory — the same convention scenes/assets already use); no user-global plugin directory.
- A broken plugin (a file that fails to run, or whose `MOSAIC_MENU_ITEMS` is malformed) must never prevent sibling plugins in the same directory from loading, and must never crash Studio.

---

### Task 1: `ScriptEngine::get_global` / `ScriptEngine::call_function`

**Files:**
- Modify: `engine/script/script_engine.hpp`
- Modify: `modules/script_python/python_script_engine.cpp`
- Test: `tests/unit/script/script_engine_test.cpp` (existing file — note this is the real
  test file for this engine; it is NOT named `python_script_engine_test.cpp`)

**Interfaces:**
- Produces: `virtual std::optional<mosaic::foundation::serialization::Value> get_global(const std::string& name) = 0;` and `virtual ScriptResult call_function(const std::string& name) = 0;` on `mosaic::engine::script::ScriptEngine`. Both are pure virtual — every existing `ScriptEngine` implementation must implement them (there is currently exactly one: `PythonScriptEngine`).

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/script/script_engine_test.cpp` (it already includes `<string>`,
`<catch2/catch_test_macros.hpp>`, and `"modules/script_python/python_script_engine.hpp"`,
and has `using mosaic::script::python::create_python_script_engine;` — add one more
include for the `Table` type used below):

```cpp
#include "foundation/serialization/value.hpp"
```

```cpp
TEST_CASE("get_global returns a marshaled string value", "[script]")
{
    auto engine = create_python_script_engine();
    REQUIRE(engine->run_string("x = 'hello'").ok);
    const auto v = engine->get_global("x");
    REQUIRE(v.has_value());
    REQUIRE(v->is<std::string>());
    REQUIRE(v->as<std::string>() == "hello");
}

TEST_CASE("get_global returns a marshaled table value", "[script]")
{
    auto engine = create_python_script_engine();
    REQUIRE(engine->run_string("d = {'a': 'b'}").ok);
    const auto v = engine->get_global("d");
    REQUIRE(v.has_value());
    REQUIRE(v->is<mosaic::foundation::serialization::Table>());
    const auto& t = v->as<mosaic::foundation::serialization::Table>();
    const auto* entry = t.find("a");
    REQUIRE(entry != nullptr);
    REQUIRE(entry->as<std::string>() == "b");
}

TEST_CASE("get_global returns nullopt for an undefined name", "[script]")
{
    auto engine = create_python_script_engine();
    REQUIRE_FALSE(engine->get_global("does_not_exist").has_value());
}

TEST_CASE("get_global returns nullopt for a function value", "[script]")
{
    auto engine = create_python_script_engine();
    REQUIRE(engine->run_string("def f(): pass").ok);
    REQUIRE_FALSE(engine->get_global("f").has_value());
}

TEST_CASE("call_function runs a defined zero-arg function", "[script]")
{
    auto engine = create_python_script_engine();
    REQUIRE(engine->run_string(
        "count = 0\ndef bump():\n    global count\n    count += 1\n    print(count)").ok);
    const auto r = engine->call_function("bump");
    REQUIRE(r.ok);
    REQUIRE(r.output.find("1") != std::string::npos);
}

TEST_CASE("call_function returns an error for an undefined name", "[script]")
{
    auto engine = create_python_script_engine();
    const auto r = engine->call_function("does_not_exist");
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.error.find("does_not_exist") != std::string::npos);
}

TEST_CASE("call_function returns an error for a non-callable name", "[script]")
{
    auto engine = create_python_script_engine();
    REQUIRE(engine->run_string("x = 5").ok);
    const auto r = engine->call_function("x");
    REQUIRE_FALSE(r.ok);
}

TEST_CASE("call_function captures an error raised inside the function", "[script]")
{
    auto engine = create_python_script_engine();
    REQUIRE(engine->run_string("def boom():\n    raise ValueError('nope')").ok);
    const auto r = engine->call_function("boom");
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.error.find("ValueError") != std::string::npos);
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --preset debug   # only if build/debug doesn't exist yet
cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)"
```
Expected: build fails — `get_global`/`call_function` are not members of `ScriptEngine`
yet, so the test file won't compile. That's the correct RED (a missing-member compile
error, not a typo).

- [ ] **Step 3: Implement**

In `engine/script/script_engine.hpp`, add `#include <optional>` to the includes at the
top of the file, then add these two methods to `class ScriptEngine`, right after
`set_global` and before `bind_commands`:

```cpp
    // Reads a global variable's current value out of the script's namespace, marshaled
    // through the same #10 Value DOM set_global writes with. Returns nullopt if the
    // name is absent, or if its value isn't representable in the Value DOM (e.g. a
    // function or class object).
    virtual std::optional<mosaic::foundation::serialization::Value>
        get_global(const std::string& name) = 0;

    // Calls an already-defined zero-argument function in this engine's namespace by
    // name (e.g. one declared by a prior run_string). Returns the same ScriptResult
    // shape as run_string: ok/error/captured stdout. ok=false with a descriptive error
    // if name isn't defined or isn't callable.
    virtual ScriptResult call_function(const std::string& name) = 0;
```

In `modules/script_python/python_script_engine.cpp`, add `#include <optional>` to the
includes at the top, then add these two methods to `class PythonScriptEngine`, right
after the existing `set_global` method and before `bind_commands`:

```cpp
    std::optional<mosaic::foundation::serialization::Value>
    get_global(const std::string& name) override
    {
        if (!globals_.contains(name)) { return std::nullopt; }
        try
        {
            return py_to_value(globals_[py::str(name)]);
        }
        catch (const std::exception&)
        {
            return std::nullopt; // e.g. a function/class/other unmarshalable object
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
        return result;
    }
```

(This mirrors `run_string`'s own stdout-capture scaffolding, already in this same file
a few dozen lines above — same RAII `StreamGuard` shape, same try/catch structure.)

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)"
./build/debug/tests/mosaic_unit_tests "[script]"
```
Expected: all `[script]` tests pass, including the eight new ones above.

- [ ] **Step 5: Commit**

```bash
git add engine/script/script_engine.hpp modules/script_python/python_script_engine.cpp tests/unit/script/script_engine_test.cpp
git commit -m "feat(script): add ScriptEngine::get_global and call_function"
```

---

### Task 2: `PluginHost` — discovery, isolation, dispatch

**Files:**
- Create: `studio/plugin_host.hpp`
- Create: `studio/plugin_host.cpp`
- Modify: `studio/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Test: `tests/unit/studio/plugin_host_test.cpp`

**Interfaces:**
- Consumes: `mosaic::engine::script::ScriptEngine` (Task 1's `get_global`/`call_function`
  plus existing `bind_world`/`bind_commands`/`run_string`), `mosaic::engine::World`,
  `mosaic::engine::reflection::ComponentReflection`, `mosaic::foundation::commands::CommandStack`
  (all pre-existing types, unchanged).
- Produces:
  ```cpp
  struct PluginMenuItem { std::string plugin_name; std::string label; std::string function_name; };
  class PluginHost {
  public:
      using EngineFactory = std::function<std::unique_ptr<mosaic::engine::script::ScriptEngine>()>;
      explicit PluginHost(EngineFactory factory);
      std::vector<std::string> load(const std::string& directory, mosaic::engine::World& world,
                                    const mosaic::engine::reflection::ComponentReflection& types,
                                    mosaic::foundation::commands::CommandStack& commands);
      const std::vector<PluginMenuItem>& menu_items() const;
      mosaic::engine::script::ScriptResult invoke(const PluginMenuItem& item);
  };
  ```
  Task 3 constructs `PluginHost` with the same `EngineFactory` value already threaded
  into `ScriptConsoleModel`/`ScriptEditorModel`, calls `load("plugins", ...)` once per
  Studio session (plus on a "Reload Plugins" menu action), and iterates `menu_items()`
  to build the "Plugins" menu.

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/studio/plugin_host_test.cpp`:

```cpp
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/world/name.hpp"
#include "engine/world/world.hpp"
#include "foundation/commands/command_stack.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "modules/script_python/python_script_engine.hpp"
#include "studio/plugin_host.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::foundation::commands::CommandStack;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::script::python::create_python_script_engine;
using mosaic::studio::PluginHost;

namespace
{
namespace fs = std::filesystem;

// A fresh, uniquely-named temp directory for one test's plugin files.
fs::path make_plugin_dir(const char* tag)
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path dir = fs::temp_directory_path() /
        (std::string("mosaic_plugin_test_") + tag + "_" + std::to_string(now));
    fs::create_directories(dir);
    return dir;
}

void write_plugin(const fs::path& dir, const std::string& filename, const std::string& source)
{
    std::ofstream out(dir / filename);
    out << source;
}
} // namespace

TEST_CASE("PluginHost loads a plugin and its menu item mutates real World state", "[studio]")
{
    World world;
    TypeRegistry reg;
    ComponentReflection types;
    register_builtin_components(reg, types);
    CommandStack commands;

    const auto e = world.create_entity();
    world.add_component<mosaic::engine::Name>(e, mosaic::engine::Name{"before"});

    const fs::path dir = make_plugin_dir("basic");
    write_plugin(dir, "hello.py",
        "MOSAIC_MENU_ITEMS = {\"Say Hello\": \"on_say_hello\"}\n\n"
        "def on_say_hello():\n"
        "    set_component(" + std::to_string(e.value()) + ", \"Name\", {\"value\": \"greeted\"})\n");

    PluginHost host(create_python_script_engine);
    const auto errors = host.load(dir.string(), world, types, commands);
    REQUIRE(errors.empty());
    REQUIRE(host.menu_items().size() == 1);
    REQUIRE(host.menu_items()[0].label == "Say Hello");

    const auto result = host.invoke(host.menu_items()[0]);
    REQUIRE(result.ok);
    REQUIRE(world.get_component<mosaic::engine::Name>(e)->value == "greeted");

    fs::remove_all(dir);
}

TEST_CASE("PluginHost keeps each plugin's globals isolated", "[studio]")
{
    World world;
    TypeRegistry reg;
    ComponentReflection types;
    register_builtin_components(reg, types);
    CommandStack commands;

    const fs::path dir = make_plugin_dir("isolated");
    write_plugin(dir, "a.py", "shared_name = 'from_a'\n");
    write_plugin(dir, "b.py",
        "MOSAIC_MENU_ITEMS = {\"Check\": \"on_check\"}\n"
        "def on_check():\n"
        "    try:\n"
        "        print(shared_name)\n"
        "    except NameError:\n"
        "        print('isolated')\n");

    PluginHost host(create_python_script_engine);
    REQUIRE(host.load(dir.string(), world, types, commands).empty());
    REQUIRE(host.menu_items().size() == 1);

    const auto result = host.invoke(host.menu_items()[0]);
    REQUIRE(result.ok);
    REQUIRE(result.output.find("isolated") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("PluginHost skips a broken plugin but still loads its sibling", "[studio]")
{
    World world;
    TypeRegistry reg;
    ComponentReflection types;
    register_builtin_components(reg, types);
    CommandStack commands;

    const fs::path dir = make_plugin_dir("broken");
    write_plugin(dir, "broken.py", "this is not valid python !!\n");
    write_plugin(dir, "good.py",
        "MOSAIC_MENU_ITEMS = {\"Good\": \"on_good\"}\ndef on_good():\n    print('ok')\n");

    PluginHost host(create_python_script_engine);
    const auto errors = host.load(dir.string(), world, types, commands);
    REQUIRE(errors.size() == 1);
    REQUIRE(errors[0].find("broken") != std::string::npos);
    REQUIRE(host.menu_items().size() == 1);
    REQUIRE(host.menu_items()[0].label == "Good");

    fs::remove_all(dir);
}

TEST_CASE("PluginHost loads a plugin with no menu items and no error", "[studio]")
{
    World world;
    TypeRegistry reg;
    ComponentReflection types;
    register_builtin_components(reg, types);
    CommandStack commands;

    const fs::path dir = make_plugin_dir("nomenu");
    write_plugin(dir, "quiet.py", "x = 1\n");

    PluginHost host(create_python_script_engine);
    const auto errors = host.load(dir.string(), world, types, commands);
    REQUIRE(errors.empty());
    REQUIRE(host.menu_items().empty());

    fs::remove_all(dir);
}

TEST_CASE("PluginHost treats a non-table MOSAIC_MENU_ITEMS as a load error", "[studio]")
{
    World world;
    TypeRegistry reg;
    ComponentReflection types;
    register_builtin_components(reg, types);
    CommandStack commands;

    const fs::path dir = make_plugin_dir("badshape");
    write_plugin(dir, "bad.py", "MOSAIC_MENU_ITEMS = 5\n");

    PluginHost host(create_python_script_engine);
    const auto errors = host.load(dir.string(), world, types, commands);
    REQUIRE(errors.size() == 1);
    REQUIRE(host.menu_items().empty());

    fs::remove_all(dir);
}

TEST_CASE("PluginHost.load a second time replaces the previous plugin set", "[studio]")
{
    World world;
    TypeRegistry reg;
    ComponentReflection types;
    register_builtin_components(reg, types);
    CommandStack commands;

    const fs::path dir = make_plugin_dir("reload");
    write_plugin(dir, "one.py",
        "MOSAIC_MENU_ITEMS = {\"One\": \"on_one\"}\ndef on_one():\n    pass\n");

    PluginHost host(create_python_script_engine);
    REQUIRE(host.load(dir.string(), world, types, commands).empty());
    REQUIRE(host.menu_items().size() == 1);

    fs::remove(dir / "one.py");
    write_plugin(dir, "two.py",
        "MOSAIC_MENU_ITEMS = {\"Two\": \"on_two\"}\ndef on_two():\n    pass\n");

    REQUIRE(host.load(dir.string(), world, types, commands).empty());
    REQUIRE(host.menu_items().size() == 1);
    REQUIRE(host.menu_items()[0].label == "Two");

    fs::remove_all(dir);
}
```

Add the new test file to `tests/CMakeLists.txt`, right after `unit/studio/asset_browser_test.cpp`:

```cmake
  unit/studio/plugin_host_test.cpp
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)"
```
Expected: build fails — `studio/plugin_host.hpp` does not exist yet.

- [ ] **Step 3: Implement**

Create `studio/plugin_host.hpp`:

```cpp
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/reflection/component_reflection.hpp"
#include "engine/script/script_engine.hpp"
#include "engine/world/world.hpp"
#include "foundation/commands/command_stack.hpp"

namespace mosaic::studio
{

// One registered menu item, read out of a plugin's MOSAIC_MENU_ITEMS dict.
struct PluginMenuItem
{
    std::string plugin_name;   // filename stem, e.g. "hello" -- for error messages
    std::string label;         // e.g. "Say Hello"
    std::string function_name; // e.g. "on_say_hello"
};

// Discovers and runs project-local *.py plugins, giving each its own ScriptEngine bound
// to the current World/reflection/commands (the same globals gameplay ScriptComponents
// already get: get_component, set_component, entities_with, undo/redo, ...). A plugin
// registers menu items declaratively via a module-level MOSAIC_MENU_ITEMS dict mapping
// label -> zero-argument function name; PluginHost reads that dict back via
// ScriptEngine::get_global and dispatches clicks via ScriptEngine::call_function -- no
// pybind11/Python.h dependency here, or anywhere else in studio/.
class PluginHost
{
public:
    using EngineFactory = std::function<std::unique_ptr<mosaic::engine::script::ScriptEngine>()>;

    explicit PluginHost(EngineFactory factory) : factory_(std::move(factory)) {}

    // Scans `directory` (non-recursive) for *.py files, running each through a fresh
    // engine bound to world/types/commands. Replaces any previously loaded plugins
    // (this is what "Reload Plugins" calls). Returns one human-readable error string
    // per plugin that failed to load (empty on full success) -- a broken plugin file
    // never prevents the others in the same directory from loading. A missing or
    // non-directory `directory` is not an error: returns an empty vector, zero menu
    // items (a project with no plugins/ folder at all is the common case).
    std::vector<std::string> load(const std::string& directory, mosaic::engine::World& world,
                                  const mosaic::engine::reflection::ComponentReflection& types,
                                  mosaic::foundation::commands::CommandStack& commands);

    // Every registered menu item across all successfully loaded plugins, in load order
    // then MOSAIC_MENU_ITEMS declaration order.
    const std::vector<PluginMenuItem>& menu_items() const { return menu_items_; }

    // Invokes item's function on its plugin's engine.
    mosaic::engine::script::ScriptResult invoke(const PluginMenuItem& item);

private:
    struct LoadedPlugin
    {
        std::string name;
        std::unique_ptr<mosaic::engine::script::ScriptEngine> engine;
    };

    EngineFactory               factory_;
    std::vector<LoadedPlugin>   plugins_;
    std::vector<PluginMenuItem> menu_items_;
};

} // namespace mosaic::studio
```

Create `studio/plugin_host.cpp`:

```cpp
#include "studio/plugin_host.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "foundation/serialization/value.hpp"

namespace mosaic::studio
{
namespace
{
namespace fs = std::filesystem;
namespace ser = mosaic::foundation::serialization;

std::string read_file(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
} // namespace

std::vector<std::string> PluginHost::load(const std::string& directory,
                                          mosaic::engine::World& world,
                                          const mosaic::engine::reflection::ComponentReflection& types,
                                          mosaic::foundation::commands::CommandStack& commands)
{
    plugins_.clear();
    menu_items_.clear();
    std::vector<std::string> errors;

    std::error_code ec;
    if (!fs::exists(directory, ec) || !fs::is_directory(directory, ec)) { return errors; }

    for (const auto& entry : fs::directory_iterator(directory))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".py") { continue; }

        const std::string name = entry.path().stem().string();
        std::unique_ptr<mosaic::engine::script::ScriptEngine> engine = factory_();
        engine->bind_world(world, types);
        engine->bind_commands(commands);

        const auto run = engine->run_string(read_file(entry.path()));
        if (!run.ok)
        {
            errors.push_back(name + ": " + run.error);
            continue;
        }

        const auto items = engine->get_global("MOSAIC_MENU_ITEMS");
        std::vector<PluginMenuItem> plugin_items;
        bool item_error = false;
        if (items.has_value())
        {
            if (!items->is<ser::Table>())
            {
                errors.push_back(name + ": MOSAIC_MENU_ITEMS must be a dict");
                item_error = true;
            }
            else
            {
                for (const auto& [label, value] : items->as<ser::Table>())
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
        if (item_error) { continue; }

        plugins_.push_back(LoadedPlugin{name, std::move(engine)});
        for (auto& item : plugin_items) { menu_items_.push_back(std::move(item)); }
    }

    return errors;
}

mosaic::engine::script::ScriptResult PluginHost::invoke(const PluginMenuItem& item)
{
    for (const auto& plugin : plugins_)
    {
        if (plugin.name == item.plugin_name)
        {
            return plugin.engine->call_function(item.function_name);
        }
    }
    mosaic::engine::script::ScriptResult result;
    result.error = "plugin '" + item.plugin_name + "' is not loaded";
    return result;
}
} // namespace mosaic::studio
```

Add `plugin_host.cpp` to `studio/CMakeLists.txt`'s `add_library(mosaic_studio STATIC ...)` list (alongside the existing `studio_layout.cpp`, `studio_app.cpp`, `inspector.cpp`, `live_link_server.cpp`).

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)"
./build/debug/tests/mosaic_unit_tests "[studio]"
```
Expected: all `[studio]` tests pass, including the six new `PluginHost` cases.

- [ ] **Step 5: Commit**

```bash
git add studio/plugin_host.hpp studio/plugin_host.cpp studio/CMakeLists.txt tests/unit/studio/plugin_host_test.cpp tests/CMakeLists.txt
git commit -m "feat(studio): add PluginHost (discovery/lifecycle + menu item dispatch)"
```

---

### Task 3: Studio integration — the "Plugins" menu

**Files:**
- Modify: `studio/studio_app.hpp`
- Modify: `studio/studio_app.cpp`
- Modify: `docs/project/next-steps.md`

**Interfaces:**
- Consumes: `PluginHost` (Task 2), the existing `ScriptConsoleModel::EngineFactory
  script_factory` constructor parameter (already threaded into `script_console_`/
  `script_editor_`), the existing private `mosaic::foundation::commands::CommandStack
  commands_` member.
- Produces: nothing consumed elsewhere in this plan — this is the final wiring point.

This is ImGui/`World` UI glue with no independent unit-test coverage for `draw_ui`
itself, consistent with how the rest of Studio's menu-bar/toolbar wiring is verified
(build-clean + full `ctest`, not a new unit test) — see e.g. the Blender Live-Link
slices' equivalent integration tasks.

- [ ] **Step 1: Wire `PluginHost` into `StudioApp`**

In `studio/studio_app.hpp`, add `#include "studio/plugin_host.hpp"` alongside the other
`#include "studio/..."` lines, then modify the constructor's initializer list to also
construct `plugin_host_`:

```cpp
    explicit StudioApp(std::string scene_path = "scene.json",
                       ScriptConsoleModel::EngineFactory script_factory = {})
        : scene_file_(std::move(scene_path)),
          script_console_(script_factory),
          script_editor_(script_factory),
          plugin_host_(script_factory)
    {
    }
```

Add two new private members, right after the existing `LiveLinkServer live_link_;` line:

```cpp
    PluginHost plugin_host_;       // project-local *.py plugins
    bool       plugins_loaded_ = false; // true after the first load() this session
```

- [ ] **Step 2: Load plugins on first frame, add the "Plugins" menu**

In `studio/studio_app.cpp`, find the existing lazy-first-frame block:

```cpp
    if (!demo_entity_)
    {
        world.for_each<mosaic::engine::script::ScriptComponent>(
            [&](mosaic::engine::World::EntityID e, mosaic::engine::script::ScriptComponent& sc)
            {
                if (!demo_entity_) { demo_entity_ = e; demo_script_ = sc.source; }
            });
    }
```

and add a second lazy-load block right after it, for plugins:

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

Then, inside the existing `if (ImGui::BeginMainMenuBar()) { ... }` block, right after
the closing `}` of the `if (ImGui::BeginMenu("File")) { ... }` block and before the
"Simulation toolbar" comment/Play-Stop button code, add a new "Plugins" menu:

```cpp
        if (ImGui::BeginMenu("Plugins"))
        {
            if (ImGui::MenuItem("Reload Plugins"))
            {
                for (const std::string& err :
                    plugin_host_.load("plugins", world, reflection, commands_))
                {
                    mosaic::foundation::log::error("plugin", "{}", err);
                }
            }
            ImGui::Separator();
            for (const PluginMenuItem& item : plugin_host_.menu_items())
            {
                if (ImGui::MenuItem(item.label.c_str()))
                {
                    const auto result = plugin_host_.invoke(item);
                    if (!result.ok)
                    {
                        mosaic::foundation::log::error("plugin", "{}: {}", item.plugin_name,
                                                       result.error);
                    }
                }
            }
            ImGui::EndMenu();
        }
```

`draw_ui`'s parameter is named `reflection` (its signature is `draw_ui(RenderDevice&
device, World& world, const ComponentReflection& reflection, const AssetRegistry&
registry)`) — use that exact parameter name, not `types`, when calling `plugin_host_.load(...)`
from inside `draw_ui`. No new `#include` is needed for `mosaic::foundation::log::error`
in this file — it is already used elsewhere in `studio_app.cpp` (a `log::to_string` call
further down) via an existing transitive include.

- [ ] **Step 3: Build and run the full suite**

```bash
cmake --build build/debug --target mosaic mosaic_studio mosaic_unit_tests -j"$(nproc)"
ctest --test-dir build/debug --output-on-failure
```
Expected: clean build; test failures should be limited to the pre-existing, unrelated
`--host`/`--connect` CLI Catch2 test-name-leading-dash discovery issue and (if it
reproduces this run) the pre-existing flaky `threaded ScriptSystem` SIGSEGV — both
documented in project memory as predating this work. Nothing else should fail.

- [ ] **Step 4: Mark the slice done in next-steps.md**

Open `docs/project/next-steps.md` and update its "Last updated" line (at the top),
following the same style as the existing Blender Live-Link entries — prepend a new
clause describing this slice: the first Plugin SDK slice (discovery/lifecycle + custom
menu items) is done; a project-local `plugins/*.py` file declares a `MOSAIC_MENU_ITEMS`
dict mapping label to a zero-argument function name, gets its own isolated
`ScriptEngine` bound to the current World (the same `get_component`/`set_component`/
etc. globals gameplay scripts already have), and a broken plugin never blocks its
siblings or crashes Studio; link this plan's spec
(`docs/superpowers/specs/2026-09-13-plugin-sdk-discovery-menu-items-design.md`). Note
explicitly that native `.so`/`.dll` plugins, per-plugin enable/disable, and Plugin SDK
slices 2 (custom Studio panels) and 3 (custom asset importers) remain future work, per
the agreed Phase 5 sequencing.

- [ ] **Step 5: Commit**

```bash
git add studio/studio_app.hpp studio/studio_app.cpp docs/project/next-steps.md
git commit -m "feat(studio): wire PluginHost into the editor (Plugins menu, first-frame load)"
```

- [ ] **Step 6: Manual verification (report results to your human partner)**

Run `mosaic editor` from inside a project directory containing a `plugins/hello.py`
with:
```python
MOSAIC_MENU_ITEMS = {"Say Hello": "on_say_hello"}

def on_say_hello():
    print("hello from a plugin!")
```
Confirm the "Plugins" menu shows "Say Hello" and "Reload Plugins"; clicking "Say Hello"
produces no crash (the print output lands in the process's own stdout, not currently
surfaced to the Console panel by this slice — that's expected, since `ScriptResult` is
only used here for error reporting, not general stdout display). Confirm a
deliberately-broken second plugin file in the same directory logs an error to the
Console panel (via `log::error`) without preventing "Say Hello" from still working.
