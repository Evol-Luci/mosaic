# UI U4 — HUD Interaction (Click) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the HUD clickable — a `UiButton` hit region + a stateful `UiSystem` that turns a press+release-inside into a named `ScriptEvent`.

**Architecture:** A neutral `PointerState` seam is filled by an injected provider (GLFW in `main.cpp` only). `UiSystem` (an `ecs::System`) reads the pointer each tick, hit-tests `UiButton` rects via `resolve_rect`, and emits `world.events().emit(ScriptEvent{button.event, {entity, action}})` on a click — delivered to `on_event` one tick later, like the physics `'contact'` bridge.

**Tech Stack:** C++20, Catch2, CMake, GLFW (input read only in `main.cpp`), reflection, the `#11` EventBus.

**Spec:** `docs/superpowers/specs/2026-09-08-ui-u4-interaction-design.md`

**Base branch:** `claude/ui-u4-interaction` (off `main` = U1–U3; `cmake --preset debug` configured).

## Global Constraints

- **Windowing wall:** no GLFW/window symbol in `engine/` or `studio/`. `UiSystem` and all UI headers are backend-free; the only mouse read is in `app/main.cpp`.
- **Build convention:** presets into `build/debug/`. Build: `cmake --build --preset debug [--target <t>]`. Test binary: `./build/debug/tests/mosaic_unit_tests "[tag]"` (Catch2 tag; comma = OR). Full suite: `ctest --preset debug`. App: `./build/debug/app/mosaic`. Do **not** use `cmake --build build` or `./build/app/...`. **Transient link failures:** the ~170 MB test binary intermittently fails at the LINK step under memory pressure (`ld` error, "Deleting file", `make Error 2`) with **no compiler error** — if a build fails, `grep -iE "error:|error C[0-9]|undefined reference"`; if empty, just re-run (up to ~3×). **After changing `demo_scene.hpp` / `main.cpp`, rebuild the `mosaic` target before any `mosaic run` check.**
- **Coordinate convention:** normalized screen space, origin top-left, x right, y down, `[0,1]`. Pointer and button rects are both normalized, so hit-testing needs no aspect.
- **Types:** `mosaic::foundation::EntityID` (aka `World::EntityID`); `EntityID{}` is null; `EntityID` has `operator==`/`!=` and `.value()` (uint64). `World::for_each<T>([](EntityID, T&){…})` iterates a component. `System::update(World&, float dt)` is the override.
- **Event delivery:** `world.events().emit(ScriptEvent{name, Value})`; sync `world.events().subscribe<ScriptEvent>(handler)` fires in-tick (tests use this); `ScriptSystem` uses a deferred subscriber, so `on_event` sees it one tick later.
- **Reflection field types:** `int`, `Vec2`, `std::string` are supported reflected members (`std::string` like `ScriptComponent.source`).

---

### Task 1: `PointerState` + `UiButton` reflected component

**Files:**
- Create: `engine/ui/pointer.hpp`
- Create: `engine/ui/ui_button.hpp`
- Create: `tests/unit/ui/ui_button_reflection_test.cpp`
- Modify: `engine/reflection/builtin_components.hpp` (register `UiButton`)
- Modify: `tests/CMakeLists.txt` (add the test source)

**Interfaces:**
- Produces:
  - `struct mosaic::engine::ui::PointerState { float x=0, y=0; bool down=false; };`
  - `struct mosaic::engine::ui::UiButton { int anchor=0; Vec2 pos; Vec2 size; std::string event="ui_click"; };` registered `"UiButton"`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/ui/ui_button_reflection_test.cpp`:

```cpp
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/ui/ui_button.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reference.hpp"
#include "foundation/serialization/reflect_value.hpp"

using Catch::Approx;
using mosaic::engine::ui::UiButton;
using mosaic::foundation::math::Vec2;

TEST_CASE("UiButton round-trips through reflection", "[ui][reflection]")
{
    UiButton b;
    b.anchor = 2; // TopRight
    b.pos    = Vec2{0.02f, 0.03f};
    b.size   = Vec2{0.25f, 0.06f};
    b.event  = "hud_click";

    const auto value = mosaic::foundation::serialization::to_value(b);
    UiButton back;
    mosaic::foundation::serialization::from_value(value, back);

    REQUIRE(back.anchor == 2);
    REQUIRE(back.pos.x == Approx(0.02f));
    REQUIRE(back.size.y == Approx(0.06f));
    REQUIRE(back.event == "hud_click");
}

TEST_CASE("UiButton is a registered built-in component", "[ui][reflection]")
{
    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    REQUIRE(types.has("UiButton"));
}
```

Add to `tests/CMakeLists.txt` (near the other `unit/ui/*` entries):

```cmake
  unit/ui/ui_button_reflection_test.cpp
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -20`
Expected: FAIL — `engine/ui/ui_button.hpp` not found / `types.has("UiButton")` false.

- [ ] **Step 3: Write the implementation**

Create `engine/ui/pointer.hpp`:

```cpp
#pragma once

namespace mosaic::engine::ui
{
// Per-tick pointer state in normalized screen space (top-left origin, matching UI
// rects). down = primary button pressed. Supplied by an injected provider; the engine
// never reads a window/mouse directly.
struct PointerState
{
    float x = 0.0f, y = 0.0f;
    bool  down = false;
};
} // namespace mosaic::engine::ui
```

Create `engine/ui/ui_button.hpp`:

```cpp
#pragma once

#include <string>

#include "foundation/math/vec2.hpp"

namespace mosaic::engine::ui
{
using mosaic::foundation::math::Vec2;

// A non-visual rectangular hit region (reuses resolve_rect). On a click (pointer press
// + release inside), UiSystem emits a ScriptEvent named `event` with payload
// { entity, action:"click" }. Overlay a UiPanel/UiImage/UiText for the visual.
struct UiButton
{
    int         anchor = 0;
    Vec2        pos    = Vec2{0.0f, 0.0f};
    Vec2        size   = Vec2{0.1f, 0.1f};
    std::string event  = "ui_click";
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

Register in `engine/reflection/builtin_components.hpp` — add the include near the other
UI includes and the registration after the `UiImage` line:

```cpp
#include "engine/ui/ui_button.hpp"
```
```cpp
    types.register_component<mosaic::engine::ui::UiButton>(registry, "UiButton");
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -5 && ./build/debug/tests/mosaic_unit_tests "[ui][reflection]" 2>&1 | tail -6`
Expected: `[ui][reflection]` cases (incl. the U1–U3 ones) PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/ui/pointer.hpp engine/ui/ui_button.hpp engine/reflection/builtin_components.hpp \
        tests/unit/ui/ui_button_reflection_test.cpp tests/CMakeLists.txt
git commit -m "feat(ui): PointerState seam + reflected UiButton component"
```

---

### Task 2: `UiSystem` — click detection

**Files:**
- Create: `engine/ui/ui_system.hpp`
- Create: `tests/unit/ui/ui_system_test.cpp`
- Modify: `tests/CMakeLists.txt` (add the test source)

**Interfaces:**
- Consumes: `PointerState`, `UiButton` (Task 1); `resolve_rect`/`to_anchor` (U1); `World`, `ScriptEvent`, the EventBus.
- Produces: `class mosaic::engine::ui::UiSystem : public ecs::System` constructed with `std::function<PointerState()>`; emits `ScriptEvent{button.event, {entity, action}}` on click.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/ui/ui_system_test.cpp`:

```cpp
#include <functional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/script/script_event.hpp"
#include "engine/ui/pointer.hpp"
#include "engine/ui/ui_button.hpp"
#include "engine/ui/ui_system.hpp"
#include "engine/world/world.hpp"

using mosaic::engine::World;
using mosaic::engine::script::ScriptEvent;
using mosaic::engine::ui::PointerState;
using mosaic::engine::ui::UiButton;
using mosaic::engine::ui::UiSystem;
using mosaic::foundation::math::Vec2;

namespace
{
// A World with one UiButton at TopLeft {0,0}..{0.2,0.2}, a UiSystem driven by a
// mutable pointer, and a sync capture of emitted ScriptEvents.
struct Harness
{
    World world;
    PointerState ptr;
    std::vector<ScriptEvent> events;

    Harness()
    {
        auto e = world.create_entity();
        UiButton b;
        b.anchor = 0; b.pos = Vec2{0.0f, 0.0f}; b.size = Vec2{0.2f, 0.2f};
        b.event = "test_click";
        world.add_component<UiButton>(e, b);
        world.add_system<UiSystem>([this]() { return ptr; });
        world.events().subscribe<ScriptEvent>(
            [this](const ScriptEvent& ev) { events.push_back(ev); });
    }
    void tick() { world.tick(0.016f); }
};
} // namespace

TEST_CASE("UiSystem emits a click on press+release inside the same button", "[ui][input]")
{
    Harness h;
    h.ptr = {0.05f, 0.05f, true};  h.tick(); // press inside (down edge)
    h.ptr = {0.05f, 0.05f, false}; h.tick(); // release inside (up edge) -> click
    REQUIRE(h.events.size() == 1);
    REQUIRE(h.events[0].name == "test_click");
}

TEST_CASE("UiSystem does not click when release is outside the button", "[ui][input]")
{
    Harness h;
    h.ptr = {0.05f, 0.05f, true};  h.tick(); // press inside
    h.ptr = {0.9f, 0.9f, false};   h.tick(); // release outside -> no click
    REQUIRE(h.events.empty());
}

TEST_CASE("UiSystem does not click when press starts outside the button", "[ui][input]")
{
    Harness h;
    h.ptr = {0.9f, 0.9f, true};    h.tick(); // press outside
    h.ptr = {0.05f, 0.05f, false}; h.tick(); // release inside -> no click
    REQUIRE(h.events.empty());
}

TEST_CASE("UiSystem emits nothing while the pointer stays up", "[ui][input]")
{
    Harness h;
    h.ptr = {0.05f, 0.05f, false}; h.tick();
    h.ptr = {0.05f, 0.05f, false}; h.tick();
    REQUIRE(h.events.empty());
}
```

Add to `tests/CMakeLists.txt`:

```cmake
  unit/ui/ui_system_test.cpp
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -20`
Expected: FAIL — `engine/ui/ui_system.hpp` not found.

- [ ] **Step 3: Write the implementation**

Create `engine/ui/ui_system.hpp`:

```cpp
#pragma once

#include <functional>
#include <utility>

#include "engine/ecs/system.hpp"
#include "engine/script/script_event.hpp"
#include "engine/ui/pointer.hpp"
#include "engine/ui/ui_button.hpp"
#include "engine/ui/ui_rect.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"
#include "foundation/serialization/value.hpp"

namespace mosaic::engine::ui
{
// Clamp an int to a ui::Anchor (out of range -> TopLeft). Mirrors render's to_anchor.
inline Anchor button_anchor(int a)
{
    return (a >= 0 && a <= 8) ? static_cast<Anchor>(a) : Anchor::TopLeft;
}

// First stateful UI system: reads an injected pointer each tick and emits a named
// ScriptEvent when the pointer presses and releases inside the same UiButton.
class UiSystem : public mosaic::engine::ecs::System
{
public:
    explicit UiSystem(std::function<PointerState()> pointer)
        : pointer_(std::move(pointer)) {}

    void update(mosaic::engine::World& world, float /*dt*/) override
    {
        const PointerState p = pointer_ ? pointer_() : PointerState{};

        if (p.down && !was_down_)
        {
            // Press: record the first button under the pointer.
            pressed_ = mosaic::foundation::EntityID{};
            world.for_each<UiButton>(
                [&](mosaic::foundation::EntityID e, UiButton& b)
                {
                    if (pressed_ != mosaic::foundation::EntityID{}) { return; }
                    if (hit(p, b)) { pressed_ = e; }
                });
        }
        else if (!p.down && was_down_)
        {
            // Release: click if the pointer is still inside the pressed button.
            if (pressed_ != mosaic::foundation::EntityID{})
            {
                world.for_each<UiButton>(
                    [&](mosaic::foundation::EntityID e, UiButton& b)
                    {
                        if (e != pressed_ || !hit(p, b)) { return; }
                        namespace ser = mosaic::foundation::serialization;
                        ser::Table payload;
                        payload["entity"] = ser::Value(static_cast<std::uint64_t>(e.value()));
                        payload["action"] = ser::Value(std::string("click"));
                        world.events().emit(mosaic::engine::script::ScriptEvent{
                            b.event, ser::Value(std::move(payload))});
                    });
            }
            pressed_ = mosaic::foundation::EntityID{};
        }
        was_down_ = p.down;
    }

private:
    static bool hit(const PointerState& p, UiButton& b)
    {
        const Rect r = resolve_rect(button_anchor(b.anchor), b.pos, b.size);
        return p.x >= r.x && p.x <= r.x + r.w && p.y >= r.y && p.y <= r.y + r.h;
    }

    std::function<PointerState()> pointer_;
    bool                         was_down_ = false;
    mosaic::foundation::EntityID pressed_  = {};
};
} // namespace mosaic::engine::ui
```

> If `World::create_entity`/`add_component`/`add_system`/`for_each`/`events()`/`tick`
> signatures differ, mirror an existing engine system test (e.g.
> `tests/unit/script/script_events_test.cpp`) — it exercises the same World + ScriptEvent
> path. `ser::Table`/`ser::Value` usage mirrors the contact bridge in `demo_scene.hpp`.

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -5 && ./build/debug/tests/mosaic_unit_tests "[ui][input]" 2>&1 | tail -8`
Expected: the 4 `[ui][input]` cases PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/ui/ui_system.hpp tests/unit/ui/ui_system_test.cpp tests/CMakeLists.txt
git commit -m "feat(ui): UiSystem click detection (press+release inside -> ScriptEvent)"
```

---

### Task 3: Wire the pointer into the runtime + demo

**Files:**
- Modify: `app/run/demo_scene.hpp` (`setup_runtime` pointer param + `UiSystem` add + `"hud_click"` log subscriber; demo `UiButton` on the camera)
- Modify: `app/main.cpp` (GLFW pointer provider)

**Interfaces:**
- Consumes: `UiSystem` (Task 2), `UiButton` (Task 1), `PointerState`.

- [ ] **Step 1: Add the pointer param + UiSystem + log subscriber to `setup_runtime`**

In `app/run/demo_scene.hpp`, add the includes near the other UI includes:

```cpp
#include "engine/ui/pointer.hpp"
#include "engine/ui/ui_button.hpp"
#include "engine/ui/ui_system.hpp"
```

Add a **defaulted** pointer-provider parameter to `setup_runtime` (after the
`audio_backend` parameter, keeping it last so existing callers are unaffected):

```cpp
    std::unique_ptr<mosaic::engine::audio::AudioBackend>    audio_backend = nullptr,
    std::function<mosaic::engine::ui::PointerState()>       pointer = {})
```

Add the `UiSystem` right after the `PhysicsSystem` `add_system` call:

```cpp
    // UI interaction: reads the injected pointer, turns clicks into ScriptEvents.
    world.add_system<mosaic::engine::ui::UiSystem>(std::move(pointer));
```

Add a demo log subscriber near the contact-bridge `subscribe` (so a `"hud_click"` prints):

```cpp
    // Demo: log when the HUD button is clicked (proves the U4 click path end to end).
    world.events().subscribe<mosaic::engine::script::ScriptEvent>(
        [](const mosaic::engine::script::ScriptEvent& e)
        {
            if (e.name == "hud_click")
            {
                mosaic::foundation::log::info("ui", "HUD button clicked");
            }
        });
```

> Confirm `mosaic::foundation::log::info` is the logging call used elsewhere in this file
> (the contact bridge / audio logs use it). If the header isn't already included, it is —
> `demo_scene.hpp` already logs.

- [ ] **Step 2: Add the demo `UiButton` on the camera**

In `build_default_scene`, next to the `UiPanel`/`UiImage`/`ScreenEffect` attaches on the
**existing camera entity**, add a `UiButton` overlapping the HUD bar (same rect as the
demo `UiPanel`, which is TopLeft `pos {0.02,0.02}` `size {0.25,0.06}`):

```cpp
    world.add_component<mosaic::engine::ui::UiButton>(camera,
        mosaic::engine::ui::UiButton{
            static_cast<int>(mosaic::engine::ui::Anchor::TopLeft),
            mosaic::foundation::math::Vec2{0.02f, 0.02f},
            mosaic::foundation::math::Vec2{0.25f, 0.06f},
            "hud_click"});
```

> Confirm the `UiButton` aggregate-init order matches `{anchor, pos, size, event}`.
> Attaches to `camera` (no new entity → default-scene counts unchanged).

- [ ] **Step 3: Supply the GLFW pointer provider in `main.cpp`**

In `app/main.cpp`, add the include:

```cpp
#include "engine/ui/pointer.hpp"
```

`setup_runtime` is called (~line 177) **before** the window is created (~line 201), and
`glfw_window` is a local declared earlier (~line 157). Capture it **by reference** so the
provider reads the live handle at tick time (null in a headless run → no clicks). Pass the
provider as the new last argument to the existing `setup_runtime(...)` call:

```cpp
        [&glfw_window]() -> mosaic::engine::ui::PointerState
        {
            if (glfw_window == nullptr) { return {}; }
            int ww = 0, wh = 0;
            glfwGetWindowSize(glfw_window, &ww, &wh);
            if (ww <= 0 || wh <= 0) { return {}; }
            double mx = 0.0, my = 0.0;
            glfwGetCursorPos(glfw_window, &mx, &my);
            mosaic::engine::ui::PointerState p;
            p.x = std::clamp(static_cast<float>(mx / ww), 0.0f, 1.0f);
            p.y = std::clamp(static_cast<float>(my / wh), 0.0f, 1.0f);
            p.down = glfwGetMouseButton(glfw_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            return p;
        }
```

> Add `#include <algorithm>` for `std::clamp` if not already present. `<GLFW/glfw3.h>` is
> already included in `main.cpp` (it creates the window). The provider is the final
> argument to the single `setup_runtime(...)` call; all other args stay as they are.

- [ ] **Step 4: Build + full suite + headless run**

```bash
cmake --build --preset debug 2>&1 | tail -5
ctest --preset debug 2>&1 | tail -4
./build/debug/app/mosaic run --window --ticks 3 2>&1 | tail -5; echo "exit: $?"
```
Expected: all tests PASS (default-scene counts unchanged — `UiButton` on the camera);
`mosaic run --window` exits 0 with no validation errors (headless click never fires, so
no `"HUD button clicked"` log — that's the maintainer's on-hardware check).

> If a default-scene test fails on an entity count, re-check the `UiButton` is on `camera`,
> not a new `create_entity()`.

- [ ] **Step 5: Commit**

```bash
git add app/run/demo_scene.hpp app/main.cpp
git commit -m "feat(ui): wire GLFW pointer into UiSystem; demo clickable HUD button"
```

---

### Task 4: Verify full suite + update the build handoff

**Files:**
- Modify: `docs/project/next-steps.md`

- [ ] **Step 1: Final verification**

```bash
cmake --build --preset debug 2>&1 | tail -3
ctest --preset debug 2>&1 | tail -4
./build/debug/app/mosaic run --window --ticks 3 2>&1 | tail -5; echo "exit: $?"
```
Expected: full suite passes; `run --window` exits 0, no validation errors.

- [ ] **Step 2: Update the handoff**

In `docs/project/next-steps.md`:
1. Update the **Last updated** line (`2026-09-08`) with a "UI — U4 (HUD interaction/click) DONE" summary: a neutral `PointerState` input seam + a reflected `UiButton` hit region + the first stateful `UiSystem` (injected pointer provider) that turns press+release-inside into a named `ScriptEvent` (`on_event` one tick later); GLFW pointer wired in `main.cpp` only (engine stays windowing-agnostic); demo clickable HUD bar + `"hud_click"` log subscriber. Headless-tested (reflection, click semantics via a fake provider); on-screen click is the maintainer's check.
2. In the UI-decomposition line, mark **U4 done** and point at **U5 (layout/hierarchy + world-space)** as next; link the U4 spec + this plan.

- [ ] **Step 3: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "docs: mark UI U4 (HUD interaction) done; U5 next"
```

---

## Self-Review

**Spec coverage:**
- `pointer.hpp` `PointerState` seam → Task 1. ✓
- `ui_button.hpp` reflected `UiButton` + `"UiButton"` registration → Task 1. ✓
- `ui_system.hpp` stateful `UiSystem` (injected provider, press/release-inside click, ScriptEvent emit) → Task 2. ✓
- `setup_runtime` pointer param + `UiSystem` add + `"hud_click"` log subscriber → Task 3. ✓
- `main.cpp` GLFW provider (window read only here) → Task 3. ✓
- Demo `UiButton` on the camera → Task 3. ✓
- Tests: reflection+registration (Task 1); click semantics — click, release-outside, press-outside, never-down (Task 2); default-scene counts (Task 3). ✓

**Placeholder scan:** no TBD/TODO; every code step has real code. The `main.cpp` provider captures `&glfw_window` (declared before `setup_runtime`, assigned after) and reads it lazily — correct for the call-order.

**Type consistency:** `PointerState{x,y,down}` (Tasks 1,2,3); `UiButton{anchor,pos,size,event}` (Tasks 1,2,3); `UiSystem(std::function<PointerState()>)` (Tasks 2,3); `resolve_rect(Anchor,Vec2,Vec2)` + `to_anchor`-style clamp (Task 2); `ScriptEvent{name,Value}` emit + sync `subscribe<ScriptEvent>` (Tasks 2,3); `EntityID{}`/`!=`/`.value()` (Task 2). Registration name `"UiButton"` + event name `"hud_click"` consistent (Tasks 1,3).
