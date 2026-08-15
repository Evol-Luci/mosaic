# Repository Layout

Mosaic is a **monorepo**, at least initially. Early on, interfaces change
constantly, and splitting repositories too soon creates artificial friction.
Directory ownership communicates architecture: where a file lives answers what it
may depend on.

## The tree

```text
mosaic/
├── CMakeLists.txt
├── CMakePresets.json
├── LICENSE
├── README.md
├── CONTRIBUTING.md
├── CODE_OF_CONDUCT.md
│
├── docs/                 architecture, design, project, rfcs, tutorials
│
├── foundation/           tiny, boring core — knows nothing above it
│   ├── core/      memory/   math/     reflection/
│   ├── serialization/  events/  commands/  jobs/
│   ├── io/  logging/  platform/
│
├── engine/               standalone runtime — no editor, no Blender, no AI
│   ├── scene/  ecs/  assets/  rendering/  physics/
│   ├── animation/  audio/  input/  scripting/  networking/  runtime/
│
├── editor/               tools that manipulate engine state via commands
│   ├── shell/  viewport/  inspector/  outliner/  asset_browser/
│   ├── console/  debugger/  profiler/  tools/
│
├── studio/               full dev environment — knows Editor and Blender
│   ├── app/  workspace/  blender/
│
├── modules/              static/dynamic modules behind interfaces
│   ├── box3d/  render_vulkan/  render_d3d12/  render_metal/
│   ├── python/  ai/  ...
│
├── tools/                mosaic-cli/  assetc/  scenec/  shaderc/  packager/  projectgen/
│
├── templates/            empty/  game/  visualization/  cinematic/
│
└── tests/                unit/  integration/  rendering/  physics/  projects/
```

## Top-level responsibilities

| Directory | Responsibility |
|---|---|
| `foundation/` | The small core: memory, jobs, math, reflection, events, commands, IO, plugin ABI. |
| `engine/` | The standalone runtime. Depends only on foundation. |
| `editor/` | Editor tools. Depend on engine. |
| `studio/` | Full environment + Blender integration. Depends on editor. |
| `modules/` | Backends and optional capabilities behind interfaces (Box3D, renderers, Python, AI). |
| `tools/` | CLI and content compilers. |
| `templates/` | Project starters. |
| `tests/` | Unit, integration, and project-level tests. |

## Enforced dependency direction

```text
Foundation  →  Engine  →  Editor  →  Studio
```

Allowed: `Engine → Foundation`, `Editor → Engine`, `Studio → Editor`.
Forbidden (enforced in CI): `Foundation → Engine`, `Foundation → Editor`,
`Engine → Editor`, `Engine → Blender`, `Engine → AI`.

Modules may depend on the interfaces they implement, never on arbitrary editor
code. The full rationale and the "Things Mosaic Will Never Do" contract are in
[ARCHITECTURE.md](../architecture/ARCHITECTURE.md).

## On splitting later

The engine is designed to be radically smaller than the development environment.
Once interfaces stabilize, `engine`, `studio`, `SDK`, and `tools` could become
separate repositories — but only after the platform has proven what it needs. Not
on day one.
