# Inspector Add/Remove Components Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the Inspector add and remove components on the selected entity, generically through reflection — a "+ Add Component" combo (registered types not present) + a per-component Remove button.

**Architecture:** `ComponentReflection` gains `registered_names()` (sorted, from `presence_`) and `add_default(world, e, name)` (a new per-type `adders_` thunk that default-constructs + attaches, guarded against replace). The Inspector's `draw_inspector` loop gains a Remove button per component and an Add-Component combo; both mark the scene dirty. Removal is deferred to after the render loop.

**Tech Stack:** C++20, CMake, Catch2, ImGui. Reuses #18 reflection (`components_on`/`remove_value`/`inspect`), the #37c Inspector.

**Spec:** `docs/superpowers/specs/2026-09-03-inspector-add-remove-component-design.md`

## Global Constraints

- **Reflection-derived (#18):** add/remove are generic thunks — no per-component UI; every registered type is add/removable.
- **Never replace / never throw:** `add_default` returns false if the component is already present or the name is unregistered; `remove_value` (existing) returns false if absent. No exceptions.
- **Tier wall:** `engine/reflection` stays Python-free; the Inspector is Studio ImGui glue.
- **Deferred:** undoable add/remove (no editor `CommandStack` yet) — add/remove are direct, like inspector field edits.
- **Build:** `cmake --build build/debug -j`; **Test:** `ctest --test-dir build/debug`. Catch2 names not starting with `--`. Namespaces: `mosaic::engine::reflection`, `mosaic::engine::physics`, `mosaic::studio`, `mosaic::foundation`.
- **API facts (verified):** `register_component<T>` registers thunks `attachers_`/`presence_`/`inspectors_`/`valuers_`/`setters_`/`removers_` then `savers_[std::move(name)]` (moves `name` LAST). `presence_` is a `std::map<std::string, Presence>` (sorted). Existing public: `components_on(World&, EntityID) -> std::vector<std::string>` (sorted), `remove_value(World&, EntityID, std::string_view) -> bool`, `inspect(...)`, `has(name)`. `World::has_component<T>(e)` / `add_component<T>(e, T{}) -> bool` (add does NOT replace). `draw_inspector(World& world, const ComponentReflection& reflection) -> bool` in `studio/studio_app.cpp`; its `true` return makes the caller `scene_file_.mark_dirty()` (studio_app.cpp:93-95). The reflection test file `tests/unit/reflection/component_value_test.cpp` has `ComponentReflection make_types(TypeRegistry&)` (calls `register_builtin_components`). Registered builtin names include `Transform`, `RigidBody`, `Collider`, `Camera`, `Material`, `MeshRenderer`, `DirectionalLight`, `PointLight`, `SphereCollider`, `CapsuleCollider`, `ScriptComponent`.

---

### Task 1: `ComponentReflection::registered_names()` + `add_default()`

**Files:**
- Modify: `engine/reflection/component_reflection.hpp`
- Modify: `tests/unit/reflection/component_value_test.cpp`

**Interfaces:**
- Produces: `std::vector<std::string> registered_names() const;` and `bool add_default(World&, EntityID, std::string_view) const;` + a per-type `adders_` thunk.

- [ ] **Step 1: Write the failing tests** — add to `tests/unit/reflection/component_value_test.cpp`. Add includes near the top: `#include <algorithm>` and `#include "engine/physics/physics_components.hpp"`. Then add two TEST_CASEs after the last one:
```cpp
TEST_CASE("registered_names lists the builtin component types, sorted", "[reflection]")
{
    TypeRegistry reg; ComponentReflection types = make_types(reg);
    const auto names = types.registered_names();
    REQUIRE(std::is_sorted(names.begin(), names.end()));
    REQUIRE(std::find(names.begin(), names.end(), "Transform") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "RigidBody") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "ScriptComponent") != names.end());
}

TEST_CASE("add_default attaches a default component by name, never replacing", "[reflection]")
{
    TypeRegistry reg; ComponentReflection types = make_types(reg);
    World world;
    auto e = world.create_entity();

    REQUIRE(types.add_default(world, e, "RigidBody"));
    REQUIRE(world.has_component<mosaic::engine::physics::RigidBody>(e));
    REQUIRE_FALSE(types.add_default(world, e, "RigidBody")); // already present
    REQUIRE_FALSE(types.add_default(world, e, "Nope"));      // unregistered

    REQUIRE(types.remove_value(world, e, "RigidBody"));
    REQUIRE_FALSE(world.has_component<mosaic::engine::physics::RigidBody>(e));
}
```

- [ ] **Step 2: Build → confirm it fails**
```bash
cmake --build build/debug -j 2>&1 | tail -5
```
Expected: compile error — `ComponentReflection` has no member `registered_names`/`add_default`.

- [ ] **Step 3: Add the `adders_` thunk + methods + member in `engine/reflection/component_reflection.hpp`**

In `register_component<T>(...)`, immediately AFTER the `removers_[name] = ...;` block and BEFORE the `savers_[std::move(name)] = ...` line (which moves `name`), add:
```cpp
        adders_[name] = [](World& world, EntityID e) -> bool
        {
            if (world.has_component<T>(e)) { return false; } // never replace
            return world.add_component<T>(e, T{});           // default-constructed
        };
```
Add the two public methods (next to `remove_value`):
```cpp
    // All registered component type names, sorted (for an "Add Component" menu).
    std::vector<std::string> registered_names() const
    {
        std::vector<std::string> names;
        names.reserve(presence_.size());
        for (const auto& entry : presence_) { names.push_back(entry.first); }
        return names;
    }

    // Attach a default-constructed component `name` to `e`. False if unregistered or already present.
    bool add_default(World& world, EntityID e, std::string_view name) const
    {
        auto it = adders_.find(std::string(name));
        return it == adders_.end() ? false : it->second(world, e);
    }
```
Add the member map (in `private:`, next to `removers_`):
```cpp
    using Adder = std::function<bool(World&, EntityID)>;
    std::unordered_map<std::string, Adder> adders_;
```
(`<vector>`/`<string>`/`<string_view>` are already included by this header — `components_on` returns a `std::vector<std::string>`.)

- [ ] **Step 4: Build + test**
```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "registered_names lists|add_default attaches" --output-on-failure
```
Expected: both PASS. Then the full suite once (`ctest --test-dir build/debug 2>&1 | tail -3`) — 0 failures.

- [ ] **Step 5: Commit**
```bash
git add engine/reflection/component_reflection.hpp tests/unit/reflection/component_value_test.cpp
git commit -m "feat(reflection): registered_names() + add_default() (generic component add by name)"
```

---

### Task 2: Inspector Add/Remove UI

**Files:**
- Modify: `studio/studio_app.cpp` (`draw_inspector`)

**Interfaces:**
- Consumes: `registered_names()`/`add_default()` (Task 1), `components_on`/`remove_value`/`inspect`.
- Produces: a per-component Remove button + an "+ Add Component" combo in the Inspector.

This task's deliverable is GUI glue; verified by **build + full suite green** (no new tests — the logic is covered by Task 1's headless tests). Visual acceptance (select an entity → add a `RigidBody`, Play, watch it fall; add a `ScriptComponent` and script it) is the user's.

- [ ] **Step 1: Rewrite `draw_inspector` in `studio/studio_app.cpp`**

Add `#include <algorithm>` near the top includes (for `std::find`). Replace the whole `draw_inspector` body:
```cpp
bool StudioApp::draw_inspector(mosaic::engine::World& world,
                               const mosaic::engine::reflection::ComponentReflection& reflection)
{
    if (!selection_.has_selection())
    {
        ImGui::TextUnformatted("(nothing selected)");
        return false;
    }
    const auto entity = selection_.selected();
    ImGuiFieldVisitor visitor(assets_.selected(), assets_.has_selection());
    for (const std::string& name : reflection.components_on(world, entity))
    {
        ImGui::PushID(name.c_str());
        if (ImGui::CollapsingHeader(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            reflection.inspect(world, entity, name, visitor);
        }
        ImGui::PopID();
    }
    return visitor.changed();
}
```
with:
```cpp
bool StudioApp::draw_inspector(mosaic::engine::World& world,
                               const mosaic::engine::reflection::ComponentReflection& reflection)
{
    if (!selection_.has_selection())
    {
        ImGui::TextUnformatted("(nothing selected)");
        return false;
    }
    const auto entity = selection_.selected();
    bool        changed = false;
    std::string to_remove;

    ImGuiFieldVisitor               visitor(assets_.selected(), assets_.has_selection());
    const std::vector<std::string>  present = reflection.components_on(world, entity);
    for (const std::string& name : present)
    {
        ImGui::PushID(name.c_str());
        if (ImGui::CollapsingHeader(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            reflection.inspect(world, entity, name, visitor);
            if (ImGui::SmallButton("Remove")) { to_remove = name; }
        }
        ImGui::PopID();
    }
    if (!to_remove.empty())
    {
        reflection.remove_value(world, entity, to_remove); // applied after the loop (no mid-render mutate)
        changed = true;
    }

    ImGui::Separator();
    if (ImGui::BeginCombo("+ Add Component", "..."))
    {
        for (const std::string& name : reflection.registered_names())
        {
            if (std::find(present.begin(), present.end(), name) != present.end()) { continue; }
            if (ImGui::Selectable(name.c_str()))
            {
                reflection.add_default(world, entity, name);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }

    return visitor.changed() || changed;
}
```

- [ ] **Step 2: Build + full suite + smoke**
```bash
cmake --build build/debug -j && ctest --test-dir build/debug 2>&1 | tail -3
grep -rn "pybind" studio/studio_app.cpp && echo "unexpected" || echo "ok"
./build/debug/app/mosaic run --window 2>err.txt; echo "exit=$?"; head -3 err.txt; rm -f err.txt
```
Expected: build clean, full suite 0 failures. The `mosaic run --window` smoke may time out / need a display — NOT a failure; note what you observed (build + suite are the gate; the editor UI is user-verified).

- [ ] **Step 3: Commit**
```bash
git add studio/studio_app.cpp
git commit -m "feat(studio): Inspector can add/remove components on the selected entity"
```

---

## Notes for the executor

- **`docs/project/next-steps.md`** — after Task 2: (a) record the user's roadmap **priority order** at the top of the "Moving forward" menu — **Editor UX → Scripting thread → More Phase 3 → Physics/engine (headless)**; (b) mark **generic add/remove-component in the Inspector — DONE** (the Editor-UX item), noting it unblocks attaching a `ScriptComponent` to any entity; keep undoable-add/remove deferred. Update the running test count (372 + 2 new = confirm actual). Commit on this branch (rides in the PR).
- **Finishing:** use `superpowers:finishing-a-development-branch`; base branch is `main`.
- The Inspector UI is thin; visual acceptance is the user's. All logic is headless-tested in Task 1.
