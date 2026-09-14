# Plugin SDK — Custom Asset Importers — Design

## Goal (Phase 5, Tool Ecosystem pillar — Plugin SDK slice 3 of 3)

Let a project-local plugin register a custom asset importer: a mapping from a file
extension to a Python function that turns a source file into a Mosaic asset. This is
the third and final planned extension point of the Plugin SDK, after slice 1 (custom
menu items) and slice 2 (custom Studio panels).

## Current state

- `engine/assets/asset_registry.hpp` — a lazy metadata→resource table
  (`AssetID` → `AssetMetadata` + a type-erased `shared_ptr<void>` loader). No concrete
  resource types live here; each asset kind (image, audio, font, script) has its own
  cook/uncook pair in `app/asset/*_cook.hpp`.
- `app/asset/asset_io.hpp`'s `import_asset(project_dir, source_file)` is the one real
  import path today: a hardcoded if/else chain on file extension —
  `.py`→script, `.wav`→audio, `.ttf`/`.otf`→font, **anything else**→attempt a PPM
  image decode. It writes a cooked blob (`assets/<stem>.<ext>`) plus a sibling
  `assets/<stem>.toml` (`AssetMetadata`), refusing to overwrite an existing stem.
- The same file's `load_project_assets(registry, assets_dir)` scans `assets/*.toml`
  at startup and registers each back into the `AssetRegistry`, dispatching on
  `meta.type` with the mirror-image if/else chain (script/audio/font, **else assume
  image**).
- `import_asset` is only reachable via the CLI (`mosaic import <project> <file>`,
  `app/run/import.cpp`) — a separate process invocation from `mosaic editor`. Studio's
  Assets panel (`draw_assets`) is **read-only**: it lists whatever is already in the
  registry; there is no import trigger anywhere in the editor UI.
- **Tier boundary**: `app/asset/asset_io.hpp` lives in the `app` tier;
  `studio::PluginHost` lives in the `studio` tier. Per
  `docs/architecture/boundaries.md`, `app` and `studio` are sibling tiers — neither
  depends on the other. `import_asset` cannot reuse `PluginHost`; this slice needs its
  own app-tier plugin-loading mechanism.

Per discussion, this slice is scoped to the **backend + CLI only**: no Studio UI
import trigger (drag-drop / an Import menu item stays future work), and imported data
is **register-only** — no new gameplay-script getter to read it back at runtime (also
future work, on demand).

## Design

### `ImportPluginHost` — a new, app-tier plugin loader

A new class, `mosaic::app::ImportPluginHost` (`app/asset/import_plugins.hpp`/`.cpp`),
structurally parallel to `studio::PluginHost` but deliberately smaller: it discovers
`plugins/*.py` files and gives each its own `ScriptEngine`, exactly like `PluginHost`
does — but it **never calls `bind_world`/`bind_commands`**. An importer has no reason
to touch `World`, entities, or undo/redo; skipping the bind means
`get_component`/`set_component`/`create_entity`/etc. simply don't exist as globals in
an importer plugin's namespace (a deliberate restriction, documented for plugin
authors). `__builtins__` (including `open()`, `json`, `csv`, and the rest of the
standard library) is still installed unconditionally by `PythonScriptEngine`'s
constructor, independent of `bind_world` — confirmed by reading
`modules/script_python/python_script_engine.cpp`. So an importer plugin reads its own
source file directly with plain Python file I/O; it needs no new "read a file" global.

```cpp
namespace mosaic::app
{
// One registered importer, read out of a plugin's MOSAIC_IMPORTERS dict.
struct PluginImporter
{
    std::string plugin_name;   // filename stem, for error messages
    std::string extension;     // e.g. ".fbx" (leading dot, matches
                                // std::filesystem::path::extension())
    std::string function_name; // e.g. "import_fbx"
};

class ImportPluginHost
{
public:
    using EngineFactory = std::function<std::unique_ptr<mosaic::engine::script::ScriptEngine>()>;

    explicit ImportPluginHost(EngineFactory factory);

    // Same all-or-nothing per-file load semantics as PluginHost::load: a malformed
    // MOSAIC_IMPORTERS dict discards that whole file, collected as an error string,
    // never blocking sibling files. A missing/non-directory `directory` returns an
    // empty vector (not an error).
    std::vector<std::string> load(const std::string& directory);

    const std::vector<PluginImporter>& importers() const;
    const PluginImporter* find_importer(const std::string& extension) const;

    struct Result { std::optional<std::string> blob; std::string error; };
    Result invoke(const PluginImporter& item, const std::string& source_path);

private:
    /* ... */
};
} // namespace mosaic::app
```

A plugin registers importers with:

```python
MOSAIC_IMPORTERS = {".csv": "import_csv"}

def import_csv(source_path):
    with open(source_path, newline="") as f:
        rows = list(csv.reader(f))
    return json.dumps(rows)
```

`MOSAIC_IMPORTERS` mirrors `MOSAIC_MENU_ITEMS`/`MOSAIC_PANELS`'s shape (a dict, values
are function-name strings) but keyed by **extension** instead of a UI label, since
there's no menu/panel surface here. `load()` parses it exactly the way `PluginHost`
parses its two dicts: not a dict → error, a non-string value → error, either
discarding the whole file.

### Capturing a return value without touching `ScriptEngine`

`ScriptEngine::call_function(name)` is zero-argument and discards its Python return
value (only `output`, captured stdout, survives) — correct for menu items and panels,
which are fire-and-forget. An importer function needs to **receive** the source path
and **return** the transformed blob, neither of which the existing interface
supports. Extending `ScriptEngine` itself (a new pure virtual both
`PythonScriptEngine` and `StudioPluginScriptEngine` would have to implement) is
possible — slice 2's Task 1 set that precedent — but unnecessary here: `invoke()`
gets the same effect by composing three methods the interface **already** has:

```cpp
engine->set_global("__mosaic_import_source__", Value(source_path));
const auto run = engine->run_string(
    "__mosaic_import_result__ = " + item.function_name + "(__mosaic_import_source__)\n");
if (!run.ok) { return {std::nullopt, run.error}; }
const auto result = engine->get_global("__mosaic_import_result__");
if (!result.has_value() || !result->is<std::string>())
{
    return {std::nullopt, "'" + item.function_name + "' must return a string"};
}
return {result->as<std::string>(), ""};
```

`set_global` marshals `source_path` through the `#10` Value DOM (no string-injection
risk); `run_string`'s existing `py::exec` try/catch already reports a raised
exception, an undefined name, or a syntax error as `run.error` with no new error
handling to write. `__mosaic_import_source__`/`__mosaic_import_result__` are
plain module-level globals — the double-underscore prefix signals "internal
plumbing" (no name-mangling risk outside a class body) the same way `MOSAIC_`-prefixed
dicts signal "registration surface"; a plugin should not otherwise use these two
names. This keeps `ScriptEngine`, `PythonScriptEngine`, and
`StudioPluginScriptEngine` completely untouched by this slice.

### `import_asset` dispatch order

Today, everything that isn't `.py`/`.wav`/`.ttf`/`.otf` falls into the PPM branch by
default. That branch becomes a genuine last resort:

1. `.py`/`.wav`/`.ttf`/`.otf` — unchanged, built-ins always win outright.
2. Otherwise, load `<project_dir>/plugins` via a fresh `ImportPluginHost` and look up
   the extension. If a plugin claims it: invoke, and on success write the blob as
   `assets/<stem>.data` with `asset_type = extension` (leading dot stripped, e.g.
   `.csv` → `"csv"`). Plugin-loading is scoped to this branch only — a `.wav` import
   never pays the cost of scanning `plugins/`.
3. Otherwise, attempt the PPM decode (unchanged).

Only the if/else chain that computes `blob_bytes`/`blob_ext`/`asset_type` changes;
everything downstream of it (stem/assets-dir resolution, the existing
stem-collision refusal, writing the blob then the `.toml`, never leaving a
half-written asset on a metadata-write failure) is shared, untouched code that
already applies uniformly to whichever branch ran.

**Reserved-type guard**: if the extension-derived `asset_type` collides with one of
the four built-in type strings (`script`, `audio`, `font`, `texture` — e.g. a plugin
registering `.texture`), `import_asset` refuses with a clear error instead of writing
metadata that `load_project_assets` would later misroute into the image decoder.

**Duplicate extension across plugins**: `find_importer` is a linear first-match scan,
same precedent as `PluginHost::find_panel` — the first plugin (by load order) to
register a given extension wins; documented, not treated as an error.

**`plugins/` resolution**: `<project_dir>/plugins`, not CWD-relative. `import_asset`
already takes `project_dir` explicitly (unlike the editor, which relies on an
undocumented CWD assumption — see `docs/design/scripting.md`'s existing "known gap"
section), so this is a deliberate improvement, not a replication of that gap.

### `load_project_assets` fix

The catch-all currently *assumes* anything that isn't script/audio/font is an image —
that assumption breaks the moment a plugin-imported asset exists (it would attempt a
PPM decode on `.data` bytes and log "corrupt cooked image"). It becomes explicit:

```cpp
if (meta.type == "script") { /* unchanged */ }
else if (meta.type == "audio") { /* unchanged */ }
else if (meta.type == "font") { /* unchanged */ }
else if (meta.type == "texture") { /* unchanged image decode, renamed from the
                                       implicit else */ }
else
{
    // Generic opaque blob -- covers every plugin-imported type. Read the sibling
    // .data file and register it as a raw string resource; nothing beyond the
    // AssetRegistry entry existing is asserted about its content, since only the
    // plugin that produced it understands the format.
    const auto blob = load_scene_file(entry.path().parent_path() /
                                      (entry.path().stem().string() + ".data"));
    if (!blob) { warn+continue; }
    registry.register_resource<std::string>(meta, std::make_shared<std::string>(*blob));
    ++count;
}
```

Register-only, per the scoping decision above: no new gameplay-script global reads it
back. `AssetRegistry::load<std::string>(id)` already works today for any C++ caller
that wants the raw bytes — no new engine-side API needed for that either.

## Testing

- **`tests/unit/app/import_plugins_test.cpp`** (new): `ImportPluginHost::load`
  discovers `MOSAIC_IMPORTERS`; a non-dict/non-string-value dict is a load error and
  discards the whole file; `invoke` marshals the source path in and the string
  return value out; `invoke` on a function that raises, returns a non-string, or
  doesn't exist each produce a descriptive error; a missing/non-directory `directory`
  returns no errors and no importers.
- **`tests/unit/app/asset_import_test.cpp`** (extended): `import_asset` with a
  temp-directory `plugins/custom.py` registering a fake extension imports it as
  `assets/<stem>.data` + `.toml` with the expected `type`; the reserved-type-collision
  guard refuses a plugin extension that maps to `"texture"`; an extension with no
  matching plugin AND no PPM match still fails the same way it does today (PPM
  decode error, unchanged).
- **`load_project_assets`** gains a test proving a generic (`type` not in
  {script,audio,font,texture}) `.toml`+`.data` pair round-trips as a raw string
  resource, and a regression test proving `type == "texture"` still decodes as an
  image (guards against the implicit-else becoming an explicit branch incorrectly).

## Files

- Create: `app/asset/import_plugins.hpp`, `app/asset/import_plugins.cpp`
- Create: `tests/unit/app/import_plugins_test.cpp`
- Modify: `app/asset/asset_io.hpp` — `import_asset`'s dispatch order + reserved-type
  guard; `load_project_assets`'s explicit `texture` branch + generic-blob fallback.
- Modify: `tests/unit/app/asset_import_test.cpp` — new cases per Testing above.
- Modify: `app/CMakeLists.txt` — add `asset/import_plugins.cpp` to `mosaic_app`'s
  source list, alongside the existing `asset/font_cook.cpp` (confirmed: every other
  file under `app/asset/` is header-only; `import_plugins.cpp` is the second `.cpp`
  there, same pattern `font_cook.cpp` already established).
- Create: `tools/example_plugins/csv_importer.py` — the `MOSAIC_IMPORTERS` example
  shown above (CSV → JSON text), demonstrating the mechanism end to end.
- Modify: `tools/example_plugins/README.md` — new row.
- Modify: `docs/design/scripting.md` — a new "Custom asset importers (slice 3)"
  subsection, mirroring slice 1/2's sections.
- Modify: `docs/project/next-steps.md` — new dated entry.

## Non-goals / follow-ups

- **A Studio UI import trigger** (drag-drop onto the Assets panel, a File > Import
  menu item) — the Assets panel stays read-only; explicitly deferred per this slice's
  scoping discussion.
- **A gameplay-script getter for imported blobs** (e.g. `load_asset_text(id)`) —
  register-only for now; add it against real demand, the same "grow later" discipline
  slice 2 applied to the `imgui_*` widget set.
- **Binary/bytes importer payloads** — v1 is text-only (the return value must be a
  Python `str`); true binary cooking (images, audio, meshes) stays native-code
  territory, same non-goal slice 1/2 already carried forward.
- **Native `.so`/`.dll` plugins** — unchanged from slice 1's scope notes.
