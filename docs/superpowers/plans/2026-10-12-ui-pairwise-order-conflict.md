# UI Pairwise Order Conflict Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Warn, at editor time, when two overlapping `UiButton`s are ranked one way by `UiButton::order` (which decides the click) and the other way by `UiPanel::order` (which decides what the user sees on top).

**Architecture:** One pure function over the `World` finds the conflicting pairs; the button-rect flattening it needs is extracted out of `UiSystem` so the check polices the exact rects the hit test uses; a shared string-keyed ledger de-duplicates both this and the existing per-entity lint so an every-frame check warns only on transitions.

**Tech Stack:** C++20, header-only `engine/ui`, Catch2 tests, CMake/Ninja.

**Spec:** `docs/superpowers/specs/2026-10-12-ui-pairwise-order-conflict-design.md`

## Global Constraints

- Tier contract: `engine/` depends only on `foundation/`; nothing in `engine/` may include `studio/` or `app/`. Enforced by `tools/ci/check_dependency_rules.py`.
- `engine/ui` is header-only. New code goes in headers with `inline`.
- Catch2 test names: no leading dashes, no unbalanced `[`, no commas.
- Every new test file must be added to `tests/CMakeLists.txt` or it silently never runs.
- Diagnostics are editor-time only: `app/run/editor.cpp` may call the check; `app/run/run.cpp` (headless `mosaic run`) must not.
- No component is ever written by a diagnostic — read-only over the `World`.
- Build: `cmake --build build/debug -j4`. Suite: `ctest` from `build/debug`. Lints: `python3 tools/ci/check_dependency_rules.py` and `python3 tools/ci/check_test_case_names.py`.
- Commit messages end with the two attribution lines used by this session's other commits.

## Review Focus

Five input classes the spec implies but which no task's happy path exercises, most likely to bite first:

1. **A `UiParent` cycle authored in a scene** — `resolve_parent_frame` depth-caps at 16; the check must terminate and still produce rects. Test added in Task 2, Step 13.
2. **Sign-only order differences (`-2` against `2`)** — the exact gap a mutation probe found in #240: a comparison written on magnitudes passes every other case. Test added in Task 2, Step 11.
3. **Zero-size button rects** — `topmost_hit` documents that a zero-size rect is hit at exactly its corner, so two degenerate buttons at one point genuinely conflict under inclusive overlap. Test added in Task 2, Step 12.
4. **A stale or nil `UiParent` target** — the frame falls back to the whole screen, which can make a pair overlap that otherwise would not; must not crash or read freed memory. Test added in Task 2, Step 14.
5. **A button whose `UiPanel` lives on a different entity (or nowhere)** — must be skipped whole, never half-compared against a neighbour that does have one. Test added in Task 2, Step 9.

---

### Task 1: Extract the button-rect flattening

**Files:**
- Create: `engine/ui/ui_button_rects.hpp`
- Modify: `engine/ui/ui_system.hpp` (delete `button_anchor` at lines 21-25, delete `candidates` body at lines 123-138, add the include)
- Test: `tests/unit/ui/ui_button_rects_test.cpp` (create)
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `HitCandidate` from `engine/ui/ui_hit.hpp`; `resolve_ui_rect` from `engine/ui/ui_hierarchy.hpp`.
- Produces: `mosaic::engine::ui::button_anchor(int) -> Anchor` and `mosaic::engine::ui::button_hit_candidates(World&) -> std::vector<HitCandidate>`. Task 2 calls `button_hit_candidates`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/ui/ui_button_rects_test.cpp`:

```cpp
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/ui/ui_button.hpp"
#include "engine/ui/ui_button_rects.hpp"
#include "engine/ui/ui_panel.hpp"
#include "engine/ui/ui_parent.hpp"
#include "engine/world/world.hpp"

using Catch::Approx;
using mosaic::engine::World;
using mosaic::engine::ui::Anchor;
using mosaic::engine::ui::button_anchor;
using mosaic::engine::ui::button_hit_candidates;
using mosaic::engine::ui::UiButton;
using mosaic::engine::ui::UiPanel;
using mosaic::engine::ui::UiParent;

TEST_CASE("button_hit_candidates carries each button's entity order and rect", "[ui][buttonrects]")
{
    // The shape UiSystem's press and hover paths both consume. If this drifts from what
    // UiSystem used to build, every hit test in the engine moves with it.
    World      world;
    const auto e = world.create_entity();
    UiButton   b;
    b.anchor = static_cast<int>(Anchor::TopLeft);
    b.pos    = {0.25f, 0.5f};
    b.size   = {0.1f, 0.2f};
    b.order  = 4;
    world.add_component<UiButton>(e, b);

    const auto cands = button_hit_candidates(world);

    REQUIRE(cands.size() == 1);
    REQUIRE(cands[0].e == e);
    REQUIRE(cands[0].order == 4);
    REQUIRE(cands[0].rect.x == Approx(0.25f));
    REQUIRE(cands[0].rect.y == Approx(0.5f));
    REQUIRE(cands[0].rect.w == Approx(0.1f));
    REQUIRE(cands[0].rect.h == Approx(0.2f));
}

TEST_CASE("button_hit_candidates resolves a nested button inside its parent frame", "[ui][buttonrects]")
{
    // Proves the flatten goes through resolve_ui_rect rather than resolve_rect: a child of
    // a half-screen panel occupies half the space its own numbers describe.
    World      world;
    const auto parent = world.create_entity();
    UiPanel    p;
    p.anchor = static_cast<int>(Anchor::TopLeft);
    p.pos    = {0.0f, 0.0f};
    p.size   = {0.5f, 0.5f};
    world.add_component<UiPanel>(parent, p);

    const auto child = world.create_entity();
    UiButton   b;
    b.anchor = static_cast<int>(Anchor::TopLeft);
    b.pos    = {0.2f, 0.2f};
    b.size   = {0.4f, 0.4f};
    world.add_component<UiButton>(child, b);
    world.add_component<UiParent>(child, UiParent{parent});

    const auto cands = button_hit_candidates(world);

    REQUIRE(cands.size() == 1);
    REQUIRE(cands[0].rect.x == Approx(0.1f)); // 0.2 * 0.5
    REQUIRE(cands[0].rect.w == Approx(0.2f)); // 0.4 * 0.5
}

TEST_CASE("button_anchor clamps an out-of-range anchor to TopLeft", "[ui][buttonrects]")
{
    REQUIRE(button_anchor(0) == Anchor::TopLeft);
    REQUIRE(button_anchor(8) == Anchor::BottomRight);
    REQUIRE(button_anchor(9) == Anchor::TopLeft);
    REQUIRE(button_anchor(-1) == Anchor::TopLeft);
}

TEST_CASE("button_hit_candidates is empty with no buttons", "[ui][buttonrects]")
{
    World world;
    world.create_entity();
    REQUIRE(button_hit_candidates(world).empty());
}
```

Add to `tests/CMakeLists.txt` beside the other `unit/ui/` entries:

```cmake
  unit/ui/ui_button_rects_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build/debug --target mosaic_unit_tests -j4`
Expected: FAIL to compile — `engine/ui/ui_button_rects.hpp: No such file or directory`.

- [ ] **Step 3: Create the header**

Create `engine/ui/ui_button_rects.hpp`:

```cpp
#pragma once

#include <vector>

#include "engine/ui/ui_button.hpp"
#include "engine/ui/ui_hierarchy.hpp"
#include "engine/ui/ui_hit.hpp"
#include "engine/ui/ui_rect.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"

namespace mosaic::engine::ui
{
// Clamp an int to a ui::Anchor (out of range -> TopLeft). Mirrors render's to_anchor and
// ui_hierarchy's hierarchy_anchor; the triplication is pre-existing and recorded in the
// backlog rather than folded into this slice.
inline Anchor button_anchor(int a)
{
    return (a >= 0 && a <= 8) ? static_cast<Anchor>(a) : Anchor::TopLeft;
}

// Every UiButton flattened into hit candidates.
//
// SHARED ON PURPOSE, and this is the whole reason the function moved out of UiSystem. The
// press path, the hover path AND the order-conflict diagnostic must all reason about the
// SAME rects: a diagnostic that resolved buttons its own way would police rectangles
// nobody ever clicks, and would drift the first time either side changed. One definition
// means a conflict it reports is a conflict the hit test will actually honour.
inline std::vector<HitCandidate> button_hit_candidates(mosaic::engine::World& world)
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
} // namespace mosaic::engine::ui
```

- [ ] **Step 4: Run test to verify it passes**

Run: `./build/debug/tests/mosaic_unit_tests "[buttonrects]"`
Expected: PASS, 4 test cases.

- [ ] **Step 5: Point UiSystem at the shared definition**

In `engine/ui/ui_system.hpp`, delete the local `button_anchor` (lines 21-25) and add the include beside the others:

```cpp
#include "engine/ui/ui_button_rects.hpp"
```

Replace the body of `candidates` with a delegation, keeping the method so no call site changes:

```cpp
    // Every UiButton flattened into hit candidates. One pass per update, shared by the
    // press path and the hover path so the two can never disagree about which button is
    // under the pointer -- if they could, a click and its hover feedback would land on
    // different buttons, which reads as the UI lying rather than as a bug.
    //
    // The flatten itself now lives in ui_button_rects.hpp, because the order-conflict
    // diagnostic has to agree with it exactly.
    static std::vector<HitCandidate> candidates(mosaic::engine::World& world)
    {
        return button_hit_candidates(world);
    }
```

- [ ] **Step 6: Run the whole suite to verify the refactor changed nothing**

Run: `cmake --build build/debug -j4 && (cd build/debug && ctest)`
Expected: PASS, every test including `tests/unit/ui/ui_system_test.cpp`. This is the real gate on Task 1 — the hit path must behave identically.

- [ ] **Step 7: Commit**

```bash
git add engine/ui/ui_button_rects.hpp engine/ui/ui_system.hpp \
        tests/unit/ui/ui_button_rects_test.cpp tests/CMakeLists.txt
git commit -m "refactor(ui): extract button_hit_candidates so a diagnostic can share it"
```

---

### Task 2: `find_order_conflicts`

**Files:**
- Modify: `engine/ui/ui_order_check.hpp` (append; keep `find_order_disagreements` untouched)
- Test: `tests/unit/ui/ui_order_conflict_test.cpp` (create)
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `button_hit_candidates` from Task 1; `UiPanel`, `Rect`.
- Produces: `enum class OrderConflictKind { Inverted, Ambiguous }`; `struct OrderConflict { EntityID a, b; OrderConflictKind kind; int a_button_order, a_panel_order, b_button_order, b_panel_order; }`; `rects_overlap(const Rect&, const Rect&) -> bool`; `find_order_conflicts(World&) -> std::vector<OrderConflict>`. Task 4 calls `find_order_conflicts` and reads every field.

- [ ] **Step 1: Write the first failing test**

Create `tests/unit/ui/ui_order_conflict_test.cpp`:

```cpp
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/ui/ui_button.hpp"
#include "engine/ui/ui_order_check.hpp"
#include "engine/ui/ui_panel.hpp"
#include "engine/ui/ui_parent.hpp"
#include "engine/world/world.hpp"

using mosaic::engine::World;
using mosaic::engine::ui::Anchor;
using mosaic::engine::ui::find_order_conflicts;
using mosaic::engine::ui::OrderConflictKind;
using mosaic::engine::ui::UiButton;
using mosaic::engine::ui::UiPanel;
using mosaic::engine::ui::UiParent;
using mosaic::foundation::EntityID;

namespace
{
// A button with a panel on its own entity -- the ownership rule -- at an explicit rect.
EntityID add_button(World& world, float x, float y, float w, float h,
                    int button_order, int panel_order)
{
    const auto e = world.create_entity();
    UiButton   b;
    b.anchor = static_cast<int>(Anchor::TopLeft);
    b.pos    = {x, y};
    b.size   = {w, h};
    b.order  = button_order;
    world.add_component<UiButton>(e, b);
    UiPanel p;
    p.anchor = static_cast<int>(Anchor::TopLeft);
    p.pos    = {x, y};
    p.size   = {w, h};
    p.order  = panel_order;
    world.add_component<UiPanel>(e, p);
    return e;
}
} // namespace

TEST_CASE("two overlapping buttons ranked oppositely are an inverted conflict", "[ui][conflict]")
{
    // THE BUG. A's button order wins the click (5 > 2) while B's panel is drawn on top
    // (7 > 1), so the click lands on the button the user cannot see.
    World      world;
    const auto a = add_button(world, 0.1f, 0.1f, 0.3f, 0.3f, 5, 1);
    const auto b = add_button(world, 0.2f, 0.2f, 0.3f, 0.3f, 2, 7);

    const auto found = find_order_conflicts(world);

    REQUIRE(found.size() == 1);
    REQUIRE(found[0].kind == OrderConflictKind::Inverted);
    REQUIRE(found[0].a == a);
    REQUIRE(found[0].b == b);
    // All four orders travel with the finding, matched to their own entity -- a message
    // that transposed them would tell a user to change the wrong number.
    REQUIRE(found[0].a_button_order == 5);
    REQUIRE(found[0].a_panel_order == 1);
    REQUIRE(found[0].b_button_order == 2);
    REQUIRE(found[0].b_panel_order == 7);
}
```

Add to `tests/CMakeLists.txt`:

```cmake
  unit/ui/ui_order_conflict_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build/debug --target mosaic_unit_tests -j4`
Expected: FAIL to compile — `find_order_conflicts` and `OrderConflictKind` are not declared.

- [ ] **Step 3: Write the implementation**

Append to `engine/ui/ui_order_check.hpp`, and add `#include <algorithm>`, `#include <cstddef>` and `#include "engine/ui/ui_button_rects.hpp"` to its includes:

```cpp
// Which way a pair of overlapping buttons disagrees.
enum class OrderConflictKind
{
    // Both axes are ordered and they rank the pair OPPOSITELY: the click provably goes to
    // the button drawn underneath. A definite bug.
    Inverted,
    // One axis is tied while the other is not, so the outcome falls to iteration order
    // over two different sparse sets. Not provably wrong in any one frame -- but
    // destroying an unrelated button can flip which of these receives the click, with
    // nothing in the scene having changed visually (see ui_hit.hpp's rule 2).
    Ambiguous
};

// Two overlapping buttons whose hit ranking disagrees with their visual ranking.
//
// `a` is always the entity with the lower EntityID value, and the four orders follow that
// assignment rather than iteration order -- otherwise the same conflict would key
// differently depending on which entity the outer loop reached first, and warn twice.
struct OrderConflict
{
    mosaic::foundation::EntityID a, b;
    OrderConflictKind            kind           = OrderConflictKind::Inverted;
    int                          a_button_order = 0;
    int                          a_panel_order  = 0;
    int                          b_button_order = 0;
    int                          b_panel_order  = 0;
};

// Do two normalized screen rects share any point?
//
// INCLUSIVE on both axes, matching topmost_hit, which tests containment with >= and <= and
// therefore treats a pointer exactly on a shared edge as inside BOTH rects. Two buttons
// that merely touch along an edge can genuinely misroute a click on that line, so touching
// counts as overlapping. Exclusive comparisons would make this check disagree with the hit
// test it exists to police.
inline bool rects_overlap(const Rect& p, const Rect& q)
{
    return p.x <= q.x + q.w && q.x <= p.x + p.w && p.y <= q.y + q.h && q.y <= p.y + p.h;
}

// Every pair of overlapping buttons whose UiButton::order ranking disagrees with their
// UiPanel::order ranking.
//
// THIS IS THE CORRECTNESS CHECK that find_order_disagreements above is a heuristic stand-in
// for. A lone button cannot misroute a click whatever its order says; the failure needs two
// buttons that overlap.
//
// Rects come from button_hit_candidates -- the SAME flatten UiSystem's press and hover
// paths use -- so a conflict reported here is one the hit test will actually honour.
//
// ONLY AUTHORED INTEGERS ARE COMPARED. The check never asks which entity comes first in a
// for_each, because that is precisely the fragile property it warns about: a verdict built
// on iteration order would change when an unrelated entity was destroyed.
//
// A button with no UiPanel on its OWN entity is skipped whole: under the ownership rule its
// visual rank is undefined, so there is nothing to compare. That is where a button
// overlaying a panel on a DIFFERENT entity falls -- still invisible here, still recorded in
// the backlog.
//
// O(n^2) in buttons, once per editor frame. Every scene in this repository has one button
// and 64 would be 2,016 pairs of four integer comparisons; a spatial index would be
// machinery defending a scene size nothing approaches.
inline std::vector<OrderConflict> find_order_conflicts(mosaic::engine::World& world)
{
    struct Ranked
    {
        mosaic::foundation::EntityID e;
        int                          button_order = 0;
        int                          panel_order  = 0;
        Rect                         rect;
    };

    std::vector<Ranked> ranked;
    for (const HitCandidate& c : button_hit_candidates(world))
    {
        const UiPanel* p = world.get_component<UiPanel>(c.e);
        if (p == nullptr)
        {
            continue; // no visual of its own -- nothing to rank it against
        }
        ranked.push_back(Ranked{c.e, c.order, p->order, c.rect});
    }

    std::vector<OrderConflict> out;
    for (std::size_t i = 0; i < ranked.size(); ++i)
    {
        for (std::size_t j = i + 1; j < ranked.size(); ++j)
        {
            const Ranked* lo = &ranked[i];
            const Ranked* hi = &ranked[j];
            if (!rects_overlap(lo->rect, hi->rect))
            {
                continue;
            }
            // Normalize so the finding's identity does not depend on iteration order.
            if (lo->e.value() > hi->e.value())
            {
                std::swap(lo, hi);
            }
            const int da = lo->button_order - hi->button_order;
            const int dp = lo->panel_order - hi->panel_order;
            if (da == 0 && dp == 0)
            {
                // Neither axis expresses a preference: the pair is symmetric, and which one
                // appears on top is as arbitrary as which one is clicked. Reporting it would
                // fire on every pair of overlapping default-constructed buttons.
                continue;
            }
            OrderConflictKind kind;
            if (da == 0 || dp == 0)
            {
                kind = OrderConflictKind::Ambiguous;
            }
            else if ((da > 0) != (dp > 0))
            {
                // Both non-zero here, so comparing SIGNS is exact. Subtracting and comparing
                // magnitudes would miss -2 against 2 -- equal in magnitude, opposite in z,
                // and the worst disagreement to miss.
                kind = OrderConflictKind::Inverted;
            }
            else
            {
                continue; // same sign: the two axes agree
            }
            out.push_back(OrderConflict{lo->e, hi->e, kind, lo->button_order, lo->panel_order,
                                        hi->button_order, hi->panel_order});
        }
    }
    return out;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `./build/debug/tests/mosaic_unit_tests "[conflict]"`
Expected: PASS, 1 test case.

- [ ] **Step 5: Add the consistent and non-overlapping cases**

Append to `tests/unit/ui/ui_order_conflict_test.cpp`:

```cpp
TEST_CASE("two overlapping buttons ranked the same way are silent", "[ui][conflict]")
{
    // The state a correctly authored scene is in: the button that receives the click is
    // also the one drawn on top.
    World world;
    add_button(world, 0.1f, 0.1f, 0.3f, 0.3f, 5, 7);
    add_button(world, 0.2f, 0.2f, 0.3f, 0.3f, 2, 1);

    REQUIRE(find_order_conflicts(world).empty());
}

TEST_CASE("non-overlapping buttons are silent however their orders rank", "[ui][conflict]")
{
    // Overlap is the gate. Two buttons at opposite corners cannot misroute a click between
    // them no matter what their orders say, and reporting them would make the diagnostic
    // fire on most scenes.
    World world;
    add_button(world, 0.0f, 0.0f, 0.2f, 0.2f, 5, 1);
    add_button(world, 0.7f, 0.7f, 0.2f, 0.2f, 2, 7);

    REQUIRE(find_order_conflicts(world).empty());
}

TEST_CASE("buttons touching along an edge are treated as overlapping", "[ui][conflict]")
{
    // Pins the INCLUSIVE comparison. The first ends at x = 0.4; the second starts there. A
    // pointer at exactly x = 0.4 is inside both, because topmost_hit tests containment with
    // >= and <=, so the misroute is reachable on that line.
    World world;
    add_button(world, 0.2f, 0.2f, 0.2f, 0.2f, 5, 1);
    add_button(world, 0.4f, 0.2f, 0.2f, 0.2f, 2, 7);

    const auto found = find_order_conflicts(world);

    REQUIRE(found.size() == 1);
    REQUIRE(found[0].kind == OrderConflictKind::Inverted);
}
```

- [ ] **Step 6: Run to verify they pass**

Run: `./build/debug/tests/mosaic_unit_tests "[conflict]"`
Expected: PASS, 4 test cases.

- [ ] **Step 7: Add the ambiguous and both-tied cases**

Append:

```cpp
TEST_CASE("tied button orders with differing panel orders are ambiguous", "[ui][conflict]")
{
    // Equal button orders means the hit tie-break falls to iteration order over the
    // UiButton sparse set, while the panels are explicitly ranked. Destroying an unrelated
    // button can flip the click without the scene looking any different.
    World world;
    add_button(world, 0.1f, 0.1f, 0.3f, 0.3f, 3, 1);
    add_button(world, 0.2f, 0.2f, 0.3f, 0.3f, 3, 8);

    const auto found = find_order_conflicts(world);

    REQUIRE(found.size() == 1);
    REQUIRE(found[0].kind == OrderConflictKind::Ambiguous);
}

TEST_CASE("tied panel orders with differing button orders are ambiguous", "[ui][conflict]")
{
    // The mirror image: the click is decided but what the user sees on top is not.
    World world;
    add_button(world, 0.1f, 0.1f, 0.3f, 0.3f, 1, 4);
    add_button(world, 0.2f, 0.2f, 0.3f, 0.3f, 9, 4);

    const auto found = find_order_conflicts(world);

    REQUIRE(found.size() == 1);
    REQUIRE(found[0].kind == OrderConflictKind::Ambiguous);
}

TEST_CASE("two fully tied buttons are not reported", "[ui][conflict]")
{
    // Deliberately silent: the author expressed no preference on either axis, so there is
    // no disagreement to fix. Two default-constructed overlapping buttons are this case,
    // which is why reporting it would be noise rather than a finding.
    World world;
    add_button(world, 0.1f, 0.1f, 0.3f, 0.3f, 0, 0);
    add_button(world, 0.2f, 0.2f, 0.3f, 0.3f, 0, 0);

    REQUIRE(find_order_conflicts(world).empty());
}
```

- [ ] **Step 8: Run to verify they pass**

Run: `./build/debug/tests/mosaic_unit_tests "[conflict]"`
Expected: PASS, 7 test cases.

- [ ] **Step 9: Add the no-panel skip (Review Focus 5)**

Append:

```cpp
TEST_CASE("a button with no panel of its own is skipped entirely", "[ui][conflict]")
{
    // Under the ownership rule its visual rank is undefined, so it must be skipped WHOLE
    // rather than compared as if its panel order were zero -- which would invent a conflict
    // against the neighbour that does have one. This is also the shape of the recorded
    // blind spot: a button overlaying a panel on a DIFFERENT entity looks exactly like this.
    World      world;
    const auto bare = world.create_entity();
    UiButton   b;
    b.anchor = static_cast<int>(Anchor::TopLeft);
    b.pos    = {0.1f, 0.1f};
    b.size   = {0.3f, 0.3f};
    b.order  = 9; // would invert against the neighbour below if it were ranked
    world.add_component<UiButton>(bare, b);

    add_button(world, 0.2f, 0.2f, 0.3f, 0.3f, 1, 8);

    REQUIRE(find_order_conflicts(world).empty());
}

TEST_CASE("three mutually overlapping conflicting buttons yield three findings", "[ui][conflict]")
{
    // Every PAIR is reported, not one finding per entity: orders 1/3, 2/2, 3/1 invert
    // against each other in all three combinations. A loop that broke out after the first
    // conflict for an entity would return two.
    World world;
    add_button(world, 0.1f, 0.1f, 0.4f, 0.4f, 1, 3);
    add_button(world, 0.15f, 0.15f, 0.4f, 0.4f, 2, 2);
    add_button(world, 0.2f, 0.2f, 0.4f, 0.4f, 3, 1);

    REQUIRE(find_order_conflicts(world).size() == 3);
}
```

- [ ] **Step 10: Add the pair-normalization case**

Append:

```cpp
TEST_CASE("the finding names the lower entity first whatever the creation order", "[ui][conflict]")
{
    // Identity must not depend on which entity the outer loop reached first, or the same
    // conflict keys two ways and warns twice. Built in BOTH creation orders; the finding
    // must come out the same way round each time.
    World first;
    const auto early = add_button(first, 0.1f, 0.1f, 0.3f, 0.3f, 5, 1);
    const auto late  = add_button(first, 0.2f, 0.2f, 0.3f, 0.3f, 2, 7);
    const auto fa    = find_order_conflicts(first);
    REQUIRE(fa.size() == 1);
    REQUIRE(fa[0].a.value() < fa[0].b.value());
    REQUIRE(fa[0].a == early);
    REQUIRE(fa[0].b == late);
    // The orders follow the entities, not the loop: `a` is the one with button order 5.
    REQUIRE(fa[0].a_button_order == 5);

    // Same geometry, same orders, opposite creation order.
    World second;
    add_button(second, 0.2f, 0.2f, 0.3f, 0.3f, 2, 7);
    add_button(second, 0.1f, 0.1f, 0.3f, 0.3f, 5, 1);
    const auto fb = find_order_conflicts(second);
    REQUIRE(fb.size() == 1);
    REQUIRE(fb[0].a.value() < fb[0].b.value());
    // The earlier-created entity is now the one with button order 2, and `a` follows the
    // ENTITY, so the orders swap with it rather than staying put.
    REQUIRE(fb[0].a_button_order == 2);
    REQUIRE(fb[0].b_button_order == 5);
}
```

- [ ] **Step 11: Add the sign-only case (Review Focus 2)**

Append:

```cpp
TEST_CASE("orders differing only in sign are an inverted conflict", "[ui][conflict]")
{
    // The exact gap a mutation probe found in #240: -2 against 2 is EQUAL in magnitude and
    // opposite in z, so a comparison written on std::abs passes every other case here and
    // misses the worst disagreement there is.
    World world;
    add_button(world, 0.1f, 0.1f, 0.3f, 0.3f, -2, 2);
    add_button(world, 0.2f, 0.2f, 0.3f, 0.3f, 2, -2);

    const auto found = find_order_conflicts(world);

    REQUIRE(found.size() == 1);
    REQUIRE(found[0].kind == OrderConflictKind::Inverted);
}
```

- [ ] **Step 12: Add the zero-size case (Review Focus 3)**

Append:

```cpp
TEST_CASE("two zero-size buttons at the same point conflict", "[ui][conflict]")
{
    // ui_hit.hpp states that a zero-size rect is hit at exactly its corner, so a pointer
    // on that point is inside both and the misroute is genuinely reachable. Degenerate
    // geometry, real consequence -- and an overlap test written with `<` would drop it.
    World world;
    add_button(world, 0.5f, 0.5f, 0.0f, 0.0f, 4, 1);
    add_button(world, 0.5f, 0.5f, 0.0f, 0.0f, 1, 4);

    REQUIRE(find_order_conflicts(world).size() == 1);
}
```

- [ ] **Step 13: Add the UiParent cycle case (Review Focus 1)**

Append:

```cpp
TEST_CASE("a UiParent cycle does not hang the conflict check", "[ui][conflict]")
{
    // A scene file can author a cycle. resolve_parent_frame depth-caps at 16, and the check
    // inherits that only because it resolves rects through the same path -- so this asserts
    // termination, not a particular rect. If this test ever hangs rather than fails, the
    // cap has been lost somewhere up the chain.
    World      world;
    const auto x = add_button(world, 0.1f, 0.1f, 0.3f, 0.3f, 5, 1);
    const auto y = add_button(world, 0.2f, 0.2f, 0.3f, 0.3f, 2, 7);
    world.add_component<UiParent>(x, UiParent{y});
    world.add_component<UiParent>(y, UiParent{x});

    REQUIRE_NOTHROW(find_order_conflicts(world));
}
```

- [ ] **Step 14: Add the stale-parent case (Review Focus 4)**

Append:

```cpp
TEST_CASE("a stale UiParent target falls back to the screen frame", "[ui][conflict]")
{
    // The parent is destroyed, so resolve_parent_frame returns the whole screen and the
    // button resolves at its own numbers. Must not crash, and must still rank: a check that
    // skipped entities with a dangling parent would go quiet on exactly the scenes most
    // likely to be mid-edit.
    World      world;
    const auto ghost = world.create_entity();
    const auto x     = add_button(world, 0.1f, 0.1f, 0.3f, 0.3f, 5, 1);
    world.add_component<UiParent>(x, UiParent{ghost});
    world.destroy_entity(ghost);
    world.tick(0.0f); // commit the deferred destruction

    add_button(world, 0.2f, 0.2f, 0.3f, 0.3f, 2, 7);

    const auto found = find_order_conflicts(world);
    REQUIRE(found.size() == 1);
    REQUIRE(found[0].kind == OrderConflictKind::Inverted);
}
```

The `destroy_entity` then `tick(0.0f)` pair is the idiom
`tests/unit/world/world_integration_test.cpp:25-26` uses: `destroy_entity` queues and
`tick` commits, after which `is_valid` rejects the handle. No systems are registered, so
the tick runs nothing else.

- [ ] **Step 15: Run the whole tag**

Run: `./build/debug/tests/mosaic_unit_tests "[conflict]"`
Expected: PASS, 13 test cases.

- [ ] **Step 16: Run mutation probes**

For each mutation below, apply it to `engine/ui/ui_order_check.hpp`, rebuild, run `./build/debug/tests/mosaic_unit_tests "[conflict]"`, confirm it FAILS, then restore the file **and `touch` it** (a restore that preserves mtime leaves ninja holding the mutant binary — this cost a bogus four-failure suite run earlier in this session):

1. `rects_overlap` made exclusive (`<` for `<=`) — expect the touching-edge and zero-size tests red.
2. The `y` axis dropped from `rects_overlap` — expect a test red (if none fails, add a pair overlapping in x but not y).
3. `(da > 0) != (dp > 0)` becomes `==` — expect the inverted tests red.
4. `Ambiguous` replaced by `Inverted` — expect the two ambiguous tests red.
5. The `da == 0 && dp == 0` early-out removed — expect the fully-tied test red.
6. The `p == nullptr` skip removed (rank it as panel order 0) — expect the no-panel test red.
7. The `lo->e.value() > hi->e.value()` swap removed — expect the normalization test red.
8. `j = i + 1` becomes `j = 0` — expect a duplicate-findings failure.

Any probe that stays GREEN is a real coverage gap: add the distinguishing test before moving on, and say so in the commit message.

- [ ] **Step 17: Commit**

```bash
git add engine/ui/ui_order_check.hpp tests/unit/ui/ui_order_conflict_test.cpp tests/CMakeLists.txt
git commit -m "feat(ui): find_order_conflicts, the pairwise order check"
```

---

### Task 3: One de-duplication ledger for both warning streams

**Files:**
- Modify: `studio/order_warnings.hpp`
- Test: `tests/unit/studio/order_warnings_test.cpp` (rewrite against the new shape, keep every existing case)

**Interfaces:**
- Consumes: `OrderDisagreement` from `engine/ui/ui_order_check.hpp`.
- Produces: `mosaic::studio::WarningLedger` with `struct Entry { std::string key, signature; }` and `std::vector<std::size_t> emit(const std::vector<Entry>&)`; `OrderWarningModel::emit` keeps its existing signature. Task 4 uses both.

- [ ] **Step 1: Write the failing ledger test**

Prepend to `tests/unit/studio/order_warnings_test.cpp` (after the includes, adding `#include <string>`):

```cpp
using mosaic::studio::WarningLedger;

TEST_CASE("the ledger emits a key the first time and not the second", "[studio][ledger]")
{
    WarningLedger ledger;
    const std::vector<WarningLedger::Entry> one{{"k1", "sig-a"}};

    const auto first = ledger.emit(one);
    REQUIRE(first.size() == 1);
    REQUIRE(first[0] == 0); // an INDEX into `current`, so a caller maps it to its finding

    REQUIRE(ledger.emit(one).empty());
}

TEST_CASE("the ledger re-emits a key whose signature changed", "[studio][ledger]")
{
    // The property the order lint's accepted drag-burst depends on: a finding whose numbers
    // moved is a different finding, not a repeat.
    WarningLedger ledger;
    REQUIRE(ledger.emit({{"k1", "sig-a"}}).size() == 1);
    REQUIRE(ledger.emit({{"k1", "sig-b"}}).size() == 1);
}

TEST_CASE("the ledger forgets a key that stops being reported", "[studio][ledger]")
{
    // Rebuild-don't-grow. A set that only ever grew would go permanently silent about a
    // problem the user fixed and then reintroduced.
    WarningLedger ledger;
    REQUIRE(ledger.emit({{"k1", "sig-a"}}).size() == 1);
    REQUIRE(ledger.emit({}).empty());
    REQUIRE(ledger.emit({{"k1", "sig-a"}}).size() == 1);
}

TEST_CASE("the ledger tracks two keys independently", "[studio][ledger]")
{
    WarningLedger ledger;
    REQUIRE(ledger.emit({{"k1", "sig"}}).size() == 1);

    const auto second = ledger.emit({{"k1", "sig"}, {"k2", "sig"}});
    REQUIRE(second.size() == 1);
    REQUIRE(second[0] == 1); // the index of k2, not of k1
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build/debug --target mosaic_unit_tests -j4`
Expected: FAIL to compile — `WarningLedger` is not declared.

- [ ] **Step 3: Add the ledger and reimplement OrderWarningModel on it**

Replace the body of `studio/order_warnings.hpp`'s class section with:

```cpp
// Transition-only reporting, shared by every editor-time diagnostic: which findings are new
// since the last round. Pure data -- no ImGui, no World, no logger.
//
// Keyed on strings so one implementation serves findings of different shapes (one entity
// and two numbers; two entities and four). The subtle part is the rebuild below, and that
// is exactly the part worth not duplicating per diagnostic.
class WarningLedger
{
public:
    // `key` is what the finding is ABOUT (an entity, a pair); `signature` is the numbers
    // that make this instance of it distinct.
    struct Entry
    {
        std::string key;
        std::string signature;
    };

    // The INDICES into `current` worth reporting now: a key absent last round, or present
    // with a different signature.
    //
    // Indices rather than keys, so a caller maps a result straight back to its own finding
    // (`for (auto i : ledger.emit(entries)) warn(findings[i]);`) instead of keeping a
    // second key-to-finding map of its own.
    std::vector<std::size_t> emit(const std::vector<Entry>& current)
    {
        std::vector<std::size_t>           fresh;
        std::map<std::string, std::string> still;
        for (std::size_t i = 0; i < current.size(); ++i)
        {
            const Entry& e    = current[i];
            const auto   seen = seen_.find(e.key);
            if (seen == seen_.end() || seen->second != e.signature)
            {
                fresh.push_back(i);
            }
            still.emplace(e.key, e.signature);
        }
        // REBUILT from `current` rather than inserted into, so a key that stops being
        // reported is forgotten and warns again if it returns. A monotonically growing map
        // would go permanently silent about a problem the user fixed and reintroduced.
        seen_ = std::move(still);
        return fresh;
    }

private:
    std::map<std::string, std::string> seen_;
};

// The per-entity order lint's de-duplication: a thin typed wrapper over the ledger.
//
// WHY EVERY FRAME AT ALL: an `order` can change mid-session from the Inspector (a DragInt
// since #242), and also from a Python script, a Studio plugin, the Script Console, an MCP
// `set_component` call or the Blender live-link -- every one of those goes through
// SetComponentCommand and reflection. A load-time-only check would miss all of them.
//
// THE VISIBLE CONSEQUENCE, REVIEWED AND KEPT: because the two numbers are part of a
// finding's signature, dragging an `order` through intermediate values emits one warning per
// value -- one drag from 0 to 7 is roughly seven lines. That looks like a de-duplication bug
// and is not one; each line reports a genuinely different disagreement. Exercised at the
// window on 2026-10-12 and accepted as it stands; debouncing during an active drag was
// considered and declined. Do not "fix" the burst without revisiting that decision.
class OrderWarningModel
{
public:
    std::vector<mosaic::engine::ui::OrderDisagreement> emit(
        const std::vector<mosaic::engine::ui::OrderDisagreement>& current)
    {
        std::vector<WarningLedger::Entry> entries;
        entries.reserve(current.size());
        for (const auto& f : current)
        {
            // The entity identifies the finding; its generation is in EntityID::value(), so
            // a recycled index cannot inherit a destroyed entity's warning history.
            entries.push_back(WarningLedger::Entry{
                std::to_string(f.e.value()),
                std::to_string(f.button_order) + ":" + std::to_string(f.panel_order)});
        }
        std::vector<mosaic::engine::ui::OrderDisagreement> fresh;
        for (const std::size_t i : ledger_.emit(entries))
        {
            fresh.push_back(current[i]);
        }
        return fresh;
    }

private:
    WarningLedger ledger_;
};
```

Update the file's includes to `<cstddef>`, `<map>`, `<string>`, `<utility>`, `<vector>` plus the existing `engine/ui/ui_order_check.hpp`; drop `<cstdint>` if nothing else needs it.

- [ ] **Step 4: Run both tags to verify**

Run: `./build/debug/tests/mosaic_unit_tests "[ledger]" && ./build/debug/tests/mosaic_unit_tests "[studio][order]"`
Expected: PASS — the four new ledger cases, and every pre-existing `OrderWarningModel` case unchanged. The second command is the real gate: this task must not change the lint's behaviour.

- [ ] **Step 5: Probe the refactor**

Apply each mutation to `studio/order_warnings.hpp`, rebuild, run `./build/debug/tests/mosaic_unit_tests "[studio]"`, confirm FAIL, restore and `touch`:

1. `seen_ = std::move(still)` becomes a loop that merges into `seen_` — expect the forget test red.
2. The signature comparison dropped (`if (seen == seen_.end())` only) — expect the changed-signature test and the `OrderWarningModel` burst test red.
3. `OrderWarningModel`'s key built from `f.e.index()` instead of `f.e.value()` — expect the recycled-generation test red.
4. `OrderWarningModel`'s signature built from `button_order` alone — expect the changed-orders test red.

- [ ] **Step 6: Commit**

```bash
git add studio/order_warnings.hpp tests/unit/studio/order_warnings_test.cpp
git commit -m "refactor(studio): one warning ledger behind both order diagnostics"
```

---

### Task 4: Report conflicts from the editor frame loop

**Files:**
- Modify: `app/run/editor.cpp` (the diagnostics block at the end of `deps.on_frame`)

**Interfaces:**
- Consumes: `find_order_conflicts`, `OrderConflict`, `OrderConflictKind` (Task 2); `WarningLedger` (Task 3).
- Produces: nothing other tasks use.

- [ ] **Step 1: Add the includes and the second ledger**

In `app/run/editor.cpp` the `engine/ui/ui_order_check.hpp` and `studio/order_warnings.hpp`
includes both already exist (added by #240). Add `<cstddef>`, `<string>` and `<vector>` if
the file does not already carry them — `std::to_string` and `std::vector` are used below,
and relying on a transitive include is how a refactor elsewhere breaks this file.

Beside the existing `order_warnings` local, add:

```cpp
    // A second ledger rather than a second model: a pairwise finding has two entities and
    // four numbers, so it builds its own key and signature. Editor-time only, like the
    // per-entity lint above -- nothing installs either in `mosaic run`.
    mosaic::studio::WarningLedger conflict_warnings;
```

and add `&conflict_warnings` to the `deps.on_frame` capture list.

- [ ] **Step 2: Emit the warnings**

Immediately after the existing `order_warnings.emit(...)` loop, add:

```cpp
        // The CORRECTNESS check the per-entity lint above stands in for: two overlapping
        // buttons whose click ranking disagrees with their visual ranking.
        {
            const auto conflicts = mosaic::engine::ui::find_order_conflicts(world);
            std::vector<mosaic::studio::WarningLedger::Entry> entries;
            entries.reserve(conflicts.size());
            for (const auto& c : conflicts)
            {
                // `a` is already the lower entity value, so the key is stable per pair.
                entries.push_back(mosaic::studio::WarningLedger::Entry{
                    std::to_string(c.a.value()) + "/" + std::to_string(c.b.value()),
                    std::to_string(static_cast<int>(c.kind)) + ":"
                        + std::to_string(c.a_button_order) + ":"
                        + std::to_string(c.a_panel_order) + ":"
                        + std::to_string(c.b_button_order) + ":"
                        + std::to_string(c.b_panel_order)});
            }
            for (const std::size_t i : conflict_warnings.emit(entries))
            {
                const auto& c = conflicts[i];
                if (c.kind == mosaic::engine::ui::OrderConflictKind::Inverted)
                {
                    mosaic::foundation::log::warn(
                        "ui",
                        "entities {} and {} overlap, but the click goes to the one drawn "
                        "underneath (UiButton orders {} and {}; UiPanel orders {} and {})",
                        c.a.value(), c.b.value(), c.a_button_order, c.b_button_order,
                        c.a_panel_order, c.b_panel_order);
                }
                else
                {
                    mosaic::foundation::log::warn(
                        "ui",
                        "entities {} and {} overlap and one of their orders is tied "
                        "(UiButton {} and {}; UiPanel {} and {}) -- which receives the click "
                        "falls to iteration order and can change when an unrelated button is "
                        "destroyed",
                        c.a.value(), c.b.value(), c.a_button_order, c.b_button_order,
                        c.a_panel_order, c.b_panel_order);
                }
            }
        }
```

- [ ] **Step 3: Build and run the full gate**

Run:
```bash
cmake --build build/debug -j4
(cd build/debug && ctest)
python3 tools/ci/check_dependency_rules.py && echo deps-ok
python3 tools/ci/check_test_case_names.py && echo names-ok
```
Expected: build clean, every test passing, both lints exit 0.

- [ ] **Step 4: Verify all three entry points**

Run:
```bash
./build/debug/app/mosaic run --ticks 5 --json >/dev/null; echo "run=$?"
timeout 60 ./build/debug/app/mosaic run --window --ticks 60 >/dev/null 2>&1; echo "window=$?"
timeout 60 ./build/debug/app/mosaic editor --ticks 60 >/dev/null 2>/tmp/ed.err; echo "editor=$?"
grep -c 'WARN. ui' /tmp/ed.err
```
Expected: all three exit 0. The demo scene has one button, so a pairwise check has no pair and the grep must print `0`.

- [ ] **Step 5: Confirm the warning fires end to end**

The call site is not covered by any unit test, so exercise it the way #240's was. Temporarily add a second overlapping button to `app/run/demo_scene.hpp` whose orders invert against the HUD button's, rebuild, run `mosaic editor --ticks 40`, and confirm **exactly one** `Inverted` warning naming both entities. Then revert the demo change with `git checkout -- app/run/demo_scene.hpp` and rebuild.

Record the observed warning text in the commit message.

- [ ] **Step 6: Commit**

```bash
git add app/run/editor.cpp
git commit -m "feat(studio): report pairwise order conflicts in the editor Console"
```

---

### Task 5: Documentation

**Files:**
- Modify: `docs/project/deferred-work-backlog.md`
- Modify: `docs/project/next-steps.md`
- Add: the spec and this plan (already written; commit them with Task 1 if not yet tracked)

- [ ] **Step 1: Strike the backlog item**

In the UI pillar section, mark **"The REAL `order` bug is pairwise and relative, not per-entity and absolute"** as SHIPPED, naming `find_order_conflicts`, and keep its reasoning as the explanation of what was built.

- [ ] **Step 2: Record what this slice deferred**

Add entries, in the existing tiered format, for:
- **The three anchor clampers** (`to_anchor`, `hierarchy_anchor`, `button_anchor`) — three copies of one four-line function in three files, pre-existing, a deliberate non-goal of this slice.
- **No end-to-end pointer test for a conflicting pair** — the check reports what `topmost_hit` and the panel sort each already guarantee separately; a test that drives a real pointer into a conflicting pair and asserts which button receives the event would close it, if Task 2 did not already fold it in.
- **Cross-entity overlap** — restate that it remains out of reach for both lints, now that the pairwise check exists and still cannot see it.

- [ ] **Step 3: Write the next-steps entry**

Prepend a dated entry covering: what shipped, that the heuristic lint stays and why, the both-tied omission and its reasoning, the inclusive-overlap decision, what the probes found, and what still wants a human at the window (nothing, unless Step 5 of Task 4 surprised us).

- [ ] **Step 4: Full gate, then commit**

```bash
cmake --build build/debug -j4 && (cd build/debug && ctest)
git add docs/
git commit -m "docs(ui): record the pairwise order conflict check and what it deferred"
```

- [ ] **Step 5: Push and open the PR**

```bash
git push -u origin feat/ui-pairwise-order-conflicts
```

Then `gh pr create --title "feat(ui): the pairwise order conflict check"` with a body
covering, in this order: what the check is and why the heuristic lint was only a stand-in;
the two finding kinds and why `Ambiguous` is reported at all; the both-tied omission as a
decision; inclusive overlap and that it matches `topmost_hit`; the shared flatten and the
drift it prevents; what the mutation probes found; the ledger refactor being
behaviour-preserving for the accepted drag-burst; and the stale comment corrected along the
way. Finish with the gate numbers and the observed warning text from Task 4 Step 5.

Wait for `build-and-test` to pass, then squash-merge and delete the branch.
