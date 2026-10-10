# Changelog

All notable changes to the Mosaic project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased] — 2026-09-09 / 2026-09-10

### Milestones: UI Pillar Complete (U1–U5, PR #128–#135) · Networking Pillar Launched (N1–N2, PR #136–#137) · Test Suite at 585 Passing Tests

Detailed summaries available in [`docs/updates/2026-09-10-summary.md`](docs/updates/2026-09-10-summary.md), [`docs/updates/2026-09-09-summary.md`](docs/updates/2026-09-09-summary.md), and [`docs/project/advancement-plans-and-evaluation.md`](docs/project/advancement-plans-and-evaluation.md).

#### Added
- **Networking Subsystem (N1–N2 Slices Launched, PR #136, #137):**
  - **N1 Neutral Transport Seam & In-Memory Loopback (PR #136):**
    - Pure-virtual `Transport` interface (`engine/net/transport.hpp`) with `send(conn, channel, bytes)`, `poll() -> vector<NetEvent>`, and `disconnect(conn)`.
    - `Channel` types (`Reliable`, `Unreliable`) and `NetEvent` structs (`Connected`, `Disconnected`, `Message`).
    - Deterministic in-memory `LoopbackTransport` (`engine/net/loopback_transport.hpp`) using synchronized FIFO queues for headless CI and multi-peer testing without OS sockets or port conflicts.
    - Added unit test suite in `tests/unit/net/loopback_transport_test.cpp` (6 tests).
  - **N2 Session Lifecycle & Protocol Handshake (PR #137):**
    - Binary protocol framing in `engine/net/message.hpp` (`MessageType::Hello = 1`, `MessageType::Welcome = 2`) with little-endian serialization helpers (`put_u32`, `get_u32`).
    - `NetworkEvent` bus types (`PeerJoined`, `PeerLeft`, `SessionEstablished`, `Disconnected`) in `engine/net/network_event.hpp`.
    - `NetworkSystem` (`engine/net/network_system.hpp`) implementing `ecs::System` with `Role::Server` and `Role::Client`. Non-blocking Hello/Welcome handshake, monotonic session ID assignment (starting from 1), session table management, and disconnect cleanup.
    - Added unit test suites in `tests/unit/net/message_test.cpp` and `tests/unit/net/network_system_test.cpp` (7 tests).
- **UI Subsystem (U1–U5 Complete, PR #128–#135):**
  - **U5a Layout / Hierarchy (PR #133):**
    - `resolve_in(frame, anchor, pos, size)` in `engine/ui/ui_rect.hpp` supporting frame-relative layout.
    - Reflected `UiParent` component (`engine/ui/ui_parent.hpp`) linking child UI elements to parent container entities.
    - `ui_hierarchy` resolver (`engine/ui/ui_hierarchy.hpp`) with cycle-safe, depth-capped (16) frame resolution.
    - Nested text scaling in `engine/ui/text_layout.hpp` (`layout_text` with parent frame parameter).
    - Routed panels, images, text, and button hit-testing through hierarchy resolution; added demo nested HUD bar text.
  - **U5b World-Space Text Labels — Pillar Complete (PR #134):**
    - Reflected `WorldLabel` component (`engine/ui/world_label.hpp`) with font asset, text, size, color, alpha, and offset.
    - Aspect-correct camera-facing billboard text generator (`build_world_text` in `engine/ui/world_text.hpp`).
    - `draw_world_text` pure-virtual seam on `RenderDevice` (`WorldGlyphVertex`).
    - In-world render pass in `RenderSystem` running after meshes/billboards with camera billboard orientation and font atlas binding.
    - Dedicated Vulkan pipeline (`modules/render_vulkan/shaders/world_text.*`) with depth-testing enabled and depth writes disabled.
    - Added floating "Orbiter" world label to demo scene.
- **Audio Subsystem (A1–A4 Complete, PR #113, #114, #115, #116):**
  - Neutral `AudioBackend` interface (`engine/audio/audio_backend.hpp`) with `play`, `stop`, `set_gain`, `set_pitch`, `set_position`, `is_playing`, `set_listener`, `register_clip`.
  - Reflected `AudioSource` and `AudioListener` ECS components in `engine/audio/audio_components.hpp`.
  - `AudioSystem` in `engine/audio/audio_system.hpp` driving playback, pitch/gain, and 3D spatial positioning from ECS state.
  - Miniaudio device backend in `modules/audio_miniaudio/` wrapping miniaudio v0.11.21; supports headless null-device testing.
  - Neutral `AudioClip` resource (`engine/audio/audio_clip.hpp`) and standalone pure-C++ RIFF/WAV cooker (`app/asset/audio_cook.hpp`) supporting 16-bit PCM and 32-bit float WAVs into `MOSAAUD1` blobs.
  - Asset pipeline `.wav` import in `app/asset/asset_io.hpp` and project clip registration.
  - Runtime integration in `setup_runtime` and `mosaic run --window` with procedural 3D demo tone on orbiter.
  - Tracked post-pillar audio follow-ups as GitHub issues #117–#122 (PR #123).
- **VFX Subsystem (V1–V4 Complete, PR #124, #125, #126, #127):**
  - Reflected `TrailRenderer` component (`engine/vfx/trail_renderer.hpp`) and `TrailSystem` (`engine/vfx/trail_system.hpp`) tracking ring-buffered position history.
  - Initial billboard-chain trail rendering (V1) reusing `draw_particles`.
  - Connected camera-facing ribbon trail geometry (`build_ribbon` in `engine/vfx/ribbon.hpp`), `draw_ribbons` seam on `RenderDevice`, and Vulkan ribbon triangle pipeline (`modules/render_vulkan/shaders/ribbon.*`).
  - Reflected `Decal` component (`engine/vfx/decal.hpp`), `draw_decals` seam on `RenderDevice`, and Vulkan textured quad pipeline (`modules/render_vulkan/shaders/decal.*`).
  - Reflected `ScreenEffect` component (`engine/vfx/screen_effect.hpp`), `draw_screen_effect` seam on `RenderDevice`, and fullscreen triangle overlay pipeline (`modules/render_vulkan/shaders/screen_effect.*`) with tint and radial vignette composite.
  - Default scene demo enhancements: ribbon trail on falling physics sphere, ground decal, and camera vignette.

#### Fixed
- **UI Viewport Inversion (PR #135):**
  - Resolved screen-space UI elements rendering vertically mirrored under Vulkan negative-height viewports by mapping `clip.y = 1.0 - 2.0 * ny` across `ui.vert`, `ui_image.vert`, and `ui_text.vert`.

#### Testing
- Expanded test suite to **585 unit and integration test cases** (100% passing across all subsystems).

---

## [2026-08-23 / 2026-08-24]

### Milestone: Mosaic Studio v0.1 Lean Core Complete (#37 / PR #56, #58) · Post-v0.1 Asset Browser (PR #62) · Vulkan Validation & Synchronization Polish (PR #60, #61)

Detailed summaries available in [`docs/updates/2026-08-23-summary.md`](docs/updates/2026-08-23-summary.md), [`docs/updates/2026-08-21-summary.md`](docs/updates/2026-08-21-summary.md), [`docs/updates/2026-08-18-summary.md`](docs/updates/2026-08-18-summary.md), [`docs/updates/2026-08-17-summary.md`](docs/updates/2026-08-17-summary.md), and [`docs/updates/2026-08-16-summary.md`](docs/updates/2026-08-16-summary.md).

#### Added
- **Studio Reflection-Driven Inspector (#37c, PR #56):**
  - Added `FieldVisitor` interface and template `reflect()` adapter in `engine/reflection/field_visitor.hpp`.
  - Added `ComponentReflection::components_on` and `ComponentReflection::inspect` in `engine/reflection/component_reflection.hpp` for in-place component traversal in `World`.
  - Implemented ImGui reflection inspector in `studio/inspector.hpp` and `studio/inspector.cpp` rendering foldable component property widgets (transform vectors, Euler rotations, handles, booleans).
  - Added test suites in `tests/unit/reflection/field_visitor_test.cpp` and `tests/unit/reflection/component_inspect_test.cpp`.
- **Studio Offscreen Viewport & Direct Two-Pass Submission (#37d, PR #58):**
  - Added neutral `RenderDevice` virtual viewport hooks in `engine/render/render_device.hpp` (`create_viewport_target`, `destroy_viewport_target`, `begin_viewport_pass`, `end_viewport_pass`, `get_viewport_texture_id`).
  - Added `ViewportPanel` sizing model and `render_scene_to_viewport` sequencing seam in `studio/viewport.hpp`.
  - Implemented Vulkan offscreen render target (color/depth textures, render pass, framebuffers, descriptor set binding) in `modules/render_vulkan/vulkan_render_device.cpp`.
  - Added lazy swapchain pass acquisition and dedicated offscreen graphics pipeline for render pass compatibility.
  - Extracted shared `build_demo_scene()` helper in `app/run/demo_scene.hpp`.
  - Added test suites in `tests/unit/render/render_device_hooks_test.cpp` and `tests/unit/studio/viewport_test.cpp`.
- **Studio Simulation Controls & Logging Console (#37e, PR #58, PR #59):**
  - Added `PlayState` model in `studio/play_state.hpp` supporting play, pause, single-tick stepping, and consumption semantics.
  - Added `ConsoleSink` and thread-safe `ConsoleBuffer` in `studio/console.hpp` capturing engine log records with capacity capping.
  - Added ImGui Toolbar (Play/Pause/Step buttons) and Console Panel (level filtering, auto-scroll, clear) in `studio/studio_app.cpp` and `studio/console.hpp`.
  - Installed stderr `ConsoleSink` in `app/main.cpp` for CLI/headless execution.
  - Added test suites in `tests/unit/studio/play_state_test.cpp` and `tests/unit/studio/console_test.cpp`.
- **Studio Asset Browser & Live Assignment (#37 Post-v0.1, PR #62):**
  - Added `AssetRegistry::for_each` and `AssetRegistry::size` in `engine/assets/asset_registry.hpp`.
  - Added mutable `FieldVisitor::field_asset` for `AssetID` fields in `engine/reflection/field_visitor.hpp`.
  - Added headless `AssetBrowserModel` and ImGui Assets panel in `studio/asset_browser.hpp` and `studio/asset_browser.cpp`.
  - Enabled live asset assignment to selected entities (e.g. swapping `Material.texture` procedural textures with live viewport updates).
  - Added test suites in `tests/unit/assets/asset_enumeration_test.cpp`, `tests/unit/reflection/field_asset_test.cpp`, and `tests/unit/studio/asset_browser_test.cpp`.

#### Changed & Fixed
- **Vulkan Synchronization Hardening (PR #60):** Replaced single `render_finished_` semaphore with per-swapchain-image semaphore array in `modules/render_vulkan/vulkan_render_device.cpp`, eliminating `VK_LAYER_KHRONOS_validation` semaphore reuse errors during in-flight presentation.
- **Vulkan Face Culling Correction (PR #61):** Configured `frontFace = VK_FRONT_FACE_CLOCKWISE` with back-face culling to account for Vulkan's negative-height viewport winding reversal.
- **Editor Frame Pacing & Paused Transforms (PR #61):** Ensured `TransformSystem::update` runs continuously every frame in `app/run/editor.cpp`, allowing inspector transform edits to update the viewport immediately while paused.

#### Housekeeping
- Updated `.gitignore` to ignore runtime ImGui docking configuration (`imgui.ini`).
- Expanded test suite to **254 unit and integration test cases** (15,797 assertions, 100% passing).

---

## [2026-08-21 / 2026-08-22]

#### Added
- **Camera Component & View/Projection Derivation (#44, PR #50):**
  - Added closed-form `Mat4::inverse_affine` transform inversion in `foundation/math/mat4.hpp` for rigid/TRS matrix inversion without general 4x4 matrix decomposition overhead.
  - Added reflected `Camera` component in `engine/render/camera.hpp` with properties `fov_y_radians`, `near_z`, `far_z`, and `is_active`.
  - Registered `Camera` in `engine/reflection/builtin_components.hpp` for scene serialization and type inspection.
  - Upgraded `RenderSystem` in `engine/render/render_system.hpp` to dynamically query the active `Camera` entity's `WorldTransform`, compute `view = inverse_affine(camera_wt.matrix)` and `projection = Mat4::perspective(...)`, with multi-camera selection and fallback view when no camera exists.
  - Updated windowed demo in `app/main.cpp` with dedicated Camera entity and scoped `SpinSystem` rotating only entities with `MeshRenderer`.
  - Added unit tests for inverse affine math (`tests/unit/math/inverse_affine_test.cpp`), camera reflection (`tests/unit/reflection/camera_reflection_test.cpp`), and camera-driven `RenderSystem` scene traversal (`tests/unit/render/render_system_test.cpp`). Total test suite expanded to **207 passing tests**.
- **Mesh Draw at WorldTransform & Graphics Pipeline (#43, PR #49):**
  - Added `Vec3` vector operations (`dot`, `cross`, `normalize`, `length`) in `foundation/math/vec3.hpp`.
  - Added `Mat4::perspective` (Vulkan clip space $Z \in [0, 1]$) and `Mat4::look_at` in `foundation/math/mat4.hpp`.
  - Added `MeshRenderer` component in `engine/render/mesh_renderer.hpp`.
  - Added procedural `make_cube_mesh()` with 24 colored vertices and 36 indices in `engine/render/cube.hpp`.
  - Added `RenderSystem` in `engine/render/render_system.hpp` traversing `WorldTransform` + `MeshRenderer` entities.
  - Added GLSL shaders `cube.vert` and `cube.frag` with SPIR-V build rules via `glslc`.
  - Added Vulkan graphics pipeline, vertex/index GPU staging and device-local buffers, and depth buffering (`VK_FORMAT_D32_SFLOAT`) in `modules/render_vulkan/vulkan_render_device.cpp`.
  - Added windowed spinning cube demo in `app/main.cpp`.
- **Vulkan RenderDevice Bootstrap & Windowed Clear Screen (#42, PR #48):**
  - Added backend-neutral `RenderDevice` interface in `engine/render/render_device.hpp` with factory `create_vulkan_render_device`.
  - Added `modules/render_vulkan` implementation: Vulkan 1.3 instance, physical device scoring, queue family indexing, swapchain management with dynamic resize recreation, and double-buffered frame synchronization.
  - Added swapchain selection helpers in `modules/render_vulkan/vulkan_swapchain_support.hpp/.cpp`.
  - Added `--window` flag to `mosaic run` CLI options parser in `app/cli/options.hpp/.cpp`.
  - Added `on_frame` hook in `app/run/frame_pump.hpp` for real-time window loop execution.
- **Foundation Job System & Concurrency (#34, PR #41):**
  - Added `ThreadPool` in `foundation/jobs/thread_pool.hpp` with worker thread management and `submit(task)` returning `std::future<void>`.
  - Added `parallel_for` with latch-synchronized chunking and exception propagation.
  - Added clean drain-and-join destruction guaranteeing zero worker thread leaks.
- **Application Tier & `mosaic run` CLI Shell (#35, PR #40):**
  - Added `mosaic_app` library and `mosaic` executable target linking CLI11 in `app/`.
  - Added `Options` parser in `app/cli/options.hpp` for `mosaic run` flags (`--scene`, `--ticks`, `--duration`, `--fps`, `--fixed-hz`, `--format`, `--log-level`).
  - Added `FramePump` in `app/run/frame_pump.hpp` for real-time wall clock simulation pacing, target FPS sleeping, and injected test clocks.
  - Added `StatusReporter`, `JsonStatusReporter`, and `TextStatusReporter` in `app/run/status_reporter.hpp`.
  - Added process `main.cpp` with `std::chrono` clock and OS signal handlers (`SIGINT`/`SIGTERM`).
  - Added `register_builtin_components()` in `engine/reflection/builtin_components.hpp` registering `Transform` and `WorldTransform`.
- **Foundation Structured Logging (#33, PR #39):**
  - Added leveled logging in `foundation/logging/log.hpp` with `LogLevel` (`trace`, `debug`, `info`, `warn`, `error`, `fatal`).
  - Added thread-safe `Logger` with atomic minimum level filtering and `ConsoleSink`.
  - Added global `log::info`, `log::warn`, `log::error` `std::format`-style functions.
- **Fixed-Timestep Runtime Loop (#22, PR #31):**
  - Added `RuntimeLoop` fixed-timestep accumulator in `engine/runtime/runtime_loop.hpp` supporting paused/active stepping, remainder banking, spiral-of-death guarding, and long-duration stability tests over 1,000 ticks.
- **Foundation Math & Transform System (#21, #26, PR #30):**
  - Added zero-dependency column-major math library (`Vec3`, `Quat`, `Mat4`) in `foundation/math/`.
  - Added reflected `Transform` component and derived `WorldTransform` component in `engine/transform/transform.hpp`.
  - Added `TransformSystem` in `engine/transform/transform_system.hpp` for hierarchical parent-child transform evaluation with depth limit protection.
- **Asset Registry & Metadata (#20, PR #29):**
  - Added `AssetMetadata` with TOML round-trip support in `engine/assets/asset_metadata.hpp`.
  - Added `AssetRegistry` with lazy cached loading (`load<T>`), eager resource registration, and `AssetReferenceResolver` in `engine/assets/asset_registry.hpp`.
  - Added `std::hash<AssetID>` in `foundation/serialization/reference.hpp`.
- **Scene Load & Save (#19, PR #32):**
  - Added `Scene::to_value`, `Scene::from_value`, `Scene::save_to_json`, `Scene::load_from_json` in `engine/scene/scene.hpp`.
  - Added two-pass scene deserialization with `EntityID` handle remapping via `SceneReferenceResolver`.
  - Added live entity traversal via `EntityRegistry::for_each` and `World::for_each_entity`.
  - Added symmetric save-direction reflection in `ComponentReflection::save` and `has`.
- **Reflection System & Dynamic Component Instantiation (#18, PR #27):**
  - Added unified `TypeID` and `type_id<T>()` in `foundation/reflection/type_id.hpp`.
  - Added `TypeRegistry` in `foundation/reflection/type_registry.hpp` with string name lookup and field reflection visitors.
  - Added `ComponentReflection` in `engine/reflection/component_reflection.hpp` for generic deserialization and dynamic attachment into `World`.
- **Engine Runtime World Container:**
  - Added `World` container in `engine/world/world.hpp` owning `EntityRegistry`, `ComponentRegistry`, and `EventBus`.
  - Added deterministic 3-stage `World::tick(dt)` execution: (1) deferred event queue drain, (2) system updates in registration order, (3) deferred entity destructions (`on_destroy` callbacks + entity deletion).
  - Added `SparseSet::each` dense traversal in `engine/ecs/sparse_set.hpp`.
  - Added `ComponentRegistry::for_each<T>` typed single-component iteration in `engine/ecs/component_registry.hpp`.
  - Added `System::update(World&, float dt)` virtual lifecycle in `engine/ecs/system.hpp`.
- **Command & Event System (#11):**
  - Added `Command` contract in `foundation/commands/command.hpp` with `id`, `parameters`, `required_permissions`, `execute()`, `undo()`, `serialize()`.
  - Added `CommandStack` undo/redo transaction log in `foundation/commands/command_stack.hpp`.
  - Added `EventBus` in `foundation/events/event_bus.hpp` supporting synchronous in-tick dispatch and double-buffered FIFO deferred event queue drained per tick.

#### Changed
- **Rendering Tracking Issue Decomposition (#36, PR #47):** Decomposed Vulkan rendering issue into five linear sub-issues (#42–#46): RenderDevice bootstrap & swapchain clear (#42), vertex/index buffers (#43), camera matrices (#44), materials/textures (#45), point lights & render graph (#46).

#### Security & Hardening
- **Serialization Recursion Depth Guard (#10 Follow-up):** Added configurable `max_depth` (default 128) in `from_json` and `from_toml` decoders (`foundation/serialization/json_backend.cpp`, `toml_backend.cpp`) to prevent stack overflow and DoS on deeply nested adversarial input.
- **Job Latch Thread Safety Fix (#34):** Fixed potential latch use-after-free by ensuring condition variable notification occurs under lock protection.

#### Documentation
- Added design specs and implementation plans for all foundation, engine, and app subsystems under `docs/superpowers/specs/` and `docs/superpowers/plans/`.
- Updated `docs/project/next-steps.md` to point active frontier at `#42` (Vulkan RenderDevice bootstrap & clear-screen).
- Updated `docs/project/advancement-plans-and-evaluation.md` with complete implementation evaluation and 186/186 test passing baseline.
