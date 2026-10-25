# U8 layout containers — decomposing the remainder

**Date:** 2026-10-25
**Parent:** [`2026-10-07-ui-pillar-decomposition.md`](2026-10-07-ui-pillar-decomposition.md), group U8
**Status:** decomposition only. No slice here is approved or planned.

## Why now

The 🔴 tier row for U8 has read *"High if interactive-game UI is a near-term goal, else
Low; that is still the open question"* since the UI pillar was decomposed on 2026-10-07.
That question is now answered — interactive-game UI is near-term — and the 2026-10-25 tier
pass found the 🟢/🟡 tables are entirely upkeep of what exists, so new capability has to
come from decomposing a 🔴. U8 is the 🔴 that needs nothing new: no renderer change, no
blocked input provider.

**This is a re-decomposition of a partially-shipped group, not a fresh one.** U8a shipped
on 2026-10-17 (PR #260) with `UiStack` and inherited alpha. Its five named successors were
each recorded in the backlog's UI-pillar section at the time, with a reason and a
dependency. This document does not discover them — it **orders** them, states what each
one's gate is, and records the two places where the surrounding evidence has changed since
they were written.

## What U8a actually shipped

Read `engine/ui/ui_stack.hpp`, `engine/ui/ui_layout.hpp` and
`engine/ui/ui_hierarchy.hpp` before planning any slice below; the division of labour they
already have is what makes the remainder small.

- **`layout_slot`** (`ui_stack.hpp`) — pure arithmetic, no ECS. Frame + axis + spacing +
  padding + index + count → a slot `Rect`. Every edge case lives here, which is why they
  are cheap to pin.
- **`stack_children`** (`ui_layout.hpp`) — the world-aware half: who a parent's children
  are, ordered by an authored key (`child_layout_order`, with the entity handle as a
  deterministic tiebreak) rather than by iteration order.
- **`slot_for_child`** (`ui_layout.hpp`) — joins the two, returns `nullopt` when the parent
  carries no `UiStack`.
- **The hook** is in `resolve_parent_frame` (`ui_hierarchy.hpp`), not in the
  `resolve_ui_rect` wrapper, because the HUD text pass calls `resolve_parent_frame`
  directly. Layout **narrows the frame**, so every downstream rule (anchor, `pos` as an
  offset, `size` as a fraction) runs unchanged inside the slot.
- **`inherited_alpha`** (`ui_layout.hpp`) — the product of ancestor panel alphas, applied
  as a render-time override that never writes back.

**The consequence for everything below:** enumerating children, ordering them
deterministically, narrowing a frame, and recursing with a depth guard are all **done**.
Grid and flow differ from stack only in slot arithmetic — one pure function in
`ui_stack.hpp`'s shape.

## Two things that changed after the successors were written

Both are findings of this pass, not of the slices.

1. **The `visible` bool is no longer blocked.** The backlog entry says *"the hit-test
   question belongs to U6"*. U6 shipped on 2026-10-10 and 2026-10-12. The dependency has
   cleared; what remains is a decision, not a prerequisite.
2. **The alpha-propagation opt-out was declined, and `ui_stack.hpp` does not know it.**
   `ui_stack.hpp`'s header comment offers two routes out of the layout-only-container trap:
   *"the deferred `visible` bool ... or an opt-out on alpha propagation; both are in the
   backlog."* The 2026-10-20 human verification pass considered an opt-out flag on
   inherited alpha and **declined it** ("fading a container should fade what is inside
   it"), recorded in `demo_scene.hpp` beside the "HP" label and in
   [`record/2026-10.md`](../../record/2026-10.md). So the comment still points at a closed
   road. **The `visible` bool is the live route**, and any slice that takes it should fix
   that comment. The decline also never reached the backlog's *Decided: no action* table,
   which is where "did we already consider this?" is supposed to answer; this pass adds it.

## The five successors, and the order

### U8b — a layout-only container (the `visible` bool)

**What.** A real `visible` bool on the UI components, so a `UiStack` can be authored as a
container with no visible chrome.

**Why first, ahead of the feature U8 is named for.** `resolve_parent_frame` returns the
whole screen when a parent has no `UiPanel`, so **a `UiStack` alone lays nothing out** — a
layout container must carry a panel. The obvious way to make that panel invisible is
`alpha = 0`, and that hides the entire subtree, because a container's alpha multiplies
down. The two workarounds that exist today (a fully transparent `color`, or an alpha low
enough to be invisible but above zero) are written into `ui_stack.hpp` precisely because
**neither is discoverable**. Every slice below authors containers, so each one pays this
trap until it is fixed. U8c would author a grid container and meet it immediately.

**The one decision, and it was the human's. SETTLED 2026-10-25: `visible = false` means
NOT DRAWN AND NON-INTERACTIVE.** `alpha = 0` hides an element but it still **hit-tests**, so
an invisible panel still eats clicks; the backlog had carried this question as the entry's
stated dependency since 2026-10-17. The alternative considered was "not drawn only", which
keeps `visible` purely a render property and leaves interactivity to a second concept later
(an `enabled` bool on `UiButton`, say).

**Why the chosen answer, written down so it is not re-argued.** "Not drawn only" would have
shipped the layout-only container *still eating clicks* — half of the recorded complaint, and
the half that reads as an engine bug rather than as a styling choice. The accepted cost is
real and should be stated plainly: this **couples the draw path and the hit path through one
field**, which is the kind of coupling U6 spent a slice untangling. So the implementation
must make that coupling explicit rather than incidental — one predicate both paths call, not
two independent `if (!visible)` checks that can drift apart. **A test must assert both halves
of the same field**: an invisible panel is not drawn *and* produces no hit candidate.

**Known landmine, now located exactly.** Adding a field to `UiPanel` re-binds positional
brace initialisers in `app/run/demo_scene.hpp`, and gcc only catches it when the types
differ. There is **exactly one** such site left: the camera's HUD-bar panel at
`app/run/demo_scene.hpp:722`. The other two `UiPanel` inits (`hud_stack` and the three slot
children) are already designated, as are `UiButton` and `UiImage`. **Convert line 722 to
designated initialisers as the first commit of the slice**, before adding the field.

**Gate.** A `UiStack` container authored with no visible chrome, whose children render at
their own authored alpha — and, per the decision, whose rect does or does not consume a
click. Plus: `ui_stack.hpp`'s "today's workarounds" paragraph is gone, replaced by the
authored way to do it.

**Depends on** the decision above. Nothing else.

### U8c — `UiGrid` and `UiFlow`

**What.** The two remaining containers U8 names. Genuinely additive: they share
`stack_children`, `child_layout_order`, `slot_for_child` and the `resolve_parent_frame`
hook, and differ only in slot arithmetic.

**Why second.** It is the capability "U8 layout containers" actually promises, and after
U8b it can be authored without meeting the invisible-container trap. It was deferred on
2026-10-17 only to keep the surface of U8a's one reserved decision — what an authored `pos`
means under a layout — as small as possible before anyone had seen a stack work. Someone
has now seen it work, and the reserved decision was answered by narrowing the frame, so
that reason has expired.

**What it needed deciding, and what is settled. SETTLED 2026-10-25 (the human's call):
`UiGrid` is authored with EXPLICIT `cols` AND `rows`.** Cells are uniform and a slot exists
whether or not a child fills it, so `slot(i) -> (i % cols, i / cols)` and a grid of 12 slots
holding 5 children keeps its shape. The alternative was `cols` only with rows derived from the
child count; it was declined for two reasons — cell *height* would then change whenever a
child is created or destroyed (an inventory grid that reflows as it empties), and it collapses
most of the distinction between `UiGrid` and `UiFlow`, which would make one of the two
redundant. **That is the line between them:** a grid has a fixed cell count on both axes; a
flow packs along one axis and wraps when full, so its line count varies with the child count.

Still open and the implementer's to settle (record the choice beside the arithmetic): flow's
wrap rule, and what happens to a **partial final line** — whether its slots size as if the
line were full (so the last row aligns with the ones above it) or divide the line among the
children present. Both are slot arithmetic, so both belong in `ui_stack.hpp`'s pure-function style where
the edge cases are cheap to pin — including the ones `layout_slot` already took a position
on: zero count, out-of-range index, negative padding or spacing floored rather than trusted,
and **never a negative extent**, because `resolve_in` multiplies by `w`/`h`.

**Gate.** Grid and flow slot arithmetic pinned as pure functions, `ctest` green, and an
authored grid in the demo scene with checkable geometry (U8a's own standard: `spacing` and
`padding` 0 and child `size` `{1,1}` means the children must *exactly* tile the container
at any window size — a seam means the division leaves a gap, an overlap means it
double-counts spacing).

**Depends on** U8a (shipped). Reads better after U8b, but is not blocked by it.

### U8d — the `parent → children` index

**What.** The `stack_children` scaling cliff: `resolve_parent_frame` runs per UI element per
frame, on **both** the draw path and the hit-test path, and now calls `stack_children`,
which is a full `for_each<UiParent>` over the world plus a sort. Roughly O(E × d × P) per
frame. The named mitigation is a per-frame memoised `parent → children` map built once per
render rather than per element.

**Why third, and this is the ordering argument this pass adds.** The backlog says *"measure
first: the index is cheap, but confirming the cost is real matters more than the shape of
the fix"*, and names the trigger as *"a UI of a few hundred elements ... invisible until
someone builds an inventory grid."* **U8c is how you build an inventory grid.** So U8c is
not merely adjacent to this item — it is the instrument that makes the measurement
possible. Taking the index before U8c would be optimising against the whole-branch review's
estimate (a 200-child stack with 500 parents ≈ 400,000 visits and ~4,000 heap allocations
per frame) instead of against a number from this engine.

Doing it after also keeps the trade honest. `slot_for_child`'s whole value is being pure and
lazy, which is what makes the draw and hit-test paths agree **by construction**; a cache is
a new correctness surface in exactly that function. Paying that before anything is slow
trades a guarantee for a guess.

**Gate.** A measured before/after on a grid of a few hundred elements authored by U8c, and
the draw path and hit-test path still agreeing — the property the cache endangers, so it is
the one the slice must assert, not just the frame time.

**Depends on** nothing technically; **ordered after U8c** so there is something real to
measure.

### Left deferred, with the blocker named

**Content-sized layout slots.** `layout_slot` divides a parent's padded rect **evenly**.
Content-sized slots need content measurement, and `UiText` has no measured height until its
font atlas is resolved — so layout would come to depend on asset loading. That is *"a
dependency worth refusing deliberately rather than acquiring by accident"* (2026-10-17), and
nothing in U8b–U8d changes it. **Depends on** a decision about whether layout may block on
assets at all. Not near-term; do not fold it into U8c.

**A layout-slot gizmo in the editor.** Authoring a `UiStack` works through reflection with
no editor work, but nothing *visualises* a slot, so an author tuning `padding` or `spacing`
reads numbers rather than seeing boxes. Independent of all three slices above and can be
taken at any time; parked because drawing a computed rect in the viewport is editor work
with its own design, and the numbers are checkable meanwhile.

**The trigger to promote it:** if authoring U8c's grid by numbers proves painful in
practice. A grid has two axes of slot arithmetic to tune instead of one, so the "numbers
are checkable meanwhile" argument is weaker for a grid than it was for a stack — but that
is a prediction, and the honest thing is to let U8c test it rather than to pre-empt it here.
Note also that the editor path is invisible to the existing gates: `ctest` plus zero
validation errors cannot see an editor-viewport-only render bug.

## Ordering

```
U8a stack + inherited alpha ✓ (#260, 2026-10-17)
      │
      ├── U8b visible bool ──── U8c UiGrid / UiFlow ──── U8d parent→children index
      │   (one human decision)  (additive arithmetic)     (measure on U8c's grid)
      │
      ├── content-sized slots ── BLOCKED on "may layout block on assets?"
      │
      └── layout-slot gizmo ──── independent, any time; promote if U8c authoring hurts
```

**The argument, not just the order.** U8b first because it is the trap U8a shipped with and
every later slice pays it, and because its only blocker turned out to be a decision rather
than a prerequisite. U8c second because it is the capability U8 names, it is additive on
machinery already proven, and the reason it was deferred has expired. U8d third because U8c
creates the thing that makes its measurement real, and because it is the only one of the
three that trades a correctness guarantee for performance — which should be paid against a
number, not an estimate.

## Decisions fixed by this decomposition

- **`visible = false` means not drawn AND non-interactive** (the human's call, 2026-10-25).
  One predicate, called by both paths; a test asserts both halves. See U8b above for the
  accepted cost.
- **`UiGrid` is authored with explicit `cols` and `rows`** (the human's call, 2026-10-25),
  which is also what keeps `UiGrid` and `UiFlow` distinct features. See U8c above.
- **U8d may legitimately ship a measurement and no cache.** Ruled by the supervisor,
  2026-10-25, because the backlog entry says "measure first" and nothing has measured yet: if
  the before/after on U8c's grid shows no cost worth a cache, the slice ships **the committed
  benchmark plus a recorded trigger** and does not add one. That is a success, not a shortfall
  — `slot_for_child`'s purity is the thing the cache would spend, and it is not spent on a
  guess. What U8d must NOT do is add the cache and report the estimate as if it were measured.
- **The alpha-propagation opt-out is closed**, declined 2026-10-20. The layout-only
  container is solved by `visible`, not by opting out of inheritance. `ui_stack.hpp`'s
  comment offering both is stale and should be corrected by whichever slice touches it.
- **Content-sized slots do not ride inside U8c.** Their blocker is a dependency decision
  about asset loading, unrelated to slot arithmetic, and folding them in would acquire that
  dependency by accident — which is the thing the 2026-10-17 deferral was protecting.
- **The `parent → children` index is not taken before the grid.** Measuring it needs a UI
  of a few hundred elements, and U8c is what builds one.
- **U9 stays separate.** Unchanged from the parent decomposition: clipping is a renderer
  slice (it needs a scissor hook on `RenderDevice`), and scroll additionally needs the
  deferred `PointerState` widening. Nothing in U8b–U8d brings either closer.
