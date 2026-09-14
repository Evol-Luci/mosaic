# Plugin SDK — Custom Studio Panels — Design

## Goal (Phase 5, Tool Ecosystem pillar — Plugin SDK slice 2 of 3)

Deliver the second Plugin SDK slice: a project-local `plugins/*.py` file declares a
`MOSAIC_PANELS` dict (mirroring slice 1's `MOSAIC_MENU_ITEMS`) mapping a panel title to
a draw function; Studio adds that panel to its dockable layout and calls the function
once per frame while the panel is open, giving the plugin real ImGui drawing power
(text, buttons, inputs, checkboxes, scroll regions) instead of the world-space `UiText`
workarounds the example plugins currently use. Slice 3 (custom asset importers) and
native `.so`/`.dll` plugins remain future work, unchanged from slice 1's scope notes.

## Design

### The core tension this slice resolves

Slice 1's `PluginHost` gives each plugin file its own `ScriptEngine`, built via
`modules/script_python::create_python_script_engine()` — the same engine gameplay
`ScriptComponent`s use. That module has zero ImGui dependency by design (it must stay
usable outside the editor entirely — `mosaic run`, headless tests). `studio/` has zero
pybind11 dependency by design (the Scripting Boundary: nothing in `studio/` has ever
called into Python directly). Drawing a custom panel needs Python code that calls
ImGui-drawing functions every frame — which needs both in the same place.

### Approach: a new, Studio-only engine module

A new module, `modules/script_python_studio`, provides `StudioPluginScriptEngine` (via
`create_studio_plugin_script_engine()`, mirroring `create_python_script_engine`'s
shape) — a second concrete `ScriptEngine` implementation whose `bind_world` installs
every existing gameplay global (`get_component`, `set_component`, ...) **plus** nine
new `imgui_*` globals. `PluginHost` switches to this factory for *every* plugin engine
it creates — not just panel-registering ones — since it's a strict superset of the
gameplay engine; a menu-item-only plugin loses nothing. `ScriptConsoleModel`/
`ScriptEditorModel` (the Script Console/Editor, used for ad-hoc gameplay scripting)
keep using the plain `create_python_script_engine` factory unchanged: calling
`imgui_button()` outside an active panel draw (not inside an open `ImGui::Begin`/`End`)
would corrupt ImGui's internal state, so the richer engine is only ever handed to
`PluginHost`, the one place that's actually safe.

This keeps the Scripting Boundary intact: `modules/script_python_studio` is the *one*
place pybind11 and ImGui coexist — the same pattern `modules/render_vulkan` already
uses for Vulkan headers. `studio/` still never includes `pybind11/pybind11.h`.

### Avoiding duplication: a shared globals installer

`PythonScriptEngine::bind_world`'s ~15 gameplay-global lambdas (`get_component`
through `undo_depth`) are extracted into a new public function,
`mosaic::script::python::install_world_globals` (declared in a new header
`modules/script_python/world_globals.hpp`, implemented in
`modules/script_python/world_globals.cpp`), taking `py::dict&` plus
pointer-to-pointer `World**`/`const ComponentReflection**`/`CommandStack**` so the
existing "commands are optional until `bind_commands` sets it" semantics (already
relied on today — `undo`/`redo`/`undo_depth` read `stack_` at call time, not install
time) carry over unchanged. Both `PythonScriptEngine::bind_world` (modules/script_python)
and `StudioPluginScriptEngine::bind_world` (modules/script_python_studio, the new,
more specific module depending on the more general one) call this same function; only
`StudioPluginScriptEngine::bind_world` additionally installs the `imgui_*` globals.

`run_string`/`get_global`/`call_function`'s stdout-capture scaffolding and CPython
interpreter lifecycle (`ensure_interpreter()`, currently file-local to
`python_script_engine.cpp`) are **not** shared this slice — `ensure_interpreter()` is
promoted to a public declaration in `python_script_engine.hpp` so
`StudioPluginScriptEngine` can call the same process-wide `std::call_once`-guarded
initializer (calling `py::initialize_interpreter()` twice from two independent
`std::call_once` flags would double-initialize CPython and assert), but the ~80 lines
of stdout-capture/error-handling logic in `run_string`/`call_function` are duplicated
into the new class. `PythonScriptEngine` is `final` and defined entirely inside its own
`.cpp` (not exposed in any header), so subclassing it isn't an option without a larger,
disproportionate refactor (exposing its declaration, removing `final`, opening a
cross-module inheritance relationship) — for a two-implementation situation, accepting
this one piece of duplication is the smaller, more contained change. If a third
`ScriptEngine` variant is ever needed, that's the point to revisit.

### The nine `imgui_*` globals (v1 widget set)

```
imgui_text(text: str) -> None
imgui_separator() -> None
imgui_same_line() -> None
imgui_spacing() -> None
imgui_button(label: str) -> bool             # True if clicked this frame
imgui_checkbox(label: str, value: bool) -> bool   # returns the new value
imgui_input_text(label: str, value: str) -> str   # returns the new value
imgui_slider_float(label: str, value: float, min: float, max: float) -> float
imgui_begin_child(id: str, height: float) -> None  # a scrollable sub-region
imgui_end_child() -> None
```

`imgui_checkbox`/`imgui_input_text`/`imgui_slider_float` all follow the same
"pass the current value in, get the (possibly unchanged) new value back" shape —
Python decides whether to act on a change by comparing, matching how
`get_component`/`set_component` already put all state-reading responsibility on the
script rather than the engine. `imgui_text` uses `ImGui::TextUnformatted` (not
`ImGui::Text`, which treats its argument as a `printf`-style format string — a plugin
author's text containing a literal `%` must not be misinterpreted).

### Mismatched `imgui_begin_child`/`imgui_end_child` safety net

A plugin that calls `imgui_begin_child()` without a matching `imgui_end_child()` (an
easy mistake — an early `return` inside an `if`, a forgotten call) leaves ImGui's
internal window stack corrupted for the rest of the session, breaking unrelated panels
too — a much worse failure mode than the draw function itself just doing something
visually wrong. `StudioPluginScriptEngine` tracks an `int child_depth_` member; its
`call_function` override resets it to `0` before invoking the plugin's function and, if
it's still positive after the call returns (including after an exception), calls
`ImGui::EndChild()` that many times and appends a warning to the result's error text.
This mirrors the exception-safety rigor slice 1's `PluginHost::load` review applied to
filesystem iteration — a cheap, contained guard against a real, easy-to-hit mistake
with a disproportionately bad blast radius.

### `PluginHost` additions

```cpp
// One registered panel, read out of a plugin's MOSAIC_PANELS dict. Structurally
// identical to PluginMenuItem but kept as its own type: a menu item and a panel are
// different concepts today, and may diverge later (e.g. a panel gaining a default-open
// flag) without disturbing menu-item call sites.
struct PluginPanel
{
    std::string plugin_name;
    std::string label;
    std::string function_name;
};
```

`load()` reads `MOSAIC_PANELS` via `get_global` exactly like `MOSAIC_MENU_ITEMS` —
same validation (must be a table if present; a non-table, or a table with a non-string
value, is a load error for the whole plugin file; absent is fine, zero panels). A
private `call_by_name(plugin_name, function_name) -> ScriptResult` helper (the
plugin-lookup-then-`call_function` logic `invoke()` already has) is shared by the
existing `invoke(const PluginMenuItem&)` and a new `invoke_panel(const PluginPanel&)`,
plus a new `const std::vector<PluginPanel>& panels() const` accessor and
`const PluginPanel* find_panel(const std::string& name) const` (linear search by
label — the panel counts here are small; no index needed).

### `StudioLayout` and `studio_app.cpp` wiring

`StudioLayout` gains `void add_panel(std::string name)` — idempotent (a no-op if a
panel with that name already exists), appends `Panel{name, /*open=*/false}` otherwise.
After every `plugin_host_.load(...)` call (the existing first-frame lazy load and the
existing "Reload Plugins" menu action, both already in `studio_app.cpp`), a new loop
calls `layout_.add_panel(item.label)` for each `plugin_host_.panels()` entry.

The existing panel-dispatch loop's terminal `else { ImGui::TextUnformatted("(empty)"); }`
becomes:

```cpp
else if (const PluginPanel* p = plugin_host_.find_panel(panel.name))
{
    const auto result = plugin_host_.invoke_panel(*p);
    if (!result.ok)
    {
        mosaic::foundation::log::error("plugin", "{}: {}", p->plugin_name, result.error);
    }
}
else
{
    ImGui::TextUnformatted("(empty)");
}
```

placed inside the same `if (ImGui::Begin(panel.name.c_str()))` block every other panel
already renders inside — so the plugin's draw function only ever runs while that
`Begin`/`End` pair is open, and its `imgui_*` calls append to that window exactly like
any other ImGui code would.

### `StudioApp` constructor

Gains a second factory parameter, `PluginHost::EngineFactory plugin_factory = {}`
(matching the existing default-empty pattern, and slice 1's final-review fix already
guards `PluginHost::load` against an empty factory). `plugin_host_` is now constructed
with `plugin_factory` instead of `script_factory`. `app/run/editor.cpp`'s single
`StudioApp` construction site passes
`[] { return mosaic::script::python_studio::create_studio_plugin_script_engine(); }`
for the new parameter, alongside the unchanged gameplay factory for `script_factory`.

## Testing

- **`tests/unit/script/world_globals_test.cpp`** (new): the extracted
  `install_world_globals` function, tested directly with a bare `py::dict` (no
  `ScriptEngine` involved) — the same behavioral cases `script_engine_test.cpp`
  already covers for `get_component`/`set_component`/etc. through the full engine, but
  targeted at the extraction itself so a future third caller can trust it in isolation.
- **`tests/unit/script/script_engine_test.cpp`** (unchanged file, re-run as
  verification): every existing case must still pass unmodified after
  `PythonScriptEngine::bind_world` is refactored to call `install_world_globals` —
  proves the extraction is behavior-preserving, not a new test requirement.
- **`tests/unit/studio/plugin_host_test.cpp`** (extended): `MOSAIC_PANELS` parsing
  (valid, absent, non-table, non-string-value — mirroring the existing
  `MOSAIC_MENU_ITEMS` cases exactly), `panels()`/`find_panel()`, `invoke_panel()`
  dispatch (a panel's draw function mutating real `World` state, proving the switch to
  `create_studio_plugin_script_engine` still gives plugins the gameplay globals). None
  of these tests call any `imgui_*` global — ImGui needs an active frame/context this
  suite doesn't have, consistent with every other ImGui-glue code in this project never
  being unit-tested.
- **`tests/unit/studio/studio_layout_test.cpp`** (extended): `add_panel` appends a new
  closed panel; calling it twice with the same name doesn't duplicate.
- **Manual** (the part this suite cannot automate): a plugin registering
  `MOSAIC_PANELS` with a draw function using several `imgui_*` calls, confirming the
  panel appears in Studio's dockable layout, renders correctly, and a deliberately
  unmatched `imgui_begin_child()` (no corresponding `imgui_end_child()`) doesn't corrupt
  any other panel afterward.

## Files

**New**
- `modules/script_python/world_globals.hpp` / `.cpp`
- `modules/script_python_studio/studio_plugin_script_engine.hpp` / `.cpp`
- `modules/script_python_studio/CMakeLists.txt`
- `tests/unit/script/world_globals_test.cpp`

**Modified**
- `modules/script_python/python_script_engine.hpp` — export `ensure_interpreter()`.
- `modules/script_python/python_script_engine.cpp` — `bind_world`/`bind_commands` call
  `install_world_globals`; `ensure_interpreter` moves from file-local to declared.
- `modules/script_python/CMakeLists.txt` — add `world_globals.cpp`.
- `studio/plugin_host.hpp` / `.cpp` — `PluginPanel`, `panels()`, `find_panel()`,
  `invoke_panel()`, shared `call_by_name` helper.
- `studio/studio_layout.hpp` / `.cpp` — `add_panel()`.
- `studio/studio_app.hpp` / `.cpp` — second constructor factory parameter, panel-list
  wiring after `load()`, dispatch-loop branch.
- `app/run/editor.cpp` — pass `create_studio_plugin_script_engine` for the new
  parameter.
- `CMakeLists.txt` (top-level or `modules/`) — add the new module's subdirectory.
- `tests/unit/studio/plugin_host_test.cpp` — extended per Testing above.
- `tests/unit/studio/studio_layout_test.cpp` — extended per Testing above.
- `tests/CMakeLists.txt` — register the new test file.
- `tools/example_plugins/` — add one new example exercising the panel API (e.g. an
  interactive version of `world_inspector.py` using a real scroll region instead of the
  fixed-row-count workaround), plus its README entry.
- `docs/design/scripting.md` — document `MOSAIC_PANELS` and the `imgui_*` surface
  alongside the existing Plugin SDK section.
- `docs/project/next-steps.md` — mark this slice done.

## Non-goals / follow-ups

- **Custom asset importers** — Plugin SDK slice 3, its own future spec.
- **Native `.so`/`.dll` plugins** — unchanged from slice 1's scope notes.
- **A larger ImGui surface** (tables, trees, drag/drop, images, custom drawing) — the
  nine-global v1 set covers the common cases; grow it later against real demand rather
  than speculatively now.
- **Per-panel enable/disable persistence, panel ordering/docking presets** — a
  plugin-registered panel behaves exactly like any other `Panel` in `StudioLayout`
  (open/closed state, docking) — no new persistence mechanism beyond what already
  exists for the built-in panels.
- **Sharing `run_string`/`get_global`/`call_function` scaffolding** between
  `PythonScriptEngine` and `StudioPluginScriptEngine` — deliberately deferred (see
  "Avoiding duplication" above); revisit if a third `ScriptEngine` implementation ever
  appears.
