# UI — Interaction Depth: Z-Order Hit Resolution and Hover (design)

**Date:** 2026-10-10
**Status:** approved design, not yet implemented
**Backlog item:** **U6 interaction depth**, from
`docs/superpowers/specs/2026-10-07-ui-pillar-decomposition.md` — described there as the
first of the six sub-projects, because its prerequisite (viewport pointer input) had just
landed and it is the gap a user meets in the first five seconds.

## Intent

A Mosaic HUD does nothing until it is clicked, and when two buttons overlap it is not
defined which one answers. Two consequences, both met immediately: the UI feels dead,
because nothing acknowledges the pointer; and overlapping hit regions resolve by accident.

This slice adds **z-order-aware hit resolution** and **hover enter/exit events**.

## Scope: capture/drag is split out, on an argument

U6 grouped three features. Two of them are **coupled** and one is not:

- **Z-order and hover must land together.** "Which button is hovered" has no answer without
  a tie-break. Building hover on the current behaviour would make it flicker between
  overlapping buttons, and the flicker would look like an event bug rather than a missing
  tie-break.
- **Capture/drag is separable, and is the riskiest third.** `UiSystem` already remembers
  `pressed_` and requires release inside the same button, so a partial capture exists. What
  remains has the subtlest failure modes — the pointer leaving the window mid-drag, a press
  surviving a play-mode change — and that is the exact bug class that shipped viewport
  pointer input **dead while thirteen of its tests stayed green**. Reviewing it beside two
  unrelated features is how that recurs.

Capture/drag is recorded in the backlog with its two open decisions.

## What scoping measured

Six findings. The first contradicts the backlog, and three changed the design.

1. **The backlog is right — it takes the first match — and that is actively wrong, not
   merely undefined.** `ui_system.hpp:38-45` guards with
   `if (pressed_ != EntityID{}) { return; }`, so the press records the **first** button
   under the pointer. Meanwhile the panel pass `stable_sort`s ascending and draws in that
   order, so among **equal** `order` values the entity drawn **last** is the one on top.
   First-hit and last-drawn are opposite ends of the same list.

   So today, two overlapping buttons with equal `order` send the click to the one the user
   **cannot see** — the visual on top is ignored in favour of the one underneath it. That is
   a user-visible bug with a wrong answer, not an unspecified tie-break, and it is the
   sharpest justification for this slice. (An earlier reading of this file during scoping
   concluded the opposite, that the last match won. It was wrong, and the conclusion is
   recorded here rather than silently dropped because it changes how strong the motivation
   is.)

2. **`UiButton` is the only UI component without an `order`.** `UiPanel` (`:23`), `UiImage`
   (`:36`) and `UiText` (`:27`) each carry `int order = 0`, reflected, and all three put it
   **last** in the struct. So giving `UiButton` one is closing an inconsistency, not adding
   a concept.

3. **The tie-break for equal `order` has a non-arbitrary answer.** The panel pass
   `stable_sort`s ascending and draws in that order (`render_system.hpp:415-432`), so among
   equal orders the entity **later in iteration order is the one drawn on top**. Hit-testing
   adopting the same rule makes it agree with what the user sees **by construction** rather
   than by coincidence. Implementation is a `stable_sort` then `back()`.

4. **The event protocol needs no new field.** Click already emits
   `ScriptEvent{b.event, {entity, action:"click"}}`, so hover reuses the same channel with
   `action:"hover_enter"` / `"hover_exit"`. Scripts must already filter on `action`. A
   `hover_event` field would be a second channel for the same button.

5. **`PointerState` is already sufficient.** It carries `x, y, down` (`engine/ui/pointer.hpp`),
   and hover needs only position — so this slice does not touch the pointer-state widening
   recorded for U9 (wheel, multi-button).

6. **The demo is a clean vehicle, because the HUD button and its visual are the same
   entity.** `demo_scene.hpp:668-679` puts a `UiPanel` and a `UiButton` on `camera` with
   identical anchor/pos/size, and the event payload already carries `entity`. So a hover
   subscriber can brighten that exact panel, and the order-disagreement trap below cannot
   bite in the demo.

## The decisions, and what they cost

**`UiButton` gets its own `order`** rather than inheriting its `UiPanel` ancestor's.
Consistency with the other three components, no hierarchy walk, no answer needed for a
button with no panel ancestor or several.

**The accepted cost, stated so it is not rediscovered as a surprise:** a button's `order`
and its overlaid visual's `order` **can disagree**, and nothing warns about it. A button can
claim to be above a visual that is drawn below it, and the only symptom is a click landing
on the wrong thing. Recorded in the backlog as its own item. The reason it is not fixed here
is that a validation pass needs a definition of "this visual belongs to this button", and
none exists — `UiParent` is a **transform** hierarchy, not an ownership one.

**Hover emits events only; the engine renders no feedback.** `UiButton` is deliberately
non-visual (a hit region; the visual is an overlaid `UiPanel`/`UiImage`/`UiText`) and
`UiSystem` does no rendering at all. Emitting events is exactly what the one existing
interaction already does.

**The cost of that, stated plainly:** the engine alone shows a user **nothing**. "The UI
feels dead" is only half-fixed by this slice — the other half is a script. So the demo gets
one, and the slice is not considered done until a human can see hover working in
`mosaic run --window`.

## Where the work happens, and why not inline

Hit resolution is a **pure free function** in `engine/ui/`, not inline in `UiSystem`:

```cpp
// engine/ui/ui_hit.hpp
struct HitCandidate
{
    mosaic::foundation::EntityID e;
    int                          order = 0;
    Rect                         rect;
};

// Nil EntityID when nothing is hit.
mosaic::foundation::EntityID topmost_hit(const std::vector<HitCandidate>& candidates,
                                         const PointerState& pointer);
```

`UiSystem` collects candidates from `for_each`, calls this, and keeps the state machine.

**Why extracted:** the failure mode is picking the *wrong* button among overlaps, which
produces **plausible** behaviour rather than broken behaviour — a click does something, just
not the thing under the cursor. That is invisible to review (a comparison that reads fine)
and invisible to eyeballing unless the tester knows which button should have answered. This
project has now twice extracted exactly this shape of decision —
`resolve_image_quad` (U7) and `should_start_playing` (editor-starts-stopped) — and in both
cases the extracted function is where the real bugs were found. A free function is also
testable with no world, no pointer provider and no fake.

**The condition under which this should be revisited:** if hit resolution ever needs to
consult the *hierarchy* (clipping regions from U9, or ownership-aware ordering), it stops
being a pure function over a flat candidate list and the seam has to move. Today it is flat.

## The change

**`engine/ui/ui_button.hpp`** — `int order = 0` added **last**, plus its `reflect` line.
Last for two reasons: it matches where the other three components put it, and it leaves
`demo_scene.hpp`'s positional brace init working. Adding it mid-struct would bind
`"hud_click"` to `order`.

**`engine/ui/ui_hit.hpp`** (new) — `HitCandidate` and `topmost_hit`. Header only,
`engine/ui` tier, depending on `ui_rect.hpp`, `pointer.hpp` and `foundation/EntityID`.

**`engine/ui/ui_system.hpp`** — the press path calls `topmost_hit` instead of taking the
first match. **The release path changes with it, which this spec did not originally say:**
the question becomes "is the pressed button still the *topmost* thing under the pointer"
rather than "is the pointer still inside it", so releasing onto a button that something now
covers does not click through. The candidate list is a **snapshot** taken before any event
fires, so a synchronous handler that adds a covering button during this same update does not
block that update's click, while the same change made between ticks does (deleting the `if (pressed_ != EntityID{}) { return; }` early-out); one new
member `hovered_`; and the hover transition emitted per update.

**`app/run/demo_scene.hpp`** — a `hover_enter`/`hover_exit` subscriber that brightens the
HUD bar panel's alpha (0.6 → 0.9) and restores it; the existing `UiButton` init converted to
designated initializers.

## The rules that are load-bearing

**Enter fires once.** `hover_enter` is emitted on the transition into a button and **not
again** while the pointer sits still inside it. A per-frame re-emit would be the natural
wrong implementation, and it would read as working in the demo (the panel would simply stay
bright) while flooding any script that counts events.

**Exit precedes enter when moving between buttons.** A pointer moving directly from button A
to button B emits `hover_exit(A)` then `hover_enter(B)`, in that order. The reverse order
leaves a script that tracks "the currently hovered thing" holding B and then being told to
clear A — i.e. clearing the wrong one, which looks like a lost exit event.

**Hover is independent of `down`.** It tracks the pointer during a press elsewhere. Coupling
them would mean a drag that starts on empty space suppresses all hover, which reads as the
UI freezing.

**Hover is inert in a stopped editor because the LOOP is stopped, not because of a
play-mode gate.** This is worth stating precisely, because the obvious explanation is wrong
and acting on it would ship a bug: `ViewportPanel::pointer_state` gates only `down` on
`play_active` (`studio/viewport.hpp`), never the position. What actually silences hover is
`app/run/editor.cpp` calling `loop.stop()` while stopped, so `world.tick` — and therefore
`UiSystem::update` — never runs at all.

**The consequence to keep in mind:** anything that makes the editor tick while stopped — an
authoring-time system, or a fix for `mosaic editor --ticks N` — turns hover on in authoring
mode. In this demo that means pointer movement writes `panel->alpha` into a scene the user
may then save. If that change is made, hover needs its own gate at that time.

## Testing

**`topmost_hit`, tested pure** — no world, no provider, no fake:

1. **No candidates** → nil `EntityID`. The empty HUD.
2. **One candidate, pointer inside** → that entity. **Pointer outside** → nil.
3. **Two overlapping, different `order`** → the higher `order` wins, asserted **both ways
   round** (higher-first and higher-second in the vector), because a single ordering passes
   with the comparison inverted.
4. **Two overlapping, equal `order`** → the one **later** in the vector wins, matching the
   panel pass's draw order. This is the case today's code gets **wrong** — it takes the
   first, which is the one drawn underneath — so this assertion is the regression test for
   the bug the slice exists to fix. It is also what `std::sort` instead of `stable_sort`
   would silently break, since only equal keys can be reordered.
5. **Pointer exactly on a rect boundary** → a hit. Today's `hit()` is inclusive on both
   edges (`>=` and `<=`); keeping it is deliberate, so it is asserted rather than left to
   be "fixed" later by someone who assumes it was an oversight.

**The hover state machine**, through `UiSystem` with a scripted pointer provider:

6. **Enter fires once**, not per frame: two updates with the pointer stationary inside a
   button yield exactly one `hover_enter`.
7. **Exit fires on leaving**, exactly once.
8. **Moving between two buttons** yields `hover_exit(A)` **then** `hover_enter(B)` — order
   asserted, not just presence.
9. **Hover is independent of `down`**: a press beginning outside any button does not
   suppress a subsequent hover.

**The behaviour change to shipped code:**

10. **A click now picks the topmost button**, not the **first** iterated. Two overlapping buttons
    with different `order`, one press-release cycle, and the event must carry the topmost
    entity. This is a fix to behaviour that shipped, so it gets its own case rather than
    riding along inside the hover tests.

**Reflection:**

11. **`UiButton` round-trips `order`** at a **non-default** value — a field left at its
    default round-trips whether or not it is serialised at all.

**Mutation probes derived from the CODE, not from the assertion list:** invert the `order`
comparison; take `front()` instead of `back()` after the sort; use `std::sort` instead of
`stable_sort` (which breaks the equal-order rule only); re-emit `hover_enter` every update;
swap the exit/enter emission order; drop the `hovered_` assignment so enter re-fires; make
hover respect `down`; return the first hit rather than the topmost in the click path.

## Non-goals

- **Pointer capture and drag.** Split out at the start of this slice, with its two open
  decisions (whether a captured press follows the pointer outside the window; whether a
  drag emits per-frame events or a start/end pair) recorded in the backlog.
- **Engine-rendered hover visuals** (a `UiHoverStyle` component). Needs a button→visual
  ownership relationship that does not exist, and would put interaction state into the
  render path, which has been kept free of it. The demo script is the answer for now.
- **Validating that a button's `order` agrees with its visual's.** The accepted cost of the
  `order` decision; needs an ownership definition first.
- **Hover on non-button components.** `UiPanel`/`UiImage`/`UiText` are not hit-testable at
  all today; making them so is a larger question about whether every visual is interactive
  by default, which this slice does not open.
- **Keyboard focus and navigation.** U10, and still blocked on an editor keyboard-input
  provider.
- **Wheel and multi-button pointer state.** Recorded for U9; hover needs only position, so
  this slice does not force the widening.
