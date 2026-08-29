# Tools T3 — `mosaic create` + project model (design)

**Decomposition:** [tools-workflow-decomposition](2026-08-29-tools-workflow-decomposition.md) (slice T3).
**Builds on:** T2 (scene load/save + `populate_world`), #10 serialization (TOML backend), #19
scene, #35 app/CLI, #20 asset registry.
**Date:** 2026-08-29.

## Summary

Turn Mosaic from *a tool you point at a scene file* into *a tool you use on your own project*.
A **project** is a directory with a TOML manifest + a `scenes/` folder (a starter scene) + an
`assets/` folder. `mosaic create <name>` scaffolds one; `mosaic editor <project>` /
`mosaic run <project>` open its default scene. The trick: T3 **resolves a project down to a
scene path**, then hands that path to T2's existing `populate_world` — so opening a project
reuses all of T2's load/render machinery unchanged.

## Scope

**In:**
- A reflected `ProjectManifest` (`name`, `default_scene`) round-tripped through the #10 TOML
  backend.
- `create_project(dir, reflection)` — scaffolds `<dir>/{mosaic.toml, scenes/main.json, assets/}`;
  the starter scene is T2's default demo serialized to JSON.
- `resolve_startup_scene(path)` — the "smart positional": a project dir → its default scene; a
  `.json` file → itself; else an error.
- A `mosaic create <name>` subcommand; an optional positional `<path>` on `run`/`editor` that
  resolves through `resolve_startup_scene` into `options.scene`.

**Deferred (later slices / YAGNI):**
- Reading per-project `assets/` (that is **T5**'s importer/cooker). T3's `assets/` is scaffolding
  for later.
- Multi-scene projects / in-editor scene switching; manifest fields beyond `name` +
  `default_scene` (version/author are speculative); project detection beyond a `mosaic.toml`
  file; project templates beyond the single demo starter.

## Architecture

Project/CLI code lives in the **`app/`** tier (per the pillar's tier rule), in a new
`app/project/`.

### 1. Project model + manifest (`app/project/project.hpp`)

```cpp
namespace mosaic::app
{
inline constexpr const char* kManifestName = "mosaic.toml";
inline constexpr const char* kScenesDir    = "scenes";
inline constexpr const char* kAssetsDir    = "assets";
inline constexpr const char* kStarterScene = "scenes/main.json"; // manifest default_scene

// The project manifest (mosaic.toml), reflected so it round-trips through the #10 TOML backend
// exactly like AssetMetadata. Minimal on purpose.
struct ProjectManifest
{
    std::string name;          // project name (defaults to the directory basename)
    std::string default_scene; // project-relative path to the scene opened by default
};

template <class V>
void reflect(V& v, ProjectManifest& m)
{
    v.member("name", m.name);
    v.member("default_scene", m.default_scene);
}
} // namespace mosaic::app
```

Serialized with `to_toml(to_value(manifest))`; parsed with `from_value(from_toml(text))` (the
same `to_value`/`from_value` reflection bridge `AssetMetadata` uses).

### 2. Project I/O + resolution (`app/project/project_io.hpp`)

Uses `<filesystem>`, the TOML backend, and T2's `scene_io` + `demo_scene`/`scene` helpers.

```cpp
// Load + parse a project's manifest from a project directory. std::nullopt if the manifest is
// missing or unparseable (caller reports).
std::optional<ProjectManifest> load_manifest(const std::string& project_dir);

// Scaffold a new project at `dir`: mosaic.toml + scenes/main.json (the default demo scene) +
// an empty assets/ (with a .gitkeep). Returns false (and reports) if `dir` already exists or a
// filesystem/write step fails — never clobbers existing work.
bool create_project(const std::string& dir,
                    const mosaic::engine::reflection::ComponentReflection& types);

// The "smart positional" resolver. Returns the scene file to load, or std::nullopt on error:
//   - `path` is a directory containing mosaic.toml -> parse it -> "<path>/<default_scene>"
//   - `path` is a .json file                       -> `path` itself (a bare scene)
//   - otherwise (missing, or a dir without a manifest) -> nullopt (reported error)
std::optional<std::string> resolve_startup_scene(const std::string& path);
```

`create_project` reuses T2 end-to-end: `build_default_scene(world)` →
`save_to_json(world, types)` → `save_scene_file("<dir>/scenes/main.json", json)`. The starter
scene references the built-in demo asset ids (`kCubeMeshId`, `kCheckerTextureId`, …), which
`setup_runtime` always registers — so a fresh project renders with no per-project asset loading
(that is T5).

### 3. CLI wiring (`app/cli/options.{hpp,cpp}`)

- `Command` enum gains `Create`.
- `Options` gains `std::optional<std::string> create_path;` and `std::optional<std::string>
  open_path;`.
- New subcommand: `mosaic create <name>` — positional `name` → `options.create_path`, sets
  `Command::Create`.
- `run` and `editor` each gain an **optional positional** `path` → `options.open_path`, kept
  alongside the existing `--scene`.
- **Mutual exclusivity:** a positional `path` together with `--scene` is a CLI error (ambiguous);
  neither = today's default behavior (default demo).

### 4. Entry wiring (`app/main.cpp`, `app/run/editor.cpp`, `app/run/create.cpp`)

- `main.cpp` dispatches `Command::Create` → `run_create(options)` (new `app/run/create.cpp`),
  which calls `create_project(*options.create_path, types)` and returns 0/1.
- For `run`/`editor`, before `populate_world`: if `options.open_path` is set, call
  `resolve_startup_scene(*options.open_path)`; on success set `options.scene` = the resolved
  path; on failure report + exit non-zero. Everything downstream is unchanged T2.

## Data flow

```
mosaic create mygame  -> create_project("mygame")
                          -> mygame/{mosaic.toml, scenes/main.json (=demo), assets/.gitkeep}
mosaic editor mygame  -> resolve_startup_scene("mygame") -> "mygame/scenes/main.json"
                          -> options.scene = that -> populate_world (T2) -> setup_runtime -> render
mosaic editor lvl.json-> resolve_startup_scene("lvl.json") -> "lvl.json" -> populate_world
mosaic run mygame --window -> same resolution -> render
```

## Error handling

- **`create <dir>` where `dir` exists** → error + non-zero exit (never overwrite). Create the
  directory first; if a later write fails, report the specific failure and exit non-zero.
- **`editor`/`run <path>`** where `path` is missing, or a directory without `mosaic.toml`, or an
  unparseable manifest → `resolve_startup_scene` returns `nullopt`; the caller reports a clear
  error and exits non-zero (before touching the render/window path where possible).
- **Manifest's `default_scene` missing on disk** → resolution returns the path; `populate_world`
  (T2) then reports the missing/unparseable scene and exits non-zero — one error path, reused.
- **Positional `path` + `--scene` both given** → CLI parse error (non-zero exit).

## Testability

Headless-first (only the windowed launch needs a device):

- **`ProjectManifest` TOML round-trip** (`tests/unit/app/project_manifest_test.cpp`): `to_toml`
  → `from_toml` preserves `name` + `default_scene`.
- **`create_project`** (`tests/unit/app/project_create_test.cpp`, temp dir): writes
  `mosaic.toml` (parses to the expected name/default_scene), `scenes/main.json` (a scene
  `populate_world` loads to 7 entities), and an `assets/` dir; **refuses to overwrite** an
  existing directory (returns false, leaves it untouched).
- **`resolve_startup_scene`** (`tests/unit/app/project_resolve_test.cpp`): a scaffolded project
  dir → its `scenes/main.json`; a `.json` file path → itself; a missing path → `nullopt`; a dir
  without a manifest → `nullopt`.
- **CLI parsing** (`tests/unit/app/options_test.cpp`): `create <name>` → `Command::Create` +
  `create_path`; `editor <path>` → `open_path`; positional `path` + `--scene` together → non-zero
  exit code.
- **Functional:** `mosaic create <tmp> && mosaic editor <tmp> --ticks 3` and `mosaic run <tmp>
  --window --ticks 3` exit 0 with 0 Vulkan validation errors (the scaffolded project opens +
  renders the starter demo).

## Ground rules honored

- **Tier contract:** project/CLI code lives in `app/`; `engine/`/`foundation/` unchanged. `app/`
  already owns CLI (CLI11) + filesystem seams (T2's `scene_io`).
- **Serialization is #10:** the manifest is a reflected struct through the TOML backend (like
  `AssetMetadata`); scenes stay #19 JSON. No new serialization mechanism.
- **Reuses T2:** `create_project` uses `build_default_scene`/`save_to_json`/`save_scene_file`;
  `resolve_startup_scene` feeds T2's `populate_world`. No duplicated load/save logic.
- **No new third-party deps** (`<filesystem>` is stdlib). Catch2 test names avoid a leading `--`.

## Acceptance

- [ ] `ProjectManifest` round-trips through TOML.
- [ ] `create_project` scaffolds `mosaic.toml` + `scenes/main.json` (loadable demo) + `assets/`;
      refuses to overwrite an existing dir.
- [ ] `resolve_startup_scene` maps a project dir → its default scene, a `.json` → itself, and
      errors on invalid paths.
- [ ] `mosaic create <name>` scaffolds a project; `mosaic editor <project>` / `mosaic run
      <project>` open + render it; a bare `.json` positional still works; positional + `--scene`
      is rejected.
- [ ] All the above unit-tested headlessly; windowed launches exit 0 with 0 validation errors.

## Depends on / relates

- **Depends on:** T2 (`populate_world`, `scene_io`, `build_default_scene`/`save_to_json`), #10
  TOML, #19 scene, #35 CLI, #20 assets. **Blocks:** T4 (`mosaic build` packages a project's
  scenes + assets from its manifest) and T5 (the importer writes into a project's `assets/`).
- **Relates:** scripting (a future pillar) runs against a loaded project scene.
