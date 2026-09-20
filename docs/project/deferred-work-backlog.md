# Deferred Work Backlog

A point-in-time survey, originally compiled 2026-09-19 immediately after
Phase 6 (Agent ecosystem) completed, closing out every phase
`docs/project/roadmap.md` currently defines. **Refreshed 2026-09-19** after
the project-health/bulk-editing arc (all 4 sub-projects) shipped, to mark
what that arc closed out and fold in what it surfaced along the way — see
"Refresh notes" at the end of the Agent API section. Originally compiled by
grepping `docs/superpowers/specs/*.md`'s "Non-goals" sections (a convention
every spec in this repo has followed since Phase 6 began),
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

- ~~**Wrap remaining CLI verbs as MCP tools**~~ — decomposed 2026-09-19
  into one-tool-per-slice sub-projects (this bundled 7-8 separate
  wrappers, too big for one slice): `plugin_list` **SHIPPED** (MCP
  tool 28), `plugin_install`/`plugin_remove` **SHIPPED** (MCP tools
  29-30), `plugin_source_{add,list,remove,pull}` **SHIPPED** (MCP
  tools 31-34). The last item, a `mosaic docs`-regeneration tool, was
  **dropped** (not built) after discovering `mosaic docs` always
  regenerates the Mosaic *engine repo's own* `docs/reference/`
  (`MOSAIC_DOCS_DIR` is baked in at compile time from
  `CMAKE_SOURCE_DIR`) -- completely independent of whatever project a
  `mosaic mcp <project-dir>` session is serving. Every other MCP tool
  here is meant for an agent building a *game* with Mosaic; this one
  would only ever matter to an agent doing self-hosted Mosaic engine
  development (like this session) -- a scope mismatch with the
  server's actual purpose, confirmed with the user rather than built
  anyway. **With this, the whole arc is closed** (3 of its 4 slices
  shipped, the 4th correctly dropped). *Source:*
  `2026-09-19-project-maintenance-build-project-design.md`
- **Spatial queries** — "entities within radius R", scene bounding box. No
  distance/containment query machinery exists yet. *Source:*
  `2026-09-16-scene-reasoning-describe-scene-design.md`
- ~~**Asset-usage queries**~~ — **SHIPPED**, `entities_referencing`, MCP
  tool 26. "What entities reference asset X", via a third `FieldVisitor`
  subclass (`AssetReferenceCollector`) alongside `AssetRefCollector`
  (`broken_references`) and `ReferencedAssetGatherer` (`orphaned_assets`),
  both in `app/mcp/mcp_tools.cpp`. Classified bounded, not architectural
  — no spec/plan doc, agreed in chat and implemented directly. *Source:*
  `2026-09-16-scene-reasoning-describe-scene-design.md`.
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
- ~~**Confirm `AssetID` referencing via `set_component`**~~ — **RESOLVED**,
  no longer deferred. Exercised repeatedly and directly during the
  project-health arc's manual verification (`broken_references`,
  `orphaned_assets`, `bulk_set_field` all set `Material.texture` to a
  literal `AssetID` string via `set_component` against a real `mosaic mcp`
  session) — confirmed working, no gap remains.
- ~~**Hot-reload imported assets into the live `AssetRegistry` mid-session**~~
  — **SHIPPED**. `import_asset` now reruns `load_project_assets()` on
  success, threaded through a new `handle_tool_call` pointer parameter.
  `broken_references`/`orphaned_assets`/`entities_referencing` see a
  same-session import immediately, no restart. *Source:*
  `2026-09-18-automated-testing-tick-design.md`.
- ~~**"Tick until condition met" tool**~~ — **SHIPPED** as `tick_until`
  (MCP tool 27), reusing `entities_where`'s exact matching primitives.
  *Source:* same
- **Broader per-system profiling** — physics/animation/particles timing,
  not just scripts. *Source:*
  `2026-09-19-optimization-script-profile-design.md`
- ~~**Bulk refactoring / project health checks**~~ — **SHIPPED, all 4
  sub-projects complete** (`entities_where`, `broken_references`,
  `orphaned_assets`, `bulk_set_field` — MCP tools 22-25). No longer
  deferred; kept here only as the historical pointer. *Source:*
  `2026-09-19-project-maintenance-build-project-design.md`. Specs:
  `2026-09-19-project-health-{entities-where,broken-references,
  orphaned-assets,bulk-set-field}-design.md`.
- **Multi-scene project support in MCP**, **concurrent MCP session locking
  on `save_scene`** — both explicitly unaddressed. *Source:*
  `2026-09-16-agent-api-mcp-server-design.md`

**Refresh notes (2026-09-19, post-arc):**
- ~~The `FieldVisitor` extension~~ — **SHIPPED**, along with both of its
  consumer follow-ups except one. `field_entity`/`field_handle_asset`
  exist (`engine/reflection/field_visitor.hpp`), both read-only, both
  defaulting to the exact prior `field_readonly` output — zero behavior
  change for any existing visitor. `broken_references`/`orphaned_assets`/
  `entities_referencing` cover `Handle<R>` references, closing that
  blind spot. `broken_references` also now detects a non-nil `EntityID`
  field that fails `world.is_valid()` (verified end-to-end against a
  real destroyed entity via `Transform.parent`) — every `"broken"` entry
  now carries a `"kind"` discriminator (`"asset"`/`"entity"`).
  Precise field type labels in component reference docs also **SHIPPED**
  — with this, every follow-up the `FieldVisitor` extension enabled is
  now closed out.
- The `AssetRegistry`-staleness gap ("hot-reload imported assets... mid-
  session") was hit and explicitly worked around three separate times
  this arc (`broken_references`, `orphaned_assets`, and implicitly by
  every tool that reads `asset_registry`) — each time by documenting the
  limitation in the affected tool's own schema description rather than
  fixing it. Same underlying gap, now with three call sites' worth of
  concrete motivation and lived experience with its exact failure mode.

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

- ~~CI/pre-commit wiring for `mosaic docs` + nav regen~~ — **SHIPPED** as
  a local pre-commit hook (`.githooks/pre-commit`, opt-in via
  `git config core.hooksPath .githooks`). Caught real staleness in
  `docs/index.html`'s nav on its first run. **Scope note:** this repo
  has no CI at all (no `.github/workflows/`), so only the pre-commit
  half was built — standing up actual CI is a separate, larger item,
  see below.
- **No CI exists in this repo at all** — discovered while shipping the
  pre-commit hook above. `CONTRIBUTING.md` claims the `Foundation →
  Engine → Editor → Studio` dependency rules are "enforced in CI," but
  there is no `.github/workflows/` (or any other CI config) anywhere —
  that enforcement doesn't actually run anywhere today. Standing up a
  first CI workflow (running the test suite, then the dependency-rule
  check, then possibly the same docs/nav staleness check the local
  hook covers) is real, undone work, not a stale doc claim to just
  delete — scope TBD (a GitHub Actions build of this project needs a
  full C++ toolchain + Vulkan/GLFW deps, non-trivial to set up).

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
- ~~Remove now-unused `types` param from `create_project`~~ — **SHIPPED**
  2026-09-20, ~40 call sites updated.

## Editor / Studio

- **In-editor "File → Reload"** — reload the current scene from disk,
  discarding in-editor edits. Explicitly deferred as its own small
  follow-up slice in `2026-08-29-tools-t2-scene-load-save-design.md`;
  Studio still only has "Reload Plugins", no scene-reload-from-disk
  action (confirmed by grep, `studio/studio_app.cpp`). Borderline
  Quick/Medium: touches the undo stack and `PlayState`, not pure UI.
  Surfaced 2026-09-20 re-derivation.
- ~~**Studio has zero persisted preferences**~~ — window size/panel
  layout **SHIPPED** 2026-09-20 (`app/studio_prefs.hpp`,
  `~/.config/mosaic/studio.toml` + `studio_layout.ini`). Still open,
  as their own future slices: **theme** (no theme system exists yet —
  a new feature, not just persistence of an existing setting) and
  **recent-projects** (no tracking or picker UI exists — also a new
  feature, needs a project-picker shown at startup with no
  project-dir arg).
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

Cross-cutting prioritization pass, originally 2026-09-19, **re-derived
2026-09-19** post-arc: the shipped item is removed, resolved items are
removed, and the two refresh-note items (asset-usage queries' now-easier
build, the `FieldVisitor` extension's now-tripled motivation) are
re-rated. Re-derive again when the backlog above has moved significantly
rather than trusting the ratings below forever.

### 🟢 Quick wins (hours-scale, single focused change)

**Empty as of 2026-09-20** — every item from the original 2026-09-19 survey
shipped. Re-derive this tier from the sections above before picking
anything else; don't assume there's nothing left just because this list
is momentarily blank.

### 🟡 Medium (a real focused sub-project, bounded new machinery)

| Item | Impact |
|---|---|
| Spatial queries | Medium-High |
| Undo support for `set_script` | Medium |
| Studio plugin (`.py`) authoring via MCP | Medium |
| Plugin discovery: project-dir not CWD | **Medium-High** — silent correctness bug for any packaged distribution |
| Per-plugin enable/disable + versioning | Medium |
| Plugin live file-watching/hot-reload | Medium |
| Component TypeID densification (#25) | Low-Medium |
| Studio theme system + persistence | Medium |
| Studio recent-projects list (picker UI + tracking) | Medium |
| In-editor "File → Reload" | Low-Medium — touches undo stack/`PlayState`, borderline Quick win |

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
| Stand up real CI (GitHub Actions) | Medium-High — `CONTRIBUTING.md` claims dependency rules are "enforced in CI" but no CI exists at all today; needs a full C++/Vulkan/GLFW toolchain in the runner |

**Picked as the largest effective win (2026-09-19, original pass):** bulk
refactoring / project health checks — **shipped**, all 4 sub-projects
complete (see specs above). Next pick TBD with the user.
