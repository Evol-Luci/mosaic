# Next Steps — Build Handoff

This is the live "you are here" for a worker picking up Mosaic. It points at the
next buildable work and the decisions that constrain it. When the work below is
done, update this file to point at what comes next.

**Last updated:** 2026-09-08 (**UI pillar — U2a (FontAtlas cooked asset) DONE** — `mosaic import <proj> font.ttf` bakes a glyph atlas + per-codepoint metrics via **stb_truetype at cook time** (vendored single header, walled in `app/asset/font_cook.cpp` — no stb symbol above the importer, mirroring the WAV parser); a neutral `FontAtlas` (`engine/ui/font_atlas.hpp`: RGBA8 atlas with glyph coverage in the **alpha** channel + `Glyph` metrics + vertical metrics) is serialized as a `"font"` asset (`.font` `MOSAFNT1` blob, byte-exact round-trip) and registered by `load_project_assets` — mirrors the T5 image / A3 audio cook pipelines. Headless-tested (read_ttf metrics on a vendored Liberation/OFL fixture, cook/uncook round-trip + reject, import→load end-to-end); full suite green + real `mosaic import` CLI round-trip verified (`type = 'font'`). Nothing renders yet — **U2b** (UiText component + string layout + a glyph draw seam + a Vulkan text pipeline + demo text) is next. spec/plan: [U2a spec](../superpowers/specs/2026-09-08-ui-u2a-font-atlas-asset-design.md) · [U2a plan](../superpowers/plans/2026-09-08-ui-u2a-font-atlas-asset.md). **UI pillar — U1 (screen-space HUD foundation) DONE** (PR [#128](https://github.com/Evol-Luci/mosaic/pull/128)) — reflected `UiPanel` + a pure `resolve_rect` layout model + a `draw_ui` seam + a topmost RenderSystem HUD pass + a Vulkan screen-space quad pipeline + a demo HUD bar; headless-tested, GPU visual maintainer-verified. **UI decomposition:** U1 → **U2a done** → **U2b** (text render) → U3 images/sprites → U4 interaction → U5 layout/hierarchy + world-space. **VFX pillar COMPLETE** — trails (billboard + ribbon), **decals** (textured world quads), and **screen effects** (fullscreen fade/tint/vignette overlay) all shipped, GPU visuals maintainer-verified; **VFX — V2 ribbon trails DONE** — trails render as a connected camera-facing ribbon via a new `draw_ribbons` seam + Vulkan triangle pipeline (visual is maintainer-verified); **VFX pillar started — V1 trails** — a `TrailRenderer`/`TrailSystem` fading tail; demo trail on the falling sphere; **Audio pillar COMPLETE (A1–A4)** — `AudioSystem` + a real miniaudio device wired into `setup_runtime`; `mosaic run --window` plays a 3D demo tone on the orbiter (audible check is the maintainer's); **Audio — A3** — cooked `.wav`→`AudioClip` asset (`mosaic import foo.wav`); 16-bit-PCM/float32 WAV parser, loads + plays through the null backend — done; **Audio pillar — A2** — a real miniaudio backend (`modules/audio_miniaudio`) implementing the neutral `AudioBackend`, proven headless on miniaudio's null device (no hardware in the sandbox) — done; **Audio pillar — A1** — neutral `AudioBackend` + reflected `AudioSource`/`AudioListener` + an `AudioSystem` driving voices from ECS state, headless via a fake backend (no device yet) — done; **particle cubic easing** — EaseCubicIn/Out/InOut added to the over-life easing set — done; **particle sub-frame spawn interpolation** — a moving emitter's continuous spawns spread along its swept path instead of clumping; orbiter trail demo — done; **script-driven particle emission** — a script bumps a scripted emitter's `burst_trigger` to fire bursts; demo puffer added — done; **particle triggered/re-armed bursts** — a `burst_trigger` edge counter re-fires a burst at runtime — done; **particle over-life easing** — per-property Linear/EaseIn/EaseOut/EaseInOut curves behind the over-life seam — done; **particle over-life curves** — color/size/alpha interpolate start→end over each particle's life — done; **particle rendering** — instanced billboards — done; **particles** Phase-4 subsystem done; v0.1 foundation + rendering + physics + Project & Tools Workflow
(T1–T5) complete; **Scripting (Python) pillar COMPLETE — S1 (Python embedding), S2 (reflection→Python
property bindings), S3 (world/commands), S4 (ScriptComponent), AND S5 (Studio script console) ALL COMPLETE.
The v0.1 vertical slice is COMPLETE.**
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
+ a shared `libpython`, e.g. distro `python3-dev`). S1 design/plan:
[S1 spec](../superpowers/specs/2026-08-30-scripting-s1-embedding-design.md) ·
[S1 plan](../superpowers/plans/2026-08-30-scripting-s1-embedding.md).
**S2** wires reflected component properties into the Python side via a `Value` DOM bridge
(`value_to_py`/`py_to_value` in `modules/script_python/value_marshal.hpp`). The engine's #18
`ComponentReflection` serializes a component to a `Value` tree; the bridge marshals it into a
Python dict; the script mutates it; the bridge converts back; and `ComponentReflection` applies the
updated `Value` tree. Two `cpp_function` globals (`get_component`/`set_component`) are injected
into each engine namespace by `bind_world`, keyed on entity `uint64` ID and reflected component
name. Unknown component names return `None`/`False` (never throw). 344 tests green (4 new script
property tests: mutate-and-observe, read-current-values, error-return, set_global marshal;
2 new marshal tests: round-trip every DOM scalar/container, uint64→Python int). S2 design/plan:
[S2 spec](../superpowers/specs/2026-08-30-scripting-s2-properties-design.md) ·
[S2 plan](../superpowers/plans/2026-08-30-scripting-s2-properties.md).
**S3** exposes entity lifecycle and undoable commands to Python. `bind_world` now installs six
entity builtins: `create_entity()` (immediate, returns uint64 id), `destroy_entity(eid)` (deferred
to `world.tick()`, returns bool), `entity_count()`, `entities()` (list of ids), `is_valid(eid)`,
and replaces the S2 direct `set_component` with a stack-aware version. A new neutral
`ScriptEngine::bind_commands(CommandStack&)` (pure-virtual, `engine/script/script_engine.hpp`;
only adds `#include "foundation/commands/command_stack.hpp"` — still no pybind11 above the module
wall) installs `undo()`/`redo()`/`undo_depth()` and routes `set_component` through a
`SetComponentCommand` when a stack is bound: `execute()` + `valid()` check + `push_executed` —
a rejected set is not recorded. Without a bound stack, `set_component` falls through to the S2
direct path (S2 tests unaffected). 355 tests green (4 new script tests: entity lifecycle
deferred-destroy, undoable set + undo + redo, failed-set not recorded, no-stack backward compat).
**S4** delivers the core payoff — "attach a Python script, press Play." A reflected
`ScriptComponent { std::string source; }` (`engine/script/script_component.hpp`, Python-free,
registered as `"ScriptComponent"`) holds a Python script defining `on_update(dt)`; a
`ScriptSystem` (`engine/script/script_system.hpp`) runs each scripted entity's `on_update(dt)` once
per `World::tick`. `ScriptSystem` is an `ecs::System` that drives the neutral `ScriptEngine` via an
injected factory (`std::function<unique_ptr<ScriptEngine>()>`), so the engine tier stays
Python-free; it caches one engine per scripted entity (compile source once, recompile on change,
prune the unseen), sets the `entity` global once and `dt` each tick, and ignores per-tick errors
(non-fatal — a throwing script skips its mutation without crashing the tick). Per-tick mutations use
the S2 direct path (no undo spam). v0.1 inlines the source in the component; a `.py`-asset reference
is a post-v0.1 refinement. 361 tests green (6 new: ScriptComponent reflection round-trip; ScriptSystem
determinism/bit-identical, per-entity state persistence, recompile-on-source-change, throwing-on_update
non-fatal, syntax-error-in-source non-fatal). S4 design/plan:
[S4 spec](../superpowers/specs/2026-08-30-scripting-s4-scriptcomponent-design.md) ·
[S4 plan](../superpowers/plans/2026-08-30-scripting-s4-scriptcomponent.md).
**S5** (Studio script console) is now **COMPLETE**. A "Script Console" panel in `mosaic editor`
hosts a multiline input area + Run/Clear buttons wired to `ScriptConsoleModel` (Task 2). The model
drives the persistent CPython REPL against the live editor `World` and a `CommandStack`; history is
colour-coded (input/output/error). The CPython backend (`mosaic_script_python`) is injected at the
app tier (`app/run/editor.cpp`) — `studio/` remains Python-free. 367 tests green (no new tests —
this task is GUI glue; the console logic is covered by S5 Task 2's headless tests).
**Deferred:** "Play-from-script" (running a script against the `PlayState` / `RuntimeLoop` from the
console) needs a neutral host-callback design; not part of v0.1. S5 design/plan:
[S5 spec](../superpowers/specs/2026-08-30-scripting-s5-studio-console/spec.md) ·
[S5 plan](../superpowers/plans/2026-08-30-scripting-s5-studio-console.md).
**Runtime integration (post-S5):** `ScriptSystem` is now wired into `setup_runtime` (before
`PhysicsSystem`) — the scaffolding both `mosaic editor` and the windowed `mosaic run` (a built
bundle's entry point) call — so a `ScriptComponent` **ticks on Play in the editor AND in a built
standalone**, closing the v0.1 "attach a script → Play → Build a standalone that plays" loop. The
default scene ships an orbiting scripted green cube (`ScriptComponent`, no RigidBody) that serializes
and survives `save → mosaic build → mosaic run`. An editor "Demo: physics" toolbar checkbox swaps
that object between script-driven (orbits) and physics-driven (falls) via a pure `set_demo_physics()`
(a script and a dynamic RigidBody can't both drive one Transform, so it swaps rather than layers).
371 tests green (2 new: the `setup_runtime` wiring tick-test + the toggle swap). `demo_scene.hpp` /
`studio/` stay Python-free (the CPython factory is injected at the app tier). **Deferred (roadmap):**
a dedicated slider / color-picker / Euler-rotation **controls panel** (editor-UX beyond v0.1's
"inspector" scope — the Inspector already edits position/rotation/scale/color as drag-fields);
**generic add/remove-component editing in the Inspector** for the selected entity — structural
composition (attach/detach whole components on the current selection, e.g. a `RigidBody` or a
`ScriptComponent`), of which the demo-physics toolbar toggle is a hardcoded, single-object precursor.
(Deliberately distinct from the controls-panel item above: that one is nicer *widgets for existing
field values*; this one is *adding and removing components*, which the Inspector cannot do at all
today.) Scripting in the **headless** `mosaic run` (no `--window`, which doesn't call
`setup_runtime`).
Design/plan: [spec](../superpowers/specs/2026-09-02-scripting-runtime-integration-design.md) ·
[plan](../superpowers/plans/2026-09-02-scripting-runtime-integration.md).
S3 design/plan:
[S3 spec](../superpowers/specs/2026-08-30-scripting-s3-world-commands-design.md) ·
[S3 plan](../superpowers/plans/2026-08-30-scripting-s3-world-commands.md).
**Post-integration fix (current tip of `main`):** `import_asset` now **refuses to overwrite on a
stem collision** — returns `nullopt` + logs instead of silently clobbering an existing asset and
orphaning its `AssetID` (`app/asset/asset_io.hpp`; a T5 follow-up, PR #83, salvaged from an orphaned
worktree). **372 tests green; `main` is clean, zero open PRs.**
**Phase 3 (Programming) — first hot-reload slice: the Studio Script Editor.** A new **Script Editor**
panel in `mosaic editor` edits the *selected* entity's `ScriptComponent` source: **Apply**
test-compiles the edit in a throwaway `ScriptEngine` (bound to the live world, exactly as
`ScriptSystem` compiles) and shows green "compiled OK" / a red error; on success it writes the source
into the component, so `ScriptSystem` **recompiles it live** — edit a script, watch the behavior
change on Play without a restart. A pure `ScriptEditorModel` (`studio/script_editor.hpp`) holds the
validate+apply logic (unit-tested); the ImGui panel is thin glue; `studio/` stays Python-free (the
CPython factory is injected at the app tier). 376 tests green (4 new: valid-apply-writes,
syntax-error-rejected-and-unchanged, no-`ScriptComponent`, null-factory). **Deferred (documented):**
external `.py`-file editing (edit in your own editor, reload on disk change — the same recompile
seam) and attaching a script to an unscripted entity (rides on the deferred generic add/remove-
component in the Inspector). Design/plan:
[spec](../superpowers/specs/2026-09-02-studio-script-editor-design.md) ·
[plan](../superpowers/plans/2026-09-02-studio-script-editor.md).

## Moving forward — the v0.1 vertical slice is DONE

v0.1 is **complete and shippable**: `mosaic create` → import an asset → open the Studio → move
objects → add physics → attach/write a Python script → **Play** → **Build a standalone that plays on
its own**. Foundation, rendering (#36), physics (P1–P5), tools (T1–T5), scripting (S1–S5), and the
runtime integration are all merged and verified (372 tests, functional + visual acceptance passed).

**Priority order (set by the maintainer, 2026-09-03):** **Editor UX → Scripting thread → More Phase 3
→ Physics / engine (fully headless)** — work the buckets below in that order (the Editor-UX
add/remove-component item, now done, was first because it unblocks attach-script-to-any-entity).
Each item still gets its own brainstorm → spec → plan → TDD cycle.

**A. Next roadmap phase** (see [roadmap.md](roadmap.md)):
- **Phase 3 — Programming** (deepen the layer just built): script **hot-reload — done** (Script
  Editor, external `.py` reload, `.py`-as-asset); **broader scripting API — first slice done**
  (`add_component`/`remove_component`/`has_component`/`component_names`/`entities_with` — scripts can
  now furnish `create_entity()` results and query composition by name; design:
  [spec](../superpowers/specs/2026-09-04-broader-script-api-design.md)). **`raycast()` — done**
  (scripts call `raycast(origin, dir, max)` → `{hit, entity, distance, point, normal}`, wired through
  a neutral `ScriptHost.raycast` seam so the script backend stays physics-agnostic; the runtime injects
  a `PhysicsSystem`-backed callback via `make_physics_raycast`; design:
  [spec](../superpowers/specs/2026-09-04-script-raycast-design.md)). **Events — done** (scripts
  `emit(name, payload)` and receive via an `on_event(name, payload)` hook; a neutral `ScriptEvent`
  rides the deferred bus, so `ScriptSystem` delivers it one tick later, FIFO; inline mode; design:
  [spec](../superpowers/specs/2026-09-04-script-events-design.md)). **Physics contact callbacks — done**
  (`setup_runtime` bridges each `ContactEvent` to a `'contact'` script event with
  `{phase, a, b, point, normal, separation, normal_impulse}`, delivered to `on_event` — scripts react
  to collisions/landings). **Broader-API bucket complete.** **Profiler — done** (`ScriptSystem` times
  each entity's `on_update`, exposes `profile()`; `mosaic run --script-profile` dumps the timing table
  sorted by cost; design: [spec](../superpowers/specs/2026-09-04-script-profiler-design.md)).
  **Debugger — done** (tracepoint inspection: a neutral `ScriptDebugger` holds breakpoints; the Python
  backend installs a non-blocking `sys.settrace` hook that records hits + local-variable snapshots; the
  Script Editor's `debug()` runs source + `on_update` once and shows hits/locals — no pause, so no
  UI-thread deadlock; design: [spec](../superpowers/specs/2026-09-05-script-debugger-design.md)).
  **✅ Phase 3 — Programming is complete.** Deferred Phase-3 niceties (own slices later): true
  **pause/step** debugging on the worker thread (the foundation is in place), richer math/input/time
  script helpers, events + profiling in threaded mode, and per-script-source profile aggregation.
  *(Watch: the threaded `ScriptWorker` unit test flaked once under heavy parallel `ctest` load —
  passed on rerun; likely contention, revisit if it recurs.)*
- **Phase 4 — Production:** animation, audio, particles, VFX, UI, navigation, networking, sequencer.
  **Animation — first slice done** (property-path keyframes: a neutral `AnimationClip`/`Track`/`Keyframe`
  model + `sample()` (linear/step, clamped); an `AnimationPlayer` component (reflected, serializes with
  the scene); a generic `AnimationSystem` that writes any reflected scalar by property path
  (`"Transform.position.x"`) via `component_value`/`set_value`; wired into `setup_runtime` after scripts,
  before physics; design: [spec](../superpowers/specs/2026-09-05-animation-system-design.md)).
  Animation follow-ups: clip-as-asset (`AssetID` import), cubic/bezier easing, per-component track
  grouping (perf), skeletal/blend trees, animation events. **Sequencer — done** (a director timeline:
  a `Sequence` component owns clip-tracks bound to target entities + start offsets, and event markers;
  `SequenceSystem` advances a master clock, samples active clips onto their targets via the shared
  `apply_clip_at`, and fires markers as `emit(name)` script events → scripts react through `on_event`;
  design: [spec](../superpowers/specs/2026-09-05-sequencer-design.md)). Sequencer follow-ups: auto-stop/
  clamp a non-looping sequence, marker payloads, clip blending, a timeline editor panel. **Navigation —
  done** (A\* pathfinding: a neutral `NavGrid` (2D, blocked cells) + `find_path` (8-connected, octile
  heuristic, no corner-cutting); a `NavAgent` component (goal cell/speed/active); a `NavigationSystem`
  that steers an agent's Transform toward its goal along the path, with a per-agent cached step-target
  so A\* only recomputes at cell centers (avoids re-rounding onto a blocked cell mid-move); grid injected
  into the system; design: [spec](../superpowers/specs/2026-09-05-navigation-design.md)). Navigation
  follow-ups: script `find_path()` seam, baking a grid from scene colliders, navmesh / 3D, dynamic
  obstacles, path smoothing, and `setup_runtime` wiring (a game supplies the grid). **Particles —
  done** (a neutral `Particle`/`Rng` (seeded splitmix64)/`integrate` model; a reflected
  `ParticleEmitter` component (emitting/rate/burst/lifetime/initial_velocity/spread/acceleration/
  seed/max_particles); a generic `ParticleSystem` that fires a burst on activation and runs
  rate-based continuous emission via a carried accumulator, seeds per-emitter deterministic velocity
  spread, applies constant acceleration, ages out expired particles, caps pools to `max_particles`,
  maintains transient per-emitter particle pools keyed by entity and pruned on destroy, exposes a
  `particles_for(EntityID)` query seam, and spawns at the entity's `Transform` origin; wired into
  `setup_runtime`; headless & bit-identical deterministic; design:
  [spec](../superpowers/specs/2026-09-06-particles-design.md)). **Particle rendering — DONE** (instanced camera-facing billboards via the neutral `draw_particles` seam, alpha-fade over life, per-emitter color/size, a Vulkan alpha-blended pipeline for swapchain + offscreen viewport, and a demo fountain in the default scene; design/plan: [spec](../superpowers/specs/2026-09-06-particle-rendering-design.md) · [plan](../superpowers/plans/2026-09-06-particle-rendering.md)). **Color/size/alpha over-life curves — DONE**
  (`ParticleEmitter`'s `color`/`size` are now the *start* values; added `end_color`/`end_size` plus an authored `alpha`/`end_alpha` pair, interpolated linearly by `t = age/lifetime` through a pure `color_at`/`size_at`/`alpha_at` seam the renderer consumes — defaults reproduce the previous constant-color, constant-size, 1→0 alpha fade; the demo fountain fades orange→magenta and shrinks. Easing is the documented next step behind the same seam; design/plan:
  [spec](../superpowers/specs/2026-09-06-particle-overlife-curves-design.md) · [plan](../superpowers/plans/2026-09-06-particle-overlife-curves.md)). **Per-property over-life easing — DONE**
  (added `color_easing`/`size_easing`/`alpha_easing` int fields (`enum Easing`: Linear/EaseIn/EaseOut/EaseInOut) and a pure `apply_easing(mode, t)` remap applied to `t` inside `color_at`/`size_at`/`alpha_at` before the lerp — every curve maps 0→0 and 1→1 so over-life endpoints stay exact, unknown modes fall back to linear; defaults (0=Linear) reproduce the prior linear interpolation, renderer unchanged; the demo fountain now holds opacity then fades fast (`alpha_easing = EaseOut`); design/plan:
  [spec](../superpowers/specs/2026-09-06-particle-easing-design.md) · [plan](../superpowers/plans/2026-09-06-particle-easing.md)). **Triggered / re-armed bursts — DONE**
  (a reflected `burst_trigger` int on `ParticleEmitter`; `ParticleSystem` tracks the last value it acted on per emitter and fires one burst per observed change — inequality, not delta — via a shared `fire_burst`, without mutating the component; the on-sight one-shot burst is preserved and default `0` is unchanged; renderer untouched. A default-scene demo is **deferred to the script-driven-emission slice**, which supplies the trigger stimulus; design/plan:
  [spec](../superpowers/specs/2026-09-06-particle-triggered-bursts-design.md) · [plan](../superpowers/plans/2026-09-06-particle-triggered-bursts.md)). **Script-driven emission — DONE**
  (needed **no new engine/binding code** — scripts already mutate any reflected component via the generic `get_component`/`set_component`, and `ScriptSystem` runs before `ParticleSystem`, so a scripted entity does `c = get_component(entity,'ParticleEmitter'); c['burst_trigger'] += 1; set_component(entity,'ParticleEmitter',c)` to drive bursts (or toggle `emitting`/`rate`). Shipped an end-to-end integration test (`tests/unit/app/script_runtime_test.cpp`) and the deferred **bursts demo**: a scripted cyan "puffer" in the default scene that bursts every ~1.2s (`app/run/demo_scene.hpp::kBurstScript`). Bounded slice — no separate spec/plan). **Sub-frame spawn interpolation — DONE**
  (a moving emitter's **continuous** spawns are spread along the path it swept this tick (`prev → curr`, fractions `(i+0.5)/n`) instead of clumping at the current position; bursts stay instantaneous; no component field, and a stationary emitter (`prev == curr`) is bit-identical to before; the demo orbiter cube now trails green→yellow particles smoothly; design/plan:
  [spec](../superpowers/specs/2026-09-06-particle-subframe-spawn-design.md) · [plan](../superpowers/plans/2026-09-06-particle-subframe-spawn.md)). **More easing curves (cubic) — DONE**
  (added `EaseCubicIn`/`EaseCubicOut`/`EaseCubicInOut` (modes 4/5/6) to `apply_easing` — monotonic, stay within [0,1]; bounded slice, no demo change. Elastic/bounce were deliberately deferred: they *overshoot* [0,1], so they need a range-handling design (esp. for alpha) before adding). Particles follow-ups:
  elastic/bounce easing (needs overshoot handling); deeper: sub-frame *age* integration (back-date a spawn's age to its sub-frame time).
  **Audio — A1 DONE** (the Audio pillar's architecture, headless: a neutral `AudioBackend` interface (play/stop/gain/pitch/position/is_playing/listener) behind the usual module wall; reflected `AudioSource` (clip/gain/pitch/looping/playing/spatial) + an `AudioListener` marker, registered as built-ins; an `AudioSystem` (injected backend) that edge-triggers play/stop on `playing`, pushes gain/pitch changes and spatial position from the entity's Transform, sets the listener pose from the `AudioListener` entity's Transform, and stops voices on prune — writing nothing back to components, deterministic (no `is_playing` polling). Proven headless with a recording `FakeAudioBackend` (10 tests); design/plan:
  [spec](../superpowers/specs/2026-09-06-audio-a1-design.md) · [plan](../superpowers/plans/2026-09-06-audio-a1.md)).
  **Audio — A2 DONE** (a real backend: `modules/audio_miniaudio` implements the neutral `AudioBackend` on **miniaudio** v0.11.21 (single header via FetchContent, `ma_*` walled in the `.cpp`). Two neutral-interface refinements landed: `play()` now carries `bool spatial` (backend disables 3D for UI sounds) and a neutral `register_clip(id, f32 PCM, frames, channels, rate)` seam supplies decoded audio. `register_clip` stores PCM per `AssetID`; `play` builds a per-voice `ma_audio_buffer`+`ma_sound` (volume/pitch/looping/spatialization) and returns a `VoiceId`; stop/gain/pitch/position/is_playing/listener map to `ma_*`. `create_miniaudio_backend(null_device)` factory. Since the sandbox has no audio device, **proven headless on miniaudio's null backend** (8 tests incl. `AudioSystem` driving the real backend); a feasibility spike validated the null-device path first. Audible playback on hardware is for the maintainer to confirm. design/plan:
  [spec](../superpowers/specs/2026-09-07-audio-a2-miniaudio-design.md) · [plan](../superpowers/plans/2026-09-07-audio-a2-miniaudio.md)).
  **Audio — A3 DONE** (cooked audio-clip asset, mirroring the T5 image pipeline: a neutral `AudioClip` resource (interleaved f32 PCM) + `app/asset/audio_cook.hpp` — `read_wav` parses uncompressed **16-bit-PCM / 32-bit-float** WAV (incl. `WAVE_FORMAT_EXTENSIBLE`) to f32 with our own minimal RIFF parser (no miniaudio in the importer — decode at cook), `cook_audio`/`uncook_audio` a compact `MOSAAUD1` blob. `import_asset` gained a `.wav` branch (→ `.audioclip` + `.toml`, overwrite-safe) and `load_project_assets` an `"audio"` branch that registers an `AudioClip` resource — so `mosaic import <proj> foo.wav` produces a loadable clip. Verified end-to-end headless incl. a loaded `AudioClip` playing through the miniaudio **null** backend; design/plan:
  [spec](../superpowers/specs/2026-09-07-audio-a3-clip-asset-design.md) · [plan](../superpowers/plans/2026-09-07-audio-a3-clip-asset.md)).
  **Audio — A4 DONE — the Audio pillar (A1–A4) is COMPLETE.** `setup_runtime` gained an injected (defaulted-null) `AudioBackend` and adds `AudioSystem`; when a real backend is present it registers a procedural demo tone (`kToneClipId`, `make_demo_tone`). The default scene now has an `AudioListener` on the camera and a looping **3D** `AudioSource` on the orbiter (pans as it circles). A testable `register_audio_clips(backend, registry)` hands cooked project clips to the backend. `mosaic run --window` creates a real miniaudio device (`create_miniaudio_backend()`; headless runs + the editor + all tests stay silent via the null default). Verified headless: the demo tone starts a voice through the runtime on the **null** device, a null backend is safe, `register_audio_clips` works, default-scene counts unchanged (audio attached to existing entities), full suite 507 green, `mosaic run --window` exits 0. **Audible hardware playback is the maintainer's check.** design/plan:
  [spec](../superpowers/specs/2026-09-07-audio-a4-runtime-wiring-design.md) · [plan](../superpowers/plans/2026-09-07-audio-a4-runtime-wiring.md)). **Audio follow-ups (post-pillar) are tracked as GitHub issues** ([#117](https://github.com/Evol-Luci/mosaic/issues/117) editor audio · [#118](https://github.com/Evol-Luci/mosaic/issues/118) listener orientation from camera rotation · [#119](https://github.com/Evol-Luci/mosaic/issues/119) attenuation/rolloff + doppler · [#120](https://github.com/Evol-Luci/mosaic/issues/120) one-shot cleanup + voice cap/stealing · [#121](https://github.com/Evol-Luci/mosaic/issues/121) 24-bit/`.mp3`/`.ogg` import · [#122](https://github.com/Evol-Luci/mosaic/issues/122) verify+demo script control of `AudioSource`) — all non-blocking polish; prioritize against Phase 4 as desired.
  **VFX — V1 (trails) DONE** (the VFX pillar's first slice: a reflected `TrailRenderer` component + a `TrailSystem` that samples each moving entity's `Transform.position` into a bounded per-entity history (transient state, seed-head-on-first-sight, one sample/`sample_interval`, ring-buffer `max_points`, prune on destroy, `points_for` query seam). Rendered as a fading **billboard chain** by appending tapered `ParticleInstance`s (per point: `frac=(i+1)/n` → size/alpha taper) into the existing `RenderSystem` billboard pass — **reusing `draw_particles`, no new device method or Vulkan pipeline**. Wired into `setup_runtime`; demo trail on the falling sphere (pale-blue tail). Headless-tested (sampling/cap/prune/determinism/reflection/registration + render taper via `FakeRenderDevice`); design/plan: [spec](../superpowers/specs/2026-09-07-vfx-trails-design.md) · [plan](../superpowers/plans/2026-09-07-vfx-trails.md)).
  **VFX — V2 (ribbon trails) DONE** — trails now render as a **connected camera-facing ribbon** instead of a billboard chain: the engine CPU-expands each trail's points into tapered world-space triangles (`engine/vfx/ribbon.hpp::build_ribbon`, `side = cross(seg_dir, dir_to_camera)` with fallbacks, width+alpha taper), submitted through a new neutral `draw_ribbons` seam (`RibbonVertex`/`RibbonDrawParams` on `RenderDevice`) and drawn by an alpha-blended Vulkan **triangle pipeline** (`ribbon.vert`/`ribbon.frag`, per-vertex pos/color/alpha, swapchain + viewport variants, modeled on the particle pipeline). Headless-tested (geometry + a `FakeRenderDevice` `draw_ribbons` call: 6 verts/segment, no trail in the particle batch); build + `mosaic run --window` exits 0 with no Vulkan validation errors; **the ribbon visual is maintainer-verified on hardware** (GPU output isn't observable in the sandbox). design/plan: [spec](../superpowers/specs/2026-09-07-vfx-ribbon-trails-design.md) · [plan](../superpowers/plans/2026-09-07-vfx-ribbon-trails.md)).
  **VFX — V3 (decals) DONE** — a reflected `Decal` component (texture/color/alpha) renders a world-oriented, alpha-blended textured quad placed by its `WorldTransform`; `RenderSystem` resolves the texture via the existing `TextureCache` and composes the MVP, submitting a new `draw_decals` seam; a Vulkan textured-quad pipeline reuses `texture_set_layout_` (shader-generated quad, no vertex buffer). Demo: a striped decal on the ground. design/plan: [spec](../superpowers/specs/2026-09-07-vfx-decals-design.md) · [plan](../superpowers/plans/2026-09-07-vfx-decals.md).
  **VFX — V4 (screen effects) DONE** — a reflected `ScreenEffect` component (tint/tint_strength/vignette) drives a fullscreen alpha **overlay** (fade/flash/color-grade + radial vignette) via a new `draw_screen_effect` seam + a fullscreen-triangle Vulkan pipeline (no scene sampling; composites tint + vignette in one blend); scripts can animate `tint_strength` for fades. Demo: a subtle vignette on the camera. design/plan: [spec](../superpowers/specs/2026-09-07-vfx-screen-effects-design.md) · [plan](../superpowers/plans/2026-09-07-vfx-screen-effects.md).
  **✅ VFX pillar COMPLETE (trails, decals, screen effects).** All GPU visuals maintainer-verified on hardware (not observable in the sandbox); headless geometry/wiring + build + `run --window` exit-0 verified. **VFX follow-ups:** scene-sampling post (bloom/blur/DOF — needs render-to-texture), projected-onto-geometry decals, ribbon texture / width- & color-over-length curves + miter joins. **Remaining Phase-4 subsystems: UI, networking.**

**B. Accumulated v0.1 follow-ups** (smaller, well-scoped):
- **Editor UX:** **generic add/remove-component in the Inspector — DONE** (a `+ Add Component` combo +
  a per-component Remove button, generic over reflection via `registered_names()`/`add_default()`;
  unblocks attaching a `ScriptComponent` to any entity; design/plan:
  [spec](../superpowers/specs/2026-09-03-inspector-add-remove-component-design.md) ·
  [plan](../superpowers/plans/2026-09-03-inspector-add-remove-component.md)). **Viewport mouse-picking
  — DONE** (click an object to select it, consumes `PhysicsSystem::raycast`). **Controls panel — DONE**
  (a dedicated panel with position/scale drags, Euler-angle rotation **sliders in degrees**, and a
  base-color **picker** for the selected entity's `Transform`/`Material`; design/plan:
  [spec](../superpowers/specs/2026-09-03-transform-controls-panel-design.md) ·
  [plan](../superpowers/plans/2026-09-03-transform-controls-panel.md)). Still open: in-editor
  **File → Reload**; undoable add/remove & controls edits via a `CommandStack`.
- **Scripting:** **external `.py`-file hot-reload — DONE** (a `ScriptComponent.path` loads its script
  from a file and `ScriptSystem` reloads/recompiles it live on an on-disk edit, via mtime-polling;
  design/plan: [spec](../superpowers/specs/2026-09-03-script-file-hotreload-design.md) ·
  [plan](../superpowers/plans/2026-09-03-script-file-hotreload.md)). **Headless `mosaic run` scripting
  — DONE** (`setup_runtime` is hoisted out of the window block, so a windowless `mosaic run` ticks
  `ScriptSystem` + `PhysicsSystem`; verified by a headless frame-pump integration test and a live
  `--ticks` run emitting physics contacts; design/plan:
  [spec](../superpowers/specs/2026-09-03-headless-run-scripting-design.md) ·
  [plan](../superpowers/plans/2026-09-03-headless-run-scripting.md)). **Play-from-script — DONE** (a
  neutral `ScriptHost` host-callback bound into every engine; scripts call `stop()`/`pause()`/`play()`,
  wired in `main.cpp` so a headless script can end the run — verified by backend, system, and
  frame-pump integration tests; design/plan:
  [spec](../superpowers/specs/2026-09-03-play-from-script-design.md) ·
  [plan](../superpowers/plans/2026-09-03-play-from-script.md)). **`.py`-as-asset — DONE**
  (`mosaic import <proj> foo.py` cooks a `type="script"` asset (`.script` blob + `.toml`);
  `load_project_assets` registers it as a `ScriptSource`; `ScriptComponent.script` references it by
  `AssetID` and `ScriptSystem` resolves it via the registry (precedence asset-id → path → inline);
  design/plan: [spec](../superpowers/specs/2026-09-03-py-as-asset-design.md) ·
  [plan](../superpowers/plans/2026-09-03-py-as-asset.md)). **Script worker thread — DONE** (opt-in
  `mosaic run --script-thread`: a dedicated Python-owning worker thread runs `on_update`s against an
  immutable snapshot and emits a write batch the main thread applies one tick later, so a heavy
  script never stalls the frame; the `ScriptContext`/`SnapshotContext` sandbox seam + `bind_context`
  keep it race-free, inline stays the default; design/plan:
  [spec](../superpowers/specs/2026-09-03-script-worker-thread-design.md) ·
  [plan](../superpowers/plans/2026-09-03-script-worker-thread.md)). **Honest caveats:** CPython's
  single GIL means no *parallelism* across scripts (the win is decoupling, not speed); uniform
  ~1-frame result latency in threaded mode; entity-lifecycle (`create_entity`/`destroy_entity`) is
  unsupported in threaded mode (would race `World`). **This completes the scripting bucket** — future
  scripting work: per-interpreter GILs (PEP 684) for real parallelism, dirty/partial snapshots, an OS
  **file-watcher** for live re-import (replacing the `.py` mtime-poll), and a thin **editor
  `PlayState`-from-script** wiring (map the host to `studio.play_state()` in `editor.cpp`).
- **Studio (post-v0.1):** **Blender live-link** preview; asset-browser follow-ups (type filtering,
  thumbnails, disk import, clear-to-nil).
- **Physics:** **contact manifolds — DONE** (Begin `ContactEvent`s now carry world `point` / `normal`
  / `separation` / `normal_impulse` from the Box3D manifold via `b3Contact_GetData`, tier-wall-clean;
  design/plan: [spec](../superpowers/specs/2026-09-03-physics-contact-manifolds-design.md) ·
  [plan](../superpowers/plans/2026-09-03-physics-contact-manifolds.md)). Still open:
  record/replay-to-disk; the **hit-event** channel (above-threshold point/normal/approach-speed) +
  multi-point / friction impulses.
- **Engine:** wire the **job pool into `World::tick`** for parallel systems (honoring each system's
  `ComponentAccess`); a dense component id space ([#25](https://github.com/Evol-Luci/mosaic/issues/25)).

When the next slice is chosen, brainstorm it and update this file to point at it. The sections below
are the historical build record (most-recent-first).

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
importer/cooker + `build_bundle` copies assets) are all done. **Physics follow-ups:**
sphere/capsule demo meshes (done), **contact manifolds — DONE** (point/normal/separation/impulse on
Begin `ContactEvent`s); **viewport mouse-picking — DONE** (click an object in the viewport to select
it, via `PhysicsSystem::raycast` + a camera unproject; spec/plan:
[spec](../superpowers/specs/2026-09-03-viewport-mouse-picking-design.md) ·
[plan](../superpowers/plans/2026-09-03-viewport-mouse-picking.md)). Still-deferred: full
record/replay-to-disk, hit-event contacts + multi-point/friction impulses, collider-less picking + gizmos.
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
