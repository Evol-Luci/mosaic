# UI Pillar — Decomposition (design)

**Date:** 2026-10-07
**Status:** decomposition only. No implementation, and deliberately so: this is the
part most worth a human's review before any code exists.
**Backlog item:** `## UI pillar (declared "COMPLETE" at U5b, but with real gaps)`,
listed 🔴 in the effort/impact tiers as *"High if interactive-game UI is a near-term
goal, else Low"*.

## Why now

That tier note made UI's priority conditional, and the condition just changed. Until
2026-10-07 a UI button could not be pressed in the editor at all — `mosaic editor`
forwarded no pointer input, so exercising UI meant `mosaic run --window`, which has no
inspector. **Viewport pointer input (PR #228) closes that**, which means the UI gaps below
are now reachable, authorable and observable in one program for the first time.

It also means **one of the listed gaps is already done.** The first bullet ends with
"editor-mode interaction provider"; that is what shipped. This decomposition removes it
rather than leaving it to be rediscovered.

## What exists today

Worth stating precisely, because two of the gaps are smaller than they read and one is
larger.

- **Components:** `UiPanel`, `UiButton`, `UiText`, `UiImage`, `UiParent`, plus
  `WorldLabel` / `WorldText` for world-space text. All reflected, so all author-visible
  in the Inspector and serialisable.
- **Anchoring and hierarchy:** `Anchor` + `resolve_rect` / `resolve_in` /
  `resolve_ui_rect` already resolve a rect against a parent frame, so parent-relative
  placement works.
- **Interaction:** `UiSystem` turns a press/release into a `ScriptEvent` and exposes
  `press_consumed()` so world-space handling can decline a click the UI claimed.
- **Text:** `layout_text` does single-line, left-to-right layout from a `FontAtlas`,
  aspect-corrected, and **skips unknown codepoints entirely**.
- **`PointerState`** carries `x`, `y`, `down` — one position, one button.

Two measurements that change the work below:

1. **`UiPanel` has an `order` field; `UiButton` does not.** `order` affects *draw*
   stacking only. `UiSystem::hit` iterates `for_each<UiButton>` and takes the **first**
   match, so hit-testing has no notion of depth at all. "z-order-aware overlap
   resolution" is therefore not "sort the existing key" — it needs a key on the
   interactive component first, and a decision about whether a button inherits its
   panel's.
2. **The neutral `RenderDevice` exposes no scissor.** The Vulkan backend sets a
   full-extent `VkRect2D` internally (`vulkan_render_device.cpp:1833`), but nothing in
   `engine/render/render_device.hpp` can ask for a sub-rect. Clipping and scroll regions
   are therefore a **render-device change before they are a UI change** — the single most
   under-estimated item on the list.

## The sub-projects

Six, each independently shippable and each leaving the engine in a working state. The
groupings are by **shared machinery**, not by theme, which is why "text input fields"
sits with keyboard navigation rather than with the other text items.

### U6 — Interaction depth

Hover enter/exit with visual feedback, pointer capture/drag, and z-order-aware overlap
resolution.

**Why first.** It is what makes the UI that already exists feel like UI. A button with no
hover state reads as broken even when it works, and it is the gap a user meets in the
first five seconds. It is also the only group whose prerequisite (a live pointer in the
editor) just landed, so it can be developed and *seen* in one program.

**Shape.** Hover is a second state alongside `press_consumed` — enter/exit are edges, so
`UiSystem` needs per-entity previous-hover state, the same shape its `was_down_` already
has. Drag needs *pointer capture*: once a press lands on an entity, subsequent frames must
route to that entity even when the cursor leaves its rect, which is a routing rule rather
than new geometry. Z-order needs the `order` decision from measurement (1) above.

**Open question for the human:** does `UiButton` get its own `order`, or inherit the
`order` of the nearest `UiPanel` ancestor through `UiParent`? Inheriting is less to author
and matches how draw order already works; an explicit field is more predictable and does
not make hit-testing walk the hierarchy. I lean inherit-with-override.

### U7 — Image presentation

Aspect-preserving fit/fill, 9-slice scaling, texture atlas sub-rects.

**Why second.** Cheapest real group, no new machinery, and entirely independent of
everything else — so it can be done whenever, and is the natural filler slice. Sub-rects
are UVs threaded from `UiImage` to the quad; 9-slice expands one quad into nine; fit/fill
is arithmetic on the resolved rect using the image's own aspect.

**Note:** 9-slice multiplies quad count per image by nine, which is the first thing in the
UI path that could matter for batching. Worth measuring, not worth pre-optimising.

### U8 — Layout containers

Inherited visibility/alpha, flow/grid/stack auto-layout.

**Why before clipping.** Auto-layout is a *pass* that computes rects before resolution,
which is additive to the existing `resolve_ui_rect`. Inherited visibility/alpha is a
hierarchy walk over machinery `UiParent` already provides. Neither needs the renderer.

**Shape.** A layout component on the parent (`UiStack` / `UiGrid` / `UiFlow`) that
overrides its children's `pos` during a pre-pass. The honest tension: children's authored
`pos` becomes ignored under a layout parent, which is surprising unless the Inspector
shows it. Worth deciding how that is communicated.

### U9 — Clipping and scroll regions

**Separated from U8 deliberately, and this is the decomposition's main claim.** Clipping
is not a layout feature here — it is a **render-device feature**. It needs a scissor
sub-rect on the neutral `RenderDevice` interface, implemented in the Vulkan backend,
threaded through the UI draw path, and intersected down the hierarchy. Scroll is then
clipping plus an offset plus wheel input, which `PointerState` does not carry.

Bundling this with U8 would hide a renderer change inside a UI slice, and the pattern this
project keeps paying for is exactly that: a slice whose risk lives somewhere other than
where its name suggests.

**Depends on:** a scissor hook on `RenderDevice`; scroll additionally depends on wheel
input, i.e. on the deferred `PointerState` widening.

### U10 — Keyboard, gamepad, and text input

Keyboard/gamepad navigation and text input fields.

**Blocked, and the blocker is already recorded.** `setup_runtime` takes an
`input_provider` that `app/run/editor.cpp` leaves empty — the same gap pointer input just
closed, one surface along, recorded in the backlog on 2026-10-07 as "Keyboard input to the
scene from the editor". Until that exists, a text field cannot be typed into in the editor
and navigation cannot be driven, so this group would ship unexercisable.

**Shape once unblocked.** Navigation needs a focus concept — which entity has it, how it
moves (explicit neighbours vs. geometric nearest), and how focus interacts with the hover
and capture states U6 introduces. Text input needs a caret, selection, and editing
semantics, which is the largest single item on the whole list and plausibly its own
decomposition.

### U11 — Text quality

Multi-line and word-wrap, kerning, non-ASCII/Unicode, SDF/multi-size atlases.

**Last, and split internally.** Word-wrap is the cheap half and is reachable now:
`layout_text` already measures advance widths, so wrapping is a break-point search over
measurements it computes. Unicode is a correctness issue with a sharp edge —
`layout_text` currently **skips unknown codepoints entirely**, so a string with any
non-atlas character silently renders shorter rather than showing a fallback glyph, which
is the kind of failure that reads as a layout bug.

SDF is the expensive half: a different atlas generation pipeline, a different fragment
shader, and a decision about whether it replaces or coexists with the bitmap path. It is
the one item here that genuinely resembles the PBR arc in shape, and if it is taken it
should get its own decomposition rather than riding inside U11.

## Ordering

```
U6 interaction depth ──┬── U8 layout containers ── U9 clipping/scroll
  (pointer input ✓)    │        (no renderer)         (NEEDS a device scissor)
                       │
                       └── U10 keyboard/gamepad/text input
                                (BLOCKED on an editor input provider)

U7 image presentation ── independent, any time

U11 text quality ── word-wrap + Unicode reachable now; SDF wants its own decomposition
```

**The argument, not just the order.** U6 first because its prerequisite just landed and it
is the most visible gap. U7 is independent and cheap, so it fills any gap in the schedule.
U8 before U9 because U8 needs nothing new and U9 needs the renderer. U10 is parked until
its blocker clears, rather than started and left unexercisable. U11 last because its cheap
half is small and its expensive half (SDF) should not be smuggled in beside the cheap half.

## Decisions fixed by this decomposition

- **Clipping is a renderer slice, not a layout slice.** U9 is separate from U8 for that
  reason alone.
- **U10 is not started before its input provider exists.** Shipping navigation that cannot
  be driven in the editor repeats the mistake pointer input just fixed.
- **SDF does not ride inside the word-wrap slice.** If wanted, it gets its own
  decomposition.
- **"Editor-mode interaction provider" is struck from the gap list**, shipped by PR #228.

## Non-goals of this decomposition

- **Choosing the hit-test `order` semantics.** Flagged as U6's open question; it wants a
  human's preference, and I have stated mine.
- **Any implementation.** No slice here has a spec yet; each gets its own
  brainstorm → spec → plan → TDD cycle, as every other decomposed arc in this project has.
- **Re-rating the 🔴 tier.** The effort/impact table says UI is "High if interactive-game
  UI is a near-term goal, else Low". That is still the user's call; this decomposition only
  removes the excuse that the item is too shapeless to start.
- **World-space interactive UI** (non-billboard labels, occlusion testing, distance fade).
  Deliberately left out of all six groups: it shares almost nothing with screen-space UI —
  it needs depth testing and a world-space hit path, not rect resolution — and folding it
  in would make a seventh group that is really a different feature wearing the same name.
  Recorded in the backlog as its own item.
