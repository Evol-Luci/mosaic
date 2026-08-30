# Tools T5 — asset importer/cooker (design)

**Decomposition:** [tools-workflow-decomposition](2026-08-29-tools-workflow-decomposition.md) (slice T5, final).
**Builds on:** T3 (project model + `assets/` folder), T4 (`build_bundle` + the `assets/` copy seam),
#20 asset registry (`Image` + `AssetMetadata` + `register_resource`), #10 TOML.
**Date:** 2026-08-29.

> **Autonomy note:** designed and built unattended overnight at the user's explicit request
> ("work independently... complete for me to test in the morning"). The design decisions that
> would normally be clarifying questions are made here with sensible defaults and recorded as
> **Rulings** for morning review.

## Summary

A generic **cook + load** asset pipeline: `mosaic import <project> <file>` turns an external
image file into a **cooked, registry-native resource** (a small binary blob) plus an
**`AssetMetadata`** record, written into the project's `assets/`. At startup, `mosaic run`/`editor`
(and a built bundle) **load the project's `assets/`** into the `AssetRegistry`, so scenes can
reference imported assets by `AssetID` and the asset browser can assign them. `build_bundle` (T4)
copies `assets/` into the bundle, closing the loop the tools pillar has been building toward.

## Rulings (autonomous design decisions)

- **R1 — Source format: PPM (binary P6).** An external image importer needs a decoder; a PPM
  parser is ~20 lines and needs **no third-party dependency** (honoring the pillar's no-new-deps
  rule) while still being a real, external image format. PNG/JPEG import (a decoder dep) is a
  post-v0.1 follow-up. Cost if wrong: users must convert to PPM first — trivial with any image
  tool; the importer's shape is decoder-agnostic, so adding PNG later is additive.
- **R2 — Cooked format: a small binary `.image` blob.** `assets/<stem>.image` =
  `"MOSAIMG1"` (8-byte magic) + `u32 width` + `u32 height` (little-endian) + `width*height*4` RGBA
  bytes; paired with `assets/<stem>.toml` (the `AssetMetadata`). Chosen over JSON-with-a-pixel-array
  (bloated, slow) and over storing the source verbatim (then every runtime re-decodes). The blob is
  the engine's `Image` on disk. Cost if wrong: a bespoke format to maintain — but it is trivial and
  versioned by the magic string.
- **R3 — Images only for v0.1.** The codec + import + load pattern generalizes to meshes (OBJ →
  `Mesh`), but v0.1 ships the image path end-to-end; meshes are a follow-up. Keeps the slice
  focused and testable.
- **R4 — Assets load from the project/bundle directory at startup**, keyed off the positional
  project path (`open_path`). A bare `--scene x.json` has no project and loads no extra assets.
  A missing/empty `assets/` is a no-op (projects may have none).
- **R5 — `build_bundle` copies the whole `assets/` tree** into the bundle (the T4 seam), so a
  built bundle is self-sufficient.
- **R6 — Import does not auto-reference the asset in a scene.** The imported asset appears in the
  `AssetRegistry` (and thus the Studio's Assets panel), where the existing asset-browser flow
  assigns it to a component's `AssetID` field (e.g. `Material.texture`). This mirrors how the demo
  textures already work; no scene rewriting.

## Scope

**In:** a PPM→`Image` cooker; `import_asset` (writes the cooked blob + `AssetMetadata`);
`load_project_assets` (registers a project's cooked assets into an `AssetRegistry`); a `mosaic
import` subcommand; startup asset-loading in `run`/`editor`; `build_bundle` copying `assets/`.

**Deferred:** non-PPM source formats (PNG/JPEG decoders); mesh/audio/etc. importers; incremental /
cached cooking; import settings / mipmaps / compression; hot-reload of assets; Blender-specific
import (its own deferred feature).

## Architecture

New `app/asset/` module (app tier, alongside `app/project/`).

### 1. Image codec (`app/asset/image_cook.hpp`)

```cpp
// Parse a binary PPM (P6) into an RGBA Image (alpha = 255). Returns nullopt on malformed input.
std::optional<mosaic::engine::render::Image> read_ppm(const std::string& bytes);

// The cooked on-disk form: "MOSAIMG1" + u32 width + u32 height (LE) + width*height*4 RGBA bytes.
std::string      cook_image(const mosaic::engine::render::Image& image);
std::optional<mosaic::engine::render::Image> uncook_image(const std::string& bytes);
```
Pure string↔`Image` transforms — no filesystem, fully unit-testable.

### 2. Import + load (`app/asset/asset_io.hpp`)

```cpp
// Import an external image file into <project>/assets/: cook it and write <assets>/<stem>.image
// plus <assets>/<stem>.toml (AssetMetadata: a fresh random uuid, type="texture", source=<file>).
// Returns the new AssetID, or nullopt (and logs) on failure. `stem` defaults to the file's stem.
std::optional<AssetID> import_asset(const std::string& project_dir, const std::string& source_file);

// Scan <assets_dir>/*.toml; for each, parse the AssetMetadata, load the sibling <stem>.image blob,
// uncook it, and register the Image in `registry` under the metadata's uuid. Missing/empty dir is
// a no-op. Returns the count registered (for logging/tests).
int load_project_assets(mosaic::engine::assets::AssetRegistry& registry, const std::string& assets_dir);
```
`import_asset` reuses T2's `save_scene_file`/`load_scene_file` (binary-safe text I/O) and the #10
TOML round-trip for `AssetMetadata` (the exact pattern `asset_metadata_test` uses). `AssetID`
generation: a fresh random 16-byte id (a small `std::random_device`-seeded helper — no dep).

### 3. CLI (`app/cli/options.{hpp,cpp}`)

`mosaic import <project> <file>` — `Command::Import`; `Options::import_project`, `Options::import_file`
(both `std::optional<std::string>`); dispatched in `main()` to a new `run_import` (`app/run/import.cpp`)
that calls `import_asset` and reports the new id.

### 4. Startup asset loading (`app/main.cpp`, `app/run/editor.cpp`)

`Options` gains `std::optional<std::string> assets_dir;`. In `main()`, when the positional
`open_path` is a **directory** (a project/bundle), set `options.assets_dir = <open_path>/assets`
(next to the existing scene resolution). Then, right after `setup_runtime(world, asset_registry)`
in both the `run --window` block and `editor.cpp`, call
`load_project_assets(asset_registry, *options.assets_dir)` when it is set. So a project's imported
assets are registered before the first frame, in addition to `setup_runtime`'s built-ins — and the
Assets panel lists them for assignment.

### 5. Bundle copy (`app/project/bundle.hpp`)

`build_bundle` copies the project's `assets/` tree into the bundle
(`std::filesystem::copy(project/assets, out/assets, recursive)`) when it exists. So `B/mosaic run B`
resolves the bundle's `assets/` through the same `load_project_assets` path — a self-sufficient
bundle.

## Data flow

```
mosaic import mygame tex.ppm
   -> read tex.ppm -> read_ppm -> Image -> cook_image
   -> mygame/assets/tex.image (blob) + mygame/assets/tex.toml (AssetMetadata{uuid, "texture", source})
mosaic editor mygame
   -> setup_runtime(world, registry)                       # built-in meshes + demo textures
   -> load_project_assets(registry, mygame/assets)         # + the imported tex under its uuid
   -> Assets panel lists it; assign it to a Material.texture; it renders
mosaic build mygame -o dist
   -> build_bundle copies mygame/assets -> dist/assets
dist/mosaic run dist  ->  load_project_assets(registry, dist/assets)  # bundle is self-sufficient
```

## Error handling

- **`import` non-project / missing source / malformed PPM / write failure** → log + non-zero exit;
  no partial asset left (write the blob, then the metadata; on metadata-write failure remove the
  blob).
- **`load_project_assets`**: a missing/empty `assets/` → 0, no error. A malformed `.toml` or a
  `.toml` whose `.image` sibling is missing/corrupt → log a warning, skip that asset, continue
  (one bad asset must not abort startup). Returns the count actually registered.
- **`build_bundle` assets copy** failure → treated like any other bundle write failure (log +
  rollback the bundle).

## Testability

Headless-first:

- **Codec** (`tests/unit/app/image_cook_test.cpp`): a hand-built P6 PPM string → `read_ppm` →
  correct width/height/RGBA (alpha 255); `cook_image`→`uncook_image` round-trips an `Image`; a
  malformed PPM / truncated blob → `nullopt`.
- **`import_asset`** (`tests/unit/app/asset_import_test.cpp`, temp project): importing a small PPM
  writes `<assets>/<stem>.image` + `<stem>.toml`; the metadata parses (type "texture", non-nil
  uuid, source set); a non-project or missing source → nullopt.
- **`load_project_assets`** (same/temp): after `import_asset`, loading the `assets/` registers the
  id (`registry.contains(id)`), and `registry.load<Image>(id)` returns the imported pixels; an
  empty dir → 0; a `.toml` with a missing `.image` → skipped (count excludes it), no throw.
- **CLI** (`options_test`): `import <project> <file>` → `Command::Import` + both fields.
- **Functional:** `mosaic create P && mosaic import P <tmp.ppm> && mosaic editor P --ticks 3` and
  `mosaic build P -o B && B/mosaic run B --window --ticks 3` exit 0 with 0 validation errors (the
  imported asset loads without crashing; the bundle carries `assets/`). A tiny PPM is written by
  the test.

## Ground rules honored

- **Tier:** all importer/CLI/IO code in `app/` (new `app/asset/`); `engine/`/`foundation/`
  unchanged. Filesystem via `<filesystem>` + T2 `scene_io`, confined to `app/`.
- **Assets go through #20:** imported assets are `register_resource<Image>` entries keyed by
  `AssetID`; `AssetMetadata` round-trips through the #10 TOML backend. No new serialization
  mechanism; `AssetID` is the on-disk reference (scenes already store it).
- **Reuse:** the `Image` resource + `TextureCache` render path (36d-2), `save_scene_file` (T2),
  `AssetMetadata` TOML (#20), the asset browser (post-v0.1 Studio) all unchanged — T5 just feeds
  the registry from disk.
- **No new third-party deps** (PPM + a hand-rolled binary blob + `std::random_device`). Catch2
  names avoid a leading `--`.

## Acceptance

- [ ] PPM→`Image` cook/uncook round-trips; malformed input → nullopt.
- [ ] `import_asset` writes a cooked blob + `AssetMetadata` under `assets/`; errors cleanly.
- [ ] `load_project_assets` registers a project's cooked assets into the registry (id resolvable to
      the imported pixels); skips bad assets; empty dir is a no-op.
- [ ] `mosaic import <project> <file>` parses + runs; `run`/`editor <project>` load the project's
      assets at startup.
- [ ] `build_bundle` copies `assets/` into the bundle; `B/mosaic run B` loads them.
- [ ] All headlessly unit-tested; functional launches exit 0, 0 validation errors.

## Depends on / relates

- **Depends on:** T3 (`assets/` folder + project resolution), T4 (bundle + `assets/` copy seam),
  #20 (`Image`/`AssetMetadata`/registry), T2 (`scene_io`), #10 TOML. **Completes** the Project &
  Tools Workflow pillar (T1–T5): create → edit/save → run → build → import assets. **Relates:**
  a PNG/mesh importer and Blender live-link are post-v0.1 follow-ups.
