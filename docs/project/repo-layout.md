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
├── docs/                 architecture, design, project, rfcs, updates
│
├── foundation/           tiny, boring core — knows nothing above it
│   ├── core/  math/  reflection/  serialization/
│   ├── events/  commands/  jobs/  logging/
│
├── engine/               standalone runtime — no Studio, no App, no Blender, no AI
│   ├── ecs/  world/  scene/  assets/  transform/  render/
│   ├── physics/  animation/  audio/  particles/  vfx/  ui/
│   ├── navigation/  net/  sequencer/  script/  runtime/  commands/  reflection/
│
├── studio/               the full editor UI + Blender live-link — depends on engine only
│   (viewport, hierarchy, inspector, console, play/stop, plugin host, script editor)
│
├── app/                  composition root: main() for every binary, CLI + MCP,
│   ├── cli/  run/  project/  plugin/  asset/  mcp/  docs/     project/plugin/asset management
│   (depends on engine AND studio; studio never depends back on app)
│
├── modules/              backends behind engine-level interfaces
│   ├── render_vulkan/  physics_box3d/  audio_miniaudio/  net_enet/
│   ├── script_python/  script_python_studio/
│
├── tools/                ci/  docs/  blender_live_link/  example_plugins/
│
├── packages/             bundled example plugins (e.g. hello-plugin/)
│
├── templates/            project scaffolds (default/, ...)
│
└── tests/                unit/  fixtures/
```

## Top-level responsibilities

| Directory | Responsibility |
|---|---|
| `foundation/` | The small core: memory, jobs, math, reflection, events, commands, IO, plugin ABI. |
| `engine/` | The standalone runtime. Depends only on foundation. |
| `studio/` | The editor UI (viewport, hierarchy, inspector, console, plugin host) + Blender live-link (out-of-process). Depends only on engine. |
| `app/` | The composition root — `main()`, CLI subcommands, the MCP automation API, project/plugin/asset management. Depends on engine and studio (studio never depends back on app). |
| `modules/` | Backends behind engine-level interfaces (Vulkan rendering, Box3D physics, miniaudio, ENet networking, embedded Python). |
| `tools/` | The dependency-rule lint, docs generation, the Blender live-link addon, example plugins. |
| `packages/` | Bundled example plugins installable via `mosaic plugin install`. |
| `templates/` | Project scaffolds `mosaic create` copies from. |
| `tests/` | Unit tests + fixtures. |

## Enforced dependency direction

```text
Foundation  →  Engine  →  Studio  →  App
```

Allowed: `Engine → Foundation`, `Studio → Engine`, `App → Engine`, `App → Studio`,
`Modules → Engine`.
Forbidden (enforced in CI via `tools/ci/check_dependency_rules.py`):
`Foundation → {Engine, Studio, App, Modules}`, `Engine → {Studio, App, Modules}`,
`Studio → App`, `Modules → {Studio, App}`, `Engine → Blender`, `Engine → AI`
(the last two have no dedicated directory to check yet).

Modules may depend on the interfaces they implement, never on arbitrary Studio or
App code. The full rationale, the real `#include`-direction basis for this
mapping, and the "Things Mosaic Will Never Do" contract are in
[ARCHITECTURE.md](../architecture/ARCHITECTURE.md).

## On splitting later

The engine is designed to be radically smaller than the development environment.
Once interfaces stabilize, `engine`, `studio`, `SDK`, and `tools` could become
separate repositories — but only after the platform has proven what it needs. Not
on day one.
