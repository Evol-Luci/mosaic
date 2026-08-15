# Project Mosaic — Documentation Set & Docs Hub Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce the 17-document Project Mosaic documentation set and a browsable docs hub (`docs/index.html`) that renders all markdown + mermaid diagrams.

**Architecture:** Plain markdown files organized under `docs/` by tier (vision → architecture → design → project → rfcs), plus root `README.md`/`CONTRIBUTING.md`/`CODE_OF_CONDUCT.md`/`LICENSE`. The hub is a single static `docs/index.html` (from the `establish-doc-hub` registry skill) that fetches and renders those files client-side, enhanced to render inline mermaid fences.

**Tech Stack:** Markdown, Mermaid.js + Marked.js (via the hub template), a Python `http.server` helper for local viewing. No build system, no runtime code — this is a documentation pass.

**Source of truth:** [docs/superpowers/specs/2026-08-15-mosaic-design.md](../specs/2026-08-15-mosaic-design.md). Content is derived from that spec plus [gemini_plans.md](../../../gemini_plans.md) and [chatgpt_plans.md](../../../chatgpt_plans.md).

## Global Constraints

Every document must be consistent with these — copied verbatim from the spec:

- **Spine:** Separate engine. Blender is a source-authoring tool, out-of-process (IPC/Python), never forked, `.blend` is never the runtime format. Gemini's bridge code = Studio "live-link preview" feature only.
- **Dependency rule (enforced in CI later):** `Foundation → Engine → Editor → Studio`. Forbidden: `Foundation→Engine`, `Foundation→Editor`, `Engine→Editor`, `Engine→Blender`, `Engine→AI`.
- **Licensing:** Core dual-licensed = **AGPLv3** (free, copyleft, "Made with Mosaic" branding required) **+ commercial** (paid, closes source, removes branding, may carry royalty terms). Engine must be **full GPL/AGPL, not LGPL**. Copyright consolidated via **CLA**. **Mosaic Studio = GPLv3**.
- **Core language:** C++20. **Math convention:** right-handed, Y-up, meters, radians.
- **Physics:** Box3D behind a `PhysicsBackend` abstraction; public API never exposes `b3*` types; determinism preserved across the boundary.
- **Rendering:** backend-neutral `RenderDevice` + `RenderGraph`; Vulkan first.
- **Scripting:** Python-first, one reflected API.
- **AI:** strictly optional module; engine never depends on it; `--without-ai` fully excludes it.
- **Repo:** monorepo.
- **No placeholders in shipped docs:** no "TBD"/"TODO"/"FIXME" in any doc under `docs/` or repo root (the RFC template's intentional fill-in fields are the only exception, and use `<...>` angle-bracket placeholders).

---

### Task 1: Repo scaffolding, licenses, and directory tree

**Files:**
- Create: `.gitignore`, `LICENSE`, `LICENSE.commercial.md`, and empty tree dirs under `docs/`.

**Interfaces:**
- Produces: the `docs/vision/`, `docs/architecture/`, `docs/design/`, `docs/project/`, `docs/rfcs/` directories that every later task writes into; the `LICENSE` files referenced by `docs/project/licensing.md`.

- [ ] **Step 1: Create the directory tree and a .gitignore**

```bash
cd /home/lucievol/Projects/mosaic
mkdir -p docs/vision docs/architecture docs/design docs/project docs/rfcs
printf '%s\n' \
  '# OS / editor cruft' '.DS_Store' '*.swp' \
  '' '# local docs server pid, if any' '' \
  '# python' '__pycache__/' '*.pyc' > .gitignore
```

- [ ] **Step 2: Add the AGPLv3 free-tier LICENSE**

Fetch the canonical AGPL-3.0 text and save it, then add the Mosaic notice header.

```bash
cd /home/lucievol/Projects/mosaic
curl -fsSL https://www.gnu.org/licenses/agpl-3.0.txt -o LICENSE
```

If `curl` is unavailable/offline, write a placeholder pointer file `LICENSE` containing exactly:
`GNU AFFERO GENERAL PUBLIC LICENSE v3.0 — full text to be vendored from https://www.gnu.org/licenses/agpl-3.0.txt` and note it in the commit message. (This is the one permitted external fetch; the text is authoritative and must not be paraphrased.)

- [ ] **Step 3: Add the commercial-license summary stub**

Write `LICENSE.commercial.md`:

```markdown
# Mosaic Commercial License (summary)

The Mosaic core is dual-licensed. This file summarizes the **commercial**
alternative to the AGPLv3 free tier (see `LICENSE`).

A commercial license is a private agreement that:

- **Waives copyleft** — your product's source may remain closed.
- **Removes the "Made with Mosaic" branding** requirement.
- **May include royalty terms** (e.g. free below a revenue threshold, a
  percentage above it), negotiated per contract.

The commercial license is only required if you do not wish to comply with the
AGPLv3 (i.e. you do not want to open-source your derivative work). Hobbyists,
students, and open-source projects never need it.

To obtain one, contact the copyright holder. See
[docs/project/licensing.md](docs/project/licensing.md) for the full rationale.

> This is a summary, not the contract. Final commercial terms are provided in
> the executed agreement.
```

- [ ] **Step 4: Verify tree and files exist**

```bash
cd /home/lucievol/Projects/mosaic
test -d docs/vision && test -d docs/architecture && test -d docs/design && test -d docs/project && test -d docs/rfcs && echo "TREE OK"
test -s LICENSE && test -s LICENSE.commercial.md && echo "LICENSES OK"
```
Expected: `TREE OK` and `LICENSES OK`.

- [ ] **Step 5: Commit**

```bash
cd /home/lucievol/Projects/mosaic
git add .gitignore LICENSE LICENSE.commercial.md
git commit -m "Add repo scaffolding, AGPLv3 license, and commercial license summary"
```

---

### Task 2: README + manifesto (Tier 1 entry point)

**Files:**
- Create: `README.md`, `docs/vision/manifesto.md`

**Interfaces:**
- Consumes: directory tree (Task 1).
- Produces: the entry-point doc the hub's homepage and sidebar link to first.

- [ ] **Step 1: Write `README.md`**

Required sections:
1. **Title + one-line pitch:** "Mosaic — a programmable 3D operating environment. Model, program, simulate, animate, render, play, film, and automate worlds — where the human and the agent are two clients of the same systems."
2. **What it is / isn't:** it is a modular suite (Blender-authored, standalone C++ engine/runtime, Box3D physics); it is *not* "open-source Unreal."
3. **The three tiers** (one sentence each: Studio, Engine, Foundation).
4. **Status:** design/vision phase; v0.1 slice defined in the roadmap.
5. **Licensing (one paragraph):** dual-licensed AGPLv3 + commercial; Studio GPLv3; see `docs/project/licensing.md`.
6. **Docs:** point to `docs/index.html` (run `./view_docs.sh`) and list the doc tiers.
7. **Contributing:** link `CONTRIBUTING.md` and note the CLA.

Keep it under ~150 lines. Cross-links use repo-relative paths (e.g. `docs/project/roadmap.md`).

- [ ] **Step 2: Write `docs/vision/manifesto.md`**

Content:
- The full manifesto block from the spec (`Build worlds. Model them. Program them…`), verbatim as a blockquote.
- The thesis paragraph: user and agent are two clients of one system; everything inspectable, scriptable, replaceable, extensible.
- A short "What this implies" list: no mandatory workflow; editor/runtime/language/AI are each just tools; the automation API is the shared substrate.
- Close with the comparison framing: "closer to Blender + Unreal + Godot + VS Code + Houdini + Git + an automation API than to any one of them."

- [ ] **Step 3: Verify no placeholders**

```bash
cd /home/lucievol/Projects/mosaic
! grep -rnE 'TBD|TODO|FIXME' README.md docs/vision/manifesto.md && echo "CLEAN"
```
Expected: `CLEAN`.

- [ ] **Step 4: Commit**

```bash
cd /home/lucievol/Projects/mosaic
git add README.md docs/vision/manifesto.md
git commit -m "Add README and vision manifesto"
```

---

### Task 3: ARCHITECTURE.md — the keystone (with mermaid)

**Files:**
- Create: `docs/architecture/ARCHITECTURE.md`

**Interfaces:**
- Consumes: manifesto (Task 2) for tone.
- Produces: the canonical tier/dependency reference that `data-model.md`, `boundaries.md`, and every design doc point back to; the "Things Mosaic Will Never Do" contract.

- [ ] **Step 1: Write the tier overview with a mermaid diagram**

Sections and content:
1. **The three tiers** — Studio → Engine → Foundation, with the external/replaceable implementations below (Blender, Box3D, Vulkan/D3D12/Metal, Python). State the sacred rule: "Foundation does not know about Blender. Engine does not know about Blender. Studio knows about both."
2. Include a mermaid `graph TD` of the tiers + the Automation API sitting above (CLI / Python / Agents as clients). Use a fenced ```mermaid block.

- [ ] **Step 2: Write the dependency-rules section**

- Allowed edges: `Engine → Foundation`, `Editor → Engine`, `Studio → Editor`.
- Forbidden edges: `Foundation → Engine`, `Foundation → Editor`, `Engine → Editor`, `Engine → Blender`, `Engine → AI`.
- Note these will be **enforced in CI**. Include a second mermaid diagram showing allowed (solid) vs forbidden (the forbidden ones described in prose, not drawn, to avoid ambiguity).

- [ ] **Step 3: Write the "Things Mosaic Will Never Do" section**

Use the spec/source list verbatim as a bulleted contract:
- The runtime will never require the editor.
- The engine will never require AI.
- The core will never depend on Blender.
- The public engine API will never expose Box3D types.
- Plugins will not require modifying core source.
- Automation will not require simulating mouse/keyboard input.
- AI features will never be mandatory.
- The project format will not depend on a proprietary service.
- A rendering backend will not define the engine's rendering API.

Add a one-line rationale: this document is the guardrail against architectural rot.

- [ ] **Step 4: Verify mermaid fences balance and no placeholders**

```bash
cd /home/lucievol/Projects/mosaic
f=docs/architecture/ARCHITECTURE.md
[ $(grep -c '```mermaid' $f) -ge 1 ] && [ $(grep -c '```' $f) -eq $(( $(grep -c '^```' $f) )) ] && echo "FENCES OK"
awk '/```/{c++} END{ if (c%2==0) print "BALANCED"; else print "UNBALANCED" }' $f
! grep -nE 'TBD|TODO|FIXME' $f && echo "CLEAN"
```
Expected: `FENCES OK`, `BALANCED`, `CLEAN`.

- [ ] **Step 5: Commit**

```bash
cd /home/lucievol/Projects/mosaic
git add docs/architecture/ARCHITECTURE.md
git commit -m "Add ARCHITECTURE.md with tiers, dependency rules, and Never-Do contract"
```

---

### Task 4: data-model.md

**Files:**
- Create: `docs/architecture/data-model.md`

**Interfaces:**
- Consumes: ARCHITECTURE.md tiers.
- Produces: the definitions the data-model seed RFC (Task 11) formalizes and every subsystem doc references.

- [ ] **Step 1: Define the core primitives**

One subsection each, with: definition, what owns it, how it serializes, and how Python sees it:
`Entity`, `Component`, `System`, `World`, `Scene`, `Asset`, `Resource`, `Command`, `Event`, `Module`, `Plugin`, `Property`, `Reference`.
Key stances (from spec): Entity is an *identity*, not the object; Components own data; Systems process; a Scene is an *asset*, a World is a *runtime container*.

- [ ] **Step 2: Add the ECS example + serialization note**

Include the `World → Entity → {Transform, MeshRenderer, PhysicsBody, AudioSource, Script}` example (mermaid `graph TD`). Note: human-readable project metadata (JSON/TOML-like) vs binary runtime resource; every asset carries `UUID, type, version, source, dependencies, metadata`. State that final *syntax* is deferred; the *schema* is what matters.

- [ ] **Step 3: Verify**

```bash
cd /home/lucievol/Projects/mosaic
f=docs/architecture/data-model.md
awk '/```/{c++} END{print (c%2==0)?"BALANCED":"UNBALANCED"}' $f
! grep -nE 'TBD|TODO|FIXME' $f && echo "CLEAN"
```
Expected: `BALANCED`, `CLEAN`.

- [ ] **Step 4: Commit**

```bash
cd /home/lucievol/Projects/mosaic
git add docs/architecture/data-model.md
git commit -m "Add data-model architecture doc"
```

---

### Task 5: boundaries.md

**Files:**
- Create: `docs/architecture/boundaries.md`

**Interfaces:**
- Consumes: ARCHITECTURE.md, data-model.md.
- Produces: the boundary interfaces referenced by `physics.md`, `rendering.md`, and the roadmap.

- [ ] **Step 1: Document the three boundaries**

1. **Blender boundary** — `IAssetAuthoringBackend` (import/export/inspect); Blender is one implementation; MayaBridge/HoudiniBridge possible later; out-of-process rationale (crashing Blender must not crash the engine). Include the C++ interface sketch from the ChatGPT plan.
2. **Physics boundary** — `PhysicsBackend`; public API surface (`PhysicsWorld, RigidBody, Collider, Shape, Joint, RaycastResult, ContactEvent`); Box3D is the initial backend, never exposed. Determinism is a boundary guarantee (forward-ref `design/physics.md`).
3. **Render boundary** — `RenderDevice`/`RenderGraph`; Vulkan/D3D12/Metal/WebGPU as backends; higher-level code deals in `Mesh/Material/Texture/Shader/Camera/Light/Pass/RenderGraph`.

- [ ] **Step 2: State where Gemini's bridge code lives**

A short subsection: the zero-copy transform buffers, ctypes viewport modal sync, compound colliders, joints, and bake-to-F-curve from `gemini_plans.md` are the **implementation of Studio's live-link preview** — they sit above the engine boundary in Studio, not in the engine. Link back to the spec decision §2.1.

- [ ] **Step 3: Verify**

```bash
cd /home/lucievol/Projects/mosaic
f=docs/architecture/boundaries.md
awk '/```/{c++} END{print (c%2==0)?"BALANCED":"UNBALANCED"}' $f
! grep -nE 'TBD|TODO|FIXME' $f && echo "CLEAN"
```
Expected: `BALANCED`, `CLEAN`.

- [ ] **Step 4: Commit**

```bash
cd /home/lucievol/Projects/mosaic
git add docs/architecture/boundaries.md
git commit -m "Add boundaries doc (Blender, physics, render abstractions)"
```

---

### Task 6: reflection.md + command-event-system.md

**Files:**
- Create: `docs/design/reflection.md`, `docs/design/command-event-system.md`

**Interfaces:**
- Consumes: data-model.md.
- Produces: the reflection + command/event concepts that `automation-and-agents.md` and `scripting.md` build on.

- [ ] **Step 1: Write `docs/design/reflection.md`**

- Thesis: reflection is the most strategically important subsystem. One metadata source → inspector, serialization, Python bindings, undo/redo, documentation, automation, agent tools, network replication, animation tracks.
- Example: `struct Transform { Vec3 position; Quat rotation; Vec3 scale; }` and the metadata view.
- Explicit payoff line: "human-friendly and agent-friendly without building two systems."

- [ ] **Step 2: Write `docs/design/command-event-system.md`**

- Commands **mutate** (have ID, params, permissions, execute, undo, serialization); Events **announce** (EntityCreated, ComponentChanged, PhysicsContact, BuildCompleted…).
- The same command backs a UI button, a Python call, a CLI invocation, and an agent JSON request — show the `{"command": "scene.create_entity", "name": "Player"}` example.
- Undo/redo is command-based (CreateEntityCommand, SetPropertyCommand…), enabling macros, automation, transaction logs.
- Events keep modules decoupled (physics contact → audio/gameplay/telemetry all react; physics knows none of them).

- [ ] **Step 3: Verify**

```bash
cd /home/lucievol/Projects/mosaic
for f in docs/design/reflection.md docs/design/command-event-system.md; do
  awk '/```/{c++} END{print FILENAME, (c%2==0)?"BALANCED":"UNBALANCED"}' $f
done
! grep -rnE 'TBD|TODO|FIXME' docs/design/reflection.md docs/design/command-event-system.md && echo "CLEAN"
```
Expected: both `BALANCED`, `CLEAN`.

- [ ] **Step 4: Commit**

```bash
cd /home/lucievol/Projects/mosaic
git add docs/design/reflection.md docs/design/command-event-system.md
git commit -m "Add reflection and command/event system design docs"
```

---

### Task 7: asset-pipeline.md + physics.md

**Files:**
- Create: `docs/design/asset-pipeline.md`, `docs/design/physics.md`

**Interfaces:**
- Consumes: boundaries.md (PhysicsBackend), data-model.md (Asset/Resource).
- Produces: the physics determinism guarantee and asset flow the roadmap's v0.1 slice depends on.

- [ ] **Step 1: Write `docs/design/asset-pipeline.md`**

- Flow: `Source Asset → Importer → Intermediate Representation → Cooker → Runtime Asset` (mermaid). Concrete example: `robot.blend → Blender Importer → Mosaic Mesh IR → Mesh Cooker → robot.mesh`.
- Key stance: Blender is a **source authoring tool**, not the runtime format; the cooked runtime artifact is distinct. Assets are content-addressed / dependency-aware so edits know exactly what to rebuild (dragon.fbx → mesh/material/skeleton/anim/collision/LOD/textures).

- [ ] **Step 2: Write `docs/design/physics.md`**

- Box3D behind `PhysicsBackend`; public types only (`PhysicsWorld/RigidBody/Collider/Shape/Joint/RaycastResult/ContactEvent`).
- **Determinism guarantee across the boundary** (the gap we close): fixed timestep + substeps, stable body/contact ordering, deterministic query results, and record/replay exposed at the *abstraction* layer (not just the backend). Note Box3D 0.1.0 is alpha → the wall is deliberate.
- **Live-link + bake (Studio feature):** summarize Gemini's compound-collider pipeline, joint types (fixed/revolute/prismatic/distance-spring/6dof), local anchor/axis computation, and headless bake-to-F-curve with decimation — explicitly labeled as Studio live-link preview built on the boundary, per boundaries.md.

- [ ] **Step 3: Verify**

```bash
cd /home/lucievol/Projects/mosaic
for f in docs/design/asset-pipeline.md docs/design/physics.md; do
  awk '/```/{c++} END{print FILENAME, (c%2==0)?"BALANCED":"UNBALANCED"}' $f
done
! grep -rnE 'TBD|TODO|FIXME' docs/design/asset-pipeline.md docs/design/physics.md && echo "CLEAN"
```
Expected: both `BALANCED`, `CLEAN`.

- [ ] **Step 4: Commit**

```bash
cd /home/lucievol/Projects/mosaic
git add docs/design/asset-pipeline.md docs/design/physics.md
git commit -m "Add asset-pipeline and physics design docs"
```

---

### Task 8: automation-and-agents.md + rendering.md + scripting.md

**Files:**
- Create: `docs/design/automation-and-agents.md`, `docs/design/rendering.md`, `docs/design/scripting.md`

**Interfaces:**
- Consumes: command-event-system.md, reflection.md, boundaries.md.
- Produces: the automation security model and rendering/scripting stances referenced by the roadmap.

- [ ] **Step 1: Write `docs/design/automation-and-agents.md`**

- It is a **Mosaic Automation API**, not an "AI API"; AI is one consumer among CLI/Python/IPC/Remote/IDE/agents (mermaid of clients → Automation API → engine).
- Query verbs (`project.list_assets`, `scene.list_entities`, `entity.get_components`, `build.status`, `runtime.get_metrics`) + mutation via commands.
- **Security model (gap we close):** permissioned scopes `READ_PROJECT, MODIFY_PROJECT, EXECUTE_CODE, EXECUTE_PROCESS, NETWORK_ACCESS, FILE_ACCESS, BUILD_PROJECT, DEPLOY_PROJECT`; agents never get god mode by default.
- **AI as optional module:** `modules/ai/` with providers (local/remote LLM, vision, embeddings, speech, image, code); core only sees `IAgentProvider`; disabled ⇒ no model init, no network, no subprocesses, no behavior change; `--without-ai` excludes it entirely.

- [ ] **Step 2: Write `docs/design/rendering.md`**

- Backend-neutral: `RenderDevice`, `RenderGraph`, resources, pipelines, passes; backends under `modules/render_*`. Vulkan first (forces an honest abstraction).
- Render graph is first-class early: describe resources/passes (ShadowMap → Depth → GBuffer → Lighting → Reflections → PostProcess → UI, mermaid); the graph resolves dependencies → automatic sync, transient resources, reordering, visualization.

- [ ] **Step 3: Write `docs/design/scripting.md`**

- Python-first (Blender affinity, tooling, automation, agents, approachable). One **reflected** engine API; other languages (C/C++, JS/TS, Lua, WASM) target the same reflected surface later.
- Plugin tiers: static modules (compiled in), dynamic native plugins (`.dll/.so/.dylib`), script plugins (Python packages).

- [ ] **Step 4: Verify**

```bash
cd /home/lucievol/Projects/mosaic
for f in docs/design/automation-and-agents.md docs/design/rendering.md docs/design/scripting.md; do
  awk '/```/{c++} END{print FILENAME, (c%2==0)?"BALANCED":"UNBALANCED"}' $f
done
! grep -rnE 'TBD|TODO|FIXME' docs/design/automation-and-agents.md docs/design/rendering.md docs/design/scripting.md && echo "CLEAN"
```
Expected: all `BALANCED`, `CLEAN`.

- [ ] **Step 5: Commit**

```bash
cd /home/lucievol/Projects/mosaic
git add docs/design/automation-and-agents.md docs/design/rendering.md docs/design/scripting.md
git commit -m "Add automation/agents, rendering, and scripting design docs"
```

---

### Task 9: licensing.md + roadmap.md + repo-layout.md

**Files:**
- Create: `docs/project/licensing.md`, `docs/project/roadmap.md`, `docs/project/repo-layout.md`

**Interfaces:**
- Consumes: the spec's §2.3 (licensing), §5 (v0.1 slice), ARCHITECTURE.md (dependency rules).
- Produces: governance docs the README and CONTRIBUTING link to.

- [ ] **Step 1: Write `docs/project/licensing.md`**

Port spec §2.3 in full, in plain language: copyleft explained; AGPL vs GPL vs LGPL (and why full copyleft, not LGPL); dual-licensing mechanics (same code, two licenses, owner holds copyright); commercial tier can carry royalty terms; branding requirement; tracking is honor-system + legal, enforced against big visible successes; CLA requirement; Studio stays GPLv3. Link `LICENSE` and `LICENSE.commercial.md`.

- [ ] **Step 2: Write `docs/project/roadmap.md`**

- The merged phased roadmap: **Phase 0 Foundation → 1 First World → 2 Simulation → 3 Programming → 4 Production → 5 Tool ecosystem → 6 Agent ecosystem**, one paragraph each. State the ordering rule: "AI should make a great engine better, not make a mediocre engine appear easier to use."
- The **v0.1 vertical slice** verbatim from spec §5 (the create→import→world→physics→script→Play→Build story + the per-tier scope list + the explicit out-of-scope list).

- [ ] **Step 3: Write `docs/project/repo-layout.md`**

- The monorepo tree (foundation/engine/editor/studio/modules/tools/templates/tests/docs) from the ChatGPT plan, with a one-line responsibility per top dir. Restate the CI-enforced dependency rule and "directory ownership communicates architecture."

- [ ] **Step 4: Verify**

```bash
cd /home/lucievol/Projects/mosaic
for f in docs/project/licensing.md docs/project/roadmap.md docs/project/repo-layout.md; do
  awk '/```/{c++} END{print FILENAME, (c%2==0)?"BALANCED":"UNBALANCED"}' $f
done
! grep -rnE 'TBD|TODO|FIXME' docs/project/*.md && echo "CLEAN"
```
Expected: all `BALANCED`, `CLEAN`.

- [ ] **Step 5: Commit**

```bash
cd /home/lucievol/Projects/mosaic
git add docs/project/licensing.md docs/project/roadmap.md docs/project/repo-layout.md
git commit -m "Add licensing, roadmap, and repo-layout project docs"
```

---

### Task 10: CONTRIBUTING.md + CODE_OF_CONDUCT.md (+ CLA)

**Files:**
- Create: `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`

**Interfaces:**
- Consumes: licensing.md (CLA requirement), repo-layout.md, roadmap.md.
- Produces: the contribution workflow the small-team phase needs.

- [ ] **Step 1: Write `CONTRIBUTING.md`**

- How the repo is structured (link repo-layout.md) and the dependency rules contributors must respect.
- The RFC workflow (link `docs/rfcs/`): non-trivial changes start as an RFC.
- **CLA section:** all contributions require signing the CLA (grants relicensing rights so contributions can appear in commercial builds); explain *why* in one paragraph (dual-licensing) and link licensing.md.
- Commit/PR conventions; where the v0.1 slice work is tracked (link roadmap.md).

- [ ] **Step 2: Write `CODE_OF_CONDUCT.md`**

Adopt a short Contributor-Covenant-style code of conduct (paraphrase; standard scope, expected behavior, enforcement contact placeholder using an angle-bracket `<maintainer-contact>` field that is explicitly a fill-in, allowed per Global Constraints).

- [ ] **Step 3: Verify**

```bash
cd /home/lucievol/Projects/mosaic
! grep -rnE 'TBD|TODO|FIXME' CONTRIBUTING.md CODE_OF_CONDUCT.md && echo "CLEAN"
grep -q 'CLA' CONTRIBUTING.md && echo "CLA PRESENT"
```
Expected: `CLEAN`, `CLA PRESENT`.

- [ ] **Step 4: Commit**

```bash
cd /home/lucievol/Projects/mosaic
git add CONTRIBUTING.md CODE_OF_CONDUCT.md
git commit -m "Add CONTRIBUTING (with CLA) and CODE_OF_CONDUCT"
```

---

### Task 11: RFC template + seed data-model RFC

**Files:**
- Create: `docs/rfcs/0000-template.md`, `docs/rfcs/0001-core-data-model.md`

**Interfaces:**
- Consumes: data-model.md.
- Produces: the RFC workflow artifact referenced by CONTRIBUTING.md.

- [ ] **Step 1: Write `docs/rfcs/0000-template.md`**

Standard RFC template with angle-bracket fill-in fields (the permitted placeholder style): `# RFC <NNNN>: <Title>`, then sections — Summary, Motivation, Guide-level explanation, Reference-level explanation, Drawbacks, Rationale & alternatives, Unresolved questions, plus a metadata header (`Status: <Draft|Accepted|…>`, `Author: <name>`, `Date: <YYYY-MM-DD>`).

- [ ] **Step 2: Write `docs/rfcs/0001-core-data-model.md`**

Instantiate the template for the core data model: formalize `Entity/Component/System/World/Scene/Asset/Resource/Command/Event/Module/Plugin` with exact proposed definitions and serialization stances from data-model.md, marked `Status: Draft`. This is the first thing a contributor can build against — make definitions unambiguous (pick one interpretation everywhere).

- [ ] **Step 3: Verify**

```bash
cd /home/lucievol/Projects/mosaic
test -s docs/rfcs/0000-template.md && test -s docs/rfcs/0001-core-data-model.md && echo "RFCS OK"
! grep -rnE 'TBD|TODO|FIXME' docs/rfcs/*.md && echo "CLEAN"
```
Expected: `RFCS OK`, `CLEAN`.

- [ ] **Step 4: Commit**

```bash
cd /home/lucievol/Projects/mosaic
git add docs/rfcs/0000-template.md docs/rfcs/0001-core-data-model.md
git commit -m "Add RFC template and seed core-data-model RFC"
```

---

### Task 12: Build the docs hub (with inline-mermaid enhancement)

**Files:**
- Create: `docs/index.html`, `view_docs.sh`

**Interfaces:**
- Consumes: every doc created in Tasks 2–11.
- Produces: the browsable hub — the primary deliverable's front door.

- [ ] **Step 1: Write `docs/index.html` from the registry template**

Use the `establish-doc-hub` v1.1.0 `hub_template.html` (already fetched) as the base. Set the sidebar `<h1>` to "Mosaic". Build nav groups matching our tiers. Paths are **relative to `docs/index.html`**:
- General: `../README.md`, `vision/manifesto.md`
- Architecture: `architecture/ARCHITECTURE.md`, `architecture/data-model.md`, `architecture/boundaries.md`
- Design: `design/reflection.md`, `design/command-event-system.md`, `design/asset-pipeline.md`, `design/physics.md`, `design/automation-and-agents.md`, `design/rendering.md`, `design/scripting.md`
- Project: `project/licensing.md`, `project/roadmap.md`, `project/repo-layout.md`, `../CONTRIBUTING.md`, `../CODE_OF_CONDUCT.md`
- RFCs: `rfcs/0000-template.md`, `rfcs/0001-core-data-model.md`

All links use `loadFile('<path>', this)` (markdown mode — the inline-mermaid enhancement in Step 2 handles diagrams, so no separate mermaid entries needed).

- [ ] **Step 2: Enhance `loadFile()` to render inline mermaid fences**

The stock template only renders mermaid when a whole file is loaded as a dedicated mermaid doc. Our architecture/design docs embed ```mermaid fences inside markdown. In the `else` (markdown) branch of `loadFile`, after `contentDiv.innerHTML = marked.parse(text);`, add: find all `<pre><code class="language-mermaid">` blocks marked.js produced, replace each with a `<div class="mermaid">…decoded source…</div>`, then `await window.mermaid.run({ nodes: contentDiv.querySelectorAll('.mermaid') })` inside a try/catch. Insert this exact block:

```javascript
                    // Render any inline ```mermaid fences that marked.js
                    // emitted as <pre><code class="language-mermaid">…</code></pre>.
                    const mermaidBlocks = contentDiv.querySelectorAll('pre code.language-mermaid');
                    mermaidBlocks.forEach(code => {
                        const holder = document.createElement('div');
                        holder.className = 'mermaid';
                        holder.textContent = code.textContent;
                        code.closest('pre').replaceWith(holder);
                    });
                    if (mermaidBlocks.length && window.mermaid) {
                        try {
                            await window.mermaid.run({ nodes: contentDiv.querySelectorAll('.mermaid') });
                        } catch (err) {
                            console.error('Inline mermaid rendering failed', err);
                        }
                    }
```

- [ ] **Step 3: Copy the local view helper**

Write `view_docs.sh` from the skill's `resources/view_docs.sh` (python3 http.server on :8000, opens `docs/index.html`), then:

```bash
cd /home/lucievol/Projects/mosaic
chmod +x view_docs.sh
```

- [ ] **Step 4: Verify the hub serves and renders**

```bash
cd /home/lucievol/Projects/mosaic
python3 -m http.server 8123 >/tmp/mosaic_http.log 2>&1 &
SRV=$!; sleep 1
curl -fsS http://localhost:8123/docs/index.html | grep -q 'Mosaic' && echo "HUB SERVES"
curl -fsS http://localhost:8123/docs/architecture/ARCHITECTURE.md | grep -q 'mermaid' && echo "DOC FETCHABLE"
kill $SRV
```
Expected: `HUB SERVES`, `DOC FETCHABLE`. (For a full visual check, run `./view_docs.sh` and confirm a mermaid diagram renders on the Architecture page.)

- [ ] **Step 5: Commit**

```bash
cd /home/lucievol/Projects/mosaic
git add docs/index.html view_docs.sh
git commit -m "Build docs hub with inline-mermaid rendering and local view helper"
```

---

### Task 13: Cross-doc consistency sweep + propose hub enhancement upstream

**Files:**
- Modify: any doc with inconsistencies found.
- No new files.

**Interfaces:**
- Consumes: the whole doc set + hub.
- Produces: a self-consistent, shipped documentation set; a registry proposal for the inline-mermaid improvement.

- [ ] **Step 1: Full placeholder + fence sweep across everything**

```bash
cd /home/lucievol/Projects/mosaic
echo "== placeholders (expect none) =="
grep -rnE 'TBD|TODO|FIXME' README.md CONTRIBUTING.md CODE_OF_CONDUCT.md docs/vision docs/architecture docs/design docs/project || echo "NONE"
echo "== mermaid fence balance =="
for f in $(find docs -name '*.md'); do awk -v F=$f '/```/{c++} END{if(c%2)print "UNBALANCED", F}' $f; done; echo "checked"
```
Expected: `NONE` for placeholders (RFCs excluded — they use angle-bracket fields); no `UNBALANCED` lines.

- [ ] **Step 2: Consistency check on locked terms**

Confirm these appear consistently (no contradictions): dual-license wording, `PhysicsBackend`/no `b3*` in public API claims, dependency-rule direction, right-handed/Y-up/meters/radians, Vulkan-first. Spot-check:

```bash
cd /home/lucievol/Projects/mosaic
grep -rl 'AGPL' docs README.md | sort
grep -rn 'LGPL' docs/project/licensing.md   # must explain why NOT LGPL
grep -rn 'Y-up' docs/architecture            # convention stated where math is introduced
```
Fix any contradiction inline (edit the offending doc), then re-run Step 1's sweep for the file you touched.

- [ ] **Step 3: Verify the hub still lists every doc**

```bash
cd /home/lucievol/Projects/mosaic
for p in vision/manifesto architecture/ARCHITECTURE architecture/data-model architecture/boundaries \
  design/reflection design/command-event-system design/asset-pipeline design/physics \
  design/automation-and-agents design/rendering design/scripting \
  project/licensing project/roadmap project/repo-layout rfcs/0000-template rfcs/0001-core-data-model; do
  grep -q "$p" docs/index.html || echo "MISSING FROM HUB: $p"
done; echo "hub nav checked"
```
Expected: no `MISSING FROM HUB` lines.

- [ ] **Step 4: Commit any fixes**

```bash
cd /home/lucievol/Projects/mosaic
git add -A && git commit -m "Consistency sweep across Mosaic docs" || echo "no fixes needed"
```

- [ ] **Step 5: Propose the inline-mermaid enhancement back to the registry**

Per the LSR "propose, don't edit" rule, submit the Step-2 (Task 12) enhancement as a proposed evolution of `establish-doc-hub` so future hubs render inline mermaid. Use MCP `propose_skill_evolution(skill_name="establish-doc-hub", base_version="1.1.0", diff=<unified diff adding the inline-mermaid block to hub_template.html's loadFile markdown branch>, rationale="Docs commonly embed ```mermaid fences inside markdown; the stock loader only renders mermaid for dedicated files, so inline diagrams show as code. This renders them.", scope="general")`. Do **not** attempt to approve/merge/release.

---

## Notes on execution

- This is a documentation plan, so the TDD cycle is adapted: each task's "test" is a concrete verification (fence balance, placeholder scan, hub fetch) rather than a unit test. Content is derived from the committed spec and the two source plans, which live in the repo — an implementer reads those for substance.
- Keep each doc focused and reasonably short; the hub renders them individually, so length per doc should serve readability, not completeness-for-its-own-sake.
- If any external fetch (AGPL text, CDN scripts) is unavailable offline, note it in the commit and proceed; the hub CDNs (marked.js, mermaid.js) require network at *view* time only.
