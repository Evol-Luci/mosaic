# Example plugins

Nine small, self-contained Python plugins for Mosaic Studio's Plugin SDK (Phase 5,
slices 1-2 — discovery/lifecycle + custom menu items + custom Studio panels; see
[docs/design/scripting.md](../../docs/design/scripting.md)). Together they exercise
every global currently available to a plugin, and every one is independently useful as
a copy-paste starting point.

| File | Menu item(s) | Demonstrates |
|---|---|---|
| `hello.py` | Say Hello | `create_entity`, `add_component`, `set_component`, `is_valid` |
| `world_inspector.py` | Show World Info | `entity_count`, `entities()`, `has_component`, `get_component`, `component_names` — also shows how to lay out a multi-row on-screen list, since `UiText` has no built-in wrapping/scrolling |
| `world_inspector_panel.py` | (registers a panel, not a menu item) "World Inspector" | `MOSAIC_PANELS`, `imgui_text`, `imgui_separator`, `imgui_begin_child`/`imgui_end_child` -- the panel-API counterpart to `world_inspector.py`, with real scrolling instead of a fixed row cap |
| `greet_named_entities.py` | Greet Named Entities | `entities_with`, `get_component`, `set_component` on an entity's existing components |
| `undo_demo.py` | Nudge Marker Right / Undo Last Change / Redo Last Undo | undoable `set_component` (routed through the editor's `CommandStack`), `undo()`, `redo()`, `undo_depth()` — three menu items registered from one file |
| `spawn_manager.py` | Spawn Debug Cube / Despawn Debug Cube | `create_entity` + multi-component `add_component`, `destroy_entity` |
| `toggle_panel.py` | Toggle Debug Panel | `has_component`, `add_component`, `remove_component` |
| `event_demo.py` | Emit Test Event | `emit(name, payload)` |
| `broken_example.py` | (never loads) | a deliberate Python syntax error — demonstrates `PluginHost`'s error-isolation guarantee: it fails to load and logs an error to Studio's Console panel, without blocking any other plugin in the same directory |

## Using them

Mosaic Studio loads every `*.py` file in a project's `plugins/` directory (see the
"Where the directory lives" section of `docs/design/scripting.md` for exactly how that
path resolves today, and its known gap). Copy whichever of these files you want into
your own project's `plugins/` directory, or symlink them if you'd like to pick up
future edits to this repo automatically:

```bash
ln -s /path/to/mosaic/tools/example_plugins/hello.py <your-project>/plugins/hello.py
```

Click **Reload Plugins** in Studio's **Plugins** menu after adding or editing a file —
plugins are scanned once at launch and are not hot-reloaded.

## A note on component field shapes

Nested reflected types (`Vec2`, `Vec3`, `Quat`, ...) marshal through `get_component`/
`set_component` as **dicts keyed by field name** — e.g. a `Vec3` is
`{"x": ..., "y": ..., "z": ...}`, never a `[x, y, z]` array. `AssetID` fields are plain
UUID strings (`AssetID::to_string()`'s format, e.g.
`"f0000000-0000-0000-0000-000000000000"`). `set_component` also supports **partial**
updates — any key you omit from the dict is left at its current value, not reset to a
default — so you only need to specify the fields you're actually changing.
