# Tools T2 — Scene load/save in the Studio (design)

**Decomposition:** [tools-workflow-decomposition](2026-08-29-tools-workflow-decomposition.md) (slice T2).
**Builds on:** T1 (meshes as assets — renderable entities now round-trip), #19 scene load/save,
#18 reflection, #37 Studio, #35 app/CLI.
**Date:** 2026-08-29.

## Summary

Make the Studio and `mosaic run` operate on **real scene files** instead of the hardcoded
`build_demo_scene`. `--scene <file>` loads a scene (#19) and renders it (enabled by T1); the
editor gains **File → Save** over a single "current scene path"; and `build_demo_scene`
is split into serializable **scene data** and non-serializable **runtime scaffolding** so any
scene — loaded from disk or the built-in default — is set up the same way. (In-editor Reload is a
deferred follow-up; see Scope.)

## The problem

`build_demo_scene(world, device, registry)` conflates two things a scene file cannot capture
together:

- **Scene data** — the entities + reflected components (ground/box/sphere/capsule/stack, lights,
  camera). This is exactly what `save_to_json`/`load_from_json` (#19) round-trips.
- **Runtime scaffolding** — registering built-in meshes + the demo textures in the
  `AssetRegistry`, adding the `PhysicsSystem` (Box3D backend), the contact-logging subscription,
  and creating the `RenderSystem`. **None of this serializes**, so it must be re-established on
  every load, independent of which scene is opened.

Today `run --window` even loads `--scene` into the world *and then* calls `build_demo_scene`,
double-populating. T2 untangles this.

## Scope

**In:**
- Split `build_demo_scene` into `setup_runtime` (scaffolding) + `build_default_scene` (entities).
- A single `populate_world` load path (scene file **or** the default demo) shared by `run` and
  `editor`; loaded entities are made renderable (`WorldTransform` ensured).
- `--scene` added to the `editor` subcommand; `run --window --scene` renders the loaded scene.
- Studio **File → Save** over a "current scene path" (default `scene.json`), a headless
  `SceneFileModel`, and a thin filesystem seam (`scene_io`).

**Deferred (YAGNI / later slices):**
- **In-editor File → Reload** — deferred to its own small follow-up slice. Reload must rebuild a
  *fresh* `World` (the `PhysicsSystem` holds internal Box3D bodies with no teardown path, so an
  in-place wipe would strand stale bodies), and `FramePump::run` currently ticks a single fixed
  `World&`. Doing Reload right means adding a world-provider indirection to the pump (which both
  `run` and `editor` use) — a world-lifecycle change orthogonal to the rest of T2. The startup
  `--scene` load already covers "open a scene"; Save covers "persist edits"; Reload/Revert is a
  convenience that earns its own slice.
- A native file picker or arbitrary Save-As (T2 uses the fixed current-scene path chosen in
  brainstorming), multi-scene projects (T3), auto-save, and per-field dirty precision beyond a
  simple indicator.

## Architecture

### 1. Split `build_demo_scene` (`app/run/`)

`app/run/demo_scene.hpp` is refactored into two functions with clear boundaries:

```cpp
// Non-serializable runtime scaffolding for ANY scene: register built-in meshes + the demo
// textures, add the PhysicsSystem (Box3D), subscribe contact logging, build the RenderSystem.
// Returns what the frame loop needs. Always called after the world is populated.
struct RuntimeScene { std::shared_ptr<engine::render::RenderSystem> render_system; };
RuntimeScene setup_runtime(engine::World& world, engine::render::RenderDevice& device,
                           engine::assets::AssetRegistry& registry);

// Pure scene DATA: creates the demo entities/components (ground, box, sphere, capsule, stacked
// box, lights, camera). No device, no registry — this is what save_to_json serializes and what
// a loaded scene replaces. Materials reference the demo textures by well-known AssetID.
void build_default_scene(engine::World& world);
```

The three demo texture AssetIDs (currently locals in `build_demo_scene`) are promoted to shared
constants next to the mesh ids (e.g. `kCheckerTextureId`), so `build_default_scene` (which sets
`Material.texture`) and `setup_runtime` (which registers the image) agree on the same ids —
mirroring how `kCubeMeshId` is shared between the demo and `register_builtin_meshes`.

### 2. Shared load path (`app/run/scene_io.hpp` + a `populate_world`)

```cpp
// app/run/scene_io.hpp — the ONLY filesystem seam (keeps engine/scene in-memory-only).
// Reads a whole file to a string; std::nullopt if it cannot be opened.
std::optional<std::string> load_scene_file(const std::string& path);
// Writes text to a file; false on failure.
bool save_scene_file(const std::string& path, const std::string& contents);
```

```cpp
// Populate `world` for a run/editor session: from options.scene if set + readable, else the
// built-in default demo. Ensures every Transform-bearing entity has a WorldTransform so a
// loaded scene renders (WorldTransform is derived and not serialized). Returns false only on a
// genuine load error (file present but unparseable), so callers can exit non-zero.
bool populate_world(engine::World& world, const Options& options,
                    const engine::reflection::ComponentReflection& types);
```

`populate_world` centralizes the source decision so `run` and `editor` cannot drift. `run`
(headless) uses it and skips the render system; `run --window` and `editor` call `setup_runtime`
after it.

### 3. Studio File-menu model (`studio/scene_file.hpp`)

Mirrors the existing `StudioLayout::exit_requested`/`request_exit` and `PlayState` one-shot
patterns — pure data, no ImGui, no filesystem:

```cpp
class SceneFileModel
{
public:
    explicit SceneFileModel(std::string path); // from --scene, or default "scene.json"
    const std::string& path() const;
    bool dirty() const;                         // unsaved edits since last save
    void mark_dirty();
    void mark_clean();                          // after a successful Save
    bool consume_save_request();                // one-shot (like PlayState::consume_step_request)
    void request_save();
private:
    std::string path_;
    bool        dirty_ = false;
    bool        save_req_ = false;
};
```

- **File menu** (`studio_app.cpp`) gains **Save** (`request_save`) above Exit; the menu bar shows
  `path` with a trailing `*` when `dirty()`. (A **Reload** item is a deferred follow-up — see
  Scope.)
- **Dirty tracking:** the inspector already edits components live and reports when a field
  changes; the editor calls `mark_dirty()` on a reported edit. The `*` is a convenience
  indicator, not correctness-critical.

### 4. Editor loop wiring (`app/run/editor.cpp`)

The loop consumes the one-shot save request each frame — the actual I/O lives here, keeping the
model pure:

- `consume_save_request()` → `save_to_json(world, reflection)` → `save_scene_file(path, json)`
  → `mark_clean()`.

(In-editor Reload is deferred; see Scope. The path is still loaded once at startup via
`populate_world`.)

## Data flow

```
startup:  populate_world(world, options, types)          // file (options.scene) OR build_default_scene
          setup_runtime(world, registry)                 // assets + PhysicsSystem + RenderSystem
frame:    TransformSystem (derived) -> render
File>Save:   save_to_json(world) -> save_scene_file(path)         -> mark_clean
```

(`setup_runtime` takes no device — registering assets, adding the `PhysicsSystem`, and creating
the `RenderSystem` need only the world + registry; texture/mesh uploads happen at render time.)

## Error handling

- **`--scene` missing/unreadable at startup:** report an error and exit non-zero (matches the
  current `run` behavior). An *absent* `--scene` is not an error — it selects the default demo.
- **Unparseable scene:** `load_from_json` throws `SerializationError`; caught, reported, exit
  non-zero at startup.
- **Save failure** (unwritable path): logged to the Console; `dirty()` stays set.

## Testability

Headless-first (only the windowed launch needs a device):

- **`SceneFileModel`** (`tests/unit/studio/scene_file_test.cpp`): path, dirty set/clear, one-shot
  `consume_save_request` (mirrors `play_state_test`).
- **`scene_io`** (`tests/unit/app/scene_io_test.cpp`): write→read round-trip equal; missing file
  → `nullopt`; unwritable path → `false`.
- **`populate_world`** (`tests/unit/app/…`): a written scene file → its entities loaded; no
  `--scene` → the default demo entities present; every `Transform` entity gains a
  `WorldTransform`; a present-but-malformed file → returns false.
- **Split round-trip:** `build_default_scene` → `save_to_json` → `load_from_json` yields the same
  entity/component set (the demo survives save/load) — reuses the #19 machinery.
- **Functional:** `mosaic editor --scene <tmp>` and `mosaic run --window --scene <tmp>` exit 0,
  0 validation errors; `mosaic editor` (no scene) still shows the demo; `mosaic run --window`
  (no scene) unchanged.

## Ground rules honored

- **Tier contract:** scene *data*/scaffolding split and the fs seam live in the **`app/`** tier;
  `engine/scene` stays in-memory-only (its stated contract). No engine/foundation changes beyond
  what T1 already added. Studio model is headless (`studio/`), ImGui confined to `studio_app.cpp`.
- **Serialization is #10/#19** — scenes are the reflected component graph as JSON; no new
  mechanism. Assets remain #20 registry resources referenced by `AssetID`.
- **The Studio edits through reflection** — load populates a `World` the existing
  hierarchy/inspector/viewport already operate on; no new editor architecture beyond the File
  menu + model.
- **No new third-party deps** (the fixed current-scene path avoids a native file dialog). Catch2
  test names avoid a leading `--`.

## Acceptance

- [ ] `build_demo_scene` split into `setup_runtime` + `build_default_scene`; demo textures'
      AssetIDs shared constants.
- [ ] `populate_world` loads from `--scene` or the default demo; loaded entities render
      (`WorldTransform` ensured). `run --window --scene` renders the file (no double-populate).
- [ ] `editor` accepts `--scene`.
- [ ] `SceneFileModel` + File → Save write the current-scene path; menu shows the path + dirty `*`.
- [ ] A scene edited in the editor saves to disk, and relaunching with `--scene <that file>`
      loads the edited state.
- [ ] All the above unit-tested headlessly; windowed launches exit 0 with 0 validation errors.
- [ ] In-editor Reload is documented as a deferred follow-up (not in T2).

## Depends on / relates

- **Depends on:** T1 (renderable entities round-trip), #19 scene, #18 reflection, #37 Studio,
  #35 CLI/app shell, #20 asset registry. **Blocks:** T3 (`mosaic create` + project model — a
  project's default scene is opened through this same load path).
- **Relates:** T5 importer will register assets a saved scene references; a native file picker is
  deferred polish.
