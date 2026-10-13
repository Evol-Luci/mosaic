# UI Pointer Capture and Drag Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** While a press is held, the pressed `UiButton` owns the pointer — nothing else hovers — and pointer movement emits `drag_start` / `drag` / `drag_end` / `drag_cancel` on that button's event channel.

**Architecture:** `UiSystem` already remembers the pressed entity across frames; this renames that field to `captured_`, makes hover resolve against it, and adds a movement threshold plus three new fields driving four new actions through the existing single `emit_button_action` helper.

**Tech Stack:** C++20, header-only `engine/ui`, Catch2, CMake/Ninja.

**Spec:** `docs/superpowers/specs/2026-10-12-ui-pointer-capture-drag-design.md`

## Global Constraints

- Tier contract: `engine/` depends only on `foundation/`. Enforced by `tools/ci/check_dependency_rules.py`.
- `engine/ui` is header-only; new code is `inline` in headers.
- Catch2 test names: no leading dashes, no unbalanced `[`, no commas.
- New test files must be added to `tests/CMakeLists.txt` or they silently never run.
- Drag threshold: **`0.01`** Euclidean in normalized screen units. Anisotropic; accepted.
- `click`, `hover_enter`, `hover_exit` payloads keep exactly `{entity, action}`. Only `drag_start`, `drag`, `drag_end` gain `x`/`y`. `drag_cancel` carries no position.
- `press_consumed()` keeps its name and meaning. `ClickNudgeSystem` must not change.
- Build `cmake --build build/debug -j4`; suite `ctest` from `build/debug`; lints `python3 tools/ci/check_dependency_rules.py` and `python3 tools/ci/check_test_case_names.py`.
- Mutation probes: restore a probed file with `mv` **then `touch` it**, or ninja keeps the mutant binary.
- Commit messages end with the session's two attribution lines.

## Review Focus

Five conditions the spec implies that no happy-path test exercises, most likely to bite first:

1. **A press that never moves** must emit `click` and *no* drag action — the most common input of all. Task 3, Step 7.
2. **Leaving the surface without having dragged** must emit nothing, not a stray `drag_cancel`. Task 4, Step 5.
3. **The captured entity destroyed mid-drag** must emit nothing further and not crash. Task 4, Step 7.
4. **A second press after a cancel** must start clean — no state leaking between presses. Task 4, Step 6.
5. **A pointer that is absent entirely** (`inside` false from the first frame, as headless `mosaic run` reports forever) must never capture or drag. Task 2, Step 5.

---

### Task 1: Rename `pressed_` to `captured_`

**Files:**
- Modify: `engine/ui/ui_system.hpp` (field at line 134; uses at 68, 78, 80, 82, 90)

**Interfaces:**
- Produces: `UiSystem::captured_` as the private field name later tasks read and write. `press_consumed()` is unchanged in name, signature and meaning.

- [ ] **Step 1: Rename the field and its five uses**

In `engine/ui/ui_system.hpp` replace the declaration:

```cpp
    mosaic::foundation::EntityID pressed_  = {};
```

with:

```cpp
    // WHO OWNS THE POINTER, not merely who was pressed. It already persisted across frames
    // and gated the click on a same-button release -- that was half a capture rule without
    // the name. The rename is what makes the routing readable: while this is set, the
    // pointer belongs to this entity and hover resolves against it alone.
    mosaic::foundation::EntityID captured_ = {};
```

Then replace every other `pressed_` with `captured_`. There are five: the press edge, the release-edge comparison, the `emit_button_action` call, the clear, and `press_consumed()`.

- [ ] **Step 2: Keep `press_consumed()`'s comment honest**

Its comment says "True when the most recent press landed on a UiButton". Append one line so the rename does not make it read as stale:

```cpp
    // True when the most recent press landed on a UiButton, so world-space click
    // handling (picking, nudging) can decline a click the UI already claimed.
    // Valid after this system's update() has run for the frame.
    //
    // Named for the question its CALLER asks ("did the UI take this press?"), not for the
    // field behind it -- ClickNudgeSystem does not care that the press is also a capture.
    bool press_consumed() const { return captured_ != mosaic::foundation::EntityID{}; }
```

- [ ] **Step 3: Build and run the whole suite**

Run: `cmake --build build/debug -j4 && (cd build/debug && ctest)`
Expected: PASS, every test. A pure rename must change nothing; the 23 `UiSystem` cases are the gate.

- [ ] **Step 4: Commit**

```bash
git add engine/ui/ui_system.hpp
git commit -m "refactor(ui): name the pressed entity what it is, a capture"
```

---

### Task 2: Pin hover to the captured button

**Files:**
- Modify: `engine/ui/ui_system.hpp` (the hover block in `update`)
- Modify: `engine/ui/ui_button.hpp` (the comment that documents today's behaviour)
- Test: `tests/unit/ui/ui_system_test.cpp`

**Interfaces:**
- Consumes: `captured_` from Task 1.
- Produces: no new API. Later tasks rely on hover already being pinned.

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/ui/ui_system_test.cpp`:

```cpp
TEST_CASE("a captured button stops another from hovering", "[ui][input]")
{
    // CAPTURE IS A ROUTING RULE. While a press is held, the pointer belongs to the button
    // it landed on, so dragging onto a neighbour must not light the neighbour up -- the
    // click is still going to the captured one, and feedback that says otherwise lies.
    Harness h;
    const auto other = h.add_overlapping(5, "on_top");

    // Press the harness's own button, which is order 0, by pressing where ONLY it sits.
    // add_overlapping covers {0,0}..{0.2,0.2}, the same rect, so press outside that and
    // inside the harness button instead: both are {0,0}..{0.2,0.2}, so order decides and
    // `other` would win. Give `other` a negative order so the harness button is topmost.
    h.world.get_component<UiButton>(other)->order = -5;

    h.ptr = {0.05f, 0.05f, true};
    h.tick();
    REQUIRE(h.world.has_component<UiHovered>(h.button));

    // Still pressed, and `other` is now topmost? It cannot be -- it is order -5. Instead
    // assert the invariant directly: while captured, `other` never gains the tag.
    h.ptr = {0.06f, 0.06f, true};
    h.tick();
    REQUIRE_FALSE(h.world.has_component<UiHovered>(other));
}

TEST_CASE("a captured button un-hovers off its rect and re-hovers on return", "[ui][input]")
{
    // The affordance that tells a user whether releasing here will fire the button. The
    // capture is kept either way -- only the hover tag follows the cursor in and out.
    Harness h;

    h.ptr = {0.05f, 0.05f, true};
    h.tick();
    REQUIRE(h.world.has_component<UiHovered>(h.button));

    h.ptr = {0.9f, 0.9f, true}; // dragged well outside, still held
    h.tick();
    REQUIRE_FALSE(h.world.has_component<UiHovered>(h.button));

    h.ptr = {0.05f, 0.05f, true}; // dragged back in
    h.tick();
    REQUIRE(h.world.has_component<UiHovered>(h.button));

    // And the capture survived the round trip: releasing inside still clicks.
    h.ptr = {0.05f, 0.05f, false};
    h.tick();
    REQUIRE(h.names_with_action("click") == std::vector<std::string>{"test_click"});
}

TEST_CASE("with no press hover still follows the cursor across buttons", "[ui][input]")
{
    // The shipped behaviour, unchanged. Capture only applies while a press is held.
    Harness h;
    const auto other = h.add_overlapping(5, "on_top");

    h.ptr = {0.05f, 0.05f, false};
    h.tick();
    REQUIRE(h.world.has_component<UiHovered>(other)); // order 5 is topmost
    REQUIRE_FALSE(h.world.has_component<UiHovered>(h.button));
}
```

Add `#include "engine/ui/ui_hover_style.hpp"` if `UiHovered` is not already visible — it is already included by this file, so check before adding.

- [ ] **Step 2: Run to verify they fail**

Run: `./build/debug/tests/mosaic_unit_tests "[ui][input]"`
Expected: the first two FAIL (the neighbour hovers, and the captured button keeps its tag off-rect); the third PASSES already.

- [ ] **Step 3: Pin the hover resolution**

In `update`, replace the line that computes the hover target:

```cpp
        const mosaic::foundation::EntityID top = topmost_hit(cands, p);
```

with:

```cpp
        const mosaic::foundation::EntityID top = topmost_hit(cands, p);

        // WHILE CAPTURED, THE POINTER BELONGS TO ONE BUTTON. Hover resolves against the
        // captured entity alone: it is hovered when the cursor is inside it and not
        // otherwise, and nothing else can hover at all. That is what makes a press-and-slide
        // read correctly -- the highlight tracks whether releasing here would fire, rather
        // than tracking what happens to be under the cursor.
        //
        // `top == captured_` rather than a fresh containment test, because `top` already
        // answers it from the same snapshot the press path uses. A second test could
        // disagree with the first.
        const mosaic::foundation::EntityID hover_target =
            (captured_ != mosaic::foundation::EntityID{})
                ? ((top == captured_) ? captured_ : mosaic::foundation::EntityID{})
                : top;
```

Then change the hover block to compare against `hover_target` instead of `top`:

```cpp
        if (hover_target != hovered_)
        {
            if (hovered_ != mosaic::foundation::EntityID{})
            {
                emit_button_action(world, hovered_, "hover_exit");
                world.remove_component<UiHovered>(hovered_);
            }
            if (hover_target != mosaic::foundation::EntityID{})
            {
                emit_button_action(world, hover_target, "hover_enter");
                world.add_component<UiHovered>(hover_target, UiHovered{});
            }
            hovered_ = hover_target;
        }
```

Leave the press and release paths reading `top`: capture is decided by what is actually under the pointer, not by the pinned hover.

- [ ] **Step 4: Run to verify they pass**

Run: `./build/debug/tests/mosaic_unit_tests "[ui][input]"`
Expected: PASS, all cases including the 23 pre-existing ones.

- [ ] **Step 5: Add the absent-pointer case (Review Focus 5)**

Append:

```cpp
TEST_CASE("an absent pointer never captures", "[ui][input]")
{
    // Headless `mosaic run` reports inside == false forever, and PointerState's `inside`
    // defaults to TRUE -- so a provider meaning "no pointer" must say so, and this pins
    // that the capture path honours it. Without this, a down-but-absent pointer would
    // capture a button nobody is pointing at.
    Harness h;
    h.ptr = PointerState{0.05f, 0.05f, true, false}; // down, but no pointer present
    h.tick();

    REQUIRE_FALSE(h.world.has_component<UiHovered>(h.button));
    REQUIRE_FALSE(h.press_consumed_for_test());
}
```

`Harness` has no accessor for `press_consumed()`. Add one to the harness struct in the same file:

```cpp
    // UiSystem is owned by the World, so reach it the way the production caller does.
    // NOT const: World::get_system<S>() is a non-const member returning S*, so a const
    // accessor here would not compile.
    bool press_consumed_for_test()
    {
        UiSystem* s = world.get_system<UiSystem>();
        return s != nullptr && s->press_consumed();
    }
```

`World::get_system<S>()` is declared at `engine/world/world.hpp:79` and returns `S*`.

- [ ] **Step 6: Run, then correct the stale comment**

Run: `./build/debug/tests/mosaic_unit_tests "[ui][input]"` — expected PASS.

In `engine/ui/ui_button.hpp`, replace:

```
// Hover is independent of the press state: it tracks the pointer during a drag that began
// elsewhere. It requires a pointer to be PRESENT (see PointerState::inside), so it is
// silent headless and while the cursor is outside the surface.
```

with:

```
// Hover is SUBORDINATE to the press state, as of pointer capture. While a press is held,
// the pointer belongs to the button it landed on: no other button hovers, and the captured
// one is hovered only while the cursor is inside its own rect -- so it un-highlights when
// you drag off and re-highlights when you drag back. With no press held, hover tracks
// whatever is topmost under the cursor. (This comment previously said hover was independent
// of the press, which was true until capture shipped.)
//
// It requires a pointer to be PRESENT (see PointerState::inside), so it is silent headless
// and while the cursor is outside the surface.
```

- [ ] **Step 7: Full suite, then commit**

```bash
cmake --build build/debug -j4 && (cd build/debug && ctest)
git add engine/ui/ui_system.hpp engine/ui/ui_button.hpp tests/unit/ui/ui_system_test.cpp
git commit -m "feat(ui): a captured button owns the pointer, so hover pins to it"
```

---

### Task 3: `drag_start` and `drag`

**Files:**
- Modify: `engine/ui/ui_system.hpp`
- Test: `tests/unit/ui/ui_system_test.cpp`

**Interfaces:**
- Consumes: `captured_` (Task 1), pinned hover (Task 2).
- Produces: `press_at_` (`Vec2`), `dragging_` (`bool`) private fields; `emit_button_action(world, e, action, const Vec2* at)` — the existing helper with a fourth parameter defaulting to `nullptr`. Task 4 calls it with a position for `drag_end` and without one for `drag_cancel`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/ui/ui_system_test.cpp`:

```cpp
TEST_CASE("a press that never moves emits a click and no drag", "[ui][input]")
{
    // THE MOST COMMON INPUT THERE IS. If drag_start fired on press instead of on movement,
    // every click in the engine would also emit drag_start and drag_end, and no handler
    // could tell a click from a one-pixel drag.
    Harness h;
    h.ptr = {0.05f, 0.05f, true};  h.tick();
    h.ptr = {0.05f, 0.05f, false}; h.tick();

    REQUIRE(h.names_with_action("click") == std::vector<std::string>{"test_click"});
    REQUIRE(h.names_with_action("drag_start").empty());
    REQUIRE(h.names_with_action("drag").empty());
    REQUIRE(h.names_with_action("drag_end").empty());
}

TEST_CASE("movement past the threshold emits one drag_start then a drag per frame",
          "[ui][input]")
{
    // drag_start is an EDGE, drag is a stream. A drag_start per frame would make a handler
    // re-initialise its state on every move, which is the bug this ordering prevents.
    Harness h;
    h.ptr = {0.05f, 0.05f, true};  h.tick(); // press, no movement yet
    h.ptr = {0.10f, 0.05f, true};  h.tick(); // 0.05 > 0.01 threshold
    h.ptr = {0.15f, 0.05f, true};  h.tick();

    REQUIRE(h.names_with_action("drag_start").size() == 1);
    REQUIRE(h.names_with_action("drag").size() == 1); // the second move only
}

TEST_CASE("movement below the threshold emits nothing until it is crossed", "[ui][input]")
{
    // The threshold exists so a hand tremor is still a click. Crossing it later must still
    // start the drag -- measured from the PRESS point, not from the previous frame, or a
    // slow drag would never accumulate enough to cross.
    Harness h;
    h.ptr = {0.050f, 0.050f, true}; h.tick();
    h.ptr = {0.055f, 0.050f, true}; h.tick(); // 0.005 < 0.01
    REQUIRE(h.names_with_action("drag_start").empty());

    h.ptr = {0.070f, 0.050f, true}; h.tick(); // 0.020 from the press point
    REQUIRE(h.names_with_action("drag_start").size() == 1);
}

TEST_CASE("a frame with no movement emits no drag", "[ui][input]")
{
    Harness h;
    h.ptr = {0.05f, 0.05f, true}; h.tick();
    h.ptr = {0.20f, 0.05f, true}; h.tick(); // drag_start
    h.ptr = {0.20f, 0.05f, true}; h.tick(); // identical position
    REQUIRE(h.names_with_action("drag").empty());
}

TEST_CASE("drag actions carry the pointer position and click does not", "[ui][input]")
{
    // A drag without coordinates cannot drive anything. A click WITH them would be a
    // payload change to a shape three shipped handlers already read.
    Harness h;
    h.ptr = {0.05f, 0.05f, true}; h.tick();
    h.ptr = {0.20f, 0.10f, true}; h.tick();

    const auto* start = h.find_event_with_action("drag_start");
    REQUIRE(start != nullptr);
    REQUIRE(start->payload.as<Table>().find("x") != nullptr);
    REQUIRE(start->payload.as<Table>().find("y") != nullptr);

    h.ptr = {0.20f, 0.10f, false}; h.tick();
    const auto* click = h.find_event_with_action("click");
    REQUIRE(click != nullptr);
    REQUIRE(click->payload.as<Table>().find("x") == nullptr);
}
```

Add this helper to `Harness`, beside `names_with_action`:

```cpp
    // The first event carrying `action`, or nullptr. For asserting on a payload rather
    // than only on which channel fired.
    const ScriptEvent* find_event_with_action(const std::string& action) const
    {
        for (const auto& ev : events)
        {
            if (!ev.payload.is<Table>()) { continue; }
            const auto* a = ev.payload.as<Table>().find("action");
            if (a != nullptr && a->is<std::string>() && a->as<std::string>() == action)
            {
                return &ev;
            }
        }
        return nullptr;
    }
```

- [ ] **Step 2: Run to verify they fail**

Run: `./build/debug/tests/mosaic_unit_tests "[ui][input]"`
Expected: the first passes already (no drag actions exist, so the empties hold); the rest FAIL on missing `drag_start`.

- [ ] **Step 3: Extend the emit helper with an optional position**

Replace `emit_button_action`'s signature and body:

```cpp
    // Emit `action` on the button's own event channel. ONE helper for click, hover and drag
    // alike: copies of this would be chances for the payload shapes to diverge, and a script
    // filtering on `action` would then work for one and not the others.
    //
    // `at` is the pointer position, included only for the drag actions. Passing nullptr
    // keeps the payload at exactly {entity, action} -- which is the shipped shape that
    // click, hover_enter and hover_exit must keep.
    //
    // Looks the button up rather than taking it by reference, because the hover and drag
    // paths must emit for an entity recorded on a PREVIOUS update, which may since have lost
    // its UiButton. Returns false in that case, with nothing emitted: the channel name lived
    // on the component, so there is no honest name to emit on.
    static bool emit_button_action(mosaic::engine::World& world,
                                   mosaic::foundation::EntityID e, const char* action,
                                   const Vec2* at = nullptr)
    {
        UiButton* b = world.get_component<UiButton>(e);
        if (b == nullptr)
        {
            return false;
        }
        namespace ser = mosaic::foundation::serialization;
        ser::Table payload;
        payload["entity"] = ser::Value(static_cast<std::uint64_t>(e.value()));
        payload["action"] = ser::Value(std::string(action));
        if (at != nullptr)
        {
            payload["x"] = ser::Value(static_cast<double>(at->x));
            payload["y"] = ser::Value(static_cast<double>(at->y));
        }
        world.events().emit(
            mosaic::engine::script::ScriptEvent{b->event, ser::Value(std::move(payload))});
        return true;
    }
```

`double` is the right storage, not a guess: `foundation/serialization/value.hpp:46` has a
`Value(double)` constructor, and `engine/script/script_system.hpp:148` already writes a float
into a payload as `Value(static_cast<double>(dt))`. (`payload["entity"]` uses `std::uint64_t`
because an `EntityID` value is one.)

- [ ] **Step 4: Add the fields and the drag emission**

Add beside the other private fields:

```cpp
    // Where the press landed, so the threshold is measured from the PRESS rather than from
    // the previous frame -- otherwise a slow drag never accumulates enough to cross it.
    mosaic::foundation::math::Vec2 press_at_{0.0f, 0.0f};
    // Has drag_start fired for this press? drag_start is an edge; drag is a stream.
    bool                           dragging_ = false;
    // The previous frame's position, so a motionless frame emits no drag.
    mosaic::foundation::math::Vec2 last_at_{0.0f, 0.0f};
```

Add the constant above the class:

```cpp
// How far the pointer must travel from the press point before a press becomes a drag.
//
// NORMALIZED SCREEN UNITS, so this is ANISOTROPIC: at 1920x1080 it is about 19px
// horizontally and 10px vertically. Accepted for now -- a pixel-accurate threshold needs
// the screen aspect, which UiSystem does not have and would have to be threaded in the way
// resolve_image_quad takes it. Recorded in the backlog.
inline constexpr float kDragThreshold = 0.01f;
```

In `update`, set the fields on the press edge:

```cpp
        if (p.down && !was_down_)
        {
            // Press: the TOPMOST button under the pointer, not the first one iterated.
            captured_ = top;
            press_at_ = mosaic::foundation::math::Vec2{p.x, p.y};
            last_at_  = press_at_;
            dragging_ = false;
        }
```

And add the drag emission after the press/release block, before `was_down_ = p.down;`:

```cpp
        // Drag, while the press is still held by a button. Nothing is emitted for a press
        // that has not moved past the threshold: that is a click, and a click emits no drag
        // action at all.
        if (p.down && was_down_ && captured_ != mosaic::foundation::EntityID{})
        {
            const mosaic::foundation::math::Vec2 at{p.x, p.y};
            if (!dragging_)
            {
                const float dx = at.x - press_at_.x;
                const float dy = at.y - press_at_.y;
                if (dx * dx + dy * dy > kDragThreshold * kDragThreshold)
                {
                    dragging_ = true;
                    emit_button_action(world, captured_, "drag_start", &at);
                    last_at_ = at;
                }
            }
            else if (at.x != last_at_.x || at.y != last_at_.y)
            {
                emit_button_action(world, captured_, "drag", &at);
                last_at_ = at;
            }
        }
```

> Squared comparison rather than `std::sqrt`, so the threshold needs no square root and the
> constant stays readable as a distance.

- [ ] **Step 5: Run to verify they pass**

Run: `./build/debug/tests/mosaic_unit_tests "[ui][input]"`
Expected: PASS, all cases.

- [ ] **Step 6: Run the mutation probes**

For each, edit `engine/ui/ui_system.hpp`, rebuild, run `./build/debug/tests/mosaic_unit_tests "[ui][input]"`, confirm FAIL, then restore **and `touch`** the file:

1. `>` becomes `>=` on the threshold — expect no failure (boundary untested); if none fails that is acceptable, note it rather than adding a boundary test for a tuned constant.
2. `dragging_ = true` removed — expect a `drag_start` per frame, test red.
3. The threshold comparison inverted (`<`) — expect red.
4. `last_at_ = at` removed from the `drag` branch — expect the motionless-frame test red.
5. `press_at_` set from `last_at_` instead of the press — expect the below-threshold test red.
6. `at` passed as `nullptr` for `drag_start` — expect the payload test red.

Any GREEN that is not item 1 is a real gap: add the distinguishing test before moving on.

- [ ] **Step 7: Full suite, then commit**

```bash
cmake --build build/debug -j4 && (cd build/debug && ctest)
git add engine/ui/ui_system.hpp tests/unit/ui/ui_system_test.cpp
git commit -m "feat(ui): drag_start and drag, on movement past a threshold"
```

---

### Task 4: `drag_end`, `drag_cancel`, and the lifecycle

**Files:**
- Modify: `engine/ui/ui_system.hpp`
- Test: `tests/unit/ui/ui_system_test.cpp`

**Interfaces:**
- Consumes: everything from Tasks 1–3.
- Produces: the finished behaviour. No new API.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("releasing inside after a drag emits drag_end and then click", "[ui][input]")
{
    // Press-wiggle-release is a click to any user, so the drag does NOT suppress it. A
    // handler that wants drag-only behaviour filters on `action`, which every handler must
    // already do.
    Harness h;
    h.ptr = {0.05f, 0.05f, true};  h.tick();
    h.ptr = {0.15f, 0.05f, true};  h.tick(); // drag_start
    h.ptr = {0.15f, 0.05f, false}; h.tick(); // release, still inside the button

    REQUIRE(h.names_with_action("drag_end").size() == 1);
    REQUIRE(h.names_with_action("click").size() == 1);
    REQUIRE(h.names_with_action("drag_cancel").empty());
}

TEST_CASE("releasing outside the captured button emits drag_end and no click", "[ui][input]")
{
    // The pointer is still on the surface -- it just is not on the button any more. That is
    // a completed drag that happened to end elsewhere, not an abort.
    Harness h;
    h.ptr = {0.05f, 0.05f, true};  h.tick();
    h.ptr = {0.90f, 0.90f, true};  h.tick(); // dragged off the button, still present
    h.ptr = {0.90f, 0.90f, false}; h.tick();

    REQUIRE(h.names_with_action("drag_end").size() == 1);
    REQUIRE(h.names_with_action("click").empty());
    REQUIRE(h.names_with_action("drag_cancel").empty());
}

TEST_CASE("the pointer leaving the surface mid-drag cancels it", "[ui][input]")
{
    // down false WITH inside false is the editor provider's way of saying the cursor left
    // the viewport panel -- deliberately, so a drag cannot keep running while the user
    // clicks in the Inspector. Aborting and completing are different outcomes, and
    // conflating them is where drag code goes wrong.
    Harness h;
    h.ptr = {0.05f, 0.05f, true}; h.tick();
    h.ptr = {0.20f, 0.05f, true}; h.tick(); // drag_start
    h.ptr = PointerState{0.20f, 0.05f, false, false}; // left the surface
    h.tick();

    REQUIRE(h.names_with_action("drag_cancel").size() == 1);
    REQUIRE(h.names_with_action("drag_end").empty());
    REQUIRE(h.names_with_action("click").empty());
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `./build/debug/tests/mosaic_unit_tests "[ui][input]"`
Expected: FAIL — no `drag_end` or `drag_cancel` is emitted by anything yet.

- [ ] **Step 3: Implement the release edge**

Replace the release branch:

```cpp
        else if (!p.down && was_down_)
        {
            const mosaic::foundation::math::Vec2 at{p.x, p.y};
            // THE TWO ENDINGS, and they are distinguishable without new plumbing. `inside`
            // false means the pointer left the surface rather than that the button came up
            // where the user meant it to -- the editor's provider drops `down` the moment
            // the cursor leaves the viewport panel, on purpose.
            //
            // Both are gated on `dragging_`: a press that never crossed the threshold
            // started no drag, so it ends none.
            if (dragging_)
            {
                if (p.inside)
                {
                    emit_button_action(world, captured_, "drag_end", &at);
                }
                else
                {
                    // No position: the provider clamps x/y to the panel edge once the cursor
                    // is outside, so reporting it would report a point the user never
                    // pointed at.
                    emit_button_action(world, captured_, "drag_cancel");
                }
            }
            // `top`, not a second topmost_hit call -- same `cands`, same `p`. The click rule
            // is unchanged: the release must land on the captured button itself, which is
            // stricter than merely still being on the surface.
            if (captured_ != mosaic::foundation::EntityID{} && top == captured_)
            {
                emit_button_action(world, captured_, "click");
            }
            captured_ = mosaic::foundation::EntityID{};
            dragging_ = false;
        }
```

> `drag_end` is emitted before `click` because a handler that commits a drag then sees the
> click should see them in that order; the reverse would read as a click that preceded the
> drag ending.

- [ ] **Step 4: Run to verify they pass**

Run: `./build/debug/tests/mosaic_unit_tests "[ui][input]"` — expected PASS.

- [ ] **Step 5: Add the no-drag-no-cancel case (Review Focus 2)**

```cpp
TEST_CASE("leaving the surface without dragging emits nothing", "[ui][input]")
{
    // No drag started, so there is none to cancel. Without the dragging_ gate this would
    // fire drag_cancel for a press the user simply moved away from.
    Harness h;
    h.ptr = {0.05f, 0.05f, true}; h.tick();
    h.ptr = PointerState{0.05f, 0.05f, false, false}; h.tick();

    REQUIRE(h.names_with_action("drag_cancel").empty());
    REQUIRE(h.names_with_action("drag_end").empty());
    REQUIRE(h.names_with_action("click").empty());
}
```

- [ ] **Step 6: Add the fresh-press case (Review Focus 4)**

```cpp
TEST_CASE("a press after a cancel starts a clean capture", "[ui][input]")
{
    // State leaking between presses would make the second drag inherit the first's
    // threshold origin, so a short second drag would start instantly or never.
    Harness h;
    h.ptr = {0.05f, 0.05f, true}; h.tick();
    h.ptr = {0.20f, 0.05f, true}; h.tick();                 // drag_start #1
    h.ptr = PointerState{0.20f, 0.05f, false, false}; h.tick(); // cancel

    h.ptr = {0.05f, 0.05f, true};  h.tick();                // fresh press
    h.ptr = {0.055f, 0.05f, true}; h.tick();                // below threshold
    REQUIRE(h.names_with_action("drag_start").size() == 1); // still just the first

    h.ptr = {0.05f, 0.05f, false}; h.tick();
    REQUIRE(h.names_with_action("click").size() == 1);
}
```

- [ ] **Step 7: Add the destroyed-mid-drag case (Review Focus 3)**

```cpp
TEST_CASE("destroying the captured button mid-drag emits nothing further", "[ui][input]")
{
    // emit_button_action already returns false when the entity lost its UiButton -- the
    // channel name lived on the component, so there is no honest name to emit on. Capture
    // inherits that discipline rather than inventing a fallback channel.
    Harness h;
    h.ptr = {0.05f, 0.05f, true}; h.tick();
    h.ptr = {0.20f, 0.05f, true}; h.tick(); // drag_start

    h.world.destroy_entity(h.button);
    h.world.tick(0.0f); // commit the deferred destruction

    h.ptr = {0.30f, 0.05f, true};  h.tick();
    h.ptr = {0.30f, 0.05f, false}; h.tick();

    REQUIRE(h.names_with_action("drag").empty());
    REQUIRE(h.names_with_action("drag_end").empty());
    REQUIRE(h.names_with_action("click").empty());
}
```

> `destroy_entity` then `tick(0.0f)` is the idiom `tests/unit/world/world_integration_test.cpp:25-26`
> uses: `destroy_entity` queues, `tick` commits. Note `h.tick()` itself calls `world.tick`,
> so the explicit commit tick also runs `UiSystem` once with the stale capture — which is
> precisely the path being tested.

- [ ] **Step 8: Probes, full gate, and the entry points**

Probes on `engine/ui/ui_system.hpp` (restore with `mv` then `touch`):

1. `drag_end` and `drag_cancel` swapped — expect red.
2. The `dragging_` gate removed from the endings — expect the no-drag-no-cancel test red.
3. `dragging_ = false` removed from the release — expect the fresh-press test red.
4. `captured_` not cleared on release — expect red.
5. `p.inside` replaced by `true` in the ending choice — expect the cancel test red.

Then:

```bash
cmake --build build/debug -j4
(cd build/debug && ctest)
python3 tools/ci/check_dependency_rules.py && echo deps-ok
python3 tools/ci/check_test_case_names.py && echo names-ok
./build/debug/app/mosaic run --ticks 5 --json >/dev/null; echo "run=$?"
timeout 60 ./build/debug/app/mosaic run --window --ticks 60 >/dev/null 2>&1; echo "window=$?"
timeout 60 ./build/debug/app/mosaic editor --ticks 40 >/dev/null 2>/tmp/ed.err; echo "editor=$?"
grep -c 'ui:' /tmp/ed.err
```

Expected: all green, all three exit 0, and `0` ui warnings — the demo has one button and nothing drags it.

- [ ] **Step 9: Commit**

```bash
git add engine/ui/ui_system.hpp tests/unit/ui/ui_system_test.cpp
git commit -m "feat(ui): drag_end on release, drag_cancel when the pointer leaves"
```

---

### Task 5: Documentation

**Files:**
- Modify: `docs/project/deferred-work-backlog.md`
- Modify: `docs/project/next-steps.md`
- Add: the spec and this plan (commit with Task 1 if not yet tracked)

- [ ] **Step 1: Strike the backlog item**

In the UI pillar section, mark U6's pointer capture/drag as SHIPPED, naming the four actions and the capture rule, and keeping the original reasoning as the explanation of what was built.

- [ ] **Step 2: Record what this slice deferred**

Add entries in the existing tiered format for:
- **A pixel-accurate drag threshold.** `kDragThreshold` is normalized and therefore anisotropic (~19px horizontal vs ~10px vertical at 1920x1080). A pixel threshold needs the screen aspect threaded into `UiSystem`, the way `resolve_image_quad` takes it.
- **The threshold value is unvalidated.** `0.01` is a guess until a human drags something; it goes on the next verification pass.
- **Drag on non-button elements** — still bounded by only `UiButton` being hit-testable, the same boundary the hover slice drew.
- **Suppressing `click` after a drag** — the road not taken, with the reasoning, so it is not revisited as an oversight.

- [ ] **Step 3: Write the next-steps entry**

A dated entry covering: U6 is complete; what capture means here; the two endings and why they are distinguishable; that hover's documented independence from the press was a shipped behaviour this deliberately changed; what the probes found; and that the threshold's *feel* is unverified and queued for a human pass.

- [ ] **Step 4: Full gate, then commit**

```bash
cmake --build build/debug -j4 && (cd build/debug && ctest)
git add docs/
git commit -m "docs(ui): record pointer capture and drag, and what it deferred"
```

- [ ] **Step 5: Push and open the PR**

```bash
git push -u origin feat/ui-pointer-capture-drag
```

Then `gh pr create --title "feat(ui): pointer capture and drag"` with a body covering: what capture means and that half the routing already existed under another name; the four actions and why `drag_start` is movement-triggered; the two endings and the `inside` distinction; the shipped hover behaviour this changes and the comment corrected with it; the probe results; and the gate numbers. Close by naming the threshold as the one thing tests cannot judge.

Wait for `build-and-test` to pass, then squash-merge and delete the branch.
