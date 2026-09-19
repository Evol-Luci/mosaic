# Deferred Work Backlog

A point-in-time survey (2026-09-19, immediately after Phase 6 — Agent
ecosystem — completed, closing out every phase `docs/project/roadmap.md`
currently defines) of every deferred/future-work/stretch-goal item recorded
across this repo's specs, plans, design docs, and `next-steps.md`. Compiled
by grepping `docs/superpowers/specs/*.md`'s "Non-goals" sections (a
convention every spec in this repo has followed since Phase 6 began),
`docs/superpowers/plans/*.md`, `docs/design/*.md`, `docs/architecture/*.md`,
and `docs/project/next-steps.md` for "deferred"/"future work"/"not solved
here"/"accepted limitation"/"non-goal"/etc.

**This is a snapshot, not a live index.** Items here may already be
picked up, superseded, or stale by the time you read this — cross-check
against `next-steps.md`'s most recent entries and open PRs before treating
anything below as still-accurate. Re-run the survey (grep the same sources)
rather than trust this file blindly once significant time has passed.

## How to read this

Grouped first by subsystem (what area of the engine/tooling it touches),
then within each subsystem area a rough effort tier is noted. See the
**Effort vs. impact** section below for the cross-cutting prioritization
view that was actually used to pick what to build next as of this writing.

## Agent API / MCP (Phase 6 — complete, these are follow-on extensions)

- **Wrap remaining CLI verbs as MCP tools** — `mosaic plugin
  {list,install,remove,source...}` and `mosaic docs` regeneration. Each
  Phase 6 slice stuck to one-tool-per-slice; these are real, valuable, just
  not yet picked up. *Source:*
  `2026-09-19-project-maintenance-build-project-design.md`
- **Spatial queries** — "entities within radius R", scene bounding box. No
  distance/containment query machinery exists yet. *Source:*
  `2026-09-16-scene-reasoning-describe-scene-design.md`
- **Asset-usage queries** — "what entities reference asset X" via
  `FieldVisitor::field_asset`. Buildable, just not this slice. *Source:*
  same
- **Generic criteria-based search (query DSL)** — "find entities where
  field Y matches Z". Most general/powerful, needs its own decomposition.
  *Source:* same
- **Studio plugin (`.py`) authoring via MCP** — write `plugins/*.py`
  declaring `MOSAIC_MENU_ITEMS`/etc., needs its own validation (throwaway
  `PluginHost` load). *Source:*
  `2026-09-16-code-generation-set-script-design.md`
- **Undo support for `set_script`** — currently bypasses `CommandStack`.
  *Source:* same
- **`ScriptEditorModel::debug()` exposure** — compile + run `on_update(0)`
  with a debugger attached, no write. *Source:* same
- **Procedural asset generation** — generate texture/mesh/audio content
  from scratch (not import). No existing machinery, needs its own
  decomposition. *Source:*
  `2026-09-18-asset-generation-import-asset-design.md`
- **Confirm `AssetID` referencing via `set_component`** — probably already
  works, never tested/documented. *Source:* same
- **Hot-reload imported assets into the live `AssetRegistry` mid-session**
  — currently only loaded once at `mosaic mcp` startup. *Source:*
  `2026-09-18-automated-testing-tick-design.md`
- **"Tick until condition met" tool** — vs. today's fixed-count `tick`.
  *Source:* same
- **Broader per-system profiling** — physics/animation/particles timing,
  not just scripts. *Source:*
  `2026-09-19-optimization-script-profile-design.md`
- **Bulk refactoring / project health checks** — rename a field
  project-wide, orphaned-asset detection, broken-reference detection.
  Genuinely novel, no existing machinery. *Source:*
  `2026-09-19-project-maintenance-build-project-design.md`
  *(in progress: sub-project 3/4 shipped, see
  `docs/superpowers/specs/2026-09-19-project-health-orphaned-assets-design.md`)*
- **Multi-scene project support in MCP**, **concurrent MCP session locking
  on `save_scene`** — both explicitly unaddressed. *Source:*
  `2026-09-16-agent-api-mcp-server-design.md`

## Plugin ecosystem

- **Native `.so`/`.dll` dynamic-library plugins** — repeatedly deferred
  across all 3 Plugin SDK slices; ABI stability, cross-platform loader,
  sandboxing are the blockers.
- **Per-plugin enable/disable + manifest/versioning**, **live
  file-watching/hot-reload** (currently manual "Reload Plugins"),
  **user-global (non-project) plugins** — all still unbuilt.
- **Public marketplace** — hosting, moderation pipeline, static/dynamic
  analysis review layers. *Status:* design-only
  (`docs/design/marketplace.md`), explicitly "not yet scheduled."
- **Package manager: network fetch, version constraints/dependency
  resolution, remote registry, templates-as-packages, transitive deps** —
  all deferred, local-only today.

## Documentation tooling

- **CI/pre-commit wiring** for `mosaic docs` and the nav regenerator
  (`--check` mode) — both stay manually-run.
- **Precise field type labels** in component reference docs — needs
  extending `FieldVisitor`.

## UI pillar (declared "COMPLETE" at U5b, but with real gaps)

- Hover enter/exit + visual feedback, keyboard/gamepad navigation, text
  input fields, pointer capture/drag, z-order-aware overlap resolution,
  editor-mode interaction provider.
- Multi-line/word-wrap text, kerning, non-ASCII/Unicode, SDF/multi-size
  atlases.
- Aspect-preserving image fit/fill, 9-slice scaling, texture atlas
  sub-rects.
- Inherited visibility/alpha, clipping/scroll regions, flow/grid/stack
  auto-layout.
- World-space interactive UI (non-billboard labels, occlusion testing,
  distance fade).

## Networking pillar (declared "COMPLETE" at N7, but with real gaps)

- NAT traversal, encryption, compression (ENet doesn't provide these
  either).
- Reconnect/backoff/session resumption on drop, protocol-version
  negotiation.
- Delta/dirty snapshots, unreliable snapshot channel,
  interpolation/prediction, relevancy/interest management, per-component
  replication opt-in.
- Authoritative/anti-cheat input validation, sequence numbers, rate
  limiting.
- Real internet-facing dedicated-server story (firewalling, matchmaking) —
  explicitly "out of scope for the foreseeable future."
- Testing beyond loopback (genuinely separate machines).

## Blender Live-Link

- Mesh/geometry sync (currently transform-only).
- Windows support (Winsock) — Linux-only, though the socket layer is
  isolated for this.
- `CommandStack`/undo integration for live-link edits.
- Per-entity change detection/send throttling, multiple simultaneous
  Blender connections.

## Rendering

- RenderGraph / multi-pass structure — deferred since the point-light
  slice, never picked up (two-pass submission chosen instead each time it
  came up).
- Shadows, specular/PBR, spot lights, world-space per-frame light UBO.

## Project templates

- **3rd-person adventure template content** — needs a new `Health`
  component, character controller, basic combat/AI scripting. Real design
  work, explicitly tracked as deferred.
- Template parameterization (setup wizard, prompted values) beyond
  verbatim copy.
- Remove now-unused `types` param from `create_project` (29 call sites) —
  cosmetic cleanup, explicitly deferred.

## Editor / Studio

- **Studio has zero persisted preferences** — window size/panel
  layout/theme/recent-projects. Named as a natural fit for the
  user-config-directory work, not built.
- **Plugin discovery doesn't follow `--project-dir`**, only CWD — breaks
  for an installed/packaged distribution (desktop launcher, shortcut).
  Real, unscheduled architectural gap: needs `mosaic.toml`-rooted path
  resolution instead of CWD-relative everywhere.

## Scripting

- Same-frame writes / per-script inline override on threaded scripts
  (deliberately excluded — GIL/race reasons, likely permanent).
- Entity lifecycle (`create_entity`/`destroy_entity`) from a threaded
  script — unsupported, documented limitation.

## Tech debt

- **Issue #25 — Component TypeID densification** — `type_id<T>()` shares
  one global counter across all reflected types, wasting
  `ComponentRegistry::storages_` slots. Correctness-safe, but flagged as
  should-fix post-v0.1 and never touched.

## Effort vs. impact

Cross-cutting prioritization pass (2026-09-19), used to pick what to build
next. Re-derive this when the backlog above has moved significantly rather
than trusting the ratings below forever.

### 🟢 Quick wins (hours-scale, single focused change)

| Item | Impact |
|---|---|
| CI/pre-commit wiring for `mosaic docs` + nav regen | Medium — closes a doc-staleness gap that's already bitten this project twice |
| Confirm `AssetID` referencing via `set_component` | Low |
| Remove unused `types` param from `create_project` | Low |
| Hot-reload imported assets into live `AssetRegistry` | Medium |
| Wrap remaining CLI verbs as MCP tools | Medium |
| Studio: persisted preferences | Medium — small effort, but every session forever |
| "Tick until condition met" tool | Medium |

### 🟡 Medium (a real focused sub-project, bounded new machinery)

| Item | Impact |
|---|---|
| Spatial queries | Medium-High |
| Asset-usage queries | Medium |
| Undo support for `set_script` | Medium |
| Studio plugin (`.py`) authoring via MCP | Medium |
| Plugin discovery: project-dir not CWD | **Medium-High** — silent correctness bug for any packaged distribution |
| Per-plugin enable/disable + versioning | Medium |
| Plugin live file-watching/hot-reload | Medium |
| Component TypeID densification (#25) | Low-Medium |
| **Bulk refactoring / project health checks** | **High** — the only item giving an agent mass-edit/self-audit capability *(in progress: sub-project 3/4 shipped, see `docs/superpowers/specs/2026-09-19-project-health-orphaned-assets-design.md`)* |

### 🔴 Large (needs its own decomposition, multi-slice)

| Item | Impact |
|---|---|
| Blender mesh/geometry sync | Medium-High — the difference between a preview and a real DCC tool |
| Native `.so`/`.dll` plugins | High, but reopens native-code security surface |
| Rendering: shadows | High (visual quality bar) |
| Rendering: PBR/specular/spot lights | High |
| RenderGraph refactor | Medium (enabling, not user-visible) |
| UI pillar gaps | High if interactive-game UI is a near-term goal, else Low |
| Networking gaps | High if shipping real multiplayer soon, else Low |
| Package manager (network/deps/registry) | Medium |
| Public marketplace | Low today, High later — gated on an actual user base |
| 3rd-person adventure template | Medium |

**Picked as the largest effective win (2026-09-19):** bulk refactoring /
project health checks — see `docs/superpowers/specs/` for its design once
brainstormed.
