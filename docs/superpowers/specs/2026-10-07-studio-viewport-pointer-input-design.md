# Studio — Pointer Input from the Viewport to the Scene (design)

**Date:** 2026-10-07
**Status:** approved design (the behaviour question was answered by the user), not implemented
**Backlog item:** "Pointer input from the editor viewport to the scene"
(`docs/project/deferred-work-backlog.md`), added 2026-10-06, the only 🟡 entry.

## Intent

Today, manipulating a scene and exercising its scripts happen in two different programs.
`mosaic editor` can select entities and edit their components live, but a click in its
viewport never reaches the game: nothing drives `UiSystem`, so a `UiButton` cannot be
pressed, and `ClickNudgeSystem` is not registered at all. `mosaic run --window` can do
both of those and has no inspector. This closes that gap.

**The behaviour question, which the backlog flagged as needing an answer before any code,
is answered: play mode only.** A viewport click drives the game's pointer while the
editor is playing, and means "select this entity" while it is not.

## The gap, measured

1. `setup_runtime`'s `pointer` parameter (`app/run/demo_scene.hpp:198`) defaults to `{}`,
   and `app/run/editor.cpp:158` does not pass one. The editor's `UiSystem` therefore holds
   an empty provider and sees `PointerState{}` — x=0, y=0, down=false — forever.
2. `ClickNudgeSystem` is never registered in the editor: `register_windowed_systems`
   (`app/run/windowed_systems.hpp`) is called only from `app/main.cpp:344`.

## What already exists, which changes the shape of the work

`StudioApp::draw_viewport` (`studio/studio_app.cpp:352`) **already** converts the mouse to
panel-relative coordinates and raycast-picks on `ImGui::IsItemClicked()` —
`GetItemRectMin()`, `GetMousePos()`, the panel size, then NDC, then `pick_entity`. The
coordinate conversion is not new work. It exists, for edit-mode selection.

That is what makes the play-mode-only decision cheap: the existing click-to-select **is**
the edit-mode behaviour and stays as it is.

## Consequence to state loudly: selection stops on viewport clicks during play

If the new provider reports presses during play while the existing pick also runs, one
click both drives the game pointer and moves the selection. So **the existing pick is
gated on `!play.is_playing()`**. That is a behaviour change to shipped code, not an
addition: clicking the viewport no longer selects while the editor is playing.

Selection does not become unreachable — the Hierarchy panel still selects by click, and
the Inspector still edits the selected entity — but the change is real and a user who
relied on viewport selection mid-play will notice.

## The seam: `ViewportPanel` records, the provider queries

`ViewportPanel` (`studio/viewport.hpp`) is the headless model, already unit-tested
(`tests/unit/studio/viewport_test.cpp`), and already shaped record-then-query
(`set_measured_size` / `size()` / `needs_resize()` / `mark_applied()`). Pointer state
belongs there by the same pattern, which is what makes it testable with no ImGui and no
window.

```cpp
// The raw per-frame facts, straight from ImGui. No policy.
struct ViewportPointerSample
{
    float mouse_x = 0.0f, mouse_y = 0.0f; // screen space, ImGui's GetMousePos
    float rect_min_x = 0.0f, rect_min_y = 0.0f; // the viewport image's top-left, screen space
    bool  hovered = false;          // ImGui::IsItemHovered on the viewport image
    bool  imgui_capturing = false;  // ImGui::GetIO().WantCaptureMouse
    bool  button_down = false;      // the raw left button, ungated
};

void ViewportPanel::set_pointer_sample(const ViewportPointerSample&);

// Policy, applied here so it is testable: panel-relative normalisation plus every
// suppression rule. `play_active` is passed rather than stored because PlayState is
// owned by StudioApp and this type must stay free of it.
engine::ui::PointerState ViewportPanel::pointer_state(bool play_active) const;
```

Tier check: `studio -> engine` is allowed, so `studio/viewport.hpp` may include
`engine/ui/pointer.hpp`. `tools/ci/check_dependency_rules.py` enforces the direction.

## The rules `pointer_state` applies

`x`/`y` are always the panel-relative position, normalised to `[0,1]` with a **top-left**
origin, because that is what `PointerState` documents and what `UiButton` rects use. They
are clamped to `[0,1]`, matching what `main.cpp`'s GLFW provider already does.

`down` is true only when **all** of these hold:

- `play_active` — the decision above;
- `hovered` — the cursor is over the viewport image, not another panel;
- *(originally `!imgui_capturing`, fed from `ImGui::GetIO().WantCaptureMouse` — **removed
  2026-10-07 after the branch review found it made `down` unreachable.** That flag reads
  like "ImGui wants the mouse for a widget" but `imgui.cpp:5184` defines it as true
  whenever the cursor is over ANY ImGui window, and the Viewport panel is one — so pairing
  it with `hovered` was mutually exclusive and shipped the feature dead. `hovered` alone
  covers the cases this condition was written for: `IsItemHovered()` is false when another
  item is active, such as a slider being dragged (`imgui.cpp:4581`, which excludes
  `MoveId`/`TabId` so a drag inside the viewport still counts), false when the viewport
  window is behind another, and false under an open modal.)*
- `button_down` — the raw button is held;
- **and the press began while the first three already held.**

**That last rule is not obvious and omitting it produces a real bug.**
`ClickNudgeSystem` fires on a press *edge* (`p.down && !was_down_`,
`app/run/click_nudge.hpp:51`). Without the rule, holding the left button in edit mode and
then clicking Play flips `down` from false to true with no new physical press — a
synthetic click the user never made, landing wherever the cursor happens to be. The same
thing happens when dragging a slider and releasing over the viewport. So the panel latches
"this press started while ineligible" and suppresses `down` for the remainder of that
press, until the raw button goes up.

## Wiring in `editor.cpp`

One lambda, shared by both systems:

```cpp
auto viewport_pointer = [&studio] {
    return studio.viewport().pointer_state(studio.play_state().is_playing());
};
```

Passed to `setup_runtime` (so `UiSystem` gets it) and to `register_windowed_systems` (so
`ClickNudgeSystem` gets it), with `register_windowed_systems(world, /*windowed=*/true, …)`
— the editor always has a window.

**Both systems must see the same value within a tick, which is why the sample is cached
rather than read live.** ImGui's `GetMousePos` is frame-snapshotted, but
`glfwGetCursorPos` is not: reading GLFW directly could hand `UiSystem` and
`ClickNudgeSystem` different positions inside one tick, and the UI-claim arbitration would
then be deciding about a different click than the one it declined. Reading the cached
`ViewportPointerSample` makes that impossible by construction.

`ClickNudgeSystem` must still be registered **after** `UiSystem` so `ui_claimed()` reflects
the UI's verdict on the same press. `register_windowed_systems` is called after
`setup_runtime`, which satisfies it, and `windowed_systems.hpp:40` already documents why.

`aspect` must be the **viewport panel's** aspect (`studio.viewport().aspect()`), not the
window's. The scene renders into a panel-sized offscreen target, so a window-derived
aspect would make `ClickNudgeSystem`'s camera ray diverge from the image the user clicked
— picking a different object the further the cursor is from centre, and only when the
panel's aspect differs from the window's, which is always.

## The one-frame latency, inherited not invented

`on_frame` (`app/run/editor.cpp:193`) runs: `glfwPollEvents` → play/loop control and the
tick → `transform_system` → render passes → `begin_ui_frame` / `draw_ui` / `end_ui_frame`.

The world ticks **before** `draw_ui`, so the sample a tick reads was captured during the
previous frame's draw — one frame, ~16ms at 60Hz. This is the same latency `ViewportPanel`
already documents for sizing ("the size measured during frame N feeds the offscreen pass
of frame N+1"), so it is precedent rather than a new compromise. It is documented in the
header rather than left for someone to rediscover. Reordering the frame to remove it is
out of scope and would move the render passes relative to the tick.

## Testing

Headless, against `ViewportPanel`, in `tests/unit/studio/viewport_test.cpp`:

1. **Panel-relative normalisation.** A sample at the rect's centre gives (0.5, 0.5); the
   top-left corner gives (0, 0); the bottom-right gives (1, 1). Top-left origin, so a
   cursor in the panel's upper half must give `y < 0.5` — the Y-flip is the easy thing to
   get wrong and a flipped provider still "works", it just drives the mirrored button.
2. **Out-of-rect positions clamp to `[0,1]`** rather than going negative or past 1.
3. **`down` requires every condition**, one case per condition: not playing; not hovered;
   ImGui capturing; button up. Each asserts `down == false` with the others satisfied, so
   a dropped `&&` fails exactly one case rather than none.
4. **The press-began-while-ineligible latch**, which is the subtle one:
   - button held, not playing → `down` false;
   - then playing with the button still held → `down` **still false** (no synthetic click);
   - release → still false;
   - press again while playing and hovered → `down` true.
5. **Coordinates are reported even when `down` is false**, because hover-driven UI
   (`UiButton` highlighting) needs the position without a press.
6. **A degenerate panel size** (1x1, the floor `to_extent` enforces) does not divide by
   zero or produce NaN.

Wiring, in `tests/unit/app/`:

7. **`register_windowed_systems` registers `ClickNudgeSystem` for the editor path.** That
   function exists specifically to make a registration decision testable — its header
   records that click-to-nudge once shipped dead behind a guard that never fired, with the
   whole suite green. The editor is a second caller of exactly that mechanism.
8. **The provider reaches `UiSystem`.** `setup_runtime` with a stub provider, then assert
   a `UiButton` under the reported position with `down` true emits its `ScriptEvent` —
   end-to-end through the real `UiSystem` rather than asserting the lambda was stored.

**Mutation probes, derived from the code** (the lesson from the multi-scatter review): drop
each of the four `&&` conditions; drop the latch; flip the Y normalisation; swap the
`aspect` provider to the window's; register `ClickNudgeSystem` before `UiSystem`; remove
the `!is_playing()` gate on the existing pick.

**Honest limit, and it bit this slice exactly as written.** No test here can show that
ImGui actually reports what `set_pointer_sample` is fed — `IsItemHovered` and
`GetItemRectMin` are called from `draw_viewport`, which needs a window and a GPU. The
policy is fully covered; the lines that *read* ImGui are covered only by a human clicking
in the viewport.

**That limit is not theoretical.** The first version of this slice fed the gate
`WantCaptureMouse`, which made `down` unreachable, and all thirteen tests were green
because every one of them supplies the sample by hand. A textual pin made it worse by
asserting the wrong call was present. The pin is now inverted — it *forbids*
`WantCaptureMouse` — which is the only automated guard that can exist here, and the
general lesson is that a textual pin asserts a line exists, never that it is correct. That is the same limit every Studio slice has had, and it is why
the acceptance check below is a manual one.

## Acceptance (manual, needs a human at the window)

With `mosaic editor`: in edit mode, clicking the viewport selects entities as before and
the demo's UI button does nothing. Press Play: clicking the button fires its script, and
clicking the sphere nudges it. Clicks over the Inspector or Hierarchy never affect the
scene. Hold the button in edit mode, press Play, and nothing is clicked until a fresh
press. Stop: selection works again.

## Non-goals

- **Pointer input in edit mode.** The decision above. If it is ever wanted, it needs its
  own arbitration rule between "select" and "game pointer", which is the question that
  made this item 🟡 in the first place.
- **Keyboard input to the scene from the editor.** `setup_runtime` takes an
  `input_provider` that the editor also leaves empty. Same shape of gap, different
  surface, and it has no 🟡 blocker to resolve — recorded in the backlog.
- **Right-click, middle-click, scroll, and multi-touch.** `PointerState` carries a single
  `down` for the primary button. Widening it is a change to an engine-tier type with its
  own consumers.
- **Removing the one-frame latency.** Would require reordering `on_frame` so the UI draw
  precedes the tick, which moves the render passes too.
- **A viewport camera independent of the scene camera.** The editor renders through the
  scene's active camera, so "where the user clicked" and "what the game sees" are the same
  ray by construction. An editor fly-camera would break that and is its own feature.
