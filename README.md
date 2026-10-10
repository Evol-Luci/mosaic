# Mosaic

**A programmable 3D operating environment.** Model, program, simulate, animate,
render, play, film, and automate worlds — in one modular suite where **the human
and the agent are two clients of the same systems.**

Mosaic is an open, modular 3D development platform. It pairs Blender as a
source-authoring tool with a standalone C++ engine/runtime and Box3D physics.

> Mosaic is **not** "open-source Unreal." That phrase describes a destination,
> not a blueprint. See the [manifesto](docs/vision/manifesto.md).

## The three tiers

- **Studio** — the full authoring environment: Blender integration, scene
  editing, asset tools, scripting, debugging, sequencing, project management.
- **Engine** — the standalone runtime: ECS, assets, rendering, physics,
  animation, audio, input, networking, scripting, serialization. No editor, no
  Blender, no AI dependency.
- **Foundation** — the tiny, boring core: memory, jobs, math, reflection,
  events, commands, logging, IO, the plugin ABI.

External, replaceable implementations sit underneath: Blender, Box3D, Vulkan,
Python. Above everything sits a single **Automation API** with CLI, Python, and
agents as equal clients.

See [ARCHITECTURE.md](docs/architecture/ARCHITECTURE.md) for the full picture and
the enforced dependency rules.

## Status

**Active development** — the **Phase 0 Foundation**, **v0.1 Engine Core**, **Application Shell**, **Vulkan Rendering**, **Studio Workbench**, **Box3D Physics (P1–P5)**, **Asset Tools Workflow (T1–T5)**, **CPython Scripting (S1–S5)**, **Animation & Sequencer**, **Navigation (A\*)**, **Particles**, **Audio Subsystem (A1–A4)**, **VFX Subsystem (V1–V4)**, **UI Subsystem (U1–U7)**, and **Networking Subsystem (N1–N7 Complete)** are operational:
- Foundation: ECS core ([#9](https://github.com/Evol-Luci/mosaic/issues/9)), reflection ([#18](https://github.com/Evol-Luci/mosaic/issues/18)), serialization ([#10](https://github.com/Evol-Luci/mosaic/issues/10)), commands & events ([#11](https://github.com/Evol-Luci/mosaic/issues/11)), foundation math & projections ([#26](https://github.com/Evol-Luci/mosaic/issues/26), [#43](https://github.com/Evol-Luci/mosaic/issues/43), [#44](https://github.com/Evol-Luci/mosaic/issues/44)), structured logging ([#33](https://github.com/Evol-Luci/mosaic/issues/33)), job concurrency & thread pool ([#34](https://github.com/Evol-Luci/mosaic/issues/34)).
- Engine & Systems: Runtime `World`, Scene load/save ([#19](https://github.com/Evol-Luci/mosaic/issues/19)), Asset registry ([#20](https://github.com/Evol-Luci/mosaic/issues/20)), Transform system ([#21](https://github.com/Evol-Luci/mosaic/issues/21)), fixed-timestep `RuntimeLoop` ([#22](https://github.com/Evol-Luci/mosaic/issues/22)), Box3D physics integration (P1–P5), CPython scripting engine (S1–S5, hot reload, worker thread, debugger, profiler), property-path keyframe animation & sequencer, 2D grid pathfinding (A\*), and deterministic particle simulation (instanced billboards, over-life easing curves, triggered bursts).
- Rendering & VFX: Neutral `RenderDevice` interface and Vulkan backend (`modules/render_vulkan`) powering `mosaic run --window` with hardware clear, mesh draw, lighting, textures, camera view/projection, offscreen viewport targets, ribbon trails (V1–V2), world-space quad decals (V3), fullscreen overlay screen effects (V4), and world-space 3D text labels (U5b).
- Audio Subsystem: Neutral `AudioBackend` and miniaudio device backend (`modules/audio_miniaudio`), reflected `AudioSource`/`AudioListener`, standalone pure-C++ RIFF/WAV cooker (`MOSAAUD1`), and 3D positional audio.
- UI Subsystem: Screen-space anchored panels (`UiPanel`), font atlas cooker via `stb_truetype` (`MOSAFNT1`), aspect-correct text rendering (`UiText`), textured quad sprites (`UiImage`) with pixel sub-rects and Stretch/Fit/Fill aspect modes (U7), interactive buttons (`UiButton`, `PointerState`, `UiSystem`) with z-order hit resolution and hover enter/exit events (U6), hierarchical nesting (`UiParent`, `ui_hierarchy`), and floating 3D world-space text labels (`WorldLabel`).
- Networking Subsystem: Neutral `Transport` seam (`engine/net/transport.hpp`), deterministic in-memory `LoopbackTransport` FIFO queues, binary message framing (`MessageType::Hello`/`Welcome`), and `NetworkSystem` session lifecycle management.
- Studio Workbench: `mosaic editor` launches an interactive ImGui docking workbench with live Hierarchy, Reflection Inspector (with dynamic component add/remove), 3D Viewport with mouse picking, Controls panel, Script Console/Editor, and Simulation Toolbar.
- Test suite: **1239 unit and integration tests**, 100% passing.

The current frontier is the UI pillar's second half: U1–U5 shipped, then were decomposed into U6–U11 (`docs/superpowers/specs/2026-10-07-ui-pillar-decomposition.md`). U7 (image sub-rects and aspect modes) and U6 (z-order hit resolution and hover) landed on 2026-10-10, each deliberately shipping two of its three features; 9-slice, pointer capture/drag, and U8–U11 remain. See the live build handoff in [docs/project/next-steps.md](docs/project/next-steps.md) and [CHANGELOG.md](CHANGELOG.md).

## Licensing

Mosaic core is **dual-licensed**: **AGPLv3** for the free tier (copyleft, with a
"Made with Mosaic" branding requirement) and a **commercial license** for closed
products (removes copyleft and branding, may carry royalty terms). Mosaic Studio
is **GPLv3** due to its Blender integration. Contributions require a CLA. Full
rationale in [docs/project/licensing.md](docs/project/licensing.md).

## Documentation

Browse the docs hub locally:

```bash
./view_docs.sh   # serves docs/index.html at http://localhost:8000
```

Doc tiers: [vision](docs/vision/), [architecture](docs/architecture/),
[design](docs/design/), [project](docs/project/), and
[RFCs](docs/rfcs/).

## Contributing

Non-trivial changes start as an [RFC](docs/rfcs/). All contributions require
signing the CLA — see [CONTRIBUTING.md](CONTRIBUTING.md). Please also read the
[Code of Conduct](CODE_OF_CONDUCT.md).
