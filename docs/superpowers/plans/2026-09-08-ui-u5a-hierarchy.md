# UI U5a — Layout / Hierarchy Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let HUD elements nest — a child positions relative to its parent container's resolved rect instead of the whole screen.

**Architecture:** Generalize `resolve_rect` to `resolve_in(frame, …)` (screen = root frame). A `UiParent { EntityID }` names a child's container (an entity with a `UiPanel`); `resolve_parent_frame`/`resolve_ui_rect` walk the parent-panel chain (depth-capped). Every rect site (panel/image/text render passes + button hit-test) routes through the hierarchy resolver.

**Tech Stack:** C++20, Catch2, CMake, reflection, `World::get_component`/`is_valid` (as `TransformSystem` uses).

**Spec:** `docs/superpowers/specs/2026-09-08-ui-u5a-hierarchy-design.md`

**Base branch:** `claude/ui-u5a-hierarchy` (off `main` = U1–U4; `cmake --preset debug` configured).

## Global Constraints

- **Build convention:** presets into `build/debug/`. Build: `cmake --build --preset debug [--target <t>]`. Test binary: `./build/debug/tests/mosaic_unit_tests "[tag]"` (Catch2 tag; comma = OR). Full suite: `ctest --preset debug`. App: `./build/debug/app/mosaic`. Do **not** use `cmake --build build` or `./build/app/...`. **Transient link failures:** the ~170 MB test binary intermittently fails at the LINK step under memory pressure (`ld` error, "Deleting file", `make Error 2`) with **no compiler error** — if a build fails, `grep -iE "error:|error C[0-9]|undefined reference"`; if empty, just re-run (up to ~3×). If a build leaves a **0-byte** `build/debug/tests/mosaic_unit_tests` (→ `ctest` `BAD_COMMAND` / "Permission denied"), `rm -f` it and rebuild. **After changing `demo_scene.hpp`, rebuild the `mosaic` target before any `mosaic run` check.**
- **Coordinate convention:** normalized screen space, origin top-left, x right, y down, `[0,1]`.
- **Screen frame:** `Rect{0.0f, 0.0f, 1.0f, 1.0f}`. `resolve_rect(a,p,s)` must stay identical to `resolve_in({0,0,1,1}, a, p, s)`.
- **Parent model:** a child's frame is its `UiParent`'s **`UiPanel`** resolved rect (recursive up the panel chain); nil/invalid parent or a parent without `UiPanel` → the screen frame. Depth cap `kMaxDepth = 16` (cycle-safe, like `TransformSystem`).
- **World API:** `world.get_component<T>(id) -> T*` (nullptr if absent); `world.is_valid(id) -> bool`; `world.for_each<T>([](EntityID, T&){…})`. `EntityID` reflects (like `Transform.parent`).
- **Default-scene counts:** all four count tests count **`Transform`** components (`for_each<Transform>`). The demo child (`UiText` + `UiParent`, **no `Transform`**) does not change them — so **no count-test edits are needed** (verify the full suite stays green).

---

### Task 1: `resolve_in` — frame-relative resolution

**Files:**
- Modify: `engine/ui/ui_rect.hpp`
- Create: `tests/unit/ui/ui_hierarchy_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `inline Rect mosaic::engine::ui::resolve_in(const Rect& frame, Anchor, Vec2 pos, Vec2 size);`

- [ ] **Step 1: Write the failing test**

Create `tests/unit/ui/ui_hierarchy_test.cpp`:

```cpp
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/ui/ui_rect.hpp"
#include "foundation/math/vec2.hpp"

using Catch::Approx;
using mosaic::engine::ui::Anchor;
using mosaic::engine::ui::Rect;
using mosaic::engine::ui::resolve_in;
using mosaic::engine::ui::resolve_rect;
using mosaic::foundation::math::Vec2;

TEST_CASE("resolve_in maps an element into a sub-frame", "[ui][layout]")
{
    const Rect frame{0.5f, 0.25f, 0.4f, 0.4f};
    const Rect r = resolve_in(frame, Anchor::TopLeft, Vec2{0.0f, 0.0f}, Vec2{0.5f, 0.5f});
    REQUIRE(r.x == Approx(0.5f));            // frame.x + 0*frame.w
    REQUIRE(r.y == Approx(0.25f));           // frame.y + 0*frame.h
    REQUIRE(r.w == Approx(0.2f));            // 0.5 * frame.w
    REQUIRE(r.h == Approx(0.2f));            // 0.5 * frame.h
}

TEST_CASE("resolve_in with the screen frame equals resolve_rect", "[ui][layout]")
{
    const Rect screen{0.0f, 0.0f, 1.0f, 1.0f};
    const Rect a = resolve_in(screen, Anchor::BottomRight, Vec2{0.1f, 0.1f}, Vec2{0.2f, 0.3f});
    const Rect b = resolve_rect(Anchor::BottomRight, Vec2{0.1f, 0.1f}, Vec2{0.2f, 0.3f});
    REQUIRE(a.x == Approx(b.x));
    REQUIRE(a.y == Approx(b.y));
    REQUIRE(a.w == Approx(b.w));
    REQUIRE(a.h == Approx(b.h));
}
```

Add to `tests/CMakeLists.txt` (near the other `unit/ui/*` entries):

```cmake
  unit/ui/ui_hierarchy_test.cpp
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -20`
Expected: FAIL — `resolve_in` not declared.

- [ ] **Step 3: Write the implementation**

In `engine/ui/ui_rect.hpp`, add `resolve_in` right after `resolve_rect`:

```cpp
// Resolve an element within an arbitrary frame rect (normalized, top-left origin).
// pos/size are fractions of `frame`; anchor picks the frame's corner/edge.
// resolve_rect(a,p,s) == resolve_in(Rect{0,0,1,1}, a, p, s).
inline Rect resolve_in(const Rect& frame, Anchor anchor, Vec2 pos, Vec2 size)
{
    const Rect local = resolve_rect(anchor, pos, size);
    return Rect{frame.x + local.x * frame.w,
                frame.y + local.y * frame.h,
                local.w * frame.w,
                local.h * frame.h};
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -5 && ./build/debug/tests/mosaic_unit_tests "[ui][layout]" 2>&1 | tail -6`
Expected: `[ui][layout]` cases (incl. the U1 `resolve_rect` ones) PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/ui/ui_rect.hpp tests/unit/ui/ui_hierarchy_test.cpp tests/CMakeLists.txt
git commit -m "feat(ui): resolve_in — frame-relative rect resolution"
```

---

### Task 2: `UiParent` reflected component

**Files:**
- Create: `engine/ui/ui_parent.hpp`
- Create: `tests/unit/ui/ui_parent_reflection_test.cpp`
- Modify: `engine/reflection/builtin_components.hpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `struct mosaic::engine::ui::UiParent { mosaic::foundation::EntityID parent; };` registered `"UiParent"`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/ui/ui_parent_reflection_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/ui/ui_parent.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reference.hpp"
#include "foundation/serialization/reflect_value.hpp"

using mosaic::engine::World;
using mosaic::engine::ui::UiParent;

TEST_CASE("UiParent round-trips through reflection", "[ui][reflection]")
{
    World w;
    const auto e = w.create_entity();
    UiParent up; up.parent = e;
    const auto value = mosaic::foundation::serialization::to_value(up);
    UiParent back;
    mosaic::foundation::serialization::from_value(value, back);
    REQUIRE(back.parent == e);
}

TEST_CASE("UiParent is a registered built-in component", "[ui][reflection]")
{
    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    REQUIRE(types.has("UiParent"));
}
```

Add to `tests/CMakeLists.txt`:

```cmake
  unit/ui/ui_parent_reflection_test.cpp
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -20`
Expected: FAIL — `engine/ui/ui_parent.hpp` not found / `types.has("UiParent")` false.

- [ ] **Step 3: Write the implementation**

Create `engine/ui/ui_parent.hpp`:

```cpp
#pragma once

#include "foundation/core/entity_id.hpp"

namespace mosaic::engine::ui
{
// Names this element's container: an entity carrying a UiPanel. The element's pos/size
// then resolve within the parent panel's resolved rect (recursively up the panel chain).
// A nil parent (or a parent without a UiPanel) resolves against the whole screen.
struct UiParent
{
    mosaic::foundation::EntityID parent;
};

template <class V>
void reflect(V& visitor, UiParent& p)
{
    visitor.member("parent", p.parent);
}
} // namespace mosaic::engine::ui
```

Register in `engine/reflection/builtin_components.hpp` — add the include near the other UI
includes and the registration after the `UiButton` line:

```cpp
#include "engine/ui/ui_parent.hpp"
```
```cpp
    types.register_component<mosaic::engine::ui::UiParent>(registry, "UiParent");
```

> If `EntityID` needs a specific include/serialization helper, check `engine/transform/transform.hpp`
> — `Transform.parent` is an `EntityID` reflected member and is the working precedent.

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -5 && ./build/debug/tests/mosaic_unit_tests "[ui][reflection]" 2>&1 | tail -6`
Expected: `[ui][reflection]` cases PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/ui/ui_parent.hpp engine/reflection/builtin_components.hpp \
        tests/unit/ui/ui_parent_reflection_test.cpp tests/CMakeLists.txt
git commit -m "feat(ui): reflected UiParent component (child -> container link)"
```

---

### Task 3: `ui_hierarchy.hpp` — the frame resolver

**Files:**
- Create: `engine/ui/ui_hierarchy.hpp`
- Modify: `tests/unit/ui/ui_hierarchy_test.cpp` (frame-resolution cases)

**Interfaces:**
- Consumes: `resolve_in` (Task 1), `UiParent` (Task 2), `UiPanel` (U1), `World`.
- Produces:
  - `Rect resolve_parent_frame(World&, EntityID, int depth=0)`
  - `Rect resolve_ui_rect(World&, EntityID, Anchor, Vec2 pos, Vec2 size)`
  - `Anchor hierarchy_anchor(int)`

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/ui/ui_hierarchy_test.cpp`:

```cpp
#include "engine/ui/ui_hierarchy.hpp"
#include "engine/ui/ui_panel.hpp"
#include "engine/ui/ui_parent.hpp"
#include "engine/world/world.hpp"

using mosaic::engine::World;
using mosaic::engine::ui::resolve_parent_frame;
using mosaic::engine::ui::resolve_ui_rect;
using mosaic::engine::ui::UiPanel;
using mosaic::engine::ui::UiParent;

TEST_CASE("resolve_parent_frame returns the screen when there is no UiParent", "[ui][layout]")
{
    World w;
    const auto e = w.create_entity();
    const Rect f = resolve_parent_frame(w, e);
    REQUIRE(f.x == Approx(0.0f)); REQUIRE(f.y == Approx(0.0f));
    REQUIRE(f.w == Approx(1.0f)); REQUIRE(f.h == Approx(1.0f));
}

TEST_CASE("resolve_ui_rect places a child inside its parent panel", "[ui][layout]")
{
    World w;
    const auto parent = w.create_entity();
    UiPanel pp; pp.anchor = static_cast<int>(Anchor::TopLeft);
    pp.pos = Vec2{0.5f, 0.5f}; pp.size = Vec2{0.4f, 0.4f};
    w.add_component<UiPanel>(parent, pp);

    const auto child = w.create_entity();
    UiParent up; up.parent = parent; w.add_component<UiParent>(child, up);

    // Child TopLeft {0,0} size {0.5,0.5} within the parent frame {0.5,0.5,0.4,0.4}.
    const Rect r = resolve_ui_rect(w, child, Anchor::TopLeft, Vec2{0.0f, 0.0f}, Vec2{0.5f, 0.5f});
    REQUIRE(r.x == Approx(0.5f));
    REQUIRE(r.y == Approx(0.5f));
    REQUIRE(r.w == Approx(0.2f));
    REQUIRE(r.h == Approx(0.2f));
}

TEST_CASE("resolve_parent_frame falls back to screen on invalid/cyclic parents", "[ui][layout]")
{
    World w;
    // Parent id that is not a live entity -> screen.
    const auto orphan = w.create_entity();
    UiParent bad; bad.parent = mosaic::foundation::EntityID{}; w.add_component<UiParent>(orphan, bad);
    const Rect f1 = resolve_parent_frame(w, orphan);
    REQUIRE(f1.w == Approx(1.0f));

    // Self-parent cycle -> depth cap -> screen (no infinite loop). Needs a UiPanel so the
    // walk recurses.
    const auto self = w.create_entity();
    UiPanel sp; sp.pos = Vec2{0.1f, 0.1f}; sp.size = Vec2{0.5f, 0.5f};
    w.add_component<UiPanel>(self, sp);
    UiParent cyc; cyc.parent = self; w.add_component<UiParent>(self, cyc);
    const Rect f2 = resolve_parent_frame(w, self); // must terminate
    REQUIRE(f2.w > 0.0f);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -20`
Expected: FAIL — `engine/ui/ui_hierarchy.hpp` not found.

- [ ] **Step 3: Write the implementation**

Create `engine/ui/ui_hierarchy.hpp`:

```cpp
#pragma once

#include "engine/ui/ui_panel.hpp"
#include "engine/ui/ui_parent.hpp"
#include "engine/ui/ui_rect.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"

namespace mosaic::engine::ui
{
// Clamp an int to a ui::Anchor (out of range -> TopLeft).
inline Anchor hierarchy_anchor(int a)
{
    return (a >= 0 && a <= 8) ? static_cast<Anchor>(a) : Anchor::TopLeft;
}

// The frame a child positions within: its parent UiPanel's resolved rect (recursive),
// or the whole screen {0,0,1,1} if no UiParent / invalid parent / parent has no UiPanel.
inline Rect resolve_parent_frame(mosaic::engine::World& world,
                                 mosaic::foundation::EntityID e, int depth = 0)
{
    constexpr int kMaxDepth = 16;
    const Rect kScreen{0.0f, 0.0f, 1.0f, 1.0f};
    if (depth >= kMaxDepth) { return kScreen; }
    UiParent* up = world.get_component<UiParent>(e);
    if (up == nullptr || !world.is_valid(up->parent)) { return kScreen; }
    UiPanel* pp = world.get_component<UiPanel>(up->parent);
    if (pp == nullptr) { return kScreen; }
    const Rect parent_frame = resolve_parent_frame(world, up->parent, depth + 1);
    return resolve_in(parent_frame, hierarchy_anchor(pp->anchor), pp->pos, pp->size);
}

// This element's screen rect, resolved within its parent frame.
inline Rect resolve_ui_rect(mosaic::engine::World& world, mosaic::foundation::EntityID e,
                            Anchor anchor, Vec2 pos, Vec2 size)
{
    return resolve_in(resolve_parent_frame(world, e), anchor, pos, size);
}
} // namespace mosaic::engine::ui
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -5 && ./build/debug/tests/mosaic_unit_tests "[ui][layout]" 2>&1 | tail -8`
Expected: all `[ui][layout]` cases PASS (no hang on the cycle case).

- [ ] **Step 5: Commit**

```bash
git add engine/ui/ui_hierarchy.hpp tests/unit/ui/ui_hierarchy_test.cpp
git commit -m "feat(ui): ui_hierarchy — parent-frame resolver (depth-capped, cycle-safe)"
```

---

### Task 4: `layout_text` gains a `frame` param

**Files:**
- Modify: `engine/ui/text_layout.hpp`
- Modify: `tests/unit/ui/text_layout_test.cpp`

**Interfaces:**
- Produces: `layout_text(..., Vec3 color, float alpha, const Rect& frame = Rect{0,0,1,1})`.

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/ui/text_layout_test.cpp` (it already bakes a fixture font via
`fixture_font()` and includes `ui_rect.hpp` transitively through `text_layout.hpp`):

```cpp
TEST_CASE("layout_text offsets glyph quads into a non-screen frame", "[ui][text]")
{
    const FontAtlas font = fixture_font();
    const mosaic::engine::ui::Rect frame{0.5f, 0.5f, 0.5f, 0.5f};
    const auto full  = layout_text(font, "A", Anchor::TopLeft, Vec2{0, 0}, 0.1f, 1.0f, kWhite, 1.0f);
    const auto inset = layout_text(font, "A", Anchor::TopLeft, Vec2{0, 0}, 0.1f, 1.0f, kWhite, 1.0f, frame);
    REQUIRE(!inset.empty());
    // The frame's origin is (0.5,0.5), so the inset glyph sits to the lower-right of the
    // screen-space one and is half the width.
    REQUIRE(inset[0].rect.x > full[0].rect.x);
    REQUIRE(inset[0].rect.w == Approx(full[0].rect.w * 0.5f));
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -20`
Expected: FAIL — `layout_text` takes 8 args, not 9.

- [ ] **Step 3: Write the implementation**

In `engine/ui/text_layout.hpp`, add a trailing defaulted `frame` param and resolve the
text box within it:

Change the signature:

```cpp
inline std::vector<TextQuad> layout_text(const FontAtlas& font, const std::string& text,
                                         Anchor anchor, mosaic::foundation::math::Vec2 pos,
                                         float height, float aspect,
                                         mosaic::foundation::math::Vec3 color, float alpha,
                                         const Rect& frame = Rect{0.0f, 0.0f, 1.0f, 1.0f})
```

Change the box line:

```cpp
    const Rect box = resolve_rect(anchor, pos, mosaic::foundation::math::Vec2{w, height});
```
to:
```cpp
    const Rect box = resolve_in(frame, anchor, pos, mosaic::foundation::math::Vec2{w, height});
```

(`resolve_in` is already declared in `ui_rect.hpp`, which `text_layout.hpp` includes.)

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -5 && ./build/debug/tests/mosaic_unit_tests "[ui][text]" 2>&1 | tail -6`
Expected: the `[ui][text]` cases (incl. the existing U2b ones with the default frame) PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/ui/text_layout.hpp tests/unit/ui/text_layout_test.cpp
git commit -m "feat(ui): layout_text accepts a frame (nested text layout)"
```

---

### Task 5: Route the render passes + button hit-test through the hierarchy resolver

**Files:**
- Modify: `engine/render/render_system.hpp` (panel/image/text passes)
- Modify: `engine/ui/ui_system.hpp` (button hit-test)
- Modify: `tests/unit/render/render_system_test.cpp` (hierarchy render case)

**Interfaces:**
- Consumes: `resolve_ui_rect`/`resolve_parent_frame` (Task 3), `layout_text(frame)` (Task 4).

- [ ] **Step 1: Write the failing test**

In `tests/unit/render/render_system_test.cpp`, add an include:

```cpp
#include "engine/ui/ui_parent.hpp"
```

Add a case (a child `UiPanel` inside a parent `UiPanel` renders inside the parent rect):

```cpp
TEST_CASE("RenderSystem resolves a child UiPanel within its parent panel", "[render][ui]")
{
    using mosaic::engine::ui::UiPanel;
    using mosaic::engine::ui::UiParent;
    AssetRegistry registry;
    World world;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 5});

    auto parent = world.create_entity();
    UiPanel pp; pp.anchor = 0; pp.pos = {0.5f, 0.5f}; pp.size = {0.4f, 0.4f}; pp.alpha = 1.0f;
    world.add_component<UiPanel>(parent, pp);

    auto child = world.create_entity();
    UiPanel cp; cp.anchor = 0; cp.pos = {0.0f, 0.0f}; cp.size = {0.5f, 0.5f}; cp.alpha = 1.0f;
    world.add_component<UiPanel>(child, cp);
    world.add_component<UiParent>(child, UiParent{parent});

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.ui_draws.size() == 1);
    const auto& quads = device.ui_draws.front();
    REQUIRE(quads.size() == 2); // parent + child
    // The child (order-independent here; both order 0) sits inside the parent frame:
    // its top-left is at the parent's top-left (0.5,0.5) and it is 0.2 wide (0.5*0.4).
    bool found_child = false;
    for (const auto& q : quads)
    {
        if (q.rect.w == Approx(0.2f) && q.rect.x == Approx(0.5f) && q.rect.y == Approx(0.5f))
        {
            found_child = true;
        }
    }
    REQUIRE(found_child);
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -20`
Expected: FAIL — the child renders at the screen-relative rect (`0.0,0.0,0.5,0.5`), not
inside the parent, so `found_child` is false.

- [ ] **Step 3: Write the implementation**

In `engine/render/render_system.hpp`, add the include:

```cpp
#include "engine/ui/ui_hierarchy.hpp"
```

**Panel pass** — replace the rect line (currently
`const auto rect = mosaic::engine::ui::resolve_rect(to_anchor(p.anchor), p.pos, p.size);`)
with:

```cpp
                    const auto rect = mosaic::engine::ui::resolve_ui_rect(
                        world, e, to_anchor(p.anchor), p.pos, p.size);
```

Confirm the panel `for_each` lambda binds the entity: it must be
`[&](mosaic::foundation::EntityID e, mosaic::engine::ui::UiPanel& p)` (rename the unused
`EntityID` param to `e`).

**Image pass** — likewise, the lambda binds `e`, and replace
`resolve_rect(to_anchor(im.anchor), im.pos, im.size)` with
`resolve_ui_rect(world, e, to_anchor(im.anchor), im.pos, im.size)`.

**Text pass** — bind `e` in the lambda and pass the parent frame to `layout_text`:

```cpp
                [&](mosaic::foundation::EntityID e, mosaic::engine::ui::UiText& t)
                {
                    ...
                    const mosaic::engine::ui::Rect frame =
                        mosaic::engine::ui::resolve_parent_frame(world, e);
                    const std::vector<mosaic::engine::ui::TextQuad> tq =
                        mosaic::engine::ui::layout_text(*font, t.text, to_anchor(t.anchor),
                                                        t.pos, t.height, aspect_, t.color, t.alpha,
                                                        frame);
                    ...
                }
```

In `engine/ui/ui_system.hpp`, add the include:

```cpp
#include "engine/ui/ui_hierarchy.hpp"
```

Change `hit` to take the world + entity and use the hierarchy resolver, and update both
call sites in `update` (they already have `world` and `e`):

```cpp
    static bool hit(mosaic::engine::World& world, mosaic::foundation::EntityID e,
                    const PointerState& p, UiButton& b)
    {
        const Rect r = resolve_ui_rect(world, e, button_anchor(b.anchor), b.pos, b.size);
        return p.x >= r.x && p.x <= r.x + r.w && p.y >= r.y && p.y <= r.y + r.h;
    }
```

Down-edge call: `if (hit(world, e, p, b)) { pressed_ = e; }`.
Up-edge call: `if (e != pressed_ || !hit(world, e, p, b)) { return; }`.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -5 && ./build/debug/tests/mosaic_unit_tests "[render],[ui]" 2>&1 | tail -8`
Expected: the new hierarchy render case PASSES; all existing render/ui/input tests still
PASS (non-nested elements resolve against the screen frame exactly as before).

- [ ] **Step 5: Commit**

```bash
git add engine/render/render_system.hpp engine/ui/ui_system.hpp \
        tests/unit/render/render_system_test.cpp
git commit -m "feat(ui): route panel/image/text passes + button hit-test through the hierarchy resolver"
```

---

### Task 6: Demo nested label + verify + next-steps

**Files:**
- Modify: `app/run/demo_scene.hpp` (child label entity)
- Modify: `docs/project/next-steps.md`

- [ ] **Step 1: Add the demo child label**

In `app/run/demo_scene.hpp`, add the include near the other UI includes:

```cpp
#include "engine/ui/ui_parent.hpp"
```

In `build_default_scene`, after the camera's `UiPanel`/`UiText`/`UiImage`/`UiButton`
attaches, add a **new child entity** — a `UiText` label parented to the camera (which
carries the HUD-bar `UiPanel`), positioned within the bar. It carries **no `Transform`**,
so the default-scene `Transform`-count tests are unaffected:

```cpp
    const auto hud_label = world.create_entity();
    world.add_component<mosaic::engine::ui::UiText>(hud_label,
        mosaic::engine::ui::UiText{
            kBuiltinFontId, "HP",
            static_cast<int>(mosaic::engine::ui::Anchor::TopLeft),
            mosaic::foundation::math::Vec2{0.05f, 0.15f}, 0.5f,
            mosaic::foundation::math::Vec3{1.0f, 1.0f, 1.0f}, 1.0f, 3});
    world.add_component<mosaic::engine::ui::UiParent>(hud_label,
        mosaic::engine::ui::UiParent{camera});
```

> `UiText` init order is `{font, text, anchor, pos, height, color, alpha, order}` (height
> `0.5` = half the bar's height; the label tracks the bar via `UiParent{camera}`).
> `kBuiltinFontId` is defined in this header.

- [ ] **Step 2: Build + full suite + headless run**

```bash
cmake --build --preset debug 2>&1 | tail -5
ctest --preset debug 2>&1 | tail -4
./build/debug/app/mosaic run --window --ticks 3 2>&1 | tail -5; echo "exit: $?"
```
Expected: all tests PASS (`Transform`-count tests unchanged — the child has no
`Transform`; the default scene round-trips with the `UiParent` `EntityID` remapped like
`Transform.parent`); `mosaic run --window` exits 0 with no validation errors.

> If `scene_save_roundtrip` fails, confirm `UiParent`'s `EntityID parent` serializes +
> remaps (it should, mirroring `Transform.parent`) — but the count assertions there count
> `Transform`, so they should not move.

- [ ] **Step 3: Update the handoff + commit**

In `docs/project/next-steps.md`:
1. Update the **Last updated** line (`2026-09-08`) with a "UI — U5a (layout/hierarchy) DONE" summary: `resolve_in` frame-relative resolution + a `UiParent` child→container link + a depth-capped `resolve_parent_frame`/`resolve_ui_rect` (walks the parent-`UiPanel` chain); every rect site (panel/image/text passes + button hit-test) routes through it, and `layout_text` gained a `frame` param; demo nested label. Headless-tested; GPU visual maintainer-verified.
2. In the UI-decomposition line, mark **U5a done** and point at **U5b (world-space UI panels)** as next; link the U5a spec + this plan.

```bash
git add app/run/demo_scene.hpp docs/project/next-steps.md
git commit -m "feat(ui): demo nested HUD label; docs: mark U5a done, U5b next"
```

---

## Self-Review

**Spec coverage:**
- `resolve_in` frame-relative resolution → Task 1. ✓
- `UiParent` reflected + `"UiParent"` registration → Task 2. ✓
- `resolve_parent_frame`/`resolve_ui_rect` (parent-panel chain, depth-capped, cycle-safe) → Task 3. ✓
- `layout_text` `frame` param → Task 4. ✓
- Panel/image/text passes + button hit-test through the resolver → Task 5. ✓
- Demo nested child → Task 6. ✓
- Tests: `resolve_in` (Task 1); frame resolution incl. no-parent/invalid/cycle (Task 3); `UiParent` reflection (Task 2); hierarchy render (Task 5); `layout_text` frame (Task 4); full-suite counts unchanged (Task 6). ✓

**Placeholder scan:** no TBD/TODO; every code step has real code. Task 5 gives the exact line replacements + the lambda entity-binding change.

**Type consistency:** `resolve_in(const Rect&, Anchor, Vec2, Vec2) -> Rect` (Tasks 1,3,4); `resolve_parent_frame(World&, EntityID, int) -> Rect` + `resolve_ui_rect(World&, EntityID, Anchor, Vec2, Vec2) -> Rect` (Tasks 3,5); `UiParent{ EntityID parent }` (Tasks 2,3,5,6); `layout_text(..., const Rect& frame = {0,0,1,1})` (Tasks 4,5); `hit(World&, EntityID, const PointerState&, UiButton&)` (Task 5). Registration name `"UiParent"` consistent (Tasks 2,6). Screen frame `{0,0,1,1}` consistent throughout.
