# Scripting Globals Reference Docs Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend `mosaic docs` to also generate `docs/reference/scripting-globals.md`, documenting all 16 `install_world_globals` functions, all 10 Studio-panel `imgui_*` functions, and the three `MOSAIC_MENU_ITEMS`/`MOSAIC_PANELS`/`MOSAIC_IMPORTERS` plugin conventions — by adding pybind11 docstrings at each registration and introspecting them, not by hand-transcribing the page.

**Architecture:** Add a `py::doc("...")` extra to each of the 26 existing `py::cpp_function` registrations (no behavior change). Extract the 10 `imgui_*` registrations out of a private class method into a standalone `install_imgui_globals` free function (mirroring `install_world_globals`'s existing shape) so a throwaway, non-drawing instance can be built purely for introspection. A new `app/docs/scripting_globals_reference.cpp` builds one throwaway `py::dict` via both install functions, reads each entry's `__doc__`, and renders two introspected sections plus one hand-written "Plugin Conventions" section. `run_docs()` calls it alongside the existing component-reference generator.

**Tech Stack:** C++20, pybind11/CPython (already a hard build dependency since S1), Catch2.

**Spec:** `docs/superpowers/specs/2026-09-16-scripting-globals-reference-docs-design.md`

## Global Constraints

- No behavior change to any of the 16 `install_world_globals` functions or the 10 `imgui_*` functions — this plan only adds docstrings and relocates the `imgui_*` registrations verbatim into a free function; the lambda bodies and guard ordering stay byte-identical.
- `mosaic docs` stays an argument-less CLI subcommand — no new options, no new subcommand.
- `docs/reference/` is already an auto-generated nav group; no `tools/docs/regenerate_nav.py` changes are needed.
- `component_reference.hpp`/`.cpp` stay untouched and Python-free — the new generator lives in its own files.

---

### Task 1: Docstrings on `install_world_globals`

**Files:**
- Modify: `modules/script_python/world_globals.cpp`
- Test: `tests/unit/script/world_globals_test.cpp`

**Interfaces:**
- Consumes: nothing new — `install_world_globals(py::dict&, World**, const ComponentReflection**, CommandStack**)` (`modules/script_python/world_globals.hpp`, unchanged signature).
- Produces: every installed `py::cpp_function` in the dict now has a non-empty `__doc__` (accessible as `globals[name].attr("__doc__").cast<std::string>()`), consumed by Task 3's generator.

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/script/world_globals_test.cpp`:

```cpp
TEST_CASE("install_world_globals: every global has a non-empty docstring", "[script]")
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

    const char* names[] = {"get_component", "set_component", "create_entity", "destroy_entity",
                           "entity_count", "entities", "is_valid", "add_component",
                           "remove_component", "has_component", "component_names",
                           "entities_with", "emit", "undo", "redo", "undo_depth"};
    for (const char* name : names)
    {
        REQUIRE(globals.contains(name));
        const std::string doc = globals[name].attr("__doc__").cast<std::string>();
        INFO("global: " << name);
        REQUIRE_FALSE(doc.empty());
        // The auto-generated signature line is always present; a real docstring
        // means there's a second, non-whitespace line after it.
        REQUIRE(doc.find('\n') != std::string::npos);
    }
}
```

Add `#include <string>` at the top of the file if not already present (it already includes `<cstdint>` — add `<string>` alongside it).

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target mosaic_unit_tests -j"$(nproc)" && ./build/tests/mosaic_unit_tests "install_world_globals: every global has a non-empty docstring"`
Expected: FAIL — every `doc` is just the auto-generated one-line signature (no `\n`), since no docstrings exist yet.

- [ ] **Step 3: Add a `py::doc(...)` extra to each of the 16 registrations**

Edit `modules/script_python/world_globals.cpp`. Each registration becomes (existing lambda body and any existing `py::arg(...)` extras unchanged — `py::doc(...)` is added as an additional trailing extra):

```cpp
    globals["get_component"] = py::cpp_function(
        [world_ptr, types_ptr](std::uint64_t eid, const std::string& name) -> py::object
        {
            auto v = (*types_ptr)->component_value(**world_ptr, mosaic::foundation::EntityID(eid), name);
            return v ? value_to_py(*v) : py::none();
        },
        py::doc("Return a reflected component's fields as a dict, or None if the entity "
                "doesn't have it."));
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
        },
        py::doc("Set a reflected component's fields from a dict; routes through the undo "
                "stack when one is bound, returns False if the set failed."));
    globals["create_entity"] = py::cpp_function(
        [world_ptr]() -> std::uint64_t { return (*world_ptr)->create_entity().value(); },
        py::doc("Create a new entity and return its id."));
    globals["destroy_entity"] = py::cpp_function(
        [world_ptr](std::uint64_t eid) -> bool
        {
            const auto e = mosaic::foundation::EntityID(eid);
            if (!(*world_ptr)->is_valid(e)) { return false; }
            (*world_ptr)->destroy_entity(e);
            return true;
        },
        py::doc("Destroy an entity (deferred to the next World::tick); returns False if "
                "the id isn't valid."));
    globals["entity_count"] = py::cpp_function(
        [world_ptr]() -> std::uint64_t { return static_cast<std::uint64_t>((*world_ptr)->entity_count()); },
        py::doc("Return the number of live entities in the world."));
    globals["entities"] = py::cpp_function(
        [world_ptr]() -> py::list
        {
            py::list out;
            (*world_ptr)->for_each_entity([&](mosaic::foundation::EntityID e)
                                          { out.append(py::int_(e.value())); });
            return out;
        },
        py::doc("Return a list of every live entity's id."));
    globals["is_valid"] = py::cpp_function(
        [world_ptr](std::uint64_t eid) -> bool
        { return (*world_ptr)->is_valid(mosaic::foundation::EntityID(eid)); },
        py::doc("Return whether an entity id currently refers to a live entity."));
    globals["add_component"] = py::cpp_function(
        [world_ptr, types_ptr](std::uint64_t eid, const std::string& name) -> bool
        { return (*types_ptr)->add_default(**world_ptr, mosaic::foundation::EntityID(eid), name); },
        py::doc("Attach a default-constructed component to an entity by name; returns "
                "False if the name is unknown."));
    globals["remove_component"] = py::cpp_function(
        [world_ptr, types_ptr](std::uint64_t eid, const std::string& name) -> bool
        { return (*types_ptr)->remove_value(**world_ptr, mosaic::foundation::EntityID(eid), name); },
        py::doc("Remove a component from an entity by name; returns False if it wasn't "
                "present."));
    globals["has_component"] = py::cpp_function(
        [world_ptr, types_ptr](std::uint64_t eid, const std::string& name) -> bool
        {
            const auto e = mosaic::foundation::EntityID(eid);
            for (const std::string& n : (*types_ptr)->components_on(**world_ptr, e))
            {
                if (n == name) { return true; }
            }
            return false;
        },
        py::doc("Return whether an entity currently has a component by name."));
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
        },
        py::doc("Return the names of every component currently attached to an entity."));
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
        },
        py::doc("Return the ids of every entity that currently has the named component."));
    globals["emit"] = py::cpp_function(
        [world_ptr](const std::string& name, py::object payload)
        {
            (*world_ptr)->events().emit(
                mosaic::engine::script::ScriptEvent{name, py_to_value(payload)});
        },
        py::arg("name"), py::arg("payload") = py::none(),
        py::doc("Emit a named ScriptEvent with an optional payload onto the world's event bus."));
    globals["undo"] =
        py::cpp_function([stack_ptr]() -> bool { return *stack_ptr != nullptr && (*stack_ptr)->undo(); },
                         py::doc("Undo the last recorded command; returns False if there's "
                                 "nothing to undo or no stack is bound."));
    globals["redo"] =
        py::cpp_function([stack_ptr]() -> bool { return *stack_ptr != nullptr && (*stack_ptr)->redo(); },
                         py::doc("Redo the last undone command; returns False if there's "
                                 "nothing to redo or no stack is bound."));
    globals["undo_depth"] = py::cpp_function(
        [stack_ptr]() -> std::uint64_t
        { return *stack_ptr != nullptr ? static_cast<std::uint64_t>((*stack_ptr)->undo_depth()) : 0; },
        py::doc("Return how many commands are currently undoable."));
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target mosaic_unit_tests -j"$(nproc)" && ./build/tests/mosaic_unit_tests "[script]"`
Expected: PASS, including the new case; no other `[script]` case regresses.

- [ ] **Step 5: Commit**

```bash
git add modules/script_python/world_globals.cpp tests/unit/script/world_globals_test.cpp
git commit -m "$(cat <<'EOF'
feat(script): add docstrings to install_world_globals registrations

Prerequisite for the scripting-globals reference docs generator (2b).

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01CEBXDEjzbGDpMyLrKCN4mY
EOF
)"
```

---

### Task 2: Extract `install_imgui_globals` into a free function, with docstrings

**Files:**
- Create: `modules/script_python_studio/imgui_globals.hpp`
- Create: `modules/script_python_studio/imgui_globals.cpp`
- Modify: `modules/script_python_studio/studio_plugin_script_engine.cpp`
- Modify: `modules/script_python_studio/CMakeLists.txt`
- Test: `tests/unit/script/imgui_globals_test.cpp` (new)
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces: `mosaic::script::python_studio::install_imgui_globals(py::dict& globals, std::function<void()> require_drawing, int* child_depth)`, declared in `modules/script_python_studio/imgui_globals.hpp`. Consumed by `StudioPluginScriptEngine::bind_world` (this task) and Task 3's generator.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/script/imgui_globals_test.cpp`:

```cpp
#include <functional>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <pybind11/embed.h>

#include "modules/script_python/python_script_engine.hpp" // ensure_interpreter
#include "modules/script_python_studio/imgui_globals.hpp"

namespace py = pybind11;

TEST_CASE("install_imgui_globals registers all 10 names without invoking them", "[script]")
{
    mosaic::script::python::ensure_interpreter();

    py::dict globals;
    globals["__builtins__"] = py::module_::import("builtins");

    bool require_drawing_called = false;
    int child_depth = 0;
    mosaic::script::python_studio::install_imgui_globals(
        globals, [&] { require_drawing_called = true; }, &child_depth);

    const char* names[] = {"imgui_text", "imgui_separator", "imgui_same_line", "imgui_spacing",
                           "imgui_button", "imgui_checkbox", "imgui_input_text",
                           "imgui_slider_float", "imgui_begin_child", "imgui_end_child"};
    for (const char* name : names)
    {
        REQUIRE(globals.contains(name));
    }
    REQUIRE_FALSE(require_drawing_called);
}

TEST_CASE("install_imgui_globals: every global has a non-empty docstring", "[script]")
{
    mosaic::script::python::ensure_interpreter();

    py::dict globals;
    globals["__builtins__"] = py::module_::import("builtins");
    int child_depth = 0;
    mosaic::script::python_studio::install_imgui_globals(globals, [] {}, &child_depth);

    const char* names[] = {"imgui_text", "imgui_separator", "imgui_same_line", "imgui_spacing",
                           "imgui_button", "imgui_checkbox", "imgui_input_text",
                           "imgui_slider_float", "imgui_begin_child", "imgui_end_child"};
    for (const char* name : names)
    {
        const std::string doc = globals[name].attr("__doc__").cast<std::string>();
        INFO("global: " << name);
        REQUIRE(doc.find('\n') != std::string::npos);
    }
}
```

Add the new test file to `tests/CMakeLists.txt`, right after `unit/script/studio_plugin_script_engine_test.cpp`:

```cmake
  unit/script/studio_plugin_script_engine_test.cpp
  unit/script/imgui_globals_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build -j"$(nproc)"`
Expected: FAIL to configure/build — `modules/script_python_studio/imgui_globals.hpp` doesn't exist yet.

- [ ] **Step 3: Create `imgui_globals.hpp`**

```cpp
#pragma once

#include <functional>

#include <pybind11/pybind11.h>

namespace mosaic::script::python_studio
{
namespace py = pybind11;

// Installs the 10 imgui_* Studio-panel-drawing globals (imgui_text, imgui_separator,
// imgui_same_line, imgui_spacing, imgui_button, imgui_checkbox, imgui_input_text,
// imgui_slider_float, imgui_begin_child, imgui_end_child) into `globals`. Every
// registered function calls `require_drawing()` first (the caller's guard against
// calling into ImGui outside an active panel draw); `child_depth` tracks
// imgui_begin_child/imgui_end_child balance exactly as StudioPluginScriptEngine's
// child_depth_ member did before this was extracted. Installing never invokes any
// of the registered functions or `require_drawing` itself -- safe to call with a
// no-op callback purely to read the registrations' names/docstrings (see
// app/docs/scripting_globals_reference.cpp).
void install_imgui_globals(py::dict& globals, std::function<void()> require_drawing,
                           int* child_depth);
} // namespace mosaic::script::python_studio
```

- [ ] **Step 4: Create `imgui_globals.cpp`**

```cpp
#include "modules/script_python_studio/imgui_globals.hpp"

#include <cstdio>
#include <string>

#include <imgui.h>

namespace mosaic::script::python_studio
{
void install_imgui_globals(py::dict& globals, std::function<void()> require_drawing,
                           int* child_depth)
{
    globals["imgui_text"] = py::cpp_function(
        [require_drawing](const std::string& text)
        {
            require_drawing();
            ImGui::TextUnformatted(text.c_str());
        },
        py::doc("Draw unformatted text in the current panel."));
    globals["imgui_separator"] = py::cpp_function(
        [require_drawing]() { require_drawing(); ImGui::Separator(); },
        py::doc("Draw a horizontal separator line."));
    globals["imgui_same_line"] = py::cpp_function(
        [require_drawing]() { require_drawing(); ImGui::SameLine(); },
        py::doc("Keep the next widget on the same line as the previous one."));
    globals["imgui_spacing"] = py::cpp_function(
        [require_drawing]() { require_drawing(); ImGui::Spacing(); },
        py::doc("Add a small vertical gap."));
    globals["imgui_button"] = py::cpp_function(
        [require_drawing](const std::string& label) -> bool
        {
            require_drawing();
            return ImGui::Button(label.c_str());
        },
        py::doc("Draw a clickable button; returns True on the frame it's clicked."));
    globals["imgui_checkbox"] = py::cpp_function(
        [require_drawing](const std::string& label, bool value) -> bool
        {
            require_drawing();
            ImGui::Checkbox(label.c_str(), &value);
            return value;
        },
        py::doc("Draw a checkbox with the given label and current value; returns the "
                "(possibly toggled) value."));
    globals["imgui_input_text"] = py::cpp_function(
        [require_drawing](const std::string& label, const std::string& value) -> std::string
        {
            require_drawing();
            char buf[1024];
            std::snprintf(buf, sizeof(buf), "%s", value.c_str());
            ImGui::InputText(label.c_str(), buf, sizeof(buf));
            return std::string(buf);
        },
        py::doc("Draw a single-line text field with the given label and current value; "
                "returns the (possibly edited) value."));
    globals["imgui_slider_float"] = py::cpp_function(
        [require_drawing](const std::string& label, float value, float lo, float hi) -> float
        {
            require_drawing();
            ImGui::SliderFloat(label.c_str(), &value, lo, hi);
            return value;
        },
        py::doc("Draw a float slider between lo and hi with the given label and current "
                "value; returns the (possibly changed) value."));
    globals["imgui_begin_child"] = py::cpp_function(
        [require_drawing, child_depth](const std::string& id, float height)
        {
            require_drawing();
            ImGui::BeginChild(id.c_str(), ImVec2(0.0f, height));
            ++(*child_depth);
        },
        py::doc("Begin a scrollable child region with the given id and height; must be "
                "matched by imgui_end_child."));
    globals["imgui_end_child"] = py::cpp_function(
        [require_drawing, child_depth]()
        {
            require_drawing();
            if (*child_depth > 0)
            {
                ImGui::EndChild();
                --(*child_depth);
            }
        },
        py::doc("End the most recently opened child region started by imgui_begin_child."));
}
} // namespace mosaic::script::python_studio
```

- [ ] **Step 5: Wire `StudioPluginScriptEngine` to the free function**

In `modules/script_python_studio/studio_plugin_script_engine.cpp`:

Add `#include "modules/script_python_studio/imgui_globals.hpp"` to the includes.

Replace the `bind_world` method's call:

```cpp
    void bind_world(mosaic::engine::World& world,
                    const mosaic::engine::reflection::ComponentReflection& types) override
    {
        world_ = &world;
        types_ = &types;
        mosaic::script::python::install_world_globals(globals_, &world_, &types_, &stack_);
        install_imgui_globals();
    }
```

with:

```cpp
    void bind_world(mosaic::engine::World& world,
                    const mosaic::engine::reflection::ComponentReflection& types) override
    {
        world_ = &world;
        types_ = &types;
        mosaic::script::python::install_world_globals(globals_, &world_, &types_, &stack_);
        mosaic::script::python_studio::install_imgui_globals(
            globals_, [this] { require_drawing(); }, &child_depth_);
    }
```

Delete the now-unused private `install_imgui_globals()` method entirely (the block from `void install_imgui_globals()` through its closing `}`, i.e. everything currently between `require_drawing()`'s definition and the `private:` member variables) — its body has been moved verbatim into `imgui_globals.cpp` in Step 4. Keep `require_drawing()` itself (still called from `call_function`'s error-recovery path is not the case — check: `require_drawing()` is only called from inside the imgui lambdas, which now live in `imgui_globals.cpp` and call it via the `require_drawing` `std::function` parameter instead of `this->require_drawing()`; the member function `require_drawing()` itself stays on the class, only its *callers* moved).

- [ ] **Step 6: Register the new source file in CMake**

`modules/script_python_studio/CMakeLists.txt`:

```cmake
add_library(mosaic_script_python_studio STATIC studio_plugin_script_engine.cpp imgui_globals.cpp)
```

- [ ] **Step 7: Run tests to verify they pass**

Run: `cmake --build build -j"$(nproc)" && ./build/tests/mosaic_unit_tests "[script]"`
Expected: PASS — the two new `imgui_globals_test.cpp` cases, and all pre-existing `studio_plugin_script_engine_test.cpp` cases (click/error-isolation behavior unchanged, same lambdas, same guard).

- [ ] **Step 8: Commit**

```bash
git add modules/script_python_studio/imgui_globals.hpp modules/script_python_studio/imgui_globals.cpp \
       modules/script_python_studio/studio_plugin_script_engine.cpp modules/script_python_studio/CMakeLists.txt \
       tests/unit/script/imgui_globals_test.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
refactor(script): extract install_imgui_globals into a free function

Mirrors install_world_globals's shape so a throwaway, non-drawing
instance can be built purely for docstring introspection. Adds
docstrings to all 10 imgui_* registrations; no behavior change.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01CEBXDEjzbGDpMyLrKCN4mY
EOF
)"
```

---

### Task 3: `app/docs/scripting_globals_reference` generator

**Files:**
- Create: `app/docs/scripting_globals_reference.hpp`
- Create: `app/docs/scripting_globals_reference.cpp`
- Modify: `app/CMakeLists.txt`
- Test: `tests/unit/app/scripting_globals_reference_test.cpp` (new)
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `mosaic::script::python::install_world_globals` (Task 1's docstrings), `mosaic::script::python_studio::install_imgui_globals` (Task 2's free function), `mosaic::script::python::ensure_interpreter()` (`modules/script_python/python_script_engine.hpp`, pre-existing).
- Produces: `std::string mosaic::app::generate_scripting_globals_reference_markdown()`, declared in `app/docs/scripting_globals_reference.hpp`. Consumed by Task 4's `run_docs()`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/app/scripting_globals_reference_test.cpp`:

```cpp
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "app/docs/scripting_globals_reference.hpp"

using mosaic::app::generate_scripting_globals_reference_markdown;

TEST_CASE("scripting globals reference has the auto-generated banner", "[docs]")
{
    const std::string md = generate_scripting_globals_reference_markdown();
    REQUIRE(md.find("<!-- AUTO-GENERATED by `mosaic docs`") != std::string::npos);
}

TEST_CASE("scripting globals reference documents every world global under its section", "[docs]")
{
    const std::string md = generate_scripting_globals_reference_markdown();
    const std::size_t world_section = md.find("## World & Entity Globals");
    const std::size_t panel_section = md.find("## Studio Panel");
    REQUIRE(world_section != std::string::npos);
    REQUIRE(panel_section != std::string::npos);
    REQUIRE(world_section < panel_section);

    const char* world_names[] = {"get_component", "set_component", "create_entity",
                                 "destroy_entity", "entity_count", "entities", "is_valid",
                                 "add_component", "remove_component", "has_component",
                                 "component_names", "entities_with", "emit", "undo", "redo",
                                 "undo_depth"};
    for (const char* name : world_names)
    {
        const std::string heading = std::string("### ") + name;
        const std::size_t pos = md.find(heading);
        INFO("global: " << name);
        REQUIRE(pos != std::string::npos);
        REQUIRE(pos > world_section);
        REQUIRE(pos < panel_section);
    }
}

TEST_CASE("scripting globals reference documents every imgui_* global under its section", "[docs]")
{
    const std::string md = generate_scripting_globals_reference_markdown();
    const std::size_t panel_section = md.find("## Studio Panel");
    const std::size_t conventions_section = md.find("## Plugin Conventions");
    REQUIRE(panel_section != std::string::npos);
    REQUIRE(conventions_section != std::string::npos);
    REQUIRE(panel_section < conventions_section);

    const char* panel_names[] = {"imgui_text", "imgui_separator", "imgui_same_line",
                                 "imgui_spacing", "imgui_button", "imgui_checkbox",
                                 "imgui_input_text", "imgui_slider_float", "imgui_begin_child",
                                 "imgui_end_child"};
    for (const char* name : panel_names)
    {
        const std::string heading = std::string("### ") + name;
        const std::size_t pos = md.find(heading);
        INFO("global: " << name);
        REQUIRE(pos != std::string::npos);
        REQUIRE(pos > panel_section);
        REQUIRE(pos < conventions_section);
    }
}

TEST_CASE("scripting globals reference documents the plugin conventions", "[docs]")
{
    const std::string md = generate_scripting_globals_reference_markdown();
    REQUIRE(md.find("MOSAIC_MENU_ITEMS") != std::string::npos);
    REQUIRE(md.find("MOSAIC_PANELS") != std::string::npos);
    REQUIRE(md.find("MOSAIC_IMPORTERS") != std::string::npos);
}
```

Add the new test file to `tests/CMakeLists.txt`, right after `unit/app/component_reference_test.cpp`:

```cmake
  unit/app/component_reference_test.cpp
  unit/app/scripting_globals_reference_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build -j"$(nproc)"`
Expected: FAIL to configure/build — `app/docs/scripting_globals_reference.hpp` doesn't exist yet.

- [ ] **Step 3: Create `scripting_globals_reference.hpp`**

```cpp
#pragma once

#include <string>

namespace mosaic::app
{
// Renders a combined markdown reference page documenting every scripting global
// available to gameplay ScriptComponent scripts, Studio menu-item/panel plugins,
// and asset importers (install_world_globals), every Studio-panel-only imgui_*
// global, and the MOSAIC_MENU_ITEMS/MOSAIC_PANELS/MOSAIC_IMPORTERS plugin
// conventions. The first two sections are generated by introspecting a throwaway
// Python globals dict's docstrings (see modules/script_python/world_globals.cpp
// and modules/script_python_studio/imgui_globals.cpp); the conventions section is
// hand-written prose, since those three names are plain dict keys a plugin file
// declares -- there's no registration call to introspect for them.
std::string generate_scripting_globals_reference_markdown();
} // namespace mosaic::app
```

- [ ] **Step 4: Create `scripting_globals_reference.cpp`**

```cpp
#include "app/docs/scripting_globals_reference.hpp"

#include <sstream>

#include <pybind11/embed.h>

#include "engine/reflection/component_reflection.hpp"
#include "engine/world/world.hpp"
#include "foundation/commands/command_stack.hpp"
#include "modules/script_python/python_script_engine.hpp" // ensure_interpreter
#include "modules/script_python/world_globals.hpp"
#include "modules/script_python_studio/imgui_globals.hpp"

namespace py = pybind11;

namespace mosaic::app
{
namespace
{
// Renders one global's "### name\n\n```\nsignature\n```\n\ndocstring body\n\n" block.
// __doc__ is pybind11's auto-generated signature line, a blank line, then the
// py::doc(...) text this generator relies on every registration carrying.
void render_global(std::ostringstream& out, const py::dict& globals, const char* name)
{
    const std::string doc = globals[name].attr("__doc__").cast<std::string>();
    const std::size_t split = doc.find('\n');
    const std::string signature = split == std::string::npos ? doc : doc.substr(0, split);
    const std::string body = split == std::string::npos
                                 ? std::string()
                                 : doc.substr(doc.find_first_not_of('\n', split));

    out << "### `" << name << "`\n\n";
    out << "```\n" << signature << "\n```\n\n";
    out << body << "\n\n";
}
} // namespace

std::string generate_scripting_globals_reference_markdown()
{
    mosaic::script::python::ensure_interpreter();

    mosaic::engine::World* world_ptr = nullptr;
    const mosaic::engine::reflection::ComponentReflection* types_ptr = nullptr;
    mosaic::foundation::commands::CommandStack* stack_ptr = nullptr;
    int child_depth = 0;

    py::dict globals;
    mosaic::script::python::install_world_globals(globals, &world_ptr, &types_ptr, &stack_ptr);
    mosaic::script::python_studio::install_imgui_globals(globals, [] {}, &child_depth);

    std::ostringstream out;
    out << "<!-- AUTO-GENERATED by `mosaic docs` -- do not edit by hand -->\n\n";
    out << "# Scripting Globals Reference\n\n";
    out << "Every global available to gameplay `ScriptComponent` scripts, Studio "
           "menu-item/panel plugins, and asset importers, plus the Studio-panel-only "
           "`imgui_*` drawing globals and the plugin registration conventions. See "
           "`docs/design/scripting.md` for the full narrative.\n\n";

    out << "## World & Entity Globals\n\n";
    const char* world_names[] = {"get_component", "set_component", "create_entity",
                                 "destroy_entity", "entity_count", "entities", "is_valid",
                                 "add_component", "remove_component", "has_component",
                                 "component_names", "entities_with", "emit", "undo", "redo",
                                 "undo_depth"};
    for (const char* name : world_names) { render_global(out, globals, name); }

    out << "## Studio Panel (`imgui_*`) Globals\n\n";
    out << "Available only inside a `MOSAIC_PANELS` draw function.\n\n";
    const char* panel_names[] = {"imgui_text", "imgui_separator", "imgui_same_line",
                                 "imgui_spacing", "imgui_button", "imgui_checkbox",
                                 "imgui_input_text", "imgui_slider_float", "imgui_begin_child",
                                 "imgui_end_child"};
    for (const char* name : panel_names) { render_global(out, globals, name); }

    out << "## Plugin Conventions\n\n";
    out << "### `MOSAIC_MENU_ITEMS`\n\n";
    out << "A module-level dict mapping a menu label string to a zero-argument "
           "function name. Studio's Plugins menu lists every registered label and "
           "dispatches a click to that function:\n\n";
    out << "```python\nMOSAIC_MENU_ITEMS = {\"Say Hello\": \"on_say_hello\"}\n\n"
           "def on_say_hello():\n    print(\"hello from a plugin!\")\n```\n\n";
    out << "### `MOSAIC_PANELS`\n\n";
    out << "A module-level dict mapping a dockable panel's title to a zero-argument "
           "draw function name, called once per frame while the panel is open. Draw "
           "functions use the `imgui_*` globals above:\n\n";
    out << "```python\nMOSAIC_PANELS = {\"My Panel\": \"draw_my_panel\"}\n\n"
           "def draw_my_panel():\n    imgui_text(\"hello from a plugin panel\")\n```\n\n";
    out << "### `MOSAIC_IMPORTERS`\n\n";
    out << "A module-level dict mapping a file extension to a one-argument import "
           "function name, consulted whenever `mosaic import` sees a source extension "
           "that isn't one of the built-in kinds. The function receives the source "
           "path and does its own file I/O; its string return value becomes the "
           "asset's cooked blob:\n\n";
    out << "```python\nMOSAIC_IMPORTERS = {\".csv\": \"import_csv\"}\n\n"
           "def import_csv(source_path):\n    ...\n    return cooked_blob\n```\n\n";

    return out.str();
}
} // namespace mosaic::app
```

- [ ] **Step 5: Register the new source file in CMake**

`app/CMakeLists.txt` — add `docs/scripting_globals_reference.cpp` to `mosaic_app`'s source list, right after `run/docs.cpp`:

```cmake
add_library(mosaic_app STATIC
  cli/options.cpp
  run/status_reporter.cpp
  run/editor.cpp
  run/create.cpp
  run/docs.cpp
  run/build.cpp
  run/import.cpp
  run/plugin.cpp
  asset/font_cook.cpp
  asset/import_plugins.cpp
  plugin/git.cpp
  docs/scripting_globals_reference.cpp
)
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build build -j"$(nproc)" && ./build/tests/mosaic_unit_tests "[docs]"`
Expected: PASS — all 4 new cases, plus the pre-existing `component_reference_test.cpp` `[docs]` cases.

- [ ] **Step 7: Commit**

```bash
git add app/docs/scripting_globals_reference.hpp app/docs/scripting_globals_reference.cpp \
       app/CMakeLists.txt tests/unit/app/scripting_globals_reference_test.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(docs): add scripting globals reference markdown generator

Introspects install_world_globals/install_imgui_globals docstrings
into docs/reference/scripting-globals.md's world/panel sections; the
plugin-conventions section (MOSAIC_MENU_ITEMS/PANELS/IMPORTERS) is
hand-written since those names have no registration call to introspect.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01CEBXDEjzbGDpMyLrKCN4mY
EOF
)"
```

---

### Task 4: Wire into `run_docs()` and update the nav/project doc

**Files:**
- Modify: `app/run/docs.cpp`
- Modify: `docs/project/next-steps.md`

**Interfaces:**
- Consumes: `mosaic::app::generate_scripting_globals_reference_markdown()` (Task 3).
- Produces: `mosaic docs` now writes both `docs/reference/components.md` and `docs/reference/scripting-globals.md` in one run. Nothing downstream consumes this beyond the CLI itself and the nav generator (unchanged, picks the new file up automatically on its next run).

- [ ] **Step 1: Update `run_docs()`**

Edit `app/run/docs.cpp`. Add `#include "app/docs/scripting_globals_reference.hpp"` to the includes. Replace the body of `run_docs` with:

```cpp
int run_docs(const Options&)
{
    mosaic::foundation::reflection::TypeRegistry     registry;
    mosaic::engine::reflection::ComponentReflection  types;
    mosaic::engine::reflection::register_builtin_components(registry, types);

    const std::filesystem::path out_dir = std::filesystem::path(MOSAIC_DOCS_DIR) / "reference";
    std::filesystem::create_directories(out_dir);

    const std::string components_md = generate_component_reference_markdown(types);
    const std::filesystem::path components_file = out_dir / "components.md";
    std::ofstream components_out(components_file, std::ios::trunc);
    if (!components_out)
    {
        mosaic::foundation::log::error("docs", "failed to open {} for writing", components_file.string());
        return 1;
    }
    components_out << components_md;
    mosaic::foundation::log::info("docs", "wrote {}", components_file.string());

    const std::string scripting_md = generate_scripting_globals_reference_markdown();
    const std::filesystem::path scripting_file = out_dir / "scripting-globals.md";
    std::ofstream scripting_out(scripting_file, std::ios::trunc);
    if (!scripting_out)
    {
        mosaic::foundation::log::error("docs", "failed to open {} for writing", scripting_file.string());
        return 1;
    }
    scripting_out << scripting_md;
    mosaic::foundation::log::info("docs", "wrote {}", scripting_file.string());

    return 0;
}
```

- [ ] **Step 2: Build and run `mosaic docs` manually**

Run: `cmake --build build -j"$(nproc)" && ./build/mosaic docs`
Expected: exit 0, both `docs/reference/components.md` and `docs/reference/scripting-globals.md` written; `docs/reference/scripting-globals.md` opens with the auto-generated banner, has `## World & Entity Globals`, `## Studio Panel (\`imgui_*\`) Globals`, and `## Plugin Conventions` sections in that order, and every one of the 26 global names appears as a `### ` heading.

Inspect the file: `head -60 docs/reference/scripting-globals.md` — confirm the rendered docstrings read sensibly (no stray `py::doc` artifacts, no truncated signature lines).

- [ ] **Step 3: Regenerate the nav and confirm the new page is picked up**

Run: `python3 tools/docs/regenerate_nav.py`
Expected: `docs/index.html`'s `Reference` group now lists both `Components` and `Scripting Globals` (or its filename-derived fallback, per the spec's documented banner-fallback case), alphabetically.

- [ ] **Step 4: Run the full test suite**

Run: `cmake --build build -j"$(nproc)" && ctest --test-dir build --output-on-failure`
Expected: all green except the pre-existing flaky `script_system` threaded-Python SIGSEGV case (full-suite-ordering only, unrelated to this change — see project memory `mosaic-threaded-script-test-flaky-segv`).

- [ ] **Step 5: Add the dated `next-steps.md` entry**

Prepend a new dated paragraph to the top of the changelog blob in `docs/project/next-steps.md` (immediately after the opening `# Next Steps — Build Handoff` line and its explanatory paragraph, in the same style as the existing entries), summarizing: scripting-globals reference docs (2b) now complete, closing out the documentation-tooling part of Phase 5 alongside 2a (components); `mosaic docs` now writes two files; docstrings added to `install_world_globals`/`install_imgui_globals`; `install_imgui_globals` extracted to a free function; link to this plan and the spec.

- [ ] **Step 6: Commit**

```bash
git add app/run/docs.cpp docs/index.html docs/reference/components.md docs/reference/scripting-globals.md \
       docs/project/next-steps.md
git commit -m "$(cat <<'EOF'
feat(docs): wire scripting globals reference into mosaic docs

mosaic docs now writes docs/reference/scripting-globals.md alongside
components.md. Closes out documentation tooling part 2 (2a components,
2b scripting globals) for Phase 5.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01CEBXDEjzbGDpMyLrKCN4mY
EOF
)"
```

---

## Self-Review Notes

- **Spec coverage:** docstrings on all 16 world globals (Task 1) ✓; docstrings + free-function extraction on all 10 imgui globals (Task 2) ✓; new generator with two introspected sections + one static conventions section (Task 3) ✓; `run_docs()` wiring, nav pickup, next-steps.md entry (Task 4) ✓. All spec files-touched entries are covered by exactly one task.
- **Placeholder scan:** no TBD/TODO; every step has real code or an exact command.
- **Type consistency:** `install_imgui_globals(py::dict&, std::function<void()>, int*)` is defined once in Task 2 and used identically in Task 2 (the class) and Task 3 (the generator); `generate_scripting_globals_reference_markdown()` is declared in Task 3 and called unchanged in Task 4.
