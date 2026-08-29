# Project & Tools Workflow — Decomposition

**Roadmap pillar:** [v0.1 Tools + the Studio's real-scene gap](../../project/roadmap.md#the-v01-vertical-slice--the-first-mountain)
— "CLI (`mosaic create/editor/run/build`), project generator, asset compiler," plus the Studio
operating on real scene files.
**Status:** Accepted (decomposition; each sub-issue gets its own spec).
**Date:** 2026-08-29.

This is a **decomposition**, not an implementation spec. It breaks the "project & tools
workflow" pillar into buildable sub-slices, fixes the cross-cutting decisions they inherit, and
sets their order — mirroring the [rendering](2026-08-17-rendering-decomposition.md),
[Studio](2026-08-22-studio-decomposition.md), and [physics](2026-08-24-physics-decomposition.md)
decompositions. Each sub-slice is then designed and built through its own brainstorm → spec →
plan → TDD cycle.

## Purpose

This pillar turns Mosaic from *a demo you run* into *a tool you use on your own project*. The
v0.1 vertical slice ends with **create a project → open it in the Studio → edit/save → run →
build a standalone**. Today the Studio edits a **hardcoded** demo scene (`build_demo_scene`),
there is no project concept, and `mosaic create`/`build` do not exist. This pillar closes that.

## The cross-cutting enabler: meshes as assets

Scene load/save already exists (#19: `save_to_json`/`load_from_json` over the reflected
component set), and reflected components round-trip — including `Material.texture`, already an
`AssetID`. **But `MeshRenderer` holds a runtime `MeshHandle` and is deliberately *not*
serialized** (its own comment defers "the serializable asset-referenced form" to "36d"). So a
saved scene loses its meshes and a loaded scene renders nothing.

Every downstream slice needs scenes to round-trip *renderable* entities, so the pillar's
foundational enabler is **meshes as assets**: `MeshRenderer` references a mesh `AssetID`
(reflected/serializable), resolved to an uploaded GPU mesh through a `MeshCache`, exactly
mirroring the 36d-2 texture pattern (`Material.texture` = `AssetID` + `TextureCache` +
`Image` resource). The built-in cube becomes a registered mesh asset. This is **T1**, and it
comes first because T2–T4 are meaningless without it.

## Scope (v0.1)

**In:** meshes as assets; scene load/save wired into the Studio + app (open/save real scene
files, rendered); a `mosaic create` project generator + a project model (manifest + scenes +
assets); a minimal `mosaic build` standalone; a generic asset importer/cooker.

**Deferred (post-v0.1 / separate work):** Blender-specific import + live-link (a Studio
feature, already deferred), hot-reload of scenes/assets, incremental/cached cooking,
multi-scene projects and scene references, and platform packaging beyond a single-target
bundle.

## Architectural spine (every slice honors this)

- **Tier contract** ([ARCHITECTURE.md](../../architecture/ARCHITECTURE.md)) unchanged:
  `foundation/` knows nothing above it; `engine/` depends only on `foundation/`; the mesh-asset
  resolution lives in `engine/render` next to the texture cache; project/CLI/build logic lives
  in the **`app/`** tier (CLI11-confined), like the existing `run`/`editor` subcommands.
- **Serialization is #10/#19.** Scenes are the reflected component graph serialized to JSON;
  project/asset manifests use TOML (the #10 TOML backend, as `AssetMetadata` already does).
  No new serialization mechanism.
- **Assets go through the #20 registry.** Meshes join textures as registry-resolved resources;
  the importer/cooker (T5) produces registry entries. `AssetID` is the persisted reference in
  every on-disk form (scenes, manifests).
- **The Studio edits through reflection.** Loading a scene populates a `World` the existing
  hierarchy/inspector/viewport already operate on — no Studio changes beyond load/save wiring
  and a File menu.
- **Determinism / headless-testability.** Project generation, scene round-trip, manifest
  parsing, and the build bundle's file layout are all headlessly unit-testable (filesystem +
  serialization); only the *rendered* loaded scene leans on the standing functional-launch
  check (`mosaic editor --scene … --ticks N` exits 0, 0 validation errors).

## Sub-slice breakdown & order

1. **T1 — Meshes as assets.** Reflected `MeshRenderer { AssetID mesh }`; a `Mesh` resource +
   `MeshCache` resolving `AssetID → MeshHandle` through the asset registry (upload once,
   cache), mirroring `TextureCache`/`Image`; the built-in cube registered as a mesh asset;
   `RenderSystem` resolves the mesh per entity. Scenes now round-trip renderable entities.
   *The "36d" the `MeshRenderer` comment deferred.*
2. **T2 — Scene load/save in the Studio.** `mosaic editor --scene <file>` / `mosaic run
   --window --scene <file>` load a scene (#19) and render it (needs T1); the editor gains
   **File → Open / Save** (writes/reads scene JSON), so the Studio edits real files instead of
   `build_demo_scene`. The demo becomes the *default/new* scene, saved to a file on first run.
3. **T3 — `mosaic create` + project model.** A project directory = a TOML manifest + a
   `scenes/` folder (a starter scene) + an `assets/` folder. `mosaic create <name>` scaffolds
   it; `mosaic editor <project>` / `mosaic run <project>` open its default scene. Project
   discovery + manifest parsing are headlessly tested.
4. **T4 — `mosaic build` (minimal standalone).** Package a runnable bundle — the `mosaic`
   runtime + the project's scene(s) + referenced assets — into an output directory that runs
   the scene without the source tree. Deliberately minimal (one target, a bundle dir + a
   manifest the runtime reads); the bundle layout is unit-tested.
5. **T5 — asset importer/cooker.** A generic `mosaic build`-time (or `import`) step that turns
   an external asset file into a registry-native resource + `AssetMetadata`, so a project can
   reference imported assets. Blender-specific import stays a separate, deferred feature.

**Suggested order:** T1 → T2 → T3 → T4 → T5 (linear). First buildable is **T1**. This completes
the roadmap's Tools pillar and the Studio's real-scene gap.

## Cross-cutting decisions (fixed here)

- **Meshes become registry assets** (T1), the enabler; `AssetID` is the on-disk mesh reference.
- **Scenes = #19 reflected-graph JSON; projects/manifests = #10 TOML.**
- **Project/CLI/build code lives in `app/`**; engine gains only the neutral mesh-asset path.
- **Studio load/save is thin wiring** over #19 + a File menu; no new editor architecture.
- Each sub-slice gets its own brainstorm → spec → plan → TDD cycle.

## Acceptance (tracking-level; each sub-issue refines its own)

- [ ] Meshes are registry assets; a scene with meshes round-trips + renders (T1).
- [ ] The Studio opens/saves real scene files; `--scene` renders (T2).
- [ ] `mosaic create` scaffolds a project the editor/run open (T3).
- [ ] `mosaic build` produces a standalone bundle that runs the project's scene (T4).
- [ ] A generic importer turns an external asset into a registry resource (T5).

## Depends on / relates

- **Depends on:** #10 serialization (JSON/TOML), #19 scene load/save, #20 asset registry +
  the 36d-2 texture-cache pattern (the reference for T1), the app/CLI shell (#35), the Studio
  (#37). **Relates:** scripting (a future pillar) runs against a loaded project scene; the
  deferred Blender live-link imports into a project.
