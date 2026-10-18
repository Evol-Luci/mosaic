# U8a — inherited alpha and stack layout — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A `UiStack` on a parent lays its children out along one axis, and a
container's alpha fades everything inside it.

**Architecture:** Layout **narrows the frame** rather than computing positions.
`resolve_parent_frame` — the one function every UI consumer funnels through —
returns the child's *slot* instead of the parent's rect when the parent carries a
`UiStack`. Everything downstream (`resolve_in`, anchors, `pos`, `size`) runs
unchanged, which is why a child's authored `pos` becomes an offset from its slot
with no new rule. Inherited alpha is a separate pure walk of the same `UiParent`
chain, applied at render time as an override that never writes back.

**Tech Stack:** C++20, header-only `engine/ui/` helpers, Catch2 v3, CMake + Ninja.

**Spec:** `docs/superpowers/specs/2026-10-17-ui-layout-containers-design.md`

## Global Constraints

- **Tier rule.** `foundation/` → `engine/` → `studio/`/`app/`, plus `modules/`.
  Nothing in `engine/` may include from `modules/`. Enforced by
  `tools/ci/check_dependency_rules.py`.
- **`engine/ui/` is header-only** and every helper there is a free function in
  `namespace mosaic::engine::ui`. Follow `ui_fit.hpp`, `ui_hit.hpp`,
  `ui_button_rects.hpp`, `ui_order_check.hpp`.
- **Defaults must be an exact no-op.** `UiStack{}` on a parent with
  `spacing = 0, padding = 0` still divides the frame into slots — so adding the
  component DOES change layout, deliberately. But **a parent with no `UiStack`
  must resolve byte-identically to today**: that is the regression guard every
  existing scene depends on.
- **`axis` is an `int`, not an enum**, matching `anchor` on all four UI
  components, and a scene file can hold any int — so it is clamped on read, the
  discipline `to_anchor` established.
- **Nothing writes back.** Inherited alpha and slots are computed, never stored.
  `pos`, `size` and `alpha` are reflected; writing to them would serialise over
  authored data.
- **`for_each` order is NOT stable.** `engine/ecs/component_registry.hpp:130`
  visits "in dense order" over a sparse set — insertion order mutated by
  swap-and-pop. Never derive layout order from it.
- **`spacing` and `padding` are fractions of the parent frame**, like `pos` and
  `size`, not absolute screen units.
- **Build:** `cmake --build build/debug -j4`. **Test:**
  `ctest --test-dir build/debug --output-on-failure`.
- **Catch2 `TEST_CASE` names** must not start with `--` nor contain an unbalanced `[`.
- **Commits** end with:
  ```
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_017SduWFXfqsJjE4EUC8vYat
  ```
- **The pre-commit hook aborts to regenerate `docs/`.** Re-run the **full**
  `git add -A docs/ && git commit` — never `--amend`.

## Review Focus

Five conditions the spec implies that no happy-path test exercises.

1. **A stack whose parent entity has no `UiPanel`.** `resolve_parent_frame`
   already returns the whole screen when the parent has no `UiPanel` — so a
   `UiStack` there would divide the *screen*, silently promoting a child to a
   screen-wide layout. Expected: no `UiPanel` means no frame to divide, so no
   slot. **Task 4.**
2. **Nested stacks.** A stack's child may itself be a stack parent.
   `resolve_parent_frame` has a `kMaxDepth = 16` guard; the slot computation must
   sit inside that recursion, not alongside it, or deep nesting re-enters
   unbounded. Expected: a two-deep nest resolves, and depth 17 degrades to the
   screen exactly as today. **Task 4.**
3. **A `UiParent` naming a destroyed entity.** Existing code guards with
   `world.is_valid`. Sibling enumeration compares `up.parent != parent` and must
   not count a child whose parent handle is stale-but-equal-by-index — the
   generation in `EntityID` is what distinguishes them, and `operator==` compares
   the full 64-bit value, so this is free *if* comparison is used rather than
   `index()`. Expected: a recycled entity id does not capture another entity's
   children. **Task 3.**
4. **`padding >= 0.5`**, which inverts the inner rect. Expected: zero extent, not
   a negative one — a negative `w`/`h` would make every downstream rect
   arithmetic produce garbage rather than nothing. **Task 1.**
5. **Slots are by index, not by visibility.** A child with `alpha = 0` is skipped
   by the draw pass but still *occupies* a slot, so hiding one element must not
   reflow the others. Expected: the count is of children, not of visible
   children. This is a deliberate choice and the test states it. **Task 3.**

---

## File Structure

| File | Responsibility |
|---|---|
| `engine/ui/ui_stack.hpp` (new) | The `UiStack` component, and `layout_slot(...)` — **pure arithmetic, no world**. All the edge cases live here and are cheapest to pin here. |
| `engine/ui/ui_layout.hpp` (new) | The world-aware half: `stack_children`, `child_layout_order`, `slot_for_child`, `inherited_alpha`. |
| `engine/ui/ui_hierarchy.hpp` (modify) | `resolve_parent_frame` returns the slot when the parent has a `UiStack`. The one hook. |
| `engine/render/render_system.hpp` (modify) | Three HUD passes multiply by `inherited_alpha`. |
| `engine/reflection/builtin_components.hpp` (modify) | One registration line. |
| `app/run/demo_scene.hpp` (modify) | Authors a stack so the feature is reachable at the window. |
| `tests/unit/ui/ui_stack_test.cpp` (new) | The pure arithmetic. |
| `tests/unit/ui/ui_layout_test.cpp` (new) | The world half, including the ordering-stability test. |

---

### Task 1: `layout_slot` — the pure arithmetic

**Files:**
- Create: `engine/ui/ui_stack.hpp`
- Create: `tests/unit/ui/ui_stack_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `mosaic::engine::ui::Rect` from `engine/ui/ui_rect.hpp`.
- Produces:
  ```cpp
  struct mosaic::engine::ui::UiStack { int axis = 0; float spacing = 0.0f; float padding = 0.0f; };
  Rect layout_slot(const Rect& frame, int axis, float spacing, float padding,
                   std::size_t index, std::size_t count);
  ```
  `axis`: 0 = vertical, 1 = horizontal. Any other value clamps to vertical.
  Returns a zero-extent rect at the frame's origin when `count == 0` or
  `index >= count`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/ui/ui_stack_test.cpp`:

```cpp
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/ui/ui_stack.hpp"

using Catch::Approx;
using mosaic::engine::ui::layout_slot;
using mosaic::engine::ui::Rect;
using mosaic::engine::ui::UiStack;

namespace
{
// The whole screen, so the arithmetic reads in the same units the spec uses.
const Rect kFull{0.0f, 0.0f, 1.0f, 1.0f};
} // namespace

TEST_CASE("UiStack defaults divide the frame evenly with no gap or inset", "[ui][layout]")
{
    // Defaults are NOT a no-op, deliberately: adding the component is what asks for a
    // layout. What must be a no-op is a parent WITHOUT the component, which is
    // ui_layout's business rather than this function's.
    const UiStack s;
    REQUIRE(s.axis == 0);
    REQUIRE(s.spacing == Approx(0.0f));
    REQUIRE(s.padding == Approx(0.0f));
}

TEST_CASE("one child fills the whole frame", "[ui][layout]")
{
    const Rect r = layout_slot(kFull, 0, 0.0f, 0.0f, 0, 1);
    REQUIRE(r.x == Approx(0.0f));
    REQUIRE(r.y == Approx(0.0f));
    REQUIRE(r.w == Approx(1.0f));
    REQUIRE(r.h == Approx(1.0f));
}

TEST_CASE("a vertical stack splits the frame along y", "[ui][layout]")
{
    // Three children, no spacing: each gets a third, and together they EXACTLY tile the
    // frame. The tiling is the property worth asserting -- a seam or an overlap is the
    // visible failure, and both show up as the thirds not summing to 1.
    const Rect a = layout_slot(kFull, 0, 0.0f, 0.0f, 0, 3);
    const Rect b = layout_slot(kFull, 0, 0.0f, 0.0f, 1, 3);
    const Rect c = layout_slot(kFull, 0, 0.0f, 0.0f, 2, 3);
    REQUIRE(a.h == Approx(1.0f / 3.0f));
    REQUIRE(a.y == Approx(0.0f));
    REQUIRE(b.y == Approx(1.0f / 3.0f));
    REQUIRE(c.y == Approx(2.0f / 3.0f));
    // Full width on the cross axis, and no gaps on the main one.
    REQUIRE(a.w == Approx(1.0f));
    REQUIRE(a.y + a.h == Approx(b.y));
    REQUIRE(b.y + b.h == Approx(c.y));
    REQUIRE(c.y + c.h == Approx(1.0f));
}

TEST_CASE("a horizontal stack is the vertical result with the axes swapped", "[ui][layout]")
{
    // Pinned as a PROPERTY rather than as two independent expectations, so the two axes
    // cannot drift apart: a bug fixed on one and not the other is the likeliest way this
    // function goes wrong.
    for (std::size_t i = 0; i < 4; ++i)
    {
        const Rect v = layout_slot(kFull, 0, 0.1f, 0.05f, i, 4);
        const Rect h = layout_slot(kFull, 1, 0.1f, 0.05f, i, 4);
        REQUIRE(h.x == Approx(v.y));
        REQUIRE(h.y == Approx(v.x));
        REQUIRE(h.w == Approx(v.h));
        REQUIRE(h.h == Approx(v.w));
    }
}

TEST_CASE("spacing eats from between the slots, not from the ends", "[ui][layout]")
{
    // Two children with a 0.2 gap: each slot is 0.4, the first starts flush at 0 and the
    // second ends flush at 1. A gap counted at the ends too would leave the stack floating
    // inside its frame, which looks like a padding bug and is not one.
    const Rect a = layout_slot(kFull, 0, 0.2f, 0.0f, 0, 2);
    const Rect b = layout_slot(kFull, 0, 0.2f, 0.0f, 1, 2);
    REQUIRE(a.y == Approx(0.0f));
    REQUIRE(a.h == Approx(0.4f));
    REQUIRE(b.y == Approx(0.6f));
    REQUIRE(b.h == Approx(0.4f));
    REQUIRE(b.y + b.h == Approx(1.0f));
}

TEST_CASE("padding insets all four sides of the frame", "[ui][layout]")
{
    // A fraction of the frame on each axis, like pos and size -- not absolute screen
    // units. One child so the inset is the only thing under test.
    const Rect r = layout_slot(kFull, 0, 0.0f, 0.1f, 0, 1);
    REQUIRE(r.x == Approx(0.1f));
    REQUIRE(r.y == Approx(0.1f));
    REQUIRE(r.w == Approx(0.8f));
    REQUIRE(r.h == Approx(0.8f));
}

TEST_CASE("padding is applied within the frame, not the screen", "[ui][layout]")
{
    // A half-size frame offset into the screen. Everything scales with the frame, which is
    // what makes a stack nest correctly -- the arithmetic never refers to the screen.
    const Rect frame{0.5f, 0.25f, 0.5f, 0.5f};
    const Rect r = layout_slot(frame, 0, 0.0f, 0.1f, 0, 1);
    REQUIRE(r.x == Approx(0.5f + 0.05f));
    REQUIRE(r.y == Approx(0.25f + 0.05f));
    REQUIRE(r.w == Approx(0.4f));
    REQUIRE(r.h == Approx(0.4f));
}

TEST_CASE("an over-large padding gives a zero extent, never a negative one", "[ui][layout]")
{
    // Review Focus #4. padding 0.6 insets 1.2 of a 1.0 frame. A negative w/h would not
    // merely look wrong: resolve_in multiplies by it, so every descendant rect becomes
    // garbage -- inverted, off-screen, and hit-testing against nothing. Clamped to zero
    // the element simply does not appear, which is legible.
    const Rect r = layout_slot(kFull, 0, 0.0f, 0.6f, 0, 1);
    REQUIRE(r.w >= 0.0f);
    REQUIRE(r.h >= 0.0f);
    REQUIRE(r.w == Approx(0.0f));
    REQUIRE(r.h == Approx(0.0f));
}

TEST_CASE("an over-large spacing gives a zero slot, never a negative one", "[ui][layout]")
{
    // The same hazard on the other knob: three children with spacing 0.9 asks for 1.8 of
    // gap in a 1.0 frame.
    const Rect r = layout_slot(kFull, 0, 0.9f, 0.0f, 1, 3);
    REQUIRE(r.h >= 0.0f);
    REQUIRE(r.h == Approx(0.0f));
}

TEST_CASE("a negative spacing or padding is treated as zero", "[ui][layout]")
{
    // Both are reflected floats on unbounded inspector DragFloats, so a negative is one
    // drag away. A negative padding would OUTSET the slot past its frame -- a child
    // escaping its container, which reads as a layout engine bug rather than as a typo.
    const Rect pad = layout_slot(kFull, 0, 0.0f, -0.5f, 0, 1);
    REQUIRE(pad.x == Approx(0.0f));
    REQUIRE(pad.w == Approx(1.0f));
    const Rect sp = layout_slot(kFull, 0, -0.5f, 0.0f, 0, 2);
    REQUIRE(sp.h == Approx(0.5f));
}

TEST_CASE("a count of zero or an index past the end gives an empty rect", "[ui][layout]")
{
    // count == 0 is a division by zero if unguarded, and reachable: a stack parent whose
    // children were all destroyed this frame.
    const Rect none = layout_slot(kFull, 0, 0.0f, 0.0f, 0, 0);
    REQUIRE(none.w == Approx(0.0f));
    REQUIRE(none.h == Approx(0.0f));
    const Rect past = layout_slot(kFull, 0, 0.0f, 0.0f, 5, 3);
    REQUIRE(past.w == Approx(0.0f));
    REQUIRE(past.h == Approx(0.0f));
}

TEST_CASE("an out-of-range axis falls back to vertical", "[ui][layout]")
{
    // `axis` is a reflected int and a scene file can hold anything -- the same reason
    // to_anchor clamps. Falling back to a REAL layout rather than to an empty rect means a
    // bad scene value shows a stacked column, not an invisible UI.
    const Rect expected = layout_slot(kFull, 0, 0.0f, 0.0f, 1, 2);
    for (const int bad : {-1, 2, 7, 1000})
    {
        const Rect r = layout_slot(kFull, bad, 0.0f, 0.0f, 1, 2);
        INFO("axis = " << bad);
        REQUIRE(r.x == Approx(expected.x));
        REQUIRE(r.y == Approx(expected.y));
        REQUIRE(r.w == Approx(expected.w));
        REQUIRE(r.h == Approx(expected.h));
    }
}
```

In `tests/CMakeLists.txt`, beside the other `unit/ui/` entries, add:

```cmake
  unit/ui/ui_stack_test.cpp
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build/debug -j4 2>&1 | grep -E "error|Error" | head -5
```

Expected: FAIL at compile with
`fatal error: engine/ui/ui_stack.hpp: No such file or directory`.

- [ ] **Step 3: Write the implementation**

Create `engine/ui/ui_stack.hpp`:

```cpp
#pragma once

#include <cstddef>

#include "engine/ui/ui_rect.hpp"

namespace mosaic::engine::ui
{

// A one-axis auto-layout container, authored on the PARENT entity.
//
// Its children's frames become evenly divided slots of the parent's rect, which is what
// makes the child's own anchor/pos/size keep working unchanged: layout narrows the frame
// and the existing arithmetic runs inside it. A child's authored `pos` therefore offsets
// it FROM its slot rather than being ignored -- see the design doc, which reserves that
// question and then finds resolve_in already answers it.
//
// `spacing` and `padding` are fractions of the parent's frame, like `pos` and `size`, not
// absolute screen units -- so a stack nests correctly and never refers to the screen.
struct UiStack
{
    // 0 = vertical (children stack downward), 1 = horizontal. An int rather than an enum,
    // matching `anchor` on all four UI components: the reflection layer and the scene
    // format already carry it that way, and a scene file can hold any int.
    int   axis    = 0;
    float spacing = 0.0f; // gap BETWEEN slots, as a fraction of the frame's main axis
    float padding = 0.0f; // inset on all four sides, as a fraction of the frame
};

template <class V>
void reflect(V& visitor, UiStack& s)
{
    visitor.member("axis", s.axis);
    visitor.member("spacing", s.spacing);
    visitor.member("padding", s.padding);
}

// The slot for child `index` of `count`, within `frame`.
//
// PURE ARITHMETIC, no world: this is where every edge case lives, and keeping it free of
// the ECS is what makes those cases cheap to pin. The world-aware half -- who the children
// are, and in what order -- is ui_layout.hpp.
//
// Returns a zero-extent rect at the frame's origin for `count == 0` or an out-of-range
// index. Never returns a negative extent: resolve_in MULTIPLIES by w/h, so a negative one
// would turn every descendant rect into garbage rather than into nothing.
inline Rect layout_slot(const Rect& frame, int axis, float spacing, float padding,
                        std::size_t index, std::size_t count)
{
    if (count == 0 || index >= count)
    {
        return Rect{frame.x, frame.y, 0.0f, 0.0f};
    }

    // Floored rather than trusted. Both are reflected floats on unbounded inspector
    // DragFloats, and a negative padding would OUTSET the slot past its own frame -- a
    // child escaping its container, which reads as an engine bug rather than as a typo.
    const float pad = padding > 0.0f ? padding : 0.0f;
    const float gap_frac = spacing > 0.0f ? spacing : 0.0f;

    const float ix = pad * frame.w;
    const float iy = pad * frame.h;
    float inner_w = frame.w - 2.0f * ix;
    float inner_h = frame.h - 2.0f * iy;
    if (inner_w < 0.0f) { inner_w = 0.0f; }
    if (inner_h < 0.0f) { inner_h = 0.0f; }
    const Rect inner{frame.x + ix, frame.y + iy, inner_w, inner_h};

    // Anything that is not 1 is vertical, so a scene file holding a stray int gets a real
    // column rather than an invisible UI -- the same posture to_anchor takes.
    const bool  horizontal = (axis == 1);
    const float extent     = horizontal ? inner.w : inner.h;

    // count - 1 gaps: spacing sits BETWEEN slots, never at the ends. Counted at the ends
    // too, the stack would float inside its frame and look like a padding bug.
    const float gap   = gap_frac * extent;
    const float gaps  = gap * static_cast<float>(count - 1);
    float       slot  = (extent - gaps) / static_cast<float>(count);
    if (slot < 0.0f) { slot = 0.0f; }

    const float off = (slot + gap) * static_cast<float>(index);
    if (horizontal)
    {
        return Rect{inner.x + off, inner.y, slot, inner.h};
    }
    return Rect{inner.x, inner.y + off, inner.w, slot};
}

} // namespace mosaic::engine::ui
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build build/debug -j4 2>&1 | tail -3 && \
  ctest --test-dir build/debug --output-on-failure -R "\[ui\]\[layout\]|UiStack|layout_slot|stack splits|stack is the vertical|spacing eats|padding insets|padding is applied|over-large|negative spacing|count of zero|out-of-range axis|one child fills" 2>&1 | tail -12
```

Expected: PASS, 12 tests.

- [ ] **Step 5: Run the whole suite**

```bash
ctest --test-dir build/debug --output-on-failure 2>&1 | tail -8
```

Expected: 100% tests passed. Nothing consumes `layout_slot` yet, so no existing
behaviour can have changed.

> Known order-dependent flakes in this suite: the threaded `ScriptSystem` write
> case and a physics raycast thread case. Re-run; if they pass in isolation,
> continue.

- [ ] **Step 6: Commit**

```bash
git add engine/ui/ui_stack.hpp tests/unit/ui/ui_stack_test.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(ui): UiStack and layout_slot, the pure half

Slot arithmetic with no world: the frame is inset by padding, divided
evenly along the axis, and spacing is taken from between the slots rather
than from the ends.

Both knobs are floored and the slot is clamped at zero, because
resolve_in MULTIPLIES by w/h -- a negative extent would turn every
descendant rect into garbage rather than into nothing.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017SduWFXfqsJjE4EUC8vYat
EOF
)"
```

---

### Task 2: Register `UiStack`

**Files:**
- Modify: `engine/reflection/builtin_components.hpp`
- Modify: `tests/unit/ui/ui_stack_test.cpp` (append two cases)

**Interfaces:**
- Consumes: `UiStack` and its `reflect` (Task 1).
- Produces: the reflection name `"UiStack"`.

**Its own task** because registration is what buys serialisation, the Inspector
and scripting, and it is separately rejectable: a reviewer might accept the
arithmetic and question whether the component should be authorable yet.

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/ui/ui_stack_test.cpp`, adding these includes at the top:

```cpp
#include "engine/reflection/builtin_components.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reflect_value.hpp"
```

```cpp
TEST_CASE("UiStack is a registered built-in component", "[ui][layout]")
{
    // Registration is what makes a stack authorable from a scene file and from the
    // Inspector, which is the whole point of putting the knobs on a component rather than
    // in code. Unregistered, it would be reachable only from C++.
    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    REQUIRE(types.has("UiStack"));
}

TEST_CASE("UiStack round-trips through reflection", "[ui][layout]")
{
    // Pins the member NAMES. A scene authored today has to load tomorrow, and a rename is
    // SILENT -- from_value leaves the field at its default, so a renamed `spacing` would
    // quietly become 0 and every stack in every scene would close up. Three distinct
    // non-default values, so a field read from the wrong member cannot pass.
    UiStack s;
    s.axis    = 1;
    s.spacing = 0.125f;
    s.padding = 0.0625f;

    const auto value = mosaic::foundation::serialization::to_value(s);
    UiStack    back;
    mosaic::foundation::serialization::from_value(value, back);

    REQUIRE(back.axis == 1);
    REQUIRE(back.spacing == Approx(0.125f));
    REQUIRE(back.padding == Approx(0.0625f));
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build/debug -j4 2>&1 | tail -3 && \
  ctest --test-dir build/debug --output-on-failure -R "UiStack is a registered" 2>&1 | tail -8
```

Expected: FAIL on `REQUIRE(types.has("UiStack"))`.

- [ ] **Step 3: Write the implementation**

In `engine/reflection/builtin_components.hpp`, add the include beside the other
`engine/ui/` ones:

```cpp
#include "engine/ui/ui_stack.hpp"
```

and the registration immediately after the `UiParent` line, since a stack is the
other half of the same parenting idea:

```cpp
    types.register_component<mosaic::engine::ui::UiStack>(registry, "UiStack");
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build build/debug -j4 2>&1 | tail -3 && \
  ctest --test-dir build/debug --output-on-failure -R "UiStack" 2>&1 | tail -8
```

Expected: PASS.

- [ ] **Step 5: Run the whole suite**

```bash
ctest --test-dir build/debug --output-on-failure 2>&1 | tail -8
```

Expected: 100% tests passed. Registering a name changes no entity count, so the
four `build_default_scene` count tests are unaffected.

- [ ] **Step 6: Commit**

```bash
git add -A engine tests
git commit -m "$(cat <<'EOF'
feat(ui): register UiStack

Buys serialisation, the Inspector and scripting with no further code.
Registered beside UiParent: a stack is the other half of the same
parenting idea.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017SduWFXfqsJjE4EUC8vYat
EOF
)"
```

---

### Task 3: `stack_children` — deterministic sibling order

**Files:**
- Create: `engine/ui/ui_layout.hpp`
- Create: `tests/unit/ui/ui_layout_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `UiStack` (Task 1); `UiParent`; `UiPanel`/`UiImage`/`UiText` for
  their `order` fields.
- Produces:
  ```cpp
  int  child_layout_order(World& world, EntityID e);
  std::vector<EntityID> stack_children(World& world, EntityID parent);
  ```

**The sharpest correctness point in the slice.** `for_each` visits "in dense
order" over a sparse set — insertion order mutated by swap-and-pop on removal —
so destroying **any** entity carrying `UiParent` can move the last one into the
hole and reshuffle an unrelated stack. Draw-order reshuffling is a flicker;
layout reshuffling moves elements to different places on screen.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/ui/ui_layout_test.cpp`:

```cpp
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/ui/ui_image.hpp"
#include "engine/ui/ui_layout.hpp"
#include "engine/ui/ui_panel.hpp"
#include "engine/ui/ui_parent.hpp"
#include "engine/ui/ui_stack.hpp"
#include "engine/ui/ui_text.hpp"
#include "engine/world/world.hpp"

using Catch::Approx;
using mosaic::engine::World;
using mosaic::engine::ui::child_layout_order;
using mosaic::engine::ui::stack_children;
using mosaic::engine::ui::UiPanel;
using mosaic::engine::ui::UiParent;
using mosaic::engine::ui::UiStack;

namespace
{
// A parent carrying a panel (so it has a rect) and a stack.
World::EntityID make_stack_parent(World& world, int axis = 0)
{
    const auto e = world.create_entity();
    world.add_component<UiPanel>(e, UiPanel{});
    world.add_component<UiStack>(e, UiStack{axis, 0.0f, 0.0f});
    return e;
}

// A child panel with an authored draw order.
World::EntityID make_child(World& world, World::EntityID parent, int order)
{
    const auto e = world.create_entity();
    UiPanel p;
    p.order = order;
    world.add_component<UiPanel>(e, p);
    world.add_component<UiParent>(e, UiParent{parent});
    return e;
}
} // namespace

TEST_CASE("stack_children finds only the named parent's children", "[ui][layout]")
{
    World world;
    const auto a = make_stack_parent(world);
    const auto b = make_stack_parent(world);
    const auto a1 = make_child(world, a, 0);
    make_child(world, b, 0);

    const auto kids = stack_children(world, a);
    REQUIRE(kids.size() == 1);
    REQUIRE(kids[0] == a1);
}

TEST_CASE("stack_children orders by authored order, not insertion", "[ui][layout]")
{
    // Inserted 30, 10, 20 and expected back 10, 20, 30. Insertion order is what `for_each`
    // would give, so this fails if the sort is dropped.
    World world;
    const auto p = make_stack_parent(world);
    const auto c30 = make_child(world, p, 30);
    const auto c10 = make_child(world, p, 10);
    const auto c20 = make_child(world, p, 20);

    const auto kids = stack_children(world, p);
    REQUIRE(kids.size() == 3);
    REQUIRE(kids[0] == c10);
    REQUIRE(kids[1] == c20);
    REQUIRE(kids[2] == c30);
}

TEST_CASE("destroying an unrelated entity does not reorder a stack", "[ui][layout]")
{
    // THE TEST THIS TASK EXISTS FOR. ComponentRegistry::for_each visits "in dense order"
    // over a sparse set: removal swaps the last element into the hole. So destroying an
    // entity that merely HAPPENS to carry UiParent -- in a different panel, or in no panel
    // at all -- would reshuffle this stack if the order came from iteration.
    //
    // The failure is invisible in any test that builds a world and reads it once. It needs
    // a destroy BETWEEN two enumerations, which is why it is written this way.
    World world;
    const auto p = make_stack_parent(world);
    const auto c1 = make_child(world, p, 10);
    const auto c2 = make_child(world, p, 20);
    const auto c3 = make_child(world, p, 30);

    // An unrelated child of an unrelated parent, created LAST so it sits at the end of the
    // dense array and its removal moves something.
    const auto other_parent = make_stack_parent(world);
    const auto stranger     = make_child(world, other_parent, 0);

    const auto before = stack_children(world, p);
    world.destroy_entity(stranger);
    world.tick(0.0f); // destroy_entity is deferred; the tick is what applies it
    const auto after = stack_children(world, p);

    REQUIRE(before.size() == 3);
    REQUIRE(after.size() == 3);
    REQUIRE(after[0] == c1);
    REQUIRE(after[1] == c2);
    REQUIRE(after[2] == c3);
}

TEST_CASE("equal orders give a stable, reproducible sequence", "[ui][layout]")
{
    // Ties are LEGAL and often harmless, so they are not an error. What they must not be
    // is storage-dependent: two children with the same order must come back in the same
    // sequence every time, which the entity index provides as a deterministic tiebreak.
    // It is a tiebreak, not a feature -- nothing should author equal orders and rely on it.
    World world;
    const auto p = make_stack_parent(world);
    make_child(world, p, 5);
    make_child(world, p, 5);
    make_child(world, p, 5);

    const auto first  = stack_children(world, p);
    const auto second = stack_children(world, p);
    REQUIRE(first.size() == 3);
    REQUIRE(first == second);
}

TEST_CASE("a hidden child still occupies a slot", "[ui][layout]")
{
    // Review Focus #5, and a deliberate choice rather than an oversight. A child with
    // alpha 0 is skipped by the draw pass but still COUNTS here, so hiding one element
    // does not reflow the others. The alternative -- counting only visible children --
    // would make a fade cause the rest of the stack to jump, and would make layout depend
    // on a render-time property.
    World world;
    const auto p = make_stack_parent(world);
    make_child(world, p, 10);
    const auto hidden = world.create_entity();
    UiPanel invisible;
    invisible.order = 20;
    invisible.alpha = 0.0f;
    world.add_component<UiPanel>(hidden, invisible);
    world.add_component<UiParent>(hidden, UiParent{p});
    make_child(world, p, 30);

    REQUIRE(stack_children(world, p).size() == 3);
}

TEST_CASE("child_layout_order prefers UiPanel, then UiImage, then UiText", "[ui][layout]")
{
    // A child may carry any of the three components that have an `order`, and the
    // precedence has to be FIXED rather than whichever is found first, or the same scene
    // would lay out differently depending on component insertion order.
    //
    // UiPanel first because it is the only `order` the renderer actually sorts by -- see
    // ui_order_check.hpp, which records that UiImage::order and UiText::order are
    // reflected, documented and ignored by the draw path. Using the number that already
    // means something keeps one concept rather than inventing a second.
    World world;
    const auto p = make_stack_parent(world);

    const auto panel_and_text = world.create_entity();
    UiPanel pp;
    pp.order = 7;
    world.add_component<UiPanel>(panel_and_text, pp);
    mosaic::engine::ui::UiText t;
    t.order = 99;
    world.add_component<mosaic::engine::ui::UiText>(panel_and_text, t);
    world.add_component<UiParent>(panel_and_text, UiParent{p});
    REQUIRE(child_layout_order(world, panel_and_text) == 7);

    const auto text_only = world.create_entity();
    mosaic::engine::ui::UiText t2;
    t2.order = 42;
    world.add_component<mosaic::engine::ui::UiText>(text_only, t2);
    world.add_component<UiParent>(text_only, UiParent{p});
    REQUIRE(child_layout_order(world, text_only) == 42);

    const auto bare = world.create_entity();
    world.add_component<UiParent>(bare, UiParent{p});
    REQUIRE(child_layout_order(world, bare) == 0);
}

TEST_CASE("a stale parent handle captures nobody", "[ui][layout]")
{
    // Review Focus #3. EntityID is (generation << 32) | index, and the registry recycles
    // indices. If sibling matching compared index() rather than the whole handle, a
    // recycled entity would inherit the destroyed one's children -- a stack that silently
    // adopts strangers.
    //
    // operator== compares all 64 bits, so this is free as long as comparison is used. The
    // test exists because using index() is the plausible mistake, not because the current
    // code makes it.
    World world;
    const auto doomed = make_stack_parent(world);
    make_child(world, doomed, 10);
    make_child(world, doomed, 20);
    world.destroy_entity(doomed);
    world.tick(0.0f);

    // A fresh entity very likely reuses the destroyed index with a bumped generation.
    const auto recycled = make_stack_parent(world);
    INFO("recycled index " << recycled.index() << " vs doomed index " << doomed.index());
    REQUIRE(stack_children(world, recycled).empty());
}
```

In `tests/CMakeLists.txt`, beside `unit/ui/ui_stack_test.cpp`, add:

```cmake
  unit/ui/ui_layout_test.cpp
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build/debug -j4 2>&1 | grep -E "error" | head -3
```

Expected: FAIL at compile with
`fatal error: engine/ui/ui_layout.hpp: No such file or directory`.

> If `world.tick(0.0f)` does not compile, check `World`'s actual tick signature
> and use it — the deferred-destroy flush is what the test needs, not the exact
> argument.

- [ ] **Step 3: Write the implementation**

Create `engine/ui/ui_layout.hpp`:

```cpp
#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

#include "engine/ui/ui_image.hpp"
#include "engine/ui/ui_panel.hpp"
#include "engine/ui/ui_parent.hpp"
#include "engine/ui/ui_stack.hpp"
#include "engine/ui/ui_text.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"

namespace mosaic::engine::ui
{

// The `order` a layout sorts this child by.
//
// FIXED PRECEDENCE -- UiPanel, then UiImage, then UiText -- rather than whichever
// component is found first, or the same scene would lay out differently depending on the
// order its components were added.
//
// UiPanel first because its `order` is the only one the renderer actually sorts by.
// ui_order_check.hpp records the rest: UiImage::order and UiText::order are reflected,
// documented, and ignored by the draw path. Sorting by the number that already means
// something keeps ONE concept rather than inventing a second one for layout.
inline int child_layout_order(mosaic::engine::World& world, mosaic::foundation::EntityID e)
{
    if (const UiPanel* p = world.get_component<UiPanel>(e)) { return p->order; }
    if (const UiImage* i = world.get_component<UiImage>(e)) { return i->order; }
    if (const UiText*  t = world.get_component<UiText>(e))  { return t->order; }
    return 0;
}

// Every child of `parent`, in layout order.
//
// ORDER COMES FROM AN AUTHORED KEY, NEVER FROM ITERATION, and this is the slice's sharpest
// correctness point. ComponentRegistry::for_each visits "in dense order" over a sparse
// set: removal swaps the last element into the hole. So deriving layout order from
// iteration would make destroying ANY entity that merely happens to carry UiParent -- in a
// different panel, or in no panel at all -- silently reshuffle this stack. Draw-order
// reshuffling is a flicker; layout reshuffling moves elements to different places.
//
// The entity index is a DETERMINISTIC TIEBREAK, not a feature: equal `order` values come
// back in a reproducible sequence rather than a storage-dependent one. Nothing should
// author equal orders and rely on the result.
//
// Hidden children still count. A child with alpha 0 is skipped by the draw pass but still
// occupies a slot, so fading one element does not reflow the rest -- and layout does not
// come to depend on a render-time property.
inline std::vector<mosaic::foundation::EntityID> stack_children(
    mosaic::engine::World& world, mosaic::foundation::EntityID parent)
{
    std::vector<std::pair<std::pair<int, std::uint32_t>, mosaic::foundation::EntityID>> keyed;
    world.for_each<UiParent>(
        [&](mosaic::foundation::EntityID e, UiParent& up)
        {
            // The WHOLE handle, not index(): EntityID is (generation << 32) | index and
            // the registry recycles indices, so comparing indices would let a recycled
            // entity inherit a destroyed one's children.
            if (up.parent != parent) { return; }
            keyed.push_back({{child_layout_order(world, e), e.index()}, e});
        });
    std::sort(keyed.begin(), keyed.end(),
              [](const auto& l, const auto& r) { return l.first < r.first; });

    std::vector<mosaic::foundation::EntityID> out;
    out.reserve(keyed.size());
    for (const auto& k : keyed) { out.push_back(k.second); }
    return out;
}

} // namespace mosaic::engine::ui
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build build/debug -j4 2>&1 | tail -3 && \
  ctest --test-dir build/debug --output-on-failure -R "stack_children|unrelated entity does not reorder|equal orders|hidden child|child_layout_order|stale parent handle" 2>&1 | tail -10
```

Expected: PASS, 7 tests.

- [ ] **Step 5: Run the whole suite**

```bash
ctest --test-dir build/debug --output-on-failure 2>&1 | tail -8
```

Expected: 100% tests passed.

- [ ] **Step 6: Run the mutation probe**

One probe, and it is the one that matters: delete the `std::sort` from
`stack_children`, rebuild, and confirm `stack_children orders by authored order,
not insertion` turns RED.

```bash
L=engine/ui/ui_layout.hpp
cp $L /tmp/ui_layout.bak
python3 -I -c "
import io
p='$L'; s=io.open(p).read()
old='''    std::sort(keyed.begin(), keyed.end(),
              [](const auto& l, const auto& r) { return l.first < r.first; });
'''
assert s.count(old)==1
io.open(p,'w').write(s.replace(old,''))
print('probe applied: sort removed')
"
touch $L && cmake --build build/debug -j4 >/dev/null 2>&1
ctest --test-dir build/debug -R "orders by authored order" 2>&1 | tail -3
cp /tmp/ui_layout.bak $L && touch $L && rm -f /tmp/ui_layout.bak
```

Expected: the probe run FAILS that case. **`touch` after restoring is required**
— `cp` preserves nothing useful but ninja keys off mtime, and a stale mutant
binary makes a later `ctest` show bogus failures elsewhere.

```bash
git diff --stat engine/ui/ui_layout.hpp   # must show the real change only
```

- [ ] **Step 7: Commit**

```bash
git add engine/ui/ui_layout.hpp tests/unit/ui/ui_layout_test.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(ui): deterministic sibling order for layout

for_each visits "in dense order" over a sparse set, and removal swaps the
last element into the hole -- so deriving layout order from iteration
would make destroying any unrelated entity carrying UiParent reshuffle a
stack. Order comes from the authored `order` instead, with the entity
index as a deterministic tiebreak only.

The test that proves it destroys a stranger BETWEEN two enumerations;
nothing that builds a world and reads it once could have caught this.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017SduWFXfqsJjE4EUC8vYat
EOF
)"
```

---

### Task 4: The hook — `resolve_parent_frame` returns the slot

**Files:**
- Modify: `engine/ui/ui_layout.hpp` (add `slot_for_child`)
- Modify: `engine/ui/ui_hierarchy.hpp` (the hook)
- Modify: `tests/unit/ui/ui_layout_test.cpp` (append cases)

**Interfaces:**
- Consumes: `layout_slot` (Task 1), `stack_children` (Task 3).
- Produces: `std::optional<Rect> slot_for_child(World&, EntityID child, const Rect& parent_rect)`
  and the modified `resolve_parent_frame`.

**The hook goes in `resolve_parent_frame`, not `resolve_ui_rect`.** There are two
entry points into the hierarchy: panels, images and button hit-tests call
`resolve_ui_rect`, and **text calls `resolve_parent_frame` directly**
(`render_system.hpp:558`, passing the frame to `layout_text`). Hooking
`resolve_ui_rect` lays out panels and images and **silently leaves labels
behind**. `resolve_ui_rect` is a one-line wrapper over `resolve_parent_frame`, so
hooking the frame covers all four consumers at once.

**Circular-include note.** `ui_hierarchy.hpp` must include `ui_layout.hpp`, and
`ui_layout.hpp` must NOT include `ui_hierarchy.hpp`. `slot_for_child` therefore
takes the already-resolved parent rect as a parameter rather than resolving it
itself — which also keeps the recursion in one place, inside
`resolve_parent_frame`'s existing `kMaxDepth` guard.

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/ui/ui_layout_test.cpp`, adding at the top:

```cpp
#include "engine/ui/ui_hierarchy.hpp"
```

```cpp
TEST_CASE("a child of a stack resolves within its slot", "[ui][layout]")
{
    // The hook, end to end. A full-screen parent with a vertical stack of two children:
    // the second child's frame is the bottom half, so a child with size {1,1} and pos
    // {0,0} fills exactly that.
    World world;
    const auto p = world.create_entity();
    UiPanel parent_panel;
    parent_panel.anchor = 0;               // TopLeft
    parent_panel.pos    = {0.0f, 0.0f};
    parent_panel.size   = {1.0f, 1.0f};    // the whole screen
    world.add_component<UiPanel>(p, parent_panel);
    world.add_component<UiStack>(p, UiStack{0, 0.0f, 0.0f});

    make_child(world, p, 10);
    const auto second = make_child(world, p, 20);

    const auto frame = mosaic::engine::ui::resolve_parent_frame(world, second);
    REQUIRE(frame.x == Approx(0.0f));
    REQUIRE(frame.y == Approx(0.5f));
    REQUIRE(frame.w == Approx(1.0f));
    REQUIRE(frame.h == Approx(0.5f));
}

TEST_CASE("a parent without a UiStack resolves exactly as before", "[ui][layout]")
{
    // THE REGRESSION GUARD every existing scene depends on. Remove the stack and the frame
    // must be the parent's whole rect, byte for byte with the pre-layout behaviour.
    World world;
    const auto p = world.create_entity();
    UiPanel parent_panel;
    parent_panel.pos  = {0.1f, 0.2f};
    parent_panel.size = {0.5f, 0.4f};
    world.add_component<UiPanel>(p, parent_panel);
    // deliberately NO UiStack
    const auto child = make_child(world, p, 0);

    const auto frame = mosaic::engine::ui::resolve_parent_frame(world, child);
    const auto expect = mosaic::engine::ui::resolve_in(
        mosaic::engine::ui::Rect{0.0f, 0.0f, 1.0f, 1.0f},
        mosaic::engine::ui::to_anchor(parent_panel.anchor), parent_panel.pos,
        parent_panel.size);
    REQUIRE(frame.x == Approx(expect.x));
    REQUIRE(frame.y == Approx(expect.y));
    REQUIRE(frame.w == Approx(expect.w));
    REQUIRE(frame.h == Approx(expect.h));
}

TEST_CASE("a UiStack on an entity with no UiPanel lays nothing out", "[ui][layout]")
{
    // Review Focus #1. resolve_parent_frame already returns the whole SCREEN when the
    // parent has no UiPanel -- there is no rect to divide. Dividing the screen instead
    // would silently promote a child into a screen-wide layout, which looks like the
    // parent was ignored and is worse than doing nothing.
    World world;
    const auto p = world.create_entity();
    world.add_component<UiStack>(p, UiStack{0, 0.0f, 0.0f}); // no UiPanel
    const auto child = make_child(world, p, 0);

    const auto frame = mosaic::engine::ui::resolve_parent_frame(world, child);
    REQUIRE(frame.x == Approx(0.0f));
    REQUIRE(frame.y == Approx(0.0f));
    REQUIRE(frame.w == Approx(1.0f));
    REQUIRE(frame.h == Approx(1.0f));
}

TEST_CASE("the authored pos offsets a child from its slot", "[ui][layout]")
{
    // The decision the decomposition reserved for the human, and it needs no new rule:
    // narrowing the FRAME means resolve_in already treats pos as an offset within it. A
    // pos of {0,0} lands exactly on the slot; a non-zero pos nudges from there.
    World world;
    const auto p = world.create_entity();
    UiPanel parent_panel;
    parent_panel.size = {1.0f, 1.0f};
    world.add_component<UiPanel>(p, parent_panel);
    world.add_component<UiStack>(p, UiStack{0, 0.0f, 0.0f});

    const auto on_slot = make_child(world, p, 10);
    const auto nudged  = make_child(world, p, 20);
    UiPanel* np = world.get_component<UiPanel>(nudged);
    np->pos  = {0.25f, 0.0f};
    np->size = {0.5f, 1.0f};

    const auto a = mosaic::engine::ui::resolve_ui_rect(
        world, on_slot, mosaic::engine::ui::to_anchor(0), mosaic::foundation::math::Vec2{0.0f, 0.0f},
        mosaic::foundation::math::Vec2{1.0f, 1.0f});
    REQUIRE(a.y == Approx(0.0f));
    REQUIRE(a.h == Approx(0.5f));

    const auto b = mosaic::engine::ui::resolve_ui_rect(
        world, nudged, mosaic::engine::ui::to_anchor(np->anchor), np->pos, np->size);
    // Slot is the bottom half; pos.x 0.25 of the slot's width shifts it right.
    REQUIRE(b.x == Approx(0.25f));
    REQUIRE(b.y == Approx(0.5f));
    REQUIRE(b.w == Approx(0.5f));
}

TEST_CASE("text is laid out by the same hook", "[ui][layout]")
{
    // The case an implementer who hooked resolve_ui_rect would have shipped broken, with
    // no existing test failing: render_system.hpp:558 calls resolve_parent_frame DIRECTLY
    // for UiText and hands the frame to layout_text. Panels would move and labels would
    // not.
    World world;
    const auto p = world.create_entity();
    UiPanel parent_panel;
    parent_panel.size = {1.0f, 1.0f};
    world.add_component<UiPanel>(p, parent_panel);
    world.add_component<UiStack>(p, UiStack{0, 0.0f, 0.0f});

    make_child(world, p, 10);
    const auto label = world.create_entity();
    mosaic::engine::ui::UiText t;
    t.order = 20;
    world.add_component<mosaic::engine::ui::UiText>(label, t);
    world.add_component<UiParent>(label, UiParent{p});

    // Exactly the call the text pass makes.
    const auto frame = mosaic::engine::ui::resolve_parent_frame(world, label);
    REQUIRE(frame.y == Approx(0.5f));
    REQUIRE(frame.h == Approx(0.5f));
}

TEST_CASE("stacks nest", "[ui][layout]")
{
    // Review Focus #2. The slot computation sits INSIDE resolve_parent_frame's existing
    // recursion, so the kMaxDepth guard covers it; a slot computed alongside the recursion
    // would re-enter unbounded.
    //
    // Outer vertical stack of two, inner stack in the second slot (bottom half) with two
    // children: the inner second child is the bottom quarter of the screen.
    World world;
    const auto outer = world.create_entity();
    UiPanel op;
    op.size = {1.0f, 1.0f};
    world.add_component<UiPanel>(outer, op);
    world.add_component<UiStack>(outer, UiStack{0, 0.0f, 0.0f});

    make_child(world, outer, 10);
    const auto inner = make_child(world, outer, 20);
    UiPanel* ip = world.get_component<UiPanel>(inner);
    ip->pos  = {0.0f, 0.0f};
    ip->size = {1.0f, 1.0f};   // fills its slot
    world.add_component<UiStack>(inner, UiStack{0, 0.0f, 0.0f});

    make_child(world, inner, 10);
    const auto deep = make_child(world, inner, 20);

    const auto frame = mosaic::engine::ui::resolve_parent_frame(world, deep);
    REQUIRE(frame.y == Approx(0.75f));
    REQUIRE(frame.h == Approx(0.25f));
}
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build build/debug -j4 2>&1 | tail -3 && \
  ctest --test-dir build/debug --output-on-failure -R "resolves within its slot|without a UiStack resolves|no UiPanel lays nothing|offsets a child from its slot|text is laid out|stacks nest" 2>&1 | tail -10
```

Expected: `a child of a stack resolves within its slot`, `the authored pos
offsets a child from its slot`, `text is laid out by the same hook` and `stacks
nest` FAIL — the frame comes back as the parent's whole rect. The two guard cases
(`a parent without a UiStack...`, `...no UiPanel lays nothing out`) PASS already,
which is correct: they assert today's behaviour and must keep passing.

- [ ] **Step 3: Write the implementation**

Add to `engine/ui/ui_layout.hpp`, after `stack_children`, with `#include <optional>`
at the top:

```cpp
// This child's slot within `parent_rect`, or nothing if its parent has no UiStack.
//
// TAKES THE PARENT'S RECT rather than resolving it, for two reasons. It breaks what would
// otherwise be a circular include -- ui_hierarchy.hpp includes this header, so this header
// cannot include that one. And it keeps the recursion in ONE place, inside
// resolve_parent_frame's existing kMaxDepth guard, rather than opening a second unbounded
// path up the parent chain.
inline std::optional<Rect> slot_for_child(mosaic::engine::World& world,
                                          mosaic::foundation::EntityID child,
                                          const Rect& parent_rect)
{
    const UiParent* up = world.get_component<UiParent>(child);
    if (up == nullptr || !world.is_valid(up->parent)) { return std::nullopt; }
    const UiStack* st = world.get_component<UiStack>(up->parent);
    if (st == nullptr) { return std::nullopt; }

    const std::vector<mosaic::foundation::EntityID> kids = stack_children(world, up->parent);
    for (std::size_t i = 0; i < kids.size(); ++i)
    {
        if (kids[i] == child)
        {
            return layout_slot(parent_rect, st->axis, st->spacing, st->padding, i,
                               kids.size());
        }
    }
    // Unreachable in practice -- `child` carries the UiParent that stack_children matched
    // on -- but returning nothing rather than asserting means a future caller that passes
    // an unrelated entity gets today's behaviour instead of a crash.
    return std::nullopt;
}
```

In `engine/ui/ui_hierarchy.hpp`, add `#include "engine/ui/ui_layout.hpp"` and
replace the last two lines of `resolve_parent_frame`:

```cpp
    const Rect parent_frame = resolve_parent_frame(world, up->parent, depth + 1);
    return resolve_in(parent_frame, to_anchor(pp->anchor), pp->pos, pp->size);
```

with:

```cpp
    const Rect parent_frame = resolve_parent_frame(world, up->parent, depth + 1);
    const Rect parent_rect = resolve_in(parent_frame, to_anchor(pp->anchor), pp->pos,
                                        pp->size);
    // LAYOUT NARROWS THE FRAME. If the parent carries a UiStack, this child's frame is its
    // slot rather than the parent's whole rect -- and everything downstream runs unchanged,
    // which is the point. resolve_in already treats `pos` as an offset within the frame and
    // `size` as a fraction of it, so a child under a stack keeps its anchor, offsets from
    // its slot, and sizes relative to it with no new rule anywhere.
    //
    // THE HOOK IS HERE AND NOT IN resolve_ui_rect, deliberately. Two call paths reach the
    // hierarchy: panels, images and button hit-tests go through resolve_ui_rect, but TEXT
    // calls this function directly (render_system.hpp:558) and hands the frame to
    // layout_text. Hooking the wrapper would have laid out panels and images and silently
    // left every label where it was.
    if (const std::optional<Rect> slot = slot_for_child(world, e, parent_rect))
    {
        return *slot;
    }
    return parent_rect;
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build build/debug -j4 2>&1 | tail -3 && \
  ctest --test-dir build/debug --output-on-failure -R "\[ui\]" 2>&1 | tail -8
```

Expected: PASS, all `[ui]` cases.

- [ ] **Step 5: Run the whole suite**

```bash
ctest --test-dir build/debug --output-on-failure 2>&1 | tail -10
```

Expected: 100% tests passed. **This is the step most likely to break something
unrelated** — every existing UI test resolves through this function. A failure
here is a real regression, not a stale test: read it before changing anything.

- [ ] **Step 6: Run the mutation probe**

Delete the `slot_for_child` branch from `resolve_parent_frame` (returning
`parent_rect` unconditionally), rebuild, and confirm the four hook cases turn
RED while the two guard cases stay GREEN. Restore and `touch`.

- [ ] **Step 7: Commit**

```bash
git add engine/ui tests/unit/ui
git commit -m "$(cat <<'EOF'
feat(ui): layout narrows the frame

resolve_parent_frame returns the child's slot when the parent carries a
UiStack, and everything downstream runs unchanged -- resolve_in already
treats pos as an offset within the frame and size as a fraction of it, so
a child under a stack keeps its anchor and needs no new rule.

The hook is here rather than in resolve_ui_rect because the text pass
calls this function directly: hooking the wrapper would have laid out
panels and images and silently left every label behind.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017SduWFXfqsJjE4EUC8vYat
EOF
)"
```

---

### Task 5: Inherited alpha

**Files:**
- Modify: `engine/ui/ui_layout.hpp` (add `inherited_alpha`)
- Modify: `engine/render/render_system.hpp` (three HUD passes)
- Modify: `tests/unit/ui/ui_layout_test.cpp` (append cases)
- Modify: `tests/unit/render/render_system_test.cpp` (append one case)

**Interfaces:**
- Consumes: `UiParent`, `UiPanel`.
- Produces: `float inherited_alpha(World& world, EntityID e)` — the product of
  every **ancestor** panel's alpha, **not** including the element's own.

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/ui/ui_layout_test.cpp`:

```cpp
TEST_CASE("inherited_alpha multiplies down the ancestor chain", "[ui][layout]")
{
    // Fading a container fades everything in it. Three deep, 0.5 * 0.5 = 0.25 for the
    // grandchild -- the ancestors' product, NOT including the element's own alpha, which
    // the render pass multiplies in separately.
    World world;
    const auto a = world.create_entity();
    UiPanel pa; pa.alpha = 0.5f;
    world.add_component<UiPanel>(a, pa);

    const auto b = world.create_entity();
    UiPanel pb; pb.alpha = 0.5f;
    world.add_component<UiPanel>(b, pb);
    world.add_component<UiParent>(b, UiParent{a});

    const auto c = world.create_entity();
    UiPanel pc; pc.alpha = 1.0f;
    world.add_component<UiPanel>(c, pc);
    world.add_component<UiParent>(c, UiParent{b});

    REQUIRE(mosaic::engine::ui::inherited_alpha(world, a) == Approx(1.0f));
    REQUIRE(mosaic::engine::ui::inherited_alpha(world, b) == Approx(0.5f));
    REQUIRE(mosaic::engine::ui::inherited_alpha(world, c) == Approx(0.25f));
}

TEST_CASE("inherited_alpha is 1 with no parent", "[ui][layout]")
{
    // The state of every element in every scene authored before this feature -- so this is
    // the regression guard for inherited alpha, and 1.0 is the only value that leaves them
    // unchanged.
    World world;
    const auto e = world.create_entity();
    UiPanel p; p.alpha = 0.3f;
    world.add_component<UiPanel>(e, p);
    REQUIRE(mosaic::engine::ui::inherited_alpha(world, e) == Approx(1.0f));
}

TEST_CASE("inherited_alpha skips a parent with no UiPanel", "[ui][layout]")
{
    // UiParent may name any entity. An ancestor without a UiPanel has no alpha to
    // contribute, and resolve_parent_frame already treats that case as "no frame" -- so
    // treating it as alpha 0 would hide children for a reason nobody authored.
    World world;
    const auto plain = world.create_entity();            // no UiPanel
    const auto child = world.create_entity();
    UiPanel p; p.alpha = 1.0f;
    world.add_component<UiPanel>(child, p);
    world.add_component<UiParent>(child, UiParent{plain});
    REQUIRE(mosaic::engine::ui::inherited_alpha(world, child) == Approx(1.0f));
}

TEST_CASE("inherited_alpha does not write to the authored alphas", "[ui][layout]")
{
    // The discipline resolve_appearance states: "nothing writes to p.color, so the
    // authored value survives and a scene saved while hovering carries no hover tint." A
    // scene saved while a container is mid-fade must carry the authored alphas, not the
    // product.
    World world;
    const auto a = world.create_entity();
    UiPanel pa; pa.alpha = 0.5f;
    world.add_component<UiPanel>(a, pa);
    const auto b = world.create_entity();
    UiPanel pb; pb.alpha = 0.5f;
    world.add_component<UiPanel>(b, pb);
    world.add_component<UiParent>(b, UiParent{a});

    (void)mosaic::engine::ui::inherited_alpha(world, b);
    REQUIRE(world.get_component<UiPanel>(a)->alpha == Approx(0.5f));
    REQUIRE(world.get_component<UiPanel>(b)->alpha == Approx(0.5f));
}
```

Append to `tests/unit/render/render_system_test.cpp`:

```cpp
TEST_CASE("a faded container fades its children", "[render][ui]")
{
    // The end-to-end half: the quad the device receives carries the PRODUCT. Asserted
    // against the fake device rather than against inherited_alpha, because the thing that
    // can be forgotten is the multiply at the call site, not the helper.
    using mosaic::engine::ui::UiPanel;
    using mosaic::engine::ui::UiParent;
    AssetRegistry registry;
    World         world;
    RenderSystem  rs;
    add_camera(world, Vec3{0, 0, 5});

    const auto container = world.create_entity();
    UiPanel outer;
    outer.alpha = 0.5f;
    outer.order = 0;
    world.add_component<UiPanel>(container, outer);

    const auto child = world.create_entity();
    UiPanel inner;
    inner.alpha = 0.5f;
    inner.order = 1;
    world.add_component<UiPanel>(child, inner);
    world.add_component<UiParent>(child, UiParent{container});

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.ui_draws.size() == 1);
    const auto& quads = device.ui_draws[0];
    REQUIRE(quads.size() == 2);
    // order 0 then 1: the container first, then the child at 0.5 * 0.5.
    REQUIRE(quads[0].alpha == Catch::Approx(0.5f));
    REQUIRE(quads[1].alpha == Catch::Approx(0.25f));
}
```

> `FakeRenderDevice`'s UI-quad recording member may be named differently from
> `ui_draws`; read the fake and use whatever it calls the vector of `draw_ui`
> payloads.

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build build/debug -j4 2>&1 | grep -E "error" | head -3
```

Expected: FAIL at compile — `inherited_alpha` is not declared.

- [ ] **Step 3: Write the implementation**

Add to `engine/ui/ui_layout.hpp`:

```cpp
// The product of every ANCESTOR panel's alpha, so that fading a container fades everything
// inside it. The element's own alpha is NOT included -- the render pass multiplies that in
// separately, which keeps the authored early-out (`alpha <= 0` means hidden) reading the
// authored value.
//
// A RENDER-TIME OVERRIDE THAT NEVER WRITES BACK, the discipline resolve_appearance states
// in render_system.hpp: "nothing writes to p.color, so the authored value survives and a
// scene saved while hovering carries no hover tint." A scene saved mid-fade must carry the
// authored alphas, not the product.
//
// An ancestor with no UiPanel contributes nothing rather than zero: UiParent may name any
// entity, resolve_parent_frame already treats that case as "no frame", and treating it as
// alpha 0 would hide children for a reason nobody authored. Same kMaxDepth as
// resolve_parent_frame, for the same reason -- a cycle in UiParent must terminate.
inline float inherited_alpha(mosaic::engine::World& world, mosaic::foundation::EntityID e)
{
    constexpr int kMaxDepth = 16;
    float         product   = 1.0f;
    mosaic::foundation::EntityID cur = e;
    for (int depth = 0; depth < kMaxDepth; ++depth)
    {
        const UiParent* up = world.get_component<UiParent>(cur);
        if (up == nullptr || !world.is_valid(up->parent)) { break; }
        if (const UiPanel* pp = world.get_component<UiPanel>(up->parent))
        {
            product *= pp->alpha;
        }
        cur = up->parent;
    }
    return product;
}
```

In `engine/render/render_system.hpp`, in each of the three HUD passes, multiply
the resolved alpha by the inherited one. For the panel pass, change:

```cpp
                    pending.push_back({p.order, UiQuad{rect, app.color, app.alpha}});
```

to:

```cpp
                    // Inherited alpha multiplies the RESOLVED alpha, after the authored
                    // early-out above. Both directions matter: an element authored at 0
                    // stays hidden whatever its ancestors say, and a container at 0 hides
                    // a child whose own alpha is 1.
                    pending.push_back(
                        {p.order, UiQuad{rect, app.color,
                                         app.alpha * mosaic::engine::ui::inherited_alpha(world, e)}});
```

Apply the same `* mosaic::engine::ui::inherited_alpha(world, e)` to the alpha the
image pass and the text pass pass downstream — for text, multiply the `app.alpha`
argument handed to `layout_text`.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build build/debug -j4 2>&1 | tail -3 && \
  ctest --test-dir build/debug --output-on-failure -R "inherited_alpha|faded container" 2>&1 | tail -8
```

Expected: PASS, 5 tests.

- [ ] **Step 5: Run the whole suite**

```bash
ctest --test-dir build/debug --output-on-failure 2>&1 | tail -10
```

Expected: 100% tests passed.

- [ ] **Step 6: Run the mutation probe**

Change `inherited_alpha` to return the nearest parent's alpha instead of the
product (`product = pp->alpha;` then `break;`), rebuild, and confirm
`inherited_alpha multiplies down the ancestor chain` turns RED. Restore and
`touch`.

- [ ] **Step 7: Commit**

```bash
git add engine tests
git commit -m "$(cat <<'EOF'
feat(ui): a container's alpha fades its children

The product of every ancestor panel's alpha, multiplied into the three
HUD passes at render time. Nothing writes back -- the discipline
resolve_appearance already states, so a scene saved mid-fade carries the
authored alphas.

The element's own alpha stays out of the product, so the authored
early-out keeps reading the authored value: an element at 0 stays hidden
whatever its ancestors say.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017SduWFXfqsJjE4EUC8vYat
EOF
)"
```

---

### Task 6: Author a stack in the demo, and the docs

**Files:**
- Modify: `app/run/demo_scene.hpp`
- Modify: `tests/unit/app/demo_scene_test.cpp`
- Modify: `docs/project/next-steps.md`, `docs/project/deferred-work-backlog.md`

**Interfaces:**
- Consumes: everything above.
- Produces: nothing other tasks read.

**Why the demo gets one.** Without it the feature ships unreachable at the
window: there is no stack in any scene, so `mosaic run --window` renders
identically and the verification item has nothing to look at.

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/app/demo_scene_test.cpp`:

```cpp
TEST_CASE("the demo scene authors a stack with children", "[app][layout]")
{
    // A stack with no children lays nothing out, and a child with no stack parent is not a
    // layout -- so the reachable feature is the PAIR, and that is what this asserts.
    using mosaic::engine::ui::UiParent;
    using mosaic::engine::ui::UiStack;
    World world;
    build_default_scene(world);

    int stacks = 0;
    World::EntityID stack_entity{};
    world.for_each<UiStack>(
        [&](mosaic::foundation::EntityID e, UiStack&)
        {
            ++stacks;
            stack_entity = e;
        });
    REQUIRE(stacks == 1);

    int children = 0;
    world.for_each<UiParent>(
        [&](mosaic::foundation::EntityID, UiParent& up)
        {
            if (up.parent == stack_entity) { ++children; }
        });
    REQUIRE(children >= 2);
}
```

Add `#include "engine/ui/ui_stack.hpp"` and `#include "engine/ui/ui_parent.hpp"`
at the top if not already present.

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build/debug -j4 2>&1 | tail -3 && \
  ctest --test-dir build/debug --output-on-failure -R "demo scene authors a stack" 2>&1 | tail -8
```

Expected: FAIL on `REQUIRE(stacks == 1)` with `stacks == 0`.

- [ ] **Step 3: Author it**

In `app/run/demo_scene.hpp`, after the existing HUD panel on the camera entity,
add a container panel with a vertical stack and three children. Use a
**distinctive, checkable geometry** so the verification item has a constant to
measure against:

```cpp
    // A vertical stack of three panels, authored so the verification pass has numbers to
    // check rather than an impression: the container is a quarter of the screen wide and
    // half its height, anchored top-right, and `spacing = 0` means the three children must
    // EXACTLY tile it -- no seam, no overlap, at any window size.
    //
    // Brace-initialised positionally, like every component in this file. UiStack's three
    // fields are int/float/float, so adding a member would re-bind these silently where
    // the types happen to line up -- re-read this call site if UiStack ever grows one.
    const auto hud_stack = world.create_entity();
    world.add_component<mosaic::engine::ui::UiPanel>(hud_stack,
        mosaic::engine::ui::UiPanel{
            static_cast<int>(mosaic::engine::ui::Anchor::TopRight),
            Vec2{0.02f, 0.02f}, Vec2{0.25f, 0.5f},
            Vec3{0.05f, 0.05f, 0.08f}, 0.55f, 10});
    world.add_component<mosaic::engine::ui::UiStack>(hud_stack,
        mosaic::engine::ui::UiStack{0, 0.0f, 0.0f});

    // Three children filling their slots, in authored order, each a different colour so a
    // reordering is visible rather than merely wrong.
    const Vec3 slot_colors[3] = {Vec3{0.9f, 0.3f, 0.3f}, Vec3{0.3f, 0.9f, 0.4f},
                                 Vec3{0.3f, 0.5f, 0.95f}};
    for (int i = 0; i < 3; ++i)
    {
        const auto slot = world.create_entity();
        world.add_component<mosaic::engine::ui::UiPanel>(slot,
            mosaic::engine::ui::UiPanel{
                static_cast<int>(mosaic::engine::ui::Anchor::TopLeft),
                Vec2{0.0f, 0.0f}, Vec2{1.0f, 1.0f},
                slot_colors[i], 0.85f, 11 + i});
        world.add_component<mosaic::engine::ui::UiParent>(slot,
            mosaic::engine::ui::UiParent{hud_stack});
    }
```

Add `#include "engine/ui/ui_stack.hpp"` beside the other `engine/ui/` includes.

> **This adds 4 entities to the default scene.** Four app tests assert the
> entity count — find them with
> `grep -rn "entity_count" tests/unit/app/ | head` and update all four. That is a
> known trap in this repo with a memory of its own; the tests are the record of
> the scene's shape, not an obstacle.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build build/debug -j4 2>&1 | tail -3 && \
  ctest --test-dir build/debug --output-on-failure 2>&1 | tail -10
```

Expected: 100% tests passed, after the four count assertions are updated.

- [ ] **Step 5: Verify at the window**

```bash
MOSAIC_HEADLESS=0 timeout 20 ./build/debug/app/mosaic run --window 2>&1 \
  | grep -ciE "validation|VUID|error"
MOSAIC_VK_SYNC_VALIDATION=1 MOSAIC_HEADLESS=0 timeout 25 ./build/debug/app/mosaic editor 2>&1 \
  | grep -cE "(READ|WRITE)_AFTER_WRITE hazard"
```

Expected: 0 for the first. For the second, the count must not be **higher** than
the pre-existing baseline — this slice adds no render pass, so any increase would
be a surprise worth reading. (The known baseline is a depth `loadOp CLEAR` class
in a non-bloom pass, recorded in the backlog.)

> The sandbox cannot screenshot a GUI window, so a clean run means the API was
> used legally, NOT that the stack looks right.

- [ ] **Step 6: Update the project docs**

In `docs/project/next-steps.md`, prepend a new `**Last updated:**` entry (the
file's convention — newest first, above the previous one) covering: U8a shipped;
the two corrections to the U8 decomposition and why each was found by reading;
the reserved decision and how narrowing the frame dissolved it; and the
verification item below. In
`docs/project/deferred-work-backlog.md`, mark U8a's half of the UI-pillar entry
shipped in the existing `~~struck~~ — **SHIPPED <date>**` style. The four
deferrals written alongside the spec stay open.

- [ ] **Step 7: Commit**

```bash
git add -A app docs tests
git commit -m "$(cat <<'EOF'
feat(demo): author a UiStack in the demo scene

Without one the feature ships unreachable at the window: no scene has a
stack, so `run --window` would render identically.

Authored with checkable geometry rather than a pleasing one -- the
container is a quarter wide and half high, and `spacing = 0` means the
three children must EXACTLY tile it, so a seam or an overlap is a
measurement rather than an impression.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017SduWFXfqsJjE4EUC8vYat
EOF
)"
```

---

## What this plan cannot verify

The arithmetic is pinned numerically, so less rests on a human than usual. One
item for the next verification pass, with its pass condition anchored on an
authored constant:

**The three stacked panels in the top-right must exactly tile their container**,
in that colour order top to bottom (red, green, blue), with no seam and no
overlap, at two different window sizes. `spacing` is authored at `0` and each
child's `size` is `{1,1}`, so the thirds must sum to the container exactly. A
seam means the slot division leaves a gap; an overlap means it double-counts
spacing; a wrong colour order means `stack_children` is not sorting by the
authored `order`.

The Inspector shows `UiStack`'s three fields through reflection with no work, so
authoring one is reachable immediately — but nothing *visualises* a slot, so an
author tuning `padding` is reading numbers. A slot gizmo is deferred and in the
backlog.
