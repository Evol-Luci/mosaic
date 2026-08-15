# Roadmap

Development is organized around **capabilities, not features**. The ordering is
deliberate: the boring foundational work determines whether the project survives
five years, and AI comes last on purpose.

> AI should make a great engine better — not make a mediocre engine appear easier
> to use.

## Phases

### Phase 0 — Foundation

Project format, reflection, serialization, entity/component model, asset IDs,
command system, event system, plugin system, build system, logging, testing, CLI.
The unglamorous core that everything else is built on.

### Phase 1 — First World

Open a project, create a scene, create and transform entities, camera, lights,
meshes, materials, save, load, play. The first milestone: *"I can build a little
3D scene."*

### Phase 2 — Simulation

Integrate Box3D behind the physics boundary: rigid bodies, collision, joints,
character controller, queries, physics debugging, replay. Now it is an actual
interactive engine.

### Phase 3 — Programming

Runtime scripting, C++ API, Python, hot reload, debugger, profiler, CLI. Now
developers can build things.

### Phase 4 — Production

Animation, audio, particles, VFX, UI, navigation, networking, sequencer.

### Phase 5 — Tool ecosystem

Plugin SDK, package manager, marketplace/repository, templates, project
generators, documentation tooling. The community grows the platform.

### Phase 6 — Agent ecosystem

Only after the underlying systems are good: agent API, scene reasoning, code
generation, asset generation, automated testing, project maintenance,
optimization.

## The v0.1 vertical slice — the first mountain

v0.1 is deliberately narrow. Success is a single honest vertical slice:

> A developer can create a project, bring in a Blender model, put it in a world,
> move it around, attach physics, write a Python script, press **Play**, and
> **Build** a standalone.

### Scope

- **Foundation:** C++20 core, math (right-handed, Y-up, meters, radians), UUIDs,
  reflection, serialization, logging, jobs, commands, events.
- **Engine:** ECS, World, Scene, asset registry, runtime loop, transform system.
- **Rendering:** Vulkan backend, mesh/material/texture, camera,
  directional + point light, a basic render graph.
- **Physics:** Box3D adapter — rigid bodies, primitive colliders, raycasts,
  contacts, deterministic step.
- **Scripting:** Python embedding, reflection bindings, command access.
- **Tools:** CLI (`mosaic create/editor/run/build/…`, all with `--json`), project
  generator, asset compiler.
- **Studio:** viewport, scene hierarchy, inspector, asset browser, console,
  play/stop, basic Blender bridge (live-link preview).

### Explicitly out of v0.1

Animation, audio, particles, VFX, UI, navigation, networking, the sequencer, the
marketplace, and the full agent ecosystem. These are named so they are *not* built
early.

## The first demo

Not a game — something embarrassingly simple that proves the workflow: terrain, an
environment, a few physics objects, a controllable character, a camera, and one
scripted interaction. The workflow itself is the demo:

```text
mosaic create game Demo → open Studio → import Blender asset →
drag into scene → add physics → attach Python script → Play → Build
```

That is the first "this actually works" moment.
