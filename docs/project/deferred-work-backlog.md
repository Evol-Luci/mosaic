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
- ~~**Verify AssetID referencing via `set_component`**~~ — **CONFIRMED**
  2026-09-20, no code change needed. `2026-09-18-asset-generation-
  import-asset-design.md`'s Non-goals speculated this was "arguably
  already possible" through the existing generic `set_component` tool
  but left it unverified. It works exactly as speculated: `set_value`'s
  default `ReferenceResolver::resolve_asset` is a plain identity
  passthrough (`foundation/serialization/reference.hpp`), so a bare
  `AssetID` field accepts any UUID string verbatim, no registry
  validation at set-time (matching `broken_references`'s own
  validate-on-read design, not a gap). Already extensively exercised
  by existing tests (`mcp_tools_test.cpp`'s `broken_references`/
  `orphaned_assets` test suites both use `set_component` on
  `Material.texture` as their own setup, both with stray and
  real-registered ids) -- re-confirmed end-to-end via the real CLI:
  imported a real PPM asset, `set_component`'d `Material.texture` to
  its id, `get_component` read it back correctly, `broken_references`
  did not flag it. *Source:* `2026-09-18-asset-generation-import-
  asset-design.md`
- ~~**Spatial queries**~~ — **SHIPPED** 2026-09-20, `entities_near`/
  `scene_bounds`, MCP tools 35-36. Computes each entity's world
  position live via `TransformSystem::compute_world()` (made public)
  rather than the cached `WorldTransform`, which never gets
  recomputed in a headless `mosaic mcp` session after startup and
  would have been silently stale for anything moved mid-session.
  *Source:* `2026-09-16-scene-reasoning-describe-scene-design.md`
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
- ~~**Studio plugin (`.py`) authoring via MCP**~~ — **SHIPPED** 2026-09-20
  (MCP tools 37-39, PR #193). `plugin_script_write` validates via a
  throwaway `PluginHost` load (a scratch directory holding only the
  candidate file, bound to the real live World) before persisting to
  the project's real `plugins/`; `plugin_script_read`/`plugin_script_list`
  round out the loop. *Source:*
  `2026-09-16-code-generation-set-script-design.md`
- ~~**Undo support for `set_script`**~~ — **SHIPPED** 2026-09-20.
  `ScriptEditorModel::apply()` now applies via `SetComponentCommand`
  (the same mechanism `set_component` uses) instead of a raw field
  mutation, so undo restores the entity's entire prior
  `ScriptComponent` snapshot. *Source:* same
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
- ~~**Per-plugin enable/disable + manifest/versioning**~~ — **shipped**
  2026-09-21 (PR #194). ~~**Live file-watching/hot-reload**~~ — **shipped**
  2026-09-21 (PR #195); Studio no longer needs a manual "Reload Plugins"
  click for edits made while the editor is open. **User-global
  (non-project) plugins** — still unbuilt.
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

- **Tiling / repeat for `UiImage`** — deferred 2026-10-10 during the image-presentation
  design. Not a flag: with a sub-rect in play, repeating means either a separate atlas
  page (so the sampler's address mode can wrap the whole texture) or shader-side wrapping
  *within* the sub-rect, because a wrapping sampler would pull in neighbouring sprites.
  Its own slice, and the decision between those two is the work.
- **UV flip / rotation for `UiImage`** — deferred 2026-10-10. Cheap to add on the sub-rect
  UV machinery that slice lands (swap or transpose the emitted UV corners), and recorded
  only because nothing currently asks for it. Do not add it speculatively; it is two
  fields and a precedence question against `sub_rect` that nobody needs answered yet.
- **Batching the UI image pass** — `RenderSystem` issues **one `draw_ui_image` call per
  `UiImage`**, each with its own push constants and its own `vkCmdDraw` of six vertices.
  Recorded 2026-10-10, before it matters rather than after: it is fine at current scale,
  but it is the number **9-slice will multiply by nine**, so a HUD with a dozen 9-sliced
  panels goes from ~12 draws to ~108. **Worth measuring rather than pre-optimising** —
  the fix (one instanced draw, or a small vertex buffer per pass) is well understood, and
  the trigger is a real frame-time number, not a hunch.
- **An authored aspect override on `UiImage`** — deliberately NOT added by the aspect-mode
  slice. `Fit`/`Fill` read the texture's real pixel dimensions; an override field would be
  a second source of truth for the same fact, and the two would disagree the moment an
  asset was re-exported. Recorded 2026-10-10 so the omission reads as a decision rather
  than an oversight.
- **`Fit` ignores the anchor when it centres** — surfaced by the U7 whole-branch review on
  2026-10-10, as a behaviour the spec is silent on rather than as a defect. `Fit` centres
  the shrunk rect inside the authored rect, so an image anchored `TopRight` with `Fit`
  **drifts inward** by half the letterbox and no longer touches the edge it was anchored
  to. "Anchor to the corner" and "centre within the rect" are in genuine tension, and a
  user placing a HUD icon flush to a corner meets the drift with no warning. Deferred
  because fixing it means choosing a rule — align the shrunk rect to the anchor's own edge,
  or add an explicit alignment field — and that is a design decision, not a bug fix; doing
  it blind would replace one silent behaviour with another. **Depends on** nothing; the
  arithmetic is three lines in `resolve_image_quad`. Note no test covers anchor interaction
  with `Fit` today: both `RenderSystem` `Fit` cases use the default anchor.
- **A sub-rect that lands entirely off the texture renders a one-texel smear** — also from
  the U7 review, 2026-10-10. `sub_rect_pos = {64, 0}` on a 64-wide texture clamps to
  `u0 == u1 == 1.0`, a zero-width UV span: under `Stretch` the user gets the rightmost
  column stretched across the whole rect, and under `Fit`/`Fill` the `src_w_px > 0` guard
  returns early so the mode is silently ignored and they get `Stretch` of that same column.
  The spec says "clamped to its bounds" and stops there. Deferred rather than fixed because
  the alternatives — treat a fully-off-texture sub-rect as the whole texture, or skip the
  draw the way an unresolved handle is skipped — are both defensible and the input is an
  authoring mistake either way; what is wrong is only that the mistake is **invisible**.
  Cheapest real improvement is probably a one-line warning at the seam rather than a
  behaviour change. **Depends on** nothing.
- **Four minor findings from the U7 whole-branch review (2026-10-10)**, recorded rather
  than fixed in that slice's one fix pass, which was reserved for the three Important
  findings. None changes what a user sees today; all four are the kind of thing that is
  cheap now and annoying later.
  - **`ImageFitMode` lives in `engine/ui/ui_fit.hpp`, but the field it describes
    (`fit_mode`) lives on `UiImage` in `ui_image.hpp`**, which does not include it. Anyone
    setting the field symbolically must add a second include — `app/run/demo_scene.hpp`
    had to. The enum arguably belongs beside the component. Deferred because moving it is
    a header-dependency decision touching the `engine/ui` tier, not a one-liner, and the
    current arrangement is merely inconvenient rather than wrong.
  - **The "position starts past the edge" test asserts bounds, not the defined outcome.**
    It checks `u0 <= 1.0`, `u1 >= u0` where the actual result is `u0 == u1 == 1.0`, so a
    fall-back-to-whole-texture implementation would also pass. Separately, the
    clamp-removal mutation it is shaped for would call `std::clamp` with `lo > hi`, which
    is **UB** — so its red depends on unspecified behaviour. Worth tightening to exact
    values and finding a mutation that is defined.
  - **`TextureCache`'s invariant is enforced distributively, not locally.** It guards on
    `handle != 0`; the reason a 0×0 `Image` cannot yield a non-zero handle lives in
    `VulkanRenderDevice::create_texture`, which returns `0u` for a zero dimension. A
    different `RenderDevice` that returned a handle for a degenerate image would silently
    break the documented invariant. Deferred because the downstream consequence is benign
    — `resolve_image_quad` degrades to full `0..1` UVs with no letterboxing and no NaN —
    so this is defensive polish, not a latent bug.
  - **The HUD image pass does not sort by `UiImage::order`,** unlike the panel pass, even
    though `order` is reflected and documented on the component. **Pre-existing, not
    introduced by U7** — flagged only because that slice was the one that re-read the loop.
    Overlapping HUD images draw in `for_each` order, which is stable but arbitrary. Fold
    into whichever slice next touches UI draw ordering rather than taking it alone.
- **9-slice scaling for `UiImage`** — **DEFERRED AS A LONGER-TERM GOAL on 2026-10-10**, by
  explicit decision rather than by running out of time. U7 shipped the UV plumbing 9-slice
  depends on, so it is *unblocked* and was the obvious next slice — and it is being passed
  over anyway, in favour of editor-facing work (editor starts stopped, then U6 interaction
  depth). **Why:** nothing currently asks for 9-slice. It makes authored HUD art scale
  nicely, which matters when there *is* authored HUD art; the demo has one checker sprite.
  The two items chosen ahead of it are both things a person meets in the first seconds of
  using the editor. Taking 9-slice now would be building the most technically interesting
  remaining UI feature rather than the most needed one. **Nothing about it decays by
  waiting** — its dependency has landed and cannot un-land, and the three decisions below
  are unaffected by time. Revisit when something real needs a scalable panel or button
  background; until then this is deliberately not the next slice.
  Split out of U7 on 2026-10-10, at the start of
  that slice, because the three features U7 grouped are **not** independent:
  9-slice needs nine different sub-rects of one texture, so it is built *on top of*
  the sub-rect UV plumbing rather than beside it. Taking it in the same slice would
  put UV plumbing, an aspect-mode enum and a **nine-fold draw-count change** in one
  diff, and the draw-count change is the only part of U7 with a performance
  dimension — a combined review would have to find it amongst two unrelated
  features. **Depends on** U7's sub-rect UVs, which land first; after that this adds
  only the nine-quad expansion on machinery already proven. **What it will need
  deciding:** whether the border insets are authored in pixels (friendlier, needs the
  texture's dimensions, which U7 is already caching) or normalized; whether the centre
  patch stretches or tiles; and whether 9 quads per image is acceptable or wants
  batching — worth measuring rather than pre-optimising, since it is the first thing
  in the UI path that multiplies draw calls.

**DECOMPOSED 2026-10-07** into six sub-projects — see
`docs/superpowers/specs/2026-10-07-ui-pillar-decomposition.md`. Decomposed now
because the tier table's condition changed: it rated UI "High if interactive-game
UI is a near-term goal, else Low", and until viewport pointer input shipped a UI
button could not be pressed in the editor at all, so none of this was reachable in
the program that can author it. **"Editor-mode interaction provider" is struck from
the list below — it shipped.**

Two measurements from the decomposition that make items smaller or larger than they
read: `UiPanel` has an `order` field but `UiButton` does not, and `UiSystem::hit`
takes the first `for_each` match, so z-order-aware hit-testing needs a key on the
interactive component before it needs a sort; and the neutral `RenderDevice` exposes
no scissor, so **clipping/scroll is a renderer change before it is a UI change** —
which is why the decomposition separates it from auto-layout rather than bundling
them as this list does.

- ~~**U6 interaction depth** — hover enter/exit + visual feedback, pointer
  capture/drag, z-order-aware overlap resolution. First: its prerequisite just
  landed, and it is the gap a user meets in the first five seconds.~~
  **SHIPPED 2026-10-10** — two of the three features. Z-order hit resolution now lives in
  `engine/ui/ui_hit.hpp`'s pure `topmost_hit`, `UiButton` carries its own `order`, and
  `UiSystem` emits `hover_enter`/`hover_exit` on the button's existing `event` channel with
  the same `{entity, action}` payload the click path uses. **The motivating bug turned out to
  be a wrong answer rather than an undefined one:** the press took the FIRST button under
  the pointer while the panel pass draws the LAST of an equal-order run on top, so a click
  on two overlapping equal-order buttons went to the button the user could not see.
  **Capture/drag remains open** (listed above). So do both accepted costs — a button's
  `order` can disagree with its visual's, and the engine still renders no hover feedback of
  its own — and neither was resolved by this slice. ~~Carries one open
  question for a human — whether `UiButton` gets its own `order` or inherits its
  `UiPanel` ancestor's.~~ **DECIDED 2026-10-10: `UiButton` gets its own `order`**, matching
  `UiPanel`, `UiImage` and `UiText`, all of which already carry one — `UiButton` was the
  only UI component without it. The known cost of that choice, recorded so it is not
  rediscovered as a surprise: a button's `order` and its overlaid visual's `order` **can
  disagree**, and nothing warns about it. **Scoped 2026-10-10 to z-order + hover**, with
  capture/drag split out (below).
- **Pointer capture and drag for `UiButton`** — split out of U6 on 2026-10-10 when U6 was
  scoped, not dropped. **Why it was separated:** z-order and hover are *coupled* — "which
  button is hovered" has no answer without a tie-break, so hover built on the current
  behaviour would flicker between overlapping buttons — whereas capture is partly present
  already (`UiSystem` remembers `pressed_` and requires release inside the same button), so
  what remains is a smaller and genuinely independent change. **And it is the riskiest
  third:** its failure modes are the subtlest (the pointer leaving the window mid-drag, a
  press surviving a play-mode change), which is the exact bug class that shipped viewport
  pointer input **dead** while thirteen tests stayed green. Reviewing it beside two
  unrelated features is how that recurs. **Depends on** U6's z-order work only loosely;
  it mainly needs the pointer-state widening recorded for U9 (wheel/multi-button) if drag
  is ever to mean more than left-button. **What it will need deciding:** whether a captured
  press follows the pointer outside the window, and whether a drag emits per-frame events
  or only a start/end pair.
- ~~**A pointer that leaves the window can still hover an edge-anchored button**~~ —
  **FIXED 2026-10-10 in the U6 review pass, and this entry was WRONG twice over while it
  stood.** It scoped the problem to `app/main.cpp` and "the pointer is over another
  application", when in the editor the clamp region is a *panel* inside a window full of
  other panels — so leaving it is a constant, ordinary action, not an edge case. And it
  justified deferring by saying the honest fix needed a `PointerState` widening "the same
  one U9 needs"; that was false for the editor path, where `ViewportPointerSample::hovered`
  was **already sampled and already consulted three lines away** for `down`. It also never
  mentioned the `(0,0)` case at all, which is not "tracks to the edge, better than
  sticking" but the exact phantom hover the `if (!pointer_)` guard was written for, arriving
  through a different door: headless `mosaic run` never creates a window, so its provider
  returned `{0,0,false}` every tick and hovered any button containing the origin forever.
  **What shipped instead:** `PointerState` gained an explicit `inside` flag, honoured in
  `topmost_hit` so press and hover cannot disagree about it; the editor provider sets it
  from `hovered`; and headless is given **no** provider rather than one returning zeroes.
  The lesson worth keeping: the entry asserted the demo was unaffected and therefore eyes
  would not catch it, and treated that as a reason to defer rather than as a reason to fix.
- **Hover styling is never cleared when a hovered button is destroyed** — from the U6
  review, 2026-10-10. `hover_exit` deliberately emits nothing when the hovered entity has
  lost its `UiButton`, because the channel name lived on that component and there is no
  honest name left to emit on. Correct as far as it goes, but a script that styled on
  `hover_enter` has no way to learn it should stop: delete a hovered button at runtime and
  the styling stays applied forever. Deferred because the fix needs a channel that does not
  depend on the component — a global `ui_hover_lost` event carrying only the entity id, or
  caching the last channel name alongside `hovered_` — and choosing between those is a
  design call. **Depends on** nothing.
- **The press/hover candidate list is a snapshot, and nothing says so** — from the U6
  review, 2026-10-10. `UiSystem::update` flattens candidates once, then emits hover, then
  handles press/release against that same list. So a **synchronous** subscriber that adds a
  covering button during `hover_enter` does not block that update's click, while the same
  change made between ticks does. A user would meet "covering a button blocks its click,
  except when the cover is created by the hover event in the same frame." Now stated in the
  spec as the rule, but untested, and the test that cites this scenario actually exercises
  the between-ticks path instead. Deferred because deciding whether the snapshot is the
  *intended* rule (rather than an artifact) is a design question, and the demo avoids it
  entirely by using `subscribe_deferred`.
- **(superseded entry, kept for the reasoning)** The original 2026-10-10 note read:
  resolved question from the U6 spec, measured during that slice rather than left open.
  The spec flagged a risk that hover might "stick" on the last position; it does not. The
  GLFW provider at `app/main.cpp:301-314` **clamps** the cursor to `[0,1]`, so a pointer
  outside the window maps onto the window **edge**. Hover therefore tracks to the edge
  instead of freezing — which is better than sticking, and still wrong for any button flush
  to that edge: it stays hovered while the pointer is somewhere else entirely, including
  over another application. The demo's HUD bar starts at x = 0.02 so it is unaffected, which
  is also why this would not be noticed by eye. **Deferred because the fix belongs in the
  PROVIDER, not in `UiSystem`:** the clamp is deliberate (it keeps clicks from resolving to
  wild coordinates), so the honest change is for `PointerState` to carry an "inside the
  window" flag — GLFW has `glfwGetWindowAttrib(w, GLFW_HOVERED)` for exactly this — and for
  hover to treat "outside" as hovering nothing. That widening is the same one U9 needs for
  wheel and multi-button, so it should be done once. Papering over it with a timeout inside
  `UiSystem` was considered and rejected: it would make hover depend on frame timing.
- ~~**Engine-rendered hover visuals (a `UiHoverStyle` component)**~~ — **SHIPPED 2026-10-10.**
  `UiHoverStyle` (authored, reflected) multiplies an element's colour and alpha while
  hovered; `UiHovered` (runtime, deliberately **unregistered**) marks the hovered entity;
  `active_hover_style` walks up the `UiParent` chain and `resolve_appearance` does the
  multiply and clamp; the three UI render passes apply it as a **render-time override** so
  nothing mutates the authored values. The demo now authors a style instead of running a
  script, and its nested "HP" label inherits the style from the bar it is parented to.
  **Still open:** the `order`-disagreement item below, which this slice unblocked but
  deliberately did not take. Originally recorded as **UNBLOCKED and specified 2026-10-10**: `docs/superpowers/specs/2026-10-10-ui-hover-ownership-design.md`. The blocker
  turned out to be a non-problem. **The ownership relation already existed as entity
  identity:** every button in the repository shares an entity with its visuals (the shipped
  template's entity 8 carries `UiButton` + `UiImage` + `UiPanel` + `UiText`; the demo puts
  `UiButton` on the `camera` entity that already holds the HUD `UiPanel`), with no
  counter-example anywhere. The convention was in the data and had simply never been written
  down. The stated rule is now "the element's own entity, or its nearest `UiParent`
  ancestor", resolved by walking **up** — `UiParent` points upward, so finding descendants
  would have meant scanning every entity. Original reasoning kept below, because it was wrong
  in an instructive way. **Why it was deferred:** it needs a button→visual **ownership**
  relationship that does not exist.
  `UiButton` is deliberately non-visual and `UiParent` is a *transform* hierarchy, not an
  ownership one, so "tint the thing this button looks like" has no definition today. It
  would also put interaction state into the render path, which has so far been kept free of
  it. **The honest cost of deferring:** on the events-only design the engine shows a user
  **nothing** by itself — a button acknowledges the pointer only if someone wrote a script
  for it, which is not what a person expects from a button. U6 ships a demo script so the
  behaviour is visible, but that is a demonstration, not a default. **Depends on** settling
  UI ownership, which is also what the `order`-disagreement item below needs — they are
  likely one piece of work.
- **An absolute hover colour, rather than a multiplier** — non-goal of the hover slice,
  2026-10-10. `UiHoverStyle` multiplies the authored colour and alpha, because `{1,1,1}` and
  `1.0` make a default-constructed style an exact no-op and one style then composes across
  differently-coloured elements. The cost is that hover cannot be a *specific* colour
  ("highlight to exactly this blue"), only a scaling of what was authored. Deferred because an
  absolute override needs an extra "is set" flag to stay safely default-constructable, plus a
  precedence rule against the multiplier — cheap on this machinery, and wanted by nothing yet.
- **Pressed and disabled visual states** — non-goal of the hover slice, 2026-10-10. The same
  mechanism would serve them: a runtime tag plus an authored style. Deferred because each
  needs its own decision about **precedence when several apply at once** (hovered *and*
  pressed, disabled overriding both), and that ordering question is the actual work rather
  than the components. **Depends on** nothing; the hover slice establishes the pattern.
- **Hover transitions or easing** — non-goal of the hover slice, 2026-10-10. Hover is
  instantaneous: the style applies on the frame the tag appears. Animating it needs a
  per-element clock the UI subsystem does not have, which is a larger addition than the fade
  itself. Not to be confused with the particle system's existing over-life easing curves,
  which are unrelated machinery.
- **Hover (and hit-testing) on non-button UI components** — `UiPanel`, `UiImage` and
  `UiText` are not hit-testable at all; only `UiButton` is. Deferred from U6 on 2026-10-10
  because making them interactive opens a larger question this slice should not answer:
  whether every visual is interactive by default (and if so, what a `UiText` click means),
  or whether interactivity stays opt-in via an overlaid `UiButton` as it is now. **Depends
  on** nothing technically — the rect resolution already exists for all three — which is
  exactly why it needs the design decision first rather than an implementation.
- **A button's `order` can disagree with its visual's `order`** — the accepted cost of the
  decision above, recorded 2026-10-10 as its own item because it is a real authoring trap
  rather than a note. `UiButton` carries its own `order` for hit-testing while the panel or
  image a user *sees* carries an independent one, so a button can claim to be on top of a
  visual that is drawn underneath it, and the only symptom is a click landing on the wrong
  thing. ~~Deferred because the fix is not obvious: a validation pass that warns on an
  overlapping button/visual pair whose orders disagree needs a definition of "belongs to"
  that does not exist yet (`UiParent` is a transform hierarchy, not an ownership one).~~
  ~~**UNBLOCKED 2026-10-10.** The ownership rule is now stated (see the `UiHoverStyle` item):
  a button owns the UI components on its **own entity**. So the check is concrete — an entity
  carrying both a `UiButton` and a `UiPanel`/`UiImage`/`UiText` whose `order` values differ —
  and needs no new relation.~~ **SHIPPED 2026-10-11 as a button-vs-`UiPanel` lint**, and the
  "`UiPanel`/`UiImage`/`UiText`" framing above turned out to be **wrong in two of its three
  parts**. `engine/ui/ui_order_check.hpp`'s `find_order_disagreements` compares a `UiButton`'s
  `order` against the `UiPanel` on its own entity only; `studio/order_warnings.hpp`'s
  `OrderWarningModel` de-duplicates so an every-frame check warns on the transition; the
  editor frame loop logs `log::warn("ui", ...)` into the Console panel. Still held back from
  the hover slice deliberately, because it is a **diagnostic** rather than a rendering
  feature, and bundling a warning system into a rendering diff is how a reviewer loses track
  of which half to scrutinise. **It is a heuristic lint, not a correctness check** — see the
  reframing item below.
- ~~**The REAL `order` bug is pairwise and relative, not per-entity and absolute**~~ —
  **SHIPPED 2026-10-12** as `find_order_conflicts` in `engine/ui/ui_order_check.hpp`, with the
  button-rect flatten extracted to `engine/ui/ui_button_rects.hpp` so the check polices the
  exact rects `UiSystem`'s hit test uses. Two kinds: **Inverted** (both axes ordered,
  opposite rankings — the definite bug) and **Ambiguous** (one axis tied, so the outcome
  falls to iteration order and can flip when an unrelated button is destroyed). Both-axes-tied
  is deliberately silent: the author expressed no preference either way, and reporting it
  would fire on every pair of overlapping default-constructed buttons. Overlap is inclusive,
  matching `topmost_hit`, so buttons touching along an edge count. The per-entity lint
  **stays** — it catches the authoring slip before a second button exists, which is exactly
  when this check has nothing to say. Original framing, which was right:  found
  2026-10-11 while building the lint above, which is why that lint is honest about being a
  heuristic. A single button in a scene cannot misroute a click whatever its `order` says: the
  failure needs **two overlapping buttons**, where A's `UiButton::order` ranks above B's while
  A's `UiPanel::order` ranks below B's, so the click goes to A and the user sees B on top. The
  shipped lint catches the authoring slip that bug grows from (editing a panel's order and
  forgetting its button), which is cheap; the real check needs resolved-rect overlap testing
  between every pair of buttons, the screen aspect, and is O(n²) over buttons. **Deferred
  because it is a different and much larger piece of work** than the recorded item asked for,
  and the cheap lint plausibly prevents most instances. **Depends on** nothing new —
  `resolve_ui_rect` already exists — so this is a scoping decision, not a blocker.
- ~~**`UiImage::order` and `UiText::order` are ignored by the renderer entirely**~~ —
  **FIXED 2026-10-11.** Both passes now collect, `stable_sort` ascending, then emit, so higher
  `order` draws on top within its own pass and equal orders keep `for_each` order. The
  semantics are documented on both components for the first time. Original finding, which
  still explains the pass-boundary limitation below:
  confirmed
  2026-10-11 by reading `engine/render/render_system.hpp`'s three HUD passes. They are fixed
  and sequential (panels, then images over panels, then text over both) and **only the panel
  pass `stable_sort`s**. So a `UiText` is above a `UiPanel` unconditionally, whatever either
  `order` says, and both fields are reflected, documented and inert. This is **why the lint
  above compares panels only**: an earlier version compared all three and fired on the shipped
  demo, whose `camera` entity carries `UiPanel{0}`, `UiImage{0}` and `UiText{1}` — a finding
  **no button order could have silenced**, because those three do not agree with each other
  either. The 🟢 quick win "Sort the HUD image pass by `UiImage::order`" is half of the fix;
  the text pass needs the same treatment and is **not** currently tracked as a quick win.
  Until both land, any cross-pass `order` comparison is meaningless.
- **Combo boxes for enum-as-int fields, instead of a bare `DragInt`** — the explicit non-goal
  of the `field_int` slice, 2026-10-11. `UiPanel`/`UiText`/`UiImage`'s `anchor`, `UiImage`'s
  `fit_mode`, `ParticleEmitter`'s three easing curves and `AudioSource::attenuation_model` are
  all enums stored as `int`, so the Inspector now lets a user drag `anchor` to 47. **Nothing
  breaks** — every one of those paths was checked and defends itself, `to_anchor` clamping
  outside 0–8, `resolve_image_quad` falling back to Stretch, `apply_easing` and
  `to_ma_attenuation_model` both having a `default:` arm — but the value is meaningless and
  the widget says nothing about it. **Deferred because the real fix needs enum metadata on the
  reflection surface** so a visitor can enumerate named values, which `reflect()` does not
  carry today; that is its own design, plausibly architectural, rather than a widget swap.
  **Depends on** that metadata and nothing else.
- **`ImGuiFieldVisitor` has no test coverage at all** — noticed 2026-10-11 while adding
  `field_int`, and pre-existing rather than introduced by it. **Re-scoped 2026-10-12, and the
  earlier framing was too optimistic:** "needs a headless ImGui context" undersells it. A
  context with a null backend is easy — `CreateContext`, set `io.DisplaySize`, build the
  default font atlas, run `NewFrame`/`Render`. But *exercising* a `DragInt`'s write-back needs
  a simulated **multi-frame mouse drag** against ImGui's internal active-item state, which is
  exactly what `imgui_test_engine` exists for and which is **not vendored** here (ImGui 1.91.5
  arrives via `FetchContent`; no test engine). A hand-rolled harness would buy weak coverage
  ("the widget does not crash", "`changed_` stays false when idle") for real effort. So this
  item is really **"adopt `imgui_test_engine`"** — a new third-party dependency, and therefore
  a decision for a human rather than something to pick up unattended. Original entry: There is no
  `tests/unit/studio/inspector_test.cpp`: `field_float`, `field_string`, `field_asset` and now
  `field_int` are each one `ImGui::` call that no test executes, because ImGui needs a live
  context. The adapter *dispatch* is well covered in
  `tests/unit/reflection/field_visitor_test.cpp`, so what is untested is narrow — "does this
  widget write back and set `changed_`" — but it is exactly where a typo would land silently.
  **Deferred because it needs an ImGui context in the test binary** (a headless context is
  possible but is new test infrastructure), not because the gap is acceptable.
- **The order lint's drag-burst: reviewed 2026-10-12 and kept.** Not deferred work — recorded
  so it is not reopened as a bug. Dragging `UiButton::order` in the Inspector emits one warning
  per intermediate value, because `OrderWarningModel` keys a finding on the entity *and* both
  numbers. Each line is a genuinely different disagreement, so this is not a de-duplication
  failure. Debouncing during an active drag was considered and **declined**. The decision is
  also written into `studio/order_warnings.hpp` beside the code that causes it.
- ~~**Three copies of the same anchor clamper**~~ — **FIXED 2026-10-12.** One
  `mosaic::engine::ui::to_anchor` now lives in `engine/ui/ui_rect.hpp`, beside the `Anchor`
  enum it clamps to, and all five call sites use it. Its upper bound is written against
  `Anchor::BottomRight` rather than a literal `8`, so extending the enum needs no edit there
  at all — which was the actual hazard, not the duplication. Original entry: `to_anchor` in
  `engine/render/render_system.hpp`, `hierarchy_anchor` in `engine/ui/ui_hierarchy.hpp` and
  `button_anchor` in `engine/ui/ui_button_rects.hpp` are the same four-line
  `(a >= 0 && a <= 8) ? cast : TopLeft` in three files. Noticed 2026-10-12 while moving the
  third one, and a **deliberate non-goal of that slice**: collapsing them is an unrelated
  refactor touching the render system, and bundling it into a diagnostic diff is how a
  reviewer loses track of which half to scrutinise. Pre-existing, correctness-safe (all three
  are identical), cheap whenever someone wants it. **Depends on** nothing.
- ~~**No end-to-end test that a conflicting pair actually misroutes a click**~~ — **CLOSED
  2026-10-12**, the same night it was filed. `tests/unit/render/render_system_test.cpp`'s "a
  conflicting pair really does misroute the click" builds one world with an inverted pair,
  drives a real `PointerState` through press and release, asserts the `ScriptEvent` came from
  the button with the higher `UiButton::order`, then renders the same world through
  `FakeRenderDevice` and asserts the OTHER button's panel is the last quad in the batch —
  so the one that answered is provably the one drawn underneath. Probed both ways: inverting
  `topmost_hit`'s ordering and reversing the panel pass's sort each turn it red, so it holds
  both mechanisms and not just one. Original entry: recorded
  2026-10-12 as the one thing the pairwise check cannot prove about itself. It reports that
  `UiButton::order` and `UiPanel::order` rank a pair differently; that this *causes* the wrong
  button to receive the event rests on `topmost_hit`'s tests plus the panel pass's sort, each
  covered separately but never together. A test that drives a real `PointerState` into a
  conflicting pair and asserts which entity gets the `ScriptEvent` would close it. **Depends
  on** nothing — `UiSystem` already takes an injected pointer, so it is headless; it was left
  out only to keep the slice's diff about the check itself.
- **Cross-entity overlap is still invisible to both order diagnostics** — restated
  2026-10-12 now that the pairwise check exists and *still* cannot see it. A button whose
  visual lives on a different entity is skipped whole by `find_order_conflicts`, because the
  ownership rule gives it no visual rank. Detecting it needs rect-overlap analysis between a
  button and panels it does not own, which is a different and much larger question than the
  pair comparison that shipped.
- **A unified z-order across all UI, crossing the pass boundary** — the explicit non-goal of
  the pass-sorting slice, 2026-10-11, and the limitation a user is most likely to hit next.
  `order` now ranks panels against panels, images against images and text against text, but
  the three HUD passes still run in a fixed sequence, so **a `UiText` is above every `UiPanel`
  whatever either `order` says** and a negative `UiText::order` will not push a label behind a
  panel. Making one `order` space work across all UI means collapsing three passes into one
  sorted stream over three different pipelines and handle types, plus a decision about how to
  batch what is currently batched per pass. **Deferred because it is architectural** — its own
  spec — while the within-pass fix was bounded and unblocked two inert fields today.
  **Depends on** nothing; it is a scope decision. Recorded in both component headers so a
  reader meets the limitation where they meet the field.
- **A button overlaying a `UiPanel` on a DIFFERENT entity is invisible to the lint** — the
  deliberate blind spot of the entity-identity ownership rule, recorded 2026-10-11 rather than
  left implicit. Detecting it needs the same rect-overlap analysis as the pairwise check above,
  so the two are **one piece of work**, not two.
- **The demo scene undercuts the ownership rule's evidence base** — noticed 2026-10-11. The
  hover slice cited "the shipped template's entity 8 carries `UiButton` + `UiImage` + `UiPanel`
  + `UiText` together" as proof that co-location means ownership. But `app/run/demo_scene.hpp`
  piles the unrelated "MOSAIC" title and the checker `UiImage` onto the `camera` entity purely
  for convenience — they are not part of the HUD button in any sense a user would recognise.
  **Co-location is not always ownership**, so the rule is a convention the repository happens
  to follow, not an invariant the data enforces. Deferred because nothing breaks today: the
  shipped lint reads only `UiPanel`, which on that entity genuinely is the button's visual.
  Worth revisiting before any feature leans harder on "the components on a button's entity".
- ~~**`int` fields are read-only in the Inspector (no `field_int`)**~~ — **FIXED 2026-10-11**,
  so the order lint's advice is now actionable in-editor: the field the warning names can be
  dragged. Original entry: recorded 2026-10-11.
  `ImGuiFieldVisitor` (`studio/inspector.hpp`) implements `field_float`, `field_string`,
  `field_asset` and `field_readonly` only, so every `order`, `anchor` and `fit_mode` renders as
  a label. **This makes the new lint's advice unactionable inside the editor**: a user reads
  "UiButton order 7 but UiPanel order 0" in the Console and then has to go and edit the scene
  file or write a script, because the field the warning names cannot be typed into. Cheap on
  the existing visitor machinery — one `field_int` override plus a `DragInt` — and it is the
  natural companion to the lint rather than part of it.
- ~~**U7 image presentation** — aspect-preserving fit/fill, 9-slice scaling, texture
  atlas sub-rects. Independent of everything else and the cheapest real group.~~
  **SHIPPED 2026-10-10** — but only two of the three features. `UiImage` gained
  `sub_rect_pos`/`sub_rect_size` (texture **pixels**, zero size = whole texture) and
  `fit_mode` (Stretch/Fit/Fill), with all the arithmetic in one pure function,
  `engine/ui/ui_fit.hpp`'s `resolve_image_quad`, tested with no world and no device.
  `TextureCache` now keeps `{handle, width, height}` — it already loaded the `Image`
  to create the texture and threw the dimensions away. `UiImagePush` went 32 → 48 and
  `ui_image.vert` interpolates the UVs with the same `mix()` form `ui_text.vert` has
  used for glyph sub-rects all along. **9-slice is still open** and still listed above;
  its stated dependency — this slice's sub-rect UVs — now exists.
- **U8 layout containers** — inherited visibility/alpha, flow/grid/stack
  auto-layout. Needs nothing from the renderer.
- **U9 clipping and scroll regions** — **needs a scissor hook on `RenderDevice`**
  first; scroll additionally needs wheel input, i.e. the deferred `PointerState`
  widening.
- **U10 keyboard, gamepad, and text input** — **blocked** on an editor input
  provider (recorded separately above). Starting it sooner would ship navigation
  nobody can drive, which is the mistake pointer input just fixed.
- **U11 text quality** — word-wrap and Unicode are reachable now (note
  `layout_text` currently *skips* unknown codepoints, so a non-atlas character
  silently shortens the string); SDF/multi-size atlases want their own
  decomposition rather than riding beside the cheap half.
- **World-space interactive UI** (non-billboard labels, occlusion testing, distance
  fade) — deliberately left OUT of all six groups. It shares almost nothing with
  screen-space UI: it needs depth testing and a world-space hit path, not rect
  resolution, so folding it in would make a seventh group that is a different
  feature wearing the same name.

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
  came up). **Shadows (below, in scoping as of 2026-09-21) will bring the
  editor's viewport path to 3 direct-submission passes** (shadow depth →
  offscreen viewport → swapchain blit) — still not being used as the
  trigger to build the graph for real (confirmed with the user: extend
  direct submission again for shadows' v1), but this is now the closest
  the project has come to the "≥3-pass slice" bar named as the graph's
  actual trigger back at 37d/46. Worth re-raising with the user the next
  time a *dynamic* pass need shows up (conditional/reorderable passes,
  not just "one more fixed pass"), since that's the harder case direct
  submission genuinely doesn't scale to.
- ~~**Shadows — v1 (directional-only, hard-edged, fixed-size ortho
  frustum)**~~ — **SHIPPED 2026-09-21.** See `docs/project/next-steps.md`
  for the full shipment entry (a real editor-pass-ordering bug found by
  functional testing, not by review, and fixed with a ruling). The
  upgrades below remain real, explicitly-deferred follow-ups, each its
  own future slice, not silently folded in:
  - **PCF / soft shadow filtering** — v1 ships hard-edged (single depth
    sample) shadows deliberately, to prove the pipeline (light-space
    matrix, depth pass, sampling) before adding filtering. A small,
    separable follow-up (a few extra texture samples averaged in the
    shader) once v1 is solid.
  - **Point-light shadows (cubemap / 6-pass omnidirectional depth)** —
    v1 is directional-light-only. Point-light shadows are a genuinely
    different technique (cubemap rendering, not a single ortho
    frustum), roughly doubling the scope of v1 — its own future slice.
  - **Scene/camera-fitted (and eventually cascaded) shadow frustum** —
    v1 uses a fixed-size orthographic box centered on the origin,
    correct for the current demo/test scenes but not for scenes bigger
    than that box or not centered near the origin. Fitting the frustum
    to the live scene bounds (similar math to the existing
    `entities_near`/`scene_bounds` MCP tool) or the camera's view
    frustum, and eventually cascaded shadow maps for large open scenes,
    are named, deliberate follow-ups, not built in v1.
  - **Configurable shadow map resolution** — v1 fixes the shadow map's
    resolution as an implementation constant, not a user- or
    project-facing setting. Exposing it (likely alongside a future
    quality/performance settings surface, not on its own) is a named
    follow-up once there's a real place for such a setting to live.
- **"Specular/PBR, spot lights" decomposed 2026-09-21** (confirmed with
  the user before brainstorming any of it): the current lighting model
  is Lambert diffuse only (no specular term at all), single directional
  + single point light ("first found," never accumulated), no shadows
  for point lights. ~~**Spot lights**~~ — **SHIPPED 2026-09-21**, the
  first, smallest slice (reused `PointLight`'s `WorldTransform`-derived
  positional machinery, no material/BRDF changes, no multi-light
  question). See `docs/project/next-steps.md` for the full shipment
  entry. Deferred, each its own future slice, not silently folded into
  spot lights:
  - **Specular highlights (Blinn-Phong)** — a shininess/specular-color
    param added to `Material` and the shader's lighting math, without
    committing to a full PBR material model. Bigger than spot lights
    (touches the whole lighting shader), smaller than full PBR.
    **Unblocked 2026-09-27** — it was blocked on the shading-data UBO
    migration below, which has now shipped. The blocker was that
    Blinn-Phong needed 5 new floats against a `PushConstants` struct
    pinned at the 256-byte ceiling with 4 padding slots left. Post-
    migration that pressure is largely gone: there are 48 bytes of
    per-draw headroom, and because shading is now done in world space
    the camera position is *per-frame* data that belongs in the
    lighting UBO rather than the per-draw payload — so specular's only
    per-draw additions are shininess and strength (8 bytes). This is
    the natural next rendering slice.
  - ~~**Perceptual `0..1` gloss remap**~~ — **resolved 2026-10-04 by PBR
    slice 2.** `Material::shininess` was a raw Phong exponent and a poor
    authoring knob: perceptually non-linear, so 8 → 16 was a large visual
    change while 200 → 208 was invisible. It was deferred 2026-09-27
    because the remap decision belonged with PBR's roughness parameter and
    shipping one earlier would have left two competing gloss scales in the
    material system at once. `roughness` **is** that remap — the deferral
    was resolved by the parameter replacing the exponent outright rather
    than by a second scale being added beside it.
  - **Metallic/roughness texture maps** — per-texel `metallic` and
    `roughness` sampled from a map, rather than the two scalar `Material`
    params. Deferred 2026-09-27 during the Blinn-Phong slice's design,
    when it was framed as shininess and specular-strength maps; PBR
    slice 2 replaced those parameters, so this now reads against
    metallic/roughness and is PBR **slice 3**, decomposed but not yet
    specced. It needs a second texture binding and an asset-pipeline
    answer (how such a map is authored, imported, and referenced) that
    has nothing to do with the shading math itself. It is also the
    reason intermediate `metallic` values exist at all — they are for
    texture blending, not for authoring by hand.
  - ~~**Fresnel / energy conservation on the specular term**~~ —
    **resolved 2026-10-04 by PBR slice 2.** Blinn-Phong v1 was
    deliberately non-energy-conserving: `specular_strength` was an
    authored fudge factor, not a physically derived `F0`. Deferred
    2026-09-27 because a Schlick Fresnel term only makes sense once the
    BRDF around it is physically based; doing it inside Blinn-Phong would
    have been half a PBR model with none of its coherence. Slice 2 ships
    Schlick Fresnel with `F0 = mix(vec3(0.04), base_color, metallic)` and
    `kD = (1 - F)(1 - metallic)`, so diffuse and specular are now
    energy-conserving relative to each other; `specular_strength`
    dissolved rather than migrating. **Still open from this item:** the
    multi-scatter / energy-compensation term — single-scatter GGX loses
    energy at high roughness, and the Fdez-Agüera or LUT correction is a
    refinement on a working BRDF, listed separately below.
  - **The light components' default intensities still mean the pre-π
    thing** — `PointLight::intensity` and `SpotLight::intensity` default
    to `1.0f` and `DirectionalLight::color` to `{1,1,1}`, all authored
    against the non-energy-conserving diffuse term. Since PBR slice 2
    divides diffuse by π, a light a user adds with the defaults now
    renders ~3.14× dimmer than the identical light did before, and a
    "unit" light is no longer a unit of anything. Deferred 2026-10-04:
    the slice-2 spec considered moving the π into the component defaults
    and rejected it with an argued reason — the demo's explicitly-set
    values would still have needed re-authoring, so it would hide where
    the constant went without avoiding the work. Recorded because the
    consequence outlives the decision; the real fix is a defined unit for
    light intensity (lumens or candela), which is its own design
    question. Surfaced by the slice-2 branch review.
  - **`metallic` outside `0..1` gives a material its diffuse lobe back**
    — the engine deliberately does not clamp shading parameters, and
    tests now explicitly guarantee an out-of-range `metallic` reaches the
    GPU unchanged. At `metallic = 2`, `F0 = 2·albedo − 0.04` can exceed 1
    and `kD = (1 − F)(1 − metallic)` flips sign twice to become `F − 1`,
    so a "more than metal" surface is both over-energetic in specular and
    diffuse-lit again. Finite and tone-map-clamped, so it is a wrong
    image rather than a NaN. Deferred 2026-10-04: the spec defines
    `metallic` only on `0..1` and calls intermediate values physically
    meaningless, so there is no stated requirement to violate — but the
    shader already bounds `roughness` on both sides for the same reason,
    and the asymmetry is the kind that reads as an oversight later.
    Depends on nothing; a one-line `clamp` in `cube.frag`. Surfaced by
    the slice-2 branch review.
  - **Per-light specular color distinct from diffuse color** — each
    light currently carries one color used for both terms. Deferred
    2026-09-27: real renderers almost never separate these, and doing
    so would grow the lighting UBO for a knob nobody has asked for.
    Low priority; listed only so the omission is deliberate.
  - **The demo resolves its asset paths relative to the working directory**
    — `build_default_scene` loads `assets/textures/sphere_mr.png` with a
    relative path, so running `mosaic run --window` from anywhere but the
    repository root silently loses the one surface that demonstrates
    per-texel roughness. Noticed 2026-10-04 while implementing PBR
    slice 3, and made loud rather than silent (it logs a warning naming
    the cwd-relative path) because the failure otherwise looks like the
    map being ignored. Deferred rather than fixed: the real answer is a
    project-root or asset-root concept that the importer, the demo, and
    the editor all share, which is an asset-pipeline decision rather than
    a rendering one. The demo is the only caller affected today.
  - ~~**Mipmaps**~~ — **SHIPPED 2026-10-05** as IBL sub-slice 4a. Every
    texture now uploads a full chain (`floor(log2(max(w,h))) + 1` levels,
    generated by a `vkCmdBlitImage` ladder) and the sampler's `maxLod`
    lets it be sampled. PBR slice 3 surfaced the need rather than causing
    it: a varying roughness map aliases badly at distance, and shimmering
    specular is the classic symptom. Gated on the format advertising
    `VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT`, with a
    single-level fallback, because a linear-filter blit against a format
    lacking it is undefined rather than an error.
  - **sRGB mip generation goes through `vkCmdBlitImage`, which some drivers
    filter in the wrong space** — base-colour textures are
    `R8G8B8A8_SRGB`, and the blit ladder that generates their mip chain
    filters them. The Vulkan spec routes blit filtering through the image
    operation rules, which include the sRGB-to-linear decode, so averaging
    is *specified* to happen in linear space — but several drivers have
    historically filtered sRGB blits in the encoded space, making each mip
    slightly darker than correct, compounding down the chain. Symptom: a
    textured surface darkens subtly with distance on one vendor and not
    another. Raised in the slice-4a review 2026-10-05 and recorded rather
    than fixed: it is spec-correct behaviour that some drivers get wrong,
    the magnitude is small, and the structural fix is a different body of
    work — downsampling through a graphics or compute pass using a
    `_UNORM` alias view of the same image, which also removes the
    `BLIT_SRC`/`BLIT_DST` format-feature dependency the generation path
    now has to check. Depends on nothing; worth doing if the darkening is
    ever actually observed.
  - **Mip chains on the environment cubemap** — deferred 2026-10-05
    during IBL sub-slice 4b's design. The skybox barely minifies, so the
    aliasing argument that motivated sub-slice 4a does not apply, and
    4c's prefiltered specular cube is built by *convolution* at
    increasing roughness rather than by blitting the environment down —
    so it is a separate cubemap with its own levels, not a mip chain of
    this one. Would require extending 4a's blit ladder to array layers.
    Depends on nothing; no known need.
  - **Cubemap and texture compression for the cooked environment** — the
    cooked cubemap form is uncompressed, matching the existing texture
    pipeline's decision. A 512² RGBA16F cubemap is about 6 MB, and 4c
    adds two more on top. Deferred 2026-10-05 during 4b's design; shares
    its answer with the existing texture-compression entry above.
  - **Runtime environment switching, and more than one environment** —
    4b loads exactly one cubemap, once. Deferred 2026-10-05: multiple
    environments need a selection concept (per-scene? per-zone?
    blended?) that nothing has asked for, and switching needs the IBL
    products (4c) rebuilt or double-buffered.
  - **`.exr` and other HDR container formats** — `.hdr` (Radiance RGBE)
    is what stb_image reads without adding a dependency. Deferred
    2026-10-05 during 4b's design: OpenEXR is a substantial third-party
    library and nothing needs its features (tiles, layers, deep data)
    today.
  - **Dynamic or captured reflection probes** — capturing the scene into
    a cubemap at runtime needs the render-to-cubemap machinery that 4b's
    design explicitly rejected for the import path, plus a policy for
    when to re-capture. Deferred 2026-10-05. Depends on 4c landing
    first, since a probe is only useful once something consumes an
    environment.
  - **A GLSL include mechanism for shared shader code** — the reason
    `tonemap_aces` is duplicated into the skybox shader rather than
    shared: the shader build has no include step, and adding one is a
    build-system change rather than a rendering one. Deferred
    2026-10-05 during 4b's design. A contract test pins the duplicated
    copies as identical, which is a weaker guarantee than one
    definition; this becomes worth doing the moment a third shader needs
    the same function.
  - **Runtime-selectable cubemap face size** — fixed at 512 by 4b.
    Deferred 2026-10-05: it is a quality-versus-memory knob with nothing
    to balance it against yet, and `AssetMetadata::import_settings`
    already exists as its home when something needs it to vary. Changing
    it requires a re-import, which is the stated cost of converting at
    import time.
  - **Specular occlusion** — IBL applies environment specular without regard to
    local occlusion, so cavities and creases receive the full reflection and
    read too bright. Deferred 2026-10-06 during IBL sub-slice 4c's design: the
    standard fix multiplies the specular IBL term by an ambient-occlusion
    factor, which needs AO maps — already deferred above as part of the glTF
    standard map set — or a screen-space AO pass. Depends on that.
  - **Parallax-corrected or box-projected environment probes** — the
    environment cubemap is treated as infinitely distant, so a reflection does
    not change as an object moves within a room and interior reflections look
    wrong. Deferred 2026-10-06 during 4c's design: the correction needs a proxy
    volume per probe and a ray-box intersection in the shader, plus the
    multiple-environment concept already deferred in 4b. Depends on that.
  - **Rotating the environment relative to the scene** — the cubemap's
    orientation is fixed at import. Deferred 2026-10-06 during 4c's design: a
    yaw offset is cheap to add (one uniform, one rotation in the sampling
    direction) but nothing has needed it, and it must be applied consistently
    to the skybox, the irradiance lookup and the prefiltered lookup or they
    disagree.
  - **A runtime quality knob for the IBL convolution sizes** — irradiance is
    fixed at 32² and prefiltered at 128² with five mip levels, changeable only
    by re-import. Deferred 2026-10-06 during 4c's design, the same decision 4b
    made for cubemap face size and for the same reason: it is a
    quality-versus-memory knob with nothing to balance it against yet, and
    `AssetMetadata::import_settings` already exists as its home.
  - **Anisotropic filtering** — still open, and deliberately NOT bundled
    with the mipmap work above. It needs `samplerAnisotropy` enabled as a
    device feature at logical-device creation, which is a change to device
    setup rather than to the upload path, and it wants a
    `maxSamplerAnisotropy` query to clamp against. The aliasing PBR
    slice 3 surfaced is mip-related, so mipmaps alone address what is
    actually visible today; anisotropy improves grazing-angle sharpness on
    surfaces like the ground plane. Deferred 2026-10-05. Depends on
    nothing beyond the device-feature change.

  - **A texture-compression / GPU-format pipeline (BCn, ASTC)** — the
    cooked image form (`MOSAIMG1`) is uncompressed RGBA8, so a 2048²
    texture costs 16 MB of VRAM. Deferred 2026-10-04 during PBR slice 3's
    design: it needs a compressor in the cook step, a format negotiation
    against device support, and a cooked-format version bump, none of
    which interact with the shading work. Becomes worth doing when
    texture memory rather than draw count is the limit.
  - **Per-material UV transforms and a second UV set** — `Vertex` carries
    one `uv`, and every map on a material samples it. Deferred
    2026-10-04 during PBR slice 3's design: a tiling/offset transform is
    cheap to add later (it is push-constant data), and a second UV set is
    a vertex-format change best taken together with the tangent decision
    that normal mapping forces. Depends on nothing; low priority until
    an asset actually needs it.
  - **Bindless textures (one descriptor array, indices in the push
    block)** — deferred 2026-10-04 while scoping PBR slice 3. It is the
    end state this is heading toward: one array of all textures bound once
    per frame, per-material indices travelling in the push constants
    (there are 40 of 256 bytes free), zero per-material binds, and every
    future map type free of new set slots. Rejected for slice 3 because it
    needs `VK_EXT_descriptor_indexing` with a capability check and a
    fallback path, and it reworks texture registration, pooling, and the
    draw path at once — a pillar, not a slice. The chosen design (a
    descriptor set per material-texture combination, two bindings in
    set 0) does not block it; it makes it a smaller change later, since
    set ownership will already have moved off the texture table. Becomes
    worth doing when the map count per material passes ~3, or when draw
    batching starts being limited by descriptor binds.
  - **Normal mapping, and the tangent-frame question it forces** —
    deferred 2026-10-04 while scoping PBR slice 3, which was originally
    decomposed as "metallic/roughness/normal" maps. Two things pushed it
    out. Slice 3 absorbed image file loading (the engine had none — every
    texture was a procedural pixel loop), so adding normal maps would have
    made one slice out of import, a descriptor rework, and two map types.
    And `Vertex` is `{position, normal, uv}` with **no tangent**, so normal
    mapping needs a tangent frame from somewhere, and that is a real
    decision rather than a detail: either per-fragment derivative tangents
    (`dFdx`/`dFdy` of world position and UV — no vertex change, costs
    fragment ALU, degrades on degenerate UVs) or a real tangent attribute
    (correct and cheap at runtime, but touches the vertex format, the
    pipeline attribute descriptions, and every mesh generator). Depends on
    slice 3's descriptor rework landing first, since it needs a third map
    bound per material.
  - **The rest of the glTF standard map set — occlusion and emissive** —
    not considered during slice 3's scoping, which deliberately took only
    base colour plus a packed metallic/roughness map. Deferred 2026-10-04:
    an AO map is meaningless until there is an ambient term worth
    occluding, which is IBL below; emissive needs a decision about whether
    it participates in tone mapping and, eventually, bloom. Neither is
    blocked by anything slice 3 does, and both get cheaper once slice 3's
    per-material descriptor layout exists.
  - **Ambient/environment specular (IBL)** — **DECOMPOSED 2026-10-05**
    into three sub-slices, for the same reason the full PBR item was
    decomposed: scoping measured four missing capabilities stacked
    underneath it, not one feature. The flat `0.1` ambient term
    contributes no highlight, and since PBR slice 2 made the ambient
    `kD`-weighted it contributes *nothing whatsoever* to a metal
    (`kD = (1 - F)(1 - metallic)` is exactly zero at `metallic == 1`), so
    the demo's metal sphere renders near-black with two specular
    highlights — confirmed visually 2026-10-04 and accepted as correct, a
    metal with nothing to reflect having nothing to show. It is the most
    visible single argument for this work.

    **What the 2026-10-05 scoping measured, each of which changed the
    plan:** there are **no cubemaps** in the engine (no
    `VK_IMAGE_VIEW_TYPE_CUBE`, no `samplerCube`; every texture is a 2D
    single-layer image); there are **no compute shaders** (zero
    `vkCmdDispatch`, zero compute pipelines — though the shadow pass
    proves offscreen render-pass machinery exists); **no mipmaps** exist
    (`mipLevels = 1`, `sampler.maxLod = 0.0f` everywhere), and prefiltered
    specular *is* a mip chain, so that is a hard prerequisite rather than
    a nicety; and **HDR decoding is switched off** (`STBI_NO_HDR` in
    `image_cook.cpp`, since float HDR has no route into the RGBA8 `Image`
    type), while an environment map is normally a `.hdr` radiance file
    precisely because an LDR sky clips the sun.

    - **(4a) Mipmaps and anisotropic filtering** — taken first because it
      is independently valuable and already wanted: see its own entry
      below, which PBR slice 3 promoted from "deferred" to "visible".
    - **(4b) Cubemaps plus an HDR image path** — cubemap images, views,
      samplers and the equirectangular-to-cube conversion, plus restoring
      a float image route so a `.hdr` environment can be loaded. Depends
      on 4a for the mip chain the prefiltered cube needs.
    - **(4c) The IBL math** — irradiance convolution for diffuse, the
      roughness-indexed prefiltered specular cube, and the BRDF LUT; the
      flat `0.1` ambient is replaced here. Depends on 4a and 4b. This is
      also what unlocks the **furnace test**, the only check that could
      show the slice-2 BRDF is *correct* rather than merely wired, which
      the slice-2 spec recorded as its honest limit.

    Rejected while scoping: one large all-at-once IBL slice (very large
    review surface, nothing verifiable until most of it exists, an early
    wrong call expensive), and a procedural analytic sky as a shortcut
    (defers the HDR path but adds a throwaway environment source to be
    replaced later, while still needing cubemaps and mipmaps anyway).

  - **Full PBR material model** — **DECOMPOSED 2026-10-04** into four
    slices; see `docs/superpowers/specs/2026-10-04-pbr-decomposition-and-tone-mapping-design.md`
    for the reasoning and the measured starting state. (1) **Tone
    mapping** — specced in that document and taken first, because an
    energy-conserving BRDF produces values above 1.0 routinely and the
    8-bit sRGB target clips them, so doing the BRDF first would make the
    renderer look *worse* until tone mapping landed. It also pays for
    itself now: specular already clips. (2) **Metallic/roughness plus a
    GGX Cook-Torrance BRDF** with Schlick Fresnel and energy
    conservation — **SHIPPED 2026-10-04.** It *absorbed* the "perceptual
    `0..1` gloss remap" item above (roughness **is** that parameter) and
    the "Fresnel / energy conservation" item. Migration settled as a
    **clean break**: remove `shininess`/`specular_strength` and update
    the committed scene and the test files referencing them, since the
    engine is pre-1.0 with no external consumers and carrying both
    parameter sets is the exact "two competing gloss scales" the remap
    was deferred to avoid. **Correction for slices 3 and 4:** this entry
    and both PBR specs named `scene.json` as the committed scene. It is
    not — `.gitignore` lists it; it is a local export. The committed
    scene is `templates/default/scenes/main.json`, and it holds two
    lights, not one.
    (3) **Texture maps** (metallic/roughness/normal) — needs further
    texture bindings plus an asset-pipeline answer. (4) **Image-based
    lighting** — the largest; the ambient term stays a flat `0.1` until
    it lands. Explicitly outside the decomposition: multi-light
    accumulation (orthogonal — a change to light gathering, not the
    BRDF) and per-light specular colour (already a deliberate omission).
    Each of 2–4 gets its own spec when its turn comes.
  - ~~**Shading-data UBO migration**~~ — **SHIPPED 2026-09-27.**
    Lighting moved into the codebase's first uniform buffer (descriptor
    set 2, one per frame-in-flight) and shading from object space to
    world space; per-draw push constants went 256 → 208 bytes. See
    `docs/project/next-steps.md` for the full shipment entry, including
    the sharpened boundary on the "identical output" claim (exact for
    scale 1; a uniform scale `s != 1` shifts point/spot attenuation
    brightness by `s`, since the old object-space distance input was
    `world_distance / s` — the new behavior is the correct one).
  - ~~**A multi-scatter / energy-compensation term for the GGX specular.**~~
    — **SHIPPED 2026-10-06.** Single-scatter GGX loses energy at high
    roughness — light that would have bounced between microfacets is simply
    dropped. **The effect is on METALS, not dielectrics**, which the original
    wording here got wrong and the slice propagated until the magnitudes
    were computed: the loss scales with the specular weight, so a dielectric
    (`F0 = 0.04`, diffuse lobe carrying 96% of the response) changes by under
    2%, while a conductor (`F0 = base_color`) loses a great deal.
    Deferred 2026-10-04 during the metallic/roughness design as a refinement
    on top of a working BRDF, with the note that **IBL sub-slice 4c's
    furnace test is what will measure it**, and that pinning the measured
    shortfall rather than widening a tolerance would leave the number to
    beat written down. That worked exactly as intended, and is the reason
    this slice was cheap to scope: the target was already in CI.
    **Fixed** with the Fdez-Agüera analytic fit (`energy_compensate` in
    `app/asset/ibl_bake.cpp`, mirrored in `cube.frag`), applied on top of
    the BRDF LUT rather than baked into it — the correction depends on the
    per-material `F0` while the table is indexed by `(NdotV, roughness)`
    alone, so baking it is not merely undesirable but impossible.
    **Measured: 0.786 → 1.000000 at roughness 0.508, 0.3085 → 1.000000 at
    roughness 1**, exact at every one of the 4096 LUT texels. The
    single-scatter baseline case was **kept, not rewritten**, because the
    LUT is unchanged — so the before and after both stand in the suite.
    **The finding worth carrying forward is about the test, not the fix.**
    At `F0 = 1` the compensated total cancels to `Ess + (1 − Ess)`
    algebraically, so the furnace returns exactly 1.0 for *any* value of
    `Favg` — and dropping to a dielectric does not help either, because
    `kD` is defined as the energy residual, making the three terms sum to 1
    for any `Favg` at albedo 1. **The test this slice existed to satisfy was
    structurally blind to the one new constant the slice introduced.**
    Covered by asking for `FmsEms` in isolation (`diffuse_color = 0`,
    `f0 = 0.04`) against independently computed references, where a bare
    `Favg = 1` is 15× to 36× out. Predicted while scoping rather than found
    in review, which is the only new thing about the sixth instance of this
    pattern. See
    `docs/superpowers/specs/2026-10-06-render-multi-scatter-energy-compensation-design.md`.
  - **Multi-scatter energy compensation for the DIRECT lighting path.**
    `cube.frag`'s `brdf()` loses the same energy at high roughness that the
    IBL path does, and for the same reason — single-scatter GGX drops the
    inter-microfacet bounces. Deferred 2026-10-06 while scoping the
    multi-scatter slice, scoped deliberately to the IBL path only, and the
    reason is the testability argument this whole arc is built on: **the
    furnace test cannot measure the direct path.** That test works by
    feeding a uniform environment through the C++ convolution integrals,
    which is why it runs in CI with no GPU; `brdf()` is shader code, so a
    compensated direct path would ship with nothing but a token-search
    contract test behind it — exactly the "wired, not correct" limitation
    PBR slice 2 recorded as its honest limit and 4c existed to fix.
    Taking it would mean re-opening that question, not just adding a term.
    **Depends on** either a C++ mirror of `brdf()` that the tests can
    integrate directly (the same move `ibl_bake` made for the IBL
    integrals), or GPU-capable CI. Worth noting the error is least visible
    here: direct highlights are small and bright, where the eye forgives a
    few percent, while the ambient term covers whole surfaces.
  - **Anisotropic roughness.** One scalar `roughness` assumes isotropic
    microfacets, so brushed metal and hair cannot be represented.
    Deferred 2026-10-04: anisotropy needs a per-vertex tangent frame,
    which the vertex format does not carry — so this is a mesh-format
    change before it is a shading change.
  - **Material layers: clear-coat, sheen, transmission.** The
    metallic/roughness workflow is the base model; each layer is its own
    feature with its own parameters and its own BRDF lobe. Deferred
    2026-10-04 as out of scope for standing up the base.
  - **An offscreen HDR render target with a fullscreen resolve pass.**
    The correct-compositing form of tone mapping, and the prerequisite
    for bloom. Deferred 2026-10-04 during the tone-mapping design:
    nothing in the renderer blends additively today — every pipeline in
    `vulkan_render_device.cpp` is either `SRC_ALPHA` (eight of nine, with
    `VK_BLEND_FACTOR_ONE` only as `srcAlphaBlendFactor`) or, for the opaque
    mesh pipeline that actually renders `cube.frag`, `blendEnable = VK_FALSE` — so per-shader tone
    mapping lerps between mapped values, which is a defensible
    approximation rather than an error. The HDR target would cost a
    render target, a render pass, a pipeline, and a decision about where
    UI draws relative to the resolve, for little gain. **The trigger is
    specific, not vague: the moment any additive pipeline is added, the
    reasoning above is invalidated and this becomes necessary** —
    `tonemap(a) + tonemap(b) != tonemap(a + b)`. Related and deliberate:
    the four unlit scene shaders (`particle`, `ribbon`, `decal`,
    `world_text`) are *not* tone mapped, because they cannot exceed 1.0
    and every reasonable curve maps 1.0 to about 0.8 — mapping them
    would darken authored colour for nothing. A test pins that, so a
    later "consistency" edit has to argue with it.
  - **Bloom.** Depends on the HDR render target above; there is nowhere
    to read bright pixels back from until that exists. Deferred
    2026-10-04.
  - ~~**Exposure control.**~~ — **SHIPPED 2026-10-07.** Tone mapping shipped
    with a hard-coded curve. Deferred 2026-10-04 because the knob's home was
    undecided (a `Camera` member? the lighting UBO? a push constant?) for no
    current caller. **Decided: a field on `Camera`** — exposure is a camera
    property physically and in every DCC tool, so it serialises with the
    scene, is per-camera, and reaches the inspector for free. Note the
    backlog's own claim that it was "one `float` through the existing
    lighting UBO (which already carries `camera_position`)" was **wrong
    about the free slot**: slice 4c took `camera_position.w` for the IBL
    flag, with an explicit warning in the backend that a future field
    claiming it would silently disable IBL. The UBO grew by a `vec4`
    instead (192 -> 208), appended at the END because the size assertion
    checks total size and not field offsets. See
    `docs/superpowers/specs/2026-10-07-render-exposure-control-design.md`.
  - **`set_component` replaces a whole component rather than merging fields, so a
    partial table silently resets every field it omits.**
    `engine/reflection/component_reflection.hpp` does `T c{}; from_value(value, c);
    *existing = std::move(c);` on the setter path (the agent API and editor
    "set component" route, distinct from the scene loader). So setting `Camera` with
    `{"fov_y_radians": 1.2}` resets `exposure` back to 1.0, discarding an authored 0.4
    — and the same applies to every component and every field. **Pre-existing and
    longstanding**, not introduced by the exposure slice; raised by that slice's branch
    review, and recorded 2026-10-07 because exposure is exactly the kind of field a
    user tunes and then loses this way. Note the *loader* path is correct and different:
    `ReadVisitor::member` no-ops on an absent key over an already-default-constructed
    object, which is why a pre-slice scene JSON loads `exposure` as 1.0 rather than 0.
    **The decision to make** is whether the setter should merge (read the existing
    component, overwrite only the named fields) or keep replace semantics and document
    them loudly — merging is almost certainly what callers expect, but it changes the
    meaning of every existing `set_component` call.
  - **Only one camera is ever active: `RenderSystem` resolves "first found".**
    `find_active_camera` returns on the first `Camera` that also has a
    `WorldTransform`, so a scene with two cameras renders through whichever the
    component iteration reaches first — with no way to choose, and no concept of
    a render target per camera. Recorded 2026-10-07: the exposure slice's spec
    listed "per-camera exposure actually varying between two cameras in one
    scene" as a Non-goal bounded by this limitation, and the branch review
    pointed out it was the one Non-goal with no backlog entry of its own (the
    analogous `first found` notes exist only for lights). **What it blocks:**
    split-screen, picture-in-picture, a separate editor viewport camera, and
    any per-camera post-processing — exposure included, which now serialises
    per-camera but can only ever take effect for one of them. **Depends on**
    deciding what multiple cameras even mean here: a priority/active flag is
    the cheap version, render-target-per-camera is the real one.
  - **Exposure on the four unlit scene shaders** (`particle`, `ribbon`,
    `decal`, `world_text`). Deferred 2026-10-07 while designing exposure
    control, deliberately and for the same reason slice 1 gave for not tone
    mapping them: they emit authored colour that cannot exceed 1.0, and
    scaling it changes what the author asked for. **The consequence is real
    and worth stating:** a bright scene exposed down makes particles read
    relatively *brighter* against it, because the scene moved and they did
    not. Whether that is wrong depends on whether these shaders are
    understood as scene elements (which a camera's sensor would scale) or as
    overlays (which it would not) — and that question is genuinely open,
    since `world_text` is plausibly UI while `particle` and `ribbon` are
    plainly in-world. **Depends on** deciding that, not on any machinery.
  - **EV-stop authoring units for exposure, and a UI slider.** Exposure
    ships as a linear multiplier with 1.0 neutral. EV stops
    (`color * pow(2, ev)`) are the photographer's unit and are the right
    thing behind a slider, but they put a `pow` in two fragment shaders and
    make the neutral value 0.0, which reads as "no exposure" rather than
    "unchanged" to anyone inspecting the component. Deferred 2026-10-07:
    the conversion belongs in the UI affordance, which does not exist yet.
  - **Auto-exposure / eye adaptation.** Deferred 2026-10-07: needs a
    luminance reduction over the framebuffer, so it **depends on the
    offscreen HDR render target** that is still deferred above. Manual
    exposure is the prerequisite and now exists.
  - **Move `view_proj` per-frame and drop `mvp` from push constants** —
    a further per-draw reduction available on top of the UBO migration
    (the vertex shader would compute `view_proj * model` instead of
    receiving a precomputed `mvp`), taking the per-draw payload from
    ~208 bytes to ~144. Deliberately not done in the migration slice:
    that slice's job is the *lighting* data, and the 48 bytes of
    headroom it restores already covers everything queued behind it.
    Worth revisiting only if the per-draw payload comes under pressure
    again.
  - **Real multi-light support** — multiple lights of each type,
    accumulated rather than "first found." Distinct from the UBO
    migration above (which is only about *where* the existing
    single-light-per-type data lives, not how many lights there are);
    the migration is a prerequisite that makes this tractable, but does
    not deliver it. Spot lights inherit the same one-light-per-type
    limitation directional/point already had.

## Project templates

- **3rd-person adventure template content** — needs a new `Health`
  component, character controller, basic combat/AI scripting. Real design
  work, explicitly tracked as deferred.
- Template parameterization (setup wizard, prompted values) beyond
  verbatim copy.
- ~~Remove now-unused `types` param from `create_project`~~ — **SHIPPED**
  2026-09-20, ~40 call sites updated.

## Editor / Studio

- **The editor should start STOPPED, not playing.** `PlayState`'s default is
  `playing_ = true` (`studio/play_state.hpp:27`), so `mosaic editor` begins
  simulating the moment it opens. **Requested 2026-10-10 after the viewport
  pointer-input acceptance pass**, where the default surfaced as the first thing
  a user meets: because viewport clicks are routed to the game while playing,
  the default click behaviour is now "drive the game" and click-to-select
  requires pressing Stop first. The product is editor-facing, so the default
  should be authoring, not simulating — the user's call, and the right one.
  **It is a one-token change plus one trap.** The token:
  `playing_ = false`. The test: `tests/unit/studio/play_state_test.cpp:7`
  ("PlayState defaults to playing") inverts, name included.
  **THE TRAP — `mosaic editor --ticks N` would never terminate.**
  `FramePump` exits on `loop_.tick_count() >= *options_.ticks`
  (`app/run/frame_pump.hpp:87`), and `RuntimeLoop::advance` returns early while
  stopped (`engine/runtime/runtime_loop.hpp:29`) so `tick_count_` never
  increments. A stopped editor therefore hangs forever instead of exiting after
  N ticks. **CI is unaffected** — its smoke test runs `mosaic run`, not `mosaic
  editor` — but `--ticks` is exactly how the editor gets verified in a sandbox
  that cannot screenshot, which is how every editor change this month was
  checked. Needs a decision, not just a fix: either `--ticks` counts *frames*
  in the editor path, or `--ticks` implies play, or the editor gains a separate
  frame limit. Picking one is the actual work here.
  **Consequences that are CORRECT and must not be "fixed" on sight:** the demo
  scene sits still until Play (physics and scripts do not step — that is what
  stopped means); the Blender live-link push is gated on `is_playing()`
  (`studio/studio_app.cpp:246`) so it stays quiet until Play; the Step button
  becomes enabled at startup, which is right. Transform propagation already runs
  every frame regardless of play state, so Inspector edits still appear
  immediately — the editor will not look frozen.

- **Drag-off-then-release is treated as a click, not a cancel.** When the cursor
  leaves the viewport mid-press, `ViewportPanel::pointer_state` reports
  `down = false`, which `UiSystem` reads as a RELEASE — and it then hit-tests the
  **clamped** position. For any `UiButton` whose rect touches a panel edge, the
  clamped position is still inside the button, so the click fires. Every other UI
  toolkit treats drag-off-then-release as a cancel. Found in review 2026-10-07
  during the viewport pointer-input slice; **not reachable in the shipped demo**,
  whose HUD button sits at `pos={0.02,0.02}` and so does not touch an edge — which
  is also why manual acceptance will not find it. Deferred rather than fixed in that
  slice because the clean fix is an **engine-tier change**: `PointerState` needs a
  notion of "the pointer left the surface" so `UiSystem` can cancel rather than
  release, and that changes a type with existing consumers. The cheap alternative —
  reporting out-of-range coordinates when not hovered — would break the clamp
  contract that stops a `UiButton` matching outside itself. **Depends on** the same
  `PointerState` widening as wheel input, so the two should be taken together.
- **Clicking the viewport's scene image can drag the Viewport panel.**
  `ImGui::Image` calls `ItemAdd(bb, 0)` with id 0, so `HoveredId` stays 0 and ImGui
  treats a press there as a click in "empty space", starting a window move
  (`imgui.cpp:5048-5057` sets `ActiveId = window->MoveId`). Inert while the panel is
  docked; on a FLOATED Viewport panel a play-mode click-and-drag — an ordinary game
  input — drags the panel under the cursor while the game also receives the press.
  Pre-existing in that the item has always been id-0, but it only becomes a
  user-facing gesture now that press-and-hold means something. Recorded 2026-10-07.
  Fix is one of `ImGui::InvisibleButton` over the image or
  `ImGuiWindowFlags_NoMove` on the panel; the choice wants a look at the window,
  since `NoMove` also stops deliberate repositioning.
- **Keyboard input to the scene from the editor** — the same shape of gap as
  pointer input, different surface. `setup_runtime` takes an `input_provider`
  (`std::function<std::vector<std::string>()>`) that `app/main.cpp` fills from
  GLFW and `app/run/editor.cpp` leaves empty, so a script reading held keys sees
  nothing in the editor. Deferred 2026-10-07 while designing viewport pointer
  input, and **deliberately not bundled with it**: pointer input needed a
  behaviour decision (edit mode versus play mode) and a panel-relative
  coordinate transform, while keyboard needs neither — keys are not positional,
  so there is no panel to be relative to and no ImGui-capture question beyond
  "is a text field focused". It is close to a one-line pass-through, which is
  exactly why bundling it would have hidden it inside a slice whose risk lives
  somewhere else. **Depends on** nothing; it should inherit the same play-mode
  gate for consistency once that exists.
- **Right-click, middle-click, scroll and multi-touch in `PointerState`** —
  `engine/ui/pointer.hpp` carries a single `down` for the primary button.
  Deferred 2026-10-07 during the viewport pointer-input design: widening it is
  a change to an engine-tier type with existing consumers (`UiSystem`,
  `ClickNudgeSystem`, and anything a plugin has bound), so it wants its own
  slice and a decision about whether buttons become a bitmask or named fields.
- **An editor fly-camera independent of the scene camera** — deferred
  2026-10-07, and the reason is a property worth keeping on purpose: the editor
  currently renders through the scene's *active* camera, so "where the user
  clicked" and "what the game sees" are the same ray **by construction**, which
  is what makes viewport picking and the play-mode pointer agree without any
  reconciliation. A fly-camera breaks that and would force every
  viewport-to-scene transform to carry two cameras. Worth doing eventually;
  worth knowing it has a cost beyond the camera controls.

- ~~**The editor forwards no pointer input to the scene, so nothing in it
  can be clicked**~~ — **SHIPPED 2026-10-07**, play-mode only. `ViewportPanel`
  records the raw ImGui facts and computes the `PointerState`; `editor.cpp`
  hands the same provider to `UiSystem` (through `setup_runtime`'s `pointer`
  parameter, which it had been leaving defaulted) and to `ClickNudgeSystem`
  (through `register_windowed_systems`, which only `main.cpp` had ever
  called). **Three things the original analysis below did not anticipate.**
  (1) The panel-relative coordinate transform *already existed* in
  `draw_viewport`, for edit-mode click-to-select — which is what made
  play-mode-only cheap, and which now has to be gated on `!is_playing()` or
  one click both drives the pointer and moves the selection. That gate is a
  behaviour change to shipped code: clicking the viewport no longer selects
  while playing. (2) `ClickNudgeSystem` fires on a press **edge**, so gating
  `down` on play mode *synthesises a click* — hold the button in edit mode,
  press Play, and `down` flips false→true with no physical press. The panel
  latches presses that began while ineligible and stays suppressed until
  release. (3) The `aspect` provider must be the viewport **panel's**, not the
  window's; the scene renders into a panel-sized offscreen target, so a
  window-derived aspect diverges the nudge ray from the clicked image.
  Original analysis follows.
- **The editor forwards no pointer input to the scene, so nothing in it
  can be clicked** — found 2026-10-06 while visually verifying IBL in the
  editor. `app/main.cpp:301-314` builds a `glfw_pointer` lambda (cursor
  position normalised against the window, plus the left button) and hands
  it to `setup_runtime`; `app/run/editor.cpp:159-160` stops its own
  `setup_runtime` call at the audio backend, so the `pointer` parameter
  takes its empty default. `UiSystem` and `ClickNudgeSystem` therefore
  receive nothing, and UI buttons, click-driven scripts and any
  pointer-reading gameplay cannot be exercised in the editor at all --
  only by `mosaic run --window`, which has no camera or object
  manipulation. The two halves of testing a scene are in different
  programs.

  **Why this is not the one-line fix it looks like.** The editor renders
  the scene into an *offscreen viewport texture* that ImGui composites
  into a panel, so window-relative cursor coordinates are simply wrong:
  they must be relative to that panel's rect, valid only while the cursor
  is over it, and suppressed entirely while ImGui has captured the mouse
  (dragging a gizmo, typing in a field). `main.cpp:299` also records that
  `UiSystem` and `ClickNudgeSystem` **must agree about the pointer within
  a tick, or the UI-claim arbitration breaks** -- so the editor needs one
  shared provider too, not two call sites that happen to compute
  something similar.

  There is a second design question underneath: whether clicking in the
  viewport should drive the *game's* pointer at all while the editor is
  in edit mode, or only in play mode -- otherwise selecting an object
  would also press whatever UI button is under the cursor. That is a
  behaviour decision, not an implementation detail.

  Depends on nothing technically; it needs the play-mode-versus-edit-mode
  answer first.

- ~~**In-editor "File → Reload"**~~ — **SHIPPED** 2026-09-20. Turned out
  to need two new primitives that didn't exist before: `World::clear()`
  (synchronous, no-system-stepping entity wipe -- `destroy_entity()`
  alone is deferred to the next `tick()`) and `CommandStack::clear()`.
  `reload_scene_from_disk()` (`app/run/scene_load.hpp`) validates via a
  throwaway scratch `World` before touching the live one. A real bug
  (confirmation popup's `OpenPopup`/`BeginPopupModal` in mismatched
  ImGui window contexts -- a well-documented pitfall) was caught by
  review and fixed.
- ~~**Studio has zero persisted preferences**~~ — window size/panel
  layout **SHIPPED** 2026-09-20 (`app/studio_prefs.hpp`,
  `~/.config/mosaic/studio.toml` + `studio_layout.ini`); **theme
  SHIPPED** 2026-09-21 (PR #196) -- a `UiTheme` enum on
  `RenderDevice` (Dark/Light/Classic, ImGui's own built-ins), a
  live-switching "Theme" menu, and persistence alongside window
  size; **recent-projects SHIPPED** 2026-09-21 (PR #197) -- a
  persisted, capped, deduped `recent_projects` list, a dismissible
  startup picker window, and a full-process relaunch (`readlink`
  + `execl`) to reopen a chosen project (no in-process
  project-switching mechanism exists, correctly left as its own
  future design rather than half-built here). **All three slices of
  this item are now shipped.**
- ~~**Plugin discovery doesn't follow the project dir**, only CWD~~ —
  **SHIPPED** 2026-09-20. `resolve_plugins_dir()`
  (`app/project/project_io.hpp`) derives `<project_dir>/plugins` from
  the CLI's positional argument; `StudioApp` uses it instead of a bare
  `"plugins"` literal. Verified via the real editor binary launched
  from an unrelated CWD. Scoped to Studio's `PluginHost` specifically
  -- `import_plugins` (used by `mosaic run`) already resolved
  correctly and needed no change.

## Scripting

- Same-frame writes / per-script inline override on threaded scripts
  (deliberately excluded — GIL/race reasons, likely permanent).
- Entity lifecycle (`create_entity`/`destroy_entity`) from a threaded
  script — unsupported, documented limitation.

## Physics pillar (gaps found 2026-09-29 while trying to demo specular)

Both were discovered while trying to get the demo sphere rolling so a
specular highlight could be seen sweeping across it. Neither is a bug in
anything shipped; they are capabilities that were never built.

- **No way to apply an impulse or set a velocity.** `PhysicsBackend`'s
  only write path into the simulation is `set_body_transform`, which
  teleports a body *and resets its velocity*, and `BodyDef` has no
  initial-velocity field. So a body can be positioned but never pushed —
  nothing in the engine can make an object move under its own momentum.
  This blocks click-to-nudge, any gameplay verb (throwing, knockback,
  jumping), and even a scripted "roll this ball" one-liner. The real
  design question inside it: an impulse applied **at a point** induces
  torque and makes a sphere *roll*, while a center-of-mass impulse makes
  it *slide* — the demo needs the former, so the API must take a world
  point, not just a direction. ~~**SHIPPED 2026-09-29**~~ --
  `PhysicsBackend::apply_impulse(BodyHandle, impulse, world_point)` plus an
  entity-level `PhysicsSystem::apply_impulse` and a `ClickNudgeSystem` that
  turns a click into an impulse along the camera ray. See
  `docs/project/next-steps.md` for the full entry, including the Critical
  dead-code bug it shipped past a fully green suite.
- **The Box3D backend's end-event shape-validity filter became load-bearing on
  2026-10-02 and nothing but a comment records it.** Found in review of the
  body-destruction slice. `b3DestroyBody` pushes a `b3ContactEndTouchEvent` for
  each of the destroyed body's shapes (box3d `src/contact.c`), and the event
  buffer is swapped at the end of `b3World_Step` -- so a body destroyed by the
  reap at step 0 produces an end event that `step()` reads after step 3 of the
  same tick. The `if (!b3Shape_IsValid(sa) || !b3Shape_IsValid(sb)) continue;`
  branch in `box3d_backend.cpp` drops it. Before this slice nothing could
  destroy a body, so that branch was unreachable and its comment speculative.
  It is now the first line of defence, and removing it makes the
  "emits no ContactEvent naming a reaped entity" test crash on
  `b3Shape_GetBody` over a freed shape. Deferred 2026-10-02: the comment now
  says all of this, which is enough to stop someone deleting it as dead code,
  but there is no test that fails if ONLY that filter is removed and the
  handle_to_entity_ erase is kept -- the crash masks the cleaner assertion. A
  fake backend that synthesises an end event for a just-reaped handle would
  separate the two guards properly; worth doing if either is ever changed.

- **Nothing enforces the GIL rule the scripting module now depends on.**
  Recorded 2026-10-03 with the GIL fix. The rule is "no thread owns the GIL
  implicitly; every entry into CPython acquires", and it is held together by
  guards a human has to remember to add: a new `ScriptEngine` entry point, a
  new `py::` member (which must also be emptied in the guarded destructor), a
  new static `py::object` (which must leak), or a new direct pybind11 caller
  outside the engines will all compile and then abort or crash at runtime.
  The two new cases in `script_gil_test.cpp` catch the worker-thread shape,
  but nothing catches a missed guard on a specific method. Deferred
  2026-10-03: the real answer is probably to stop exposing raw `py::` types
  across the module boundary at all, so there is only one place that can
  touch CPython — which is a bigger refactor than the bug warranted. Until
  then the destructor comment and this entry are the enforcement.

  Two specifics worth naming, both from the 2026-10-03 review. **`value_to_py`
  (`value_marshal.hpp`) is the single weakest point** in the otherwise
  self-enforcing argument: every other unguarded public function takes an existing
  `py::dict&`/`py::handle`, so a caller cannot reach it without already holding the
  GIL, but `value_to_py(const Value&) -> py::object` *mints* a Python object from
  non-Python input, so a future caller in `app/` or `studio/` could call it with no
  GIL and the returned object's destructor would decref unguarded too. And **the
  main thread's `tstate->gilstate_counter` immediately after init was not verified**
  from source (CPython's `pystate.c` is not vendored). If it is >=1, main-thread
  guards nest harmlessly, which is the intended and almost certainly actual
  behaviour. If it is 0, the first main-thread guard's release would tear down and
  recreate the main thread state on every call -- quiet per-call churn plus a lost
  main-thread `sys` thread dict, not a crash. The one-line check is to print
  `PyThreadState_Get()->gilstate_counter` right after `initialize_interpreter`
  returns; worth doing if unexplained main-thread Python state loss ever appears.
  **RESOLVED 2026-10-04** by the thread-dict probe described in the `PyThreadState`
  churn entry below: the main thread's thread dict survives across an
  acquire/release pair, which it could not if the counter were 0, so the quiet-churn
  branch does not occur.
- **The threaded script path creates and destroys a `PyThreadState` per engine call.**
  **Investigated 2026-10-04; the cheap fix was tried and PROVABLY DOES NOT WORK.** Read this
  before attempting it again.

  *Confirmed real, with a falsifiable test.* The observable is the thread **dict**, not the
  `PyThreadState` pointer: `PyThreadState_Clear` clears the dict, so a value written under one
  `gil_scoped_acquire` and read back under the next survives if and only if no teardown
  happened in between. On a worker thread it does **not** survive — the churn is real.
  (Pointer identity was tried first and is **unsound**: the freed state is re-allocated at the
  same address, so that version of the test passed on the churning build. Do not use it.)

  *`PYBIND11_SIMPLE_GIL_MANAGEMENT` does not fix it.* The idea was that
  `PyGILState_Ensure`/`Release` is cached per thread by CPython, so no interface change would
  be needed — one `target_compile_definitions(pybind11_headers INTERFACE ...)` line. Tried,
  with the macro verified present in the test translation unit by an `#error` probe: **the
  test stayed red.** The caching only applies to *nested* `Ensure` calls; the outermost
  `Release` on a thread still tears the state down, under both pybind11 implementations. The
  premise that this was a one-line fix was simply wrong. (Note for whoever retries: the real
  target is `pybind11_headers`; `pybind11::headers` is an ALIAS and
  `target_compile_definitions` rejects it.)

  *What does work, measured.* Nesting. Holding one outer `gil_scoped_acquire` for the duration
  and letting the inner acquires nest makes the probe survive (verified directly). So the
  remedy is the one the review originally proposed: a guard spanning `execute(job)`, which
  makes every inner acquire a counter bump. That needs a **neutral hook on `ScriptEngine`** —
  an opaque RAII batch token (`virtual std::unique_ptr<BatchScope> begin_batch()` defaulting to
  `nullptr`) — because `engine/script/script_worker.cpp` is deliberately pybind11-free. One
  wrinkle to design around: the GIL is global, not per-engine, so taking the token from one
  arbitrary engine to cover all of them reads oddly and wants a better home than
  `ScriptEngine` instances. An alternative worth weighing is a one-time "attach this thread to
  the script runtime" hook that establishes a persistent thread state at worker start (Ensure
  then `PyEval_SaveThread`, keeping the state alive while releasing the GIL), which fixes the
  churn without bracketing every batch.

  *Bonus result, already banked.* The same probe **resolves the `gilstate_counter` question**
  recorded above from #215's review: the main thread's dict survives across acquire/release,
  so its counter is >=1 at init and the quiet-churn branch the reviewer could not rule out
  does not occur. That uncertainty can be considered closed.

  Original entry follows.
  Found 2026-10-03 in review of the GIL fix. `ScriptWorker::execute` makes three
  independent guarded calls per scripted entity per job (`bind_context`,
  `set_global("dt")`, `run_string`), and on a worker thread each guard's release drops
  the counter to zero, so `PyThreadState_Clear` + `PyThreadState_DeleteCurrent` run and
  the next acquire calls `PyThreadState_New`. At 100 scripted entities and 60 Hz under
  `--script-thread` that is roughly 18,000 thread-state create/destroy pairs a second --
  precisely the case pybind11's own `gil.h` comment warns about. Correctness is
  unaffected and the main-thread path does not churn. Deferred 2026-10-03 because the
  cheap fix is unavailable where it belongs: nesting is free, so one guard spanning
  `execute(job)` would make every inner acquire a counter bump -- but
  `engine/script/script_worker.cpp` is deliberately pybind11-free, so it needs a neutral
  hook on `ScriptEngine` (an opaque scoped batch token), and adding an interface method
  wants its own design rather than riding along with a crash fix.
- **`app/run/editor.cpp`'s shutdown-safety comment is slightly stronger than the code
  supports.** Pre-existing, noted 2026-10-03 in review. It argues the engine destructors
  are safe because they "only drop their `py::dict`" and never dereference the bound
  `world`/`reflection` -- but dropping the dict can run `__del__` on script-defined
  objects, and those can call the installed `get_component`/`set_component`/host
  callbacks, which *do* dereference `world_`/`context_`/`host_`. Unchanged by the GIL
  work (the dict was always destroyed there, and now at least the GIL is correctly held
  while it happens). Deferred: the fix is either to clear the namespace before unbinding,
  or to say plainly in the comment that teardown order depends on no script holding a
  live object with a `__del__` that touches the world.
- **Handle reuse / a free list for the Box3D backend's `bodies_` vector.**
  Recorded 2026-10-02 as a non-goal of the body-destruction slice. That slice
  tombstones a destroyed slot with `b3_nullBodyId` rather than erasing it,
  because `handle == index + 1` and erasing would shift every later handle. So
  the vector only ever grows: a session that spawns and destroys many bodies
  accumulates dead slots even though the Box3D bodies themselves are freed.
  Strictly better than leaking the bodies, but a leak of a different size
  rather than its absence. Deferred 2026-10-02: a free list would reuse slots
  but reintroduces the ABA problem that the generational `EntityID` solves on
  the ECS side -- a recycled handle could be mistaken for the body that
  previously held it -- so it wants a generational `BodyHandle`, which is an
  interface change. No measured pressure yet.
- **Changing a physics body's shape or type in place.** Recorded 2026-10-02 as
  a non-goal of the body-destruction slice. `BodyDef` is consumed once at
  `create_body` and nothing can alter a live body's shape, collider extents, or
  static/dynamic type afterwards. The body-destruction slice makes
  destroy-and-recreate possible for the first time, but deliberately does not
  expose it. Deferred 2026-10-02: a scene editor changing a collider's extents,
  or gameplay toggling a body between static and dynamic, both want this, and
  both need a decision about whether velocity and accumulated state carry over
  across the recreate. Related: reaping a body when its *collider* component is
  removed while `RigidBody` stays -- the body currently persists with the shape
  it was created with, and arguably should be recreated instead, which makes it
  the same question rather than a separate one.
- ~~**Destroying an entity never destroys its physics body**~~ — **SHIPPED
  2026-10-02.** `PhysicsBackend` gained a pure-virtual `destroy_body`, and
  `PhysicsSystem::update()` gained a reap as step 0 inside the existing lock:
  any entry in `bodies_` whose `RigidBody` has vanished loses its body and its
  three map entries. The predicate is "`RigidBody` is gone" rather than "the
  entity is invalid", which covers `destroy_entity` committing, `World::clear()`
  (which bypasses the deferred queue and runs no systems), and removing
  `RigidBody` from a living entity -- all three with one check. The Box3D side
  was the bulk of it: `handle == index + 1` in an insert-only vector, so a
  destroyed slot is tombstoned with `b3_nullBodyId` and all four handle-taking
  methods now route through one `live_body()` guard, where three of them
  previously checked only the range. Evidence: with the backend
  call dropped but the maps still erased, the call-counting tests fail merely
  because the call is absent while the stacked-boxes test fails
  `1.99934 < 1.49934` -- the upper box still propped up by a ghost. That
  asymmetry is why a positional test was written instead of a call-counting one.
  Original entry follows.

- **Destroying an entity never destroys its physics body.** Found 2026-10-01
  while scoping the ContactEvent lock-ordering fix. Neither `PhysicsSystem` nor
  `PhysicsBackend` has any body-removal path: `create_body` has no counterpart,
  and `bodies_`, `handle_to_entity_` and `synced_pose_` are only ever inserted
  into. So destroying a simulated entity leaks its Box3D body, which keeps
  stepping and keeps generating contacts forever, while the three maps keep
  stale entries that grow without bound over a long session. `update()` happens
  to tolerate it -- step 2 and step 4 both `continue` when `get_component`
  returns null, and step 5 drops contacts whose handles are unmapped -- which is
  exactly why this has stayed invisible. Deferred 2026-10-01: the fix adds a
  pure virtual (`destroy_body`) to `PhysicsBackend`, which every backend must
  then implement, plus a decision about how `PhysicsSystem` learns an entity
  died (poll the ECS each tick for vanished bodies, or an explicit
  entity-destroyed event it subscribes to). That is an interface change and a
  design question, not a patch. Noted now because the ContactEvent fix lets a
  subscriber call back into `PhysicsSystem` for the first time, which makes
  "handler destroys a colliding entity" a reachable shape rather than a
  hypothetical one -- it is still safe today, but it is one step closer.
- **Angular impulse / torque API.** Box3D exposes
  `b3Body_ApplyAngularImpulse`, but the impulse slice deliberately ships
  only the linear-at-a-point call, which already induces spin via torque
  and is what the demo needs. Deferred 2026-09-29: a direct torque verb
  has no caller yet, and adding an unused second way to spin a body
  invites picking the wrong one.
- **Script (Python) bindings for impulses and raycasts.** Scripts can
  read and write Transforms but cannot push a body or query the physics
  world, so "roll this ball when the player presses X" is not yet
  expressible in a scene's own script. Deferred 2026-09-29: it needs its
  own binding-surface design (how a script names an entity, what a
  raycast result looks like in Python) and is a bigger question than the
  C++ primitive it would wrap. Depends on the impulse API existing first.
- ~~**The `[script]` SIGSEGV**~~ — **ROOT-CAUSED AND FIXED 2026-10-03. It was
  never a flake, and the cause was written in our own comment.**
  `ensure_interpreter` noted that after init "the calling (main) thread holds
  the GIL ... no explicit GIL handoff" — so whoever won the `std::call_once`
  race kept the GIL for the process. The threaded script test passed alone
  because the `ScriptWorker` thread won and held it; it died after any other
  engine had been created on the main thread, because the worker then called
  `PyDict_New()` with no GIL. `ctest` never saw it because
  `catch_discover_tests` gives every `TEST_CASE` its own process, so the worker
  was always first — **the gate was green and blind at the same time.**
  Fixed by making the invariant real: no thread owns the GIL implicitly, and
  every entry into CPython acquires. Four things that were not obvious going
  in, each of which cost a failing-gate iteration: `globals_` had to become a
  `std::optional<py::dict>` in both engines, because a `py::dict` *member* is
  built before the constructor body and destroyed after the destructor body,
  leaving no scope in which to guard `PyDict_New`; `restricted_builtins`'
  function-local static `ProxyClasses` had to start leaking, because
  `__cxa_atexit` destroys it with no GIL and `dec_ref` aborts -- and that abort is a
  *debug* symptom only (`PYBIND11_ASSERT_GIL_HELD_INCREF_DECREF` is defined when
  `NDEBUG` is unset), so in a Release build the same path is a silent unguarded
  `Py_XDECREF`: the leak is **more** necessary there, not less; `trace_fn_` had
  to be emptied inside the guarded destructor, since a `py::` member outlives
  the body; and every direct pybind11 user outside the engines — the docs
  generator and four test files that build their own `py::dict` — had to
  acquire too. Evidence: `[script]` in one process is 5/5 clean where it was
  RC=139 every time, the whole binary now runs in a single process (1081
  cases, 31577 assertions) which the crash made impossible, and removing the
  release makes the new tests **deadlock** rather than crash.
  The entry that called this "unexplained" follows for context.

- **The `[script]` SIGSEGV now has a deterministic, one-command reproduction,
  and `ctest` structurally cannot see it.** Found 2026-10-03 while guarding
  `raycast`'s sequence reads. #208's entry recorded this failure mode as
  unexplained and said it "has not reproduced since". It reproduces **3/3 on
  pristine `main`** with no local changes at all:

  ```
  ./build/debug/tests/mosaic_unit_tests "[script]"   # RC=139, every run
  ```

  The failing case is `script_system_test.cpp`'s "threaded ScriptSystem applies
  script writes one tick later", which **passes in isolation** -- so it is the
  in-process ordering of the `[script]` cases that kills it, almost certainly
  embedded-CPython interpreter init/teardown across threads, as #208 guessed.
  Verified it is not caused by anything in flight: reverting a local engine
  change and then the new tests too left it failing identically on untouched
  `main`.

  **Why CI has stayed green through all of this:**
  `catch_discover_tests(mosaic_unit_tests)` registers each `TEST_CASE` as its
  own ctest test, so every case runs in a fresh process and the ordering that
  triggers the crash never occurs. The gate reporting 1079/1079 is therefore
  true and also blind to this. Anyone running the tag directly -- which is the
  normal way to work on scripting -- hits it immediately.

  Deferred 2026-10-03 rather than fixed: the cause is interpreter lifetime
  across threads, which is the same machinery #208 spent nine days in, and it
  deserves its own slice rather than riding along with an unrelated binding
  fix. But it is no longer "cannot reproduce" -- the command above is the
  starting point, and `script_system_test.cpp:195` is the assertion that goes
  first.
- ~~**`raycast()`'s script binding indexes its sequence arguments without a
  length check**~~ — **SHIPPED 2026-10-03.** It now uses the same `to_vec3`
  helper `apply_impulse` has used since #207, so a short sequence or a
  non-numeric element yields the miss dict the call already returns when no
  host callback is set, rather than raising out of pybind11 and aborting the
  tick. The host is deliberately not called at all on malformed input --
  zero-filling the missing components would hand the simulation a ray the
  script never asked for -- which the tests pin with a call counter. On the
  behaviour-change worry recorded below: what goes away is an *undocumented*
  exception. `raycast`'s `py::doc` promised only "hit data as a dictionary" and
  never promised to raise; it now states the miss-on-malformed rule explicitly.
  Each of the three new sections asserts both the miss AND that the script
  keeps running, and all three fail under the mutation that restores unguarded
  indexing. Original entry follows.

- **`raycast()`'s script binding indexes its sequence arguments without a
  length check.** `python_script_engine.cpp`'s `raycast` reads
  `origin[0..2]` and `direction[0..2]` directly, so a script passing a
  two-element list raises out of pybind11 rather than returning a miss,
  killing the tick. Found 2026-09-29 while adding `apply_impulse` (which
  guards its own reads). Deferred: pre-existing, and tightening it would
  change behaviour a script might already depend on, so it wants its own
  change rather than riding along silently.
- ~~**Five small `ScriptWorker` tidy-ups found in review 2026-09-30**~~ —
  **ADDRESSED 2026-10-04, and two of the five were WRONG.** Checked each against
  the code rather than taking the entry at its word:

  **(a) "`host_` is now write-only" — FALSE.** It is read at
  `script_worker.cpp:19`, inside the recording `apply_impulse` lambda
  (`if (!host_.apply_impulse) { return false; }`). There is a live reader; the
  member is not dead. No change made.

  **(d) "`execute()` clears `pending_impulses_` after `std::move`-ing it, a
  no-op" — FALSE.** A moved-from `std::vector` is valid but *unspecified*, so the
  `clear()` guarantees empty instead of relying on an implementation detail. It is
  defensive and correct. No change made.

  **(b) "`has_pending()` has no callers" — true. Removed.** Grep found only its
  declaration and definition; public API with zero consumers and its correctness
  verified by nothing. The gate proved nothing used it.

  **(c) "`dispatch()` clears `result_ready_` unconditionally" — true, and now
  documented with two measurements rather than a vague comment.** The clear is
  deliberate, not vestigial: without it a `collect()` after two dispatches would
  return the STALE result while the newer job was still pending, which is worse
  than losing the older one. Two things were measured and are recorded in the
  header: the drop is reachable only by a race between `dispatch()` and the worker
  dequeuing, which came out **9 passes / 11 failures over 20 runs** — so a test of
  it would be flaky rather than load-bearing; and **removing the clear entirely
  leaves the whole suite green (1081/1081)**, so nothing pins it today. The
  precondition (collect before dispatch) is now stated as the contract.

  **(e)** stands as recorded: a script-spawned Python thread calling
  `apply_impulse` after `on_update` returns would race `execute()`'s tail, and
  `SnapshotContext` has the identical exposure, so it is not a new hazard class.

  Lesson worth keeping: this entry had been carried for five days with two
  incorrect claims in it. A recorded finding is a hypothesis until someone
  re-checks it against the code.
- **`apply_impulse`'s return value is optimistic for non-scripted targets.**
  **DOCUMENTED 2026-10-04** at the binding's `py::doc` rather than changed -- the
  behaviour below is still the right call, it was simply invisible to a script author
  reading the docstring. The binding now says that under `--script-thread` `True` means
  "accepted for replay on the main thread", not "a body was pushed", and that a push
  aimed at anything outside the job snapshot (including a non-existent id) is buffered
  and returns `True`. Original reasoning follows and still stands.
  The worker answers exactly for entities in the job snapshot, but an entity
  absent from it cannot be judged, so the push is buffered and `True` is
  returned; the main-thread replay re-checks `has_body` and drops it if there
  is none. Consequence: a script that latches on the return value when pushing
  some *other* object can latch on a push that is later dropped, and an
  invented entity id now returns `True` rather than `False`. Chosen 2026-09-30
  over the alternative, which was to refuse every unjudgeable push — that made
  a controller script pushing a crate silently do nothing under
  `--script-thread`, which is a far worse failure than an imprecise bool. The
  real fix is to snapshot the physics-relevant components of bodied
  non-scripted entities too, so the worker can answer exactly in every case.
- **Deferred host calls are not a general mechanism.** Only
  `apply_impulse` is buffered across the script-worker boundary; the
  collected result carries a writes channel and an impulses channel as two
  parallel vectors. Deferred 2026-09-30 during the buffering design: if a
  third deferrable verb appears, generalising the channel (a tagged
  deferred-call list) is the better change than adding a third parallel
  vector — but two is not yet a pattern, and generalising now would be
  speculative.
- **Snapshot-served ray geometry.** The physics backend mutex makes a
  worker raycast wait for the main thread's physics update. The only
  approach that removes the dependency entirely is answering rays against
  collider geometry captured in the job snapshot, with no shared state at
  all. Deferred 2026-09-30: it means reimplementing ray-vs-box/sphere/
  capsule outside Box3D and keeping two intersection implementations in
  agreement forever, whose failure mode is silent divergence from the real
  physics world. This is the right answer only if blocking ever shows up
  in a profile.
- ~~**`PhysicsSystem::update()` emits `ContactEvent` synchronously while holding
  `backend_mutex_`**~~ — **SHIPPED 2026-10-01.** `update()` now maps contacts
  into a local vector inside a braced block holding the lock, and emits after
  the block closes. The extent is delimited lexically on purpose: a `unique_lock`
  plus `unlock()` would have been a smaller diff but would leave the locked
  region ending at an unmarked statement, so a later edit adding a call-out above
  it would reintroduce exactly this bug. The pose write-back already preceded
  emission, so handler-visible ordering did not change -- the entry below, and the
  #209 summary it came from, were wrong to claim it would. Evidence: both new
  re-entrancy tests fail on a 3000 ms watchdog with the emit back under the lock
  and pass with it outside; the watchdog runs the tick on a detached thread so the
  failure is a named assertion in seconds rather than a 25-minute ctest stall.
  Original entry follows.

- **`PhysicsSystem::update()` emits `ContactEvent` synchronously while
  holding `backend_mutex_` — a lock-ordering constraint, not just a
  re-entrancy one.** Found 2026-09-30 in review of the mutex slice.
  `EventBus::emit` dispatches immediate subscribers on the same stack, so a
  subscriber runs inside the lock. The narrow risk is a subscriber that calls
  `raycast`/`has_body`/`apply_impulse` and hangs on a plain `std::mutex`;
  today's two subscribers were checked (one logs, one re-emits onto the
  *deferred* bus) and neither does. The wider and already-live consequence is
  that **every lock a synchronous subscriber takes is now ordered *under*
  `backend_mutex_`** — which today includes the global logger mutex, since the
  contact logger calls `log::info` once per contact inside the lock. Nothing
  inverts that ordering right now, but it is an unwritten invariant: a future
  log sink, or any sync subscriber holding a lock that another thread holds
  while waiting on `backend_mutex_`, deadlocks in a way that looks nothing
  like the re-entrancy case. There is a performance half to the same point: a
  worker `raycast()` can wait behind per-contact logging and the `Table`
  construction in the contact-to-`ScriptEvent` bridge — work with nothing to
  do with the backend. The spec accepts holding the lock across ECS
  traversal explicitly; it never considered holding it across subscriber
  dispatch. Deferred 2026-09-30: the fix is to collect contacts under the
  lock and emit them after releasing it, which changes when subscribers
  observe a contact relative to the pose write-back — a behaviour decision
  worth its own slice. A recursive mutex would hide the re-entrant hang
  without making the read correct, and would do nothing about the ordering.
- ~~**A spurious watchdog trip in the contact-reentry tests degrades into a
  use-after-free**~~ — **SHIPPED 2026-10-03.** The World and the state the
  handlers write are now heap-owned (`std::shared_ptr<World>` plus a `Probe`
  struct), and `tick_with_watchdog` captures both by value, so a thread that
  outlives the `TEST_CASE` keeps what it touches alive and they leak on a trip --
  self-healingly for a spurious slow-runner trip (the thread finishes late and
  drops its references), but for the rest of the test *process* on a genuine
  deadlock trip, since Catch2 keeps running later cases after a failed
  `REQUIRE`. Both are bounded by process exit, and `backend_mutex_` is
  per-instance, so a leaked parked thread cannot block the next case. **The entry below understated the hazard:** heap-allocating the World
  alone would not have fixed it, because the subscriber lambdas also captured
  `handler_calls`/`hit_the_box`/`pushed`/`box` by reference -- all `TEST_CASE`
  stack locals, reachable from inside the World's own EventBus. The ordering
  that makes the fix correct: `Probe` deliberately does NOT own the World, since
  the EventBus holds a lambda owning the Probe, and a Probe owning the World
  back would close a reference cycle and leak on the *passing* path too; the
  handlers hold World and PhysicsSystem as raw non-owning pointers instead.
  Measured both ways with a 50 ms watchdog against 4,000,000 ticks, so the
  thread is certainly still running when the case unwinds: before, RC=134
  (SIGABRT) and RC=139 (SIGSEGV), printing the assertion and *then* dying on a
  signal; after, **RC=1 five times out of five** with the clean named failure.
  Original entry follows.

- **A spurious watchdog trip in the contact-reentry tests degrades into a
  use-after-free instead of the named failure it is designed to produce.**
  Found 2026-10-01 in review of the contact-emit slice.
  `tests/unit/physics/physics_contact_reentry_test.cpp`'s `tick_with_watchdog`
  runs the tick on a detached thread capturing `&world`, so when the main
  thread times out and `REQUIRE` throws, `world` is destroyed at scope exit
  while that thread may still be using it. In the genuine-deadlock case the
  thread is parked on a mutex that is then destroyed -- UB, though observed to
  unwind cleanly and print the assertion. In the case the 3000 ms margin is
  actually guarding against (a loaded runner, no deadlock), the thread is
  *actively ticking* a destroyed `World`, so the run reports a segfault rather
  than "the tick did not return" -- defeating the whole reason the watchdog was
  chosen over a ctest `TIMEOUT`. Damage is bounded: `catch_discover_tests` gives
  each case its own process, so it cannot take other cases down. Deferred
  2026-10-01: the fix is to heap-allocate the `World` on the watchdog path and
  deliberately leak it when the flag never sets, which turns both failure modes
  into a clean named assertion. Not folded into that slice because it only
  affects a failure path that the four-orders-of-magnitude timing margin makes
  very unlikely, and leaking a `World` on purpose wants its own justification
  in the test's comments rather than arriving as a fix-round edit.
- ~~**No ctest `TIMEOUT` property anywhere in the suite.**~~ — **SHIPPED 2026-10-07**
  as `catch_discover_tests(mosaic_unit_tests PROPERTIES TIMEOUT 60)`. The value is
  measured, not chosen by taste: over a full run the slowest single case is **1.11s**
  (`PluginFileWatcher.poll detects a modified file's mtime`, which waits out filesystem
  mtime granularity), the next tier is 0.40s (the IBL furnace cases baking a 64x64 BRDF
  LUT), and the whole suite is 24s — so 60s is ~54x the slowest case and still ~10x
  clear if CI runs 5x slower than this machine, while one case reaching 60s in a 24s
  suite is unambiguously wrong rather than merely slow. Verified with
  `ctest --show-only=json-v1`: all 1158 discovered tests carry `TIMEOUT 60.0`, rather
  than trusting that the CMake call did what its docs say. It does **not** replace the
  in-test watchdogs, which give a named assertion in seconds and work when running the
  binary directly; it is the backstop for the next hang nobody anticipated. Original
  entry follows.
- ~~**`PhysicsSystem::raycast is callable from another thread while update runs` is
  flaky on CI, and the flakiness is in the test's design.**~~ — **FIXED 2026-10-09**,
  in the same PR that recorded it. The case ran 200 main-thread ticks, set `stop`,
  joined the reader thread, and only *then* asserted `REQUIRE(hits.load() > 0)`.
  Nothing synchronised the two, so on a **2-core** GitHub runner with the main thread
  busy stepping box3d the reader could be starved for the entire run and `hits` stayed
  0 — failing a test whose actual claims (no deadlock, no crash, the ray hits) had all
  been met. Observed failing 2026-10-09 on a **docs-only** PR that changed no code, and
  **not reproducible on a 20-core machine in 35 runs**, including under deliberate CPU
  contention — which is the whole story: the reader always gets a core here and may get
  none there. **The fix removes the dependency on the scheduler rather than widening a
  tolerance:** the main thread now waits (bounded, 5s, uncontended so it completes in
  microseconds) for the reader to land its first hit *before* the tick loop, so
  `hits > 0` is established deterministically; the concurrent phase that follows asserts
  nothing a scheduler can zero out — reaching the join without a deadlock or crash *is*
  the claim; and the ray's geometry is re-asserted afterwards **from the main thread**,
  which cannot be starved. The 5s ceiling exists so a genuine failure reports instead of
  hanging, and its message distinguishes "the reader was never scheduled" from "raycast
  from a non-main thread returns no hit".
- ~~**Nine existing `TEST_CASE` names contain commas, so none of them can be run by
  name.**~~ — **WITHDRAWN 2026-10-09: the premise was wrong, and the count was wrong.**
  There are **89**, not nine (the original count came from grepping four files), and
  **they are not broken.** `CatchAddTests.cmake` escapes commas when it generates each
  `add_test`, which the generated
  `build/debug/tests/mosaic_unit_tests-*_tests.cmake` shows directly:
  `add_test( [==[... (0,0,0,1)]==] ... [==[... (0\,0\,0\,1)]==] )`. So every one of
  them runs correctly under `ctest`, and under a manual invocation that escapes the
  comma, and under `ctest -R`. Verified both ways 2026-10-09.
  **The defect was in my own tooling, not in the test names.** Catch2 treats a comma in
  a name filter as a test-spec separator, so the *unescaped* per-name invocation used by
  mutation-probe scripts matched nothing and exited non-zero — which the scripts read as
  a failing test. That cost a full probe round on the multi-scatter slice and two
  false-red columns on the exposure slice. The fix is to escape the comma (or filter by
  tag) in probe scripts, which is now recorded, and **not** to rename 89 well-written
  test names to work around it. Acting on the original entry would have been pure churn
  against a non-problem.
  **What survives as real:** a leading `--` and an unbalanced `[` in a `TEST_CASE` name
  genuinely break ctest discovery, and those two are worth a guard — see the lint entry
  below.
- ~~**A guard against `TEST_CASE` names that break ctest discovery.**~~ — **SHIPPED
  2026-10-09** as `tools/ci/check_test_case_names.py`, wired into
  `.githooks/pre-commit` and CI alongside the dependency lint, with 12 unit tests of
  its own. It rejects exactly two things, both silent when violated — the suite still
  builds and the damage appears as tests that *do not exist* rather than tests that
  fail: a name beginning with `--`, which ctest parses as a flag, and a name with an
  unbalanced `[`, which breaks `CatchAddTests.cmake`'s tag parsing. **The suite is
  currently clean, so this is a guard and not a cleanup** — it exists because both
  traps have cost this project real time before, and neither announces itself.
  **It deliberately does NOT reject commas,** and that non-rule has its own unit test
  so nobody "completes" the lint by adding one: `CatchAddTests.cmake` escapes commas
  when generating `add_test`, so comma-containing names run fine under ctest.
- **No ctest `TIMEOUT` property anywhere in the suite.** Recorded 2026-10-01 as
  a non-goal of the ContactEvent lock-ordering slice. `tests/CMakeLists.txt`
  calls `catch_discover_tests(mosaic_unit_tests)` with no timeout, so ctest's
  1500s default applies to every case in the suite: any test that hangs costs 25 minutes
  of CI before it fails, and says only "Timeout" when it does. Deferred
  2026-10-01 rather than folded into that slice, which solves its own hang with
  an in-test watchdog instead -- the watchdog gives a named failure in seconds
  and works when running the test binary directly, which a ctest property does
  not. A suite-wide timeout is still worth having as a backstop for the next
  hang nobody anticipated; it needs its own value chosen against the slowest
  legitimate test rather than a number picked to fit one case.
- **Nothing enforces that a system never calls user code while holding an
  internal lock.** Recorded 2026-10-01 as a non-goal of the ContactEvent slice,
  which fixes the single live instance. `PhysicsSystem` is the only system that
  holds a mutex today, so there is currently nothing else to audit -- but the
  rule it now follows ("collect under the lock, call out after releasing it")
  exists only as a comment and a test in one file. Deferred 2026-10-01: a real
  enforcement mechanism means either a lock-order-checking wrapper around the
  mutex or a convention the dependency linter could verify, and inventing either
  before a second system takes a lock would be speculative.
- **The two concurrent physics tests busy-spin their reader thread** with no
  `yield` or `sleep`. Found 2026-09-30 in review. On a 1-vCPU CI runner the
  reader monopolizes a core, so the interleaving profile differs sharply from
  a dev box and the overlap detector's sensitivity becomes
  machine-dependent. Not a correctness issue, and the detector is
  empirically sensitive where it has been measured (it fails 5/5 runs under
  the removed-lock mutation locally). Deferred 2026-09-30 deliberately
  rather than "fixed" by adding a yield: throttling the reader would
  *reduce* the overlap pressure the detector depends on, so the change needs
  a re-run of the mutation probe on CI hardware to show it still fails
  without the lock. Doing that blind would trade a measured detector for an
  unmeasured one.
- **Nothing enforces that the script worker is joined before
  `PhysicsSystem` is destroyed.** Found 2026-09-30 in review. A worker
  thread blocked on `backend_mutex_` while `PhysicsSystem` is being
  destroyed is undefined behaviour: the mutex and `backend_` die with the
  system. It is safe today only by registration order — `ScriptSystem` is
  added before `PhysicsSystem` in `setup_runtime`, and the system vector
  destroys front to back, so `ScriptWorker`'s destructor joins the thread
  first. Entirely pre-existing; the mutex neither introduces nor worsens it.
  Recorded because the new lock *reads* as though lifetime were handled,
  which makes the real invariant easier to break by reordering two
  `add_system` calls. Deferred 2026-09-30: the fix is an explicit
  shutdown ordering or a `PhysicsSystem` that outlives its readers by
  construction, which is a lifetime-design question rather than a patch.
- **Other engine systems are not thread-safe, and nothing enforces that
  future `ScriptHost` hooks are.** The physics boundary is closed because
  it is the one scripts reach. `set_playing` and `request_stop` were
  verified safe (they write an atomic), but that is a property of today's
  implementations rather than a rule anything checks. Deferred 2026-09-30:
  a general answer means deciding what thread-safety contract `ScriptHost`
  imposes on its implementors, which is a design question rather than a
  patch.
- **Reader-writer or lock-free refinement of the physics lock.** A plain
  mutex serializes a worker raycast against the whole physics update.
  Deferred 2026-09-30 as speculative: there is no measured contention, and
  a finer-grained lock is the classic way to turn a correct coarse lock
  into a subtly wrong fine one.
- ~~**`raycast()` still runs off the script-worker thread**~~ — **SHIPPED
  2026-09-30.** Closed with a `mutable std::mutex` on `PhysicsSystem`, taken by
  `update()`, `raycast()`, `apply_impulse()` **and** `has_body()`. The lock
  protects two things, not one: the seven `backend_` call sites, and the
  `bodies_`/`handle_to_entity_` maps that `update()` writes while the other
  three read them — so `has_body()` needs it despite never touching the
  backend, and `apply_impulse()` needs it despite being main-thread-only
  (it runs from `ScriptSystem::update_threaded`'s replay, *outside*
  `update()`). The entry below guessed this needed the same buffering/snapshot
  design as the impulse work; it did not — blocking a worker raycast for the
  duration of one physics update is bounded and was the user's call. Evidence:
  the new concurrent smoke test **segfaults 3/3 runs** without the lock, and a
  dedicated overlap-detecting fake backend **fails 5/5 runs** when the
  `lock_guard` is removed from `raycast()` alone. Original entry follows.

- **`raycast()` still runs off the script-worker thread.** The
  `--script-thread` path binds the same `ScriptHost` into worker-thread
  engines, and while `apply_impulse` is now deliberately left unwired there,
  `raycast` is not: it queries the live physics world from the worker thread
  while the main thread may be inside `PhysicsSystem`'s step. It is a *read*
  rather than a write and predates all of this, which is why it was not
  changed alongside the impulse guard — but it is the same boundary, and a
  read racing a step can still return torn or mid-integration results.
  Deferred 2026-09-30: fixing it properly means the same buffering/snapshot
  design decision the impulse entry above needs, and the two should be solved
  together rather than one at a time.
- **Two thin spots in the new physics-binding tests.** Found 2026-09-30 in
  review, both real but neither load-bearing today. (a) The demo ball-pusher
  test asserts only `fabs(delta) > 0.05`, which would not distinguish the
  intended origin-seeking oscillation from a wrong-sign runaway that rolls the
  sphere off the platform — bounding the sphere's `|x|` would pin the actual
  intent. (b) `make_physics_apply_impulse`'s `world.is_valid(e)` guard is
  unproven: removing it fails nothing, because the only invalid-id test uses
  an id that also fails `has_body`. A test that destroys a bodied entity and
  pushes it in the same tick would make it load-bearing.
- ~~**Script-thread impulses are disabled, not buffered**~~ — **SHIPPED
  2026-09-30.** Impulses are now recorded by `ScriptWorker` against the job
  snapshot and replayed on the main thread through the neutral host callback,
  so the same script works on both paths with the one-tick lag the threaded
  path already has for component writes. The `!script_thread` guard is gone.
  `raycast`'s off-thread read is unchanged and still tracked below. Original
  entry follows for context.

- **Script-thread impulses are disabled, not buffered.** On the
  `--script-thread` path, `on_update` runs on a worker thread while the main
  thread may be inside `PhysicsSystem`'s step, and `apply_impulse` writes
  straight into the live Box3D body -- an unsynchronized write during a step
  is undefined behaviour. Found 2026-09-30 in whole-branch review, once the
  default demo scene began calling `apply_impulse` and made the hazard
  reachable with no user code. Mitigated by NOT wiring the hook on that path,
  so it returns `False` there per `ScriptHost`'s unset-hook contract. That is
  a guard, not a fix: a script that works single-threaded silently does
  nothing under `--script-thread`. The real fix is to buffer impulses the way
  the threaded path already buffers component writes, and apply them on the
  main thread -- which needs a design decision about ordering against the
  physics step. Note `raycast` still runs off-thread on that path; it is a
  read and pre-existing, but it is the same boundary.
- ~~**A dict passed to `apply_impulse` raises instead of returning `False`**~~ —
  **SHIPPED 2026-10-04, for `raycast` too.** `to_vec3` now takes a `py::handle` and
  checks sequence-ness itself, and both bindings take `py::object` instead of
  `py::sequence`. The signature was the whole problem: with `py::sequence` there,
  pybind11's *dispatcher* rejected a non-sequence before the binding body ran, so the
  guard the spec relied on never got a chance. A dict, an int and `None` now all
  return `False`/a miss without aborting the tick.
  **Taken wider than recorded, deliberately:** the entry scoped this to
  `apply_impulse`, but #214 had just made `raycast` degrade for malformations *within*
  a sequence, so fixing only one would have left a dict returning `False` from one
  binding and raising from the other -- a new inconsistency rather than a closed one.
  Also documented at both surfaces, including the one imprecision that stays:
  `apply_impulse` under `--script-thread` returns `True` meaning "accepted for replay",
  not "a body was pushed", since the worker can only judge entities in its own job
  snapshot. That was a deliberate 2026-09-30 choice and remains so -- it was just
  undocumented at the binding. One behaviour is pinned as a two-layer guard rather than
  one: `str` passes `PySequence_Check`, so a string falls through to the element casts
  and fails there, which the tests now cover so the layers stay honest. Mutation:
  restoring `py::sequence` in either signature fails that binding's cases (3 and 4
  sections respectively). Original entry follows.

- **A dict passed to `apply_impulse` raises instead of returning `False`.**
  The spec says a malformed argument returns `False` rather than raising, but
  that only covers malformations *within* a sequence. A non-sequence -- and
  the single most likely wrong argument here is a **dict**, because this same
  scripting surface uses dicts for component values (`tr['position']`) -- is
  rejected by pybind11's dispatcher before the binding's own guard runs, so
  `apply_impulse(e, {'x':1,'y':0,'z':0}, ...)` raises and aborts that
  script's `on_update` for the tick. Found 2026-09-30 in review. Deferred:
  it mirrors `raycast`'s existing behaviour exactly, and tightening it means
  taking `py::object` and hand-rolling the sequence check for both bindings,
  which is its own change.
- **Other physics verbs from scripts** — setting a body's velocity
  directly, teleporting a body, and overlap/shape queries. Deferred
  2026-09-29 during the scripting-bindings design: impulse and raycast are
  the two with a caller today, and each of the others needs its own
  semantics decision (does a script-set velocity survive the next tick's
  Transform sync? does a teleport wake the body?) rather than being a
  matching pass-through.
- **Raycast layer masks and all-hits queries from scripts** — the backend
  exposes closest-hit only, so anything richer starts with a
  `PhysicsBackend` change rather than a binding. Deferred 2026-09-29.
- **A `Vec3` Python class instead of dicts** — nicer ergonomics than
  `{'x':…,'y':…,'z':…}` for hand-written literals, but it would introduce a
  second vector representation alongside the dicts every existing global
  already uses, and the two would have to coexist indefinitely. Deferred
  2026-09-29: worth doing only as a deliberate migration of the whole
  scripting surface, not per-binding.
- **Surfacing scripting-binding failures to the log** — every binding
  returns `False`/`None` on bad input rather than raising, which is right
  for a game loop (a raising binding kills the tick) but gives a script
  author with a typo'd key no feedback at all. Deferred 2026-09-29: the
  fix is a logging/diagnostics decision spanning every global, not just
  the physics ones.
- **Continuous forces, as distinct from instantaneous impulses.** An
  impulse is a one-shot momentum change; sustained thrust, wind, or
  magnetism needs a per-tick force accumulator. Deferred 2026-09-29: no
  caller today, and the two have genuinely different semantics, so
  bolting `apply_force` onto the impulse call would blur them.
- **Drag-to-throw.** Click-and-drag to aim, release to launch with a
  magnitude from the drag vector. Deferred 2026-09-29: it needs pointer
  press/drag/release state tracking that nothing else in the engine wants
  yet, whereas a click nudge needs only the press edge already available.
- **A physics body whose `Transform` is written every tick loses any impulse.**
  `PhysicsSystem::update` step 2 teleports a body whenever its `Transform`
  diverges from the pose the system last wrote, and `set_body_transform` zeroes
  both linear and angular velocity. So if a script, the animation system, or
  client-side replication drives a `RigidBody` entity's `Transform`, clicking
  that object does nothing visible -- the impulse is erased on the next tick.
  Found 2026-09-29 in review; latent, not live, since no demo body is in that
  category today. Deferred: the fix is a real design question (does an authored
  Transform win over the simulation, or does the body become kinematic?) rather
  than a patch.
- **UI click arbitration is `UiButton`-only.** `UiSystem::press_consumed()` is
  true only when a press lands on a `UiButton`, so a click on a decorative
  `UiImage` or `UiText` falls through and nudges whatever world object sits
  behind it. The demo's HUD bar is a button and is correctly claimed, so the
  spec's stated criterion is met. Deferred 2026-09-29 as a recorded limitation:
  generalizing it means deciding which UI element kinds are click-opaque, which
  is a UI-pillar decision rather than a physics one.
- **`ClickNudgeSystem` declares no `ComponentAccess`.** It reads `Camera` and
  `WorldTransform` and causes `Transform` writes through the physics backend,
  but declares nothing -- matching `UiSystem` and `AudioSystem`, and harmless
  today because nothing reads `access()`. Deferred 2026-09-29: when a job-system
  scheduler lands, an empty access set reads as "depends on nothing" and this
  system becomes a candidate to run concurrently with `PhysicsSystem`. The fix
  is two `reads<>()` lines, but it should land with whatever actually consumes
  the access sets, so the declaration and its enforcement are reviewed together.
- ~~**`app/main.cpp`'s system wiring is untestable**~~ — **SHIPPED 2026-09-29.**
  `app/run/windowed_systems.hpp`'s `register_windowed_systems()` now owns the
  conditional registration, and tests assert what is registered under which
  condition. Investigation narrowed the problem first: `setup_runtime` was
  already a testable seam with five test callers registering eleven systems,
  and the dead feature had simply been wired *outside* it. Only conditional
  registration carries this risk — an unconditional one cannot silently
  vanish — so `TransformSystem` stayed where it is. Known limit, stated in the
  header: this makes the registration decision testable but cannot verify the
  caller passes the right condition; `main()` therefore passes the parsed CLI
  flag straight through with no intermediate that can go stale. Original entry
  follows for context.

- **`app/main.cpp`'s system wiring is untestable, and it has already shipped a
  dead feature once.** Found 2026-09-29: `ClickNudgeSystem` was registered
  behind `if (glfw_window != nullptr)` placed ~60 lines *before* the window is
  created, so the guard was always false and clicking did nothing in the real
  app. Every automated check passed — the unit tests construct the system
  directly and bypass `main()` entirely, and the launch check only proves the
  process starts. The gap is structural: `main()` is one long function whose
  wiring decisions no test can observe. The fix is to extract the registration
  decisions into small functions that take their inputs explicitly (e.g.
  `register_windowed_systems(World&, bool windowed, providers...)`) so a test
  can assert *that* a system is registered under the right conditions.
  Deferred 2026-09-29 rather than done inside a fix round, because carving a
  testable seam out of `main()` is a design change that deserves its own
  review. Depends on nothing.
- **`ClickNudgeSystem::camera_position` duplicates `studio::pick_entity`'s
  camera selection.** Both independently implement "first entity with a
  `Camera` and a `WorldTransform`, take the world matrix translation". They
  agree today by construction, since both run in the same tick over the same
  components. Deferred 2026-09-29: the clean fix is for `RaycastHit` to carry
  the ray origin `pick_entity` already computed, which changes a studio API the
  editor's picker also consumes — a separate, reviewable change rather than a
  fix-round edit. If an explicit active-camera policy is ever added and only one
  site is updated, the nudge would push along a ray from the wrong eye while
  picking still used the right one.
- ~~**Static bodies cannot be created rotated**~~ — **SHIPPED 2026-09-29.**
  `BodyDef` gained a `rotation` field, passed through from the entity's
  `Transform` and into `b3BodyDef::rotation` (which already existed and was
  simply never wired). Investigation found this was **two** live bugs, not the
  one recorded: a rotated *static* body kept its Transform rotation while its
  collider stayed axis-aligned, and a rotated *dynamic* body had its authored
  rotation **overwritten with identity by the pose write-back on the first
  tick** — visibly flattening itself the instant physics started. Nothing
  pinned the old behavior (no test used a non-identity rotation on a physics
  body, and no `BodyDef{...}` aggregate-init sites existed), so the fix was a
  clean addition. Original entry follows for context.

- **Static bodies cannot be created rotated.** `BodyDef` carries
  `{type, position, shape, density}` — no rotation — so `create_body`
  always produces an axis-aligned body. A tilted ramp, a slanted wall, or
  any angled static geometry is therefore unrepresentable: the renderer
  draws the entity rotated while the collider stays axis-aligned, and the
  two silently disagree. The only workaround today is to mutate the
  entity's Transform *after* creation so the "external edit" path calls
  `set_body_transform`, which does accept a rotation — a hack that
  depends on a sync heuristic rather than on the API meaning what it
  says. Deferred 2026-09-29: the fix is to add `rotation` to `BodyDef`
  and pass it through `create_body`, which is small but touches the
  backend interface and wants its own tests. Not needed once impulses
  exist, which is why it is not being folded into that slice.

## Tech debt

- ~~**Issue #25 — Component TypeID densification**~~ — **SHIPPED**
  2026-09-20. `storages_` now indexed by a new, purely-internal dense
  slot (`detail::component_storage_slot<T>()`) instead of the shared
  `type_id<T>()` -- `ComponentTypeID` itself deliberately untouched,
  avoiding a reintroduction of the earlier "#9 identity drift" bug.
- ~~**`LiveLinkServer` tests hard-code listen ports, and one flaked on CI**~~ —
  **SHIPPED 2026-09-30.** All eight now call `start(0)` and ask the kernel for a
  free ephemeral port. `start(0)` was already possible; what was missing was any
  way to learn the kernel's choice, so `LiveLinkServer` gained
  `port()` (via `getsockname()`, reset to 0 by `stop()`). Production is
  unchanged: `studio_app` still calls `start(kDefaultLiveLinkPort)` and `port()`
  returns exactly that. The already-bound-port case got *stronger* rather than
  merely portable — it now binds a port it **knows** is taken
  (`server1.port()`) instead of hoping a hard-coded number is free.
  The mutation probe turned up a second defect worth recording: `TestClient`
  ignored `connect()`'s return value, and `send_text()` on a never-connected
  socket raises **SIGPIPE**, killing the whole test binary as a bare exit 141
  with no named assertion — which is exactly how the original port collision
  would have presented on CI. With `connected()` asserted, removing
  `getsockname()` now fails **7 named assertions across 7 cases**. Original
  entry follows.

- **`LiveLinkServer` tests hard-code listen ports, and one flaked on CI.**
  `tests/unit/studio/live_link_server_test.cpp` binds fixed ports (34906,
  34907, ...). On 2026-09-29 the case "LiveLinkServer start() fails on an
  already-bound port" failed a GitHub Actions run (1015/1016) and passed on
  a re-run of the same branch with no code change in that area, which is the
  signature of a lingering socket in `TIME_WAIT` or a port collision on a
  shared runner. Each Catch2 case runs as its own process, so the exposure is
  real. **Do not add a third `-E` exclusion for it** -- the fix is to bind
  port 0 and ask the OS which port it got, so the tests stop competing for
  fixed numbers. Deferred 2026-09-29: small and self-contained, but it is a
  test-infrastructure change unrelated to whatever slice is in flight.
- **`enet_transport_test.cpp` hard-codes six UDP ports too** (34001-34006).
  Found 2026-09-30 while scoping the `LiveLinkServer` ephemeral-port fix, which
  covers the TCP side only. Same failure signature is available to it: a fixed
  port colliding with anything else on a shared CI runner. Not folded into that
  slice because the fix is materially bigger — ENet's `enet_host_create` accepts
  port 0 and records the kernel's choice in `host->address.port`, but Mosaic's
  transport sits behind its own neutral interface, so learning the bound port
  means adding an accessor to that interface and threading it through the
  backend. Deferred 2026-09-30: a one-file test change on the TCP side versus an
  interface change on the UDP side is the difference between two slices, and the
  UDP tests have not actually flaked yet where the LiveLink one has. Depends on
  nothing; pick it up if a net test ever fails on CI.
- ~~**Embedded-Python worker-thread flakiness (ScriptSystem/ScriptWorker)**~~ —
  **ROOT-CAUSED AND FIXED 2026-09-30. It was never a flake.**
  `ScriptWorker::run()` cleared `pending_` when a job was dequeued but set
  `result_ready_` only after `execute()` returned — and `execute()` runs
  *unlocked*, for as long as starting Python and compiling scripts takes. In
  that window both flags were false, so `collect()`'s early-out
  (`if (!pending_ && !result_ready_) return {};`) could not distinguish "job
  executing right now" from "nothing was ever dispatched" and silently
  returned an empty result, dropping that tick's writes. Fixed by adding an
  `executing_` flag covering the window, so `collect()` waits for an in-flight
  job instead of skipping it. Measured: the headline test went from **2/12 to
  12/12**, the other from flaky to 12/12, and the full suite is green with **no
  `-E` exclusions**, which have accordingly been removed from the CI gate.
  **The old entry below understated it as "rare/near-impossible to reproduce
  locally"; it actually failed ~80% of the time locally.** That mischaracterisation
  is why a real race sat behind a CI exclusion for nine days — worth remembering
  the next time something is labelled a flake.
  **One caveat, stated rather than buried:** the original entry recorded TWO
  failure modes, and this fix explains only one. The SIGSEGV under full-suite
  ordering, attributed to embedded-Python interpreter init/teardown across
  threads, is untouched by an `executing_` flag and remains unexplained. It has
  not reproduced since, and is now gated by CI rather than excluded from it --
  but if a segfault returns, this is the thread to pull.
  Original entry follows.

- **Embedded-Python worker-thread flakiness (ScriptSystem/ScriptWorker)**
  — real, currently un-root-caused. Two tests --
  `tests/unit/script/script_system_test.cpp`'s "threaded ScriptSystem
  applies script writes one tick later" (a known SIGSEGV flake,
  previously observed under full-suite local ordering) and
  `tests/unit/script/script_worker_test.cpp`'s "ScriptWorker runs a
  job on its thread and returns the write batch" (a size-mismatch
  failure, not a crash) -- both touch the same embedded-Python +
  worker-thread machinery and both are rare/near-impossible to
  reproduce in local dev runs, but failed on 2 of the first 3 real
  GitHub Actions CI runs (`.github/workflows/ci.yml`, added 2026-09-21)
  -- a materially higher rate than anything seen in this project's
  local-dev history, almost certainly because CI's shared/constrained
  runner vCPUs expose thread-scheduling races local hardware doesn't.
  **Not fixed, only worked around for now:** both are excluded from
  the CI `ctest` gate via `-E` (still run by every local/full-suite
  invocation) so CI stays usably green; the actual race in
  `engine/script/script_worker.hpp`/`ScriptSystem`'s threaded path
  needs real investigation (likely embedded-Python interpreter
  init/teardown ordering across threads) as its own scoped piece of
  work, not something to guess at blind.

## Effort vs. impact

Cross-cutting prioritization pass, originally 2026-09-19, **re-derived
2026-09-19** post-arc: the shipped item is removed, resolved items are
removed, and the two refresh-note items (asset-usage queries' now-easier
build, the `FieldVisitor` extension's now-tripled motivation) are
re-rated. Re-derive again when the backlog above has moved significantly
rather than trusting the ratings below forever.

### 🟢 Quick wins (hours-scale, single focused change)

**Empty as of 2026-09-20, re-derived three times today** — the original
2026-09-19 survey's items shipped, then two follow-up re-derivations each
found exactly one more genuine item (both now shipped too), then a third
pass found nothing at all ("the well is dry" -- every remaining Non-goals/
deferred-work mention traces to already-shipped items, not-yet-applicable
conditions, or content already tracked in the Medium/Large tiers below).
Re-derive again once real new work has landed since the last pass — don't
immediately re-run a fourth time expecting different material from the
same unchanged source docs.

**Re-derived 2026-10-04** after eleven slices landed (#209–#219). Two
genuine quick wins now exist, both surfaced by those slices rather than by
re-reading the same source docs:

| Item | Why it is a quick win |
|---|---|
| ~~**Exposure control**~~ | **SHIPPED 2026-10-07.** The "one `float` through the existing lighting UBO" framing was wrong — there was no free slot, `camera_position.w` being the IBL flag — so the UBO grew by a `vec4`. The design question *was* the real work, as predicted; it landed on `Camera`. |
| ~~**A suite-wide ctest `TIMEOUT`**~~ | **SHIPPED 2026-10-07** as `PROPERTIES TIMEOUT 60` on `catch_discover_tests` — one argument, not a `set_tests_properties` call. The prediction that "the work is choosing the value, not writing the line" was exactly right: the line took seconds, the value came from timing a full run (slowest case 1.11s, suite 24s, replacing ctest's silent 1500s default). |

**Re-derived 2026-10-10** after U7 image presentation shipped. Both 2026-10-04 entries are
now struck through, and this pass takes its material from that slice's review findings
rather than from re-reading the same source docs — which is the condition the 2026-09-20
note set for re-deriving at all.

| Item | Why it is a quick win |
|---|---|
| **Tighten the "position starts past the edge" test to exact values** | Two `REQUIRE`s change from bounds to `u0 == u1 == 1.0`, plus a `v`-axis pair that is currently absent. The real work is finding a clamp mutation that is **defined** rather than UB — the obvious one calls `std::clamp` with `lo > hi` — so expect the thinking to be in choosing the probe, not writing the assertions. |
| **Warn when a sub-rect lands entirely off its texture** | One diagnostic at the `resolve_image_quad` seam. A user who mistypes `sub_rect_pos` today gets a silent one-texel smear; the behaviour is defensible but its invisibility is not. Deliberately a *warning* rather than a behaviour change, because "treat as whole texture" and "skip the draw" are both defensible and choosing between them is a design call, not a quick win. |
| ~~**Sort the HUD image pass by `UiImage::order`**~~ | **SHIPPED 2026-10-11, image AND text together.** Two of this row's own claims were wrong. It was **not** "a copy of a shipped loop": the panel pass batches every quad into ONE `draw_ui` call after sorting, while the image and text passes draw per entity with their own texture/atlas handles — so each needed a collect → sort → emit reshape, not a copied `stable_sort`. And `order` was **not** "documented" on either component: both fields carried no comment at all, unlike `UiPanel::order`. The semantics are now written down, including that the order cannot cross a pass boundary. |
| ~~**An Inspector `field_int` (`DragInt`)**~~ | **SHIPPED 2026-10-11.** `FieldVisitor::field_int` (defaulting to `field_readonly`, like every other optional field hook), one `std::is_same_v<U, int>` branch in `FieldVisitorAdapter`, one `DragInt` override. The payoff is wider than the order lint that prompted it: `ParticleEmitter`'s `burst`/`burst_trigger`/`max_particles` and three easing curves, `TrailRenderer::max_points`, `AudioSource::attenuation_model` and `NavAgent::goal_x`/`goal_y` all became editable too. `int` only, deliberately — the reflected `std::uint32_t` fields are ids, dimensions and versions. Combo boxes for the enum-as-int fields are the follow-up below. |

### 🟡 Medium (a real focused sub-project, bounded new machinery)

| Item | Why this tier |
|---|---|
| ~~**Pointer input from the editor viewport to the scene**~~ — **SHIPPED 2026-10-07**, play-mode only; the behaviour question that made this 🟡 was answered before any code. The warnings below were all real, and two more turned up: `ClickNudgeSystem`'s press-edge detection synthesises a click when the play gate opens mid-press (fixed with a latch), and the nudge aspect has to come from the panel rather than the window. The panel-relative transform turned out to already exist, for edit-mode selection — which now needs gating on `!is_playing()`. Original entry: | Not the one-line pass-through it resembles. The scene renders to an offscreen texture composited by ImGui, so coordinates must be panel-relative, gated on hover, and suppressed while ImGui holds the mouse; `UiSystem` and `ClickNudgeSystem` must share one provider or the UI-claim arbitration breaks. And it needs a behaviour decision first — whether a viewport click drives the game's pointer in edit mode at all, or only in play mode. High impact: today, manipulating a scene and exercising its scripts are in two different programs. |

**The five original items in this tier shipped as of 2026-09-21:** Studio
plugin (`.py`) authoring via MCP (PR #193), per-plugin enable/disable
+ versioning (PR #194), plugin live file-watching/hot-reload
(PR #195), Studio theme system + persistence (PR #196), Studio
recent-projects list (PR #197).

**Re-derived 2026-10-04.** One genuine medium item now exists:

| Item | Shape |
|---|---|
| **An offscreen HDR render target plus a fullscreen resolve pass** | New render target, new pass, new pipeline, and a decision about where UI draws relative to the resolve. Bounded new machinery rather than a quick win, and it is the prerequisite for bloom. **Trigger, not preference:** the moment any additive-blending pipeline is added, in-shader tone mapping stops being defensible and this becomes necessary. |

Otherwise the next pick comes from the 🔴 Large
tier below — each of those items needs its own decomposition before
implementation starts.

### 🔴 Large (needs its own decomposition, multi-slice)

| Item | Impact |
|---|---|
| Blender mesh/geometry sync | Medium-High — the difference between a preview and a real DCC tool |
| Native `.so`/`.dll` plugins | High, but reopens native-code security surface |
| ~~Rendering: full PBR material model~~ — **COMPLETE 2026-10-07.** Decomposed 2026-10-04; all of it shipped: 1 tone mapping, 2 metallic/roughness + GGX Cook-Torrance, 3 texture maps + real image import, 4a mipmaps, 4b cubemaps/HDR/skybox, 4c image-based lighting, then multi-scatter energy compensation closing the one correctness gap the arc had recorded against itself (furnace test 0.31 → 1.000000 at roughness 1). | ~~High~~ done |
| RenderGraph refactor | Medium (enabling, not user-visible) |
| UI pillar gaps — **DECOMPOSED 2026-10-07** into U6–U11, see `docs/superpowers/specs/2026-10-07-ui-pillar-decomposition.md`. U6 (interaction depth) is first and carries one open question for a human; U7 (image presentation) is independent and needs no decision; U10 is blocked on an editor keyboard-input provider. | High if interactive-game UI is a near-term goal, else Low — **and that is still the open question**, which the decomposition does not answer. It only removes the excuse that the item was too shapeless to start. |
| Networking gaps | High if shipping real multiplayer soon, else Low |
| Package manager (network/deps/registry) | Medium |
| Public marketplace | Low today, High later — gated on an actual user base |
| 3rd-person adventure template | Medium |

**Stand up real CI (GitHub Actions) — SHIPPED 2026-09-21 (PR #198)**,
removed from the table above. `.github/workflows/ci.yml`: debug-preset
build, full `ctest`, headless `mosaic run --ticks N --json` smoke test,
on every push to `main` and every PR.

**Dependency-rule lint — SHIPPED 2026-09-21 (PR #199)**, the follow-up
named above. `tools/ci/check_dependency_rules.py` (+ its own unit
tests), wired into CI and `.githooks/pre-commit`. Surfaced and fixed a
real doc-vs-reality mismatch along the way: `ARCHITECTURE.md`/
`repo-layout.md` described a `Foundation → Engine → Editor → Studio`
layout with an `editor/` directory that was never built — rewritten to
describe the real tiers (`foundation/engine/studio/app` + `modules/`)
confirmed by grepping every `#include` direction in the tree. See the
`next-steps.md` entry for the full story, including the real
`app → studio` direction (opposite of what the old docs said) and the
CI-flake exclusion this PR's own run surfaced (tracked separately in
Tech debt above).

**Rendering: shadows — SHIPPED 2026-09-21 (PR #200)** and **spot lights
— SHIPPED 2026-09-21 (PR #201)**, both removed from the table above.
Directional-light-only, hard-edged, fixed-frustum shadow mapping, and a
third simultaneous light type reusing the point light's positional
machinery plus a `smoothstep` cone edge. Both extended direct
submission rather than building the RenderGraph, which stays deferred
(its own row above) until a genuinely *dynamic* pass need appears. The
per-slice deferrals each opened — PCF, point/spot-light shadows,
scene-fitted and cascaded frustums, configurable shadow resolution,
real multi-light support — are tracked in the Rendering section above,
not here.

**The old "Rendering: PBR/specular/spot lights" row is now only "full
PBR."** Decomposing it turned out to be the right call: spot lights
shipped as their own small slice, and the **shading-data UBO migration
— SHIPPED 2026-09-27 (PR #202)** (world-space shading, the codebase's
first uniform buffer, push constants 256 → 208 bytes) unblocked
**Blinn-Phong specular**, which is now a *small* slice rather than part
of a Large item — it needs 8 bytes of per-draw data against 48 bytes of
headroom, since world-space shading moved the camera position into the
per-frame UBO. Specular is tracked in the Rendering section above and
does **not** need its own decomposition pass. What genuinely remains
Large is the full metallic/roughness Cook-Torrance material model,
which still needs its own design decisions (scalar params vs. texture
maps for v1) and likely its own sub-decomposition.

**Picked as the largest effective win (2026-09-19, original pass):** bulk
refactoring / project health checks — **shipped**, all 4 sub-projects
complete (see specs above). Next pick TBD with the user.

**Scoping pass on "Stand up real CI" (2026-09-21, after the Medium tier
closed out)** — investigated the actual dependency chain rather than
guessing at a workflow file blind, since getting package names wrong
means a slow guess-push-fail loop with no one around to weigh
trade-offs. Findings from reading `CMakeLists.txt` and
`modules/render_vulkan/CMakeLists.txt` directly:
- Hard `REQUIRED` dependencies at configure time: `Threads`, system
  `Vulkan` (`find_package(Vulkan REQUIRED)` — needs the SDK/loader +
  headers installed, not just runtime-resolvable), `Python3`
  (Interpreter + Development, for the embedded pybind11 scripting),
  and a `glslc` binary on `PATH` (`find_program(GLSLC glslc
  REQUIRED)` — shaders are compiled to SPIR-V at *build* time, so a
  CI runner needs the compiler even though it never needs a real GPU
  to run it). GLFW is fetched and built from source via
  `FetchContent` — no system package needed for it.
- **The good news, confirmed by actually running the full suite
  locally and reading what it touches:** `ctest`/`mosaic_unit_tests`
  never creates a real `VkDevice`/window — it's headless logic
  (confirmed via this session's own repeated full-suite runs, all
  green, on a sandbox with no functioning GPU-backed Vulkan ICD).
  Only `mosaic run --window`/`mosaic editor` (never invoked by
  `ctest`) touch a real GPU. **This means CI likely does NOT need a
  working GPU or software rasterizer (lavapipe/llvmpipe) at all** —
  just the Vulkan *headers + loader* to satisfy `find_package`/link,
  and a real `glslc` binary to compile shaders at build time. This
  significantly de-risks the item versus the original "needs a full
  Vulkan toolchain in the runner" framing.
- **The actual open risk is `glslc` package availability on GitHub's
  `ubuntu-latest` runner image** — not confirmed by installing it
  anywhere, only reasoned about from memory (Ubuntu's own repos have
  historically been inconsistent about shipping a standalone `glslc`
  package versus only `glslang-tools`' `glslangValidator`, a
  different tool). The standard robust fix other Vulkan projects use
  is adding LunarG's official Vulkan SDK apt repository and
  installing the `vulkan-sdk` meta-package (guarantees loader,
  headers, validation layers, and `glslc` together) rather than
  hunting for the right combination of distro packages — but this
  needs an actual CI run to confirm, not more reading.
- **Not attempted tonight, deliberately:** writing and iterating on
  the actual `.github/workflows/*.yml` needs a real push-and-watch
  loop (`gh run watch`) to get the LunarG-repo-vs-distro-packages
  question and the exact CMake preset/build-type right, plus
  judgment calls this session's own practice would normally raise
  with the user first rather than decide alone at 1am: which preset
  (debug/release/both as a matrix?), whether to also gate on the
  dependency-rule check `CONTRIBUTING.md` already claims is
  "enforced" (it isn't, anywhere, today — a lint pass would need
  writing from scratch, not just wiring up a workflow), and whether
  a `ctest`-only pipeline is an acceptable v1 or whether launching
  `mosaic run --ticks N` headlessly (no `--window`) should also gate
  merges as a smoke test. **Next: brainstorm this properly with the
  user** (a real spec/plan cycle, not a chat-agreed bounded slice —
  this is the first item in the whole session that touches
  infrastructure outside the repo itself) before writing the
  workflow file.
