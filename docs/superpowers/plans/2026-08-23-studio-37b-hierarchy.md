# Studio 37b — Scene Hierarchy Panel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fill the editor's Hierarchy panel with the world's entities, add a selection model, surface the selection in the Inspector placeholder, and fold in two 37a follow-up fixes.

**Architecture:** A pure `SelectionModel` and a `build_hierarchy(World)` enumeration seam (both unit-tested) live in the `studio/` tier; `StudioApp::draw_ui(const World&)` renders the entity list as clickable ImGui rows and shows the selection. `run_editor` populates a small demo world and passes it in. Two folded 37a fixes harden `run_editor`'s init and the Vulkan ImGui image count.

**Tech Stack:** C++20, Dear ImGui (docking), Catch2, CMake.

**Design spec:** `docs/superpowers/specs/2026-08-23-studio-37b-hierarchy-design.md`

## Global Constraints

- **Tier contract:** `studio/` may depend on `engine/` and `foundation/` (it reads `World`/`EntityID`); `engine/` and `foundation/` never depend on `studio/`. `studio/CMakeLists.txt` links `mosaic_engine` PUBLIC (World appears in public signatures) and `mosaic_imgui` PRIVATE (ImGui is used only in `.cpp`).
- **Language / build:** C++20. `cmake --preset debug` to (re)configure after CMake changes; `cmake --build --preset debug`; full suite `ctest --preset debug`; unit binary `build/debug/tests/mosaic_unit_tests "<tag>"`; app binary `build/debug/app/mosaic`.
- **Tests:** Catch2 under `tests/unit/studio/`, tag `[studio]`, registered in `tests/CMakeLists.txt` (source list + `mosaic_studio` already linked into `mosaic_unit_tests`). `TEST_CASE` names must not start with `--`.
- **Editor is windowed-only:** verify functionally with `mosaic editor --duration 1` (exit 0, silent stderr). The GUI cannot be screenshotted in the sandbox.
- **Entity labels:** `"Entity " + std::to_string(id.index())`. Flat list (no nesting), no `Name` component — both deferred.
- **Commits:** one per task; end messages with `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.

---

### Task 1: `SelectionModel`

**Files:**
- Create: `studio/selection.hpp`
- Test: `tests/unit/studio/selection_test.cpp`
- Modify: `tests/CMakeLists.txt` (add the test source)

**Interfaces:**
- Produces: `mosaic::studio::SelectionModel` — `selected() -> EntityID`, `has_selection() -> bool`, `is_selected(EntityID) -> bool`, `select(EntityID)`, `clear()`. Default state: nothing selected.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/studio/selection_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "studio/selection.hpp"

using mosaic::foundation::EntityID;
using mosaic::studio::SelectionModel;

TEST_CASE("SelectionModel starts with nothing selected", "[studio]")
{
    SelectionModel sel;
    REQUIRE_FALSE(sel.has_selection());
    REQUIRE_FALSE(static_cast<bool>(sel.selected()));
}

TEST_CASE("SelectionModel select then query", "[studio]")
{
    SelectionModel sel;
    const EntityID a{3, 1};
    sel.select(a);
    REQUIRE(sel.has_selection());
    REQUIRE(sel.selected() == a);
    REQUIRE(sel.is_selected(a));
    REQUIRE_FALSE(sel.is_selected(EntityID{4, 1}));
}

TEST_CASE("SelectionModel clear resets to nothing selected", "[studio]")
{
    SelectionModel sel;
    sel.select(EntityID{2, 1});
    sel.clear();
    REQUIRE_FALSE(sel.has_selection());
    REQUIRE_FALSE(sel.is_selected(EntityID{2, 1}));
}

TEST_CASE("SelectionModel selecting a different entity updates selection", "[studio]")
{
    SelectionModel sel;
    sel.select(EntityID{1, 1});
    sel.select(EntityID{2, 1});
    REQUIRE(sel.is_selected(EntityID{2, 1}));
    REQUIRE_FALSE(sel.is_selected(EntityID{1, 1}));
}
```

Register it: in `tests/CMakeLists.txt`, add `unit/studio/selection_test.cpp` to the `mosaic_unit_tests` source list (next to `unit/studio/studio_layout_test.cpp`).

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --preset debug && cmake --build --preset debug && build/debug/tests/mosaic_unit_tests "[studio]"`
Expected: FAIL — `studio/selection.hpp` does not exist.

- [ ] **Step 3: Create the header**

Create `studio/selection.hpp`:

```cpp
#pragma once

#include "foundation/core/entity_id.hpp"

namespace mosaic::studio
{

// The editor's current entity selection, or none. Pure studio state; the Hierarchy
// panel writes it and the Inspector reads it. A default (nil) EntityID means nothing
// is selected.
class SelectionModel
{
public:
    mosaic::foundation::EntityID selected() const { return selected_; }
    bool has_selection() const { return static_cast<bool>(selected_); }
    bool is_selected(mosaic::foundation::EntityID id) const { return selected_ == id; }
    void select(mosaic::foundation::EntityID id) { selected_ = id; }
    void clear() { selected_ = mosaic::foundation::EntityID{}; }

private:
    mosaic::foundation::EntityID selected_{};
};

} // namespace mosaic::studio
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build --preset debug && build/debug/tests/mosaic_unit_tests "[studio]"`
Expected: PASS (new selection cases + the existing `StudioLayout` cases).

- [ ] **Step 5: Commit**

```bash
git add studio/selection.hpp tests/unit/studio/selection_test.cpp tests/CMakeLists.txt
git commit -m "$(printf 'studio 37b: SelectionModel (#37)\n\nCo-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>')"
```

---

### Task 2: `build_hierarchy(World)` + studio→engine dependency

**Files:**
- Create: `studio/hierarchy.hpp`
- Modify: `studio/CMakeLists.txt` (link `mosaic_engine` PUBLIC)
- Test: `tests/unit/studio/hierarchy_test.cpp`
- Modify: `tests/CMakeLists.txt` (add the test source)

**Interfaces:**
- Consumes: `mosaic::engine::World` (`create_entity`, `for_each_entity`).
- Produces: `mosaic::studio::HierarchyRow { EntityID id; std::string label; }` and `std::vector<HierarchyRow> mosaic::studio::build_hierarchy(const mosaic::engine::World&)` — one row per live entity, in `for_each_entity` order, `label == "Entity <index>"`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/studio/hierarchy_test.cpp`:

```cpp
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/world/world.hpp"
#include "studio/hierarchy.hpp"

using mosaic::engine::World;
using mosaic::studio::build_hierarchy;
using mosaic::studio::HierarchyRow;

TEST_CASE("build_hierarchy on an empty world yields no rows", "[studio]")
{
    World world;
    REQUIRE(build_hierarchy(world).empty());
}

TEST_CASE("build_hierarchy yields one labelled row per entity", "[studio]")
{
    World world;
    const auto a = world.create_entity();
    const auto b = world.create_entity();
    const auto c = world.create_entity();

    const std::vector<HierarchyRow> rows = build_hierarchy(world);
    REQUIRE(rows.size() == 3);

    // Every created entity appears exactly once, labelled "Entity <index>".
    bool saw_a = false, saw_b = false, saw_c = false;
    for (const HierarchyRow& row : rows)
    {
        REQUIRE(row.label == "Entity " + std::to_string(row.id.index()));
        if (row.id == a) { saw_a = true; }
        if (row.id == b) { saw_b = true; }
        if (row.id == c) { saw_c = true; }
    }
    REQUIRE(saw_a);
    REQUIRE(saw_b);
    REQUIRE(saw_c);
}
```

Register it: in `tests/CMakeLists.txt`, add `unit/studio/hierarchy_test.cpp` to the `mosaic_unit_tests` source list.

- [ ] **Step 2: Run tests to verify they fail**

Run: `build/debug/tests/mosaic_unit_tests "[studio]"`
Expected: FAIL — `studio/hierarchy.hpp` does not exist.

- [ ] **Step 3: Create the header**

Create `studio/hierarchy.hpp`:

```cpp
#pragma once

#include <string>
#include <vector>

#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"

namespace mosaic::studio
{

// One row in the Hierarchy panel: the entity and its display label.
struct HierarchyRow
{
    mosaic::foundation::EntityID id;
    std::string                  label; // "Entity <index>"
};

// Flat list of every live entity in the world, in iteration order. Separates entity
// enumeration (unit-tested here) from the ImGui rendering that consumes it.
inline std::vector<HierarchyRow> build_hierarchy(const mosaic::engine::World& world)
{
    std::vector<HierarchyRow> rows;
    world.for_each_entity(
        [&](mosaic::foundation::EntityID id)
        {
            rows.push_back(HierarchyRow{id, "Entity " + std::to_string(id.index())});
        });
    return rows;
}

} // namespace mosaic::studio
```

- [ ] **Step 4: Link `mosaic_engine` into the studio tier**

In `studio/CMakeLists.txt`, change the link line so the studio lib can see `mosaic_engine` (World is in its public headers) while keeping ImGui private:

```cmake
target_link_libraries(mosaic_studio PUBLIC mosaic_engine)
target_link_libraries(mosaic_studio PRIVATE mosaic_imgui)
```

(Replace the single existing `target_link_libraries(mosaic_studio PRIVATE mosaic_imgui)` line with these two.)

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --preset debug && cmake --build --preset debug && build/debug/tests/mosaic_unit_tests "[studio]"`
Expected: PASS (empty-world + labelled-rows cases + all prior `[studio]` cases).

- [ ] **Step 6: Commit**

```bash
git add studio/hierarchy.hpp studio/CMakeLists.txt tests/unit/studio/hierarchy_test.cpp tests/CMakeLists.txt
git commit -m "$(printf 'studio 37b: build_hierarchy enumeration + studio->engine dep (#37)\n\nCo-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>')"
```

---

### Task 3: `StudioApp` renders the hierarchy + `run_editor` demo world (+ folded fix e)

**Files:**
- Modify: `studio/studio_app.hpp` (add `SelectionModel`, `draw_ui(const World&)`, private draw helpers, `selection()` accessor)
- Modify: `studio/studio_app.cpp` (render Hierarchy rows + Inspector selection)
- Modify: `app/run/editor.cpp` (demo world; `draw_ui(world)`; wrap `init_ui` in cleanup — fix (e))

**Interfaces:**
- Consumes: `SelectionModel` (Task 1), `build_hierarchy` (Task 2), `mosaic::engine::World`, `mosaic::engine::transform::Transform`.
- Produces: `StudioApp::draw_ui(const mosaic::engine::World&)`, `StudioApp::selection() -> SelectionModel&`.

> Functional deliverable — no unit test (ImGui glue + editor loop). Verified by a clean build and `mosaic editor --duration 1` exit 0. Note the `draw_ui` signature change: `run_editor` must be updated in the same task or the build breaks.

- [ ] **Step 1: Update the `StudioApp` header**

Replace `studio/studio_app.hpp` with:

```cpp
#pragma once

#include "engine/world/world.hpp"
#include "studio/selection.hpp"
#include "studio/studio_layout.hpp"

namespace mosaic::studio
{

// Draws the editor shell each frame: a full-window dockspace, a menu bar (File > Exit),
// the scene Hierarchy (entities from the World, click to select), the Inspector (shows
// the current selection), and empty placeholder panels. ImGui glue only; the caller
// wraps draw_ui() between the device's begin_ui_frame()/end_ui_frame().
class StudioApp
{
public:
    void draw_ui(const mosaic::engine::World& world);

    StudioLayout&   layout() { return layout_; }
    SelectionModel& selection() { return selection_; }
    bool            exit_requested() const { return layout_.exit_requested(); }

private:
    void draw_hierarchy(const mosaic::engine::World& world);
    void draw_inspector();

    StudioLayout   layout_;
    SelectionModel selection_;
};

} // namespace mosaic::studio
```

- [ ] **Step 2: Update the `StudioApp` implementation**

Replace `studio/studio_app.cpp` with:

```cpp
#include "studio/studio_app.hpp"

#include <imgui.h>

#include "studio/hierarchy.hpp"

namespace mosaic::studio
{

void StudioApp::draw_ui(const mosaic::engine::World& world)
{
    // Full-window dockspace host so panels can be docked/tabbed/floated.
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Exit"))
            {
                layout_.request_exit();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    for (const Panel& panel : layout_.panels())
    {
        if (!panel.open)
        {
            continue;
        }
        if (ImGui::Begin(panel.name.c_str()))
        {
            if (panel.name == "Hierarchy")
            {
                draw_hierarchy(world);
            }
            else if (panel.name == "Inspector")
            {
                draw_inspector();
            }
            else
            {
                ImGui::TextUnformatted("(empty)");
            }
        }
        ImGui::End();
    }
}

void StudioApp::draw_hierarchy(const mosaic::engine::World& world)
{
    for (const HierarchyRow& row : build_hierarchy(world))
    {
        if (ImGui::Selectable(row.label.c_str(), selection_.is_selected(row.id)))
        {
            selection_.select(row.id);
        }
    }
}

void StudioApp::draw_inspector()
{
    if (selection_.has_selection())
    {
        ImGui::Text("Selected: Entity %u", selection_.selected().index());
    }
    else
    {
        ImGui::TextUnformatted("(nothing selected)");
    }
}

} // namespace mosaic::studio
```

- [ ] **Step 3: Populate the demo world and pass it to `draw_ui` in `run_editor`**

In `app/run/editor.cpp`, add the transform include next to the other engine includes:

```cpp
#include "engine/transform/transform.hpp"
```

Replace the empty-world line (`engine::World world;      // empty, idle: no systems registered`) with a small populated world:

```cpp
    engine::World world; // idle (no systems); a few entities so the hierarchy is populated
    for (int i = 0; i < 4; ++i)
    {
        const auto e = world.create_entity();
        world.add_component<engine::transform::Transform>(e, engine::transform::Transform{});
    }
```

Update the `on_frame` lambda to capture and pass the world:

```cpp
    deps.on_frame = [window, &device, &studio, &world] {
        glfwPollEvents();
        device->begin_frame();
        device->clear(engine::render::Color{0.10f, 0.10f, 0.12f, 1.0f});
        device->begin_ui_frame();
        studio.draw_ui(world);
        device->end_ui_frame();
        device->end_frame();
    };
```

- [ ] **Step 4: Fold in fix (e) — wrap `init_ui()` in GLFW cleanup**

In `app/run/editor.cpp`, replace the bare `device->init_ui();` line with a guarded version that tears the device down before the window on failure:

```cpp
    try
    {
        device->init_ui();
    }
    catch (const std::exception& e)
    {
        reporter->error(std::string("editor UI init failed: ") + e.what());
        device.reset(); // ImGui/Vulkan torn down before the window
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
```

- [ ] **Step 5: Build, launch, and check no regression**

Run: `cmake --build --preset debug && build/debug/app/mosaic editor --duration 1; echo "exit=$?"`
Expected: builds; the editor opens with a populated Hierarchy; exits after ~1s with `exit=0` and silent stderr.

Run: `build/debug/app/mosaic run --window --duration 1; echo "exit=$?" && ctest --preset debug`
Expected: `run --window` still exits 0; full suite green.

- [ ] **Step 6: Commit**

```bash
git add studio/studio_app.hpp studio/studio_app.cpp app/run/editor.cpp
git commit -m "$(printf 'studio 37b: hierarchy + inspector-selection render; demo world; guard init_ui (#37)\n\nCo-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>')"
```

---

### Task 4: Folded fix (g) — ImGui image count from the swapchain

**Files:**
- Modify: `modules/render_vulkan/vulkan_render_device.cpp` (`init_ui` image-count fields)

**Interfaces:** none (internal backend fix).

> No unit test (GPU/ImGui). Verified by a clean build, a green suite, and `mosaic editor --duration 1` exit 0.

- [ ] **Step 1: Use the actual swapchain image count**

In `modules/render_vulkan/vulkan_render_device.cpp`, in `init_ui()`, replace the two lines:

```cpp
        info.MinImageCount = kFramesInFlight;
        info.ImageCount = kFramesInFlight;
```

with the actual swapchain image count (the swapchain images live in `images_`, populated at device creation, before `init_ui` runs):

```cpp
        info.MinImageCount = static_cast<uint32_t>(images_.size());
        info.ImageCount = static_cast<uint32_t>(images_.size());
```

(`images_.size()` is `caps.minImageCount + 1` — at least 2, satisfying ImGui's `MinImageCount >= 2` requirement.)

- [ ] **Step 2: Build, verify the editor, and run the suite**

Run: `cmake --build --preset debug && build/debug/app/mosaic editor --duration 1; echo "exit=$?" && ctest --preset debug`
Expected: builds; editor exits 0 silent; full suite green.

- [ ] **Step 3: Commit**

```bash
git add modules/render_vulkan/vulkan_render_device.cpp
git commit -m "$(printf 'studio 37b: ImGui image count from the swapchain, not kFramesInFlight (#37)\n\nCo-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>')"
```

---

## Self-Review

**Spec coverage:**
- `SelectionModel` (select/clear/has_selection/is_selected/selected) → Task 1. ✓
- `build_hierarchy(World)` + `HierarchyRow`, flat list, `"Entity <index>"` labels → Task 2. ✓
- studio→engine dependency (PUBLIC engine, PRIVATE imgui) → Task 2. ✓
- `StudioApp` selection member + `draw_ui(const World&)` rendering Hierarchy (Selectable + select) and Inspector selection text → Task 3. ✓
- `run_editor` demo world + `draw_ui(world)` → Task 3. ✓
- Folded fix (e) init_ui GLFW cleanup → Task 3. ✓
- Folded fix (g) ImGui image count from swapchain → Task 4. ✓
- Unit tests (SelectionModel, build_hierarchy) + functional editor launch → Tasks 1, 2, 3. ✓
- Non-goals (nesting, Name component, multi-select, inspector/viewport/console content) — nothing implemented. ✓

**Placeholder scan:** No TBD/TODO; every code step is complete; test bodies concrete.

**Type consistency:** `SelectionModel` API matches between Task 1 (definition + tests) and Task 3 (`is_selected`, `select`, `has_selection`, `selected().index()`). `build_hierarchy`/`HierarchyRow` match between Task 2 (definition + tests) and Task 3 (`row.id`, `row.label`). `draw_ui(const World&)` signature matches between Task 3's header, its `.cpp`, and `run_editor`'s `studio.draw_ui(world)` call. `images_` is the swapchain image vector referenced in Task 4 (consistent with `vulkan_render_device.cpp`'s existing `images_.size()` usage).
