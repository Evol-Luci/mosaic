# `.py`-as-asset — Design

## Problem

A `ScriptComponent` today carries either inline `source` or an absolute `.py`
`path` (mtime hot-reload, from the earlier slice). Neither is
project-portable: an inline blob duplicates code across entities, and an
absolute path breaks when the project moves or is bundled. There is no way to
**import a `.py` into a project once and reference it from many entities by a
stable `AssetID`** — the way textures already work (`Material.texture` is an
`AssetID`).

## Goal

Import a `.py` file into a project as a first-class asset (cooked blob +
`AssetMetadata`, `type = "script"`), load it into the `AssetRegistry` at
startup, and let a `ScriptComponent` reference it by `AssetID`. `ScriptSystem`
resolves the id → source through the registry and runs it. Project-relative:
the script lives under `<project>/assets/` and is referenced by a stable id,
not a machine path.

Live re-import / OS file-watching is **out of scope** (a separate roadmap
item); an asset-referenced script resolves once and caches.

## Architecture

`ScriptSystem` (`engine/script/`) and `AssetRegistry` (`engine/assets/`) are the
**same tier**, so `ScriptSystem` may depend on the registry directly — no new
seam or callback indirection needed. A script asset's "resource" is just its
text, held in a tiny neutral `ScriptSource` value type.

```
authoring:  mosaic import <proj> foo.py
              -> assets/foo.script  (verbatim source blob)
              -> assets/foo.toml    (AssetMetadata{ type="script", uuid=<new> })

startup:    load_project_assets(registry, <proj>/assets)
              -> register_resource<ScriptSource>(meta, {text})   // type=="script"

runtime:    ScriptComponent{ script = <uuid> }
              -> ScriptSystem: registry.load<ScriptSource>(uuid).text -> compile+run
```

### 1. `ScriptSource` resource — `engine/script/script_source.hpp` (new)

```cpp
#pragma once
#include <string>
namespace mosaic::engine::script
{
// A script asset's payload: the Python source text. Registered in the
// AssetRegistry under a script AssetID; ScriptSystem resolves + runs it.
struct ScriptSource { std::string text; };
} // namespace mosaic::engine::script
```

### 2. `ScriptComponent` reference — `engine/script/script_component.hpp`

Add a reflected `AssetID script` field (nil = unset):

```cpp
#include "foundation/serialization/reference.hpp"
...
struct ScriptComponent
{
    std::string                                 source; // inline (fallback)
    std::string                                 path;   // .py file (fallback)
    mosaic::foundation::serialization::AssetID  script; // script asset (nil = unset)
};
// reflect: member("source"), member("path"), member("script")
```

### 3. `ScriptSystem` resolution — `engine/script/script_system.hpp`

`ScriptSystem` gains an **optional** `AssetRegistry*` (defaulted ctor param,
after `host`). Resolution precedence in `update()` becomes **asset id → path →
inline**:

```
if (!sc.script.is_nil() && registry_) {
    auto res = registry_->load<ScriptSource>(sc.script);
    desired = res ? res->text : entry.source;   // missing asset -> keep last-good
    entry.path.clear();
}
else if (!sc.path.empty()) { ...existing file/mtime branch... }
else { ...existing inline branch... }
```

The rest of the recompile-on-change / per-entity-engine logic is unchanged.
`AssetRegistry::load` is non-const (it caches), so `registry_` is a mutable
pointer.

### 4. Import + load pipeline — `app/asset/asset_io.hpp`

- **`import_asset`**: dispatch on the source extension. `.py` →
  write `<stem>.script` (the source text verbatim, via `save_scene_file`) +
  `<stem>.toml` with `AssetMetadata{ type="script", uuid=random, version=1,
  source=<src> }`. Keep the existing PPM/image path for other extensions. Same
  "refuse rather than overwrite" + "no half-written asset" guarantees.
- **`load_project_assets`**: branch on `meta.type`. `"script"` → read the
  sibling `<stem>.script` blob, `register_resource<ScriptSource>(meta, {text})`.
  `"texture"` (or default) → the existing image path. A missing/unreadable
  script blob logs a warning and is skipped (one bad asset never aborts
  startup), matching the image path.

### 5. Wiring — `app/run/demo_scene.hpp`

`setup_runtime` already receives the `AssetRegistry&`. Pass it to the system:

```cpp
world.add_system<ScriptSystem>(reflection, std::move(script_factory),
                               std::move(host), &registry);
```

`main.cpp` already calls `load_project_assets` around `setup_runtime` (before
`pump.run`), so a windowed **and** headless run resolves script assets before
the first tick — no `main.cpp` change beyond what the headless-run slice
already did.

## Data flow / precedence

An entity referencing a script asset ignores `path`/`source`; a nil `script`
falls back to the existing behaviour. So old scenes are unaffected, and the new
field is purely additive.

## Testing (headless)

1. **Reflection** (`script_component` coverage): a `ScriptComponent` with a
   `script` AssetID serializes/round-trips (the field reflects).
2. **`ScriptSystem` resolves an asset** (`script_system_test`): register a
   `ScriptSource` (a script that moves a Transform) in an `AssetRegistry`;
   attach a `ScriptComponent{script=id}`; construct `ScriptSystem(types,
   factory, {}, &registry)`; `world.tick` → the entity moved. Also: a nil-script
   component still runs its inline `source` (precedence/fallback).
3. **Import** (`asset_io`/import test): `import_asset(tmp_project, foo.py)`
   returns an id and writes `assets/foo.script` + `assets/foo.toml`
   (`type=="script"`); a second import of the same stem refuses (nullopt).
4. **Load** (`asset_io` test): after writing a script asset, `load_project_assets`
   registers exactly one `ScriptSource` resolvable by its uuid with the right
   text.
5. **End-to-end (optional integration)**: import a `.py`, `load_project_assets`,
   attach `ScriptComponent{script=id}`, `setup_runtime` (which wires the
   registry), tick → runs. Covered by 2+3+4 composing; include if cheap.

All headless (temp dirs for filesystem tests, mirroring `project_create_test` /
`bundle_test`). Manual acceptance: `mosaic import <proj> foo.py` then a headless
`mosaic run` of a scene referencing it.

## Out of scope (YAGNI / follow-ups)

- OS file-watcher / live re-import of script assets (separate roadmap item).
- Editor UI to assign a script asset (the Inspector's `Assign selected` already
  works generically for `AssetID` fields — a nice free consequence, not built
  here).
- Dependency tracking / bundling changes beyond the existing `*.toml` scan
  (bundling already copies `assets/`).
