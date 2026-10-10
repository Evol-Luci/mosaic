# UI Interaction Depth Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Resolve overlapping `UiButton` hit regions by z-order, and emit `hover_enter`/`hover_exit` events so a HUD can acknowledge the pointer.

**Architecture:** All of the tie-break arithmetic goes in one pure free function, `topmost_hit` in `engine/ui/ui_hit.hpp`, testable with no world and no pointer provider. `UiSystem` collects candidates from `for_each`, calls it once per update, and keeps the state machine (`pressed_`, and a new `hovered_`). Hover emits on the button's existing `b.event` channel with new `action` values, so no component field and no second channel is added.

**Tech Stack:** C++20 header-only `engine/ui` tier; Catch2 + ctest; `foundation::events::EventBus` (synchronous and deferred disciplines); `foundation::serialization` for event payloads.

**Spec:** `docs/superpowers/specs/2026-10-10-ui-interaction-depth-design.md`

## Global Constraints

- Tier rules are enforced by `tools/ci/check_dependency_rules.py`. `engine/ui` may depend on `foundation/*` and other `engine/*`, never on `studio/`, `app/` or `modules/`.
- Test case names are checked by `tools/ci/check_test_case_names.py`. A name must not begin with `--` and must not contain an unmatched `[`. **So no test name may start with a flag-like token**; write "the pointer leaves the button" rather than "--hover exits".
- `world.events().subscribe<E>` is **synchronous** — the handler runs before `emit` returns, mid-tick. `subscribe_deferred` delivers exactly one tick later. Both exist; pick deliberately.
- `ScriptEvent` is `{std::string name; serialization::Value payload;}`. The click path already emits `{entity, action}`; hover must match that payload shape exactly.
- `order` is an `int` on a reflected component, so a scene file can hold any value including negatives. Nothing may assume it is non-negative.
- Every component's `order` field is declared **last** in its struct (`UiPanel`, `UiImage`, `UiText`). `UiButton::order` goes last too.
- Run `ctest --test-dir build/debug` for the suite. The pre-commit hook regenerates `docs/reference/components.md` and **aborts**; `git add` what it rewrote and re-run the **full** commit — never `git commit --amend`, which would rewrite the previous commit.

## Review Focus

Five input classes the spec implies but whose tests do not fall out of it naturally. Each has a test added to the task that owns the code.

1. **A null pointer provider hovers a button at the origin.** `UiSystem`'s constructor accepts a `std::function` that may be empty, and `update` falls back to `PointerState{}` — which is `{0, 0, false}`, a real position inside any button anchored `TopLeft` at `{0,0}`. The existing test harness's button is exactly that. Before this slice the fallback was harmless (no press ever arrives); with hover it would emit a spurious `hover_enter` for a pointer that does not exist. Task 4.
2. **A hovered entity destroyed or stripped of its `UiButton`.** `hovered_` holds an `EntityID`, not a component. If the entity goes away while hovered, the exit event's channel name (`b.event`) is unknowable, so the choice is between emitting nothing and emitting on a guessed name. Silence plus clearing the state is correct, but it must be deliberate and tested, or `hovered_` keeps a stale id and the *next* real hover emits no enter. Task 4.
3. **A zero-size button.** `size = {0,0}` makes `hit()` true only at the exact corner, because the comparison is inclusive on both edges. A scene with a collapsed button would then have a one-point hit region rather than none. Task 1.
4. **A negative `order`.** Authored scene data can hold `-1`, which is a legitimate way to push a button behind others. Sorting handles it, but a comparison written with unsigned types or `std::abs` would not. Task 1.
5. **Three or more buttons sharing one `order`.** The equal-order rule is "later in iteration wins", and with only two candidates a `std::sort`/`stable_sort` difference can pass by luck. Stability must hold across a longer run. Task 1.

---

## File Structure

| File | Responsibility |
|---|---|
| `engine/ui/ui_hit.hpp` (new) | `HitCandidate` and `topmost_hit` — the whole tie-break, pure, no world |
| `tests/unit/ui/ui_hit_test.cpp` (new) | The tie-break's contract, headless |
| `engine/ui/ui_button.hpp` | gains `int order = 0` (last) + its reflect line |
| `engine/ui/ui_system.hpp` | press path calls `topmost_hit`; new `hovered_` member and hover transition |
| `tests/unit/ui/ui_system_test.cpp` | extended `Harness` (second button), click-topmost case, hover state machine |
| `tests/unit/ui/ui_button_reflection_test.cpp` | `order` round-trip at a non-default value |
| `app/run/demo_scene.hpp` | hover subscriber brightening the HUD bar; `UiButton` init made designated |
| `tests/CMakeLists.txt` | registers `unit/ui/ui_hit_test.cpp` |

---

### Task 1: `topmost_hit` — the whole tie-break, pure

**Files:**
- Create: `engine/ui/ui_hit.hpp`
- Create: `tests/unit/ui/ui_hit_test.cpp`
- Modify: `tests/CMakeLists.txt` (register the new file beside the other `unit/ui/` entries)

**Interfaces:**
- Consumes: `mosaic::engine::ui::Rect` from `engine/ui/ui_rect.hpp`; `mosaic::engine::ui::PointerState` from `engine/ui/pointer.hpp`; `mosaic::foundation::EntityID` from `foundation/core/entity_id.hpp`.
- Produces: `mosaic::engine::ui::HitCandidate{mosaic::foundation::EntityID e; int order; Rect rect;}` and
  `topmost_hit(const std::vector<HitCandidate>& candidates, const PointerState& pointer) -> mosaic::foundation::EntityID`.
  Tasks 3 and 4 call these exact names.

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/ui/ui_hit_test.cpp`.

```cpp
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/ui/ui_hit.hpp"

using mosaic::engine::ui::HitCandidate;
using mosaic::engine::ui::PointerState;
using mosaic::engine::ui::Rect;
using mosaic::engine::ui::topmost_hit;
using mosaic::foundation::EntityID;

namespace
{
// Distinct ids so a test can tell WHICH candidate was chosen; EntityID{} is the nil
// "nothing was hit" answer, so no candidate may use it.
//
// Generation 1, not the raw single-argument ctor: generations start at 1 and a zero value
// is the reserved null handle, so `EntityID{n, 1}` is a real handle while `EntityID{n}`
// would build one with generation 0. These never pass through a World, but a test id that
// could not exist is a trap for whoever reuses this helper.
EntityID id(std::uint32_t n) { return EntityID{n, 1}; }

// A rect covering the top-left fifth of the screen, which the pointer {0.05, 0.05} is
// inside and {0.9, 0.9} is outside.
constexpr Rect kTopLeft{0.0f, 0.0f, 0.2f, 0.2f};
constexpr PointerState kInside{0.05f, 0.05f, false};
constexpr PointerState kOutside{0.9f, 0.9f, false};
} // namespace

TEST_CASE("an empty candidate list hits nothing", "[ui][hit]")
{
    // The empty HUD, and the first thing UiSystem will pass once per update on any scene
    // with no buttons -- which is most scenes.
    REQUIRE(topmost_hit({}, kInside) == EntityID{});
}

TEST_CASE("a single candidate is hit only when the pointer is inside it", "[ui][hit]")
{
    const std::vector<HitCandidate> one{HitCandidate{id(7), 0, kTopLeft}};
    REQUIRE(topmost_hit(one, kInside) == id(7));
    REQUIRE(topmost_hit(one, kOutside) == EntityID{});
}

TEST_CASE("the higher order wins among overlapping candidates", "[ui][hit]")
{
    // Asserted BOTH ways round. With only one ordering, a comparison written backwards
    // passes: whichever candidate happens to sit where the bug returns it.
    SECTION("higher order listed second")
    {
        const std::vector<HitCandidate> v{HitCandidate{id(1), 0, kTopLeft},
                                          HitCandidate{id(2), 5, kTopLeft}};
        REQUIRE(topmost_hit(v, kInside) == id(2));
    }
    SECTION("higher order listed first")
    {
        const std::vector<HitCandidate> v{HitCandidate{id(2), 5, kTopLeft},
                                          HitCandidate{id(1), 0, kTopLeft}};
        REQUIRE(topmost_hit(v, kInside) == id(2));
    }
}

TEST_CASE("equal orders resolve to the one later in iteration order", "[ui][hit]")
{
    // THE regression test for the bug this slice exists to fix. Today UiSystem takes the
    // FIRST button under the pointer, while the panel pass stable-sorts ascending and draws
    // in that order -- so among equal orders the entity drawn LAST is on top. First-hit and
    // last-drawn are opposite ends of the same list, which means a click currently goes to
    // the button the user CANNOT see.
    const std::vector<HitCandidate> v{HitCandidate{id(1), 3, kTopLeft},
                                      HitCandidate{id(2), 3, kTopLeft}};
    REQUIRE(topmost_hit(v, kInside) == id(2));
}

TEST_CASE("equal orders stay stable across three or more candidates", "[ui][hit]")
{
    // With only two candidates a std::sort / stable_sort difference can pass by luck --
    // two elements are trivially "stable" under many orderings. Three with one distinct
    // key in the middle is the shortest case that actually constrains it.
    const std::vector<HitCandidate> v{HitCandidate{id(1), 2, kTopLeft},
                                      HitCandidate{id(2), 2, kTopLeft},
                                      HitCandidate{id(3), 2, kTopLeft}};
    REQUIRE(topmost_hit(v, kInside) == id(3));
}

TEST_CASE("a negative order is ordered normally rather than treated as magnitude", "[ui][hit]")
{
    // `order` is an int on a reflected component, so a scene file can hold -1 -- a
    // legitimate way to push a button behind its neighbours. A comparison using unsigned
    // types or std::abs would rank -5 ABOVE 1.
    const std::vector<HitCandidate> v{HitCandidate{id(1), -5, kTopLeft},
                                      HitCandidate{id(2), 1, kTopLeft}};
    REQUIRE(topmost_hit(v, kInside) == id(2));
    const std::vector<HitCandidate> both_negative{HitCandidate{id(1), -5, kTopLeft},
                                                  HitCandidate{id(2), -1, kTopLeft}};
    REQUIRE(topmost_hit(both_negative, kInside) == id(2)); // -1 is above -5
}

TEST_CASE("only candidates under the pointer compete on order", "[ui][hit]")
{
    // A high-order button elsewhere on screen must not win. The natural wrong
    // implementation sorts first and then tests the single winner for containment, which
    // returns nothing here even though a lower-order button IS under the pointer.
    const std::vector<HitCandidate> v{
        HitCandidate{id(1), 0, kTopLeft},                          // under the pointer
        HitCandidate{id(2), 99, Rect{0.8f, 0.8f, 0.2f, 0.2f}}};    // high, but far away
    REQUIRE(topmost_hit(v, kInside) == id(1));
}

TEST_CASE("the pointer exactly on a rect edge counts as inside", "[ui][hit]")
{
    // UiSystem::hit is inclusive on both edges today (`>=` and `<=`). Keeping that is
    // deliberate, so it is asserted -- otherwise the next person reads the inclusive
    // comparison as an oversight and "fixes" it, silently shrinking every hit region by a
    // pixel on two sides.
    const std::vector<HitCandidate> v{HitCandidate{id(1), 0, kTopLeft}};
    REQUIRE(topmost_hit(v, PointerState{0.0f, 0.0f, false}) == id(1));   // top-left corner
    REQUIRE(topmost_hit(v, PointerState{0.2f, 0.2f, false}) == id(1));   // bottom-right
    REQUIRE(topmost_hit(v, PointerState{0.2f, 0.0f, false}) == id(1));   // top-right
    REQUIRE(topmost_hit(v, PointerState{0.20001f, 0.1f, false}) == EntityID{}); // just past
}

TEST_CASE("a zero-size button is hit only at its exact corner", "[ui][hit]")
{
    // A collapsed button (size {0,0}) has a one-point hit region rather than none, because
    // the comparison is inclusive. Asserted rather than left undefined: a scene that
    // animates a button's size to zero should not have it swallow a click from the middle
    // of the screen, and should not be a special case either.
    const std::vector<HitCandidate> v{HitCandidate{id(1), 0, Rect{0.5f, 0.5f, 0.0f, 0.0f}}};
    REQUIRE(topmost_hit(v, PointerState{0.5f, 0.5f, false}) == id(1));
    REQUIRE(topmost_hit(v, PointerState{0.51f, 0.5f, false}) == EntityID{});
}

TEST_CASE("the pointer's down flag does not affect what is hit", "[ui][hit]")
{
    // topmost_hit answers "what is under the pointer", never "was it pressed". Keeping the
    // button state out of this function is what lets hover and press share it.
    const std::vector<HitCandidate> v{HitCandidate{id(1), 0, kTopLeft}};
    REQUIRE(topmost_hit(v, PointerState{0.05f, 0.05f, true}) == id(1));
    REQUIRE(topmost_hit(v, PointerState{0.05f, 0.05f, false}) == id(1));
}
```

Register the file in `tests/CMakeLists.txt` beside the other `unit/ui/` entries (find them with `grep -n "unit/ui/" tests/CMakeLists.txt`), or none of these cases exist.

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build/debug 2>&1 | grep -m5 "error:"
```
Expected: FAIL at compile time — `engine/ui/ui_hit.hpp: No such file or directory`. A compile failure is the correct red; there is nothing to call yet.

- [ ] **Step 3: Implement `engine/ui/ui_hit.hpp`**

```cpp
#pragma once

#include <algorithm>
#include <vector>

#include "engine/ui/pointer.hpp"
#include "engine/ui/ui_rect.hpp"
#include "foundation/core/entity_id.hpp"

namespace mosaic::engine::ui
{
// One hit-testable region, flattened out of the world so the tie-break needs no World,
// no component storage and no pointer provider to test.
struct HitCandidate
{
    mosaic::foundation::EntityID e;
    int                          order = 0;
    Rect                         rect;
};

// Which candidate is under the pointer, resolving overlaps by z-order. Nil EntityID when
// nothing is hit.
//
// TWO rules, and the second is the one that was broken:
//
//   1. Higher `order` wins. `order` is a signed int because a scene file can author -1 to
//      push a button behind its neighbours.
//   2. Among EQUAL orders, the candidate later in `candidates` wins -- because the panel
//      pass stable-sorts ascending and draws in that order, so the entity drawn last is
//      the one on top. Hit-testing adopting the same rule makes it agree with what the
//      user SEES by construction rather than by coincidence.
//
// Before this function, UiSystem took the FIRST button under the pointer while the
// renderer drew the LAST one on top -- opposite ends of the same list -- so a click on two
// overlapping equal-order buttons went to the one the user could not see.
//
// Containment is filtered BEFORE ordering, not after. Sorting first and then testing the
// single winner would return nothing whenever a high-order button sits elsewhere on screen
// while a lower-order one is genuinely under the pointer.
//
// Edges are inclusive on both sides, matching the comparison this replaces. A consequence
// worth stating rather than discovering: a zero-size rect is hit at exactly its corner.
inline mosaic::foundation::EntityID topmost_hit(const std::vector<HitCandidate>& candidates,
                                                const PointerState& pointer)
{
    const HitCandidate* best = nullptr;
    for (const HitCandidate& c : candidates)
    {
        const bool inside = pointer.x >= c.rect.x && pointer.x <= c.rect.x + c.rect.w
                            && pointer.y >= c.rect.y && pointer.y <= c.rect.y + c.rect.h;
        if (!inside)
        {
            continue;
        }
        // `>=` rather than `>` is rule 2: a later candidate of EQUAL order replaces the
        // earlier one, which is what "later in iteration is on top" means. With `>` the
        // first of an equal-order run would win, which is precisely today's bug.
        if (best == nullptr || c.order >= best->order)
        {
            best = &c;
        }
    }
    return best != nullptr ? best->e : mosaic::foundation::EntityID{};
}
} // namespace mosaic::engine::ui
```

A single pass with `>=` rather than a sort: it is O(n) instead of O(n log n), it cannot be broken by someone swapping `std::stable_sort` for `std::sort`, and the equal-order rule becomes one documented character instead of an invariant of the sort algorithm. The spec describes the behaviour as "stable_sort then back()"; this is the same answer by construction, and the three-candidate test pins it.

- [ ] **Step 4: Run to verify it passes**

```bash
cmake --build build/debug && ./build/debug/tests/mosaic_unit_tests "[ui][hit]"
```
Expected: PASS, 10 cases.

- [ ] **Step 5: Confirm the tier rules still hold**

```bash
python3 tools/ci/check_dependency_rules.py && python3 tools/ci/check_test_case_names.py
```
Expected: both exit 0 with no output. `ui_hit.hpp` includes only `engine/ui` siblings and `foundation/core/entity_id.hpp`.

- [ ] **Step 6: Commit**

```bash
git add engine/ui/ui_hit.hpp tests/unit/ui/ui_hit_test.cpp tests/CMakeLists.txt
git commit -F - <<'EOF'
feat(ui): topmost_hit -- z-order hit resolution, pure and headless

The whole tie-break in one function with no world and no pointer provider,
because the failure mode is picking the WRONG button among overlaps, which
produces plausible behaviour rather than broken behaviour: a click does
something, just not the thing under the cursor. Invisible to review and to
eyeballing alike.

Two rules. Higher order wins, signed because a scene can author -1 to push a
button behind its neighbours. And among EQUAL orders the later candidate wins,
because the panel pass stable-sorts ascending and draws in that order, so the
entity drawn last is on top -- which is the rule that was broken: UiSystem took
the FIRST button under the pointer while the renderer drew the LAST on top, so a
click on two overlapping equal-order buttons went to the button the user could
not see.

Containment is filtered BEFORE ordering. Sorting first and testing the single
winner returns nothing whenever a high-order button sits elsewhere on screen
while a lower-order one is genuinely under the pointer -- asserted, because it
reads as "the click did nothing" rather than as a tie-break bug.
EOF
```

---

### Task 2: `UiButton` gains `order`

**Files:**
- Modify: `engine/ui/ui_button.hpp`
- Test: `tests/unit/ui/ui_button_reflection_test.cpp` (exists)

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces: `UiButton::order` (`int`, default 0, declared **last** in the struct, reflected as `"order"`). Tasks 3 and 4 read it.

- [ ] **Step 1: Write the failing test**

Extend the round-trip in `tests/unit/ui/ui_button_reflection_test.cpp`. Read it first (`cat tests/unit/ui/ui_button_reflection_test.cpp`) — it already sets `anchor`, `pos`, `size` and `event` and asserts each after the round-trip. Add, beside the other assignments:

```cpp
    b.order  = 4; // non-default: a field left at its default round-trips either way
```
and after the round-trip:
```cpp
    REQUIRE(back.order == 4);
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build/debug 2>&1 | grep -m3 "error:"
```
Expected: FAIL at compile time — `'struct mosaic::engine::ui::UiButton' has no member named 'order'`.

- [ ] **Step 3: Implement**

In `engine/ui/ui_button.hpp`, add to the struct **after `event`**, so it is last:

```cpp
    // Z-order for hit resolution: higher wins when regions overlap, and among equal orders
    // the entity later in iteration wins (matching the panel pass's draw order). Signed so
    // a scene can author -1 to push a button behind its neighbours.
    //
    // UiButton was the only UI component without this -- UiPanel, UiImage and UiText each
    // carry one, all declared last, which is why this goes last too: it also keeps
    // demo_scene.hpp's positional brace init compiling, since adding it before `event`
    // would bind "hud_click" to an int.
    //
    // KNOWN COST, accepted deliberately: this order and the order of the UiPanel/UiImage a
    // user actually SEES are independent, so they can disagree. A button can claim to be
    // above a visual drawn below it, and the only symptom is a click landing on the wrong
    // thing. Validating that needs an ownership relation between a button and its visual,
    // which does not exist (UiParent is a transform hierarchy); recorded in the backlog.
    int         order  = 0;
```
and in `reflect`, after the `event` line:
```cpp
    visitor.member("order", b.order);
```

- [ ] **Step 4: Run to verify it passes**

```bash
cmake --build build/debug && ./build/debug/tests/mosaic_unit_tests "[ui][reflection]"
```
Expected: PASS. Nothing else should break: `order` is appended, so every positional `UiButton` brace init in the tree still compiles. Confirm there is exactly one such init and that it is untouched:
```bash
grep -rn "UiButton{" --include=*.hpp --include=*.cpp . | grep -v "^./build"
```
Expected: one hit in `app/run/demo_scene.hpp` (converted to designated initializers in Task 5), and none that fail to compile.

- [ ] **Step 5: Commit**

```bash
git add engine/ui/ui_button.hpp tests/unit/ui/ui_button_reflection_test.cpp
git commit -F - <<'EOF'
feat(ui): UiButton gains an order field for hit resolution

UiButton was the only UI component without one -- UiPanel, UiImage and UiText
each carry `int order = 0` declared last. Appended rather than inserted, both for
that consistency and because adding it before `event` would bind the demo's
positional "hud_click" to an int.

Signed, because a scene file can author -1 to push a button behind its
neighbours, and the hit resolution must order that normally rather than by
magnitude.

The known cost is recorded on the field itself: this order and the order of the
panel or image a user actually sees are independent and can disagree, with no
warning. Validating it needs a button-to-visual ownership relation that does not
exist today.
EOF
```

---

### Task 3: the press path resolves overlaps by z-order

**Files:**
- Modify: `engine/ui/ui_system.hpp` (the press block, currently `if (p.down && !was_down_)`)
- Test: `tests/unit/ui/ui_system_test.cpp` (exists — extend its `Harness`)

**Interfaces:**
- Consumes: `topmost_hit` / `HitCandidate` (Task 1); `UiButton::order` (Task 2).
- Produces: a private `UiSystem::candidates(World&)` helper returning `std::vector<HitCandidate>`, which Task 4 reuses for hover. Behaviour change: the press records the **topmost** button, not the first.

- [ ] **Step 1: Write the failing test**

`tests/unit/ui/ui_system_test.cpp` has a `Harness` (read it first: `sed -n '20,45p' tests/unit/ui/ui_system_test.cpp`) that builds a `World` with one `UiButton` at TopLeft `{0,0}..{0.2,0.2}` named `test_click`, a `UiSystem` driven by a mutable `ptr`, and a **synchronous** `ScriptEvent` capture. Extend it with a helper that adds a second overlapping button, rather than writing a second harness:

```cpp
    // A second button overlapping the first, at a caller-chosen order and event name, so a
    // test can assert WHICH of two stacked buttons answered.
    mosaic::foundation::EntityID add_overlapping(int order, const std::string& event)
    {
        auto e = world.create_entity();
        UiButton b;
        b.anchor = 0; b.pos = Vec2{0.0f, 0.0f}; b.size = Vec2{0.2f, 0.2f};
        b.event = event;
        b.order = order;
        world.add_component<UiButton>(e, b);
        return e;
    }
```

Then append:

```cpp
TEST_CASE("a click goes to the topmost of two overlapping buttons", "[ui][input]")
{
    // The behaviour change, and it is a fix to shipped code rather than a new feature: the
    // press used to take the FIRST button under the pointer. It gets its own case rather
    // than riding inside a hover test.
    //
    // The harness's own button has order 0 and is created FIRST, so an implementation that
    // kept taking the first match would emit "test_click" here.
    Harness h;
    h.add_overlapping(5, "on_top");

    h.ptr = {0.05f, 0.05f, true};  h.tick();
    h.ptr = {0.05f, 0.05f, false}; h.tick();

    REQUIRE(h.events.size() == 1);
    REQUIRE(h.events[0].name == "on_top");
}

TEST_CASE("a click goes to the later of two buttons sharing an order", "[ui][input]")
{
    // The equal-order rule through the real system. The harness's button is order 0 and
    // first; the added one is order 0 and second, so "later wins" picks the added one --
    // which is also the one the panel pass would draw on top.
    Harness h;
    h.add_overlapping(0, "added_second");

    h.ptr = {0.05f, 0.05f, true};  h.tick();
    h.ptr = {0.05f, 0.05f, false}; h.tick();

    REQUIRE(h.events.size() == 1);
    REQUIRE(h.events[0].name == "added_second");
}

TEST_CASE("a press on the topmost button still requires release inside it", "[ui][input]")
{
    // The press/release pairing must survive the z-order change: picking the topmost on
    // press must not make the release path match a different button.
    Harness h;
    h.add_overlapping(5, "on_top");

    h.ptr = {0.05f, 0.05f, true}; h.tick();  // press on the topmost
    h.ptr = {0.9f, 0.9f, false};  h.tick();  // release far away -> no click at all
    REQUIRE(h.events.empty());
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build/debug && ./build/debug/tests/mosaic_unit_tests "a click goes to the topmost of two overlapping buttons"
```
Expected: FAIL — one assertion, `h.events[0].name == "on_top"` with the actual value `test_click`, because the press still takes the first match. (If it instead fails to compile on `add_overlapping`, the harness edit in Step 1 was not applied.)

- [ ] **Step 3: Implement**

In `engine/ui/ui_system.hpp`, add the includes and a private helper, then replace the press block.

Includes — add beside the existing `engine/ui/` ones:
```cpp
#include <vector>

#include "engine/ui/ui_hit.hpp"
```

A private helper beside `hit`, replacing nothing yet:
```cpp
    // Every UiButton flattened into hit candidates. One pass per update, shared by the
    // press path and the hover path so the two can never disagree about which button is
    // under the pointer -- if they could, a click and its hover feedback would land on
    // different buttons, which reads as the UI lying rather than as a bug.
    static std::vector<HitCandidate> candidates(mosaic::engine::World& world)
    {
        std::vector<HitCandidate> out;
        world.for_each<UiButton>(
            [&](mosaic::foundation::EntityID e, UiButton& b)
            {
                out.push_back(HitCandidate{
                    e, b.order,
                    resolve_ui_rect(world, e, button_anchor(b.anchor), b.pos, b.size)});
            });
        return out;
    }
```

Then the press block becomes — note the early-out `if (pressed_ != EntityID{}) { return; }` is **deleted**, since that was the "first match wins" bug:
```cpp
        if (p.down && !was_down_)
        {
            // Press: record the TOPMOST button under the pointer. This used to take the
            // first button `for_each` yielded, while the panel pass draws the last of an
            // equal-order run on top -- so a click on two overlapping buttons went to the
            // one the user could not see.
            pressed_ = topmost_hit(candidates(world), p);
        }
```

The release block is unchanged: it already matches on `e != pressed_`, so it follows whichever button the press chose.

- [ ] **Step 4: Run to verify it passes**

```bash
cmake --build build/debug && ./build/debug/tests/mosaic_unit_tests "[ui][input]"
```
Expected: PASS, including the four pre-existing click cases — the single-button scenes they use are unaffected, because with one candidate "topmost" and "first" are the same answer.

- [ ] **Step 5: Run the whole suite**

```bash
ctest --test-dir build/debug 2>&1 | tail -4
```
Expected: all passing. `studio/` calls `press_consumed()`, whose meaning is unchanged.

- [ ] **Step 6: Commit**

```bash
git add engine/ui/ui_system.hpp tests/unit/ui/ui_system_test.cpp
git commit -F - <<'EOF'
fix(ui): a click goes to the topmost button, not the first one iterated

The press path recorded the FIRST button under the pointer -- an explicit
`if (pressed_ != EntityID{}) return;` early-out -- while the panel pass
stable-sorts ascending and draws in that order, so the entity drawn LAST is on
top. First-hit and last-drawn are opposite ends of the same list, which means a
click on two overlapping equal-order buttons went to the button the user could
NOT see. A wrong answer, not an unspecified tie-break.

Candidates are now flattened once per update by a helper the hover path will
share, so press and hover can never disagree about which button is under the
pointer. Two paths that could disagree would land a click and its feedback on
different buttons, which reads as the UI lying rather than as a bug.

The four pre-existing click cases are untouched and still pass: with one
candidate, "topmost" and "first" are the same answer.
EOF
```

---

### Task 4: hover enter and exit

**Files:**
- Modify: `engine/ui/ui_system.hpp` (new `hovered_` member; the hover transition in `update`)
- Test: `tests/unit/ui/ui_system_test.cpp`

**Interfaces:**
- Consumes: `topmost_hit` (Task 1); `UiButton::order` (Task 2); `UiSystem::candidates` (Task 3).
- Produces: `hover_enter` / `hover_exit` actions on the button's existing `b.event` channel, with the same `{entity, action}` payload shape the click path emits. `UiSystem::hovered()` accessor for callers that want the state rather than the events.

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/ui/ui_system_test.cpp`. The harness's `ScriptEvent` subscriber is **synchronous** (`EventBus::subscribe` fires before `emit` returns), so an event emitted during a tick is visible immediately after that tick — no extra tick needed to observe it.

```cpp
TEST_CASE("hover enter fires once when the pointer moves onto a button", "[ui][input]")
{
    Harness h;
    h.ptr = {0.05f, 0.05f, false}; h.tick();
    REQUIRE(h.events.size() == 1);
    REQUIRE(h.events[0].name == "test_click"); // the button's own channel, not a new one
}

TEST_CASE("hover enter does not re-fire while the pointer sits still", "[ui][input]")
{
    // The rule that makes enter/exit worth more than a polled flag, and the natural wrong
    // implementation -- emit whenever the pointer is inside -- would look CORRECT in the
    // demo (the panel would simply stay bright) while flooding any script that counts.
    Harness h;
    h.ptr = {0.05f, 0.05f, false}; h.tick();
    h.ptr = {0.06f, 0.06f, false}; h.tick(); // moved, still inside
    h.ptr = {0.06f, 0.06f, false}; h.tick(); // not moved at all
    REQUIRE(h.events.size() == 1);
}

TEST_CASE("hover exit fires once when the pointer leaves the button", "[ui][input]")
{
    Harness h;
    h.ptr = {0.05f, 0.05f, false}; h.tick(); // enter
    h.ptr = {0.9f, 0.9f, false};   h.tick(); // leave
    h.ptr = {0.95f, 0.95f, false}; h.tick(); // still outside -> nothing more
    REQUIRE(h.events.size() == 2);
}

TEST_CASE("the hover payload names the entity and the action", "[ui][input]")
{
    // Same payload shape the click path already emits, so a script filters on `action`
    // rather than learning a second convention.
    Harness h;
    h.ptr = {0.05f, 0.05f, false}; h.tick();
    REQUIRE(h.events.size() == 1);
    REQUIRE(h.events[0].payload.is<Table>());
    const Table& table = h.events[0].payload.as<Table>();
    REQUIRE(table.find("action") != nullptr);
    REQUIRE(table.find("action")->as<std::string>() == "hover_enter");
    // Written as a uint64_t by the emitter, so read as one -- as<std::int64_t>() on a
    // uint64_t variant would throw rather than convert.
    REQUIRE(table.find("entity") != nullptr);
    REQUIRE(table.find("entity")->is<std::uint64_t>());
}

TEST_CASE("moving between two buttons emits exit before enter", "[ui][input]")
{
    // ORDER asserted, not just presence. Enter-before-exit leaves a script that tracks
    // "the currently hovered thing" holding B and then being told to clear A -- i.e.
    // clearing the wrong one, which presents as a lost exit event.
    //
    // The second button is placed so the two do NOT overlap, so a single pointer move
    // crosses from one into the other.
    Harness h;
    auto e = h.world.create_entity();
    UiButton b;
    b.anchor = 0; b.pos = Vec2{0.5f, 0.5f}; b.size = Vec2{0.2f, 0.2f};
    b.event = "second";
    h.world.add_component<UiButton>(e, b);

    h.ptr = {0.05f, 0.05f, false}; h.tick(); // enter the first
    h.events.clear();
    h.ptr = {0.55f, 0.55f, false}; h.tick(); // directly into the second

    REQUIRE(h.events.size() == 2);
    REQUIRE(h.events[0].name == "test_click"); // exit from the first, on ITS channel
    REQUIRE(h.events[0].payload.as<Table>().find("action")->as<std::string>() == "hover_exit");
    REQUIRE(h.events[1].name == "second");
    REQUIRE(h.events[1].payload.as<Table>().find("action")->as<std::string>() == "hover_enter");
}

TEST_CASE("hover tracks the pointer during a press that began on empty space", "[ui][input]")
{
    // Hover is independent of `down`. Coupling them would mean a drag starting on empty
    // space suppresses all hover, which reads as the UI freezing mid-gesture.
    Harness h;
    h.ptr = {0.9f, 0.9f, true};    h.tick(); // press outside any button
    h.events.clear();
    h.ptr = {0.05f, 0.05f, true};  h.tick(); // drag onto the button, still down
    REQUIRE(h.events.size() == 1);
    REQUIRE(h.events[0].payload.as<Table>().find("action")->as<std::string>() == "hover_enter");
}

TEST_CASE("a click and its hover both land on the topmost button", "[ui][input]")
{
    // press and hover share one candidate pass, so they cannot disagree. If they could, a
    // button would light up while a different one answered the click.
    Harness h;
    h.add_overlapping(5, "on_top");

    h.ptr = {0.05f, 0.05f, false}; h.tick();           // hover
    REQUIRE(h.events.size() == 1);
    REQUIRE(h.events[0].name == "on_top");

    h.events.clear();
    h.ptr = {0.05f, 0.05f, true};  h.tick();
    h.ptr = {0.05f, 0.05f, false}; h.tick();
    // the click, on the same channel the hover used
    REQUIRE(!h.events.empty());
    REQUIRE(h.events.back().name == "on_top");
}

TEST_CASE("a button covered after the press no longer receives the click", "[ui][input]")
{
    // A behaviour change this task introduces deliberately, so it is pinned rather than
    // left as prose. The release path used to ask "is the pointer inside the button it
    // pressed"; it now asks "is that button still the TOPMOST under the pointer". If a
    // higher-order button appears over it between press and release, clicking the one
    // underneath would be clicking through something the user can see.
    //
    // Reachable because a scene can add or reorder UI at runtime -- which is exactly what
    // the hover subscriber in Task 5 does to a sibling component.
    Harness h;
    h.ptr = {0.05f, 0.05f, true}; h.tick();   // press: only one button exists, so it wins
    h.add_overlapping(5, "on_top");            // a higher button appears over it
    h.ptr = {0.05f, 0.05f, false}; h.tick();   // release

    // No click on either: the pressed button is no longer topmost, and the new button was
    // never pressed.
    const bool any_click = [&] {
        for (const auto& ev : h.events)
        {
            const auto* a = ev.payload.as<Table>().find("action");
            if (a != nullptr && a->as<std::string>() == "click") { return true; }
        }
        return false;
    }();
    REQUIRE_FALSE(any_click);
}

TEST_CASE("a UiSystem with no pointer provider emits no hover at all", "[ui][input]")
{
    // The constructor takes a std::function that may be EMPTY, and update falls back to
    // PointerState{} -- which is {0, 0, false}, a real position inside any button anchored
    // TopLeft at {0,0}. The harness's button is exactly that. Harmless before this slice
    // (no press ever arrives); with hover it would emit a phantom hover_enter for a pointer
    // that does not exist.
    World world;
    std::vector<ScriptEvent> events;
    auto e = world.create_entity();
    UiButton b;
    b.anchor = 0; b.pos = Vec2{0.0f, 0.0f}; b.size = Vec2{0.2f, 0.2f};
    b.event = "phantom";
    world.add_component<UiButton>(e, b);
    world.add_system<UiSystem>(std::function<PointerState()>{}); // no provider
    world.events().subscribe<ScriptEvent>(
        [&](const ScriptEvent& ev) { events.push_back(ev); });

    world.tick(0.016f);
    world.tick(0.016f);
    REQUIRE(events.empty());
}

TEST_CASE("destroying a hovered button emits no exit and clears the state", "[ui][input]")
{
    // hovered_ holds an EntityID, not a component. If the entity goes away while hovered,
    // the exit event's channel name (b.event) is unknowable -- so emitting nothing is the
    // only honest option. What must NOT happen is hovered_ keeping the stale id, because
    // then the next genuine hover of a different button would be treated as "no change"
    // and emit no enter.
    Harness h;
    h.ptr = {0.05f, 0.05f, false}; h.tick(); // enter the harness's button
    h.events.clear();

    h.world.destroy_entity(h.hovered_entity());
    h.tick();                                 // notices the hovered entity is gone
    REQUIRE(h.events.empty());                // no channel to emit on

    // A fresh button in the same place must still produce an enter.
    auto e = h.world.create_entity();
    UiButton b;
    b.anchor = 0; b.pos = Vec2{0.0f, 0.0f}; b.size = Vec2{0.2f, 0.2f};
    b.event = "replacement";
    h.world.add_component<UiButton>(e, b);
    h.tick();
    REQUIRE(h.events.size() == 1);
    REQUIRE(h.events[0].name == "replacement");
}
```

Two helpers this needs on `Harness`: `hovered_entity()`, and the button entity id. Add to `Harness`:

```cpp
    mosaic::foundation::EntityID button; // the entity created in the constructor

    // What the UiSystem currently considers hovered, for the destroyed-while-hovered case.
    mosaic::foundation::EntityID hovered_entity() const { return button; }
```
and assign `button = e;` in the constructor where the first entity is created.

`World::destroy_entity(EntityID)` exists (`world.hpp:37`) and is **deferred** — it is
serviced by `tick()`, not immediately — which is why the case destroys and then ticks before
asserting. `world.remove_component<UiButton>(button)` (`world.hpp:60`) is the immediate
alternative and exercises the same branch; either is acceptable, but do not assert anything
between the destroy and the tick.

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build/debug && ./build/debug/tests/mosaic_unit_tests "hover enter fires once when the pointer moves onto a button"
```
Expected: FAIL — `h.events.size() == 1` with an actual value of 0, because no hover is emitted yet.

- [ ] **Step 3: Implement**

In `engine/ui/ui_system.hpp`:

Extract the payload emission so the click and hover paths cannot drift apart. Add beside `candidates`:
```cpp
    // Emit `action` on the button's own event channel. One helper for click and hover
    // both: two copies of this would be two chances for the payload shapes to diverge,
    // and a script filtering on `action` would then work for one and not the other.
    //
    // Looks the button up rather than taking it by reference, because the hover path must
    // emit for an entity it recorded on a PREVIOUS update -- which may since have lost its
    // UiButton. Returns false in that case, with nothing emitted: the channel name lived on
    // the component, so there is no honest name to emit on.
    static bool emit_button_action(mosaic::engine::World& world,
                                   mosaic::foundation::EntityID e, const char* action)
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
        world.events().emit(
            mosaic::engine::script::ScriptEvent{b->event, ser::Value(std::move(payload))});
        return true;
    }
```

Rewrite `update` so one candidate pass serves both paths:
```cpp
    void update(mosaic::engine::World& world, float /*dt*/) override
    {
        // An absent provider means there is no pointer, NOT a pointer at the origin.
        // PointerState{} is {0, 0, false}, which sits inside any button anchored TopLeft at
        // {0,0} -- so falling back to it would emit a phantom hover for a pointer that does
        // not exist. Harmless for clicks (no press ever arrives) and not for hover.
        if (!pointer_)
        {
            return;
        }
        const PointerState p = pointer_();
        const std::vector<HitCandidate> cands = candidates(world);
        const mosaic::foundation::EntityID top = topmost_hit(cands, p);

        // --- hover, before press, so a script sees the enter for the button it is about
        // --- to be told was clicked.
        if (top != hovered_)
        {
            if (hovered_ != mosaic::foundation::EntityID{})
            {
                // Return value deliberately ignored: a false means the hovered entity lost
                // its UiButton, and clearing hovered_ below is the whole response.
                emit_button_action(world, hovered_, "hover_exit");
            }
            if (top != mosaic::foundation::EntityID{})
            {
                emit_button_action(world, top, "hover_enter");
            }
            hovered_ = top;
        }

        if (p.down && !was_down_)
        {
            // Press: the TOPMOST button under the pointer, not the first one iterated.
            pressed_ = top;
        }
        else if (!p.down && was_down_)
        {
            if (pressed_ != mosaic::foundation::EntityID{} && topmost_hit(cands, p) == pressed_)
            {
                emit_button_action(world, pressed_, "click");
            }
            pressed_ = mosaic::foundation::EntityID{};
        }
        was_down_ = p.down;
    }

    // What the pointer is currently over, for callers that want the state rather than the
    // events. Nil when nothing is hovered.
    mosaic::foundation::EntityID hovered() const { return hovered_; }
```
and the new member beside the others:
```cpp
    mosaic::foundation::EntityID hovered_  = {};
```

The release path no longer walks `for_each`: `topmost_hit(cands, p) == pressed_` is the same question ("is the pointer still on the button it pressed") asked against the shared candidate list. Note it compares the **topmost**, so a release under a button that has since been covered by a higher-order one correctly does not click — which is the consistent reading of "release inside the pressed button".

- [ ] **Step 4: Run to verify it passes**

```bash
cmake --build build/debug && ./build/debug/tests/mosaic_unit_tests "[ui][input]"
```
Expected: PASS, all cases including the four original click cases.

- [ ] **Step 5: Run the whole suite**

```bash
ctest --test-dir build/debug 2>&1 | tail -4
```
Expected: all passing. Watch for `studio/` tests: `press_consumed()` is unchanged, but the viewport pointer tests drive `UiSystem` through the studio provider and now also produce hover events — which no existing assertion counts, but confirm rather than assume.

- [ ] **Step 6: Commit**

```bash
git add engine/ui/ui_system.hpp tests/unit/ui/ui_system_test.cpp
git commit -F - <<'EOF'
feat(ui): hover enter and exit events on the button's own channel

Reuses `b.event` with action "hover_enter"/"hover_exit", exactly as the click path
already emits {entity, action} -- no new component field and no second channel, so
a script filters on `action` rather than learning a second convention. One helper
emits for both paths, because two copies are two chances for the payload shapes to
drift apart.

Enter fires on the TRANSITION, not while the pointer is inside. The natural wrong
implementation would look correct in the demo -- the panel would simply stay
bright -- while flooding any script that counts events.

Exit precedes enter when moving between buttons. The reverse leaves a script
tracking "the hovered thing" holding B and then being told to clear A, which
presents as a lost exit event.

Two edge cases the spec implied and neither path would have hit:

An absent pointer provider now returns early instead of falling back to
PointerState{}. That fallback is {0, 0, false} -- a real position inside any
button anchored TopLeft at {0,0} -- so hover would have emitted a phantom enter
for a pointer that does not exist. Harmless for clicks, since no press arrives.

A hovered entity that loses its UiButton emits nothing, because the channel name
lived on the component and there is no honest name left to emit on -- but hovered_
is still cleared, so the next genuine hover is not mistaken for "no change" and
silently swallowed.
EOF
```

---

### Task 5: make it visible, probe it, and hand it off

**Files:**
- Modify: `app/run/demo_scene.hpp` (hover subscriber; `UiButton` init made designated)
- Modify: `docs/project/next-steps.md`, `docs/project/deferred-work-backlog.md`

**Interfaces:**
- Consumes: everything above. Terminal task.

- [ ] **Step 1: Run the mutation probes**

Derived from the **code**, statement by statement — not from the assertion list, which cannot find unasserted code. Script them into one run (each needs a rebuild), apply, rebuild, run `"[ui]"`, confirm the expected red, revert, and **rebuild before judging the baseline** (a restored source with a stale binary reports the mutant's results).

| # | Mutation | Must turn red |
|---|---|---|
| 1 | `ui_hit.hpp`: `c.order >= best->order` becomes `>` | "equal orders resolve to the one later in iteration order", "a click goes to the later of two buttons sharing an order" |
| 2 | `ui_hit.hpp`: `c.order >= best->order` becomes `<=` | both "higher order wins" sections |
| 3 | `ui_hit.hpp`: drop the `if (!inside) continue;` filter | "only candidates under the pointer compete on order", "a single candidate is hit only when the pointer is inside it" |
| 4 | `ui_hit.hpp`: `pointer.x <= c.rect.x + c.rect.w` becomes `<` | "the pointer exactly on a rect edge counts as inside", "a zero-size button is hit only at its exact corner" |
| 5 | `ui_hit.hpp`: return `candidates.front().e` when any hit | the order cases |
| 6 | `ui_system.hpp`: emit `hover_enter` whenever `top != EntityID{}` (drop the `top != hovered_` guard) | "hover enter does not re-fire while the pointer sits still" |
| 7 | `ui_system.hpp`: swap the exit and enter emission order | "moving between two buttons emits exit before enter" |
| 8 | `ui_system.hpp`: drop `hovered_ = top;` | "hover enter does not re-fire while the pointer sits still" |
| 9 | `ui_system.hpp`: gate the hover block on `p.down` | "hover tracks the pointer during a press that began on empty space" |
| 10 | `ui_system.hpp`: restore the `if (!pointer_)` fallback to `PointerState{}` | "a UiSystem with no pointer provider emits no hover at all" |
| 11 | `ui_system.hpp`: in `emit_button_action`, `return true` when `b == nullptr` (emitting on a default-constructed name) | "destroying a hovered button emits no exit and clears the state" |
| 12 | `ui_button.hpp`: drop the `visitor.member("order", b.order);` line | the `UiButton` reflection round-trip |

**One of the spec's eight named probes does not apply, and that is a deviation worth stating
rather than quietly dropping.** The spec lists "use `std::sort` instead of `stable_sort`
(which breaks the equal-order rule only)". This plan's `topmost_hit` deliberately does **not
sort** — it is a single pass with `>=`, which is O(n), cannot be broken by swapping a sort
algorithm, and makes the equal-order rule one documented character instead of an invariant of
`stable_sort`. Probe 1 (`>=` becomes `>`) is the direct equivalent: it breaks exactly the
equal-order case and nothing else, which is what the spec's probe was testing for. Note this
in the handoff so a reader comparing spec to implementation sees a decision, not drift.

Record each result. A probe that produces no red is a finding to write down, not a failure to hide.

- [ ] **Step 2: Make hover visible in the demo**

On the events-only design the engine shows a user **nothing**, so this step is what makes the slice checkable by eye rather than an optional flourish.

`app/run/demo_scene.hpp` already has a synchronous `ScriptEvent` subscriber that logs `hud_click` (around line 295). Add a hover subscriber beside it. Use **`subscribe_deferred`**, not `subscribe`:

```cpp
    // Demo: brighten the HUD bar while the pointer is over its button, which is the only
    // thing that makes U6's hover events visible -- the engine renders no hover feedback
    // of its own, by design.
    //
    // DEFERRED, not synchronous. A synchronous handler runs inside UiSystem::update while
    // `world.for_each<UiButton>` is on the stack, and this one MUTATES a component. A
    // different component type is safe today, but the deferred discipline exists precisely
    // to remove that reentrancy hazard, and one tick of latency on a hover tint is
    // imperceptible.
    world.events().subscribe_deferred<mosaic::engine::script::ScriptEvent>(
        [&world](const mosaic::engine::script::ScriptEvent& e)
        {
            if (e.name != "hud_click") { return; }
            if (!e.payload.is<mosaic::foundation::serialization::Table>()) { return; }
            const auto* table = &e.payload.as<mosaic::foundation::serialization::Table>();
            const auto* action = table->find("action");
            if (action == nullptr || !action->is<std::string>()) { return; }
            const bool enter = (action->as<std::string>() == "hover_enter");
            const bool exit  = (action->as<std::string>() == "hover_exit");
            if (!enter && !exit) { return; }
            const auto* ent = table->find("entity");
            if (ent == nullptr || !ent->is<std::uint64_t>()) { return; }
            // The HUD button and its UiPanel are the SAME entity in this scene, so the
            // payload's entity id addresses the panel directly -- no button-to-visual
            // relationship is needed or implied.
            //
            // EntityID's single-argument ctor takes the RAW 64-bit handle, which is exactly
            // what the emitter wrote, so it round-trips index AND generation. Rebuilding it
            // from a truncated 32-bit index would produce a generation-0 handle that
            // matches nothing.
            auto* panel = world.get_component<mosaic::engine::ui::UiPanel>(
                mosaic::foundation::EntityID{ent->as<std::uint64_t>()});
            if (panel == nullptr) { return; }
            panel->alpha = enter ? 0.9f : 0.6f;
        });
```

The `Value` API used above is the real one, verified while writing this plan: `is<T>()`,
`as<T>()` returning a reference, and `Table::find(key)` returning a `const Value*` (**not**
an iterator, and there is no `at()`). `as<T>()` on the wrong alternative **throws**, so every
read is guarded by `is<T>()` first. The payload's `entity` is a `std::uint64_t`.

Also convert the existing `UiButton` init (around line 674) to designated initializers, so a future field cannot silently re-bind it:

```cpp
    world.add_component<mosaic::engine::ui::UiButton>(camera,
        mosaic::engine::ui::UiButton{
            .anchor = static_cast<int>(mosaic::engine::ui::Anchor::TopLeft),
            .pos    = mosaic::foundation::math::Vec2{0.02f, 0.02f},
            .size   = mosaic::foundation::math::Vec2{0.25f, 0.06f},
            .event  = "hud_click"});
```

- [ ] **Step 3: Verify the suite and both entry points**

```bash
cmake --build build/debug && ctest --test-dir build/debug 2>&1 | tail -4
python3 tools/ci/check_dependency_rules.py && python3 tools/ci/check_test_case_names.py && echo "ci gates clean"
timeout 180 ./build/debug/app/mosaic run --window --ticks 90 2>&1 | tail -2; echo "exit=${PIPESTATUS[0]}"
timeout 180 ./build/debug/app/mosaic editor --ticks 90 2>&1 | tail -2; echo "exit=${PIPESTATUS[0]}"
```
Expected: all tests passing, both gates clean, both commands exit 0 with no `VUID` / `validation` / `error` lines. The demo-scene entity-count tests are unaffected — no entity or component is added, only a subscriber and a field default.

- [ ] **Step 4: Check whether hover sticks when the pointer leaves the window**

The spec names this as an unresolved risk. `PointerState` has no "outside the window" signal, so if the window provider reports stale coordinates after the pointer leaves, hover would stay on with the mouse elsewhere.

```bash
grep -rn "PointerState\|cursor_pos\|GetCursorPos\|glfwGetCursorPos" app/run/*.cpp app/run/*.hpp | grep -v editor_play_policy | head
```
Report what the window path actually does. If it does go stale, **do not** paper over it with a timeout in `UiSystem` — the fix belongs in the provider, and the honest outcome is a backlog item plus a line in the handoff.

- [ ] **Step 5: Report what a human must check**

Screenshots are unavailable in this sandbox, so do not claim visual confirmation. Report:

> With `mosaic run --window`: moving the pointer over the HUD bar at the top-left should **brighten it**, and moving away should dim it back. In `mosaic editor` it will do nothing until you press **Play** — hover arrives through the same play-mode-gated provider as clicks, and the editor now starts stopped.

- [ ] **Step 6: Retire the backlog entry and write the handoff**

In `docs/project/deferred-work-backlog.md`, strike through **U6 interaction depth** with `**SHIPPED 2026-10-10**` and what actually happened, keeping the original text. State explicitly that capture/drag remains open, and that the two accepted costs (a button's `order` can disagree with its visual's; the engine renders no hover feedback) are still live items rather than resolved by this slice.

Add a `**Last updated:** 2026-10-10 (...)` entry at the top of `docs/project/next-steps.md`, matching the existing dense-parenthetical style, covering: what shipped; that the motivating bug was a **wrong** answer rather than an undefined one (first-hit versus last-drawn being opposite ends of the same list, so a click went to the button the user could not see); that an earlier scoping read of that file reached the opposite conclusion and was corrected; why the tie-break is a pure function and the condition for revisiting it (hit resolution needing the hierarchy, e.g. U9 clipping); the two accepted costs; the two edge cases found by writing the Review Focus list rather than by testing (the phantom hover from an absent provider, and the hovered entity losing its component); the probe results including any that produced no red; the gate count; and that hover feedback is visible only in `mosaic run --window` with visual confirmation outstanding.

- [ ] **Step 7: Commit, push, open the PR**

```bash
git add app/run/demo_scene.hpp docs/project/next-steps.md docs/project/deferred-work-backlog.md
git commit -F - <<'EOF'
docs: U6 interaction depth shipped, and the demo shows hover

The demo brightens the HUD bar while the pointer is over its button. On the
events-only design the engine renders no hover feedback at all, so this is what
makes the slice checkable by eye rather than an optional flourish.

The subscriber is DEFERRED rather than synchronous: a synchronous handler runs
inside UiSystem::update with `world.for_each<UiButton>` on the stack, and this one
mutates a component. One tick of latency on a hover tint is imperceptible, and the
deferred discipline exists precisely to remove that reentrancy hazard.

The demo's UiButton init becomes designated, so a future field cannot silently
re-bind it the way adding fields to UiImage re-bound `tint` during U7.
EOF
git push -u origin u6-interaction-depth
```
Then open a PR against `main` and request review before merging.

---
