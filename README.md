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

**Active development** — the **Phase 0 Foundation**, **v0.1 Headless Engine Core**, **Application Shell**, **Vulkan Windowed Rendering**, and the **Mosaic Studio v0.1 Lean Core** are operational:
- Foundation: ECS core ([#9](https://github.com/Evol-Luci/mosaic/issues/9)), reflection ([#18](https://github.com/Evol-Luci/mosaic/issues/18)), serialization ([#10](https://github.com/Evol-Luci/mosaic/issues/10)), commands & events ([#11](https://github.com/Evol-Luci/mosaic/issues/11)), foundation math & projections ([#26](https://github.com/Evol-Luci/mosaic/issues/26), [#43](https://github.com/Evol-Luci/mosaic/issues/43), [#44](https://github.com/Evol-Luci/mosaic/issues/44)), structured logging ([#33](https://github.com/Evol-Luci/mosaic/issues/33)), job concurrency & thread pool ([#34](https://github.com/Evol-Luci/mosaic/issues/34)).
- Engine: Runtime `World`, Scene load/save ([#19](https://github.com/Evol-Luci/mosaic/issues/19)), Asset registry ([#20](https://github.com/Evol-Luci/mosaic/issues/20)), Transform system ([#21](https://github.com/Evol-Luci/mosaic/issues/21)), fixed-timestep `RuntimeLoop` ([#22](https://github.com/Evol-Luci/mosaic/issues/22)), built-in component registration, `MeshRenderer`, `Camera`, `Material`, `DirectionalLight`, and lit `RenderSystem` scene traversal ([#43](https://github.com/Evol-Luci/mosaic/issues/43), [#44](https://github.com/Evol-Luci/mosaic/issues/44), [#45](https://github.com/Evol-Luci/mosaic/issues/45), [#46](https://github.com/Evol-Luci/mosaic/issues/46)).
- App & Rendering: Standalone `mosaic` CLI executable, argument parsing, real-time `FramePump` with signal handling ([#35](https://github.com/Evol-Luci/mosaic/issues/35)), neutral `RenderDevice` interface and Vulkan backend (`modules/render_vulkan`) powering `mosaic run --window` with hardware clear, mesh draw, camera-driven view/projection, and offscreen rendering targets ([#42](https://github.com/Evol-Luci/mosaic/issues/42)–[#46](https://github.com/Evol-Luci/mosaic/issues/46)).
- Studio Workbench ([#37](https://github.com/Evol-Luci/mosaic/issues/37)): `mosaic editor` launches an interactive ImGui docking workbench with a live **Hierarchy** panel, a **Reflection Inspector** with in-place component editing, a dynamic offscreen 3D **Viewport** panel, a simulation **Toolbar & Console** (Play/Pause/Step + thread-safe log sink), and an **Asset Browser** with live texture property assignment.
- Test suite: **254 unit and integration tests**, 100% passing.

The current frontier targets the post-v0.1 Studio milestones (Blender live-link, asset thumbnail caching/import) and physics integration (Box3D adapter). See the live build handoff in [docs/project/next-steps.md](docs/project/next-steps.md) and [CHANGELOG.md](CHANGELOG.md).

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
