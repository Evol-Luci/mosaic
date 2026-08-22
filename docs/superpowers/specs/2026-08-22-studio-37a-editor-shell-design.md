# Studio 37a — Editor Shell (design)

**Tracking issue:** [#37 Studio shell](https://github.com/Evol-Luci/mosaic/issues/37).
**Decomposition:** [studio-decomposition](2026-08-22-studio-decomposition.md) (slice 37a).
**Date:** 2026-08-22.

## Summary

Stand up the Studio's window: a `mosaic editor` command that opens a Dear ImGui
**docking** shell — a full-window dockspace, a menu bar, and empty placeholder panels
(Hierarchy / Inspector / Console) — over the existing GLFW window and Vulkan device.
This is the foundational slice every other Studio slice builds on; it establishes the
`studio/` tier, the `mosaic editor` entry point, and the neutral UI hooks on
`RenderDevice`. It renders **no scene content** — the viewport is 37d, the panels are
filled by 37b/37c/37e.

## Scope

**In:** the `studio/` tier + `mosaic_studio` library; the `mosaic editor` CLI
subcommand; Dear ImGui (docking branch) wired into the Vulkan backend and the frame
loop; a dockspace + menu bar + empty labelled panels; the neutral `RenderDevice` UI
hooks.

**Out (later slices):** the entity hierarchy content (37b), the reflection inspector
(37c), the embedded viewport / offscreen render (37d), play/stop + console content
(37e). No world/scene is loaded or displayed in 37a.

## Architecture

### `studio/` tier + `mosaic_studio` library

A new top-level **`studio/`** tier, structured like `app/` (a library behind test
seams + a thin entry). It depends on `engine/` and the neutral render interface
(`engine/render/render_device.hpp`) and on Dear ImGui. **`engine/` and `foundation/`
never depend on `studio/` or ImGui** — the tier contract is unchanged.

Two units, split so the logic is testable without a window:

- **`StudioLayout`** — a plain model of the shell: the set of panels (each a `name` +
  `bool open`) and menu state (e.g. an `exit_requested` flag). Constructed with the
  default panel set (`Hierarchy`, `Inspector`, `Console`). Pure data + small methods
  (`toggle(name)`, `is_open(name)`); **no ImGui**. Unit-tested directly.
- **`StudioApp`** — owns a `StudioLayout` and a `RenderDevice&`; its `draw_ui()` walks
  the layout and emits the ImGui calls (dockspace, menu bar, `Begin/End` per open
  panel). This is the ImGui glue; verified functionally, not unit-tested.

### `mosaic editor` CLI subcommand

`app/cli/options.cpp` gains an `editor` subcommand alongside `run`. `ParseResult`
carries which command was chosen via a small `enum class Command { Run, Editor }` on
`Options` (default `Run`). `editor` **implies a window** (there is no headless editor)
and accepts `--duration` / `--ticks` for bounded runs (so it can be exercised in CI).
`main.cpp` dispatches on the command: `run` keeps its existing path; `editor` runs the
studio path.

The editor path mirrors `run --window`'s setup: `glfwInit` → create window → create the
Vulkan `RenderDevice` → **`device.init_ui()`** → run the frame loop → tear down (device
before window, as today).

### Neutral UI hooks on `RenderDevice`

Three new **virtual methods with default no-op bodies** (so `FakeRenderDevice` and
headless `mosaic run` need no changes):

```cpp
virtual void init_ui() {}        // create ImGui context + backends; upload fonts
virtual void begin_ui_frame() {} // ImplVulkan_NewFrame + ImplGlfw_NewFrame + ImGui::NewFrame
virtual void end_ui_frame() {}   // ImGui::Render + ImplVulkan_RenderDrawData(current cmd)
```

The Vulkan backend overrides them:

- **`init_ui`** — create the ImGui context, enable docking
  (`io.ConfigFlags |= ImGuiConfigFlags_DockingEnable`), init `imgui_impl_glfw`
  (`InitForVulkan`, using the stored `GLFWwindow*`) and `imgui_impl_vulkan` (instance,
  physical device, device, graphics queue + family, a **dedicated ImGui descriptor
  pool**, the existing `render_pass_`, `kFramesInFlight` as image count / min image
  count, no MSAA), then upload the font atlas. The backend stores the `GLFWwindow*` at
  construction (it is already passed to `create_vulkan_render_device`).
- **`begin_ui_frame`** — the three `NewFrame` calls, in backend→platform→ImGui order.
- **`end_ui_frame`** — `ImGui::Render()` then
  `ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd_[frame_])`. It records into
  the **current frame's command buffer, inside the active render pass** (the same pass
  `begin_frame` opened and `end_frame` closes), so it must be called between
  `begin_frame` and `end_frame`.
- **Shutdown** — `ImGui_ImplVulkan_Shutdown` / `ImGui_ImplGlfw_Shutdown` /
  `ImGui::DestroyContext` + the ImGui descriptor pool, in the backend destructor
  (before the device is destroyed).

Rationale (settled during brainstorming): ImGui must record into the command buffer and
render pass the device already owns, so the device is the cohesive home for these hooks;
a separate UI interface would need the device's frame internals exposed for no boundary
gain. ImGui *core* (the `Begin/End`/widget calls in `StudioApp`) is an ordinary C++
library — the studio tier calling it does not cross the render boundary; only the
backend links `imgui_impl_glfw` / `imgui_impl_vulkan`.

### Frame loop (reuse `FramePump`)

The editor reuses the existing `app/run/frame_pump.hpp` `FramePump` rather than a bespoke
loop — it already provides frame pacing and the stop conditions (window-close,
`--duration`, `--ticks`, SIGINT). Wiring:

- `on_frame` = `glfwPollEvents()` → `device.begin_frame()` → `device.clear(bg)` →
  `device.begin_ui_frame()` → `studio.draw_ui()` → `device.end_ui_frame()` →
  `device.end_frame()`.
- `stop_requested` = `glfwWindowShouldClose(window)` or the SIGINT/duration/ticks stops
  (as `run --window` wires today), plus the layout's `exit_requested` (File ▸ Exit).

The `FramePump` advances an **empty, idle `World`** (no registered systems), so the sim
tick is a harmless no-op. Ticking a real world is 37e's concern.

## Data flow

```
main (Command::Editor)
  └─ window + Vulkan RenderDevice + StudioApp(device)
       └─ device.init_ui()
       └─ FramePump.on_frame each frame:
            poll → begin_frame → clear
                 → begin_ui_frame → StudioApp::draw_ui(reads StudioLayout) → end_ui_frame
                 → end_frame
       └─ stop on window-close / File▸Exit / --duration / SIGINT
```

`StudioApp::draw_ui()` reads `StudioLayout` (which panels are open) and issues the
dockspace + menu bar + per-panel `Begin/End`. File ▸ Exit sets `exit_requested` on the
layout, which `stop_requested` observes.

## Dependency: Dear ImGui (docking branch)

Fetched via FetchContent (docking branch), built into a small **`mosaic_imgui`** static
lib from the ImGui sources: **core** (`imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`,
`imgui_widgets.cpp`) + the **backends** (`imgui_impl_glfw.cpp`, `imgui_impl_vulkan.cpp`).
`modules/render_vulkan` links it (core + backends); `studio/` links it (core only).
Consistent with the project's existing FetchContent deps (GLFW, Catch2, nlohmann,
toml++); ImGui lives in the backend/app tier, never in `engine/` or `foundation/`.

## Error handling

Window / device / ImGui init failures surface through the existing paths: `glfwInit` /
`glfwCreateWindow` failures report via the status reporter and exit non-zero (as
`run --window` does today); Vulkan errors go through `vk_check` (throws
`std::runtime_error`). If `init_ui` fails, `mosaic editor` reports and exits non-zero.
There is no headless editor — `editor` always opens a window.

## Testing

Following the decomposition's testability boundary (headless logic unit-tested; the
draw verified functionally):

- **Unit — `editor` subcommand parsing** (`[cli]`): `mosaic editor` yields
  `Command::Editor`; `mosaic run` yields `Command::Run`; `editor` accepts
  `--duration`/`--ticks`; unknown/missing subcommand behaves like the existing tests.
- **Unit — `StudioLayout`** (`[studio]`): default construction contains the three
  expected panels all open; `toggle(name)` flips `is_open`; `exit_requested` defaults
  false and sets true. No ImGui, no window.
- **Functional:** `mosaic editor --duration 1` launches the docked shell and exits 0
  with silent stderr. The GUI window cannot be screenshotted in the sandbox, so a clean
  bounded launch is the acceptance for the ImGui/GPU path. (`FakeRenderDevice`'s no-op
  UI hooks keep any non-window studio logic tests windowless.)

## Non-goals

- Entity hierarchy content (37b), reflection inspector (37c), embedded viewport /
  offscreen render (37d), play/stop + console content (37e).
- Any scene/world loading or display.
- Persisting the dock layout to disk (ImGui's `.ini` default behavior is acceptable;
  a curated default layout can come later).
- Theming beyond ImGui defaults.
