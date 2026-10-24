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

- ~~**Wrap remaining CLI verbs as MCP tools**~~ — SHIPPED, see [record](../record/undated.md#wrap-remaining-cli-verbs-as-mcp-tools).
- ~~**Verify AssetID referencing via `set_component`**~~ — CONFIRMED 2026-09-20, see [record](../record/2026-09.md#verify-assetid-referencing-via-set_component).
- ~~**Spatial queries**~~ — SHIPPED 2026-09-20, see [record](../record/2026-09.md#spatial-queries).
- ~~**Asset-usage queries**~~ — SHIPPED, see [record](../record/undated.md#asset-usage-queries).
- **Generic criteria-based search (query DSL)** — "find entities where
  field Y matches Z". Most general/powerful, needs its own decomposition.
  *Source:* same
- ~~**Studio plugin (`.py`) authoring via MCP**~~ — SHIPPED 2026-09-20, see [record](../record/2026-09.md#studio-plugin-py-authoring-via-mcp).
- ~~**Undo support for `set_script`**~~ — SHIPPED 2026-09-20, see [record](../record/2026-09.md#undo-support-for-set_script).
- **`ScriptEditorModel::debug()` exposure** — compile + run `on_update(0)`
  with a debugger attached, no write. *Source:* same
- **Procedural asset generation** — generate texture/mesh/audio content
  from scratch (not import). No existing machinery, needs its own
  decomposition. *Source:*
  `2026-09-18-asset-generation-import-asset-design.md`
- ~~**Confirm `AssetID` referencing via `set_component`**~~ — RESOLVED, see [record](../record/undated.md#confirm-assetid-referencing-via-set_component).
- ~~**Hot-reload imported assets into the live `AssetRegistry` mid-session**~~ — SHIPPED, see [record](../record/undated.md#hot-reload-imported-assets-into-the-live-assetregistry-mid-session).
- ~~**"Tick until condition met" tool**~~ — SHIPPED, see [record](../record/undated.md#tick-until-condition-met-tool).
- **Broader per-system profiling** — physics/animation/particles timing,
  not just scripts. *Source:*
  `2026-09-19-optimization-script-profile-design.md`
- ~~**Bulk refactoring / project health checks**~~ — SHIPPED, see [record](../record/undated.md#bulk-refactoring--project-health-checks).
- **Multi-scene project support in MCP**, **concurrent MCP session locking
  on `save_scene`** — both explicitly unaddressed. *Source:*
  `2026-09-16-agent-api-mcp-server-design.md`

**Refresh notes (2026-09-19, post-arc):**
- ~~**The `FieldVisitor` extension**~~ — SHIPPED, see [record](../record/undated.md#the-fieldvisitor-extension).
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
- ~~**Per-plugin enable/disable + manifest/versioning**~~ — SHIPPED 2026-09-21, see [record](../record/2026-09.md#per-plugin-enabledisable--manifestversioning).
- **Public marketplace** — hosting, moderation pipeline, static/dynamic
  analysis review layers. *Status:* design-only
  (`docs/design/marketplace.md`), explicitly "not yet scheduled."
- **Package manager: network fetch, version constraints/dependency
  resolution, remote registry, templates-as-packages, transitive deps** —
  all deferred, local-only today.

## Documentation tooling

**Deferred while designing the `next-steps.md` rolling archive (2026-10-21).** The archive
slice itself is scoped to `next-steps.md` only; these were named in that design and
deliberately left out.

- ~~**`deferred-work-backlog.md` has the same unbounded-growth problem**~~ — DESIGNED 2026-10-21, see [record](../record/2026-10.md#deferred-work-backlogmd-has-the-same-unbounded-growth-problem).

**Non-goals of that design (2026-10-21), each deferred with its reason:**

- **A checker for `next-steps.md`'s verifiable claims.** The design makes every claim
  carry the command that regenerates it (`ctest --preset debug -N` beside a test count), so
  a reader can check one by hand. A tool that runs them all and fails on drift is a real
  follow-up — it would have caught the six-week-stale gate that misled a reader on
  2026-10-21 — but the right time to build it is after the file's shape has settled.
- **Splitting the record by subsystem instead of by month.** Monthly matches the existing
  archive and needs no judgement call per entry; by-subsystem reads better when revisiting
  one area. Revisit only if reading the record by subsystem becomes a real need.
- **Reconciling `next-steps.md` with the agent memory directory.** The landmine list the
  design puts in `next-steps.md` duplicates what an agent's memory already holds. The
  intended rule is recorded in the spec — the repo file is authoritative, memory holds only
  what is personal to how an agent works — but actually reconciling the two is its own
  piece of work, and doing it badly recreates the duplication problem one layer up.
- **Backfilling record entries for work predating the convention.** The record starts with
  what the archive already holds; reconstructing earlier slices from git history is
  speculative narrative, which is the opposite of what the record is for.
- **Automating the archive split in the pre-commit hook** — considered and declined for now.
  The hook already aborts commits to regenerate docs; adding a step that *moves content
  between files* to that same path is a much larger surface for surprise, and the failure
  mode (content silently relocated mid-commit) is worse than the one it prevents (a file
  that gets big slowly). Chosen instead: a script run deliberately and reviewed in a PR.
  Revisit if the manual run is actually forgotten for more than a month or two.
- **The eight existing `docs/updates/*-summary.md` files stay as they are** — the Aug-Sep
  2026 development summaries. They are a *different* document type (retrospective narrative
  with PR links) from the handoff blocks being archived beside them, and rewriting them into
  the new convention would be churn for no reader benefit. Deferred permanently unless the
  archive's nav turns out to read badly with two kinds of file in one directory.

- ~~**CI/pre-commit wiring for `mosaic docs` + nav regen**~~ — SHIPPED, see [record](../record/undated.md#cipre-commit-wiring-for-mosaic-docs--nav-regen).
- ~~**No CI exists in this repo at all**~~ — SHIPPED 2026-09-21, see [record](../record/2026-09.md#no-ci-exists-in-this-repo-at-all).

**Non-goals of the 2026-10-23 tier re-derivation, each deferred with its reason:**

- **Ranking the older live bullets in the subsystem sections.** The tiers rank only the 25 live
  bullets the 2026-10-20..23 PRs created. The other ~100 (UI, Physics, Rendering, Editor) carry no new
  evidence, and the standing rule forbids re-deriving from unchanged sources. Ranking them is a
  per-subsystem judgement that wants a human for impact. Revisit when real work lands in one
  subsystem: rank that subsystem's bullets then, from that work.
- **Promoting the tier pass's checks to `tools/docs/` and CI.** The conservation check (live-bullet
  count and title multiset), the record-anchor check and the tier-row traceability check were
  written for one pass and run by hand, as a scratch script that was NOT committed, so they are
  currently **unreproducible**: the evidence for that pass exists only as pasted output. Whoever
  picks this up is rebuilding them, not wiring up something that exists. A standing version wants
  the design the claims-checker item above has. Depends on that item; do them together.
- **Ten record anchors in this file are already dead, and a one-file check cannot see them.**
  Found 2026-10-23 by a checker scanning all of `docs/` (80 record anchors); the tier pass's own
  check scanned only this file and reported 0 dead, which was true of what it covered. The same 10
  are dead on `main`, so they predate the tier pass. Most likely from #280's migration: the stubs
  link to slugs the record headings do not produce (headings written with different text or
  punctuation), e.g. `undated.md#bulk-refactoring--project-health-checks`,
  `2026-09.md#issue-25--component-typeid-densification`, `2026-10.md#u8a--inherited-alpha-and-stack-layout`
  and the U6, U7 and GLFW-`inside` stubs. Deferred: out of scope for the tier pass. This is the
  concrete evidence that the anchor check should be promoted to CI and scoped over all of `docs/`.
- **Teaching `archive_shipped_backlog.py` to find an unstruck original sitting under its own stub,
  and to collapse prose "Re-derived" blocks.** The 2026-10-23 pass found 14 live bullets directly
  under their own struck stubs (the tool classifies only entries that START with `~~`, so it
  cannot see them) and folded them into the record by hand, plus 5 shipped bullets that were never
  struck. Deferred: a one-off cleanup, and a detector that guesses at "same entry" needs the
  prefix-key matching this pass used and its false-positive cost measured first.

## UI pillar (declared "COMPLETE" at U5b, but with real gaps)

- ~~**U8a — inherited alpha and stack layout**~~ — SHIPPED 2026-10-17, see [record](../record/2026-10.md#u8a--inherited-alpha-and-stack-layout).

- **`stack_children` scans the whole world once per laid-out element, per frame.**
  Introduced by U8a 2026-10-17 and recorded by its author rather than found in
  review. `resolve_parent_frame` is called per UI element per frame — by the draw
  path AND the hit-test path — and now calls `slot_for_child`, which calls
  `stack_children`, which is a full `for_each<UiParent>` over the world plus a
  sort. So the cost is roughly **O(E × d × P)** per frame: E elements drawn, d
  the depth of each one's parent chain, P the number of entities carrying
  `UiParent`. That is quadratic in the size of a parented UI.

  **Not a problem at today's scale and deliberately not optimised:** the demo has
  four parented elements, so this is a handful of comparisons a frame, and the
  obvious fixes (build a parent→children index once per frame, or memoise per
  resolution pass) both add a cache whose invalidation is a new correctness
  surface — in a function whose *whole value* is being pure and lazy, which is
  what makes the draw and hit-test paths agree by construction. Paying that
  complexity before anything is slow would be trading a guarantee for a guess.

  **Sharpened by the whole-branch review**, which put real numbers on it: the
  resolve happens in the panel pass, the image pass, the text pass AND
  `button_hit_candidates`, so 4-5 times a frame, and each `resolve_parent_frame`
  also allocates `2D` vectors. A 30-child stack in a world of 100 `UiParent`
  entities is roughly 12,000 visits, 120 sorts and 240 heap allocations per
  frame; a 200-child stack with 500 parents is ~400,000 visits and ~4,000
  allocations per frame, which would dominate the UI's cost. Non-stack scenes
  pay nothing -- the `UiStack` lookup short-circuits before the scan.

  **The trigger to fix it** is a UI of a few hundred elements, which nothing here
  has yet; the review's phrase for it is "a real scaling cliff that is invisible
  until someone builds an inventory grid". The named mitigation is a per-frame
  memoised `parent -> children` map built once per render rather than per
  element. Measure first: the index is cheap, but confirming the cost is real
  matters more than the shape of the fix. Depends on nothing.

- **`UiGrid` and `UiFlow`** — deferred 2026-10-17 from the U8a layout design,
  which builds `UiStack` only. The three share all their machinery (enumerating a
  parent's children, ordering them deterministically, narrowing the frame to a
  slot) and differ only in slot arithmetic, so they are genuinely additive once a
  stack exists. Deferred to keep the surface of the slice's one reserved decision
  (what an authored `pos` means under a layout) as small as possible before
  anyone has seen it work. Depends on U8a shipping.
- **Content-sized layout slots** — U8a divides a parent's padded rect **evenly**
  along the axis. Content-sized slots need content measurement, and `UiText` has
  no measured height until its font atlas is resolved, so layout would come to
  depend on asset loading — a dependency worth refusing deliberately rather than
  acquiring by accident. Deferred 2026-10-17. Depends on a decision about whether
  layout may block on assets at all.
- **A real `visible` bool on the UI components** — U8 asked for "inherited
  visibility/alpha", and U8a delivers alpha only. **These are not the same
  feature:** `alpha = 0` hides an element but it still HIT-TESTS, so an invisible
  panel still eats clicks. Deferred 2026-10-17 for two reasons: the hit-test
  question belongs to U6, and adding a field to `UiPanel` re-binds the positional
  brace initialisers in `app/run/demo_scene.hpp` where the compiler would not
  necessarily catch it. Depends on deciding whether invisible means
  non-interactive.
- **A layout-slot gizmo in the editor** — authoring a `UiStack` works through
  reflection with no editor work, but nothing *visualises* a slot, so an author
  tuning `padding` or `spacing` is reading numbers rather than seeing boxes.
  Deferred 2026-10-17: drawing a computed rect in the viewport is editor work
  with its own design, and the numbers are checkable meanwhile. Depends on
  nothing.
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

- ~~**U6 interaction depth — hover enter/exit + visual feedback, pointer capture/drag, z-order-aware overlap resolution. First: its prerequisite just landed, and it …**~~ — SHIPPED 2026-10-10, see [record](../record/2026-10.md#u6-interaction-depth--hover-enterexit--visual-feedback-pointer-capturedrag-z-order-aware-overlap-resolution-first-its-prerequisite-just-landed-and-it-).
- ~~**Pointer capture and drag for `UiButton`**~~ — SHIPPED 2026-10-12, see [record](../record/2026-10.md#pointer-capture-and-drag-for-uibutton).
- ~~**A pointer that leaves the window can still hover an edge-anchored button**~~ — FIXED 2026-10-10, see [record](../record/2026-10.md#a-pointer-that-leaves-the-window-can-still-hover-an-edge-anchored-button).
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
- ~~**Engine-rendered hover visuals (a `UiHoverStyle` component)**~~ — SHIPPED 2026-10-10, see [record](../record/2026-10.md#engine-rendered-hover-visuals-a-uihoverstyle-component).
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
- ~~**The REAL `order` bug is pairwise and relative, not per-entity and absolute**~~ — SHIPPED 2026-10-12, see [record](../record/2026-10.md#the-real-order-bug-is-pairwise-and-relative-not-per-entity-and-absolute).
- ~~**`UiImage::order` and `UiText::order` are ignored by the renderer entirely**~~ — FIXED 2026-10-11, see [record](../record/2026-10.md#uiimageorder-and-uitextorder-are-ignored-by-the-renderer-entirely).
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
- ~~**Three copies of the same anchor clamper**~~ — FIXED 2026-10-12, see [record](../record/2026-10.md#three-copies-of-the-same-anchor-clamper).
- ~~**No end-to-end test that a conflicting pair actually misroutes a click**~~ — CLOSED 2026-10-12, see [record](../record/2026-10.md#no-end-to-end-test-that-a-conflicting-pair-actually-misroutes-a-click).
- **Cross-entity overlap is still invisible to both order diagnostics** — restated
  2026-10-12 now that the pairwise check exists and *still* cannot see it. A button whose
  visual lives on a different entity is skipped whole by `find_order_conflicts`, because the
  ownership rule gives it no visual rank. Detecting it needs rect-overlap analysis between a
  button and panels it does not own, which is a different and much larger question than the
  pair comparison that shipped.
- ~~**The GLFW provider never reports `inside = false`, so `drag_cancel` is unreachable in `mosaic run --window`**~~ — FIXED 2026-10-12, see [record](../record/2026-10.md#the-glfw-provider-never-reports-inside--false-so-drag_cancel-is-unreachable-in-mosaic-run---window).
- **`UiSystem`'s drag branch does not gate on `p.inside` while the release path does** —
  latent, found by review 2026-10-12. Unreachable with both shipped providers (the editor
  drops `down` off-panel; GLFW never drops `inside`), but a third provider reporting
  `down && !inside` — the obvious shape for "cursor left but button held" — would stream
  `drag` at clamped coordinates forever and never reach `drag_cancel`. One `&& p.inside` makes
  the two paths agree about what "on the surface" means. Deferred as a Minor with no reachable
  effect; worth folding into whichever slice adds a third provider.
- ~~**A press surviving a play-mode change emits a stale drag action**~~ — FIXED 2026-10-18, see [record](../record/2026-10.md#a-press-surviving-a-play-mode-change-emits-a-stale-drag-action).
- **A one-frame hover lag on the release frame** — `hover_target` is computed before the
  release edge clears `captured_`, so releasing over a *different* button leaves it unhovered
  for that frame and it enters on the next. About 16ms at 60Hz. Recorded 2026-10-12 so it is
  not rediscovered as a bug; not worth changing.
- **DECISIONS TAKEN 2026-10-12** on the five questions the UI arc left standing, recorded so
  they are not re-litigated:
  - **`Ambiguous` order conflicts stay as they are** until a real scene has overlapping
    buttons. The noise question cannot be judged from a repository whose only scene has one
    button, and guessing at it now would tune against an imagined scene.
  - **No `imgui_test_engine`**, so `ImGuiFieldVisitor`'s widget calls stay machine-untested.
    The cost is larger than "a test-only dependency" suggests: ImGui carries hook points
    guarded by `IMGUI_ENABLE_TEST_ENGINE` (eight sites in `imgui.cpp`), so adopting it means
    compiling the ImGui the **editor** links with that define, or maintaining two ImGui
    builds. For coverage of four widget calls whose dispatch layer is already well tested,
    that is not worth it. Revisit only if an Inspector widget bug actually ships.
  - **Enum metadata on the reflection surface is worth adding** — the prerequisite for combo
    boxes on `anchor`, `fit_mode` and the easing curves, which today are bare `DragInt`s a
    user can drag to 47.
  - ~~**Bloom is wanted soon**, which fires the trigger the 🟡 offscreen HDR target has been
    waiting on.~~ — the HDR target **shipped 2026-10-16**; bloom is the next rendering slice.
  - ~~**Standalone pointer semantics should match the editor**~~ — done, above.
- **The resolve must not take its UV from clip position** — found 2026-10-13 while scoping the
  backend half, before any of it was written. Both render passes use a **negative-height
  (Y-flipped) viewport** (`vulkan_render_device.cpp:1854` and `:2043`); `ui.vert` compensates
  explicitly with `clip.y = 1 - 2*ny`, but `screen_effect.vert` does **not** — and nobody
  noticed because a vignette is radially symmetric, so a vertical flip is invisible in it.
  The plan said to reuse `screen_effect.vert` for the resolve, which would have shipped an
  upside-down scene detectable only by eye. **Resolution: derive the UV from
  `gl_FragCoord.xy`**, which is framebuffer space and immune to the viewport flip, rather
  than from clip position. Recorded here because the same trap waits for every future
  fullscreen pass that samples a texture — bloom's blur chain first among them.
- **A pixel-accurate drag threshold** — `kDragThreshold` is `0.01` in **normalized** screen
  units, so it is anisotropic: about 19px horizontally against 10px vertically at 1920x1080.
  A pixel threshold needs the screen aspect inside `UiSystem`, which it does not have and
  which would have to be threaded in the way `resolve_image_quad` takes it. Recorded
  2026-10-12 as the accepted cost of the capture/drag slice. **Depends on** nothing; it is a
  plumbing question, not a design one.
- **The drag threshold's value is unvalidated** — `0.01` is a guess until a human drags
  something. Whether it feels comfortable, and whether the un-highlight on drag-off reads
  correctly, are exactly the claims no test can make; both are queued for the next human
  verification pass. Recorded 2026-10-12 so the number is not mistaken for a measured one.
- **Drag on non-button elements** — only `UiButton` is hit-testable, so only a button can
  capture. The same boundary the hover slice drew, and widening it is the recorded "hover and
  hit-testing on non-button UI components" item above rather than a separate question.
- **Suppressing `click` after a drag** — the road not taken, 2026-10-12. Releasing inside a
  button after dragging emits `drag_end` **and** `click`, because press-wiggle-release is a
  click to any user and suppressing it would change shipped behaviour for an input nobody
  considers a drag. A handler wanting drag-only behaviour filters on `action`, which every
  handler must already do. Recorded so it is not revisited as an oversight.
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
- ~~**`int` fields are read-only in the Inspector (no `field_int`)**~~ — FIXED 2026-10-11, see [record](../record/2026-10.md#int-fields-are-read-only-in-the-inspector-no-field_int).
- ~~**U7 image presentation — aspect-preserving fit/fill, 9-slice scaling, texture atlas sub-rects. Independent of everything else and the cheapest real group.**~~ — SHIPPED 2026-10-10, see [record](../record/2026-10.md#u7-image-presentation--aspect-preserving-fitfill-9-slice-scaling-texture-atlas-sub-rects-independent-of-everything-else-and-the-cheapest-real-group).
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
- ~~**Shadows — v1 (directional-only, hard-edged, fixed-size ortho frustum)**~~ — SHIPPED 2026-09-21, see [record](../record/2026-09.md#shadows--v1-directional-only-hard-edged-fixed-size-ortho-frustum).
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
  - **A GLSL include mechanism for shared shader code** — originally the
    reason `tonemap_aces` was duplicated into the skybox shader rather
    than shared: the shader build has no include step, and adding one is
    a build-system change rather than a rendering one. Deferred
    2026-10-05 during 4b's design. **That particular duplication is gone
    as of 2026-10-16** — the HDR resolve moved the curve out of both
    object shaders into `hdr_resolve.frag`, which is now its only copy in
    the tree, and the contract test pins that rather than pinning two
    copies equal. The item stands because nothing about the build
    changed: the next function two shaders both want will reach the same
    wall. Lower priority now that the one real instance resolved itself.
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
  - ~~**An offscreen HDR render target with a fullscreen resolve pass.**~~
    — **SHIPPED 2026-10-16.** The scene renders into a swapchain-sized
    `R16G16B16A16_SFLOAT` target and `hdr_resolve.frag` tone maps the whole
    image once on the way to the destination. **The boundary:** everything
    world-space is inside — opaque meshes, skybox, decals, particles,
    ribbons, the vignette, world text — and the UI draws onto the resolved
    result, because a `UiPanel`'s colour is authored rather than captured
    and through ACES the HUD would dim whenever the world got bright. The
    resolve is a **draw, not a pass**: a fullscreen triangle into whichever
    target is recording, which is what keeps `mosaic run --window` and the
    editor on one code path. **Bloom is now unblocked.** The original
    reasoning is kept below, because its trigger fired exactly as written.
    What it did not anticipate: collapsing the scene's pipeline pairs
    (every pipeline was built twice, once per destination pass) made the
    backend *smaller*, fourteen scene pipelines to seven, and the editor's
    viewport pass had to become lazy so the HDR pass could open without
    nesting inside it. Deferred 2026-10-04 during the tone-mapping design:
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
  - **A configurable tone-mapping curve** — ACES (Narkowicz) stays. Now in
    ONE place rather than two, which makes swapping it a one-function
    edit, but *choosing* a second curve is its own question: what the
    alternatives are for (Reinhard reads flatter, AgX rolls off longer),
    whether it is per-camera or per-project, and how it serialises.
    Deferred 2026-10-16 because nothing has asked for a second curve;
    cheapness is not a reason to add a knob. Depends on nothing.

  - ~~**Bloom.**~~ — **SHIPPED 2026-10-17.** A `Bloom` component on the
    camera entity (`intensity` 0, `threshold` 1.0, `knee` 0.5), one device
    hook `build_bloom(BloomParams)` between `end_hdr_scene_pass` and
    `draw_hdr_resolve`, and in the Vulkan backend one half-resolution
    `R16G16B16A16_SFLOAT` image with N mips (N clamped to 6), one render
    pass with `loadOp = LOAD`, three pipelines and `N+1` descriptor sets
    driven by a single loop. Bloom is added INSIDE the tone curve, and
    exposure scales it too, because bloom is light. Spec
    `docs/superpowers/specs/2026-10-17-render-bloom-design.md`, plan
    `docs/superpowers/plans/2026-10-17-render-bloom.md`.

    **The claim this entry used to carry — that bloom is the RenderGraph's
    trigger — was wrong**, and building it is what proved so; the graph's
    trigger is restated in its own entry below. **Two things the plan
    decided that the spec left open:** `bloom_level_count` lives in its
    own module file, because nothing in the suite creates a `VkDevice` and
    sizing logic inside `vulkan_render_device.cpp` has no possible test;
    and the downsample and upsample share ONE shader, since offsetting the
    taps by a *source* texel is what makes one a box downsample and the
    other a tent upsample, so they differ only in blend state.

    **Both real defects came from the one step that runs real Vulkan, not
    from 1404 tests.** `create_bloom_chain` clears the chain through
    `begin_single_time_commands`, so placed beside `create_hdr_target` —
    the obvious template for a swapchain-sized resource, and above the
    command pool — it segfaulted; the constructor already carried that
    exact lesson for the IBL stand-ins. And the plan's validation step
    named `mosaic edit`, which is not a subcommand: it exits 106 with zero
    VUIDs, which looks like a pass, and would have reported the editor
    path verified without ever running it. Both are now pinned by contract
    tests in `tests/unit/render/bloom_backend_contract_test.cpp`.

    Its design and plan deferred six things, each its own entry below:
    lens dirt and anamorphic bloom, per-object bloom masks, temporal
    stability, a bloom quality setting, retuning the demo's threshold, and
    the per-resize rebuild of every swapchain-derived target. Originally
    deferred 2026-10-04, blocked until 2026-10-16.

  - **Lens dirt, anamorphic and streak bloom** — the three effects most
    often bundled with bloom in a shipping renderer. Deferred from the
    bloom design 2026-10-17: each is a *separate* effect with its own
    authored assets (a dirt texture; a directional rather than isotropic
    kernel), not a parameter on the one being built. Depends on bloom
    shipping; nothing else.

  - **Per-object bloom masks** — "this material glows, that one does not",
    independent of how bright it is. Deferred from the bloom design
    2026-10-17 because it needs a channel the renderer does not have: a
    stencil bit, or a fourth component in a target that currently spends
    alpha on nothing. Threshold bloom is the brightness-driven version and
    is strictly cheaper. Depends on a decision about where that channel
    lives.

  - **Temporal stability for bloom (TAA or a history buffer)** — the soft
    knee handles a highlight crossing the *threshold*, but a sub-pixel
    specular highlight that appears and disappears between frames still
    flickers, and the mip chain amplifies it. Deferred from the bloom
    design 2026-10-17: fixing it needs frame-to-frame history, which is a
    renderer-wide feature (it would also want to serve antialiasing) and
    much larger than the effect that exposes it. Depends on a history
    buffer existing.

  - **A bloom quality setting / configurable mip count** — `N` is derived
    from the resolution and clamped at 6. Deferred from the bloom design
    2026-10-17 for the same reason the second tone curve was: a knob needs
    a reason, and nothing has asked. The derivation is one function, so
    making it authorable later is cheap. Depends on nothing.

  - ~~**A depth-attachment `loadOp CLEAR` is not ordered against its own layout
    transition.**~~ — **FIXED 2026-10-18.** It was in TWO passes, not one:
    `create_offscreen_render_pass` (the editor's viewport target) and
    `create_hdr_render_pass`. Both cleared a depth attachment while naming only
    `COLOR_ATTACHMENT_OUTPUT` in their external dependency's destination scope,
    so the `UNDEFINED -> DEPTH_STENCIL_ATTACHMENT_OPTIMAL` transition (a write)
    and the `loadOp CLEAR` (another write) were unordered. `create_render_pass`
    (the swapchain) already had it right and was the template; a contract test
    now holds all three together. Verified by the layer: that message class went
    from 10 occurrences to 0.

  - ~~**Cross-submit `WRITE_AFTER_WRITE`: one command buffer's `storeOp` against
    the next submission's `vkCmdBeginRenderPass`.**~~ — **FIXED 2026-10-18, and
    my own analysis of it was wrong in the direction that matters.**

    The diagnosis was right: `kFramesInFlight` is 2 while `depth_image_`,
    `hdr_color_`, `hdr_depth_`, `vp_color_` and `vp_depth_` are single images
    shared by both frames, and `begin_frame` waits on the fence from two
    submissions ago — so frames N and N+1 overlap and race on the shared depth
    attachment.

    **The prescribed fixes were all wrong.** This entry previously listed three
    options — per-frame duplicates of five images (which would have made the
    whole bloom chain per-frame), reducing to one frame in flight, or explicit
    cross-frame barriers — and called the choice between them a real design
    decision about memory against latency. **None of that was necessary.**
    `VK_SUBPASS_EXTERNAL` already covers prior submissions on the same queue, so
    the entire fix was naming the previous frame's writes in each pass's
    **source scope**: `LATE_FRAGMENT_TESTS` and
    `VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT`. Three render passes, no
    duplicated memory, no lost frame overlap.

    **Two bits cost a measurement each to find, and both are worth remembering.**
    `LATE_FRAGMENT_TESTS` rather than only `EARLY` — depth writes occur in both
    stages and the layer attributes the end-of-pass `storeOp` to LATE. And the
    **swapchain** pass, whose dependency had `srcAccessMask = 0` and therefore
    named no prior access at all; fixing only the offscreen and HDR passes left
    the hazard count unchanged at 10 and looked like total failure.

    **How it was actually found:** by reading the layer's full message instead of
    its count. The one-line summary says only "writes to resource, which was
    previously written ... by the attachment storeOp"; the sentence *after* it
    names the access and the stage, which is the entire answer. Verified: both
    entry points now report **zero** hazards of any kind under
    `MOSAIC_VK_SYNC_VALIDATION=1`, and a contract test pins all three passes.
  - ~~**Synchronization validation is never enabled, so the project's strongest
    automated check cannot tell "correctly synchronised" from "accidentally
    works".**~~ — **SHIPPED 2026-10-17**, and it found a real hazard in bloom on
    its first run. Opt-in via `MOSAIC_VK_SYNC_VALIDATION=1`, because the feature
    is genuinely expensive; the point was that it was previously unreachable at
    any price. The run logs `synchronization validation ENABLED` at startup, so
    a clean log now proves WHAT was checked — the failure this guards against is
    not a missing check but a false claim about one. Original entry: `vulkan_render_device.cpp` enables `VK_LAYER_KHRONOS_validation`
    with no `VkValidationFeaturesEXT`, so the sync-validation layer is off on
    every run — including the two-entry-point grep this project treats as its
    real gate. Raised by the bloom branch review 2026-10-17, and bloom is why
    it now matters: it is the first feature in this renderer with a
    **multi-pass intra-frame dependency on one image** (eleven passes, each
    sampling the mip the last one wrote), and sync validation is the only
    automated thing that can see a wrong barrier there. The branch's own
    subpass dependency was widened on exactly this reasoning, with no way to
    confirm it automatically. Deferred: it is a flag or env var plus a decision
    about whether it runs always or on demand (it is not free), which is its
    own small slice rather than part of bloom. Depends on nothing; it would
    retroactively strengthen every render slice already shipped.

    Its own entry rather than a line in the resize item below, because the two
    are unrelated: this one is about what the gate can SEE, not about what the
    renderer does.

  - **Every swapchain-derived target is rebuilt on each resize, including
    mid-drag.** `recreate_swapchain` already destroys and recreates the HDR
    colour and depth images; the bloom plan (2026-10-17) adds up to six image
    views, six framebuffers and one more allocation to that, and a window drag
    emits a continuous stream of resize events. Deferred 2026-10-17 from the
    bloom plan: it is a stutter while dragging, not a correctness problem, and
    the fix -- allocate for the largest extent seen and render to a sub-rect --
    is a design question about *every* swapchain-derived target at once rather
    than about bloom. Depends on nothing; it gets easier once there is more than
    one target with the problem, which is now.

  - **Retune the demo's bloom threshold** — *conditional*, pending the next
    human verification pass. The demo lights itself with a real outdoor HDR
    sky whose radiance genuinely exceeds 1.0, so at the design's default
    threshold of 1.0 the sky and horizon will glow. That is physically
    right but may veil the image. Deferred from the bloom design 2026-10-17
    because the sandbox cannot screenshot a GUI window, so which side of it
    the demo lands on is not knowable from here. If it does veil, the fix
    is the *scene's* authored threshold, not the pipeline. Depends on a
    human at the window.
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

- **Remove the dead `exposure` fields from `cube.frag` and
  `skybox.frag`** — the HDR resolve slice (2026-10-16) moved exposure
  into `hdr_resolve.frag`, which takes it as a push-constant parameter.
  The old declarations are now unread: `fl.exposure` (a trailing `vec4`
  in cube.frag's per-frame UBO) and `pc.exposure` (a `float` in
  skybox.frag's push block). Deferred rather than done in that slice
  because each block's layout is shared byte-for-byte with a C++ struct
  — `FrameLighting` and `SkyboxPush` — so dropping the GLSL member alone
  is silent corruption rather than a compile error, and the matching C++
  change is a separate edit with its own size assertion to update.
  Depends on nothing; it is a two-sided deletion, not a design question.

- **Give the viewport pass its own lazy-open flag independent of the
  HDR pass** — the same slice made `begin_viewport_pass` lazy
  (`viewport_pass_requested_` + `ensure_viewport_pass`) so the HDR scene
  pass could open without nesting inside it. The pass-state bookkeeping
  in the Vulkan backend is now three booleans (`swapchain_pass_open_`,
  `viewport_pass_open_`/`_requested_`, `in_hdr_pass_`) read by a dozen
  draw entry points. A single `enum class PassState` would make the
  invariant "exactly one pass is recording" checkable instead of
  conventional. Deferred: low priority today, and worth doing at the
  moment bloom adds its mip-chain passes rather than immediately before.

- **The RenderGraph** — deferred again, and its **trigger is now
  sharper**. This backlog twice predicted bloom would fire it, on the
  grounds that a mip chain's pass count varies with resolution. The
  bloom design (2026-10-17) found that wrong: a mip chain is a *linear*
  chain whose length is a function of resolution and is recomputed on
  resize — the same lifecycle the HDR target already has — so in code it
  is `for (i = 0; i < levels_; ++i)`, a loop, not a graph. There is no
  conditionality, no reordering, no data-dependent topology. **The real
  trigger, restated:** passes whose *existence or order* depends on
  per-frame scene content ("run the reflection pass only if a reflective
  material is visible"; several cameras with inter-target dependencies),
  or a user-authorable post chain where the order is data rather than
  code. Deferred because nothing has asked for one of those yet. See
  `docs/superpowers/specs/2026-10-17-render-bloom-design.md`.

- ~~**Retune the demo's particle, ribbon, decal and world-text colours**~~ — DONE 2026-10-17, see [record](../record/2026-10.md#retune-the-demos-particle-ribbon-decal-and-world-text-colours).

- **The particle fade curves hide the colour journey** — raised by the
  2026-10-17 verification pass: "the fading colour particles are staying
  the same colour and just fading out". `color_at` lerps correctly every
  frame; the cause is elsewhere. `end_size` is `0.03` against a `size` of
  `0.15` (a 5x shrink) and `end_alpha` is 0 on an `EaseOut` curve, so the
  end colour lands on a particle a fifth the size and nearly transparent,
  alpha-blended over a bright white ground. Deliberately NOT changed with
  the colours: it is the effect's motion design rather than its palette,
  and the verification pass only authorised the palette. Depends on
  nothing; it is a judgement about how a particle should die.

- ~~**No demo entity exercises bloom on ribbons or world text**~~ — DONE 2026-10-21, see [record](../record/2026-10.md#no-demo-entity-exercises-bloom-on-ribbons-or-world-text).

- ~~**Demo props are placed against the world origin, not against the surface they sit on**~~ — DONE 2026-10-21, see [record](../record/2026-10.md#demo-props-are-placed-against-the-world-origin-not-against-the-surface-they-sit-on).

- **Lime "GLOW" world label: legible but low-contrast; revisit only if it becomes a problem
  (human decision 2026-10-21).** The verification pass's verdict, verbatim: "The word GLOW is
  legible, though the contrast is pretty low. I count this passing for now. We can look at
  adjusting it later if it becomes a problem in the future." Kept at `{1.0, 2.85, 0.25}` in
  `app/run/demo_scene.hpp` (the decision is recorded beside it too). **`WorldLabel` has no
  outline or shadow field** (font, text, size, colour, alpha, offset only), so the options
  are: a different colour, a different position (against something darker than the light-grey
  ground), or new engine work to add an outline/shadow. **Re-confirmed after #275**: the verdict
  above was first given while the label was being drawn TWICE (double-blended, brighter than
  authored) because of the `draw_world_text` aliasing bug; once that was fixed the label draws
  once and dimmer, and the human re-checked and kept the decision. It therefore rests on the
  label's true appearance. Deferred: passing as is; low priority.
  Depends on nothing. Note the human judged it on a look that was double-blended (see the
  batching entry in `next-steps.md`), so a re-check after that fix is owed.
- ~~**`draw_particles` and `draw_ribbons` share the world-text backend shape.**~~ — SHIPPED 2026-10-24, see [record](../record/2026-10.md#draw_particles-and-draw_ribbons-share-the-world-text-backend-shape).
- **World labels are not depth-sorted for blending.** The world-text batch preserves
  `for_each` order, which is dense order over a sparse set and unstable across an unrelated
  entity's destruction. Overlapping labels in different fonts, or the same font, may blend in
  a different order after such a change. Back-to-front by camera distance would make it
  deterministic. Deferred: no scene overlaps two labels; the existing behaviour was the same.

## Project templates

- **3rd-person adventure template content** — needs a new `Health`
  component, character controller, basic combat/AI scripting. Real design
  work, explicitly tracked as deferred.
- Template parameterization (setup wizard, prompted values) beyond
  verbatim copy.
- ~~**Remove now-unused `types` param from `create_project`**~~ — SHIPPED 2026-09-20, see [record](../record/2026-09.md#remove-now-unused-types-param-from-create_project).

## Editor / Studio

- ~~**The editor should start STOPPED, not playing.**~~ — SHIPPED 2026-10-10, see [record](../record/2026-10.md#the-editor-should-start-stopped-not-playing).

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

- ~~**The editor forwards no pointer input to the scene, so nothing in it can be clicked**~~ — SHIPPED 2026-10-07, see [record](../record/2026-10.md#the-editor-forwards-no-pointer-input-to-the-scene-so-nothing-in-it-can-be-clicked).

  Depends on nothing technically; it needs the play-mode-versus-edit-mode
  answer first.

- ~~**In-editor "File → Reload"**~~ — SHIPPED 2026-09-20, see [record](../record/2026-09.md#in-editor-file--reload).
- ~~**Studio has zero persisted preferences**~~ — SHIPPED 2026-09-20, see [record](../record/2026-09.md#studio-has-zero-persisted-preferences).
- ~~**Plugin discovery doesn't follow the project dir, only CWD**~~ — SHIPPED 2026-09-20, see [record](../record/2026-09.md#plugin-discovery-doesnt-follow-the-project-dir-only-cwd).

## Scripting

- Same-frame writes / per-script inline override on threaded scripts
  (deliberately excluded — GIL/race reasons, likely permanent).
- Entity lifecycle (`create_entity`/`destroy_entity`) from a threaded
  script — unsupported, documented limitation.

## Physics pillar (gaps found 2026-09-29 while trying to demo specular)

Both were discovered while trying to get the demo sphere rolling so a
specular highlight could be seen sweeping across it. Neither is a bug in
anything shipped; they are capabilities that were never built.

- ~~**No way to apply an impulse or set a velocity.**~~ — SHIPPED 2026-09-29, see [record](../record/2026-09.md#no-way-to-apply-an-impulse-or-set-a-velocity).
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
- ~~**Destroying an entity never destroys its physics body**~~ — SHIPPED 2026-10-02, see [record](../record/2026-10.md#destroying-an-entity-never-destroys-its-physics-body).
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
- ~~**The `[script]` SIGSEGV**~~ — FIXED 2026-10-03, see [record](../record/2026-10.md#the-script-sigsegv).

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
- ~~**`raycast()`'s script binding indexes its sequence arguments without a length check**~~ — SHIPPED 2026-10-03, see [record](../record/2026-10.md#raycasts-script-binding-indexes-its-sequence-arguments-without-a-length-check).
- ~~**Five small `ScriptWorker` tidy-ups found in review 2026-09-30**~~ — ADDRESSED 2026-10-04, see [record](../record/2026-10.md#five-small-scriptworker-tidy-ups-found-in-review-2026-09-30).
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
- ~~**`PhysicsSystem::update()` emits `ContactEvent` synchronously while holding `backend_mutex_`**~~ — SHIPPED 2026-10-01, see [record](../record/2026-10.md#physicssystemupdate-emits-contactevent-synchronously-while-holding-backend_mutex_).
- ~~**A spurious watchdog trip in the contact-reentry tests degrades into a use-after-free**~~ — SHIPPED 2026-10-03, see [record](../record/2026-10.md#a-spurious-watchdog-trip-in-the-contact-reentry-tests-degrades-into-a-use-after-free).
- ~~**No ctest `TIMEOUT` property anywhere in the suite.**~~ — SHIPPED 2026-10-07, see [record](../record/2026-10.md#no-ctest-timeout-property-anywhere-in-the-suite).
- ~~**`PhysicsSystem::raycast is callable from another thread while update runs` is flaky on CI, and the flakiness is in the test's design.**~~ — FIXED 2026-10-09, see [record](../record/2026-10.md#physicssystemraycast-is-callable-from-another-thread-while-update-runs-is-flaky-on-ci-and-the-flakiness-is-in-the-tests-design).
- ~~**Nine existing `TEST_CASE` names contain commas, so none of them can be run by name.**~~ — WITHDRAWN 2026-10-09, see [record](../record/2026-10.md#nine-existing-test_case-names-contain-commas-so-none-of-them-can-be-run-by-name).
- ~~**A guard against `TEST_CASE` names that break ctest discovery.**~~ — SHIPPED 2026-10-09, see [record](../record/2026-10.md#a-guard-against-test_case-names-that-break-ctest-discovery).
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
- ~~**`raycast()` still runs off the script-worker thread**~~ — SHIPPED 2026-09-30, see [record](../record/2026-09.md#raycast-still-runs-off-the-script-worker-thread).
- **Two thin spots in the new physics-binding tests.** Found 2026-09-30 in
  review, both real but neither load-bearing today. (a) The demo ball-pusher
  test asserts only `fabs(delta) > 0.05`, which would not distinguish the
  intended origin-seeking oscillation from a wrong-sign runaway that rolls the
  sphere off the platform — bounding the sphere's `|x|` would pin the actual
  intent. (b) `make_physics_apply_impulse`'s `world.is_valid(e)` guard is
  unproven: removing it fails nothing, because the only invalid-id test uses
  an id that also fails `has_body`. A test that destroys a bodied entity and
  pushes it in the same tick would make it load-bearing.
- ~~**Script-thread impulses are disabled, not buffered**~~ — SHIPPED 2026-09-30, see [record](../record/2026-09.md#script-thread-impulses-are-disabled-not-buffered).
- ~~**A dict passed to `apply_impulse` raises instead of returning `False`**~~ — SHIPPED 2026-10-04, see [record](../record/2026-10.md#a-dict-passed-to-apply_impulse-raises-instead-of-returning-false).
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
- ~~**`app/main.cpp`'s system wiring is untestable**~~ — SHIPPED 2026-09-29, see [record](../record/2026-09.md#appmaincpps-system-wiring-is-untestable).
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
- ~~**Static bodies cannot be created rotated**~~ — SHIPPED 2026-09-29, see [record](../record/2026-09.md#static-bodies-cannot-be-created-rotated).

## Tech debt

- ~~**Issue #25 — Component TypeID densification**~~ — SHIPPED 2026-09-20, see [record](../record/2026-09.md#issue-25--component-typeid-densification).
- ~~**`LiveLinkServer` tests hard-code listen ports, and one flaked on CI**~~ — SHIPPED 2026-09-30, see [record](../record/2026-09.md#livelinkserver-tests-hard-code-listen-ports-and-one-flaked-on-ci).
- ~~**`enet_transport_test.cpp` hard-codes six UDP ports too (34001-34006).**~~ — SHIPPED 2026-10-24, see [record](../record/2026-10.md#enet_transport_testcpp-hard-codes-six-udp-ports-too-34001-34006).
- ~~**Embedded-Python worker-thread flakiness (ScriptSystem/ScriptWorker)**~~ — FIXED 2026-09-30, see [record](../record/2026-09.md#embedded-python-worker-thread-flakiness-scriptsystemscriptworker).

## CI and build

Deferred from the CI build-caching design, `docs/superpowers/specs/2026-10-20-ci-build-caching-design.md`
(2026-10-20). Each entry says why it was not folded into that slice. Measurements quoted are
from that spec; `ctest` and wide-header figures are local or inferred where marked.

- ~~**`ctest -j` in CI**~~ — SHIPPED 2026-10-21, see [record](../record/2026-10.md#ctest--j-in-ci).
- **GitHub Actions Node.js 20 deprecation.** Every run now carries an annotation that
  `actions/cache/restore@v4`, `actions/cache/save@v4` and `actions/checkout@v4` target Node.js 20
  and are being forced to Node.js 24 (seen in the post-merge runs, 2026-10-21). Nothing is broken,
  but a floating `@v4` can start behaving differently under a different Node, and the day the
  forced-run fallback is removed the workflow needs the actions bumped. Found while checking
  annotations on the cache re-run; unrelated to caching. Deferred: no action is needed until the
  actions publish a Node 24 version.
- **Release-preset build caching.** CI builds only the `debug` preset, so there is nothing to
  cache. Deferred until a release job exists; the ccache bucket key would need the build type's
  compiler flags to differ only via ccache's own hash (they do), so the design extends cleanly.
- **Caching the apt layer.** Rejected on current evidence rather than merely deferred: the
  installs are about 20-30 s of a run, and a cached layer risks pinning stale package lists
  against the third-party LunarG apt repository. Recorded so it is not re-derived. Revisit only if
  the apt steps become a large share after the build is cached.
- **A source-only FetchContent cache.** Not needed if `GIT_SHALLOW TRUE` lands the clone saving
  (9 tag-pinned deps: 45.3 s full vs 4.9 s shallow, local). Would also freeze whatever a mutable
  tag pointed at. Revisit if shallow clones disappoint on CI.
- **Test selection / caching test results.** Skipping tests for unchanged code is unsound without
  a change-impact analysis the repo does not have. Distinct from `ctest -j`, which only runs the
  same tests faster. 🔴 and low priority.
- **Per-PR cache writes.** Decided restore-only so PR saves cannot churn the ~10 GB LRU pool and
  evict `main`'s entry (a quota reason, not a security one: GitHub scopes PR caches to the PR).
  Cost: a PR pushed N times pays N times for its own changed objects. Revisit with hit-rate data
  from a few real PR iterations.
- **A periodic cold-vs-cached parity check.** A scheduled workflow that builds cold and compares
  sha256 of the artifacts with a cached build would turn a one-time correctness proof into a
  standing guardrail against a cache key that drifts loose. Deferred: it spends a weekly cold
  build and the ship-time invalidation probes cover the same risk. Depends on the cached build
  being reproducible on CI, which the caching slice establishes.
- **A shared/remote compiler cache for developer machines.** The shallow-clone change already
  helps a local first build; a network cache is a different trust and operations problem.
- **Cache housekeeping.** Deleting stale caches via the API from a scheduled job. Today the only
  lever against a bad cache is bumping the `v1` epoch in the key. Depends on needing it.
- **Pin mutable FetchContent tags to commit SHAs.** Found while designing: 9 of 10 dependencies
  are pinned by tag (box3d alone by SHA). A retagged upstream would change a build silently.
  Unrelated to caching but it changes the trust model of any dependency cache; GIT_SHALLOW needs
  a tag or branch, so pinning to SHAs would trade away shallow clones unless done per dependency.
  **Sharpened by the shallow clones (Slice A):** if an upstream tag is ever moved, a developer with
  an existing shallow `build/*/_deps` can hit a confusing fetch failure, because the shallow
  history does not contain the new commit. CI is immune (it always clones fresh). Recovery is
  `rm -rf build/*/_deps` and reconfigure; that is noted beside the declarations in `CMakeLists.txt`.
  Documented rather than fixed in that slice. This strengthens the case for SHA pinning.
- **Reduce header fan-out / precompiled headers / unity builds.** `foundation/core/entity_id.hpp`
  is included by 171 of 254 first-party TUs (`ninja -t deps`); editing it costs 171 recompiles
  even with a perfect cache (233 s locally vs 285 s cold). Caching cannot help this; cutting the
  fan-out can, for cold and wide-header builds alike. Different kind of work.
- ~~**The default-scene HUD-underlay case takes 16.5 s locally and 23.4 s on CI.**~~ — FIXED 2026-10-21, see [record](../record/2026-10.md#the-default-scene-hud-underlay-case-takes-165-s-locally-and-234-s-on-ci).
- ~~**ENet servers take a fixed port; the structural fix is bind 0 plus a `port()` accessor.**~~ — SHIPPED 2026-10-24, see [record](../record/2026-10.md#enet-servers-take-a-fixed-port-the-structural-fix-is-bind-0-plus-a-port-accessor).
- **`port reports an explicitly requested port verbatim` releases then rebinds** (LiveLink, and since 2026-10-23 the ENet case too).**
  It learns a free port from `start(0)`, `stop()`s, then binds that number; a concurrent
  `start(0)` or outgoing connection in that window could take it. **Accepted, not planned
  (2026-10-21 ruling):** a microsecond window against a pool of ~28 000 ports (~1/28 000 per
  overlapping bind), and the only alternative, holding the probe socket bound, makes it impossible
  to bind that number. `SO_REUSEADDR` (already set) does **not** close it: it only permits binding
  over TIME_WAIT. Revisit only if this case is ever seen red.
- **Longest-first scheduling for `ctest -j`.** **Measured and declined (2026-10-21), not
  overlooked.** CI has no `Testing/Temporary/CTestCostData.txt`, so ctest schedules in definition
  order. From CI run 39534182320: serial work before the long case (#413) is 10.71 s, so under `-j4`
  it is dispatched at ~2.7 s and the step ends at ~26.0 s; longest-first would end at 23.4 s, a gain
  of **~2.6 s**. Worth a `COST` property only if a long case ever lands late in declaration order.
- **A per-test duration budget.** `TIMEOUT 60` is a hang backstop and says nothing about a case
  that quietly grows 40x (the HUD-underlay case did, unseen, for weeks). A CI step that reads
  `ctest --output-junit` and fails or annotates any case above a stated fraction of the timeout
  would catch the next one. Deferred: needs a threshold chosen from CI data, and a flaky budget is
  worse than none. The HUD-underlay case it would have tripped on day one is fixed (2026-10-21; slowest case is now ~1.1 s), so this is unblocked.
- **`execution.jobs` in the `debug` test preset (local `ctest -j`).** The slice puts `-j` in the
  workflow only. Putting it in `CMakePresets.json` would change every developer's default, on
  machines with unknown memory and with the sandbox's slow cross-process loopback. Deferred:
  CI-only was the goal, and a local default is a separate decision.
- **A CI job that runs `mosaic_unit_tests` as one process.** The full-suite-order SIGSEGV in the
  threaded `ScriptSystem` case (see the Physics-pillar `[script]` SIGSEGV entry on `catch_discover_tests` blindness) is
  invisible to ctest by construction. A second job running the binary directly would cover it, at
  the cost of another ~100 s job. Deferred: the mode has not reproduced since #208, and fixing it
  is that entry's work. Depends on whether that mode is ever re-observed.

## Effort vs. impact

The cross-cutting view: of the live work above, **what is worth doing next**. Last derived
**2026-10-23**, from the 16 PRs merged after the previous pass's own PR (#266): #267, #268,
#271-#284. Six earlier passes (2026-09-19 to 2026-10-20) and the tier tables they edited are in
the record: [2026-09](../record/2026-09.md#effort-vs-impact-the-tier-tables-and-re-derivation-notes-2026-09-19-to-2026-09-21)
and [2026-10](../record/2026-10.md#effort-vs-impact-the-re-derivation-notes-of-2026-10-04-to-2026-10-20-and-the-tier-tables-they-edited).

**The standing rule for re-deriving (set 2026-09-20, obeyed here).** *Re-derive again once real
new work has landed since the last pass -- don't immediately re-run a fourth time expecting
different material from the same unchanged source docs.* This pass's material is the live bullets
and record entries that the 16 PRs themselves created (25 live bullets, against roughly 130 live overall, from the CI-caching, parallel
`ctest`, world-text, ENet and docs-record slices), not a re-grep of the specs' Non-goals. The
older live bullets in the subsystem sections are **not ranked here** and are not claimed to be:
they carry no new evidence. Every row below names a live bullet above by its bold title, or a PR;
a row that matched nothing was cut.

*The overall figure is deliberately approximate ("roughly 130"), not exact.* It was written as an
exact number twice during this pass and was stale both times, because every backlog edit changes
it. It is the same failure that got the `main` SHA row removed from `next-steps.md`'s State table
(#283): a figure that is wrong more often than right teaches a reader to distrust the stable ones.
Do not "fix" it back into an exact count; if you need the exact one, derive it (top-level `- `
bullets not starting `- ~~`) on the day you need it.

**Reading a row.** *Effort*: hours (one focused change with a gate) / slice (a spec-plan-implement
cycle) / multi-slice (needs its own decomposition). *Gate*: the observation that would show it done.

### 🟢 Quick wins (hours-scale, single focused change)

| Order | Item | Effort / impact | Gate | Source |
|---|---|---|---|---|
| 1 | ~~**`draw_particles` and `draw_ribbons` share the world-text backend shape.**~~ — SHIPPED 2026-10-24, see [record](../record/2026-10.md#draw_particles-and-draw_ribbons-share-the-world-text-backend-shape). | — | — | PR #275 |
| 2 | **A per-test duration budget.** | Hours. `tools/ci/check_ctest_run.py --report` already reads per-case seconds from the JUnit file; this adds one threshold. Unblocked since #274 (slowest case ~1.1 s against `TIMEOUT 60`). | Red against a synthetic JUnit with one case over the budget. | PR #273, unblocked by #274 |
| 3 | **`execution.jobs` in the `debug` test preset (local `ctest -j`).** | Hours, but **decision-gated**: it changes every developer's default on machines of unknown memory. Low impact (CI already parallel). | A human's yes/no; the edit is one preset field. | PR #273 |

### 🟡 Medium (a slice with its own spec and gate)

| Order | Item | Effort / impact | Gate | Source |
|---|---|---|---|---|
| 1 | **A checker for `next-steps.md`'s verifiable claims.** | Slice. Medium impact: this very pass found the handoff's "twelve PRs" wrong at the time it was written (it is 16, or 18 counting #265-#284). | Must go red against a deliberately wrong claim. | PR #278 |
| 2 | **Pin mutable FetchContent tags to commit SHAs.** | Slice. Reproducibility/supply chain: 9 of 10 dependencies are tag-pinned. `GIT_SHALLOW` needs a tag, so SHA pinning trades shallow clones away unless done per dependency. | A retagged upstream no longer changes a build silently. | PR #268 |
| 3 | **Reduce header fan-out / precompiled headers / unity builds.** | Slice, **measure first**. `entity_id.hpp` is in 171 of 254 TUs; a perfect cache still recompiles all of them (233 s vs 285 s cold). Caching cannot help; this can. Impact is on the developer loop, not CI. | A measured edit-one-header rebuild time before and after. | PR #271 |

### 🔴 Large (needs its own decomposition, multi-slice)

Carried from the previous pass; none gained new evidence from the 16 PRs, so none is re-rated.

| Item | Impact |
|---|---|
| **Test selection / caching test results.** | Needs change-impact analysis; low priority. |
| **Mesh/geometry sync (currently transform-only).** | Medium-High: the difference between a preview and a real DCC tool. |
| **Native `.so`/`.dll` dynamic-library plugins** | High, but reopens the native-code security surface. |
| **The RenderGraph** | Medium (enabling, not user-visible). Trigger restated in the Rendering section: passes whose existence or order depends on per-frame scene content. |
| **U8 layout containers** | U8a shipped (#260); the remainder, and U9-U11, are in the UI section. High if interactive-game UI is a near-term goal, else Low; that is still the open question. |
| **NAT traversal, encryption, compression** | High if shipping real multiplayer soon, else Low. Stands for the Networking pillar's gaps. |
| **Package manager: network fetch, version constraints/dependency** | Medium. |
| **Public marketplace** | Low today, High later: gated on an actual user base. |
| **3rd-person adventure template content** | Medium. |

### Decided: no action (with the trigger that would reopen each)

Not work to schedule. Each was weighed and declined, or is waiting on a named condition, so the
question "did we already consider this?" answers here.

| Item | Trigger |
|---|---|
| **Lime "GLOW" world label: legible but low-contrast; revisit only if it becomes a problem** | The human's call (2026-10-21, re-confirmed after #275). |
| **World labels are not depth-sorted for blending.** | Two labels ever overlap in a scene. |
| **GitHub Actions Node.js 20 deprecation.** | The actions publish a Node 24 version. |
| **Release-preset build caching.** | A release CI job exists. |
| **Caching the apt layer.** | Apt becomes a large share of a run once the build is cached. |
| **A source-only FetchContent cache.** | Shallow clones disappoint on CI. |
| **Per-PR cache writes.** | Hit-rate data from a few real PR iterations. |
| **A periodic cold-vs-cached parity check.** | The ship-time invalidation probes stop being enough. |
| **A shared/remote compiler cache for developer machines.** | A different trust and operations decision. |
| **Cache housekeeping.** | A bad cache that bumping the `v1` epoch cannot cure. |
| **`port reports an explicitly requested port verbatim` releases then rebinds** | The case is ever seen red. |
| **Longest-first scheduling for `ctest -j`.** | A long case lands late in declaration order (measured gain ~2.6 s). |
| **A CI job that runs `mosaic_unit_tests` as one process.** | The full-suite-order SIGSEGV is re-observed (it cannot occur under `ctest`). |
| **Splitting the record by subsystem instead of by month.** | Reading the record by subsystem becomes a real need. |
| **Reconciling `next-steps.md` with the agent memory directory.** | After the claims checker above exists. |
| **Backfilling record entries for work predating the convention.** | Never planned: it would be speculative narrative. |
| **Automating the archive split in the pre-commit hook** | The manual run is forgotten for more than a month or two. |
| **The eight existing `docs/updates/*-summary.md` files stay as they are** | Deferred permanently unless the archive's nav reads badly. |
