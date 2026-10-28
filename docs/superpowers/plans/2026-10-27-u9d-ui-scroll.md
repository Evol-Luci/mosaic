# U9d — `UiScroll`: a clipped subtree with an offset — plan

Spec: `docs/superpowers/specs/2026-10-27-u9d-ui-scroll-decisions.md` (the supervisor's
rulings) and `docs/superpowers/specs/2026-10-26-u9-clipping-scroll-decomposition.md`
(`### U9d`).

---

## The oracle, chosen before any measurement

**This is written before a single number is collected, so it cannot be a rationalisation of
whatever the code turned out to do.** If any of the four statements below is observed to be
false, the slice is wrong — not the statement.

**O1 — A SCROLL MOVES THE CHILDREN AND NOTHING ELSE MOVES. (The conservation law.)**
For any element in a scrolled subtree, between offset `a` and offset `b`:

* its resolved `x`, `w` and `h` are **bit-identical** — not approximately equal, identical,
  because `resolve_in` derives all three from the frame's `x`/`w`/`h`, which a vertical
  scroll does not touch at all; and
* its resolved `y` changes by `-(b - a) * container.h` to within one float rounding of the
  reassociation `(f.y - d) + p` vs `(f.y + p) - d` (asserted at 1e-6, which is ~4000x the
  float epsilon at these magnitudes and ~0.002 of a 1080p pixel), and
* **every element in the subtree moves by the SAME delta** — a scroll is a rigid
  translation, so if two siblings move by different amounts, something in the path is
  scaling rather than translating.

**O2 — THE CLIP IS BIT-IDENTICAL BEFORE AND AFTER A SCROLL.** `resolve_clip` on a scrolled
child must return a rect whose four floats compare `==` across any change of offset,
including the extremes and including an out-of-range authored offset. This is the one gate
the decomposition states in those words, and it is bit-exact *by construction*: a
container's own rect is resolved from its ancestors, which know nothing about its offset. A
tolerance here would hide exactly the failure mode it is meant to catch.

**O3 — OFFSET ZERO IS THE PRE-FEATURE ENGINE, BIT FOR BIT.** A world containing `UiScroll`
at `offset = 0`, and a world containing no `UiScroll` at all, resolve every child rect to
the same four floats. Together with "a wheel delta with no scroll region present changes
nothing", this is what makes the feature free for every scene that does not use it.

**O4 — WHAT IS SCROLLED OUT IS NOT CLICKABLE.** A `UiButton` inside the region must stop
producing a hit candidate at exactly the offset that carries it out of the clip, and the
transition must be driven by a *scroll*, not by an authored clip. This is the U8b bug class
(a faded visual over a fully live button) and the only gate that reaches it.

**What the oracle cannot see, stated now so it is not claimed later:** nothing here observes
a pixel, and nothing here observes how the scroll *looks*. Sub-pixel bitmap-glyph shimmer
(ruling 1) is the human's eye pass and this plan makes no claim about it.

---

## Design decisions (spec section 4 — the implementer's to decide and record)

**Where the offset lives: a field on the component.** `UiScroll{ float offset; float
content; }`, reflected, so it serializes with the scene and an inspector can drag it. The
alternative — system-side state keyed by entity — would make the authored scene unable to
say "this region starts scrolled", and would be invisible to the inspector.

**Units: both fields are multiples of the container's own height.** `content = 1.8` means
"my content is 1.8 container-heights tall"; `offset = 0.4` means "scrolled down by 0.4
container-heights". One interpretation, dimensionless, so it survives a window resize and a
nested container with no second rule anywhere. The alternative (normalized screen units)
would make an authored number mean different things depending on where the container sits in
the hierarchy.

**How the content extent is measured: it is AUTHORED, not measured.** Measuring it inside
the layout path is a recursion hazard, not merely work: a child's rect needs its frame, the
frame needs the offset bound, the bound needs the content extent, and the extent needs the
children's rects. Breaking that cycle means a second parent-chain walk resolving every
sibling at offset 0, plus font metrics for any text child — which the layout path does not
have where the frame is computed. `UiGrid` already took this posture ("a grid's cells are
authored"). Deferred to the backlog with the cycle written down as the reason.

**The wheel-to-offset step is a CONSTANT, and it is derived rather than tuned:**
`kScrollNotch = 0.1f` container-heights per notch, and the demo region's labels are authored
at `height = 0.1`, which `layout_text` maps to exactly `0.1 * frame.h` of normalized screen
Y per line. **So one notch is exactly one line of text in the authored region** — that is
the derivation, and it is checkable arithmetic rather than a feel. A per-region tunable is
declined for now on `UiFlow`'s rule (one meaning everywhere; a second default is a second
thing to get wrong) and backlogged against a second region wanting a different feel.

**The clamp: `offset` is held in `[0, max(0, content - 1)]`, and it is clamped ON READ as
well as on write.** On write so the inspector shows the truth; on read because `offset` is
a reflected float on an unbounded inspector drag, and the project's standing discipline for
those is "floored rather than trusted" (`ui_stack.hpp`, `inherited_alpha`). `content <= 1`
means nothing scrolls: `max_offset` is 0 and the region is inert, which is the right answer
for content that fits.

**Who applies the wheel: `UiSystem::update`, not a new system.** The pointer provider is
already called twice per tick; a third system would make it three times per tick and up to
24 observations of one delta per frame. `UiSystem` already holds the `PointerState` for the
tick. It is a ticked system, which is what keeps a *stopped* editor from scrolling — the
landmine says the mechanism is "no tick, no provider call at all", so driving scroll from
anywhere else would scroll a stopped editor.

**Which region consumes a delta: every region whose rect contains the pointer.** With one
authored region there is no ambiguity, and chaining/arbitration is an explicit non-goal.

---

## Steps

1. Commit the spec (done) and this plan — the oracle on record before any data.
2. Capture on the base commit: `ctest --preset debug` count and status; the renderer gate
   (`timeout 12 env MOSAIC_VK_SYNC_VALIDATION=1 ./build/debug/app/mosaic editor`) verbatim.
3. `engine/ui/ui_scroll.hpp`: the component, `scroll_max_offset`, `scroll_offset_of`,
   `scroll_shift_for_children`, `apply_wheel_scroll`. Register in
   `builtin_components.hpp`.
4. Hook `resolve_parent_frame` (`ui_hierarchy.hpp`) — the one place all three draw passes
   and the hit path reach, per its own header comment. Call `apply_wheel_scroll` from
   `UiSystem::update`.
5. Tests, written against O1-O4 by name: `tests/unit/ui/ui_scroll_test.cpp` plus a
   no-region regression in the wheel path and a scrolled-button case reusing
   `button_hit_candidates`.
6. Author the demo region (spec ruling 3: clipping + scrolling + a dead scrolled-out button
   + wrapped text, all visible at once). No Transform, no script.
7. Mutations: (a) apply the shift to the clip instead of the children; (b) drop the
   clip filter in `button_hit_candidates`. Record the predicted failing assertion beside
   the actual one.
8. The renderer gate again, diffed against the base capture. This is the first time it has
   had anything to say: the authored region is the first real `vkCmdSetScissor` sub-rect in
   the project's history.
9. GPU tests (`-L gpu`), both build dirs rebuilt after every mutation restore.
10. Docs: backlog (eight non-goals, the U9 🔴 row -> COMPLETE), `next-steps.md` under 8192
    bytes, `docs/record/2026-10.md`.
