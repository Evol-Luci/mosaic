# Next Steps — Build Handoff

This is the live "you are here" for a worker picking up Mosaic. It points at the
next buildable work and the decisions that constrain it. When the work below is
done, update this file to point at what comes next.

**Last updated:** 2026-08-30 (v0.1 foundation + rendering + physics + Project & Tools Workflow
(T1–T5) complete; **Scripting (Python) pillar UNDERWAY — S1 (Python embedding) COMPLETE.**
The final v0.1 pillar was decomposed into five slices
([scripting decomposition](../superpowers/specs/2026-08-30-scripting-decomposition.md)):
**S1** embed → **S2** reflection→Python property bindings → **S3** world/commands → **S4**
`ScriptComponent` (attach a script, press Play) → **S5** Studio script console.
**S1** embeds **real CPython** behind a neutral, Python-free `ScriptEngine` interface
(`engine/script/script_engine.hpp`); the CPython + **pybind11** implementation is walled off in
`modules/script_python` (injected via `create_python_script_engine()`, exactly like the Box3D /
Vulkan backends). `run_string` runs a script in an isolated per-engine namespace, captures stdout
+ errors into a `ScriptResult` (never throws). **This slice adds the pillar's two build deps** —
a Python dev install (`find_package(Python3 Development)`) and pybind11 (FetchContent v2.13.6,
`PYBIND11_FINDPYTHON ON` to reconcile a machine's multiple Pythons); both are contained behind the
module wall. 335 tests green (5 script tests: run+capture, compute, namespace isolation, syntax +
runtime error capture, output-on-error). Runtime feasibility was confirmed by a throwaway spike
before committing. **Build requirement note:** building now needs an *embeddable* Python (headers
+ a shared `libpython`, e.g. distro `python3-dev`). **Next: S2** (expose reflected component
properties to Python via the #18 bridge). S1 design/plan:
[S1 spec](../superpowers/specs/2026-08-30-scripting-s1-embedding-design.md) ·
[S1 plan](../superpowers/plans/2026-08-30-scripting-s1-embedding.md).

Prior pillar — **T5** added `mosaic import <project> <file>` and the full asset-copy step in `build_bundle`.
`import_asset` (in `app/asset/asset_io.hpp`) accepts a PPM image, cooks it into a compact binary
blob (`.image`), writes `AssetMetadata` as a `.toml` sidecar (uuid, type, version, source), and
places both under `<project>/assets/`. `build_bundle` now copies the project's `assets/` tree
into the bundle (step 4.5, after manifest write, before binary copy) via `fs::copy` recursive;
`load_project_assets` scans bundle `assets/*.toml` at startup and registers each cooked image
into the `AssetRegistry` under its metadata uuid, so the bundle runs with its own assets from its
own copied binary — no source tree needed. 329 tests green; functionally, `mosaic create P &&
mosaic import P t5.ppm && mosaic build P -o B && B/mosaic run B --window --ticks 3` exits 0 with
no validation errors, and `B/assets/` contains the cooked `t5.image` + `t5.toml`. T5 design/plan:
[T5 spec](../superpowers/specs/2026-08-29-tools-t5-asset-importer-design.md) ·
[T5 plan](../superpowers/plans/2026-08-29-tools-t5-asset-importer.md).
**The Project & Tools Workflow pillar (T1–T5) is COMPLETE.**
**Next pillar: Scripting (Python)** — expose the ECS/world API to Python scripts.

Prior slice — **T4** added `mosaic build <project> [-o <out>]`: it packages a project into a
**self-contained, project-format bundle** — a copy of the running `mosaic` binary + the default
scene + the manifest + an executable `run.sh` — in `app/project/bundle.hpp` (`build_bundle`,
runtime-binary path injected for headless tests; runtime resolved via `/proc/self/exe`). Because
the bundle is project-format, the copied binary runs it through **T3's existing** `mosaic run
<dir>` path — so T4 only added the *build* side; running is free. Overwrite-safe: a dir is treated
as a regenerable prior bundle only if it carries the build artifacts (`mosaic` + `run.sh`), a
plain project or arbitrary `-o <path>` is refused untouched, and a self-build (`-o <project>`) is
refused — so no user data is clobbered; write failures roll back the bundle. Copying real
per-project `assets/` files was deferred to T5 (done). 319 tests green at the time. T4 design/plan:
[T4 spec](../superpowers/specs/2026-08-29-tools-t4-build-design.md) ·
[T4 plan](../superpowers/plans/2026-08-29-tools-t4-build.md).

Prior slice — **T3** introduced a **project** = a TOML `mosaic.toml` manifest (reflected `ProjectManifest{name,
default_scene}`) + a `scenes/` folder (a starter scene) + an `assets/` folder. `mosaic create
<name>` scaffolds one (starter scene = the T2 default demo serialized to `scenes/main.json`;
refuses to overwrite; rolls back a partial scaffold on failure). A "smart positional" arg on
`run`/`editor` — `resolve_startup_scene(path)` — maps a **project dir → its default scene** and a
**`.json` → itself**, then feeds T2's `populate_world` (so opening a project reuses all of T2's
load/render path; `editor.cpp` was untouched — resolution is centralized in `main()`). New
`app/project/` module (`project.hpp` + `project_io.hpp`), a `create` subcommand, and `Command`/
`Options` additions. Reading per-project `assets/` is deferred to **T5** (importer). 313 tests
green; `mosaic create X && mosaic editor X` / `mosaic run X --window` open + render the scaffolded
project. **Next: T4** (`mosaic build` — package a project's scene(s) + referenced assets into a
runnable standalone bundle). T3 design/plan:
[T3 spec](../superpowers/specs/2026-08-29-tools-t3-project-model-design.md) ·
[T3 plan](../superpowers/plans/2026-08-29-tools-t3-project-model.md).

Prior slice — **T2** split the hardcoded `build_demo_scene` into `setup_runtime` (non-serialized scaffolding:
built-in meshes + demo textures + `PhysicsSystem` + `RenderSystem`) and `build_default_scene`
(pure entity data), added a `scene_io` filesystem seam and a shared `populate_world` (load
`--scene` or the default demo, then derive `WorldTransform` so a loaded scene renders), wired
both `mosaic run` and `mosaic editor` to that path (and gave `editor` a `--scene` flag, fixing
the old `run --window --scene` double-populate), and added a headless `SceneFileModel` driving
**editor File → Save** (writes the current scene JSON; menu shows the path + a `*` when dirty).
The Studio now edits **real scene files**: open one via `--scene`, edit, Save, relaunch loads it.
**In-editor File → Reload is a deferred follow-up** (it needs a fresh-`World` rebuild + a
`FramePump` world-provider indirection — `PhysicsSystem` has no teardown and the pump ticks a
fixed `World&`). 302 tests green; `mosaic run --window` / `mosaic editor` exit 0, 0 validation
errors. **Next: T3** (`mosaic create` + project model — a TOML manifest + `scenes/`/`assets/`
folders; the project's default scene opens through T2's load path). T2 design/plan:
[T2 spec](../superpowers/specs/2026-08-29-tools-t2-scene-load-save-design.md) ·
[T2 plan](../superpowers/plans/2026-08-29-tools-t2-scene-load-save.md).

Prior slice — **T1** makes `MeshRenderer` reference a mesh **`AssetID`** (reflected,
serialized) resolved to a GPU mesh through a `MeshCache` — mirroring the 36d-2 texture pattern
(`Image`+`TextureCache`+`Material.texture`). A new `Mesh` resource + `MeshCache` (upload-once,
`AssetID → MeshHandle`), the built-in cube registered as a mesh asset under `kCubeMeshId`
(`register_builtin_meshes`), and `RenderSystem` resolving each entity's mesh via the cache mean
**scenes now round-trip renderable entities** — the enabler T2–T5 need. Headless tests cover the
cache (upload-once/nil/unregistered), the `MeshRenderer` reflection + scene round-trip, and the
updated render-system resolution; `mosaic run --window` still renders (0 validation errors, 287
tests green). **Next: T2** (scene load/save wired into the Studio — `mosaic editor --scene`, File
→ Open/Save). Design/plan:
[tools decomposition](../superpowers/specs/2026-08-29-tools-workflow-decomposition.md) ·
[T1 spec](../superpowers/specs/2026-08-29-tools-t1-meshes-as-assets-design.md) ·
[T1 plan](../superpowers/plans/2026-08-29-tools-t1-meshes-as-assets.md).
Prior state: v0.1 foundation + rendering complete; **Studio #37 v0.1 lean
core COMPLETE** — 37a–37e all built. `mosaic editor` opens an ImGui docking window with a
scene **hierarchy** (click to select), a **reflection-driven inspector** that edits a
selected entity's components live (the #18 payoff), a **viewport** panel showing the live 3D
scene (spinning, textured, lit cube) via a panel-sized offscreen target sampled by ImGui, and
a **toolbar play/stop/step + Console panel** — play/stop drives the `RuntimeLoop` (pause =
`stop()`, resume = `start()`; a Step ticks once while paused), and the Console surfaces #33
logging live via a capturing `Sink` + thread-safe ring buffer. Rendering 36a–36e-1 remains
complete (`mosaic run --window` draws the same cube directly). **RenderGraph** stays deferred
(37d chose direct two-pass submission; re-filed to the first dynamic/≥3-pass slice). The
**logging-sink gap** surfaced by 37e is now **closed** — `app/main.cpp` installs a stderr
`ConsoleSink` for every command, so `mosaic run`/headless runs emit their `log::` output and
Vulkan validation messages surface (the JSON reporter still writes structured output to stdout,
so stdout stays pure JSON). **Validation-layer fix:** with `VK_LAYER_KHRONOS_validation` now
installed, a pre-existing swapchain-semaphore-reuse bug (latent since the 36a swapchain work)
surfaced and is fixed — `render_finished_` is now per swapchain image, indexed by
`image_index_`, so a present-wait semaphore is never reused while its present is still pending
(`editor --ticks 180` now emits 0 `pSignalSemaphores` warnings). **Post-v0.1 Studio asset
browser is built:** an **Assets** panel lists registered assets (via new `AssetRegistry`
enumeration) and assigns a selected asset to a component's `AssetID` field live — e.g. swap
the cube's `Material.texture` and watch the viewport update — through the reflected-property
surface (a new mutable `FieldVisitor::field_asset`, so humans and the future agent API share
one path), with a headless `AssetBrowserModel` and three procedural demo textures.
**Two visual bugs from acceptance testing were fixed** (PR #61): inside-out culling (a
negative-height viewport reverses winding, so `frontFace` is now `CLOCKWISE`; pre-existing
since 36b) and paused inspector edits not refreshing the viewport (the editor now runs
`TransformSystem` every frame as derived state, gating only the spin behind play/stop).
Design/plan for the asset browser:
[spec](../superpowers/specs/2026-08-23-studio-asset-browser-design.md) ·
[plan](../superpowers/plans/2026-08-23-studio-asset-browser.md).
**Physics (Phase 2) is now underway — P1 is built.** The physics pillar was decomposed
into five slices (P1–P5) behind a neutral `PhysicsBackend` wall, with Box3D
([erincatto/box3d](https://github.com/erincatto/box3d), MIT, C17) as the backend. **P1**:
`engine/physics/physics_backend.hpp` (neutral `PhysicsBackend`/`BodyDef`/`BoxShape`/`Pose`/
`WorldDef`) + `modules/physics_box3d` (Box3D via FetchContent, pinned to `30c67b5`,
implementing the interface; `b3*` never leaves the module), proven by a **headless
deterministic** falling-box unit test — a box drops and rests on a ground plane at y≈0.5,
bit-identically across runs. **P2 is now built too** — reflected `RigidBody`/`Collider`
components + a `PhysicsSystem` that owns an **injected** backend, lazily creates bodies from
`RigidBody`+`Collider`+`Transform`, steps once per `World::tick`, and writes dynamic poses
back to `Transform` (position + real rotation — completing P1's rotation read-back). The
editor/`mosaic run --window` demo is now a **textured box dropping onto a ground slab** —
**Play finally simulates**, and Pause/Step single-step the fall. Tested headlessly: a
fake-backend sync test (one body per physics entity, dynamic write-back, static untouched) +
a real-backend ECS integration test (box falls + rests at y≈1.0, deterministic).
**Editor-interaction fix (found in acceptance testing):** the `PhysicsSystem` now syncs
**both ways** — a new `PhysicsBackend::set_body_transform` (teleport + zero velocity) plus a
per-tick divergence check push external `Transform` edits (inspector drags) into the
simulation, so a physics object can be moved while playing and edits survive Pause→Play
(previously the one-way body→Transform write-back clobbered edits / jumped the object back).
**P3 is now built too** — a tagged neutral `Shape` (box/sphere/capsule) mapped to Box3D
shapes, reflected `SphereCollider`/`CapsuleCollider` components (P2's `Collider` stays box),
and a `PhysicsSystem` that selects the shape per entity. The demo drops a **sphere, capsule,
and a box stack** onto the ground; headless tests cover sphere-rest, box-stack stability, and
capsule-settle, all deterministic. **"Plane" = flat box** (Box3D has no plane primitive).
**P4 is now built too** — a neutral `RaycastResult` + `PhysicsBackend::raycast` (closest-hit
via Box3D's `b3World_CastRayClosest`, body identified through userData) and an ECS-level
`PhysicsSystem::raycast` returning the hit `EntityID` (via a `BodyHandle → EntityID` reverse
map). Headless deterministic hit/miss/nearest-of-two tests at both the backend and ECS levels
(0 validation errors). No visuals — viewport mouse-picking is a deferred editor feature.
**P5 is now built — the physics pillar (P1–P5) is COMPLETE.** Box3D touch events map to a
neutral `BodyContact`, surfaced as a public `ContactEvent { phase; EntityID a; EntityID b; }`
emitted on the #11 `EventBus` by `PhysicsSystem` each tick; a contact-sequence determinism test
records the event stream over a scenario and asserts it identical across runs (the record/replay
guarantee applied to events); and the demo **logs contacts** so `mosaic run`/the editor Console
show `contact begin: entity … <-> entity …` as the bodies land (282 tests, 0 validation errors).
**What remains for the v0.1 vertical slice:** **Scripting (Python)**. The **Project & Tools
Workflow pillar (T1–T5) is now COMPLETE** — T1 (meshes as assets), T2 (scene load/save), T3
(`mosaic create` + project model), T4 (`mosaic build` standalone bundle), T5 (asset
importer/cooker + `build_bundle` copies assets) are all done. **Physics follow-ups (deferred):**
sphere/capsule demo meshes (colliders currently drawn as cubes), full record/replay-to-disk,
contact manifolds/points/impulses, and viewport mouse-picking (consumes `PhysicsSystem::raycast`).
Physics design/plan:
[decomposition](../superpowers/specs/2026-08-24-physics-decomposition.md) ·
[P1 spec](../superpowers/specs/2026-08-24-physics-p1-backend-falling-box-design.md) · [P1 plan](../superpowers/plans/2026-08-24-physics-p1-backend-falling-box.md) ·
[P2 spec](../superpowers/specs/2026-08-24-physics-p2-system-design.md) · [P2 plan](../superpowers/plans/2026-08-24-physics-p2-system.md) ·
[P3 spec](../superpowers/specs/2026-08-24-physics-p3-shapes-design.md) · [P3 plan](../superpowers/plans/2026-08-24-physics-p3-shapes.md) ·
[P4 spec](../superpowers/specs/2026-08-24-physics-p4-raycast-design.md) · [P4 plan](../superpowers/plans/2026-08-24-physics-p4-raycast.md) ·
[P5 spec](../superpowers/specs/2026-08-24-physics-p5-contacts-design.md) · [P5 plan](../superpowers/plans/2026-08-24-physics-p5-contacts.md).
Also still open: post-v0.1 Studio work (Blender live-link; asset-browser follow-ups —
type-aware filtering, thumbnails, disk import, clear-to-nil))

## Where we are

- Docs, architecture, and the licensing/CLA groundwork are in place.
- **[RFC 0001: Core Data Model](../rfcs/0001-core-data-model.md) is Accepted.** The
  four questions that gated the foundation are settled (see the RFC's *Resolved
  decisions*), so the data-model portion of **Phase 0 — Foundation** is unblocked
  and ready to implement.

**ECS core ([#9](https://github.com/Evol-Luci/mosaic/issues/9)) is built and
merged.** The C++ toolchain is stood up (CMake + CMakePresets, C++20, Catch2 via
FetchContent), and the first foundation code is in place:

- `foundation/core/entity_id.hpp` — `EntityID`, the **32 index / 32 generation**
  handle; `EntityID(0)` null; slot **retired** (not recycled) on generation exhaustion.
- `engine/ecs/` — `EntityRegistry` (slot table, free-list recycle, stale-handle
  detection, retirement), sparse-set component storage (`ComponentRegistry` +
  a minimal `ComponentTypeID` shim), and the declaration-only `System` contract.
- `tests/unit/ecs/` — unit tests, all green.

**Serialization ([#10](https://github.com/Evol-Luci/mosaic/issues/10)) is built and
merged.** Reflection-driven, lossless cross-format projection; normative stance in
[RFC 0001 *Serialization stance*](../rfcs/0001-core-data-model.md#serialization-stance):

- `foundation/serialization/` — `Value` DOM, `AssetID`/`Handle<T>`/`ReferenceResolver`,
  reflection bridge (`to_value`/`from_value`), JSON backend (nlohmann), TOML backend
  (toml++), and error handling via `SerializationError`. `from_json`/`from_toml`
  guard against adversarial deeply-nested input with a configurable
  nesting-depth cap (default 128) that throws `SerializationError` rather than
  overflowing the stack — the non-blocking hardening follow-up from the #10 review. ✓
- `tests/unit/serialization/` — unit tests for every tag: `[value]`, `[reference]`,
  `[reflect]`, `[refenc]`, `[json]`, `[toml]`, `[crossfmt]` — 66 tests, all green.

**Command / event system ([#11](https://github.com/Evol-Luci/mosaic/issues/11)) is
built and merged.** The last Phase 0 foundation primitive; normative semantics live
in [design/command-event-system.md](../design/command-event-system.md), and
`required_permissions` ties into
[design/automation-and-agents.md](../design/automation-and-agents.md):

- `foundation/commands/` — `Command` (the `id, parameters, required_permissions,
  execute(), undo(), serialize()` contract, with a default `serialize()` that
  projects `{ id, parameters, required_permissions }` through the serialization
  `Value` DOM) and `CommandStack`, the undoable transaction log (execute pushes +
  clears redo; undo inverts LIFO; redo re-executes).
- `foundation/events/` — `EventBus`: synchronous subscribers fire in-tick before
  `emit` returns; deferred subscribers are served from a **double-buffered queue**
  drained once per tick, FIFO in global emit order across all event types, never
  mid-tick. A re-emit from inside a drain defers to the next tick.
- `tests/unit/events/` + `tests/unit/commands/` — `[events]`, `[commands]` unit
  tests plus a command/event integration test (`[commands][events]`): a deferred
  event emitted by a command announces on the next drain, and **undo announces
  nothing** — the "enqueue on execute, not undo" acceptance. 80 tests, all green.
  - *Interpretation note for the World layer:* the RFC says a deferred event
    enqueues "on execute, not when undone/redone." `CommandStack::redo()` re-runs
    the command's `execute()`, so a redo **does** re-announce — events live in the
    command body, not the log, and the UI must observe a redo. This resolves the
    RFC's ambiguity in the natural direction (redo == re-execute); flag it if the
    World/UI layer needs redo to be silent.

The three Phase 0 foundation implementation issues (#9, #10, #11) are now complete.

**The runtime `World` (Engine layer) is built and merged.** The first Engine unit
after the foundation primitives, designed and planned in
[specs/2026-08-16-engine-world-runtime-design.md](../superpowers/specs/2026-08-16-engine-world-runtime-design.md)
and [plans/2026-08-16-engine-world-runtime.md](../superpowers/plans/2026-08-16-engine-world-runtime.md):

- `engine/world/` — `World`: owns the `EntityRegistry`, `ComponentRegistry`, and a
  per-world `EventBus`; delegates entity/component ops; runs registered systems.
  `tick(dt)` does exactly three things in order: **drain deferred events** (the
  #11 fixed sync point), **run systems** in registration order, **commit deferred
  destructions** (`on_destroy → destroy`). `destroy_entity` is deferred so systems
  never have entities vanish mid-`for_each`.
- `engine/ecs/` additions — `SparseSet::each` / `ComponentRegistry::for_each<T>`
  single-component iteration, and `System::update(World&, dt)` (default no-op).
- `tests/unit/world/` + `tests/unit/ecs/` — `[world]` cases plus the interleaved
  create/add/destroy/recycle integration test the #9 handoff asked for. Full suite
  95 tests, all green.

Deliberately **out of scope** (their own future specs): Scene load/save, concrete
systems (e.g. transform), and the outer runtime loop. `World` exposes `tick(dt)`;
nothing calls it in a loop yet.

**Reflection system ([#18](https://github.com/Evol-Luci/mosaic/issues/18)) is built
and merged.** The gap `World` deliberately left open — reflection-driven *generic*
component construction — is now closed. Designed and planned in
[specs/2026-08-16-reflection-system-design.md](../superpowers/specs/2026-08-16-reflection-system-design.md)
and [plans/2026-08-16-reflection-system.md](../superpowers/plans/2026-08-16-reflection-system.md);
normative intent in [design/reflection.md](../design/reflection.md):

- `foundation/reflection/` — `type_id.hpp` (`TypeID` + `type_id<T>()`, the stable
  within-run type handle) and `type_registry.hpp` (`TypeRegistry`: register a
  reflected type by stable name, look it up by name **or** `TypeID`, enumerate its
  fields via the existing `reflect()` visitor). Pure foundation — no `World`.
- `engine/reflection/component_reflection.hpp` — `ComponentReflection`: the
  engine-layer bridge. `register_component<T>(registry, "Name")` records metadata
  **and** an attach thunk; `construct(world, e, name, value)` deserializes a
  `Value` into a fresh `T` (via the #10 `from_value`) and attaches it to an entity
  — **no concrete C++ type at the call site**. This is the path Scene load/save
  will call.
- The #9 `ComponentTypeID` shim is now a **re-export** of
  `foundation::reflection::TypeID`: one identity source, no drift. `ComponentRegistry`
  is unchanged and now shares its per-type integer with `TypeInfo::id`.
- `tests/unit/reflection/` — `[reflection]` cases: register/lookup by name & id,
  field enumeration, generic construct-and-attach into a `World`, the
  reflect → `to_value` → construct round-trip (nested + vector fields), and
  unknown-name / invalid-entity rejection. Full suite 107 tests, all green.
  - *Durability rule for future workers:* the **persisted** identity of a type is
    its **name** (a string) — Scene files, replication, any on-disk form key on it.
    `TypeID` is a runtime-only handle and may vary in assignment order across runs;
    never persist it.

**Scene load/save ([#19](https://github.com/Evol-Luci/mosaic/issues/19)) is built
and merged.** The first persistence path — a `World`'s entity+component graph to
JSON and back. Designed and planned in
[specs/2026-08-16-scene-load-save-design.md](../superpowers/specs/2026-08-16-scene-load-save-design.md)
and [plans/2026-08-16-scene-load-save.md](../superpowers/plans/2026-08-16-scene-load-save.md):

- `engine/scene/scene.hpp` — `to_value`/`from_value` (World ↔ scene `Value`) and
  `save_to_json`/`load_from_json` (via the #10 backend). Header-only, in-memory
  only; filesystem I/O is a caller / asset-registry concern.
- `engine/ecs/` + `engine/world/` — `EntityRegistry::for_each` /
  `World::for_each_entity` live-entity enumeration (also feeds the future editor
  hierarchy), so component-less entities round-trip faithfully.
- `engine/reflection/` — `ComponentReflection` gained the **save** direction
  (`save`/`has`) symmetric to `construct`; per-entity component order is
  deterministic (sorted type name).
- **Load is two-pass:** create every entity + build a serialized-id → new-handle
  remap, then generically construct components through a `SceneReferenceResolver`
  so intra-scene `EntityID` references remap to the loaded handles. An unknown
  component type name throws `SerializationError`; a reference outside the scene
  resolves to null. `AssetID`/`Handle` ids round-trip now; *resolving* them to
  live resources is the asset registry (#20).
- `tests/unit/scene/` — `[scene]` cases: populated round-trip (empties preserved),
  non-trivial reference remap (bumped generation), broken-ref → null, unknown-type
  throw, malformed-JSON throw, JSON convenience round-trip. Full suite 120 tests,
  all green.

**Asset registry ([#20](https://github.com/Evol-Luci/mosaic/issues/20)) is built
and merged.** The runtime-side registry — resolve/cache `AssetID` → loaded
resources, validate references on deserialize. Designed and planned in
[specs/2026-08-16-asset-registry-design.md](../superpowers/specs/2026-08-16-asset-registry-design.md)
and [plans/2026-08-16-asset-registry.md](../superpowers/plans/2026-08-16-asset-registry.md):

- `engine/assets/asset_metadata.hpp` — `AssetMetadata` (uuid, type, version, source,
  dependencies, open-ended metadata/import_settings), reflected, round-tripping
  through TOML via #10.
- `engine/assets/asset_registry.hpp` — `AssetRegistry`: `register_asset(meta, loader)`
  (lazy) / `register_resource<T>` (eager), `load<T>(id)` resolves + caches
  (loader invoked once), `metadata`/`contains`. Resources are type-erased
  (`shared_ptr<void>` + `load<T>` cast) — no `Resource` base class, since no concrete
  resource types exist until rendering. Missing id → null/nil, non-throwing.
- `AssetReferenceResolver` (same header) — the #10 `ReferenceResolver` hook: on
  deserialize a registered `AssetID`/`Handle` passes through, an unknown one becomes
  a nil handle.
- `foundation/serialization/reference.hpp` gained `std::hash<AssetID>` (mirrors
  `EntityID`), the natural cross-reference key.
- `tests/unit/assets/` — `[assets]` cases: hash-as-map-key, metadata TOML round-trip,
  register/cached-load (loader-once), eager resource, unknown → null, Handle
  validate/nil on deserialize. Full suite 127 tests, all green.
  - **Deferred (small follow-up):** wiring `AssetReferenceResolver` *into* Scene's
    load path (a combined entity-remap + asset-validation resolver), and any real
    loader/resource type (rendering), importer, and cooker.

**Foundation math + transform system ([#21](https://github.com/Evol-Luci/mosaic/issues/21),
[#26](https://github.com/Evol-Luci/mosaic/issues/26)) is built and merged.** The
first concrete `System`, and the math layer the rest of the slice inherits. Designed
and planned in
[specs/2026-08-16-transform-system-design.md](../superpowers/specs/2026-08-16-transform-system-design.md)
and [plans/2026-08-16-transform-system.md](../superpowers/plans/2026-08-16-transform-system.md):

- `foundation/math/{vec3,quat,mat4}.hpp` — minimal, hand-rolled (no GLM), header-only.
  **Conventions now fixed for rendering/physics:** column-major storage, column
  vectors, right-handed, Y-up, radians; `world = parent * local`, `local = T*R*S`.
  `Vec3`/`Quat` are reflected so transforms serialize.
- `engine/transform/transform.hpp` — reflected `Transform` (authored local
  position/rotation/scale + optional `parent` EntityID, nil = root) and the derived
  `WorldTransform{ Mat4 }` (system-written, not serialized).
- `engine/transform/transform_system.hpp` — `TransformSystem`, the first concrete
  `System`. `update` walks each entity's parent chain (`world(e) = world(parent) *
  local(e)`) and writes its `WorldTransform`; a depth guard bounds malformed cycles.
- `tests/unit/math/` + `tests/unit/transform/` — `[math]` (Vec3/Quat/Mat4 ops, TRS,
  transform_point) and `[transform]` (single→world, parent compose, rotated parent,
  moving a parent updates children next tick, reflection round-trip). Full suite 142
  tests, all green.
  - *Perf follow-up:* `update` recomputes each entity by re-walking ancestors —
    memoize per tick or process parent-first when scenes grow.

**Runtime loop ([#22](https://github.com/Evol-Luci/mosaic/issues/22)) is built and
merged.** The outer driver that finally *runs* the engine. Designed and planned in
[specs/2026-08-16-runtime-loop-design.md](../superpowers/specs/2026-08-16-runtime-loop-design.md)
and [plans/2026-08-16-runtime-loop.md](../superpowers/plans/2026-08-16-runtime-loop.md):

- `engine/runtime/runtime_loop.hpp` — `RuntimeLoop`: a deterministic fixed-timestep
  accumulator. `advance(world, real_dt)` banks externally-supplied elapsed time and
  dispatches whole `World::tick(fixed_dt)` calls; `step` is manual single-step;
  `start`/`stop` pause/resume; a `max_steps_per_advance` cap guards the spiral of
  death. Owns pacing **only** — the per-tick order stays in `World::tick`.
- The real `std::chrono` measure-and-sleep frame pump is deliberately **not** here —
  it belongs to the app/CLI (`mosaic run`) that owns the process main loop; the loop
  consumes an injected `real_dt` so it stays deterministic and testable.
- `tests/unit/runtime/` — `[runtime]` cases: fixed dispatch + remainder carry,
  start/stop/single-step, determinism (same schedule → equal tick count),
  spiral-of-death cap, and a populated-`World` + `TransformSystem` run stable over
  1000 ticks. Full suite 149 tests, all green.

**The headless engine core of the v0.1 vertical slice is now complete** (#9 ECS,
#10 serialization, #11 commands/events, the runtime `World`, #18 reflection, #19
Scene, #20 assets, #21 transform + #26 math, #22 runtime loop).

**Logging ([#33](https://github.com/Evol-Luci/mosaic/issues/33)) is built** (in
[PR #39](https://github.com/Evol-Luci/mosaic/pull/39)). The nearest of the remaining
foundation pieces — a leveled, categorized, thread-safe logging facility every later
subsystem (app/CLI, rendering, assets, jobs) consumes. Designed and planned in
[specs/2026-08-16-logging-design.md](../superpowers/specs/2026-08-16-logging-design.md)
and [plans/2026-08-16-logging.md](../superpowers/plans/2026-08-16-logging.md):

- `foundation/logging/log.hpp` — `Level` (trace/debug/info/warn/error), `Record`, a
  `noexcept` `Sink` + `ConsoleSink`, and a `Logger` (sink list + `std::atomic`
  min-level + a `std::mutex` serializing dispatch; below-threshold messages drop
  without taking the lock). A process-global `std::format`-style free-function
  surface (`log::info(cat, fmt, ...)`) forwards to a function-local-static default
  `Logger`, so promoting `Logger` to a public *injectable* API later is additive.
- Header-only and stdlib-only — honors the foundation tier contract.
- `tests/unit/logging/` — `[logging]` cases: level routing, min-level filter, the
  default `Info` level, multiple sinks, `std::format` substitution, exact
  `ConsoleSink` output, and a concurrent 8×500 no-lost/no-torn test whose sink is
  **deliberately unguarded** so the `Logger` mutex is the real subject under test
  (proven a genuine detector: removing the lock aborts the test). Full suite 160
  tests, all green.
  - *Deferred (post-v0.1):* lazy-eval logging macros with `source_location` capture,
    and file/rotating/JSON sinks + async queue (all named out of scope in #33).

**App / CLI shell ([#35](https://github.com/Evol-Luci/mosaic/issues/35)) is built** —
the engine now *runs a world outside a test*. `mosaic run` owns a `main` and the
real-time frame pump the runtime loop deliberately left out. Designed and planned in
[specs/2026-08-17-app-cli-shell-design.md](../superpowers/specs/2026-08-17-app-cli-shell-design.md)
and [plans/2026-08-17-app-cli-shell.md](../superpowers/plans/2026-08-17-app-cli-shell.md):

- New top-level `app/` tier: a `mosaic_app` library holding all logic behind test
  seams + a thin `mosaic` executable. `app/cli/` (CLI11 arg parsing → a plain
  `Options`; CLI11 confined to the app tier), `app/run/frame_pump.hpp` (the
  `FramePump` — measures wall-clock dt, drives `RuntimeLoop::advance`, sleeps to a
  target rate, stops on SIGINT / `--duration` / `--ticks`; clock/sleep/stop are
  **injected** as plain-`double`-seconds seams so tests run with no real time),
  `app/run/status_reporter.*` (`--json` periodic + summary lines, human-readable via
  logging #33), and `app/main.cpp` (real `std::chrono` + SIGINT wiring).
- `engine/reflection/builtin_components.hpp` — a shared `register_builtin_components`
  (registers `Transform`) the app and the future editor both call, so their component
  sets can't drift.
- `tests/unit/app/` — `[cli]`/`[app]` cases: arg parsing, the injected-clock pump
  (exact `--ticks` count, duration/signal stop, progress cadence — the pump is the
  real subject, `RuntimeLoop(fixed_dt, 1)` makes counts exact), non-positive
  `--fps`/`--fixed-hz` rejected, and the JSON reporter shapes. Full suite 175, all
  green; `mosaic run --ticks 10 --json` emits a summary and exits 0.
  - *Deferred (future Tools issues):* `mosaic create/editor/build`, a project
    generator, and the asset compiler — the CLI is structured for those subcommands.

**Jobs ([#34](https://github.com/Evol-Luci/mosaic/issues/34)) is built** — the last
remaining v0.1 foundation piece. A minimal task system for parallel work. Designed and
planned in
[specs/2026-08-17-jobs-design.md](../superpowers/specs/2026-08-17-jobs-design.md)
and [plans/2026-08-17-jobs.md](../superpowers/plans/2026-08-17-jobs.md):

- `foundation/jobs/thread_pool.{hpp,cpp}` — `ThreadPool`: N worker threads pulling FIFO
  from a mutex/condvar queue. `submit(task)` returns `std::future<void>` (a throwing
  task surfaces on `.get()`); `parallel_for(begin, end, body, grain)` splits a range
  into up to `thread_count` chunks, **blocks on an internal latch** until all complete,
  and rethrows the first body exception from the calling thread. The destructor
  **drains the queue then joins** — no leaked/detached threads, no unfulfilled future.
- Compiled `mosaic_jobs` STATIC lib (stdlib threads only, no third-party dep), wired via
  `find_package(Threads)` — honors the foundation tier contract.
- `tests/unit/jobs/` — `[jobs]` cases: submit-runs-all, future exception propagation,
  drain-on-shutdown (1000 tasks), single-thread pool, `parallel_for` exact coverage,
  empty range, body-exception rethrow, custom grain. Full suite 186, all green.
  - *Deferred (post-v0.1, per #34):* work-stealing, job dependencies/graphs, priorities,
    fibers, and **wiring the pool into `World::tick`** for parallel system execution
    (honoring each `System`'s `ComponentAccess`) — a separate engine follow-up; today
    ticking is single-threaded by design.

## What to build next

The engine is now **visible and operable**: `mosaic run --window` renders, and `mosaic
editor` opens the Studio with a hierarchy + a live reflection inspector. The v0.1 foundation
(logging #33, app/CLI #35, jobs #34) and rendering (#36 / 36a–36e-1) are complete; the
frontier is the remainder of the **Studio (#37)** — the viewport, then play/stop + console.
Per the [v0.1 scope](roadmap.md#the-v01-vertical-slice--the-first-mountain), each sub-issue
gets its own brainstorm → spec → plan → TDD cycle:

- **First *visible* subsystem** — rendering [#36](https://github.com/Evol-Luci/mosaic/issues/36)
  (Vulkan, **complete for v0.1**) then the Studio shell [#37](https://github.com/Evol-Luci/mosaic/issues/37)
  (**underway** — see the Studio decomposition below); the Studio depends on rendering.
  **#36 was decomposed** (design:
  [specs/2026-08-17-rendering-decomposition.md](../superpowers/specs/2026-08-17-rendering-decomposition.md))
  into five linear sub-issues:
  1. ~~[#42](https://github.com/Evol-Luci/mosaic/issues/42) 36a — device + window +
     swapchain (clear-screen)~~ ✓ **built.** The backend-neutral `RenderDevice`
     interface + `modules/render_vulkan` (GLFW, swapchain + resize recreation, per-frame
     sync, debug validation → logging) are in place; `mosaic run --window` clears the
     window each frame. Design/plan: [spec](../superpowers/specs/2026-08-17-render-36a-clear-screen-design.md) · [plan](../superpowers/plans/2026-08-17-render-36a-clear-screen.md).
  2. ~~[#43](https://github.com/Evol-Luci/mosaic/issues/43) 36b — mesh draw at `WorldTransform`~~ ✓ **built.** A `RenderSystem` (render-time traversal, fixed camera) draws entities with `WorldTransform` + `MeshRenderer` via a Vulkan graphics pipeline (glslc SPIR-V, depth buffer); `mosaic run --window` shows a spinning cube. `foundation/math` gained `perspective`/`look_at`. Design/plan: [spec](../superpowers/specs/2026-08-18-render-36b-mesh-draw-design.md) · [plan](../superpowers/plans/2026-08-18-render-36b-mesh-draw.md).
  3. ~~[#44](https://github.com/Evol-Luci/mosaic/issues/44) 36c — camera + view/projection~~ ✓ **built.** A reflected `Camera` component (lens: fov/near/far) placed via a `Transform`; the `RenderSystem` derives `view = inverse_affine(camera WorldTransform)` from the active camera (fallback if none). `foundation/math` gained `inverse_affine`. Design/plan: [spec](../superpowers/specs/2026-08-18-render-36c-camera-design.md) · [plan](../superpowers/plans/2026-08-18-render-36c-camera.md).
  4. [#45](https://github.com/Evol-Luci/mosaic/issues/45) 36d — material + texture + directional light — **split into two**:
     - ~~36d-1 (lighting)~~ ✓ **built.** Reflected `Material{base_color}` + `DirectionalLight` components; `Vertex` gained a normal; Lambert + ambient shading via object-space light in push constants (no descriptor sets). `foundation/math` gained `transform_direction`. Design/plan: [spec](../superpowers/specs/2026-08-18-render-36d1-lighting-design.md) · [plan](../superpowers/plans/2026-08-18-render-36d1-lighting.md).
     - ~~36d-2 (texture)~~ ✓ **built.** UVs on `Vertex`; `create_texture` + a Vulkan sampler + **descriptor sets**; an `Image` asset resource + a `TextureCache` resolving `Material.texture` (`AssetID`) through the asset registry (#20, cached/uploaded once); a procedural checkerboard demo. `foundation/math` gained `Vec2`. Design/plan: [spec](../superpowers/specs/2026-08-22-render-36d2-texture-design.md) · [plan](../superpowers/plans/2026-08-22-render-36d2-texture.md).
  5. [#46](https://github.com/Evol-Luci/mosaic/issues/46) 36e — point light + minimal render graph — **split into two**:
     - ~~36e-1 (point light)~~ ✓ **built.** A reflected `PointLight` component (`color`, `intensity`, `range`; world position derived from its entity's `WorldTransform`, like `Camera`); object-space Lambert point term with windowed inverse-square attenuation added to the fragment shader; the vertex shader now passes object-space fragment position; push constants grew 112→144 bytes (device `maxPushConstantsSize` assert). `mosaic run --window` now shows point-light distance falloff across the spinning cube alongside the existing directional light. Known limitation: attenuation distance is measured in object space, so non-uniform mesh scale warps it (exact for the unit-scale demo cube). Design/plan: [spec](../superpowers/specs/2026-08-22-render-36e1-point-light-design.md) · [plan](../superpowers/plans/2026-08-22-render-36e1-point-light.md).
     - **Render graph deferred** — a point light does not create a second pass, so the "≥2 passes justify a graph" bar is not met; re-filed to the first slice that genuinely needs ≥2 passes (a shadow-map depth pre-pass, or the Studio's offscreen-viewport-to-texture in #37). Not dropped. **Update: 37d met the ≥2-pass bar but still chose direct two-pass submission** (see 37d below); the RenderGraph is now re-filed to the first *dynamic* or ≥3-pass slice.

  Decisions fixed in the decomposition: **GLFW** windowing (backend/app only, never
  engine/foundation); the **RenderGraph is deferred past 36e** (direct submission behind
  the neutral device interface until ≥2 passes justify a graph — 36e-1 did not meet
  that bar; deferred to the next ≥2-pass slice, which is **37d**). Each sub-issue gets its
  own brainstorm → spec → plan → TDD cycle.

- **Studio shell** [#37](https://github.com/Evol-Luci/mosaic/issues/37) — the editor
  front-end, and where reflection (#18) becomes user-visible. **Decomposed** (design:
  [specs/2026-08-22-studio-decomposition.md](../superpowers/specs/2026-08-22-studio-decomposition.md))
  into five slices — v0.1 scope is the **lean core + console** (asset browser + Blender
  live-link deferred), GUI is **Dear ImGui (docking)**:
  1. ~~37a — editor shell~~ ✓ **built.** New `studio/` tier + `mosaic_studio`; a `mosaic
     editor` subcommand opens an ImGui docking window (dockspace + menu bar + placeholder
     panels); three default-no-op UI hooks on `RenderDevice` driven by the Vulkan backend.
     Design/plan: [spec](../superpowers/specs/2026-08-22-studio-37a-editor-shell-design.md) · [plan](../superpowers/plans/2026-08-22-studio-37a-editor-shell.md).
  2. ~~37b — scene hierarchy~~ ✓ **built.** A `SelectionModel` + `build_hierarchy(World)`
     seam; the Hierarchy panel lists entities (via `World::for_each_entity`) with
     click-to-select. Folded in two 37a review follow-ups. Design/plan:
     [spec](../superpowers/specs/2026-08-23-studio-37b-hierarchy-design.md) · [plan](../superpowers/plans/2026-08-23-studio-37b-hierarchy.md).
  3. ~~37c — reflection inspector~~ ✓ **built** (the #18 payoff). A mutable `FieldVisitor`
     + a `reflect()`-bridging adapter (the shared reflected-property API) + `ComponentReflection::components_on`/`inspect`;
     the Inspector edits a selected entity's component fields **live** (float `DragFloat`,
     others read-only). Design/plan: [spec](../superpowers/specs/2026-08-23-studio-37c-inspector-design.md) · [plan](../superpowers/plans/2026-08-23-studio-37c-inspector.md).
  4. ~~37d — viewport~~ ✓ **built.** A dockable **Viewport** panel shows the live 3D scene
     rendered into a panel-sized **offscreen** color+depth target and sampled by ImGui
     (`ImGui::Image`). The neutral `RenderDevice` gained four default-no-op viewport hooks
     (`set_viewport_size`/`begin_viewport_pass`/`end_viewport_pass`/`viewport_texture`); the
     Vulkan backend implements them behind a **lazy swapchain-pass** refactor so `mosaic run
     --window` and `mosaic editor` share one device unchanged. A headless `ViewportPanel`
     model + a `render_scene_to_viewport` seam carry the testable sizing/ordering; `mosaic
     editor` now renders the shared demo scene each frame. **RenderGraph decision:** minimal
     **direct two-pass submission** (one command buffer, one submit; inter-pass hazard via the
     offscreen pass `finalLayout` + subpass dependencies) — the graph stays deferred to the
     first dynamic/≥3-pass slice. Design/plan: [spec](../superpowers/specs/2026-08-23-studio-37d-viewport-design.md) · [plan](../superpowers/plans/2026-08-23-studio-37d-viewport.md).
  5. ~~37e — play/stop + console~~ ✓ **built.** A headless `PlayState` drives the
     `RuntimeLoop` from the editor frame loop (pause = `stop()`, resume = `start()`, Step =
     one `RuntimeLoop::step()` while paused); Play/Stop/Step live in the menu bar. A
     thread-safe `ConsoleBuffer` (capacity-capped ring, owned copies of the transient
     `Record` strings) + a `ConsoleSink : log::Sink` registered on the process-global logger
     capture #33 log output; the Console panel renders it. **Surfaced a gap:** the app
     installs no sink outside the editor, so `mosaic run` drops all logging — filed as a
     follow-up (install a stderr `ConsoleSink` in the CLI entry path). Design/plan:
     [spec](../superpowers/specs/2026-08-23-studio-37e-play-stop-console-design.md) · [plan](../superpowers/plans/2026-08-23-studio-37e-play-stop-console.md).

**Suggested order (rendering):** ~~#42 (36a)~~ ✓ → ~~#43 (36b)~~ ✓ → ~~#44 (36c)~~ ✓ →
~~36d-1 (lighting)~~ ✓ → ~~36d-2 (texture)~~ ✓ → ~~36e-1 (point light)~~ ✓ → **rendering complete.**

**Suggested order (Studio #37):** ~~37a (shell)~~ ✓ → ~~37b (hierarchy)~~ ✓ →
~~37c (inspector)~~ ✓ → ~~37d (viewport)~~ ✓ → ~~37e (play/stop + console)~~ ✓ →
**Studio #37 v0.1 lean core complete.**

**Full v0.1 order:** ~~#33 logging~~ ✓ → ~~#35 app/CLI shell~~ ✓ → ~~#34 jobs~~ ✓ →
~~#36 decompose~~ ✓ → ~~#42–#46 rendering (36a–36e-1)~~ ✓ → ~~37a (shell)~~ ✓ →
~~37b (hierarchy)~~ ✓ → ~~37c (inspector)~~ ✓ → ~~37d (viewport)~~ ✓ → ~~37e (play/stop + console)~~ ✓ →
**v0.1 Studio (#37) lean core complete.**

## Suggested sequence

```
#9 ECS core ✓ ──┬── #10 Serialization ✓
                └── #11 Command/event system ✓
                        │
                        └── World runtime ✓ ──┬── #18 reflection ✓ ── #19 Scene load/save ✓
                                              ├── #20 asset registry ✓
                                              ├── #21 transform system ✓ (+ foundation math #26)
                                              └── #22 runtime loop ✓
```

**The v0.1 headless engine core is complete.** The next frontier is the app/CLI
shell, the first visible subsystem (rendering / studio), and the remaining
foundation pieces (logging, jobs) — see *What to build next*.

#9, #10, #11, the World runtime, #18 reflection, #19 Scene load/save, #20 asset
registry, #21 transform system (+ foundation math #26), and #22 runtime loop are all
done. **The v0.1 headless engine core is complete** — the frontier moves off this
tree to the app/CLI shell, the first visible subsystem, and the remaining foundation
pieces (see *What to build next*).

## Ground rules for the worker

- Honor the tier/dependency contract in
  [ARCHITECTURE.md](../architecture/ARCHITECTURE.md): `foundation/` knows nothing
  above it; `engine/` depends only on `foundation/`.
- Each issue lists acceptance criteria and expects unit tests under `tests/unit/`.
- ~~Reflection metadata is a soft dependency for #10; #9 shipped a minimal
  `ComponentTypeID` shim (type identity only). The reflection system reconciles
  it — and the `World` needs its *generic* construction path before Scene
  load/save is buildable.~~ **Done by #18:** `ComponentTypeID` is now a re-export
  of `foundation::reflection::TypeID` (one identity source), and
  `engine/reflection/component_reflection.hpp` supplies the generic construction
  path Scene load/save calls.
- ~~ECS handoff: own the `on_destroy(e)` → `EntityRegistry::destroy(e)` sequencing
  and add an interleaved create/add/destroy/recycle integration test.~~ **Done by
  the `World`:** `tick` commits deferred destructions in that order, and
  `tests/unit/world/world_integration_test.cpp` covers the interleaved cycle.
- Event-drain handoff: the `World::tick` owns the once-per-tick
  `EventBus::drain_deferred()` at the tick's start. The still-open piece is the
  **runtime loop** that calls `World::tick(dt)` each frame — there is no scheduler
  yet, by design.
- Known tech debt ([#25](https://github.com/Evol-Luci/mosaic/issues/25), post-v0.1):
  `type_id<T>()` shares one global counter across all reflected types, so
  `ComponentRegistry::storages_` is sized by the max id of *any* reflected type
  (null gaps for non-components). Accepted, correctness-safe for v0.1; give
  components a dense id space later without reintroducing the #9 identity drift.
- If a decision in RFC 0001 turns out to be wrong under implementation, don't
  silently deviate — open an issue proposing an amendment and link the RFC. (See
  the redo/announce interpretation note under *Where we are* — a resolved
  ambiguity, not a deviation, but worth revisiting at the World/UI layer.)

## After the engine core

The **v0.1 headless engine core is complete**: entities, serialization,
commands/events, the runtime `World`, reflection, Scene load/save, the asset
registry, foundation math, the transform system, and the runtime loop. Everything
composes into a world that can be populated, serialized, reloaded, and ticked
deterministically, and **`mosaic run` now runs it in real time outside a test** — but
nothing is *visible* yet.

The **v0.1 foundation is now complete** too — logging
[#33](https://github.com/Evol-Luci/mosaic/issues/33), the app/CLI shell
[#35](https://github.com/Evol-Luci/mosaic/issues/35), and jobs
[#34](https://github.com/Evol-Luci/mosaic/issues/34) are all built. The one remaining
on-ramp is the first *visible* subsystem (its own future spec):
- **First visible subsystem** — Vulkan rendering
  [#36](https://github.com/Evol-Luci/mosaic/issues/36) (decomposed into #42–#46;
  see [the decomposition](../superpowers/specs/2026-08-17-rendering-decomposition.md))
  then the Studio shell [#37](https://github.com/Evol-Luci/mosaic/issues/37). The
  milestone that makes the engine something you can look at and operate (Studio depends on
  rendering). **Rendering 36a–36e-1 is complete** (`mosaic run --window` draws a spinning
  cube, viewed through a `Camera`, lit by a directional + point light and textured with a
  checkerboard resolved through the asset registry); the render-graph half of #46 remains
  deferred (not dropped) — 37d met the ≥2-pass bar but chose direct two-pass submission, so
  the graph is re-filed to the first dynamic/≥3-pass slice. **Studio #37 is underway**
  (decomposed into 37a–37e; see [the decomposition](../superpowers/specs/2026-08-22-studio-decomposition.md)):
  **all five slices — 37a (editor shell), 37b (hierarchy), 37c (reflection inspector), 37d
  (viewport), 37e (play/stop + console) — are built.** `mosaic editor` opens a docking window
  with a scene hierarchy, a live reflection inspector, a viewport showing the live 3D scene via
  an offscreen render target, and a play/stop/step toolbar + Console panel. **The v0.1 Studio
  (#37) lean core is complete.** Deferred post-v0.1 (their own future work): the asset browser
  and the Blender live-link preview. A small follow-up also stands: install a stderr
  `ConsoleSink` in the CLI entry path so `mosaic run` output (currently dropped) is visible.

Reassess this file as each lands.
