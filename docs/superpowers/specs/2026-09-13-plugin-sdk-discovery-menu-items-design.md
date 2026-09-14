# Plugin SDK — Discovery/Lifecycle + Custom Menu Items — Design

## Goal (Phase 5, Tool Ecosystem pillar — first Plugin SDK slice)

Deliver the first slice of the roadmap's Plugin SDK: a project-local `.py` plugin can
register a Studio menu item that, when clicked, runs plugin code with the same World
access gameplay scripts already have (get/set component, entity create/destroy/query,
undo/redo). This is stage 1 of "Python-based, via the existing embedded interpreter"
(native `.so`/`.dll` dynamic-library plugins are a separate, later, much heavier
sub-project — explicitly out of scope here). Within the Plugin SDK itself, this is
slice 1 of 3 (discovery/lifecycle + menu items → custom Studio panels → custom asset
importers); the latter two are separate future specs.

No plugin infrastructure exists in the codebase today — `roadmap.md` lists "plugin
system" under Phase 0, but nothing was ever built. This slice is the first of it.

## Design

### Approach

A new Studio-tier `PluginHost` (`studio/plugin_host.hpp`/`.cpp`) discovers `.py` files
in a project-local `plugins/` directory at Studio launch, constructs one `ScriptEngine`
per plugin file (via the same injected `EngineFactory` pattern `ScriptConsoleModel`/
`ScriptEditorModel` already use — no new wiring needed in `app/run/editor.cpp`), binds
it to the current `World`/`ComponentReflection`/`CommandStack` exactly like those two
classes do, and runs the plugin's source through it.

A plugin registers menu items declaratively — a module-level dict, not a callback
registration function:

```python
# plugins/hello.py
MOSAIC_MENU_ITEMS = {"Say Hello": "on_say_hello"}

def on_say_hello():
    for e in entities_with("Name"):
        print("hello,", get_component(e, "Name")["value"])
```

`get_component`/`entities_with`/etc. are the exact same globals `PythonScriptEngine`
already installs via `bind_world` (`engine/script`'s existing gameplay-script surface,
reused verbatim) — a plugin author who has already written a `ScriptComponent` for this
engine already knows this API.

### Why not store a Python callable in C++

The obvious design — `register_menu_item(label, callback)` where `callback` is a Python
callable C++ holds onto — was rejected. Studio (`studio/`) does not, anywhere today,
depend on pybind11 or `Python.h`; it only ever holds `std::unique_ptr<ScriptEngine>`
behind the neutral interface, injected via a factory (`ScriptConsoleModel::EngineFactory`,
`ScriptEditorModel::EngineFactory`). Storing a raw `py::object` in `PluginHost` would
require `studio/` to link `modules/script_python` directly, breaking the boundary the
whole editor tier currently respects (per `docs/architecture/boundaries.md`'s Scripting
Boundary: "`Python.h` and `pybind11` are walled off inside `modules/script_python/`").

Instead, `ScriptEngine` gains two small, genuinely neutral primitives — general enough
that any future consumer could use them, not menu-item-specific:

```cpp
// Reads a global variable's current value out of this engine's namespace, marshaled
// through the same #10 Value DOM set_global writes with. nullopt if the name is absent
// or its value isn't representable in the Value DOM (e.g. a function or class object).
virtual std::optional<mosaic::foundation::serialization::Value>
    get_global(const std::string& name) = 0;

// Calls an already-defined zero-argument function in this engine's namespace by name
// (e.g. one declared by a prior run_string). Returns the same ScriptResult shape as
// run_string: ok/error/captured stdout. ok=false with a "not callable"/"not defined"
// error if name doesn't resolve to a callable.
virtual ScriptResult call_function(const std::string& name) = 0;
```

`PluginHost` never touches pybind11: it calls `run_string` (existing), `get_global`
(new) to read `MOSAIC_MENU_ITEMS` back as a `Value::Table` (the dict marshals through
the existing `value_to_py`/`py_to_value` table case — already round-trip-tested), and
`call_function` (new) on menu-item click. The Studio/pybind11 boundary is fully
preserved; both new methods live in the same neutral header as everything else.

### `PythonScriptEngine` implementation of the two new methods

```cpp
std::optional<mosaic::foundation::serialization::Value>
get_global(const std::string& name) override
{
    if (!globals_.contains(name)) { return std::nullopt; }
    try { return py_to_value(globals_[py::str(name)]); }
    catch (const std::exception&) { return std::nullopt; } // e.g. a function/class object
}

ScriptResult call_function(const std::string& name) override
{
    ScriptResult result;
    // ... same stdout-capture scaffolding run_string already uses ...
    try
    {
        if (!globals_.contains(name) || !py::isinstance<py::function>(globals_[py::str(name)]))
        {
            result.error = "'" + name + "' is not a defined function";
            return result;
        }
        globals_[py::str(name)]();
        result.ok = true;
    }
    catch (const py::error_already_set& e) { result.error = e.what(); }
    return result;
}
```

(The real implementation factors the existing stdout-capture RAII helper `run_string`
already uses into something both methods can share — an implementation detail, not a
new design decision.)

### `studio/plugin_host.hpp`/`.cpp`

```cpp
namespace mosaic::studio
{
struct PluginMenuItem
{
    std::string plugin_name; // e.g. "hello" (filename stem) -- for error messages
    std::string label;       // e.g. "Say Hello"
    std::string function_name; // e.g. "on_say_hello"
};

class PluginHost
{
public:
    using EngineFactory = std::function<std::unique_ptr<mosaic::engine::script::ScriptEngine>()>;

    explicit PluginHost(EngineFactory factory) : factory_(std::move(factory)) {}

    // Scans `directory` for *.py files (non-recursive), running each through a fresh
    // ScriptEngine bound to world/types/commands. Clears any previously loaded plugins
    // first (this is also what "Reload Plugins" calls). Load errors (a file that
    // throws while executing, or whose MOSAIC_MENU_ITEMS isn't a string->string table)
    // are collected into the returned vector, never thrown -- one broken plugin must
    // not prevent the others from loading.
    std::vector<std::string> load(const std::string& directory,
                                  mosaic::engine::World& world,
                                  const mosaic::engine::reflection::ComponentReflection& types,
                                  mosaic::foundation::commands::CommandStack& commands);

    // Every registered menu item across all successfully loaded plugins, in load order
    // then declaration order within MOSAIC_MENU_ITEMS.
    const std::vector<PluginMenuItem>& menu_items() const { return menu_items_; }

    // Invokes the item's function on its plugin's engine. Returns the ScriptResult
    // (ok/error/output) so the caller can report a failure without crashing.
    mosaic::engine::script::ScriptResult invoke(const PluginMenuItem& item);

private:
    EngineFactory factory_;
    struct LoadedPlugin
    {
        std::string name;
        std::unique_ptr<mosaic::engine::script::ScriptEngine> engine;
    };
    std::vector<LoadedPlugin> plugins_;
    std::vector<PluginMenuItem> menu_items_;
};
} // namespace mosaic::studio
```

`load()`'s per-file sequence: read the file; `factory_()` a fresh engine;
`bind_world(world, types)`; `bind_commands(commands)`; `run_string(source)` — a
`ScriptResult.ok == false` here is a load error, collected and skipped (the plugin's
engine is dropped, no menu items are read from it); otherwise `get_global(
"MOSAIC_MENU_ITEMS")` — absent is fine (a plugin with no menu items, e.g. a future
panel/importer-only plugin from slices 2/3, is not an error); present-but-not-a-table,
or a table with a non-string value, is a load error; otherwise each `(label,
function_name)` pair becomes a `PluginMenuItem`.

### Studio integration (`studio/studio_app.hpp`/`.cpp`)

`StudioApp` gains a `PluginHost plugin_host_;` member, constructed with the same
`script_factory` already threaded into `script_console_`/`script_editor_` — no new
constructor parameter, no new `editor.cpp` wiring. On launch (first `draw_ui` call,
mirroring how `demo_entity_` is lazily discovered on first use), `plugin_host_.load(
"<project>/plugins", world, reflection, command_stack)` runs once; a "Reload Plugins"
menu item re-runs it. A new top-level "Plugins" menu (after "File", before the Play/Stop
toolbar group) lists every `menu_items()` entry as a clickable `ImGui::MenuItem`; a
click calls `plugin_host_.invoke(item)`, and a `!ok` result logs
`log::error("plugin", "{}: {}", item.plugin_name, result.error)` — the same
Console-panel-visible path every other subsystem's errors already go through. Load
errors from `load()`'s returned vector are logged the same way, once, right after
loading.

There is no tracked "project root" string anywhere in `editor.cpp`/`StudioApp` today —
`mosaic editor` is simply run from inside the project directory, and every existing
path (scenes, assets) resolves relative to the process's current working directory.
`plugins/` follows the identical convention: `plugin_host_.load("plugins", ...)`, a
literal relative path, no new path-resolution concept introduced.

## Testing

- **`tests/unit/script/python_script_engine_test.cpp`** (extended): `get_global`
  returns the marshaled `Value` for a set variable (string, int, table/dict cases);
  returns `nullopt` for an undefined name and for a name bound to a function/class
  (unmarshalable); `call_function` on a defined zero-arg function returns `ok=true` and
  runs it (assert a side effect, e.g. a global counter incremented); `call_function` on
  an undefined name, and on a name bound to a non-callable, both return `ok=false` with
  a descriptive error; `call_function` on a function that raises returns `ok=false` with
  the exception message (mirrors `run_string`'s existing error-capture test coverage).
- **`tests/unit/studio/plugin_host_test.cpp`** (new): headless, using a real temp
  directory (same style as `scene_io_test.cpp`'s `tmpnam`-based fixtures) with `.py`
  files written directly by the test, and a real `PythonScriptEngine` via
  `create_python_script_engine()` as the factory (no mock — `ScriptEngine` is already
  proven correct by its own suite; this test proves `PluginHost`'s discovery/dispatch
  logic against the real engine, the same way `ScriptConsoleModel`'s existing tests do).
  Cases: a single valid plugin's menu item appears in `menu_items()` and `invoke()`
  actually mutates World state (e.g. asserts a component value after invoking a plugin
  function that calls `set_component`); two plugins in the same directory both load,
  each keeping its own isolated globals (a name defined in one is not visible to the
  other — proves per-file engine isolation); a plugin file that raises at top level is
  skipped (its menu items, if it had gotten that far, do not appear) and its error is
  in the returned load-error vector, while a sibling valid plugin in the same directory
  still loads correctly; a plugin whose `MOSAIC_MENU_ITEMS` is missing loads with zero
  menu items and no error; a plugin whose `MOSAIC_MENU_ITEMS` is the wrong shape (not a
  table, or a table with a non-string value) is a load error; calling `load()` a second
  time (simulating "Reload Plugins") replaces the previous plugin set entirely (an item
  from a since-deleted-on-disk file no longer appears).
- **Manual**: none required — this slice's logic is fully headlessly testable (no
  Blender, no GUI-only behavior; `ImGui::MenuItem` rendering itself is the only
  untested-by-this-suite part, consistent with every other ImGui-glue code in Studio).

## Files

**New**
- `studio/plugin_host.hpp` / `studio/plugin_host.cpp`
- `tests/unit/studio/plugin_host_test.cpp`

**Modified**
- `engine/script/script_engine.hpp` — add `get_global`, `call_function`.
- `modules/script_python/python_script_engine.cpp` — implement both.
- `tests/unit/script/python_script_engine_test.cpp` — extended per Testing above.
- `studio/studio_app.hpp` / `studio/studio_app.cpp` — `PluginHost` member (reusing the
  existing `script_factory` constructor parameter), "Plugins" menu, lazy first-frame
  load + "Reload Plugins" action.
- `studio/CMakeLists.txt` — add `plugin_host.cpp` to `mosaic_studio`.
- `tests/CMakeLists.txt` — register the two new/extended test files (only
  `plugin_host_test.cpp` is actually new as a file; the script-engine test file is
  extended in place).
- `docs/project/next-steps.md` — mark this slice done.

## Non-goals / follow-ups

- **Native `.so`/`.dll` dynamic-library plugins** — a separate, later, much heavier
  sub-project (ABI stability, cross-platform loader, sandboxing/crash isolation);
  explicitly deferred per the "both, in stages" decision.
- **Custom Studio panels** and **custom asset importers** — Plugin SDK slices 2 and 3,
  each their own future spec, building on this slice's discovery/lifecycle mechanism
  and the same per-plugin-engine pattern.
- **Per-plugin enable/disable, manifest/versioning, a plugin marketplace** — every
  `.py` file found loads unconditionally; no persisted on/off state. Reasonable once
  there's an actual ecosystem of installed plugins to manage, not before.
- **User-global plugins** (outside any one project) — project-local only for this
  slice, matching how scenes/assets/scripts are already project-scoped.
- **Live file-watching / hot-reload** — "Reload Plugins" is a manual re-scan; no
  filesystem watcher.
- **Multi-argument or async menu-item functions** — `call_function` is zero-argument,
  synchronous, blocking the UI thread for its duration (matches how the Script Console
  already runs scripts synchronously — no new threading model introduced).
- Package manager, templates/project generators, documentation tooling — the rest of
  Phase 5, sequenced after the full Plugin SDK (all three slices) per the agreed order.
