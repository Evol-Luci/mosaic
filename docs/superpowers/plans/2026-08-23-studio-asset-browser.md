# Studio Asset Browser Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An Assets panel that lists registered assets and assigns a selected asset to a component's `AssetID` field (e.g. swap the cube's `Material.texture` live in the viewport).

**Architecture:** Small engine additions — `AssetRegistry` enumeration and a mutable `FieldVisitor::field_asset` on the reflected-property surface — plus a headless `AssetBrowserModel` and a studio Assets panel that feeds the inspector's new "Assign" affordance. Assignment goes through reflection (the shared human/agent path), and `TextureCache` already resolves `Material.texture` each frame, so reassigning the id updates the viewport with no new render plumbing.

**Tech Stack:** C++20, Dear ImGui (docking), `engine/assets` (#20), `engine/reflection` (#18), `engine/render` (`TextureCache`), Catch2, CMake.

## Global Constraints

- **C++20**, header-only where the surrounding studio tier is (`viewport.hpp`, `play_state.hpp`, `console.hpp` are header-only).
- **Tier contract** ([ARCHITECTURE.md](../../architecture/ARCHITECTURE.md)): `AssetBrowserModel`, the Assets panel, and the ImGui visitor live in `studio/`; `engine/` gains only neutral, ImGui-free surface (`AssetRegistry::for_each`, `FieldVisitor::field_asset`); `foundation/`/`engine/` never depend on `studio/` or ImGui.
- **Reflected-property surface is the one editing path** — assignment goes through `FieldVisitor::field_asset`, not a `Material`-specific editor.
- **`field_asset` is non-pure with a read-only default** (`field_readonly(name, value.to_string())`) so every existing `FieldVisitor` implementer keeps compiling; only `ImGuiFieldVisitor` overrides it.
- **Assignment is unrestricted** (any `AssetID`) for this slice; a type mismatch degrades to the default white texture via `TextureCache`. Type-aware filtering is deferred.
- **No new third-party dependencies.** Catch2 test names must not start with `--`.
- **GUI verification is functional only** (sandbox can't screenshot): `mosaic editor --ticks N` launches, exits 0, and — with `VK_LAYER_KHRONOS_validation` installed — emits **0 validation errors**. The visual "texture changes on assign" check is the maintainer's.
- **Build/test commands** (binaryDir `build/debug`, exe at `build/debug/app/mosaic`):
  - Build: `cmake --build build/debug -j`
  - Test: `ctest --test-dir build/debug --output-on-failure`
  - Validation run: `./build/debug/app/mosaic editor --ticks 180 2>&1 | grep -c ERROR` (expect 0)

## Key facts (verified against the codebase)

- `mosaic::foundation::serialization::AssetID`: `AssetID()` is nil (all-zero bytes), `to_string()`, `is_nil()`, `operator==`, `bytes()`, and `std::hash<AssetID>` all exist.
- `AssetRegistry::entries_` is `std::unordered_map<AssetID, Entry>`; `Entry` holds `AssetMetadata metadata`.
- `AssetMetadata` aggregate: `{ AssetID uuid; std::string type; std::uint32_t version=0; std::string source; std::vector<AssetID> dependencies; ... }` — `AssetMetadata{id, "texture"}` is valid.
- `FieldVisitorAdapter::member` routes `AssetID` to `visitor.field_readonly(name, value.to_string())` today (change to `field_asset`).
- `StudioApp::draw_ui(RenderDevice&, World&, ComponentReflection&)`; called at `app/run/editor.cpp:156` as `studio.draw_ui(*device, world, reflection)`.
- `ComponentReflection::inspect(World&, EntityID, string_view, FieldVisitor&)` takes the visitor by reference.
- `studio_layout.cpp` panels: `{"Hierarchy",true},{"Viewport",true},{"Inspector",true},{"Console",true}`; `studio_layout_test.cpp` asserts `panels().size() == 4`.

---

## File Structure

- `engine/assets/asset_registry.hpp` — **modify.** Add `for_each(fn)` + `size()`.
- `engine/reflection/field_visitor.hpp` — **modify.** Add virtual `field_asset` (default read-only) + route the `AssetID` adapter branch to it.
- `studio/asset_browser.hpp` — **create.** Header-only `AssetEntry` + `AssetBrowserModel` (list + selection).
- `studio/inspector.{hpp,cpp}` — **modify.** `ImGuiFieldVisitor` gains a selection-carrying constructor + a `field_asset` override (current id + Assign button).
- `studio/studio_app.{hpp,cpp}` — **modify.** Own an `AssetBrowserModel`; Assets panel body; `draw_ui` gains an `AssetRegistry&`; inspector constructs the visitor with the selection.
- `studio/studio_layout.cpp` — **modify.** Register the `"Assets"` panel.
- `app/run/demo_scene.hpp` — **modify.** Register three procedural textures.
- `app/run/editor.cpp` — **modify.** Pass `asset_registry` to `draw_ui`.
- `tests/unit/assets/asset_enumeration_test.cpp` — **create.**
- `tests/unit/reflection/field_asset_test.cpp` — **create.**
- `tests/unit/studio/asset_browser_test.cpp` — **create.**
- `tests/unit/studio/studio_layout_test.cpp` — **modify** (4 → 5 panels).
- `tests/CMakeLists.txt` — **modify.** Register the three new test files.

---

## Task 1: AssetRegistry enumeration

**Files:**
- Modify: `engine/assets/asset_registry.hpp`
- Create: `tests/unit/assets/asset_enumeration_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `template <class F> void AssetRegistry::for_each(F&& fn) const` — calls `fn(const AssetMetadata&)` for every registered asset; `std::size_t AssetRegistry::size() const`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/assets/asset_enumeration_test.cpp`:

```cpp
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/assets/asset_registry.hpp"

using mosaic::engine::assets::AssetMetadata;
using mosaic::engine::assets::AssetRegistry;
using mosaic::foundation::serialization::AssetID;

namespace
{
AssetID make_id(std::uint8_t tag)
{
    std::array<std::uint8_t, 16> b{};
    b[0] = tag;
    return AssetID{b};
}
} // namespace

TEST_CASE("AssetRegistry enumerates registered assets", "[assets]")
{
    AssetRegistry reg;
    REQUIRE(reg.size() == 0);

    reg.register_asset(AssetMetadata{make_id(1), "texture"}, [] { return nullptr; });
    reg.register_asset(AssetMetadata{make_id(2), "mesh"}, [] { return nullptr; });
    REQUIRE(reg.size() == 2);

    std::vector<std::string> types;
    reg.for_each([&](const AssetMetadata& m) { types.push_back(m.type); });
    REQUIRE(types.size() == 2);
    REQUIRE(std::find(types.begin(), types.end(), "texture") != types.end());
    REQUIRE(std::find(types.begin(), types.end(), "mesh") != types.end());
}
```

Register it in `tests/CMakeLists.txt` next to the other `unit/assets/...` entries (search for an existing `unit/assets/` line; add this beneath it):

```cmake
  unit/assets/asset_enumeration_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build/debug -j 2>&1 | tail -20`
Expected: compile error — `AssetRegistry` has no member `size` / `for_each`.

- [ ] **Step 3: Add enumeration to the registry**

In `engine/assets/asset_registry.hpp`, add these public methods (place them after `metadata(...)`, before the `private:` section):

```cpp
    // Invoke fn(const AssetMetadata&) for every registered asset, in unspecified order.
    template <class F>
    void for_each(F&& fn) const
    {
        for (const auto& [id, entry] : entries_)
        {
            fn(entry.metadata);
        }
    }

    std::size_t size() const { return entries_.size(); }
```

Ensure `#include <cstddef>` is present at the top (for `std::size_t`); add it if missing.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build/debug -j && ctest --test-dir build/debug --output-on-failure -R "AssetRegistry enumerates"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/assets/asset_registry.hpp tests/unit/assets/asset_enumeration_test.cpp tests/CMakeLists.txt
git commit -m "assets: AssetRegistry::for_each + size enumeration (#37)"
```

---

## Task 2: Mutable `field_asset` on the reflected-property surface

**Files:**
- Modify: `engine/reflection/field_visitor.hpp`
- Create: `tests/unit/reflection/field_asset_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `virtual void FieldVisitor::field_asset(std::string_view name, mosaic::foundation::serialization::AssetID& value)` (non-pure; default calls `field_readonly(name, value.to_string())`); `FieldVisitorAdapter` routes `AssetID` fields to it.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/reflection/field_asset_test.cpp`:

```cpp
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/field_visitor.hpp"
#include "engine/render/material.hpp"

using mosaic::engine::reflection::FieldVisitor;
using mosaic::engine::reflection::FieldVisitorAdapter;
using mosaic::engine::render::Material;
using mosaic::foundation::serialization::AssetID;

namespace
{
AssetID make_id(std::uint8_t tag)
{
    std::array<std::uint8_t, 16> b{};
    b[0] = tag;
    return AssetID{b};
}

// Reassigns any asset field to new_id and records that it saw one.
class AssignVisitor : public FieldVisitor
{
public:
    AssetID new_id;
    bool    saw_asset = false;

    void field_float(std::string_view, float&) override {}
    void field_readonly(std::string_view, const std::string&) override {}
    void begin_group(std::string_view) override {}
    void end_group() override {}
    void field_asset(std::string_view, AssetID& value) override
    {
        saw_asset = true;
        value = new_id;
    }
};

// Does NOT override field_asset, so the base-class default (read-only) applies.
class ReadonlyCapture : public FieldVisitor
{
public:
    std::vector<std::string> readonly_names;

    void field_float(std::string_view, float&) override {}
    void field_readonly(std::string_view name, const std::string&) override
    {
        readonly_names.emplace_back(name);
    }
    void begin_group(std::string_view) override {}
    void end_group() override {}
};
} // namespace

TEST_CASE("field_asset lets a visitor reassign an AssetID field", "[reflection][assets]")
{
    Material m; // texture starts nil
    REQUIRE(m.texture.is_nil());

    AssignVisitor visitor;
    visitor.new_id = make_id(9);
    FieldVisitorAdapter adapter{visitor};
    reflect(adapter, m);

    REQUIRE(visitor.saw_asset);
    REQUIRE(m.texture == make_id(9));
}

TEST_CASE("default field_asset falls back to read-only", "[reflection][assets]")
{
    Material m;
    ReadonlyCapture visitor;
    FieldVisitorAdapter adapter{visitor};
    reflect(adapter, m);

    // The AssetID field 'texture' is presented read-only by the default field_asset.
    REQUIRE(std::find(visitor.readonly_names.begin(), visitor.readonly_names.end(), "texture") !=
            visitor.readonly_names.end());
}
```

Add `#include <algorithm>` at the top of the test (for `std::find`).

Register it in `tests/CMakeLists.txt` next to the other `unit/reflection/...` entries:

```cmake
  unit/reflection/field_asset_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build/debug -j 2>&1 | tail -20`
Expected: compile error — `AssignVisitor::field_asset` marked `override` but `FieldVisitor` has no such virtual (and the adapter still routes `AssetID` to `field_readonly`, so the first test's `saw_asset` would be false even if it compiled).

- [ ] **Step 3: Add `field_asset` and route the adapter**

In `engine/reflection/field_visitor.hpp`, add the virtual to `FieldVisitor` (after `field_readonly`):

```cpp
    // A reference-to-asset field. The visitor may reassign it (e.g. from an asset
    // browser). Default falls back to the read-only display so existing visitors keep
    // compiling.
    virtual void field_asset(std::string_view name,
                             mosaic::foundation::serialization::AssetID& value)
    {
        field_readonly(name, value.to_string());
    }
```

Then in `FieldVisitorAdapter::member`, change the `AssetID` branch from:

```cpp
        else if constexpr (std::is_same_v<U, ser::AssetID>)
        {
            visitor.field_readonly(name, value.to_string());
        }
```

to:

```cpp
        else if constexpr (std::is_same_v<U, ser::AssetID>)
        {
            visitor.field_asset(name, value);
        }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build/debug -j && ctest --test-dir build/debug --output-on-failure -R "field_asset|default field_asset"`
Expected: PASS (2 cases).

- [ ] **Step 5: Run the full suite (no 37c regression)**

Run: `ctest --test-dir build/debug --output-on-failure`
Expected: all green — the default `field_asset` keeps every non-overriding visitor rendering `AssetID` read-only exactly as before.

- [ ] **Step 6: Commit**

```bash
git add engine/reflection/field_visitor.hpp tests/unit/reflection/field_asset_test.cpp tests/CMakeLists.txt
git commit -m "reflection: mutable field_asset for AssetID fields (#37)"
```

---

## Task 3: AssetBrowserModel

**Files:**
- Create: `studio/asset_browser.hpp`
- Create: `tests/unit/studio/asset_browser_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `AssetRegistry::for_each` (Task 1); `AssetID` (`to_string`, `operator==`, nil default).
- Produces: `mosaic::studio::AssetEntry { AssetID id; std::string type; std::string source; }`; `mosaic::studio::AssetBrowserModel` with `void refresh(const AssetRegistry&)`, `const std::vector<AssetEntry>& entries() const`, `void select(AssetID)`, `bool has_selection() const`, `AssetID selected() const`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/studio/asset_browser_test.cpp`:

```cpp
#include <array>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "engine/assets/asset_registry.hpp"
#include "studio/asset_browser.hpp"

using mosaic::engine::assets::AssetMetadata;
using mosaic::engine::assets::AssetRegistry;
using mosaic::foundation::serialization::AssetID;
using mosaic::studio::AssetBrowserModel;

namespace
{
AssetID make_id(std::uint8_t tag)
{
    std::array<std::uint8_t, 16> b{};
    b[0] = tag;
    return AssetID{b};
}
} // namespace

TEST_CASE("AssetBrowserModel refreshes a sorted list from the registry", "[studio][assets]")
{
    AssetRegistry reg;
    reg.register_asset(AssetMetadata{make_id(2), "b"}, [] { return nullptr; });
    reg.register_asset(AssetMetadata{make_id(1), "a"}, [] { return nullptr; });

    AssetBrowserModel model;
    model.refresh(reg);

    REQUIRE(model.entries().size() == 2);
    // Sorted by id string: make_id(1) ("0100...") < make_id(2) ("0200...").
    REQUIRE(model.entries()[0].id == make_id(1));
    REQUIRE(model.entries()[0].type == "a");
    REQUIRE(model.entries()[1].id == make_id(2));
}

TEST_CASE("AssetBrowserModel tracks selection", "[studio][assets]")
{
    AssetRegistry reg;
    reg.register_asset(AssetMetadata{make_id(1), "a"}, [] { return nullptr; });
    reg.register_asset(AssetMetadata{make_id(2), "b"}, [] { return nullptr; });

    AssetBrowserModel model;
    model.refresh(reg);
    REQUIRE_FALSE(model.has_selection());

    model.select(make_id(2));
    REQUIRE(model.has_selection());
    REQUIRE(model.selected() == make_id(2));
}

TEST_CASE("AssetBrowserModel drops a selection that no longer exists", "[studio][assets]")
{
    AssetRegistry with_two;
    with_two.register_asset(AssetMetadata{make_id(1), "a"}, [] { return nullptr; });
    with_two.register_asset(AssetMetadata{make_id(2), "b"}, [] { return nullptr; });

    AssetBrowserModel model;
    model.refresh(with_two);
    model.select(make_id(2));
    REQUIRE(model.has_selection());

    AssetRegistry with_one; // no id 2
    with_one.register_asset(AssetMetadata{make_id(1), "a"}, [] { return nullptr; });
    model.refresh(with_one);
    REQUIRE_FALSE(model.has_selection());
}
```

Register it in `tests/CMakeLists.txt` next to the other `unit/studio/...` entries:

```cmake
  unit/studio/asset_browser_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build/debug -j 2>&1 | tail -20`
Expected: compile error — `studio/asset_browser.hpp` not found.

- [ ] **Step 3: Write the model header**

Create `studio/asset_browser.hpp`:

```cpp
#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "engine/assets/asset_registry.hpp"
#include "foundation/serialization/reference.hpp"

namespace mosaic::studio
{

struct AssetEntry
{
    mosaic::foundation::serialization::AssetID id;
    std::string                                type;
    std::string                                source; // may be empty
};

// The asset browser's list + selection. Rebuilt each frame from the registry via
// refresh(); the selection persists across refreshes while its id still exists.
class AssetBrowserModel
{
public:
    void refresh(const mosaic::engine::assets::AssetRegistry& registry)
    {
        entries_.clear();
        registry.for_each([&](const mosaic::engine::assets::AssetMetadata& m)
                          { entries_.push_back(AssetEntry{m.uuid, m.type, m.source}); });
        std::sort(entries_.begin(), entries_.end(), [](const AssetEntry& a, const AssetEntry& b)
                  { return a.id.to_string() < b.id.to_string(); });

        if (has_selection_)
        {
            bool still_present = false;
            for (const AssetEntry& e : entries_)
            {
                if (e.id == selected_)
                {
                    still_present = true;
                    break;
                }
            }
            if (!still_present)
            {
                has_selection_ = false;
                selected_ = {};
            }
        }
    }

    const std::vector<AssetEntry>& entries() const { return entries_; }

    void select(mosaic::foundation::serialization::AssetID id)
    {
        selected_ = id;
        has_selection_ = true;
    }

    bool has_selection() const { return has_selection_; }
    mosaic::foundation::serialization::AssetID selected() const { return selected_; }

private:
    std::vector<AssetEntry>                    entries_;
    mosaic::foundation::serialization::AssetID selected_{}; // nil when none
    bool                                       has_selection_ = false;
};

} // namespace mosaic::studio
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build/debug -j && ctest --test-dir build/debug --output-on-failure -R "AssetBrowserModel"`
Expected: PASS (3 cases).

- [ ] **Step 5: Commit**

```bash
git add studio/asset_browser.hpp tests/unit/studio/asset_browser_test.cpp tests/CMakeLists.txt
git commit -m "studio: AssetBrowserModel (list + selection) (#37)"
```

---

## Task 4: Register three demo textures

**Files:**
- Modify: `app/run/demo_scene.hpp`

**Interfaces:**
- Consumes: existing `build_demo_scene`, `AssetRegistry::register_resource`, `render::Image`, `AssetID`.
- Produces: three registered `type="texture"` `Image` assets (checkerboard `C0DE…`, solid tint `A1…`, stripes `B2…`); the cube's `Material.texture` remains the checkerboard.

- [ ] **Step 1: Replace the single-texture block with three registered textures**

In `app/run/demo_scene.hpp`, find the checkerboard block (the `auto checker = std::make_shared<render::Image>(); ... registry.register_resource<render::Image>(... tex_id ...);` section and the following `world.add_component<render::Material>(...)`). Replace from the `auto checker` line through the `register_resource` call for the checkerboard with a helper plus three registrations:

```cpp
    // Three procedural textures so the Asset Browser has something to assign. Each is a
    // 64x64 RGBA Image registered as a "texture" asset under a distinct AssetID.
    std::array<std::uint8_t, 16> checker_uuid{};
    checker_uuid[0] = 0xC0;
    checker_uuid[1] = 0xDE;
    std::array<std::uint8_t, 16> solid_uuid{};
    solid_uuid[0] = 0xA1;
    std::array<std::uint8_t, 16> stripes_uuid{};
    stripes_uuid[0] = 0xB2;

    auto register_texture = [&](std::array<std::uint8_t, 16> uuid, auto&& pixel_fn)
    {
        auto image = std::make_shared<render::Image>();
        image->width  = 64;
        image->height = 64;
        image->pixels.resize(64 * 64 * 4);
        for (std::uint32_t y = 0; y < 64; ++y)
            for (std::uint32_t x = 0; x < 64; ++x)
            {
                pixel_fn(x, y, &image->pixels[(y * 64 + x) * 4]);
            }
        const mosaic::foundation::serialization::AssetID id{uuid};
        registry.register_resource<render::Image>(
            mosaic::engine::assets::AssetMetadata{id, "texture"}, image);
        return id;
    };

    const auto tex_id = register_texture(checker_uuid,
        [](std::uint32_t x, std::uint32_t y, std::uint8_t* p)
        {
            const std::uint8_t c = (((x / 8) + (y / 8)) % 2 == 0) ? 230 : 60;
            p[0] = c; p[1] = c; p[2] = c; p[3] = 255;
        });
    register_texture(solid_uuid,
        [](std::uint32_t, std::uint32_t, std::uint8_t* p)
        {
            p[0] = 220; p[1] = 120; p[2] = 60; p[3] = 255; // warm solid tint
        });
    register_texture(stripes_uuid,
        [](std::uint32_t x, std::uint32_t, std::uint8_t* p)
        {
            const std::uint8_t c = ((x / 8) % 2 == 0) ? 200 : 40;
            p[0] = c; p[1] = c; p[2] = c; p[3] = 255; // vertical stripes
        });

    world.add_component<render::Material>(cube_entity,
        render::Material{Vec3{1.0F, 1.0F, 1.0F}, tex_id}); // starts on the checkerboard
```

Ensure `#include <array>` and `#include "engine/assets/asset_metadata.hpp"` are present in `app/run/demo_scene.hpp` (they already are — the file registered the checkerboard before).

- [ ] **Step 2: Build + verify `mosaic run` unchanged**

Run: `cmake --build build/debug -j 2>&1 | tail -5 && ./build/debug/app/mosaic run --window --ticks 30 2>&1 | grep -c ERROR; echo "exit=$?"`
Expected: builds clean; `0` validation errors; the cube still shows the checkerboard (unchanged — the two extra textures are just registered, not used yet).

- [ ] **Step 3: Commit**

```bash
git add app/run/demo_scene.hpp
git commit -m "demo_scene: register three procedural textures for the asset browser (#37)"
```

---

## Task 5: Assets panel + inspector assignment

**Files:**
- Modify: `studio/inspector.hpp`, `studio/inspector.cpp`
- Modify: `studio/studio_app.hpp`, `studio/studio_app.cpp`
- Modify: `studio/studio_layout.cpp`
- Modify: `tests/unit/studio/studio_layout_test.cpp`
- Modify: `app/run/editor.cpp`

**Interfaces:**
- Consumes: `AssetBrowserModel` (Task 3), `FieldVisitor::field_asset` (Task 2), `AssetRegistry::for_each` (Task 1).
- Produces: `ImGuiFieldVisitor(AssetID selected, bool has_selection)` + its `field_asset` override; `StudioApp::assets()`; `StudioApp::draw_ui(RenderDevice&, World&, ComponentReflection&, const AssetRegistry&)`.

- [ ] **Step 1: Update the layout test (failing) + register the Assets panel**

In `tests/unit/studio/studio_layout_test.cpp`, change the panel-count case to expect 5 and assert the Assets panel:

```cpp
TEST_CASE("StudioLayout default has the five panels, all open", "[studio]")
{
    StudioLayout layout;
    REQUIRE(layout.panels().size() == 5);
    REQUIRE(layout.is_open("Hierarchy"));
    REQUIRE(layout.is_open("Viewport"));
    REQUIRE(layout.is_open("Inspector"));
    REQUIRE(layout.is_open("Assets"));
    REQUIRE(layout.is_open("Console"));
}
```

In `studio/studio_layout.cpp`, add `"Assets"` to the panel list (after `"Inspector"`):

```cpp
    : panels_{{"Hierarchy", true}, {"Viewport", true}, {"Inspector", true}, {"Assets", true}, {"Console", true}}
```

- [ ] **Step 2: Run the layout test (verify it now passes after the source change)**

Run: `cmake --build build/debug -j && ctest --test-dir build/debug --output-on-failure -R "StudioLayout"`
Expected: PASS (both StudioLayout cases).

- [ ] **Step 3: Give `ImGuiFieldVisitor` a selection + `field_asset` override**

In `studio/inspector.hpp`, replace the class body with:

```cpp
#pragma once

#include "engine/reflection/field_visitor.hpp"
#include "foundation/serialization/reference.hpp"

namespace mosaic::studio
{

// Renders each visited field as an ImGui widget: floats are editable (DragFloat), AssetID
// fields show their id plus an "Assign" button (writes the browser's current selection),
// everything else is a read-only label.
class ImGuiFieldVisitor : public mosaic::engine::reflection::FieldVisitor
{
public:
    ImGuiFieldVisitor() = default;
    ImGuiFieldVisitor(mosaic::foundation::serialization::AssetID selected, bool has_selection)
        : selected_(selected), has_selection_(has_selection)
    {
    }

    void field_float(std::string_view name, float& value) override;
    void field_readonly(std::string_view name, const std::string& display) override;
    void field_asset(std::string_view name,
                     mosaic::foundation::serialization::AssetID& value) override;
    void begin_group(std::string_view name) override;
    void end_group() override;

private:
    mosaic::foundation::serialization::AssetID selected_{};
    bool                                       has_selection_ = false;
};

} // namespace mosaic::studio
```

In `studio/inspector.cpp`, add the `field_asset` implementation (after `field_readonly`):

```cpp
void ImGuiFieldVisitor::field_asset(std::string_view name,
                                    mosaic::foundation::serialization::AssetID& value)
{
    const std::string label(name);
    ImGui::PushID(label.c_str());
    ImGui::LabelText(label.c_str(), "%s", value.to_string().c_str());
    ImGui::BeginDisabled(!has_selection_);
    if (ImGui::Button("Assign selected"))
    {
        value = selected_;
    }
    ImGui::EndDisabled();
    ImGui::PopID();
}
```

- [ ] **Step 4: Own the browser + add the Assets panel + thread the registry through `draw_ui`**

In `studio/studio_app.hpp`: add the includes and members. Add near the other studio includes:

```cpp
#include "engine/assets/asset_registry.hpp"
#include "studio/asset_browser.hpp"
```

Change the `draw_ui` declaration to take the registry:

```cpp
    void draw_ui(mosaic::engine::render::RenderDevice& device, mosaic::engine::World& world,
                 const mosaic::engine::reflection::ComponentReflection& reflection,
                 const mosaic::engine::assets::AssetRegistry& registry);
```

Add the accessor (near `viewport()`):

```cpp
    AssetBrowserModel& assets() { return assets_; }
```

Declare the panel body (near `draw_console`):

```cpp
    void draw_assets(const mosaic::engine::assets::AssetRegistry& registry);
```

Add the member (near `console_`):

```cpp
    AssetBrowserModel assets_;
```

In `studio/studio_app.cpp`:

Update the `draw_ui` definition signature to match (add the registry param):

```cpp
void StudioApp::draw_ui(mosaic::engine::render::RenderDevice& device, mosaic::engine::World& world,
                        const mosaic::engine::reflection::ComponentReflection& reflection,
                        const mosaic::engine::assets::AssetRegistry& registry)
```

Add an `"Assets"` branch to the panel dispatch (after the `"Inspector"` branch):

```cpp
            else if (panel.name == "Assets")
            {
                draw_assets(registry);
            }
```

Add the panel body (after `draw_console`):

```cpp
void StudioApp::draw_assets(const mosaic::engine::assets::AssetRegistry& registry)
{
    assets_.refresh(registry);
    for (const AssetEntry& entry : assets_.entries())
    {
        const std::string label =
            entry.type + "  " + (entry.source.empty() ? entry.id.to_string() : entry.source);
        const bool selected = assets_.has_selection() && assets_.selected() == entry.id;
        if (ImGui::Selectable(label.c_str(), selected))
        {
            assets_.select(entry.id);
        }
    }
}
```

Construct the inspector visitor with the browser's selection — change `draw_inspector`'s
`ImGuiFieldVisitor visitor;` to:

```cpp
    ImGuiFieldVisitor visitor(assets_.selected(), assets_.has_selection());
```

- [ ] **Step 5: Pass the registry from the editor**

In `app/run/editor.cpp`, update the `draw_ui` call (currently `studio.draw_ui(*device, world, reflection);`) to:

```cpp
        studio.draw_ui(*device, world, reflection, asset_registry);
```

- [ ] **Step 6: Build**

Run: `cmake --build build/debug -j 2>&1 | tail -8`
Expected: builds clean.

- [ ] **Step 7: Full suite + functional verification**

Run: `ctest --test-dir build/debug --output-on-failure`
Expected: all tests PASS (Tasks 1–3 additions + updated layout test).

Run: `./build/debug/app/mosaic editor --ticks 180 2>&1 | grep -c ERROR; echo "exit=$?"`
Expected: `0` validation errors, `exit=0`. In the window, the **Assets** panel lists three
textures; selecting one and clicking **Assign selected** on the cube `Material`'s `texture`
field swaps the cube's texture in the viewport (maintainer confirms visually).

- [ ] **Step 8: Commit**

```bash
git add studio/inspector.hpp studio/inspector.cpp studio/studio_app.hpp studio/studio_app.cpp studio/studio_layout.cpp tests/unit/studio/studio_layout_test.cpp app/run/editor.cpp
git commit -m "studio: Assets panel + inspector asset assignment (#37)"
```

---

## Task 6: Update the build handoff (next-steps)

**Files:**
- Modify: `docs/project/next-steps.md`

- [ ] **Step 1: Record the asset browser**

Update `docs/project/next-steps.md`: change the "Last updated" line and note that the
**post-v0.1 Studio asset browser** is built — an Assets panel lists registered assets and
assigns a selected asset to a component's `AssetID` field (the cube's `Material.texture`)
live via the reflected-property surface (a new mutable `FieldVisitor::field_asset`), with
`AssetRegistry` enumeration and a headless `AssetBrowserModel`. Note the remaining deferred
Studio work (Blender live-link; asset browser follow-ups: type-aware filtering, thumbnails,
disk import, clear-to-nil). Link the [spec](../superpowers/specs/2026-08-23-studio-asset-browser-design.md)
and [plan](../superpowers/plans/2026-08-23-studio-asset-browser.md).

- [ ] **Step 2: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "next-steps: Studio asset browser (browse + assign) built (#37)"
```

---

## Self-Review

**Spec coverage:**
- `AssetRegistry` enumeration (spec §1) → Task 1. ✓
- Mutable `field_asset` + adapter routing (spec §2) → Task 2. ✓
- `AssetBrowserModel` (spec §3) → Task 3. ✓
- Demo assets (spec §Demo assets) → Task 4. ✓
- Assets panel + inspector assignment + `draw_ui` registry + layout + editor wiring (spec §4) → Task 5. ✓
- Testability (unit: enumeration, field_asset path, browser model; functional editor) → Tasks 1–3 + Task 5 Step 7. ✓
- Deferred items (type filtering, thumbnails, import, clear-to-nil, Handle read-only) → Global Constraints + Task 6 note. ✓

**Placeholder scan:** No TBD/TODO; every code step shows complete code; commands have expected output. ✓

**Type consistency:** `for_each`/`size` (Task 1) used by `AssetBrowserModel::refresh` (Task 3); `field_asset(std::string_view, AssetID&)` signature identical across Tasks 2/5; `AssetBrowserModel`/`AssetEntry` members consistent across Tasks 3/5; `draw_ui(..., const AssetRegistry&)` matches between `studio_app.hpp`/`.cpp` (Task 5) and the editor call (Task 5 Step 5); `make_id` test helper defined per test file. ✓

**One watch-item flagged:** Task 4's `register_texture` lambda captures `registry` by reference and is called three times; confirm `app/run/demo_scene.hpp` still compiles clean under the project's warning flags (no unused captures), and that `mosaic run --window` is visually unchanged (still checkerboard) since only registration — not the cube's `Material.texture` — changed.
