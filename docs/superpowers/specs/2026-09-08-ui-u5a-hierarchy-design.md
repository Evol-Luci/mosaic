# UI U5a — Layout / hierarchy — Design

## Goal (Phase 4, UI pillar — slice 5a)

Let HUD elements **nest**: a child element positions relative to its parent container's
resolved rect instead of the whole screen, so a group (a panel + its labels/icons/buttons)
moves and anchors together. This is the first half of U5 (**U5b** = world-space UI
panels, next).

The mechanism generalizes U1's `resolve_rect`: resolution happens within an arbitrary
**frame** rect; the screen is just the root frame `{0,0,1,1}`. A `UiParent { EntityID }`
component names a child's container (an entity carrying a `UiPanel`); a hierarchy resolver
walks the parent chain to produce each element's screen rect, applied at every UI rect
site (panel/image/text render passes + button hit-test).

Base branch: `main` (U1–U4 merged). **Scope (YAGNI):** positioning only — no inherited
alpha/visibility, no clipping/scroll, no flow/grid auto-layout. Parents are `UiPanel`
containers.

## Design

### `engine/ui/ui_rect.hpp` — `resolve_in`

Add a frame-relative resolve; `resolve_rect` becomes the screen-frame case (behavior
unchanged for all existing callers):

```cpp
// Resolve an element within an arbitrary frame rect (normalized, top-left origin).
// pos/size are fractions of `frame`; anchor picks the frame's corner/edge.
inline Rect resolve_in(const Rect& frame, Anchor anchor, Vec2 pos, Vec2 size)
{
    // Resolve in a local [0,1] frame, then map into `frame`.
    const Rect local = resolve_rect(anchor, pos, size); // existing screen-space math
    return Rect{ frame.x + local.x * frame.w,
                 frame.y + local.y * frame.h,
                 local.w * frame.w,
                 local.h * frame.h };
}
```

`resolve_rect(a, p, s)` is unchanged and equals `resolve_in({0,0,1,1}, a, p, s)`.

### `engine/ui/ui_parent.hpp` — reflected component

```cpp
#pragma once
#include "foundation/serialization/reference.hpp" // (EntityID reflects like Transform.parent)
#include "foundation/core/entity_id.hpp"

namespace mosaic::engine::ui
{
// Names this element's container: an entity carrying a UiPanel. The element's pos/size
// then resolve within the parent panel's resolved rect (recursively up the panel chain).
// nil parent (or a parent without a UiPanel) -> resolve against the whole screen.
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

Registered `"UiParent"` in `builtin_components.hpp`. (`EntityID` is a reflected field —
`Transform.parent` uses the same pattern.)

### `engine/ui/ui_hierarchy.hpp` — the frame resolver

Walks `UiParent → parent UiPanel`, mirroring `TransformSystem`'s depth-capped parent-chain
walk (`world.get_component<T>` / `world.is_valid`):

```cpp
#pragma once
#include "engine/ui/ui_panel.hpp"
#include "engine/ui/ui_parent.hpp"
#include "engine/ui/ui_rect.hpp"
#include "engine/world/world.hpp"

namespace mosaic::engine::ui
{
inline Anchor hierarchy_anchor(int a) // clamp int -> Anchor (out of range -> TopLeft)
{ return (a >= 0 && a <= 8) ? static_cast<Anchor>(a) : Anchor::TopLeft; }

// This element's screen rect, resolved within its parent frame.
Rect resolve_ui_rect(mosaic::engine::World& world, mosaic::foundation::EntityID e,
                     Anchor anchor, Vec2 pos, Vec2 size);

// The frame a child positions within: the parent UiPanel's resolved rect (recursive),
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
    // The parent panel's own rect = resolve_in(its parent frame, its fields).
    const Rect parent_frame = resolve_parent_frame(world, up->parent, depth + 1);
    return resolve_in(parent_frame, hierarchy_anchor(pp->anchor), pp->pos, pp->size);
}

inline Rect resolve_ui_rect(mosaic::engine::World& world, mosaic::foundation::EntityID e,
                            Anchor anchor, Vec2 pos, Vec2 size)
{
    return resolve_in(resolve_parent_frame(world, e), anchor, pos, size);
}
} // namespace mosaic::engine::ui
```

Cycle safety: the `kMaxDepth` cap bounds recursion (a self- or mutually-parented loop
falls back to the screen frame), same discipline as `TransformSystem`.

### Apply at every rect site

- **`engine/render/render_system.hpp` panel pass** — replace
  `resolve_rect(to_anchor(p.anchor), p.pos, p.size)` with
  `mosaic::engine::ui::resolve_ui_rect(world, e, to_anchor(p.anchor), p.pos, p.size)`
  (the `for_each` lambda already has the entity `e`).
- **image pass** — same, with `im`.
- **text pass** — `layout_text` gains a trailing `const Rect& frame` parameter (defaulted
  to `Rect{0,0,1,1}` so existing callers/tests are unaffected); internally it resolves the
  text box with `resolve_in(frame, anchor, pos, {W, height})` instead of `resolve_rect`.
  The text pass passes `resolve_parent_frame(world, e)` as `frame`.
- **`engine/ui/ui_system.hpp` button hit-test** — `hit()` uses
  `resolve_ui_rect(world, e, button_anchor(b.anchor), b.pos, b.size)`; `update` threads
  the entity `e` and `world` into `hit`.

Include `engine/ui/ui_hierarchy.hpp` where used (`render_system.hpp`, `ui_system.hpp`).

### Demo — a nested child label

Add **one child entity**: a `UiText` label carrying `UiParent{ camera }` (the camera
already has the HUD-bar `UiPanel`), positioned within the bar (e.g. anchor `TopLeft`,
small `pos`/`height`) so it tracks the bar. This adds one entity → the **four default-scene
count tests update by +1** (per the project's default-scene-count convention).

## Testing (headless)

- **`resolve_in`** — an element within a non-screen frame (e.g. frame
  `{0.5, 0.25, 0.4, 0.4}`, child anchor `TopLeft` `pos{0,0}` `size{0.5,0.5}` →
  `{0.5, 0.25, 0.2, 0.2}`); frame `{0,0,1,1}` reproduces `resolve_rect`.
- **`resolve_parent_frame` / `resolve_ui_rect`** — a child with `UiParent → panel`
  resolves inside the panel's rect; a nested panel chain composes; no `UiParent` → screen;
  invalid parent / parent without `UiPanel` → screen; a self-parent cycle → screen (depth
  cap, no infinite loop).
- **`UiParent` reflection** — round-trip (the `EntityID parent` field) + registration under
  `"UiParent"`.
- **`RenderSystem` hierarchy** — a parent `UiPanel` + a child `UiPanel` with
  `UiParent`: the child's emitted `UiQuad` rect lies inside the parent's rect (via
  `FakeRenderDevice`).
- **`layout_text` frame** — passing a non-screen `frame` offsets/scales the glyph quads
  into that frame; the default frame reproduces current behavior.
- **Default-scene counts** — the four count tests updated for the +1 child entity.

`mosaic run --window` exits 0 with no validation errors is the integration check; the
on-screen nested layout is the maintainer's hardware check.

## Files

**New**
- `engine/ui/ui_parent.hpp` — reflected `UiParent`.
- `engine/ui/ui_hierarchy.hpp` — `resolve_parent_frame` / `resolve_ui_rect`.
- `tests/unit/ui/ui_hierarchy_test.cpp` — `resolve_in` + frame-resolution tests.
- `tests/unit/ui/ui_parent_reflection_test.cpp` — reflection + registration.

**Modified**
- `engine/ui/ui_rect.hpp` — `resolve_in`.
- `engine/ui/text_layout.hpp` — `layout_text` gains a defaulted `frame` param.
- `engine/render/render_system.hpp` — panel/image/text passes use the hierarchy resolver.
- `engine/ui/ui_system.hpp` — button hit-test uses `resolve_ui_rect`.
- `engine/reflection/builtin_components.hpp` — register `UiParent`.
- `app/run/demo_scene.hpp` — the demo child label entity.
- `tests/unit/render/render_system_test.cpp` — a hierarchy render case.
- `tests/unit/ui/text_layout_test.cpp` — a `frame` case (or fold into hierarchy test).
- `tests/CMakeLists.txt` — new test sources.
- The four default-scene count tests (+1 entity).

## Non-goals / follow-ups

- **U5b:** world-space UI panels (positioned in 3D via `WorldTransform`, decal-style).
- Inherited alpha/visibility (a hidden parent hides children); clipping / scroll regions;
  flow/grid/stack auto-layout; anchoring a child to a non-`UiPanel` parent; a unified
  `order` across the whole tree.
