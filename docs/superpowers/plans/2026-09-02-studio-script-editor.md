# Studio Script Editor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An in-editor Script Editor panel that edits the selected entity's `ScriptComponent`, validates the new source on Apply (green OK / red error), and — on success — writes it into the component so `ScriptSystem` recompiles it live (script hot-reload, in-editor).

**Architecture:** A pure-logic `ScriptEditorModel` (Studio, no ImGui) test-compiles a source string in a throwaway `ScriptEngine` bound to the live world — exactly as `ScriptSystem` compiles — and writes it into the entity's `ScriptComponent` only on success. A thin ImGui panel edits the *selected* entity's script (via `SelectionModel`), calls the model on Apply, and shows the result. CPython enters only via the app-injected factory; `studio/` stays Python-free.

**Tech Stack:** C++20, CMake, Catch2, ImGui, pybind11 (from S1, via the injected factory). Reuses S4/S5 (`ScriptComponent`/`ScriptEngine`/`ScriptSystem`), `SelectionModel`.

**Spec:** `docs/superpowers/specs/2026-09-02-studio-script-editor-design.md`

## Global Constraints

- **Tier wall:** `studio/` stays Python-free — `ScriptEditorModel` uses the neutral `ScriptEngine` + a `std::function` factory + the reflected `ScriptComponent`; NO pybind11. CPython enters only via the app-injected factory (already wired in `app/run/editor.cpp`).
- **Validate-then-write:** `apply` runs the source in a throwaway engine bound to the live world; a compile/top-level error returns `Error` and **does not** modify the component; success writes the source (→ `ScriptSystem` recompiles live).
- **Never throw into the UI:** `run_string` never throws (S1); a null factory / missing `ScriptComponent` → an `Error` result.
- **Build:** `cmake --build build/debug -j`; **Test:** `ctest --test-dir build/debug`. Catch2 names not starting with `--`. Namespaces: `mosaic::studio`, `mosaic::engine::script`, `mosaic::engine::reflection`, `mosaic::foundation`, `mosaic::script::python`.
- **API facts (verified):** `SelectionModel::selected() -> EntityID` (nil default) + `has_selection() -> bool` (`studio/selection.hpp`). `World::get_component<ScriptComponent>(e)` (nullptr if absent). `ScriptComponent { std::string source; }`. Neutral `ScriptEngine`: `bind_world(World&, const ComponentReflection&)`, `set_global(string, Value)`, `run_string(const string&) -> ScriptResult{bool ok; string error; string output;}`. `create_python_script_engine()` (`modules/script_python/python_script_engine.hpp`, ns `mosaic::script::python`) → `unique_ptr<ScriptEngine>`. `mosaic_unit_tests` already links `mosaic_studio` + `mosaic_script_python`. `StudioApp` ctor already takes `ScriptConsoleModel::EngineFactory` (= `std::function<unique_ptr<ScriptEngine>()>`). The panel dispatch is an `if/else if (panel.name == ...)` chain in `studio_app.cpp` (~:82-109, `"Script Console"` branch at ~:105); default panels in `studio_layout.cpp:7-8`; `studio_layout_test.cpp` asserts the panel count (currently 6). No `imgui_stdlib` is vendored → use a `char` buffer.

---

### Task 1: `ScriptEditorModel` (Studio, pure logic)

**Files:**
- Create: `studio/script_editor.hpp`
- Test: `tests/unit/studio/script_editor_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: neutral `ScriptEngine` (`bind_world`/`set_global`/`run_string`), `ScriptComponent`, `create_python_script_engine` (test factory).
- Produces: `mosaic::studio::ScriptEditorModel` with `ScriptApplyResult apply(const std::string& source, World&, const ComponentReflection&, EntityID)`; `struct ScriptApplyResult { enum class Kind { None, Ok, Error }; Kind kind; std::string message; }`.

- [ ] **Step 1: Write the failing test `tests/unit/studio/script_editor_test.cpp`**

```cpp
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/script/script_component.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "modules/script_python/python_script_engine.hpp"
#include "studio/script_editor.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::script::ScriptComponent;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::studio::ScriptApplyResult;
using mosaic::studio::ScriptEditorModel;
using mosaic::script::python::create_python_script_engine;

namespace
{
ScriptEditorModel::EngineFactory py_factory()
{
    return [] { return create_python_script_engine(); };
}
} // namespace

TEST_CASE("script editor applies a valid source and writes it into the component", "[studio]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    world.add_component<ScriptComponent>(e, ScriptComponent{"def on_update(dt):\n    pass\n"});

    ScriptEditorModel editor(py_factory());
    const std::string src = "def on_update(dt):\n    x = 1\n";
    const ScriptApplyResult r = editor.apply(src, world, types, e);
    REQUIRE(r.kind == ScriptApplyResult::Kind::Ok);
    REQUIRE(world.get_component<ScriptComponent>(e)->source == src);
}

TEST_CASE("script editor rejects a syntax error and leaves the component unchanged", "[studio]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    const std::string original = "def on_update(dt):\n    pass\n";
    world.add_component<ScriptComponent>(e, ScriptComponent{original});

    ScriptEditorModel editor(py_factory());
    const ScriptApplyResult r = editor.apply("def on_update(dt) not python !!", world, types, e);
    REQUIRE(r.kind == ScriptApplyResult::Kind::Error);
    REQUIRE(r.message.find("SyntaxError") != std::string::npos);
    REQUIRE(world.get_component<ScriptComponent>(e)->source == original); // unchanged
}

TEST_CASE("script editor errors on an entity without a ScriptComponent", "[studio]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity(); // no ScriptComponent

    ScriptEditorModel editor(py_factory());
    const ScriptApplyResult r = editor.apply("def on_update(dt):\n    pass\n", world, types, e);
    REQUIRE(r.kind == ScriptApplyResult::Kind::Error);
}

TEST_CASE("script editor with no factory reports unavailable", "[studio]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    world.add_component<ScriptComponent>(e, ScriptComponent{"def on_update(dt):\n    pass\n"});

    ScriptEditorModel editor; // empty factory
    const ScriptApplyResult r = editor.apply("def on_update(dt):\n    pass\n", world, types, e);
    REQUIRE(r.kind == ScriptApplyResult::Kind::Error);
    REQUIRE(r.message.find("not available") != std::string::npos);
}
```

- [ ] **Step 2: Register the test + confirm it fails**

Add to `tests/CMakeLists.txt` after `unit/studio/script_console_test.cpp`:
```cmake
  unit/studio/script_editor_test.cpp
```
Build → FAIL (`studio/script_editor.hpp` missing).

- [ ] **Step 3: Create `studio/script_editor.hpp`**

```cpp
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "engine/reflection/component_reflection.hpp"
#include "engine/script/script_component.hpp"
#include "engine/script/script_engine.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"
#include "foundation/serialization/value.hpp"

namespace mosaic::studio
{
// The outcome of a validate-on-apply.
struct ScriptApplyResult
{
    enum class Kind { None, Ok, Error };
    Kind        kind = Kind::None;
    std::string message; // captured stdout on Ok (usually empty); the error text on Error
};

// Validates + applies an edited ScriptComponent source. Pure logic (no ImGui): the panel owns the
// edit buffer and calls apply(). Drives the NEUTRAL ScriptEngine via an injected factory, so Studio
// stays Python-free. apply() mirrors ScriptSystem's compile step, so a source that validates here
// compiles identically when ScriptSystem reloads it live.
class ScriptEditorModel
{
public:
    using EngineFactory = std::function<std::unique_ptr<mosaic::engine::script::ScriptEngine>()>;

    explicit ScriptEditorModel(EngineFactory factory = {}) : factory_(std::move(factory)) {}

    // Test-compile `source` against a throwaway engine bound to the live world; on success, write it
    // into entity e's ScriptComponent (-> ScriptSystem recompiles it live). On a compile/top-level
    // error, return Error{error} and leave the component untouched. Never throws.
    ScriptApplyResult apply(const std::string& source, mosaic::engine::World& world,
                            const mosaic::engine::reflection::ComponentReflection& reflection,
                            mosaic::foundation::EntityID e)
    {
        if (!factory_)
        {
            return {ScriptApplyResult::Kind::Error, "scripting is not available in this build"};
        }
        std::unique_ptr<mosaic::engine::script::ScriptEngine> engine = factory_();
        engine->bind_world(world, reflection);
        engine->set_global(
            "entity",
            mosaic::foundation::serialization::Value(static_cast<std::uint64_t>(e.value())));
        const mosaic::engine::script::ScriptResult r = engine->run_string(source);
        if (!r.ok)
        {
            return {ScriptApplyResult::Kind::Error, r.error};
        }
        mosaic::engine::script::ScriptComponent* sc =
            world.get_component<mosaic::engine::script::ScriptComponent>(e);
        if (sc == nullptr)
        {
            return {ScriptApplyResult::Kind::Error, "selected entity has no ScriptComponent"};
        }
        sc->source = source;
        return {ScriptApplyResult::Kind::Ok, r.output};
    }

private:
    EngineFactory factory_;
};
} // namespace mosaic::studio
```

- [ ] **Step 4: Build + test**
```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "script editor" --output-on-failure
```
Expected: the four `[studio]` cases PASS. Then the full suite once (`ctest --test-dir build/debug 2>&1 | tail -3`) — 0 failures.

- [ ] **Step 5: Commit**
```bash
git add studio/script_editor.hpp tests/unit/studio/script_editor_test.cpp tests/CMakeLists.txt
git commit -m "feat(studio): ScriptEditorModel — validate + apply an edited ScriptComponent source"
```

---

### Task 2: Script Editor panel + StudioApp wiring

**Files:**
- Modify: `studio/studio_app.hpp` (include + ctor + members + draw decl)
- Modify: `studio/studio_app.cpp` (panel dispatch + `draw_script_editor`)
- Modify: `studio/studio_layout.cpp` (add the panel)
- Modify: `tests/unit/studio/studio_layout_test.cpp` (panel count 6 → 7)

**Interfaces:**
- Consumes: `ScriptEditorModel` (Task 1), `SelectionModel`, `ScriptComponent`.
- Produces: a `"Script Editor"` editor panel.

This task's deliverable is GUI glue; verified by **build + full suite green + `mosaic run --window` smoke** (the render/UI path is intact). The panel's logic is already covered by Task 1's headless tests. Visual acceptance is the user's (edit the orbit cube's script in `mosaic editor`, Apply, watch it change on Play).

- [ ] **Step 1: Add the include + ctor copy + members + draw decl to `studio/studio_app.hpp`**

Add near the other studio includes:
```cpp
#include "studio/script_editor.hpp"
```
Change the ctor to pass a **copy** of the factory to both the console and the editor (do NOT `std::move` into one and leave the other empty):
```cpp
    explicit StudioApp(std::string scene_path = "scene.json",
                       ScriptConsoleModel::EngineFactory script_factory = {})
        : scene_file_(std::move(scene_path)),
          script_console_(script_factory),
          script_editor_(script_factory)
    {
    }
```
Add the private draw declaration next to `draw_script_console`:
```cpp
    void draw_script_editor(mosaic::engine::World& world,
                            const mosaic::engine::reflection::ComponentReflection& reflection);
```
Add private members after the demo-physics members:
```cpp
    ScriptEditorModel                           script_editor_;
    char                                        script_edit_buf_[8192] = {};
    std::optional<mosaic::foundation::EntityID> script_edit_entity_;
    ScriptApplyResult                           script_apply_result_;
```
(`<optional>` and `foundation/core/entity_id.hpp` are already included for the demo members; `ScriptEditorModel`/`ScriptApplyResult` come from the new include.)

- [ ] **Step 2: Dispatch the panel + implement `draw_script_editor` in `studio/studio_app.cpp`**

Add `#include <cstdio>` near the top includes (for `std::snprintf`). In the panel `if/else if` chain, after the `"Script Console"` branch, add:
```cpp
            else if (panel.name == "Script Editor")
            {
                draw_script_editor(world, reflection);
            }
```
Add the method (next to `draw_script_console`), core ImGui only (no `imgui_stdlib`):
```cpp
void StudioApp::draw_script_editor(
    mosaic::engine::World& world,
    const mosaic::engine::reflection::ComponentReflection& reflection)
{
    if (!selection_.has_selection())
    {
        ImGui::TextUnformatted("Select a scripted entity to edit its script.");
        return;
    }
    const mosaic::foundation::EntityID e = selection_.selected();
    mosaic::engine::script::ScriptComponent* sc =
        world.get_component<mosaic::engine::script::ScriptComponent>(e);
    if (sc == nullptr)
    {
        ImGui::TextUnformatted("Selected entity has no ScriptComponent.");
        return;
    }
    // Load the source into the edit buffer when the selected scripted entity changes.
    if (script_edit_entity_ != e)
    {
        script_edit_entity_ = e;
        std::snprintf(script_edit_buf_, sizeof(script_edit_buf_), "%s", sc->source.c_str());
        script_apply_result_ = ScriptApplyResult{};
    }
    ImGui::InputTextMultiline("##script_edit", script_edit_buf_, sizeof(script_edit_buf_),
                              ImVec2(-1.0f, 200.0f));
    if (ImGui::Button("Apply"))
    {
        script_apply_result_ = script_editor_.apply(script_edit_buf_, world, reflection, e);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(applies live on Play)");
    if (script_apply_result_.kind == ScriptApplyResult::Kind::Ok)
    {
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "compiled OK");
    }
    else if (script_apply_result_.kind == ScriptApplyResult::Kind::Error)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.4f, 0.4f, 1.0f));
        ImGui::TextWrapped("%s", script_apply_result_.message.c_str());
        ImGui::PopStyleColor();
    }
}
```

- [ ] **Step 3: Add the panel to `studio/studio_layout.cpp`**

The `panels_` initializer (lines 7-8) currently ends with `{"Console", true}, {"Script Console", true}}`. Add `{"Script Editor", true}` before the closing `}}`:
```cpp
    : panels_{{"Hierarchy", true}, {"Viewport", true}, {"Inspector", true}, {"Assets", true},
              {"Console", true}, {"Script Console", true}, {"Script Editor", true}}
```

- [ ] **Step 4: Update the panel-count assertion in `tests/unit/studio/studio_layout_test.cpp`**

Read the file. It asserts the default panel count (currently `== 6`, after S5's "Script Console"). Bump it to `7` and, if the test has per-panel `is_open("...")` checks, add `REQUIRE(layout.is_open("Script Editor"));`. Change only the count and (optionally) add the one assertion — no other logic.

- [ ] **Step 5: Build + full suite + smoke**
```bash
cmake --build build/debug -j && ctest --test-dir build/debug 2>&1 | tail -3
grep -rn "pybind\|python_script_engine" studio/ && echo "TIER WALL VIOLATION" || echo "tier wall clean (no pybind in studio/)"
./build/debug/app/mosaic run --window 2>err.txt; echo "exit=$?"; head -3 err.txt; rm -f err.txt
```
Expected: build clean, full suite 0 failures, no pybind in `studio/`. The `mosaic run --window` smoke may exit non-zero / time out if no display is available — that is NOT a failure (note what you observed; build + suite are the gate; the editor GUI is user-verified).

- [ ] **Step 6: Commit**
```bash
git add studio/studio_app.hpp studio/studio_app.cpp studio/studio_layout.cpp tests/unit/studio/studio_layout_test.cpp
git commit -m "feat(studio): Script Editor panel — edit + validate + hot-reload the selected script"
```

---

## Notes for the executor

- **`docs/project/next-steps.md`** — after Task 2, add a short entry: the Studio **Script Editor** panel (edit a scripted entity's source, validate-on-Apply, live recompile via `ScriptSystem`) is the first Phase-3 hot-reload slice; note the deferred external-`.py`-file editing + attach-to-unscripted-entity. Update the running test count — confirm the actual `ctest` total (372 + 4 new model tests = **376**) and use it. Commit on this branch (rides in the PR).
- **Finishing:** use `superpowers:finishing-a-development-branch`; base branch is `main` (this branch was cut from `main`).
- **ImGui note:** no `imgui_stdlib` is vendored — use the fixed `char` buffer + `std::snprintf` as shown; follow the existing ImGui calls in `studio_app.cpp` (e.g. `draw_script_console`).
- The GIL/single-thread assumption holds (validation runs on the UI/main thread).
