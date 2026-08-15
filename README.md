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

Design / vision phase. This repository currently holds the specification, design
docs, and roadmap. The first buildable milestone is the narrow **v0.1 vertical
slice** defined in the [roadmap](docs/project/roadmap.md): create a project →
import a Blender model → place it in a world → attach physics → write a Python
script → press **Play** → **Build** a standalone.

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
