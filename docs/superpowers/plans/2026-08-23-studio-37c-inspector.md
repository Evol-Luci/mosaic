# Studio 37c — Reflection-Driven Inspector Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Inspector panel enumerate and edit a selected entity's components through reflection — editing a `Transform`'s fields live — via a mutable `FieldVisitor` (the shared reflected-property API).

**Architecture:** A `FieldVisitor` abstraction + a `reflect()`-bridging `FieldVisitorAdapter` (engine tier, no ImGui) let a visitor read/edit a component's fields in place. `ComponentReflection` gains `components_on` + `inspect` (per-type `present`/`inspect` thunks). The studio's `ImGuiFieldVisitor` renders each field (float → editable `DragFloat`, else read-only). `run_editor` registers builtin components and builds a varied demo world.

**Tech Stack:** C++20, Dear ImGui (docking), Catch2, CMake.

**Design spec:** `docs/superpowers/specs/2026-08-23-studio-37c-inspector-design.md`

## Global Constraints

- **Tier contract:** `FieldVisitor`/`FieldVisitorAdapter` live in `engine/reflection/` and use NO ImGui and NO studio. `engine/`/`foundation/` never depend on `studio/`. `studio/` may depend on `engine/` (already links `mosaic_engine`); ImGui is used only in `studio/*.cpp`, never a studio header.
- **Editable leaf = `float` only** in 37c. `EntityID`/`AssetID`/`Handle` and any other scalar are read-only (`field_readonly`). Nested reflectables (`Vec3`, `Quat`) render via `begin_group`/`end_group`.
- **Language / build:** C++20. `cmake --preset debug` to reconfigure after CMake/source-list changes; `cmake --build --preset debug`; suite `ctest --preset debug`; unit binary `build/debug/tests/mosaic_unit_tests "<tag>"`; app binary `build/debug/app/mosaic`.
- **Tests:** Catch2 under `tests/unit/reflection/`, tag `[reflection]`, registered in `tests/CMakeLists.txt`. `TEST_CASE` names must not start with `--`.
- **Editor is windowed-only:** verify functionally with `mosaic editor --duration 1` (exit 0, silent stderr); GUI can't be screenshotted in the sandbox.
- **Reflection field counts (verified):** `Vec3` reflects 3 floats (x,y,z); `Quat` 4 (x,y,z,w); `Transform` = `position`(Vec3) + `rotation`(Quat) + `scale`(Vec3) + `parent`(EntityID) ⇒ 3 groups, 10 floats, 1 read-only leaf. `Camera` = 3 top-level floats.
- **Commits:** one per task; end messages with `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.

---

### Task 1: `FieldVisitor` + `FieldVisitorAdapter`

**Files:**
- Create: `engine/reflection/field_visitor.hpp`
- Test: `tests/unit/reflection/field_visitor_test.cpp`
- Modify: `tests/CMakeLists.txt` (add the test source)

**Interfaces:**
- Produces: `mosaic::engine::reflection::FieldVisitor` (abstract: `field_float(string_view, float&)`, `field_readonly(string_view, const std::string&)`, `begin_group(string_view)`, `end_group()`) and `FieldVisitorAdapter{FieldVisitor&}` with a templated `member(string_view, M&)` that bridges `reflect()` to it.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/reflection/field_visitor_test.cpp`:

```cpp
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/field_visitor.hpp"
#include "engine/transform/transform.hpp"
#include "foundation/math/vec3.hpp"

using mosaic::engine::reflection::FieldVisitor;
using mosaic::engine::reflection::FieldVisitorAdapter;
using mosaic::engine::transform::Transform;

namespace
{
struct RecordingVisitor : FieldVisitor
{
    std::vector<std::string> events;
    void field_float(std::string_view n, float&) override { events.push_back("float:" + std::string(n)); }
    void field_readonly(std::string_view n, const std::string& d) override
    {
        events.push_back("ro:" + std::string(n) + "=" + d);
    }
    void begin_group(std::string_view n) override { events.push_back("begin:" + std::string(n)); }
    void end_group() override { events.push_back("end"); }
};

struct WritingVisitor : FieldVisitor
{
    float value;
    explicit WritingVisitor(float v) : value(v) {}
    void field_float(std::string_view, float& f) override { f = value; }
    void field_readonly(std::string_view, const std::string&) override {}
    void begin_group(std::string_view) override {}
    void end_group() override {}
};
} // namespace

TEST_CASE("FieldVisitorAdapter visits a Transform as groups, floats, and a readonly parent", "[reflection]")
{
    Transform t;
    RecordingVisitor rec;
    FieldVisitorAdapter adapter{rec};
    reflect(adapter, t);

    REQUIRE(rec.events.front() == "begin:position");

    int floats = 0;
    int groups = 0;
    bool saw_parent_ro = false;
    for (const std::string& e : rec.events)
    {
        if (e.rfind("float:", 0) == 0) { ++floats; }
        if (e.rfind("begin:", 0) == 0) { ++groups; }
        if (e.rfind("ro:parent=", 0) == 0) { saw_parent_ro = true; }
    }
    REQUIRE(groups == 3);  // position, rotation, scale
    REQUIRE(floats == 10); // Vec3(3) + Quat(4) + Vec3(3)
    REQUIRE(saw_parent_ro);
}

TEST_CASE("FieldVisitorAdapter writes float edits into the live object", "[reflection]")
{
    Transform t;
    t.position = mosaic::foundation::math::Vec3{1.0f, 2.0f, 3.0f};
    WritingVisitor w{9.0f};
    FieldVisitorAdapter adapter{w};
    reflect(adapter, t);

    REQUIRE(t.position.x == 9.0f);
    REQUIRE(t.position.y == 9.0f);
    REQUIRE(t.scale.z == 9.0f);
}
```

Register it: in `tests/CMakeLists.txt`, add `unit/reflection/field_visitor_test.cpp` to the `mosaic_unit_tests` source list (next to the other `unit/reflection/...` entries).

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --preset debug && cmake --build --preset debug && build/debug/tests/mosaic_unit_tests "[reflection]"`
Expected: FAIL — `engine/reflection/field_visitor.hpp` does not exist.

- [ ] **Step 3: Create the header**

Create `engine/reflection/field_visitor.hpp`:

```cpp
#pragma once

#include <string>
#include <string_view>
#include <type_traits>

#include "foundation/core/entity_id.hpp"
#include "foundation/reflection/reflect.hpp"
#include "foundation/serialization/reference.hpp" // AssetID, Handle

namespace mosaic::engine::reflection
{

// A visitor over a reflected object's fields. `field_float` gets a mutable reference (the
// one editable leaf); everything else is presented read-only via a formatted string.
// Nested reflectables (Vec3, Quat, ...) are bracketed by begin_group/end_group. No ImGui,
// no studio dependency — this is the shared reflected-property surface.
class FieldVisitor
{
public:
    virtual ~FieldVisitor() = default;
    virtual void field_float(std::string_view name, float& value) = 0;
    virtual void field_readonly(std::string_view name, const std::string& display) = 0;
    virtual void begin_group(std::string_view name) = 0;
    virtual void end_group() = 0;
};

// std::vector<mosaic::foundation::serialization::Handle<R>> detection, kept local so this
// header needs only reference.hpp (not the serialization value machinery).
template <class> inline constexpr bool is_handle_v = false;
template <class R>
inline constexpr bool is_handle_v<mosaic::foundation::serialization::Handle<R>> = true;

// Bridges reflect()'s templated member(name, field) to the virtual FieldVisitor. The
// if constexpr ladder mirrors serialization's encode(); every serialization-supported
// field type maps to a visitor call, so the adapter is total (no compile error on an
// unhandled field — it renders read-only).
struct FieldVisitorAdapter
{
    FieldVisitor& visitor;

    template <class M>
    void member(std::string_view name, M& value)
    {
        namespace ser = mosaic::foundation::serialization;
        using U = std::remove_cv_t<M>;
        if constexpr (std::is_same_v<U, float>)
        {
            visitor.field_float(name, value);
        }
        else if constexpr (std::is_same_v<U, mosaic::foundation::EntityID>)
        {
            visitor.field_readonly(name, std::to_string(value.value()));
        }
        else if constexpr (std::is_same_v<U, ser::AssetID>)
        {
            visitor.field_readonly(name, value.to_string());
        }
        else if constexpr (is_handle_v<U>)
        {
            visitor.field_readonly(name, value.asset_id().to_string());
        }
        else if constexpr (std::is_same_v<U, bool>)
        {
            visitor.field_readonly(name, value ? "true" : "false");
        }
        else if constexpr (std::is_same_v<U, std::string>)
        {
            visitor.field_readonly(name, value);
        }
        else if constexpr (std::is_arithmetic_v<U>)
        {
            visitor.field_readonly(name, std::to_string(value));
        }
        else if constexpr (mosaic::foundation::reflection::Reflectable<U>)
        {
            visitor.begin_group(name);
            reflect(*this, value);
            visitor.end_group();
        }
        else
        {
            visitor.field_readonly(name, "<unsupported>");
        }
    }
};

} // namespace mosaic::engine::reflection
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build --preset debug && build/debug/tests/mosaic_unit_tests "[reflection]"`
Expected: PASS (2 new cases + all pre-existing `[reflection]` cases).

- [ ] **Step 5: Commit**

```bash
git add engine/reflection/field_visitor.hpp tests/unit/reflection/field_visitor_test.cpp tests/CMakeLists.txt
git commit -m "$(printf 'studio 37c: FieldVisitor + reflect() adapter (#37)\n\nCo-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>')"
```

---

### Task 2: `ComponentReflection` — `components_on` + `inspect`

**Files:**
- Modify: `engine/reflection/component_reflection.hpp` (per-type `present`/`inspect` thunks; `components_on`, `inspect`)
- Test: `tests/unit/reflection/component_inspect_test.cpp`
- Modify: `tests/CMakeLists.txt` (add the test source)

**Interfaces:**
- Consumes: `FieldVisitor`/`FieldVisitorAdapter` (Task 1); `World::has_component<T>`, `World::get_component<T>` (mutable `T*`).
- Produces: `ComponentReflection::components_on(World&, EntityID) -> std::vector<std::string>` (sorted registered component names the entity holds) and `ComponentReflection::inspect(World&, EntityID, std::string_view name, FieldVisitor&)` (runs the visitor over the live component; no-op if unregistered/absent).

- [ ] **Step 1: Write the failing test**

Create `tests/unit/reflection/component_inspect_test.cpp`:

```cpp
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/reflection/field_visitor.hpp"
#include "engine/render/camera.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::FieldVisitor;
using mosaic::engine::reflection::register_builtin_components;

namespace
{
struct WritingVisitor : FieldVisitor
{
    float value;
    explicit WritingVisitor(float v) : value(v) {}
    void field_float(std::string_view, float& f) override { f = value; }
    void field_readonly(std::string_view, const std::string&) override {}
    void begin_group(std::string_view) override {}
    void end_group() override {}
};

struct Fixture
{
    mosaic::foundation::reflection::TypeRegistry registry;
    ComponentReflection                          reflection;
    World                                         world;
    Fixture() { register_builtin_components(registry, reflection); }
};
} // namespace

TEST_CASE("components_on lists an entity's registered components, sorted", "[reflection]")
{
    Fixture f;
    const auto e = f.world.create_entity();
    f.world.add_component<mosaic::engine::transform::Transform>(e, {});
    f.world.add_component<mosaic::engine::render::Camera>(e, {});

    const std::vector<std::string> names = f.reflection.components_on(f.world, e);
    REQUIRE(names == std::vector<std::string>{"Camera", "Transform"});

    const auto bare = f.world.create_entity();
    f.world.add_component<mosaic::engine::transform::Transform>(bare, {});
    REQUIRE(f.reflection.components_on(f.world, bare) == std::vector<std::string>{"Transform"});
}

TEST_CASE("inspect edits the live component through the visitor", "[reflection]")
{
    Fixture f;
    const auto e = f.world.create_entity();
    f.world.add_component<mosaic::engine::transform::Transform>(e, {});

    WritingVisitor w{7.0f};
    f.reflection.inspect(f.world, e, "Transform", w);

    const auto* t = f.world.get_component<mosaic::engine::transform::Transform>(e);
    REQUIRE(t != nullptr);
    REQUIRE(t->position.x == 7.0f);
    REQUIRE(t->scale.y == 7.0f);
}

TEST_CASE("inspect is a no-op for an unregistered name", "[reflection]")
{
    Fixture f;
    const auto e = f.world.create_entity();
    WritingVisitor w{1.0f};
    f.reflection.inspect(f.world, e, "NotAComponent", w); // must not throw
    REQUIRE(f.reflection.components_on(f.world, e).empty());
}
```

Register it: in `tests/CMakeLists.txt`, add `unit/reflection/component_inspect_test.cpp` to the `mosaic_unit_tests` source list.

- [ ] **Step 2: Run tests to verify they fail**

Run: `build/debug/tests/mosaic_unit_tests "[reflection]"`
Expected: FAIL — `components_on`/`inspect` do not exist.

- [ ] **Step 3: Add the include and register the new thunks**

In `engine/reflection/component_reflection.hpp`, add the include after the existing includes:

```cpp
#include "engine/reflection/field_visitor.hpp"
```

Inside `register_component<T>`, add the `present` and `inspect` thunks. The existing body sets `attachers_[name]` then `savers_[std::move(name)]`; insert the two new registrations **before** the `savers_[std::move(name)]` line (which consumes `name`):

```cpp
        presence_[name] = [](World& world, EntityID e) { return world.has_component<T>(e); };
        inspectors_[name] = [](World& world, EntityID e, FieldVisitor& visitor)
        {
            if (T* component = world.get_component<T>(e))
            {
                FieldVisitorAdapter adapter{visitor};
                reflect(adapter, *component);
            }
        };
```

- [ ] **Step 4: Add the two query methods**

In the same class, add after the `construct(...)` method:

```cpp
    // Registered component type names the entity holds, in sorted order (presence_ is a map).
    std::vector<std::string> components_on(World& world, EntityID e) const
    {
        std::vector<std::string> names;
        for (const auto& [name, present] : presence_)
        {
            if (present(world, e))
            {
                names.push_back(name);
            }
        }
        return names;
    }

    // Run `visitor` over the live component `name` on entity `e` (no-op if unregistered
    // or the entity lacks it).
    void inspect(World& world, EntityID e, std::string_view name, FieldVisitor& visitor) const
    {
        auto it = inspectors_.find(std::string(name));
        if (it != inspectors_.end())
        {
            it->second(world, e, visitor);
        }
    }
```

Add `#include <vector>` to the header's includes if not already present.

- [ ] **Step 5: Add the member maps**

In the class's `private:` section, next to `attachers_`/`savers_`, add:

```cpp
    using Presence = std::function<bool(World&, EntityID)>;
    // std::map so components_on returns sorted (deterministic) names.
    std::map<std::string, Presence> presence_;

    using Inspector = std::function<void(World&, EntityID, FieldVisitor&)>;
    std::unordered_map<std::string, Inspector> inspectors_;
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build --preset debug && build/debug/tests/mosaic_unit_tests "[reflection]"`
Expected: PASS (components_on sorted, inspect-edits-live, unregistered no-op, plus all prior `[reflection]` cases).

- [ ] **Step 7: Commit**

```bash
git add engine/reflection/component_reflection.hpp tests/unit/reflection/component_inspect_test.cpp tests/CMakeLists.txt
git commit -m "$(printf 'studio 37c: ComponentReflection components_on + inspect (#37)\n\nCo-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>')"
```

---

### Task 3: Studio inspector + `draw_ui` wiring + `run_editor` reflection setup

**Files:**
- Create: `studio/inspector.hpp`
- Create: `studio/inspector.cpp`
- Modify: `studio/CMakeLists.txt` (add `inspector.cpp`)
- Modify: `studio/studio_app.hpp` (draw_ui + draw_inspector signatures; include component_reflection)
- Modify: `studio/studio_app.cpp` (draw_inspector renders components via reflection)
- Modify: `app/run/editor.cpp` (registry + reflection + varied demo world; pass reflection to draw_ui)

**Interfaces:**
- Consumes: `ComponentReflection::components_on`/`inspect` (Task 2), `FieldVisitor` (Task 1), `register_builtin_components`, `Transform`/`Camera`/`Material`/`PointLight`.
- Produces: `mosaic::studio::ImGuiFieldVisitor`; `StudioApp::draw_ui(mosaic::engine::World&, const mosaic::engine::reflection::ComponentReflection&)`.

> Functional deliverable — no unit test (ImGui glue + editor loop). The `draw_ui` signature changes, so `studio_app.*` and `editor.cpp` must land together. Verified by a clean build and `mosaic editor --duration 1` exit 0.

- [ ] **Step 1: Create the ImGui field visitor**

Create `studio/inspector.hpp`:

```cpp
#pragma once

#include "engine/reflection/field_visitor.hpp"

namespace mosaic::studio
{

// Renders each visited field as an ImGui widget: floats are editable (DragFloat, edits the
// live field in place), everything else is a read-only label. Nested groups render as an
// indented, ID-scoped block so same-named leaves (e.g. position.x vs scale.x) don't collide.
class ImGuiFieldVisitor : public mosaic::engine::reflection::FieldVisitor
{
public:
    void field_float(std::string_view name, float& value) override;
    void field_readonly(std::string_view name, const std::string& display) override;
    void begin_group(std::string_view name) override;
    void end_group() override;
};

} // namespace mosaic::studio
```

Create `studio/inspector.cpp`:

```cpp
#include "studio/inspector.hpp"

#include <string>

#include <imgui.h>

namespace mosaic::studio
{

void ImGuiFieldVisitor::field_float(std::string_view name, float& value)
{
    ImGui::DragFloat(std::string(name).c_str(), &value, 0.05f);
}

void ImGuiFieldVisitor::field_readonly(std::string_view name, const std::string& display)
{
    ImGui::LabelText(std::string(name).c_str(), "%s", display.c_str());
}

void ImGuiFieldVisitor::begin_group(std::string_view name)
{
    const std::string label(name);
    ImGui::PushID(label.c_str());
    ImGui::TextUnformatted(label.c_str());
    ImGui::Indent();
}

void ImGuiFieldVisitor::end_group()
{
    ImGui::Unindent();
    ImGui::PopID();
}

} // namespace mosaic::studio
```

Add `inspector.cpp` to `studio/CMakeLists.txt`'s `mosaic_studio` source list (next to `studio_app.cpp`).

- [ ] **Step 2: Update the `StudioApp` header**

In `studio/studio_app.hpp`, add the include after `engine/world/world.hpp`:

```cpp
#include "engine/reflection/component_reflection.hpp"
```

Change `draw_ui`'s declaration and `draw_inspector`'s declaration:

```cpp
    void draw_ui(mosaic::engine::World& world,
                 const mosaic::engine::reflection::ComponentReflection& reflection);
```

```cpp
    void draw_inspector(mosaic::engine::World& world,
                        const mosaic::engine::reflection::ComponentReflection& reflection);
```

(Leave `draw_hierarchy(const mosaic::engine::World&)` as-is.)

- [ ] **Step 3: Update the `StudioApp` implementation**

In `studio/studio_app.cpp`, add the inspector include after `#include "studio/hierarchy.hpp"`:

```cpp
#include "studio/inspector.hpp"
```

Change `draw_ui`'s signature to match the header and pass `world`/`reflection` to the inspector. Replace the current `draw_ui(const mosaic::engine::World& world)` line and the Inspector dispatch branch:

```cpp
void StudioApp::draw_ui(mosaic::engine::World& world,
                        const mosaic::engine::reflection::ComponentReflection& reflection)
```

and in the panel loop change the Inspector branch from `draw_inspector();` to:

```cpp
            else if (panel.name == "Inspector")
            {
                draw_inspector(world, reflection);
            }
```

Replace the whole `draw_inspector()` definition with:

```cpp
void StudioApp::draw_inspector(mosaic::engine::World& world,
                               const mosaic::engine::reflection::ComponentReflection& reflection)
{
    if (!selection_.has_selection())
    {
        ImGui::TextUnformatted("(nothing selected)");
        return;
    }
    const auto entity = selection_.selected();
    ImGuiFieldVisitor visitor;
    for (const std::string& name : reflection.components_on(world, entity))
    {
        ImGui::PushID(name.c_str());
        if (ImGui::CollapsingHeader(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            reflection.inspect(world, entity, name, visitor);
        }
        ImGui::PopID();
    }
}
```

(`draw_hierarchy` still takes `const World&`; calling it with the mutable `world` is fine.)

- [ ] **Step 4: Set up reflection + a varied demo world in `run_editor`**

In `app/run/editor.cpp`, add these includes with the other engine includes:

```cpp
#include "engine/reflection/builtin_components.hpp"
#include "engine/render/camera.hpp"
#include "engine/render/material.hpp"
#include "engine/render/point_light.hpp"
#include "foundation/reflection/type_registry.hpp"
```

Replace the current demo-world block (the `engine::World world; ...` loop that adds a `Transform` to 4 entities) with reflection setup + a varied world:

```cpp
    mosaic::foundation::reflection::TypeRegistry     registry;
    mosaic::engine::reflection::ComponentReflection  reflection;
    mosaic::engine::reflection::register_builtin_components(registry, reflection);

    engine::World world; // idle (no systems); varied components so the inspector shows several types
    {
        const auto e0 = world.create_entity();
        world.add_component<engine::transform::Transform>(e0, engine::transform::Transform{});
        world.add_component<engine::render::Camera>(e0, engine::render::Camera{});

        const auto e1 = world.create_entity();
        world.add_component<engine::transform::Transform>(e1, engine::transform::Transform{});
        world.add_component<engine::render::Material>(e1, engine::render::Material{});

        const auto e2 = world.create_entity();
        world.add_component<engine::transform::Transform>(e2, engine::transform::Transform{});
        world.add_component<engine::render::PointLight>(e2, engine::render::PointLight{});

        const auto e3 = world.create_entity();
        world.add_component<engine::transform::Transform>(e3, engine::transform::Transform{});
    }
```

Update the `on_frame` lambda to capture `&reflection` and pass it (find the lambda that calls `studio.draw_ui(world)`):

```cpp
    deps.on_frame = [window, &device, &studio, &world, &reflection] {
        glfwPollEvents();
        device->begin_frame();
        device->clear(engine::render::Color{0.10f, 0.10f, 0.12f, 1.0f});
        device->begin_ui_frame();
        studio.draw_ui(world, reflection);
        device->end_ui_frame();
        device->end_frame();
    };
```

(`registry`/`reflection` are declared before `deps`, so capturing `&reflection` by reference is safe — both outlive `pump.run()`.)

- [ ] **Step 5: Build, launch, and check no regression**

Run: `cmake --preset debug && cmake --build --preset debug && build/debug/app/mosaic editor --duration 1; echo "exit=$?"`
Expected: builds; the editor opens; selecting an entity shows its components with editable float fields; exits after ~1s with `exit=0` and silent stderr.

Run: `build/debug/app/mosaic run --window --duration 1; echo "exit=$?" && ctest --preset debug`
Expected: `run --window` still exits 0; full suite green.

- [ ] **Step 6: Commit**

```bash
git add studio/inspector.hpp studio/inspector.cpp studio/CMakeLists.txt studio/studio_app.hpp studio/studio_app.cpp app/run/editor.cpp
git commit -m "$(printf 'studio 37c: ImGui reflection inspector + demo world reflection setup (#37)\n\nCo-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>')"
```

---

## Self-Review

**Spec coverage:**
- `FieldVisitor` (float editable, readonly, group begin/end) + `FieldVisitorAdapter` reflect() bridge with the encode-mirroring ladder → Task 1. ✓
- `ComponentReflection` present/inspect thunks + `components_on` (sorted) + `inspect` (live edit, no-op on unknown) → Task 2. ✓
- Studio `ImGuiFieldVisitor` (DragFloat editable, LabelText readonly, ID-scoped indented groups) + `draw_inspector` via reflection + `draw_ui(World&, const ComponentReflection&)` → Task 3. ✓
- `run_editor` registry + reflection + varied demo world + pass reflection → Task 3. ✓
- Tier: FieldVisitor/adapter in engine/reflection with no ImGui; ImGui only in studio `.cpp` → Tasks 1, 3. ✓
- Tests: adapter read + live edit (Task 1), components_on + inspect-live + unknown no-op (Task 2), functional editor launch (Task 3). ✓
- Non-goals (bool/int/string/handle editing, euler widget, add/remove, multi-select, undo, viewport, play/stop) — nothing implemented. ✓

**Placeholder scan:** No TBD/TODO; every code step is complete; test bodies concrete.

**Type consistency:** `FieldVisitor` method names (`field_float`, `field_readonly`, `begin_group`, `end_group`) match across Task 1 (definition + test visitors), Task 2 (thunk uses `FieldVisitorAdapter`, test `WritingVisitor` overrides them), and Task 3 (`ImGuiFieldVisitor` overrides them). `components_on`/`inspect` signatures match between Task 2 (definition + tests) and Task 3 (`draw_inspector` calls). `draw_ui(World&, const ComponentReflection&)` matches between Task 3's header, `.cpp`, and the `run_editor` call site. `FieldVisitorAdapter{visitor}` aggregate init matches its definition in Task 1.
