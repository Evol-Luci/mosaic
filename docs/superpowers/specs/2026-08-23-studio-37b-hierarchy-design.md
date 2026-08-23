# Studio 37b — Scene Hierarchy Panel (design)

**Tracking issue:** [#37 Studio shell](https://github.com/Evol-Luci/mosaic/issues/37).
**Decomposition:** [studio-decomposition](2026-08-22-studio-decomposition.md) (slice 37b).
**Builds on:** 37a (the editor shell — `mosaic editor` opens an ImGui docking window).
**Date:** 2026-08-23.

## Summary

Fill the editor's **Hierarchy** panel with the world's entities and add a **selection
model**: the panel lists every live entity, clicking one selects it, and the selection
is surfaced (a teaser) in the Inspector placeholder. This is the seam the reflection
inspector (37c) reads from. The slice also folds in two non-blocking follow-ups the 37a
final review recorded.

## Scope

**In:** a `SelectionModel` (studio tier); a `build_hierarchy(World)` enumeration seam; the
Hierarchy panel rendering the entity list with click-to-select; the Inspector placeholder
showing the current selection; a small demo world in `run_editor` so the panel is
populated; and the two folded 37a fixes below.

**Deferred (later slices / YAGNI):** parent-child tree nesting (flat list for now), a
`Name` component (labels are `Entity <index>`), multi-select, drag-reparenting, and the
real reflection inspector (37c).

## Architecture

### `SelectionModel` (`studio/selection.hpp`)

Pure studio state — the currently selected entity, or none:

```cpp
class SelectionModel
{
public:
    mosaic::foundation::EntityID selected() const { return selected_; }
    bool has_selection() const { return static_cast<bool>(selected_); }
    bool is_selected(mosaic::foundation::EntityID id) const { return selected_ == id; }
    void select(mosaic::foundation::EntityID id) { selected_ = id; }
    void clear() { selected_ = mosaic::foundation::EntityID{}; }

private:
    mosaic::foundation::EntityID selected_{}; // default = nil = nothing selected
};
```

Header-only, depends only on `foundation/core/entity_id.hpp`. Unit-tested. `EntityID{}` is
nil (its `operator bool` is false), so "nothing selected" is the natural default.

### `build_hierarchy(const World&)` (`studio/hierarchy.hpp`)

A testable seam that separates entity enumeration from the ImGui draw:

```cpp
struct HierarchyRow
{
    mosaic::foundation::EntityID id;
    std::string                  label; // "Entity <index>"
};

std::vector<HierarchyRow> build_hierarchy(const mosaic::engine::World& world);
```

Walks `World::for_each_entity` in iteration order, producing one row per live entity with
`label = "Entity " + std::to_string(id.index())`. Empty world → empty vector. This is the
unit-tested logic; the ImGui rendering that consumes it is functional-only.

### `StudioApp` changes

`StudioApp` gains a `SelectionModel selection_`. Its draw entry point changes from
`draw_ui()` to **`draw_ui(const World&)`** so the Hierarchy panel can enumerate entities.
Rendering:

- **Hierarchy panel:** for each `HierarchyRow` from `build_hierarchy(world)`, draw an
  `ImGui::Selectable(row.label, selection_.is_selected(row.id))`; on click,
  `selection_.select(row.id)`. The selected row is highlighted by the `Selectable`'s
  selected state.
- **Inspector panel (placeholder):** show the selection as text — `has_selection()` →
  `"Selected: Entity <index>"`, else `"(nothing selected)"`. This proves selection flows
  Hierarchy → Inspector; 37c replaces this placeholder with real reflected fields.
- **Console panel:** unchanged placeholder (37e).

`selection()` accessor exposes the model (for tests / future slices). The dockspace, menu
bar, and File ▸ Exit are unchanged from 37a.

### Tier

`studio/` gains a dependency on **`mosaic_engine`** (it now reads `World` and `EntityID`).
This is allowed by the tier contract — `studio/` is app-tier, above `engine/`, and
`engine/`/`foundation/` still never depend on `studio/`. `studio/CMakeLists.txt` links
`mosaic_engine` PUBLIC (the `World` type appears in `StudioApp`/`build_hierarchy` public
signatures) and keeps `mosaic_imgui` PRIVATE.

### `run_editor` (app tier)

Replace the empty idle world with a **small demo world** so the hierarchy is populated:
create ~4 entities, each with a `Transform` (so 37c's inspector has real components to
edit later). The world is still idle (no systems registered; the `FramePump` tick remains
a harmless no-op — play/stop is 37e). Pass the world to `studio.draw_ui(world)` in the
`on_frame` hook.

## Folded 37a follow-ups

Recorded as non-blocking Minors in the 37a final review; fixed here since 37b touches both
call sites.

- **(e) `init_ui()` GLFW-cleanup gap** — in `run_editor`, `device->init_ui()` currently
  runs outside the try/catch that tears down GLFW on device-creation failure, so an
  `init_ui` throw (e.g. descriptor-pool creation) would leak the window + GLFW. Wrap it:

  ```cpp
  try { device->init_ui(); }
  catch (const std::exception& e) {
      reporter->error(std::string("editor UI init failed: ") + e.what());
      device.reset();            // ImGui/Vulkan torn down before the window
      glfwDestroyWindow(window);
      glfwTerminate();
      return 1;
  }
  ```

  `device.reset()` before `glfwDestroyWindow` preserves teardown order; if `init_ui` threw
  before setting `ui_ready_`, the device destructor's ImGui shutdown is already guarded.

- **(g) ImGui `ImageCount` vs swapchain image count** — the Vulkan backend's
  `ImGui_ImplVulkan_InitInfo` sets `MinImageCount`/`ImageCount` from `kFramesInFlight`
  (2); the actual swapchain allocates `caps.minImageCount + 1` images (typically 3), held
  in `images_`. Set both fields from `static_cast<uint32_t>(images_.size())` so ImGui's
  internal per-image allocation matches the real swapchain. (Safe today because
  `RenderDrawData` records into the passed command buffer rather than indexing per
  swapchain image, but aligning it removes the latent mismatch.)

## Data flow

```
run_editor: demo World (≈4 entities w/ Transform) created once
  on_frame → begin_ui_frame → studio.draw_ui(world) → end_ui_frame
     Hierarchy panel: build_hierarchy(world) → Selectable rows → click sets selection_
     Inspector panel: reads selection_ → "Selected: Entity N" / "(nothing selected)"
```

Selection is studio-side state (`SelectionModel`), stable across frames because it stores
an `EntityID` handle and the world lives for the whole editor session.

## Error handling

Unchanged from 37a plus the (e) fix above: window/device/`init_ui` failures report and
exit non-zero with GLFW cleaned up. `build_hierarchy` on an empty world yields an empty
list (the panel simply shows nothing). A stale selection (entity destroyed) is not
possible in 37b — the demo world is static — but `is_selected` comparing a nil/absent id
simply never matches, so the Inspector falls back to "(nothing selected)".

## Testing

Following the decomposition's boundary (headless logic unit-tested; the draw verified
functionally):

- **Unit — `SelectionModel`** (`[studio]`): default `has_selection()==false`; `select`
  then `is_selected`/`selected` reflect it; `clear` resets to no selection; selecting a
  different id updates `is_selected` for both.
- **Unit — `build_hierarchy`** (`[studio]`): an empty world yields an empty vector; a
  world with N created entities yields N rows whose `id`s match the created handles and
  whose `label`s equal `"Entity <index>"`; row count tracks entities created.
- **Functional:** `mosaic editor --duration 1` launches with the demo world, renders the
  populated Hierarchy, and exits 0 with silent stderr (the GUI cannot be screenshotted in
  the sandbox; a clean bounded launch is the acceptance for the draw path).

## Non-goals

- Parent-child nesting, a `Name` component, multi-select, drag-reparenting.
- The reflection inspector (37c), the embedded viewport (37d), play/stop + console content
  (37e).
- Scene load/save wiring into the editor (the demo world is constructed in code).
