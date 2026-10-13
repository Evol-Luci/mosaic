# UI pointer capture and drag — design

**Status:** spec, written 2026-10-12. Closes U6 (interaction depth), whose other two
thirds — z-order hit resolution and hover enter/exit — shipped in #236.

## Why

The U6 decomposition names the shape:

> Drag needs *pointer capture*: once a press lands on an entity, subsequent frames must
> route to that entity even when the cursor leaves its rect, which is a routing rule
> rather than new geometry.

Half of that routing rule already exists. `UiSystem::pressed_` persists across frames, and
a click fires only when the release lands on the same button — so a press already "belongs"
to a button for as long as it is held.

What is missing is the other half and the events on top of it:

1. **Hover ignores the press.** `ui_button.hpp` documents this as deliberate: "Hover is
   independent of the press state: it tracks the pointer during a drag that began
   elsewhere." So dragging off button A onto button B lights up B while the click, if it
   lands, still belongs to A. The visual feedback contradicts where the click will go.
2. **There is no drag event at all.** A script can learn that a button was clicked, entered
   or exited, and nothing about the pointer moving while held.

## What capture means here

**While a press is held by a button, that button owns the pointer.** Concretely:

- No other button hovers. Hover resolution considers only the captured entity.
- The captured button is hovered **only while the cursor is inside its own rect**, so it
  un-highlights when you drag off and re-highlights when you drag back. That is what a
  desktop button does, and it is the affordance that tells a user whether releasing here
  will fire it.
- Click semantics are **unchanged**: release inside the captured button still emits
  `click`.

This changes shipped behaviour, so `ui_button.hpp`'s comment is corrected rather than left
contradicting the code.

## The events

Four new actions on the button's existing event channel, beside `click`, `hover_enter` and
`hover_exit`:

| Action | When |
|---|---|
| `drag_start` | the pointer has moved past the threshold while captured — once per press |
| `drag` | each subsequent frame the pointer moves while captured |
| `drag_end` | the press is released while the pointer is still on the surface |
| `drag_cancel` | the pointer leaves the surface while captured |

**`drag_start` fires on movement, not on press.** If it fired on press, every ordinary
click would also emit `drag_start` and `drag_end`, and a handler could not tell a click
from a one-pixel drag. A press that never moves past the threshold emits none of these —
it is a click and nothing else.

**The threshold is `0.01` in normalized screen units**, Euclidean. On a 1920-wide window
that is about 19px horizontally and 10px vertically — **anisotropic, and accepted**. A
pixel-accurate threshold needs the screen aspect, which `UiSystem` does not have and would
have to be threaded in the way `resolve_image_quad` takes it. Recorded as a backlog item
rather than widening this slice's surface.

**A drag still ends in a `click` when released inside.** Pressing, wiggling and releasing on
a button is a click to any user, and suppressing it would change shipped behaviour for an
input nobody considers a drag. A handler that wants drag-only behaviour filters on `action`,
which every handler must already do. The alternative — suppress `click` once `drag_start`
has fired — is recorded as the road not taken.

### Payload

`drag_start`, `drag` and `drag_end` carry the pointer position:

```
{ entity, action, x, y }
```

`drag_cancel` carries `{ entity, action }` only: the pointer's position at the moment it
left the surface is clamped to the panel edge by the provider, so reporting it would be
reporting a coordinate the user never pointed at.

`click`, `hover_enter` and `hover_exit` keep their existing `{ entity, action }` shape
exactly. Widening them would be a change to a payload three shipped handlers already read.

## Leaving the surface is not a release

The editor's pointer provider **deliberately drops `down` when the cursor leaves the
viewport panel** (`studio/viewport.hpp`): "the cursor leaving the panel mid-press must stop
reporting `down` immediately". That opposes OS-level capture, which would hold the pointer
until the physical button comes up — and the provider is right to, because the rest of the
editor window belongs to ImGui and a drag must not keep running while the user clicks in
the Inspector.

So capture here is bounded by the pointer's *presence*, and the two endings are
distinguishable without any new plumbing:

| `down` | `inside` | Meaning | Emits |
|---|---|---|---|
| false | true | released on the surface | `drag_end` if a drag started, plus `click` if the release is inside the captured rect |
| false | false | the pointer left the surface | `drag_cancel` if a drag started |

Both endings are gated on a drag having started. A press that never moved past the
threshold is a click, and a click emits no drag action at all.

That distinction is the reason `drag_cancel` exists as its own action rather than folding
into `drag_end`: aborting a drag and completing one are different outcomes, and conflating
them is where drag code goes wrong.

**A pointer that vanishes is also a cancel.** If the provider stops reporting a pointer at
all — the Viewport panel is closed or collapsed mid-drag — `inside` is false and the same
rule applies. No special case.

## Design

### 1. `UiSystem` gains three fields

```cpp
mosaic::foundation::EntityID captured_;   // who owns the pointer; nil when none
Vec2                         press_at_;   // where the press landed, for the threshold
bool                         dragging_;   // has drag_start fired for this press
```

`pressed_` is **renamed to `captured_`**, because that is now what it means: it already
persisted across frames and gated the click, and the rename is what makes the routing rule
readable. `press_consumed()` keeps its name and its meaning (a press belongs to the UI), so
`ClickNudgeSystem` and the world-space picking path are untouched.

### 2. The update order becomes capture-first

Today: resolve `top` → hover → press/release. The new order:

1. Resolve `top` from the flattened candidates, as now.
2. **If captured**, hover resolves against the captured entity alone: hovered if and only
   if `top == captured_`. If not captured, hover resolves against `top`, as now.
3. Press edge: `captured_ = top`, `press_at_ = pointer`, `dragging_ = false`.
4. While captured and `down`: if not yet `dragging_` and the pointer has moved past the
   threshold, emit `drag_start` and set `dragging_`; else if `dragging_` and the pointer
   moved at all, emit `drag`.
5. Release edge (`down` false):
   - `click` if `inside` **and the release is within the captured button's own rect**
     (`top == captured_`) — the shipped rule, unchanged. Note this is a stricter condition
     than `inside`, which only says the pointer is still on the surface.
   - `drag_end` if `inside` **and `dragging_`**; `drag_cancel` if not `inside` **and
     `dragging_`**. A press that never crossed the threshold started no drag, so it ends
     none: it emits `click` or nothing, and never a `drag_end` or `drag_cancel`.
   - Clear all three fields on every path.

Steps 4 and 5 emit through the existing `emit_button_action` helper, extended to take an
optional position. One helper still, because two would be two chances for the payload
shapes to diverge — the reason the existing comment gives for having one.

### 3. A captured entity can be destroyed mid-drag

`emit_button_action` already returns false when the entity has lost its `UiButton`, and the
hover path already relies on that. Capture needs the same discipline: if the captured
entity is no longer valid, capture is dropped silently with no `drag_end` or `drag_cancel`
— there is no channel name left to emit on, which is the same reason the hover path gives.

## Non-goals

- **A pixel-accurate drag threshold.** Needs the screen aspect in `UiSystem`; recorded.
- **Dragging the element itself.** This slice routes the pointer and emits events; moving a
  `UiPanel` in response is a script's job, or a later component.
- **Multi-touch or multiple simultaneous captures.** `PointerState` is a single pointer.
- **Capture by a non-button element.** Only `UiButton` is hit-testable, which is the same
  boundary the hover slice drew, and widening it is U6's recorded "hover on non-button
  components" item.
- **Drag between the viewport and other ImGui panels.** The provider's design forbids it,
  deliberately.
- **Suppressing `click` after a drag.** Decided against above.

## Testing

All headless. `UiSystem` takes an injected pointer, and `tests/unit/ui/ui_system_test.cpp`
already has a `Harness` with a mutable `PointerState`, event capture and
`add_overlapping(order, event)`.

**Capture and hover:**
- Pressing a button and moving off it: no other button hovers.
- The captured button un-hovers when the cursor leaves its rect and re-hovers on return.
- With no press, hover still follows the cursor across buttons (the shipped behaviour).

**Drag events:**
- A press with no movement emits `click` and no drag action at all.
- Movement past the threshold emits exactly one `drag_start`, then `drag` per moving frame.
- A frame where the pointer does not move emits no `drag`.
- Movement below the threshold emits nothing; crossing it later still emits `drag_start`.
- `drag_start`/`drag`/`drag_end` carry `x` and `y`; `click` and the hover actions do not.
- Release inside emits `drag_end` *and* `click`, in that order.
- Leaving the surface **without** having crossed the threshold emits nothing — no
  `drag_cancel`, since no drag started.
- Release outside the captured rect but on the surface emits `drag_end` and no `click`.
- The pointer leaving the surface emits `drag_cancel`, no `click`, no `drag_end`.
- A second press after a cancel starts a fresh capture (nothing leaks between presses).
- Destroying the captured entity mid-drag emits nothing further and does not crash.

**Mutation probes**, derived from the code: threshold comparison inverted; `drag_start`
emitted per frame rather than once; hover not pinned while captured; `drag_cancel` and
`drag_end` swapped; the position omitted from the payload; capture not cleared on release.

## What this cannot verify

Nothing here needs a GPU, but the *feel* of a drag — whether the threshold is comfortable,
whether the un-highlight on drag-off reads correctly — is exactly the kind of claim tests
cannot make. It goes on the next human verification pass, which is now a registry skill
(`human-verification-pass`) rather than an ad-hoc habit. The threshold value in particular
is a guess until someone drags something.
