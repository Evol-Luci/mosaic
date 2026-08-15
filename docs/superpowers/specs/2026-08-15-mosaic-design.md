# Project Mosaic — Design Spec (v0.1)

**Date:** 2026-08-15
**Status:** Approved (design phase) — implementation = the documentation set below
**Source material:** Synthesis of two independent architecture proposals ([gemini_plans.md](../../../gemini_plans.md), [chatgpt_plans.md](../../../chatgpt_plans.md))

---

## 1. What Mosaic is

Mosaic is **a programmable 3D operating environment** — an open, modular development
suite where the editor, runtime, tools, scripting system, renderer, simulation, and
automation layer are all first-class and independently extensible.

It is **not** "open-source Unreal." That phrase describes a destination, not a blueprint.
The real thesis:

> Build worlds. Model them. Program them. Simulate them. Animate them. Render them.
> Play them. Film them. Automate them.
>
> The editor is a tool. The runtime is a tool. The language is a tool. AI is a tool.
> No single workflow is mandatory. Everything should be inspectable, scriptable,
> replaceable, and extensible.
>
> **The user and the agent are simply two different clients of the same underlying systems.**

Mosaic stands on two upstream projects:

- **Blender** (GPLv3) — a mature DCC covering modeling, sculpting, rigging, animation,
  geometry nodes, compositing, and countless file formats. Used as a **source-authoring
  tool**, never forked into the repo, never the runtime format.
- **Box3D** (MIT, Erin Catto) — a young C17 data-oriented physics engine with SIMD,
  multithreading, CCD, determinism, record/replay, joints, sensors, and character
  movement. Used as the **initial physics backend behind an abstraction**, never exposed
  directly. Box3D 0.1.0 is explicitly alpha, which is exactly why it sits behind a wall.

---

## 2. Decisions locked in this session

These four decisions gate everything downstream and are recorded here as the source of truth.

### 2.1 Architectural spine — **Separate engine** (not Blender-hosted)

The two source plans quietly described **two different products**:

- Gemini's plan runs physics *inside Blender* as an addon — Blender is the runtime host.
- ChatGPT's plan makes Blender a *source-authoring tool only*; the engine is a separate
  C++ application and `.blend` is never the runtime format.

**Decision:** Adopt the **separate-engine** spine. The engine/runtime is a standalone
C++ application. Blender communicates out-of-process (IPC / Python). Gemini's excellent
bridge code (zero-copy ctypes transform buffers, viewport modal sync loop, compound
colliders, joints, headless bake-to-F-curve) is **repurposed as Mosaic Studio's
"live-link preview" feature**, not the foundation of the engine.

Rationale: only the separate-engine spine can deliver the promised lean, shippable
standalone runtime and keep the door open for a permissive/commercial licensing story.

### 2.2 Resourcing & near-term goal — **Vision/docs first → small team forming**

The immediate deliverable is the **spec + plans documentation + a docs hub**, serving as
both a design artifact and a recruiting artifact. Immediately after, the project
transitions into **small-team-forming build**. Therefore the docs must simultaneously:

- **sell the vision** (manifesto, clear diagrams, a compelling thesis), and
- **be buildable-against** — "strong enough that somebody can start opening GitHub
  issues and writing code against them."

### 2.3 Licensing — **Dual-license (AGPLv3 + commercial), CLA-backed**

Goal expressed by the owner: free for almost everyone, name/branding attached to shipped
games, and a way to profit if a game built on Mosaic makes serious money — while staying
as "open" as possible.

**Reality:** OSI "open source" forbids royalty requirements and commercial-use
restrictions. You cannot have literal OSI-open-source *and* enforceable royalties. So:

**Decision — dual-licensing:**

- **Free tier — AGPLv3.** Copyleft: anyone may use Mosaic, but derivative works
  (including linked games) must also be released as open source under AGPLv3, and must
  retain the "Made with Mosaic" branding. AGPL (vs GPL) additionally covers
  network/SaaS use — relevant for multiplayer servers and cloud tooling.
- **Commercial tier — paid private license.** Waives copyleft (studios keep source
  closed) and removes branding. Because it is a private contract, it **may carry royalty
  terms** (e.g. free under a revenue threshold, a percentage above it). This captures the
  "profit on a hit" goal without breaking the open free tier.

**Requirements this imposes:**

- The engine must be **full GPL/AGPL, not LGPL** — LGPL permits linking without copyleft
  spreading, which would gut the commercial-license leverage.
- **Copyright must be consolidated.** All contributors sign a **Contributor License
  Agreement (CLA)** granting relicensing rights, or the owner cannot legally include
  their code in paid commercial builds. The `CONTRIBUTING` docs must include the CLA from
  day one.
- **Mosaic Studio** remains **GPLv3** because it integrates Blender (even out-of-process
  Python/IPC integration is safest treated as GPL for the Studio application).

Tracking is honor-system + legal obligation; enforcement targets the big, visible
successes — which are exactly the commercial users who owe a license anyway.

This decision changes only a `LICENSE` file, a splash-branding requirement, and the
`CONTRIBUTING`/CLA doc. **Zero architectural impact.**

### 2.4 Documentation set — **Full set (17 documents) + docs hub**

Approved in full (see §6).

---

## 3. What we took from each plan

| Concern | Source | What we adopt |
|---|---|---|
| Three-tier separation (Studio/Editor → Engine → Foundation) | Both | The tiering, with ChatGPT's stricter dependency rules |
| Enforced dependency rules in CI | ChatGPT | `Foundation → Engine → Editor → Studio`; forbidden edges enforced |
| Reflection as central primitive | ChatGPT | One metadata system → inspector, serialization, bindings, undo, agent tools, docs |
| Command + Event architecture | ChatGPT | Commands mutate (undoable), events announce; humans and agents share them |
| Asset pipeline (source → IR → cooked) | ChatGPT | Blender is a source tool; runtime asset is a distinct cooked format |
| ECS / World-vs-Scene model | ChatGPT | Entities are identities; components own data; systems process |
| Automation API (agent = one client) | Both | Permissioned automation API; AI is an optional consumer module |
| AI strictly optional (`--without-ai`) | Both | Engine never depends on AI; no hidden inference when disabled |
| "Things Mosaic Will Never Do" guardrail | ChatGPT | Ships in `ARCHITECTURE.md` as an anti-rot contract |
| Monorepo + repo layout | ChatGPT | Adopted as the documented structure |
| Box3D C ABI bridge, zero-copy transforms | Gemini | Repurposed as Studio **live-link preview** implementation |
| Compound colliders, joints, local-anchor math | Gemini | The physics-authoring feature set surfaced in Studio |
| Headless bake-to-F-curve + decimation | Gemini | The "bake simulation to animation" Studio feature |
| Determinism via record/replay | Both | Elevated to an explicit cross-boundary guarantee (see §4) |
| Codename **Mosaic** | ChatGPT | Adopted (repo already named `mosaic`) |

---

## 4. Gaps we are closing (not covered by either plan)

1. **Licensing resolution** — decided (§2.3); neither plan committed.
2. **Determinism across the abstraction boundary** — the *engine's public physics API*
   must preserve Box3D's determinism guarantee (fixed timestep, stable body ordering,
   deterministic query results, record/replay exposed at the abstraction layer, not just
   the backend). Documented in `docs/design/physics.md`.
3. **CLA / copyright consolidation** — required by dual-licensing; folded into
   `CONTRIBUTING.md`.
4. **Automation-API security model** — permissioned scopes
   (`READ_PROJECT`, `MODIFY_PROJECT`, `EXECUTE_CODE`, `EXECUTE_PROCESS`,
   `NETWORK_ACCESS`, `FILE_ACCESS`, `BUILD_PROJECT`, `DEPLOY_PROJECT`); agents never get
   god mode by default. Documented in `docs/design/automation-and-agents.md`.
5. **The narrow v0.1 vertical slice** — an explicit, minimal milestone (§5) so the
   small team builds one honest slice, not breadth.
6. **RFC workflow** — an RFC template + one seed RFC so "open issues and build against
   the docs" is real from day one.

---

## 5. The v0.1 vertical slice (the first mountain)

Deliberately narrow. Success = a developer can:

> create a project → bring in a Blender model → put it in a world → move it around →
> attach physics → write a Python script → press **Play** → **Build** a standalone.

Scope:

- **Foundation:** C++20 core, math (right-handed, Y-up, meters, radians), UUIDs,
  reflection, serialization, logging, jobs, commands, events.
- **Engine:** ECS, World, Scene, asset registry, runtime loop, transform system.
- **Rendering:** Vulkan backend, mesh/material/texture, camera, directional+point light,
  a basic render graph.
- **Physics:** Box3D adapter — rigid bodies, primitive colliders, raycasts, contacts,
  deterministic step.
- **Scripting:** Python embedding, reflection bindings, command access.
- **Tools:** CLI (`mosaic create/editor/run/build/…`, all with `--json`), project
  generator, asset compiler.
- **Studio:** viewport, scene hierarchy, inspector, asset browser, console, play/stop,
  basic Blender bridge (live-link preview).

Everything else (animation, audio, particles, VFX, UI, navigation, networking,
sequencer, marketplace, full agent ecosystem) is explicitly **out of v0.1**.

---

## 6. The documentation set to produce

**Tier 1 — Vision & spec**
1. `README.md` — pitch, manifesto, hub links.
2. `docs/vision/manifesto.md` — the thesis; human and agent as two clients.
3. `docs/architecture/ARCHITECTURE.md` — three tiers, dependency rules,
   **"Things Mosaic Will Never Do"**, mermaid diagrams.
4. `docs/architecture/data-model.md` — Entity/Component/World/Scene/Asset/Resource/
   System/Command/Event/Module/Plugin; serialization; Python view.
5. `docs/architecture/boundaries.md` — Blender (`IAssetAuthoringBackend`), Box3D
   (`PhysicsBackend`), render-backend boundaries; where Gemini's bridge lives.

**Tier 2 — Subsystem design**
6. `docs/design/reflection.md`
7. `docs/design/command-event-system.md`
8. `docs/design/asset-pipeline.md`
9. `docs/design/physics.md` — Box3D adapter + determinism guarantee + live-link/bake.
10. `docs/design/automation-and-agents.md` — permissioned API; AI as optional module.
11. `docs/design/rendering.md` — RenderDevice + RenderGraph, Vulkan first.
12. `docs/design/scripting.md` — Python-first, one reflected API.

**Tier 3 — Governance & roadmap**
13. `docs/project/licensing.md` — dual-license decision in full + CLA.
14. `docs/project/roadmap.md` — Phase 0→6 + the v0.1 slice.
15. `docs/project/repo-layout.md` — monorepo + CI-enforced dependency rules.
16. `CONTRIBUTING.md` (+ CLA note), `CODE_OF_CONDUCT.md`.
17. `docs/rfcs/` — RFC template + seed RFC (the data model).

**Then:** build the **docs hub** (`docs/index.html`) via the `establish-doc-hub`
registry skill — auto-fetches and renders these markdown files + mermaid diagrams.

---

## 7. Success criteria

- A newcomer reading the hub understands *what Mosaic is* and *why* within 5 minutes.
- A contributor can pick the data-model RFC and start implementing against unambiguous
  definitions.
- The four locked decisions (spine, resourcing, licensing, doc set) are discoverable and
  justified.
- No document contradicts another; the dependency rules and the "Never Do" list agree
  with every subsystem doc.
- The hub renders all docs + mermaid diagrams and is browsable locally with no server.

---

## 8. Non-goals for this documentation pass

- No engine/runtime code is written in this pass.
- No decision on renderer beyond "Vulkan first, backend-neutral interface."
- No final serialization *syntax* (schema matters more than JSON/TOML/YAML choice).
- No marketplace, netcode, or agent-ecosystem detail beyond naming and boundaries.
