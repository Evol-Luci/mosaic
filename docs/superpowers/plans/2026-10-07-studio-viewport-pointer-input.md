# Viewport Pointer Input Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make a click in the Studio viewport drive the game's pointer while the editor is playing, so manipulating a scene and exercising its scripts stop being two different programs.

**Architecture:** `ViewportPanel` gains a record-then-query pointer seam — `StudioApp::draw_viewport` feeds it the raw ImGui facts, and a `pointer_state(bool play_active)` method applies panel-relative normalisation plus every suppression rule. `editor.cpp` builds one lambda from it and hands the same lambda to both `UiSystem` (via `setup_runtime`) and `ClickNudgeSystem` (via `register_windowed_systems`). All the policy is headless and unit-tested; only the four lines that read ImGui are not.

**Tech Stack:** C++20, Catch2, Dear ImGui (docking), CMake presets (`debug`), ctest.

**Spec:** `docs/superpowers/specs/2026-10-07-studio-viewport-pointer-input-design.md`

## Global Constraints

- Branch: `studio-viewport-pointer-input` (created; spec committed as `29bc5bd`).
- Build: `cmake --build build/debug`. Test: `ctest --test-dir build/debug`. One case:
  `./build/debug/tests/mosaic_unit_tests "<case name>"`.
- **No commas in `TEST_CASE` names.** Catch2 treats a comma in a name filter as a
  test-spec separator, so a comma'd name matches nothing and exits non-zero —
  indistinguishable from a failure. This has already cost this project a full probe round
  and two false-red columns.
- `mosaic_unit_tests` is ~170MB and intermittently fails at the LINK step under memory
  pressure. Grep the output for a real compiler diagnostic; if there is none, re-run.
- A full build is ~4 minutes. Batch mutation probes into one scripted run rather than
  rebuilding per probe interactively.
- Tier rule: `app -> studio -> engine -> foundation`, enforced by
  `tools/ci/check_dependency_rules.py`. `studio/viewport.hpp` may include
  `engine/ui/pointer.hpp`; it must NOT include anything from `app/`.
- `studio/viewport.hpp` must stay free of ImGui and of `PlayState` — that is what keeps it
  testable. `play_active` is a parameter, not a stored dependency.
- The pre-commit hook regenerates `docs/index.html` and `docs/reference/*.md`; when it
  reports them stale, `git add` what it rewrote and commit again.
- `docs/project/next-steps.md` is updated on this branch, in this PR.

## Review Focus

1. **Press-edge synthesis when eligibility opens.** `ClickNudgeSystem` fires on
   `p.down && !was_down_`, so any transition of `down` from false to true that is not a
   fresh physical press is a synthetic click. Task 2 covers the play-mode and
   ImGui-capture cases; the same latch must also hold when the cursor *enters* the panel
   with the button already held (drag from the Inspector onto the viewport and release).
2. **A degenerate or zero-size panel.** `to_extent` floors the stored size at 1, but
   `rect_min` plus a 1x1 size means every cursor position normalises to roughly the same
   texel. Must not divide by zero or produce NaN. Task 1 pins it.
3. **The Y origin.** `PointerState` is top-left-origin; `pick_entity` wants NDC with +y up;
   `ClickNudgeSystem` already owns that flip in `pointer_to_ndc_y`. A provider that
   flips Y itself would double-flip and silently drive the vertically mirrored button.
   Task 1 pins the origin explicitly at a non-centre point.
4. **`aspect` sourced from the window rather than the panel.** Diverges the nudge ray from
   the clicked image, worse the further from centre, and always — the two aspects differ
   whenever the viewport is not the whole window. Task 3 pins it.
5. **Selection and the game pointer both firing on one click.** If the existing
   `IsItemClicked` pick is not gated on `!is_playing()`, a play-mode click both nudges and
   re-selects. Task 4 pins the gate textually, since the call site needs ImGui.

---

### Task 1: `ViewportPanel`'s pointer seam — coordinates

**Files:**
- Modify: `studio/viewport.hpp` (add `ViewportPointerSample`, `set_pointer_sample`, `pointer_state`; include `engine/ui/pointer.hpp`)
- Test: `tests/unit/studio/viewport_test.cpp` (already registered in `tests/CMakeLists.txt`)

**Interfaces:**
- Consumes: `mosaic::engine::ui::PointerState` from `engine/ui/pointer.hpp`.
- Produces: `mosaic::studio::ViewportPointerSample{float mouse_x, mouse_y, rect_min_x, rect_min_y; bool hovered, imgui_capturing, button_down;}`, `ViewportPanel::set_pointer_sample(const ViewportPointerSample&)`, `ViewportPanel::pointer_state(bool play_active) const -> engine::ui::PointerState`. Tasks 2-4 all build on these exact names.

- [ ] **Step 1: Write the failing coordinate tests**

Append to `tests/unit/studio/viewport_test.cpp`:

```cpp
TEST_CASE("ViewportPanel reports the cursor relative to the panel with a top-left origin",
          "[studio][viewport]")
{
    // The panel is 800x600 at screen (100, 50). PointerState is normalised [0,1] with a
    // TOP-LEFT origin, matching what engine/ui/pointer.hpp documents and what UiButton
    // rects use.
    //
    // The off-centre assertions are the point. A centre-only check passes with the Y axis
    // inverted, because the centre is its own mirror -- and an inverted provider still
    // "works", it just drives the vertically mirrored button. ClickNudgeSystem already
    // owns the flip to NDC in pointer_to_ndc_y, so flipping here too would double-flip.
    ViewportPanel panel;
    panel.set_measured_size(800.0f, 600.0f);

    mosaic::studio::ViewportPointerSample s;
    s.rect_min_x = 100.0f;
    s.rect_min_y = 50.0f;
    s.hovered = true;
    s.button_down = false;

    s.mouse_x = 500.0f; s.mouse_y = 350.0f; // exact centre
    panel.set_pointer_sample(s, true);
    REQUIRE(panel.pointer_state(true).x == Approx(0.5f));
    REQUIRE(panel.pointer_state(true).y == Approx(0.5f));

    s.mouse_x = 100.0f; s.mouse_y = 50.0f;  // top-left corner
    panel.set_pointer_sample(s, true);
    REQUIRE(panel.pointer_state(true).x == Approx(0.0f));
    REQUIRE(panel.pointer_state(true).y == Approx(0.0f));

    s.mouse_x = 900.0f; s.mouse_y = 650.0f; // bottom-right corner
    panel.set_pointer_sample(s, true);
    REQUIRE(panel.pointer_state(true).x == Approx(1.0f));
    REQUIRE(panel.pointer_state(true).y == Approx(1.0f));

    // The asymmetric case that pins the origin: upper quarter must be y = 0.25, not 0.75.
    s.mouse_x = 300.0f; s.mouse_y = 200.0f;
    panel.set_pointer_sample(s, true);
    REQUIRE(panel.pointer_state(true).x == Approx(0.25f));
    REQUIRE(panel.pointer_state(true).y == Approx(0.25f));
}

TEST_CASE("ViewportPanel clamps the reported cursor into the unit range",
          "[studio][viewport]")
{
    // The cursor can sit outside the panel while a press is still being tracked, and
    // main.cpp's GLFW provider already clamps for the same reason. Unclamped values let a
    // UiButton rect match outside itself and make ClickNudgeSystem's NDC exceed [-1,1],
    // producing a camera ray that misses the frustum entirely.
    ViewportPanel panel;
    panel.set_measured_size(200.0f, 100.0f);
    mosaic::studio::ViewportPointerSample s;
    s.rect_min_x = 0.0f; s.rect_min_y = 0.0f; s.hovered = true;

    s.mouse_x = -50.0f; s.mouse_y = -10.0f;
    panel.set_pointer_sample(s, true);
    REQUIRE(panel.pointer_state(true).x == Approx(0.0f));
    REQUIRE(panel.pointer_state(true).y == Approx(0.0f));

    s.mouse_x = 10000.0f; s.mouse_y = 10000.0f;
    panel.set_pointer_sample(s, true);
    REQUIRE(panel.pointer_state(true).x == Approx(1.0f));
    REQUIRE(panel.pointer_state(true).y == Approx(1.0f));
}

TEST_CASE("ViewportPanel survives a degenerate one-by-one panel", "[studio][viewport]")
{
    // to_extent floors the stored size at 1, so this is the smallest reachable panel --
    // it happens on the first frame before ImGui has measured anything. The normalisation
    // divides by the size, so a 0 would be a divide-by-zero and a NaN would propagate
    // into every UiButton hit test as a silently-never-matching rect.
    ViewportPanel panel;
    panel.set_measured_size(0.0f, 0.0f); // floors to 1x1
    REQUIRE(panel.size().width == 1u);
    mosaic::studio::ViewportPointerSample s;
    s.rect_min_x = 0.0f; s.rect_min_y = 0.0f; s.mouse_x = 0.5f; s.mouse_y = 0.5f;
    s.hovered = true;
    panel.set_pointer_sample(s, true);
    const auto p = panel.pointer_state(true);
    REQUIRE(std::isfinite(p.x));
    REQUIRE(std::isfinite(p.y));
    REQUIRE(p.x >= 0.0f);
    REQUIRE(p.x <= 1.0f);
}
```

Add `#include <cmath>` to the test file's includes if absent (for `std::isfinite`).

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build/debug 2>&1 | grep -m5 "error:"`
Expected: FAIL at compile time — `'ViewportPointerSample' is not a member of 'mosaic::studio'` and no `set_pointer_sample` / `pointer_state`. A compile failure is the correct red; there is nothing to call yet.

- [ ] **Step 3: Implement the sample and coordinate half**

In `studio/viewport.hpp`, add the include and the type above `class ViewportPanel`:

```cpp
#include "engine/ui/pointer.hpp"
```

```cpp
// The raw per-frame facts ImGui reports about the viewport image. No policy — every
// suppression rule lives in ViewportPanel::pointer_state, which is what lets the rules be
// tested with no window and no GPU.
struct ViewportPointerSample
{
    float mouse_x = 0.0f, mouse_y = 0.0f;       // screen space, ImGui::GetMousePos
    float rect_min_x = 0.0f, rect_min_y = 0.0f; // the image's top-left, ImGui::GetItemRectMin
    bool  hovered = false;                      // ImGui::IsItemHovered on the image
    bool  imgui_capturing = false;              // ImGui::GetIO().WantCaptureMouse
    bool  button_down = false;                  // the raw left button, ungated
};
```

Inside `ViewportPanel`'s `public:` section:

```cpp
    // Record this frame's raw pointer facts. Called from StudioApp::draw_viewport.
    //
    // Takes `play_active` as well as the sample because the press latch Task 2 adds has to
    // judge eligibility at the MOMENT a press starts, and play mode is part of that. The
    // signature is final here so Task 2 only fills in behaviour, never changes the shape.
    //
    // ONE FRAME OF LATENCY, inherited rather than invented: editor.cpp's on_frame ticks
    // the world BEFORE drawing the UI, so a tick reads the sample captured during the
    // previous frame's draw (~16ms at 60Hz). That is the same latency this class already
    // documents for sizing, where the size measured in frame N feeds frame N+1's
    // offscreen pass.
    void set_pointer_sample(const ViewportPointerSample& s, bool /*play_active*/)
    {
        pointer_ = s; // Task 2 adds the latch update here.
    }

    // The pointer as the engine should see it: panel-relative, normalised to [0,1] with a
    // TOP-LEFT origin, with `down` gated by every suppression rule.
    //
    // `play_active` is a PARAMETER, not a stored dependency, so this header stays free of
    // PlayState (owned by StudioApp) and of ImGui. That is the whole reason the policy is
    // testable.
    engine::ui::PointerState pointer_state(bool play_active) const
    {
        engine::ui::PointerState p;
        // Divide by the MEASURED size, which to_extent floors at 1, so this cannot divide
        // by zero even on the first frame before ImGui has measured anything.
        const float w = static_cast<float>(measured_.width);
        const float h = static_cast<float>(measured_.height);
        p.x = clamp01((pointer_.mouse_x - pointer_.rect_min_x) / w);
        // No Y flip here. PointerState is top-left-origin by contract, and
        // ClickNudgeSystem::pointer_to_ndc_y already flips it on the way to NDC --
        // flipping here too would double-flip and drive the mirrored button.
        p.y = clamp01((pointer_.mouse_y - pointer_.rect_min_y) / h);
        p.down = false; // Task 2 fills this in.
        return p;
    }
```

And in `private:`:

```cpp
    static float clamp01(float v)
    {
        if (!(v > 0.0f)) { return 0.0f; } // also catches NaN
        return (v < 1.0f) ? v : 1.0f;
    }

    ViewportPointerSample pointer_{};
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build/debug && ./build/debug/tests/mosaic_unit_tests "[studio][viewport]"`
Expected: PASS, 7 cases (4 pre-existing + 3 new).

- [ ] **Step 5: Commit**

```bash
git add studio/viewport.hpp tests/unit/studio/viewport_test.cpp
git commit -m "feat(studio): ViewportPanel reports a panel-relative cursor

Records the raw ImGui facts and normalises them to [0,1] with a top-left
origin, matching what PointerState documents. No Y flip -- ClickNudgeSystem
already owns the flip to NDC, so flipping here would double-flip and drive
the vertically mirrored button. Clamped, because the cursor can leave the
panel mid-press. \`down\` is still hardcoded false; the suppression rules
land next."
```

---

### Task 2: The suppression rules, including the press-edge latch

**Files:**
- Modify: `studio/viewport.hpp` (fill in `down`, add the latch state and `update_latch`)
- Test: `tests/unit/studio/viewport_test.cpp`

**Interfaces:**
- Consumes: `ViewportPointerSample` and `pointer_state(bool)` from Task 1.
- Produces: no new names. `pointer_state`'s `down` becomes meaningful; `set_pointer_sample` gains the side effect of advancing the latch.

- [ ] **Step 1: Write the failing suppression tests**

```cpp
TEST_CASE("ViewportPanel stops reporting a press the moment a condition stops holding",
          "[studio][viewport]")
{
    // One case per condition, and the SHAPE matters more than the conditions do.
    //
    // The naive construction -- record a press with one condition already unmet, assert
    // no down -- cannot distinguish the gate in pointer_state from the latch in
    // set_pointer_sample, because a press that STARTS ineligible is blocked by the latch
    // regardless. Dropping `pointer_.hovered &&` from pointer_state would leave that test
    // green, so it would be a guard that guards nothing.
    //
    // So each case starts a LEGITIMATE press -- every condition met, latch clear -- and
    // then removes one condition mid-press. Only pointer_state's gate can make `down`
    // false then, which is exactly what each probe deletes. These are also the real
    // scenarios: press in the viewport and drag off it; press and then hit Stop.
    ViewportPanel panel;
    panel.set_measured_size(100.0f, 100.0f);
    auto held = [] {
        mosaic::studio::ViewportPointerSample s;
        s.mouse_x = 50.0f; s.mouse_y = 50.0f;
        s.hovered = true; s.imgui_capturing = false; s.button_down = true;
        return s;
    };

    SECTION("a legitimate press is reported")
    {
        panel.set_pointer_sample(held(), true);
        REQUIRE(panel.pointer_state(true).down);
    }
    SECTION("play stops mid-press -> no down")
    {
        // The slice's behaviour decision, enforced while a press is in flight. Press,
        // then hit Stop without releasing: the scene must stop seeing the press at once.
        panel.set_pointer_sample(held(), true);
        REQUIRE(panel.pointer_state(true).down);
        panel.set_pointer_sample(held(), false);
        REQUIRE_FALSE(panel.pointer_state(false).down);
    }
    SECTION("cursor leaves the panel mid-press -> no down")
    {
        panel.set_pointer_sample(held(), true);
        REQUIRE(panel.pointer_state(true).down);
        auto s = held(); s.hovered = false;
        panel.set_pointer_sample(s, true);
        REQUIRE_FALSE(panel.pointer_state(true).down);
    }
    SECTION("ImGui takes the mouse mid-press -> no down")
    {
        panel.set_pointer_sample(held(), true);
        REQUIRE(panel.pointer_state(true).down);
        auto s = held(); s.imgui_capturing = true;
        panel.set_pointer_sample(s, true);
        REQUIRE_FALSE(panel.pointer_state(true).down);
    }
    SECTION("button released -> no down")
    {
        panel.set_pointer_sample(held(), true);
        auto s = held(); s.button_down = false;
        panel.set_pointer_sample(s, true);
        REQUIRE_FALSE(panel.pointer_state(true).down);
    }
}

TEST_CASE("ViewportPanel suppresses a press that began while ineligible",
          "[studio][viewport]")
{
    // The subtle one, and omitting it produces a click the user never made.
    //
    // ClickNudgeSystem fires on a press EDGE (`p.down && !was_down_`), so any false->true
    // transition of `down` that is not a fresh physical press is a synthetic click landing
    // wherever the cursor happens to be. Hold the left button in edit mode, press Play,
    // and without this latch `down` flips true with no new press. Same for releasing an
    // inspector drag over the viewport.
    //
    // So: a press that began while any condition was unmet stays suppressed until the raw
    // button goes UP, regardless of what becomes eligible in between.
    ViewportPanel panel;
    panel.set_measured_size(100.0f, 100.0f);
    mosaic::studio::ViewportPointerSample s;
    s.mouse_x = 50.0f; s.mouse_y = 50.0f; s.hovered = true;

    // Frame 1: button held in EDIT mode.
    s.button_down = true;
    panel.set_pointer_sample(s, /*play_active=*/false);
    REQUIRE_FALSE(panel.pointer_state(false).down);

    // Frame 2: now playing, button STILL held. No new press happened.
    panel.set_pointer_sample(s, /*play_active=*/true);
    REQUIRE_FALSE(panel.pointer_state(true).down);

    // Frame 3: still held. Still suppressed -- the latch does not time out.
    panel.set_pointer_sample(s, /*play_active=*/true);
    REQUIRE_FALSE(panel.pointer_state(true).down);

    // Frame 4: released. Nothing reported, and the latch clears.
    s.button_down = false;
    panel.set_pointer_sample(s, /*play_active=*/true);
    REQUIRE_FALSE(panel.pointer_state(true).down);

    // Frame 5: a genuine fresh press while eligible.
    s.button_down = true;
    panel.set_pointer_sample(s, /*play_active=*/true);
    REQUIRE(panel.pointer_state(true).down);
}

TEST_CASE("ViewportPanel suppresses a press dragged in from outside the panel",
          "[studio][viewport]")
{
    // Review Focus 1's second half: the same synthesis, reached by moving the cursor
    // rather than by changing modes. Press on the Inspector, drag onto the viewport,
    // release. Without the latch, `hovered` going true mid-press flips `down` true and
    // nudges whatever is under the cursor.
    ViewportPanel panel;
    panel.set_measured_size(100.0f, 100.0f);
    mosaic::studio::ViewportPointerSample s;
    s.mouse_x = 50.0f; s.mouse_y = 50.0f;

    s.hovered = false; s.button_down = true; // pressed elsewhere
    panel.set_pointer_sample(s, true);
    REQUIRE_FALSE(panel.pointer_state(true).down);

    s.hovered = true; // dragged over the viewport, still held
    panel.set_pointer_sample(s, true);
    REQUIRE_FALSE(panel.pointer_state(true).down);

    s.button_down = false; // released
    panel.set_pointer_sample(s, true);
    s.button_down = true;  // fresh press, now over the viewport
    panel.set_pointer_sample(s, true);
    REQUIRE(panel.pointer_state(true).down);
}

TEST_CASE("ViewportPanel reports coordinates even with no press", "[studio][viewport]")
{
    // Hover-driven UI needs the position without a press -- a UiButton highlighting under
    // the cursor, for instance. A provider that zeroed x/y when `down` is false would make
    // every hover effect snap to the panel's top-left corner.
    ViewportPanel panel;
    panel.set_measured_size(100.0f, 100.0f);
    mosaic::studio::ViewportPointerSample s;
    s.mouse_x = 75.0f; s.mouse_y = 25.0f; s.hovered = true; s.button_down = false;
    panel.set_pointer_sample(s, true);
    const auto p = panel.pointer_state(true);
    REQUIRE_FALSE(p.down);
    REQUIRE(p.x == Approx(0.75f));
    REQUIRE(p.y == Approx(0.25f));
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build/debug && ./build/debug/tests/mosaic_unit_tests "[studio][viewport]" 2>&1 | tail -6`
Expected: FAIL. `down` is hardcoded false from Task 1, so every case asserting `down` is true fails. The `REQUIRE_FALSE` cases pass already — that is expected and is why the `REQUIRE(down)` cases exist.

- [ ] **Step 3: Implement the rules and the latch**

The latch lives in `set_pointer_sample`, because that is the only place that sees every
frame and can therefore tell a fresh press from one already in flight. `play_active` is
already a parameter there, so eligibility can be judged at the instant a press begins.

```cpp
    void set_pointer_sample(const ViewportPointerSample& s, bool play_active)
    {
        // Judge the latch BEFORE storing, so a fresh press is detected by comparing this
        // frame's raw button against the previous frame's.
        if (!s.button_down)
        {
            press_blocked_ = false; // released: the next press starts clean
        }
        else if (!pointer_.button_down)
        {
            // A fresh press. Legitimate only if EVERY condition already held at the moment
            // it started -- play mode included, which is why this function needs it.
            press_blocked_ = !(play_active && s.hovered && !s.imgui_capturing);
        }
        // A press already in flight keeps the verdict it was given. That is the whole
        // point: becoming eligible mid-press must not retroactively make it a click.
        pointer_ = s;
    }
```

And `pointer_state` gains the gate (replacing Task 1's `p.down = false;`):

```cpp
        // Re-checked here as well as in the latch, deliberately, and not redundantly: the
        // latch decides whether a press was legitimate WHEN IT STARTED, while these
        // conditions decide whether it is still legitimate NOW. Play stopping mid-press,
        // or the cursor leaving the panel mid-press, must stop reporting `down`
        // immediately -- the latch alone would keep reporting it.
        p.down = play_active && pointer_.hovered && !pointer_.imgui_capturing
                 && pointer_.button_down && !press_blocked_;
```

In `private:`:

```cpp
    // True when the press currently in flight began while something made it ineligible.
    // Cleared only by the raw button going up, so it cannot be escaped by changing mode
    // or moving the cursor.
    bool press_blocked_ = false;
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build/debug && ./build/debug/tests/mosaic_unit_tests "[studio][viewport]" 2>&1 | tail -4`
Expected: PASS, 11 cases.

- [ ] **Step 5: Commit**

```bash
git add studio/viewport.hpp tests/unit/studio/viewport_test.cpp
git commit -m "feat(studio): gate the viewport pointer on play mode, hover and ImGui

down requires play mode, hover, ImGui not wanting the mouse, the raw button
held, AND that the press began while all of those already held.

That last rule is not decoration. ClickNudgeSystem fires on a press EDGE, so
any false->true transition of down that is not a fresh physical press is a
synthetic click: hold the button in edit mode, press Play, and without the
latch the scene gets clicked wherever the cursor sits. Same for dragging an
inspector slider and releasing over the viewport."
```

---

### Task 3: Wire the provider into the editor

**Files:**
- Modify: `studio/studio_app.cpp` (`draw_viewport`: feed the sample; gate the existing pick on `!is_playing()`)
- Modify: `app/run/editor.cpp` (build the lambda; pass to `setup_runtime`; call `register_windowed_systems`)
- Test: `tests/unit/app/windowed_systems_test.cpp` if it exists, else `tests/unit/app/` (new file, add to `tests/CMakeLists.txt`)

**Interfaces:**
- Consumes: `ViewportPanel::set_pointer_sample(sample, play_active)` and `pointer_state(bool)` from Tasks 1-2; `mosaic::app::register_windowed_systems` + `WindowedSystemDeps` from `app/run/windowed_systems.hpp`; `setup_runtime`'s `pointer` parameter (`app/run/demo_scene.hpp:198`).
- Produces: a live pointer path in `mosaic editor`. Terminal for the wiring.

- [ ] **Step 1: Write the failing wiring test**

Check first whether a test for `register_windowed_systems` already exists:
`grep -rln "register_windowed_systems" tests/`. If it does, add to it; the case below
assumes a new file `tests/unit/app/editor_pointer_wiring_test.cpp` and, if so, add that
path to `tests/CMakeLists.txt` beside the other `unit/app/` entries.

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "app/run/windowed_systems.hpp"
#include "engine/ui/pointer.hpp"
#include "engine/ui/ui_system.hpp"
#include "engine/world/world.hpp"
#include "studio/viewport.hpp"

using Catch::Approx;

TEST_CASE("the editor's viewport pointer drives a UiButton through UiSystem",
          "[app][pointer]")
{
    // End-to-end through the REAL UiSystem rather than asserting a lambda was stored.
    // The bug this guards against is the one the editor shipped with: setup_runtime's
    // `pointer` parameter defaults to {}, so UiSystem held an empty provider and saw
    // PointerState{} forever -- and nothing failed, because no test exercised the wiring.
    //
    // windowed_systems.hpp's own header records the precedent: click-to-nudge once
    // shipped dead behind a guard that never fired, with the whole suite green.
    mosaic::studio::ViewportPanel panel;
    panel.set_measured_size(100.0f, 100.0f);

    mosaic::studio::ViewportPointerSample s;
    s.mouse_x = 50.0f; s.mouse_y = 50.0f;
    s.hovered = true; s.button_down = true;
    panel.set_pointer_sample(s, /*play_active=*/true);

    auto provider = [&panel] { return panel.pointer_state(/*play_active=*/true); };

    mosaic::engine::World world;
    world.add_system<mosaic::engine::ui::UiSystem>(provider);

    // A button covering the whole panel, so the reported (0.5, 0.5) is inside it.
    const auto e = world.create_entity();
    mosaic::engine::ui::UiButton b;
    b.x = 0.0f; b.y = 0.0f; b.width = 1.0f; b.height = 1.0f;
    world.add_component<mosaic::engine::ui::UiButton>(e, b);

    world.tick(1.0f / 60.0f);

    auto* ui = world.get_system<mosaic::engine::ui::UiSystem>();
    REQUIRE(ui != nullptr);
    INFO("If this fails, the provider is not reaching UiSystem -- check that editor.cpp "
         "passes it to setup_runtime rather than leaving the default {}.");
    REQUIRE(ui->press_consumed());
}

TEST_CASE("register_windowed_systems registers click-to-nudge for the editor too",
          "[app][pointer]")
{
    // The editor is a second caller of the mechanism this function exists to make
    // testable. Before this slice only main.cpp called it, so the editor had no
    // ClickNudgeSystem at all.
    mosaic::engine::World world;
    mosaic::app::WindowedSystemDeps deps;
    deps.pointer = [] { return mosaic::engine::ui::PointerState{}; };
    deps.aspect = [] { return 1.0f; };
    deps.ui_claimed = [] { return false; };

    mosaic::app::register_windowed_systems(world, /*windowed=*/true, std::move(deps));
    REQUIRE(world.get_system<mosaic::app::ClickNudgeSystem>() != nullptr);
}
```

Verify `UiButton`'s actual field names before relying on them:
`grep -n "struct UiButton" -A 12 engine/ui/ui_button.hpp`. Adjust the rect fields to match.
Verify `UiSystem::press_consumed()` exists: `grep -n "press_consumed" engine/ui/ui_system.hpp`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build/debug && ./build/debug/tests/mosaic_unit_tests "[app][pointer]" 2>&1 | tail -6`
Expected: FAIL — if the file is new and unregistered, the filter matches nothing (Catch2 exits non-zero with "No tests ran"), which means the CMake entry is missing. Add it, rebuild, and expect a real assertion failure instead.

- [ ] **Step 3: Feed the sample from `draw_viewport`**

In `studio/studio_app.cpp`'s `draw_viewport`, after `ImGui::Image(...)`, record the sample
and gate the existing pick:

```cpp
        mosaic::studio::ViewportPointerSample sample;
        const ImVec2 rmin = ImGui::GetItemRectMin();
        const ImVec2 mpos = ImGui::GetMousePos();
        sample.mouse_x = mpos.x;
        sample.mouse_y = mpos.y;
        sample.rect_min_x = rmin.x;
        sample.rect_min_y = rmin.y;
        sample.hovered = ImGui::IsItemHovered();
        sample.imgui_capturing = ImGui::GetIO().WantCaptureMouse;
        sample.button_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        viewport_.set_pointer_sample(sample, play_.is_playing());

        // Click-to-select is the EDIT-mode behaviour. Gated on !is_playing() so a
        // play-mode click drives the game's pointer instead of also moving the selection;
        // without the gate one click does both. Selection stays reachable from the
        // Hierarchy panel while playing.
        if (!play_.is_playing() && ImGui::IsItemClicked())
        {
            ... existing pick body unchanged ...
        }
```

Check the member's actual name for `PlayState` on `StudioApp` first:
`grep -n "PlayState" studio/studio_app.hpp`. Use whatever it is called rather than `play_`.

- [ ] **Step 4: Wire `editor.cpp`**

Before the `setup_runtime` call at `app/run/editor.cpp:158`:

```cpp
    // ONE lambda, shared by UiSystem and ClickNudgeSystem. Both must see the same value
    // within a tick or the UI-claim arbitration decides about a different click than the
    // one it declined. Reading the CACHED ImGui sample makes that impossible by
    // construction; reading glfwGetCursorPos directly would not, because it is not
    // frame-snapshotted.
    auto viewport_pointer = [&studio] {
        return studio.viewport().pointer_state(studio.play_state().is_playing());
    };
```

Pass it to `setup_runtime` in the `pointer` position (check the parameter order at
`app/run/demo_scene.hpp:190-201` and pass by name/position accordingly), then after
`setup_runtime` returns:

```cpp
    // The editor always has a window, so `windowed` is unconditionally true here.
    // Registered AFTER setup_runtime so ClickNudgeSystem ticks after UiSystem and sees
    // its verdict on the same press -- World ticks systems in registration order.
    mosaic::app::WindowedSystemDeps pointer_deps;
    pointer_deps.pointer = viewport_pointer;
    // The PANEL's aspect, not the window's: the scene renders into a panel-sized
    // offscreen target, so a window-derived aspect would make the nudge ray diverge from
    // the image that was actually clicked -- worse the further from centre, and always,
    // since the two aspects differ whenever the viewport is not the whole window.
    pointer_deps.aspect = [&studio] { return studio.viewport().aspect(); };
    pointer_deps.ui_claimed = [&world]() -> bool {
        auto* ui = world.get_system<mosaic::engine::ui::UiSystem>();
        return ui != nullptr && ui->press_consumed();
    };
    mosaic::app::register_windowed_systems(world, /*windowed=*/true, std::move(pointer_deps));
```

Add the includes `app/run/windowed_systems.hpp` and `engine/ui/ui_system.hpp` to
`editor.cpp` if absent.

- [ ] **Step 5: Run to verify it passes**

Run: `cmake --build build/debug && ./build/debug/tests/mosaic_unit_tests "[app][pointer]"`
Expected: PASS, 2 cases.

- [ ] **Step 6: Run the whole suite**

Run: `ctest --test-dir build/debug 2>&1 | tail -4`
Expected: all passing. Note the case `threaded ScriptSystem applies script writes one tick
later` SIGSEGVs intermittently under full-suite ordering from embedded-Python GIL
teardown; it is pre-existing and unrelated. If it is the only failure, re-run to confirm.

- [ ] **Step 7: Commit**

```bash
git add studio/studio_app.cpp app/run/editor.cpp tests/ CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(studio): drive the scene's pointer from the editor viewport

editor.cpp builds one provider from ViewportPanel's cached sample and hands
the same lambda to UiSystem (via setup_runtime, whose pointer parameter it
previously left defaulted) and to ClickNudgeSystem (via
register_windowed_systems, which only main.cpp had ever called).

aspect comes from the viewport PANEL, not the window: the scene renders into
a panel-sized offscreen target, so a window-derived aspect diverges the nudge
ray from the clicked image.

Click-to-select is now gated on !is_playing(). Without it one play-mode click
both drives the game pointer and moves the selection. Selection stays
reachable from the Hierarchy panel while playing."
```

---

### Task 4: Pin the wiring decisions that need ImGui

**Files:**
- Test: `tests/unit/studio/` (new case in an existing file, or `tests/unit/app/editor_pointer_wiring_test.cpp` from Task 3)

**Interfaces:**
- Consumes: nothing new. Textual assertions over `studio/studio_app.cpp` and `app/run/editor.cpp`.
- Produces: nothing. Terminal task.

- [ ] **Step 1: Write the contract assertions**

The four lines that read ImGui, and the two provider choices in `editor.cpp`, cannot be
executed in CI. They get the same instrument this project already uses for shader and
backend code: a textual pin, with its limit stated.

```cpp
TEST_CASE("the editor gates click-to-select on not playing", "[studio][wiring]")
{
    // Cannot be executed here -- the call site needs ImGui and a window -- so it is pinned
    // textually, the same instrument texture_colorspace_contract_test uses on the Vulkan
    // backend and for the same reason.
    //
    // Without the gate, one play-mode click both nudges the body under the cursor AND
    // re-selects it, so the Inspector jumps to whatever the user just poked. That reads as
    // an editor quirk rather than a bug, and no headless test can see it.
    const std::string src = read_source("studio/studio_app.cpp");
    REQUIRE(src.find("!play_.is_playing() && ImGui::IsItemClicked()") != std::string::npos);
}

TEST_CASE("the editor sources the nudge aspect from the viewport panel", "[studio][wiring]")
{
    // The failure is silent and position-dependent: a window-derived aspect makes
    // ClickNudgeSystem's camera ray diverge from the image the user clicked, picking a
    // different object the further the cursor is from centre. It is wrong whenever the
    // viewport is not the whole window, which is always.
    const std::string src = read_source("app/run/editor.cpp");
    REQUIRE(src.find("studio.viewport().aspect()") != std::string::npos);
}

TEST_CASE("the editor feeds the viewport sample from ImGui and not from GLFW",
          "[studio][wiring]")
{
    // Both systems must see the same pointer within one tick. ImGui's GetMousePos is
    // frame-snapshotted; glfwGetCursorPos is not, so reading it directly could hand
    // UiSystem and ClickNudgeSystem different positions inside a single tick -- and the
    // UI-claim arbitration would then be declining a different click than the one
    // ClickNudgeSystem acts on.
    const std::string src = read_source("studio/studio_app.cpp");
    REQUIRE(src.find("ImGui::GetMousePos()") != std::string::npos);
    REQUIRE(src.find("ImGui::GetIO().WantCaptureMouse") != std::string::npos);
    REQUIRE(src.find("glfwGetCursorPos") == std::string::npos);
}
```

`read_source` is a small local helper, mirroring `read_shader` in
`tests/unit/render/shader_source.hpp`:

```cpp
std::string read_source(const std::string& rel)
{
    const std::string path = std::string(MOSAIC_TEST_DIR) + "/../" + rel;
    std::ifstream in(path);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return mosaic::tests::normalize_ws(mosaic::tests::code_only(ss.str()));
}
```

Reuse `code_only` / `normalize_ws` from `tests/unit/render/shader_source.hpp` — include it
and add that directory to the test target's include path if it is not already visible, the
same way `unit/serialization` is added at `tests/CMakeLists.txt:216`.

- [ ] **Step 2: Run to verify they pass**

Run: `cmake --build build/debug && ./build/debug/tests/mosaic_unit_tests "[studio][wiring]"`
Expected: PASS, 3 cases. These pin code Task 3 already wrote, so they are green on arrival
— the red comes from the probes in Step 3.

- [ ] **Step 3: Mutation probes, derived from the code**

Derived statement-by-statement from what Tasks 1-3 added, NOT from the assertion list —
deriving probes from assertions cannot find unasserted code, which is how the multi-scatter
slice shipped an unpinned statement. Apply each, rebuild, run `[studio]` and `[app]`,
confirm the expected red, revert.

| # | Mutation | Must turn red |
|---|---|---|
| 1 | drop `play_active &&` from `pointer_state` | "stops reporting a press the moment a condition stops holding" (play-stops section) |
| 2 | drop `pointer_.hovered &&` | same (cursor-leaves section) |
| 3 | drop `!pointer_.imgui_capturing &&` | same (ImGui-takes-mouse section) |
| 4 | drop `!press_blocked_` | both latch cases |
| 5 | `press_blocked_ = false;` unconditionally in the fresh-press branch | both latch cases |
| 6 | `p.y = clamp01(1.0f - (…))` (flip Y) | "top-left origin" |
| 7 | `p.x = p.y = 0.0f` when `!down` | "reports coordinates even with no press" |
| 8 | `pointer_deps.aspect` → a window-derived value | "sources the nudge aspect from the viewport panel" |
| 9 | remove the `!play_.is_playing()` gate | "gates click-to-select on not playing" |
| 10 | register `ClickNudgeSystem` before `UiSystem` in `editor.cpp` | **expected GREEN** — nothing asserts ordering; record it as a known gap rather than inventing a test that cannot see it |
| 11 | `setup_runtime` call reverted to the default `{}` pointer | "drives a UiButton through UiSystem" |

Probe 10 producing no red is a finding to record, not a failure: system registration order
is a runtime property of `World` and no headless assertion in this slice reaches it.

- [ ] **Step 4: Confirm the baseline**

```bash
git diff --stat   # expect empty for source files
cmake --build build/debug && ctest --test-dir build/debug 2>&1 | tail -4
```
Expected: clean diff, all tests passing. Rebuild before judging the baseline — a restored
source file with a stale binary reports the mutant's results.

- [ ] **Step 5: Commit**

```bash
git add tests/
git commit -m "test(studio): pin the pointer wiring that needs ImGui

Three textual pins for code CI cannot execute: the !is_playing() gate on
click-to-select, the panel-sourced aspect, and that the sample comes from
ImGui's frame-snapshotted GetMousePos rather than from glfwGetCursorPos.

Eleven probes derived from the code. Ten produce the expected red. The
eleventh -- registering ClickNudgeSystem before UiSystem -- produces none,
because system ordering is a runtime property of World that no headless
assertion in this slice reaches. Recorded as a known gap."
```

---

### Task 5: Verify, and write the handoff

**Files:**
- Modify: `docs/project/next-steps.md` (new `**Last updated:**` entry at the top, after the preamble)
- Modify: `docs/project/deferred-work-backlog.md` (retire the 🟡 row and the Editor/Studio entry)

**Interfaces:**
- Consumes: nothing. Terminal task.
- Produces: the handoff.

- [ ] **Step 1: Verify both entry points**

```bash
timeout 180 ./build/debug/app/mosaic editor --ticks 120 2>&1 | tail -5; echo "exit=$?"
timeout 180 ./build/debug/app/mosaic run --window --ticks 120 2>&1 | tail -5; echo "exit=$?"
```
Expected: both exit 0 with no `VUID` / `validation` / `error` lines. `run --window` must be
checked too: the provider changes are editor-only, but `register_windowed_systems` and
`ClickNudgeSystem` are shared, so a regression there would hit the CLI path.

- [ ] **Step 2: Report what a human must check**

Screenshots are unavailable in this sandbox, so do not claim visual confirmation. Report
the acceptance list from the spec for the user to run:

> With `mosaic editor`: in **edit mode**, clicking the viewport selects entities as before
> and the demo's UI button does nothing. Press **Play**: clicking the button fires its
> script, and clicking the sphere nudges it. Clicks over the Inspector or Hierarchy never
> affect the scene. Hold the button in edit mode, press Play — nothing is clicked until a
> fresh press. Press **Stop**: viewport selection works again.

- [ ] **Step 3: Retire the backlog entries**

In `docs/project/deferred-work-backlog.md`: strike through the 🟡 table row
(`**Pointer input from the editor viewport to the scene**`) with `**SHIPPED 2026-10-07**`
and the resolution (play mode only), and strike through the Editor / Studio entry
beginning `**The editor forwards no pointer input to the scene**`. Keep the original
reasoning in both; add what actually happened, including that the coordinate conversion
already existed for edit-mode selection and that the press-edge latch was the part the
scoping did not anticipate.

- [ ] **Step 4: Write the next-steps entry**

Add a `**Last updated:** 2026-10-07 (...)` block at the top of
`docs/project/next-steps.md`, after the preamble and above the previous entry, matching the
existing dense-parenthetical style. Cover: what shipped and the behaviour decision; that
the coordinate conversion already existed, which is what made play-mode-only cheap; the
press-edge latch and why omitting it synthesises a click; the panel-versus-window aspect
trap; the one-frame latency and that it is inherited from the sizing precedent; the
behaviour change to click-to-select during play; the probe that produced no red (system
ordering) as a known gap; the gate count; and that visual acceptance is outstanding.

- [ ] **Step 5: Commit and open the PR**

```bash
git add docs/project/next-steps.md docs/project/deferred-work-backlog.md
git commit -m "docs: viewport pointer input shipped"
git push -u origin studio-viewport-pointer-input
```
Then open a PR against `main` describing the slice, and request review before merging.
