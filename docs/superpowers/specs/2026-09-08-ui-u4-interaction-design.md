# UI U4 — HUD interaction (click) — Design

## Goal (Phase 4, UI pillar — slice 4)

Make the HUD **clickable**. A reflected `UiButton` component defines a rectangular hit
region; a new stateful `UiSystem` reads a per-tick pointer state, detects a click
(press + release inside the same button), and emits a named `ScriptEvent` — delivered
to scripts via `on_event(name, payload)`, exactly like the physics `'contact'` bridge.

This introduces the **first runtime input seam** (mouse input never reaches the World
today) and the **first stateful `UiSystem`**. The engine stays windowing-agnostic: the
pointer state is supplied by an injected provider that only `main.cpp` fills from GLFW.

**Scope (YAGNI):** click only (press+release inside); one primary button; first-hit on
overlap; non-visual hit region. No hover/press/release events, no visual feedback, no
keyboard/focus/gamepad — all follow-ups.

Base branch: `main` (U1–U3 merged).

## Design

### `engine/ui/pointer.hpp` — neutral input seam

```cpp
#pragma once

namespace mosaic::engine::ui
{
// Per-tick pointer state in normalized screen space (top-left origin, matching UI
// rects). `down` = primary button currently pressed. Supplied by an injected provider;
// the engine never reads a window/mouse directly.
struct PointerState
{
    float x = 0.0f, y = 0.0f;
    bool  down = false;
};
} // namespace mosaic::engine::ui
```

### `engine/ui/ui_button.hpp` — reflected component

```cpp
#pragma once

#include <string>

#include "foundation/math/vec2.hpp"

namespace mosaic::engine::ui
{
using mosaic::foundation::math::Vec2;

// A non-visual rectangular hit region (reuses resolve_rect for placement). On a click
// (pointer press + release inside), UiSystem emits a ScriptEvent named `event` with
// payload { entity, action:"click" }. Overlay a UiPanel/UiImage/UiText for the visual.
struct UiButton
{
    int         anchor = 0;                 // ui::Anchor (0 = TopLeft; clamped)
    Vec2        pos    = Vec2{0.0f, 0.0f};  // normalized offset from the anchor
    Vec2        size   = Vec2{0.1f, 0.1f};  // fraction of the screen
    std::string event  = "ui_click";        // ScriptEvent name emitted on click
};

template <class V>
void reflect(V& visitor, UiButton& b)
{
    visitor.member("anchor", b.anchor);
    visitor.member("pos", b.pos);
    visitor.member("size", b.size);
    visitor.member("event", b.event);
}
} // namespace mosaic::engine::ui
```

Registered `"UiButton"` in `builtin_components.hpp` (serializes, Inspector-editable,
script-mutable).

### `engine/ui/ui_system.hpp` — stateful `UiSystem`

An `ecs::System` (`engine/ecs/system.hpp`: `virtual void update(World&, float dt)`) with
an injected pointer provider:

```cpp
class UiSystem : public mosaic::engine::ecs::System
{
public:
    explicit UiSystem(std::function<PointerState()> pointer)
        : pointer_(std::move(pointer)) {}

    void update(mosaic::engine::World& world, float /*dt*/) override;

private:
    std::function<PointerState()> pointer_;
    bool                          was_down_ = false;
    mosaic::foundation::EntityID  pressed_ = {}; // button under the pointer at press-down
};
```

`update` logic each tick:

1. `const PointerState p = pointer_ ? pointer_() : PointerState{};`
2. **Helper** `hit(EntityID, UiButton&) -> bool`: `resolve_rect(to_anchor(anchor), pos,
   size)` then `p.x ∈ [rect.x, rect.x+rect.w] && p.y ∈ [rect.y, rect.y+rect.h]`.
3. **Down edge** (`!was_down_ && p.down`): set `pressed_` = the first `UiButton` whose
   rect contains `p` (via `world.for_each<UiButton>`), or nil if none.
4. **Up edge** (`was_down_ && !p.down`): if `pressed_` is valid, is still a live entity
   with a `UiButton`, and `p` is inside its rect → emit
   `world.events().emit(ScriptEvent{button.event, payload})` where
   `payload = { "entity": uint64(id), "action": "click" }` (a `ser::Table`, mirroring the
   contact bridge). Then reset `pressed_ = {}`.
5. `was_down_ = p.down;`

Deterministic (no rendering, no `is_*` polling); the click `ScriptEvent` rides the
deferred bus, so `on_event` fires one tick later. `UiSystem` lives in `engine/ui/` and is
backend-free (no window/GLFW).

### Wiring — `setup_runtime` + `main.cpp`

- **`setup_runtime`** gains an injected, defaulted pointer provider parameter
  (`std::function<mosaic::engine::ui::PointerState()> pointer = {}`), mirroring the
  defaulted `audio_backend`. It adds `world.add_system<ui::UiSystem>(pointer)`. A default
  (empty) provider means `PointerState{down=false}` → nothing ever clicks (headless +
  editor safe).
- **`main.cpp`** (windowed `mosaic run`) supplies the provider from GLFW:
  `glfwGetCursorPos(window, &mx, &my)` + `glfwGetMouseButton(window,
  GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS`, normalized by the framebuffer size
  (`x = mx/fbw`, `y = my/fbh`, clamped to `[0,1]`). This is the only place a mouse is
  read; `engine/`/`studio/` stay windowing-agnostic.

### Demo — clickable HUD panel (no new entity, no script edits)

- Attach a `UiButton` to the **existing camera entity** (like `UiPanel`), sized to
  overlap the demo HUD bar, `event = "hud_click"`.
- In `setup_runtime`, add a **log subscriber**: `world.events().subscribe<ScriptEvent>(
  [](const ScriptEvent& e){ if (e.name == "hud_click") log::info("ui", "HUD clicked"); })`
  — so clicking the bar prints to the console. Least-coupled observable demo (no scripted
  `on_event` entity, no default-scene count change, no script-test churn).

## Testing (headless)

- **`UiButton` reflection** — round-trip (incl. `std::string event`) + registration
  under `"UiButton"`.
- **`UiSystem` click semantics** — a **fake pointer provider** (a lambda returning a
  scripted sequence of `PointerState`s) drives a `World` with one `UiButton`; a **sync
  `subscribe<ScriptEvent>`** captures emissions across `world.tick()` calls:
  - press inside → release inside → exactly one `ScriptEvent` with the button's `event`
    name and `payload.entity` == the button id.
  - press inside → release **outside** → no event.
  - press **outside** → release inside → no event.
  - pointer never down → no event.
- **Hit-test** — a point inside vs. outside a resolved rect (edge inclusive on the
  low edge).
- **Default-scene counts** — unchanged (`UiButton` on the camera entity).

`UiSystem` wiring into `setup_runtime` is covered by the existing runtime tick test path;
the on-screen click is the maintainer's hardware check (headless has no pointer).

## Files

**New**
- `engine/ui/pointer.hpp` — `PointerState`.
- `engine/ui/ui_button.hpp` — reflected `UiButton`.
- `engine/ui/ui_system.hpp` — `UiSystem` (declaration + inline `update`, header-only like
  the other engine systems).
- `tests/unit/ui/ui_button_reflection_test.cpp`, `tests/unit/ui/ui_system_test.cpp`.

**Modified**
- `engine/reflection/builtin_components.hpp` — register `UiButton`.
- `app/run/demo_scene.hpp` — `setup_runtime` pointer-provider param + `UiSystem` add +
  the `"hud_click"` log subscriber; demo `UiButton` on the camera.
- `app/main.cpp` — GLFW pointer provider for the windowed run.
- `tests/CMakeLists.txt` — new test sources.

## Non-goals / follow-ups

Hover enter/exit + press/release events; visual feedback (hover/press tint — needs the
UISystem to touch rendering); keyboard/gamepad navigation + focus; text input fields;
pointer capture / drag; z-order-aware overlap resolution (v1 is first-hit); editor-mode
interaction (the editor injects its own provider).
