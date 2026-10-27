# U9d — `UiScroll`: decisions settled before implementation

**Date:** 2026-10-27
**Status:** supervisor's rulings, written before any code, so they are not re-litigated
per-question during the slice. The slice itself is specified by the
[U9 decomposition](2026-10-26-u9-clipping-scroll-decomposition.md)'s `### U9d` section;
this file only settles what that section left open.

U9d is the last slice of U9. Its dependencies are both in: `UiClipRect` (U9b, `main`) and
`PointerState::wheel` (U9c).

---

## 1. The scroll offset is NOT snapped to whole pixels. Continuous, and the eye check decides.

The decomposition says U9d "decides the pixel-edge rounding policy". Having traced it, that
question is narrower than it sounds and the answer is "not yet", for a reason worth recording.

**What is already settled, and is not this question.** U9a shipped the *clip edge* policy:
`clip_to_pixel_rect` (`modules/render_vulkan/vulkan_ui_clip.cpp`) rounds each edge with
`llround` after clamping **each edge independently** to the render area. That is not up for
revisiting here, and U9d does not touch it — because a scroll **moves the children, not the
clip** (the decomposition is explicit: "an offset moves children and the clip does not
move"). The clip's pixel edges are therefore static during a scroll.

**So the real question is the content, and the answer is that nothing in this engine snaps
content to pixels today.** Verified rather than assumed: no `snap`/`round`/`floor` in
`engine/ui/*.hpp` or `engine/render/render_system.hpp`, and `ui.vert` maps a normalized
float rect straight to clip space (`gl_Position = vec4(nx * 2.0 - 1.0, 1.0 - ny * 2.0, …)`)
with no quantisation. A scroll offset will therefore place glyph quads at sub-pixel
positions.

**That matters because the font is a bitmap atlas, not an SDF** (SDF is explicitly out of
scope in U11). Sub-pixel positioning of bitmap glyphs resamples them, and the usual symptom
is text that looks soft and shimmers as it moves.

**The ruling: ship continuous, and make the shimmer the explicit subject of U9d's eye
check.** The alternative — snapping the offset to whole destination pixels — requires the
destination extent inside the layout pass, which is resolution-independent normalized space
by design and knows nothing about a device (`ui_destination_extent()` lives in the backend).
Buying that coupling on a *prediction* about how something looks, before anyone has looked,
is the same error U8d recorded when it shipped a measurement and no cache: do not rule on a
number, or an appearance, you have not observed. If the eye check says it shimmers, the
snapping option is backlogged with that observation as its trigger.

**Keep the weaker sentence.** U9d may claim "no gate observed a positioning artefact"; it may
not claim "sub-pixel scrolling looks fine", because no gate here can see that.

## 2. Vertical only.

One axis in U9d. A horizontal or two-axis scroll is a second axis field, and `UiFlow`'s
lesson applies to any new axis or enum field: one meaning everywhere, a differing default at
most. Carrying a second axis with no authored user is a second default to get wrong — the
same reason U9c declined the horizontal wheel. Backlogged.

## 3. The authored demo region is scoped to close FOUR owed eye checks in ONE pass.

The decomposition already requires the first authored scroll region to live in the demo
scene. It is additionally scoped here, because the human's attention is the scarce resource
and four items are currently owed:

The region must make all of these visible at once:
- **clipping** — content cut at the container boundary (U9a/U9b have *never* been seen; no
  scene authors a `UiClipRect` today, which is why the renderer gate was vacuous for both);
- **scrolling** — content taller than its clip, driven by the wheel;
- **the hit-test filter** — a `UiButton` scrolled out of the clip must not be clickable,
  which is U9b's filter exercised through a real scroll rather than an authored clip;
- **wrapped text** — at least one label inside the region with a `\n` or a `UiText::wrap_width`
  set, because U11b shipped word-wrap that **nothing authors**, so no human has seen it.

**The four entity-count tests are NOT in play, and that is a deliberate constraint.** They
count Transforms (`demo_scene_test.cpp:59`, `populate_world_test.cpp:52`,
`scene_save_roundtrip_test.cpp:40`) and scripts (`demo_scene_test.cpp:74`). Existing UI
entities carry neither — `demo_scene.hpp` says so where `hud_label` is authored. **Author no
Transform and no script in the scroll region** and all four stay green. If a Transform
becomes necessary, that is a finding to report, not a thing to work around by editing four
tests.

## 4. What remains the implementer's to decide, and to record

- Where the offset lives (a field on the scroll component vs separate state) and how the
  content extent is measured.
- Whether the wheel-to-offset step is a tunable or a constant. **If the data decides it, decide
  it** — do not defer it to "tune later", which hands the decision to whoever next notices it
  feels wrong, without the measurements.
- The clamp's exact behaviour at both ends, which the decomposition requires ("scrolling past
  the end clamps").

## Non-goals, each to be recorded in the backlog in the same turn

- Pixel-snapping the scroll offset (ruling 1; trigger is the eye check).
- Horizontal and two-axis scrolling (ruling 2).
- Scrollbar visuals and any affordance showing content continues.
- Nested scroll regions and scroll chaining (which region consumes a wheel delta).
- Smooth or inertial scrolling — the wheel delta is discrete and easing has its own
  frame-timing questions.
- Keyboard or drag scrolling.
- The grid's overflow policy enum (clip / scroll / grow), which becomes *decidable* after
  U9d but is its own authored-policy decision; "grow" was already declined by the human.

## What belongs to the human

Merging; **the eye pass**, which is the whole point of the authoring above and is the only
thing that can judge whether scrolled bitmap text looks acceptable; and any ruling where
this slice overturns a decision above **with evidence** — that is the process working.
