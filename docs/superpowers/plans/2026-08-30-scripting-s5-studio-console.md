# Scripting S5 — Studio script console Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Script Console panel in the editor: type Python, press Run, and it executes against the live editor `World` (viewport updates as it mutates). Reuses S1–S3; the pillar's visible cap.

**Architecture:** A pure-logic `ScriptConsoleModel` (Studio, no ImGui — headlessly testable) drives the NEUTRAL `ScriptEngine` via an injected factory, holding a persistent REPL engine + a history. StudioApp hosts it (a `CommandStack` + the model + a `char` input buffer + `draw_script_console` glue). The app injects `create_python_script_engine`; `bind_world` is tightened to `const ComponentReflection&` so the editor's const reflection binds.

**Tech Stack:** C++20, CMake, Catch2, ImGui, pybind11 (from S1, via the injected factory). Reuses S2/S3 (`bind_world`/`bind_commands`/`get_component`/`set_component`/entity+undo builtins).

**Spec:** `docs/superpowers/specs/2026-08-30-scripting-s5-studio-console-design.md`

## Global Constraints

- **Tier wall:** `studio` uses ONLY the neutral `ScriptEngine` + a `std::function` factory — NO pybind11. The CPython backend is injected by the app (`app/run/editor.cpp`). `engine` stays Python-free.
- **Reuse S2/S3 unchanged:** the console adds no new script API — it runs strings through the existing surface.
- **Never throw into the UI:** `run_string` never throws (S1); the console shows errors as history entries. A null factory is handled gracefully (one Error entry).
- **`bind_world` const:** after Task 1 it takes `const mosaic::engine::reflection::ComponentReflection&`; all existing callers (non-const) still bind.
- **Build:** `cmake --build build/debug -j`; **Test:** `ctest --test-dir build/debug`. Catch2 names not starting with `--`. Namespaces: `mosaic::studio`, `mosaic::engine::script`, `mosaic::engine::reflection`, `mosaic::foundation::commands`, `mosaic::script::python`.
- **API facts (verified):** `mosaic_unit_tests` already links `mosaic_studio` + `mosaic_script_python` (no CMake change for the model test). `mosaic_app` does NOT link `mosaic_script_python` (Task 3 adds it). Backend `bind_world` is at `python_script_engine.cpp:113`; member `ComponentReflection* types_` at :190 (→ `const`). `StudioLayout` default panels are `{"Hierarchy","Viewport","Inspector","Assets","Console"}` (studio_layout.cpp:7). Panel dispatch is an `if (panel.name == ...)` chain in `studio_app.cpp` (~:61-84). `StudioApp::draw_ui(RenderDevice&, World&, const ComponentReflection&, const AssetRegistry&)`. `create_python_script_engine()` is in `modules/script_python/python_script_engine.hpp` (namespace `mosaic::script::python`). No `imgui_stdlib` is vendored → use a `char` buffer for `InputTextMultiline`.

---

### Task 1: `bind_world` takes `const ComponentReflection&`

**Files:**
- Modify: `engine/script/script_engine.hpp`
- Modify: `modules/script_python/python_script_engine.cpp`

**Interfaces:**
- Produces: `virtual void ScriptEngine::bind_world(World&, const ComponentReflection&) = 0;` (const reflection).

- [ ] **Step 1: Change the neutral declaration** in `engine/script/script_engine.hpp` — the `bind_world` pure virtual currently reads:
```cpp
    virtual void bind_world(mosaic::engine::World& world,
                            mosaic::engine::reflection::ComponentReflection& types) = 0;
```
Change the second parameter to `const`:
```cpp
    virtual void bind_world(mosaic::engine::World& world,
                            const mosaic::engine::reflection::ComponentReflection& types) = 0;
```

- [ ] **Step 2: Change the backend override + member** in `modules/script_python/python_script_engine.cpp`:
  - The `bind_world` override signature's second parameter → `const mosaic::engine::reflection::ComponentReflection& types`.
  - The member declaration (around line 190) `mosaic::engine::reflection::ComponentReflection* types_ = nullptr;` → `const mosaic::engine::reflection::ComponentReflection* types_ = nullptr;`.
  - `types_ = &types;` still compiles (const& → const*). The closures call `types_->component_value(...)`, `types_->set_value(...)`, and construct `SetComponentCommand(*world_, *types_, ...)` — verify those accept a const reflection. `component_value`/`set_value`/`remove_value` are `const` methods (OK). **`SetComponentCommand`'s constructor takes `ComponentReflection&` (non-const)** — so dereferencing a `const*` there won't compile. Fix `SetComponentCommand` to store/accept `const ComponentReflection&` too (its `component_value`/`set_value`/`remove_value` calls are all const): in `engine/commands/set_component_command.hpp`, change the ctor param and the `types_` member to `const reflection::ComponentReflection&`.

- [ ] **Step 3: Build + full suite** (no new test — existing S2/S3/S4 bind_world tests exercise the const path):
```bash
cmake --build build/debug -j && ctest --test-dir build/debug 2>&1 | tail -3
```
Expected: 0 failures (361 tests). If `SetComponentCommand` didn't need the const change, the build tells you; make the minimal change that compiles, keeping all calls to const methods.

- [ ] **Step 4: Commit**
```bash
git add engine/script/script_engine.hpp modules/script_python/python_script_engine.cpp engine/commands/set_component_command.hpp
git commit -m "refactor(script): bind_world takes const ComponentReflection& (const-correct)"
```

---

### Task 2: `ScriptConsoleModel` (Studio, pure logic)

**Files:**
- Create: `studio/script_console.hpp`
- Test: `tests/unit/studio/script_console_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: the neutral `ScriptEngine` (`bind_world` const from Task 1, `bind_commands`, `run_string`); `create_python_script_engine` (test factory).
- Produces: `mosaic::studio::ScriptConsoleModel` with `run(const std::string& code, World&, const ComponentReflection&, CommandStack&)` + `history()` + `clear_history()`; `struct ConsoleEntry{ enum class Kind{Input,Output,Error}; Kind kind; std::string text; }`.

- [ ] **Step 1: Write the failing test `tests/unit/studio/script_console_test.cpp`**

```cpp
#include <memory>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/commands/command_stack.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "modules/script_python/python_script_engine.hpp"
#include "studio/script_console.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::transform::Transform;
using mosaic::foundation::commands::CommandStack;
using mosaic::foundation::math::Vec3;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::studio::ConsoleEntry;
using mosaic::studio::ScriptConsoleModel;
using mosaic::script::python::create_python_script_engine;

namespace
{
ScriptConsoleModel::EngineFactory py_factory()
{
    return [] { return create_python_script_engine(); };
}

bool history_has(const ScriptConsoleModel& m, ConsoleEntry::Kind kind, const std::string& needle)
{
    for (const auto& e : m.history())
    {
        if (e.kind == kind && e.text.find(needle) != std::string::npos) { return true; }
    }
    return false;
}
} // namespace

TEST_CASE("script console runs code and captures output", "[studio]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world; CommandStack commands;
    ScriptConsoleModel console(py_factory());

    console.run("print(6*7)", world, types, commands);
    REQUIRE(history_has(console, ConsoleEntry::Kind::Input, "print(6*7)"));
    REQUIRE(history_has(console, ConsoleEntry::Kind::Output, "42"));
}

TEST_CASE("script console mutates the live world", "[studio]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world; CommandStack commands;
    ScriptConsoleModel console(py_factory());

    console.run("e = create_entity()", world, types, commands);
    REQUIRE(world.entity_count() == 1);
}

TEST_CASE("script console is a persistent REPL across runs", "[studio]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world; CommandStack commands;
    ScriptConsoleModel console(py_factory());

    console.run("x = 5", world, types, commands);
    console.run("print(x)", world, types, commands);
    REQUIRE(history_has(console, ConsoleEntry::Kind::Output, "5"));
}

TEST_CASE("script console surfaces errors without throwing", "[studio]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world; CommandStack commands;
    ScriptConsoleModel console(py_factory());

    console.run("raise ValueError('boom')", world, types, commands);
    REQUIRE(history_has(console, ConsoleEntry::Kind::Error, "boom"));
    // still usable afterwards
    console.run("print(1+1)", world, types, commands);
    REQUIRE(history_has(console, ConsoleEntry::Kind::Output, "2"));
}

TEST_CASE("script console routes mutations through the bound command stack", "[studio]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world; CommandStack commands;
    auto e = world.create_entity();
    Transform t{}; t.position = Vec3{1, 2, 3};
    world.add_component<Transform>(e, t);

    ScriptConsoleModel console(py_factory());
    // pass the entity id in via a run that reads it from a global set by a prior run
    console.run(std::string("eid = ") + std::to_string(e.value()), world, types, commands);
    console.run("t = get_component(eid, 'Transform')\n"
                "t['position']['x'] = 9.0\n"
                "set_component(eid, 'Transform', t)", world, types, commands);
    REQUIRE(world.get_component<Transform>(e)->position.x == 9.0f);
    console.run("undo()", world, types, commands);
    REQUIRE(world.get_component<Transform>(e)->position.x == 1.0f); // reverted via the stack
}

TEST_CASE("script console with no factory reports unavailable, no crash", "[studio]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world; CommandStack commands;
    ScriptConsoleModel console; // empty factory

    console.run("print(1)", world, types, commands);
    REQUIRE(history_has(console, ConsoleEntry::Kind::Error, "not available"));
}
```

- [ ] **Step 2: Register the test + confirm it fails**

Add to `tests/CMakeLists.txt` after `unit/studio/console_test.cpp`:
```cmake
  unit/studio/script_console_test.cpp
```
Build → FAIL (`studio/script_console.hpp` missing).

- [ ] **Step 3: Create `studio/script_console.hpp`**

```cpp
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "engine/reflection/component_reflection.hpp"
#include "engine/script/script_engine.hpp"
#include "engine/world/world.hpp"
#include "foundation/commands/command_stack.hpp"

namespace mosaic::studio
{
// One line in the console transcript.
struct ConsoleEntry
{
    enum class Kind { Input, Output, Error };
    Kind        kind;
    std::string text;
};

// Runs script strings against the live editor World, keeping a transcript. Pure logic (no ImGui):
// the ImGui panel owns the input buffer and calls run(). Drives the NEUTRAL ScriptEngine via an
// injected factory, so Studio stays Python-free. The engine is a persistent REPL — created and bound
// once, reused across runs, so state (globals) survives between commands.
class ScriptConsoleModel
{
public:
    using EngineFactory = std::function<std::unique_ptr<mosaic::engine::script::ScriptEngine>()>;

    explicit ScriptConsoleModel(EngineFactory factory = {}) : factory_(std::move(factory)) {}

    const std::vector<ConsoleEntry>& history() const { return history_; }
    void clear_history() { history_.clear(); }

    void run(const std::string& code, mosaic::engine::World& world,
             const mosaic::engine::reflection::ComponentReflection& reflection,
             mosaic::foundation::commands::CommandStack& commands)
    {
        if (code.empty()) { return; }
        if (!engine_)
        {
            if (!factory_)
            {
                history_.push_back({ConsoleEntry::Kind::Error,
                                    "scripting is not available in this build"});
                return;
            }
            engine_ = factory_();
            engine_->bind_world(world, reflection);
            engine_->bind_commands(commands);
        }
        history_.push_back({ConsoleEntry::Kind::Input, code});
        const mosaic::engine::script::ScriptResult r = engine_->run_string(code);
        if (!r.output.empty()) { history_.push_back({ConsoleEntry::Kind::Output, r.output}); }
        if (!r.ok) { history_.push_back({ConsoleEntry::Kind::Error, r.error}); }
    }

private:
    EngineFactory                                                  factory_;
    std::unique_ptr<mosaic::engine::script::ScriptEngine>          engine_; // persistent REPL
    std::vector<ConsoleEntry>                                      history_;
};
} // namespace mosaic::studio
```

- [ ] **Step 4: Build + test**
```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "script console" --output-on-failure
```
Expected: the six `[studio]` cases PASS. Then full suite once (`ctest --test-dir build/debug 2>&1 | tail -3`) — 0 failures.

- [ ] **Step 5: Commit**
```bash
git add studio/script_console.hpp tests/unit/studio/script_console_test.cpp tests/CMakeLists.txt
git commit -m "feat(studio): ScriptConsoleModel — REPL that scripts the live world"
```

---

### Task 3: StudioApp integration + app wiring

**Files:**
- Modify: `studio/studio_app.hpp` (members + ctor factory + draw decl)
- Modify: `studio/studio_app.cpp` (panel dispatch + `draw_script_console`)
- Modify: `studio/studio_layout.cpp` (add the panel)
- Modify: `app/run/editor.cpp` (inject the factory)
- Modify: `app/CMakeLists.txt` (link `mosaic_script_python`)

**Interfaces:**
- Consumes: `ScriptConsoleModel` (Task 2), `create_python_script_engine` (app).
- Produces: a "Script Console" editor panel bound to the world + a `CommandStack`; `StudioApp(scene_path, EngineFactory)`.

This task's deliverable is GUI glue + wiring; it is verified by **build + full suite green + the standing `mosaic run --window` smoke check** (exit 0, silent stderr — the render/UI path is intact). The console's *logic* is already covered by Task 2's headless tests; the ImGui glue here is thin. (Visual acceptance — typing a script in `mosaic editor` and watching the viewport — is the user's, per project memory that the sandbox can't screenshot the GUI.)

- [ ] **Step 1: Add members + ctor factory to `studio/studio_app.hpp`**

Add includes: `#include "foundation/commands/command_stack.hpp"` and `#include "studio/script_console.hpp"`. Change the ctor to accept a factory and add members:
```cpp
    explicit StudioApp(std::string scene_path = "scene.json",
                       ScriptConsoleModel::EngineFactory script_factory = {})
        : scene_file_(std::move(scene_path)), script_console_(std::move(script_factory))
    {
    }
```
Add the `draw_script_console` private declaration next to `draw_console`:
```cpp
    void draw_script_console(mosaic::engine::World& world,
                             const mosaic::engine::reflection::ComponentReflection& reflection);
```
Add private members (near `console_`):
```cpp
    mosaic::foundation::commands::CommandStack commands_;
    ScriptConsoleModel                         script_console_;
    char                                       script_input_[8192] = {};
```

- [ ] **Step 2: Add the panel to `studio/studio_layout.cpp`**

Add `{"Script Console", true}` to the `panels_` initializer:
```cpp
    : panels_{{"Hierarchy", true}, {"Viewport", true}, {"Inspector", true}, {"Assets", true},
              {"Console", true}, {"Script Console", true}}
```

- [ ] **Step 3: Dispatch + implement the panel in `studio/studio_app.cpp`**

In the panel `if/else if` chain (after the `"Console"` branch), add:
```cpp
            else if (panel.name == "Script Console")
            {
                draw_script_console(world, reflection);
            }
```
Add the method (near `draw_console`), reading the existing ImGui usage in the file for include/style. It uses only core ImGui (no `imgui_stdlib`):
```cpp
void StudioApp::draw_script_console(
    mosaic::engine::World& world,
    const mosaic::engine::reflection::ComponentReflection& reflection)
{
    ImGui::InputTextMultiline("##script_input", script_input_, sizeof(script_input_),
                              ImVec2(-1.0f, 120.0f));
    if (ImGui::Button("Run"))
    {
        script_console_.run(script_input_, world, reflection, commands_);
        script_input_[0] = '\0';
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) { script_console_.clear_history(); }
    ImGui::Separator();
    ImGui::BeginChild("##script_history");
    for (const mosaic::studio::ConsoleEntry& entry : script_console_.history())
    {
        ImVec4 color(0.9f, 0.9f, 0.9f, 1.0f); // Output default
        if (entry.kind == mosaic::studio::ConsoleEntry::Kind::Input)
        {
            color = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
        }
        else if (entry.kind == mosaic::studio::ConsoleEntry::Kind::Error)
        {
            color = ImVec4(0.95f, 0.4f, 0.4f, 1.0f);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(entry.text.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
}
```

- [ ] **Step 4: Inject the factory in `app/run/editor.cpp`**

Add `#include "modules/script_python/python_script_engine.hpp"`. Change the StudioApp construction (around line 91):
```cpp
    mosaic::studio::StudioApp studio(
        options.scene.value_or("scene.json"),
        [] { return mosaic::script::python::create_python_script_engine(); });
```

- [ ] **Step 5: Link the backend into the app in `app/CMakeLists.txt`**

Add `mosaic_script_python` to `mosaic_app`'s link libraries (the `PRIVATE` block that already has `mosaic_studio`):
```cmake
target_link_libraries(mosaic_app PRIVATE mosaic_studio mosaic_script_python)
```

- [ ] **Step 6: Build + full suite + smoke**
```bash
cmake --build build/debug -j && ctest --test-dir build/debug 2>&1 | tail -3
# Smoke the render/UI path is intact (not the editor GUI, which needs a display):
./build/debug/app/mosaic run --window --frames 1 2>err.txt; echo "exit=$?"; cat err.txt
```
Expected: build clean, full suite 0 failures. The `mosaic run --window` smoke should exit 0 with silent/benign stderr (if `--frames` isn't a flag, use the project's standard bounded windowed-run per prior T-series; if a display is unavailable in this environment, note that the build + suite are the gate and the editor GUI is user-verified). Do NOT block the task on an unavailable display — report what you observed.

- [ ] **Step 7: Commit**
```bash
git add studio/studio_app.hpp studio/studio_app.cpp studio/studio_layout.cpp app/run/editor.cpp app/CMakeLists.txt
git commit -m "feat(studio): Script Console panel wired to the live editor world"
```

---

## Notes for the executor

- **`docs/project/next-steps.md`** — after Task 3, mark **S5 complete → the Scripting pillar (S1–S5) COMPLETE → the v0.1 vertical slice COMPLETE**. Note the deferred Play-from-script (needs a neutral host-callback). Commit on this branch (rides in the S5 PR). Update the running test count.
- **Finishing:** use `superpowers:finishing-a-development-branch`; base branch is `claude/scripting-s4-component` (this branch is STACKED on S4 → S3 → S2). Note the stack in the PR body.
- **ImGui note:** if `ImGui::InputTextMultiline` with a `char[]` needs a specific ImGui version's signature, follow the existing ImGui calls in `studio_app.cpp`/`inspector.cpp`. No `imgui_stdlib` is vendored — do not add it.
- The GIL/single-thread assumption holds (the console runs on the UI/main thread).
