# UI U1 — Screen-Space HUD Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add anchored, resolution-independent colored HUD panels that composite over the finished 3D scene, as the first slice of the Phase-4 UI pillar.

**Architecture:** A pure neutral layout model (`engine/ui/ui_rect.hpp`: `Anchor` + `resolve_rect`) and a reflected `UiPanel` component (`engine/ui/ui_panel.hpp`). `RenderSystem` reads panels, resolves each to a normalized screen `Rect`, sorts by `order`, and submits them through a new default-no-op `draw_ui` seam on `RenderDevice` — as the topmost pass, after the screen-effect overlay. A Vulkan screen-space quad pipeline (modeled on the existing screen-effect pipeline) draws the alpha-blended quads. GPU visual is maintainer-verified; the sandbox verifies headless logic + build + `mosaic run --window` exit 0.

**Tech Stack:** C++20, Catch2 (FetchContent), CMake, Vulkan (module-walled in `modules/render_vulkan`), reflection via `foundation/reflection` + `engine/reflection`.

**Spec:** `docs/superpowers/specs/2026-09-08-ui-u1-screen-hud-design.md`

## Global Constraints

- **Neutral/module wall:** no Vulkan (`Vk*`) or any backend type may appear above `modules/`. `engine/` and `app/` see only the neutral `RenderDevice` interface. All new engine headers are backend-free.
- **Header-only engine units:** `engine/ui/*.hpp` are header-only with `inline` free functions (mirrors `engine/vfx/ribbon.hpp::build_ribbon`); no new engine `.cpp`, so no `engine/CMakeLists.txt` change.
- **Coordinate convention:** normalized screen space, origin **top-left**, x right, y down, all in `[0,1]`.
- **Reflection field types:** the reflection surface has no enum type — `anchor` and `order` are `int` on the component; the render path casts `anchor` to `ui::Anchor`, clamping out-of-range to `TopLeft`.
- **Namespaces:** neutral UI types live in `namespace mosaic::engine::ui`. `Vec2`/`Vec3` come from `foundation/math`.
- **GPU verification caveat:** the on-screen HUD is the maintainer's hardware check; automated verification is headless tests + `cmake --build` + `mosaic run --window --ticks N` exiting 0 with no Vulkan validation errors.
- **Test executable:** all unit tests compile into the single `mosaic_unit_tests` target; add new test files to `tests/CMakeLists.txt`.

---

### Task 1: Neutral layout model — `Anchor`, `Rect`, `resolve_rect`

**Files:**
- Create: `engine/ui/ui_rect.hpp`
- Create: `tests/unit/ui/ui_rect_test.cpp`
- Modify: `tests/CMakeLists.txt` (add the new test source)

**Interfaces:**
- Produces:
  - `enum class mosaic::engine::ui::Anchor { TopLeft=0, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight };`
  - `struct mosaic::engine::ui::Rect { float x=0, y=0, w=0, h=0; };`
  - `inline Rect resolve_rect(Anchor anchor, Vec2 pos, Vec2 size);`

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/ui/ui_rect_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/ui/ui_rect.hpp"
#include "foundation/math/vec2.hpp"

using mosaic::engine::ui::Anchor;
using mosaic::engine::ui::Rect;
using mosaic::engine::ui::resolve_rect;
using mosaic::foundation::math::Vec2;
using Catch::Approx;

namespace
{
// pos is a normalized offset from the anchor toward the screen interior;
// size is a fraction of the screen (w, h).
constexpr Vec2 kPos{0.10f, 0.20f};
constexpr Vec2 kSize{0.30f, 0.15f};
} // namespace

TEST_CASE("resolve_rect anchors to the top-left corner", "[ui][layout]")
{
    const Rect r = resolve_rect(Anchor::TopLeft, kPos, kSize);
    REQUIRE(r.x == Approx(0.10f)); // offset in from the left
    REQUIRE(r.y == Approx(0.20f)); // offset down from the top
    REQUIRE(r.w == Approx(0.30f));
    REQUIRE(r.h == Approx(0.15f));
}

TEST_CASE("resolve_rect anchors to the bottom-right corner", "[ui][layout]")
{
    const Rect r = resolve_rect(Anchor::BottomRight, kPos, kSize);
    // pos pushes toward the interior: in from the right / up from the bottom.
    REQUIRE(r.x == Approx(1.0f - 0.30f - 0.10f)); // 0.60
    REQUIRE(r.y == Approx(1.0f - 0.15f - 0.20f)); // 0.65
    REQUIRE(r.w == Approx(0.30f));
    REQUIRE(r.h == Approx(0.15f));
}

TEST_CASE("resolve_rect centers on the middle", "[ui][layout]")
{
    const Rect r = resolve_rect(Anchor::Center, Vec2{0.0f, 0.0f}, kSize);
    REQUIRE(r.x == Approx(0.5f - 0.30f / 2.0f)); // 0.35
    REQUIRE(r.y == Approx(0.5f - 0.15f / 2.0f)); // 0.425
}

TEST_CASE("resolve_rect edge anchors mix an edge and a centered axis", "[ui][layout]")
{
    // Top: horizontally centered, offset down from the top edge.
    const Rect top = resolve_rect(Anchor::Top, Vec2{0.0f, 0.05f}, kSize);
    REQUIRE(top.x == Approx(0.5f - 0.30f / 2.0f)); // centered X
    REQUIRE(top.y == Approx(0.05f));               // in from top

    // Right: vertically centered, offset in from the right edge.
    const Rect right = resolve_rect(Anchor::Right, Vec2{0.05f, 0.0f}, kSize);
    REQUIRE(right.x == Approx(1.0f - 0.30f - 0.05f)); // in from right
    REQUIRE(right.y == Approx(0.5f - 0.15f / 2.0f));  // centered Y
}
```

Add to `tests/CMakeLists.txt` (in the `add_executable(mosaic_unit_tests ...)` source list, alphabetically near other unit tests):

```cmake
  unit/ui/ui_rect_test.cpp
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build --target mosaic_unit_tests 2>&1 | tail -20`
Expected: FAIL — `engine/ui/ui_rect.hpp` not found (no such file).

- [ ] **Step 3: Write the implementation**

Create `engine/ui/ui_rect.hpp`:

```cpp
#pragma once

#include "foundation/math/vec2.hpp"

namespace mosaic::engine::ui
{
using mosaic::foundation::math::Vec2;

// Which screen reference point an element is anchored to. Origin is top-left,
// x right, y down — matching normalized screen space [0,1].
enum class Anchor
{
    TopLeft = 0, Top, TopRight,
    Left,        Center, Right,
    BottomLeft,  Bottom, BottomRight
};

// A resolved rectangle in normalized screen space: origin top-left, x/y/w/h in [0,1].
struct Rect
{
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
};

namespace detail
{
// One axis: reference band 0 = Left/Top (offset in from the low edge),
// 1 = Center (centered, pos nudges), 2 = Right/Bottom (offset in from the high edge).
inline float resolve_axis(int band, float pos, float size)
{
    if (band == 0) { return pos; }                     // flush to low edge, offset inward
    if (band == 2) { return 1.0f - size - pos; }       // flush to high edge, offset inward
    return 0.5f - size * 0.5f + pos;                   // centered
}
} // namespace detail

// Resolve an anchored element to a normalized screen Rect.
//   size : element size as a fraction of the screen (w = size.x, h = size.y).
//   pos  : normalized offset FROM the anchor point toward the screen interior
//          (pos {0,0} places the element flush against its anchor).
inline Rect resolve_rect(Anchor anchor, Vec2 pos, Vec2 size)
{
    const int a = static_cast<int>(anchor); // 0..8, row-major: col = horizontal band, row = vertical band
    const int col = a % 3;                  // 0 = Left, 1 = Center, 2 = Right
    const int row = a / 3;                  // 0 = Top,  1 = Center, 2 = Bottom
    Rect r;
    r.x = detail::resolve_axis(col, pos.x, size.x);
    r.y = detail::resolve_axis(row, pos.y, size.y);
    r.w = size.x;
    r.h = size.y;
    return r;
}
} // namespace mosaic::engine::ui
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build --target mosaic_unit_tests 2>&1 | tail -5 && ctest --test-dir build -R "ui" --output-on-failure 2>&1 | tail -20`
Expected: the `[ui][layout]` cases PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/ui/ui_rect.hpp tests/unit/ui/ui_rect_test.cpp tests/CMakeLists.txt
git commit -m "feat(ui): neutral Anchor/Rect layout model (resolve_rect)"
```

---

### Task 2: `UiPanel` reflected component + registration

**Files:**
- Create: `engine/ui/ui_panel.hpp`
- Create: `tests/unit/ui/ui_panel_reflection_test.cpp`
- Modify: `engine/reflection/builtin_components.hpp` (register `UiPanel`)
- Modify: `tests/CMakeLists.txt` (add the new test source)

**Interfaces:**
- Consumes: `mosaic::engine::ui` from Task 1 (namespace + `Vec2`).
- Produces:
  - `struct mosaic::engine::ui::UiPanel { int anchor=0; Vec2 pos{0,0}; Vec2 size{0.2f,0.1f}; Vec3 color{1,1,1}; float alpha=1.0f; int order=0; };`
  - Registered under the reflection name `"UiPanel"`.
  - `template <class V> void reflect(V&, UiPanel&);`

- [ ] **Step 1: Write the failing test**

Create `tests/unit/ui/ui_panel_reflection_test.cpp` (modeled verbatim on `tests/unit/vfx/decal_test.cpp` — same includes/APIs):

```cpp
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/ui/ui_panel.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reference.hpp"
#include "foundation/serialization/reflect_value.hpp"

using Catch::Approx;
using mosaic::engine::ui::UiPanel;
using mosaic::foundation::math::Vec2;
using mosaic::foundation::math::Vec3;

TEST_CASE("UiPanel round-trips through reflection", "[ui][reflection]")
{
    UiPanel p;
    p.anchor = 2;                 // TopRight
    p.pos    = Vec2{0.03f, 0.04f};
    p.size   = Vec2{0.25f, 0.06f};
    p.color  = Vec3{0.1f, 0.2f, 0.3f};
    p.alpha  = 0.6f;
    p.order  = 5;

    const auto value = mosaic::foundation::serialization::to_value(p);
    UiPanel back;
    mosaic::foundation::serialization::from_value(value, back);

    REQUIRE(back.anchor == 2);
    REQUIRE(back.pos.x == Approx(0.03f));
    REQUIRE(back.size.y == Approx(0.06f));
    REQUIRE(back.color.z == Approx(0.3f));
    REQUIRE(back.alpha == Approx(0.6f));
    REQUIRE(back.order == 5);
}

TEST_CASE("UiPanel is a registered built-in component", "[ui][reflection]")
{
    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    REQUIRE(types.has("UiPanel"));
}
```

Add to `tests/CMakeLists.txt`:

```cmake
  unit/ui/ui_panel_reflection_test.cpp
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --target mosaic_unit_tests 2>&1 | tail -20`
Expected: FAIL — `engine/ui/ui_panel.hpp` not found / `find("UiPanel")` returns null.

- [ ] **Step 3: Write the implementation**

Create `engine/ui/ui_panel.hpp`:

```cpp
#pragma once

#include "foundation/math/vec2.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::engine::ui
{
using mosaic::foundation::math::Vec2;
using mosaic::foundation::math::Vec3;

// A screen-space HUD panel: an anchored, alpha-blended colored rectangle.
// anchor is an int cast to ui::Anchor (0 = TopLeft); the render path clamps
// out-of-range values to TopLeft. pos is a normalized offset from the anchor
// toward the interior; size is a fraction of the screen. order stacks panels
// (higher draws on top). A normal component: scripts can animate alpha/pos.
struct UiPanel
{
    int   anchor = 0;
    Vec2  pos    = Vec2{0.0f, 0.0f};
    Vec2  size   = Vec2{0.2f, 0.1f};
    Vec3  color  = Vec3{1.0f, 1.0f, 1.0f};
    float alpha  = 1.0f;
    int   order  = 0;
};

template <class V>
void reflect(V& visitor, UiPanel& p)
{
    visitor.member("anchor", p.anchor);
    visitor.member("pos", p.pos);
    visitor.member("size", p.size);
    visitor.member("color", p.color);
    visitor.member("alpha", p.alpha);
    visitor.member("order", p.order);
}
} // namespace mosaic::engine::ui
```

> If `Vec2` has no `reflect()` overload yet (check `foundation/math/vec2.hpp`; `Vec3` is reflected and used by `ScreenEffect`), add a `reflect(V&, Vec2&)` in `vec2.hpp` mirroring the `Vec3` one (members `x`, `y`) — this is the minimal enabling change and belongs in this task.

Register it in `engine/reflection/builtin_components.hpp` — add the include near the other component includes and the registration line after the `ScreenEffect` line:

```cpp
#include "engine/ui/ui_panel.hpp"
```
```cpp
    types.register_component<mosaic::engine::ui::UiPanel>(registry, "UiPanel");
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build --target mosaic_unit_tests 2>&1 | tail -5 && ctest --test-dir build -R "ui" --output-on-failure 2>&1 | tail -20`
Expected: `[ui][reflection]` cases PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/ui/ui_panel.hpp engine/reflection/builtin_components.hpp \
        tests/unit/ui/ui_panel_reflection_test.cpp tests/CMakeLists.txt foundation/math/vec2.hpp
git commit -m "feat(ui): reflected UiPanel component, registered as \"UiPanel\""
```

---

### Task 3: `draw_ui` render seam + RenderSystem HUD pass

**Files:**
- Modify: `engine/render/render_device.hpp` (add `UiQuad` + `draw_ui`)
- Modify: `engine/render/render_system.hpp` (final HUD pass)
- Modify: `tests/unit/render/render_system_test.cpp` (FakeRenderDevice capture + tests)

**Interfaces:**
- Consumes: `ui::Rect`/`resolve_rect` (Task 1), `ui::UiPanel` (Task 2).
- Produces:
  - `struct mosaic::engine::render::UiQuad { mosaic::engine::ui::Rect rect; mosaic::foundation::math::Vec3 color; float alpha=1.0f; };`
  - `virtual void RenderDevice::draw_ui(const std::vector<UiQuad>&) {}` (default no-op).
  - `RenderSystem::render` emits panels via `draw_ui` as the final pass.

- [ ] **Step 1: Write the failing tests**

In `tests/unit/render/render_system_test.cpp`, add to `FakeRenderDevice` (next to `screen_effect_draws`):

```cpp
    std::vector<std::vector<mosaic::engine::render::UiQuad>> ui_draws;
    void draw_ui(const std::vector<mosaic::engine::render::UiQuad>& quads) override
    {
        ui_draws.push_back(quads);
    }
```

Add includes near the others:

```cpp
#include "engine/ui/ui_panel.hpp"
#include "engine/ui/ui_rect.hpp"
```

Add these test cases (a camera is required for `render` to run — reuse `add_camera`):

```cpp
TEST_CASE("RenderSystem emits a UiQuad per visible UiPanel, order-sorted", "[render][ui]")
{
    using mosaic::engine::ui::Anchor;
    using mosaic::engine::ui::UiPanel;
    AssetRegistry registry;
    World world;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 5});

    auto back = world.create_entity();
    UiPanel bp; bp.anchor = static_cast<int>(Anchor::TopLeft);
    bp.pos = {0.1f, 0.1f}; bp.size = {0.2f, 0.2f}; bp.order = 0;
    world.add_component<UiPanel>(back, bp);

    auto front = world.create_entity();
    UiPanel fp; fp.anchor = static_cast<int>(Anchor::TopLeft);
    fp.pos = {0.3f, 0.3f}; fp.size = {0.1f, 0.1f}; fp.order = 10;
    world.add_component<UiPanel>(front, fp);

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.ui_draws.size() == 1);
    const auto& quads = device.ui_draws.front();
    REQUIRE(quads.size() == 2);
    // order-sorted: order 0 first, order 10 last.
    REQUIRE(quads[0].rect.x == Approx(0.1f));  // the back panel
    REQUIRE(quads[1].rect.x == Approx(0.3f));  // the front panel
    REQUIRE(quads[1].rect.w == Approx(0.1f));
}

TEST_CASE("RenderSystem skips fully transparent UiPanels", "[render][ui]")
{
    using mosaic::engine::ui::UiPanel;
    AssetRegistry registry;
    World world;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 5});

    auto e = world.create_entity();
    UiPanel p; p.alpha = 0.0f;
    world.add_component<UiPanel>(e, p);

    FakeRenderDevice device;
    rs.render(world, device, registry);

    // No visible panels -> draw_ui not called at all.
    REQUIRE(device.ui_draws.empty());
}
```

> `RenderSystem` is default-constructed and `render(World&, RenderDevice&, AssetRegistry&) const` takes the registry as its third arg — matching the decal/screen-effect cases already in this file (`RenderSystem rs; rs.render(world, device, registry);`).

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build --target mosaic_unit_tests 2>&1 | tail -20`
Expected: FAIL — `UiQuad` undefined / `draw_ui` not a member.

- [ ] **Step 3: Write the implementation**

In `engine/render/render_device.hpp`, add the include and the `UiQuad` struct near `ScreenEffectParams`:

```cpp
#include "engine/ui/ui_rect.hpp"
```
```cpp
// One HUD quad in normalized screen space (top-left origin), alpha-blended.
struct UiQuad
{
    mosaic::engine::ui::Rect       rect;
    mosaic::foundation::math::Vec3 color;
    float                          alpha = 1.0f;
};
```

Add the seam to `RenderDevice` next to `draw_screen_effect`:

```cpp
    // Draw alpha-blended screen-space HUD quads (topmost). Default no-op (like draw_decals).
    virtual void draw_ui(const std::vector<UiQuad>& /*quads*/) {}
```

In `engine/render/render_system.hpp`, add includes:

```cpp
#include <algorithm>
#include <utility>
#include "engine/ui/ui_panel.hpp"
#include "engine/ui/ui_rect.hpp"
```

Add a small clamping helper (file-local, near the top of the render namespace or as a `static` in the header):

```cpp
inline mosaic::engine::ui::Anchor to_anchor(int a)
{
    return (a >= 0 && a <= 8) ? static_cast<mosaic::engine::ui::Anchor>(a)
                              : mosaic::engine::ui::Anchor::TopLeft;
}
```

At the very end of `render`, **after** the screen-effect overlay block:

```cpp
        // HUD pass: screen-space alpha-blended panels, drawn last (topmost, over the
        // screen-effect overlay). order-sorted so higher `order` draws on top.
        {
            std::vector<std::pair<int, UiQuad>> pending;
            world.for_each<mosaic::engine::ui::UiPanel>(
                [&](mosaic::foundation::EntityID, mosaic::engine::ui::UiPanel& p)
                {
                    if (p.alpha <= 0.0f) { return; }
                    const auto rect = mosaic::engine::ui::resolve_rect(
                        to_anchor(p.anchor), p.pos, p.size);
                    pending.push_back({p.order, UiQuad{rect, p.color, p.alpha}});
                });
            std::stable_sort(pending.begin(), pending.end(),
                             [](const auto& l, const auto& r) { return l.first < r.first; });
            if (!pending.empty())
            {
                std::vector<UiQuad> quads;
                quads.reserve(pending.size());
                for (auto& pr : pending) { quads.push_back(pr.second); }
                device.draw_ui(quads);
            }
        }
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build --target mosaic_unit_tests 2>&1 | tail -5 && ctest --test-dir build -R "render|ui" --output-on-failure 2>&1 | tail -25`
Expected: the two `[render][ui]` cases PASS; existing render tests still PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/render/render_device.hpp engine/render/render_system.hpp \
        tests/unit/render/render_system_test.cpp
git commit -m "feat(ui): draw_ui render seam + RenderSystem HUD pass (order-sorted, topmost)"
```

---

### Task 4: Vulkan screen-space quad pipeline (`draw_ui`)

**Files:**
- Create: `modules/render_vulkan/shaders/ui.vert`
- Create: `modules/render_vulkan/shaders/ui.frag`
- Modify: `modules/render_vulkan/vulkan_render_device.cpp` (pipeline + `draw_ui` override)
- Modify: `modules/render_vulkan/CMakeLists.txt` (compile `ui.vert`/`ui.frag`)

**Interfaces:**
- Consumes: `draw_ui(const std::vector<UiQuad>&)` seam (Task 3).
- Produces: a real `draw_ui` override; no new public API.

**Template to follow:** the existing screen-effect pipeline in `vulkan_render_device.cpp` — `ScreenEffectPush` (line ~73), `create_screen_effect_pipeline()` (~2135), the `draw_screen_effect` override (~712) picking `vp_screen_effect_pipeline_` when `viewport_pass_open_`, and the `screen_effect_pipeline_`/`vp_screen_effect_pipeline_`/`screen_effect_pipeline_layout_` members (~2542) + their `vkDestroyPipeline` teardown (~280). Mirror all of it with `ui_` names. Difference from screen-effect: UI draws **one quad per element** via a push-constant loop (screen-effect draws a single fullscreen triangle).

- [ ] **Step 1: Write the shaders**

Create `modules/render_vulkan/shaders/ui.vert`:

```glsl
#version 450

// Push constant: normalized rect (top-left origin) + color + alpha.
layout(push_constant) uniform Push {
    vec4 rect;   // x, y, w, h in [0,1]
    vec4 color;  // rgb + a (a unused here, passed through)
} pc;

layout(location = 0) out vec4 v_color;

// Unit quad as two triangles via gl_VertexIndex (0..5), corners in [0,1].
vec2 corner(int i) {
    if (i == 0) return vec2(0.0, 0.0);
    if (i == 1) return vec2(1.0, 0.0);
    if (i == 2) return vec2(0.0, 1.0);
    if (i == 3) return vec2(1.0, 0.0);
    if (i == 4) return vec2(1.0, 1.0);
    return vec2(0.0, 1.0);
}

void main() {
    vec2 uv = corner(gl_VertexIndex);
    float nx = pc.rect.x + uv.x * pc.rect.z; // normalized [0,1], top-left origin
    float ny = pc.rect.y + uv.y * pc.rect.w;
    // to NDC: x [-1,1] left->right, y [-1,1] top->bottom (top-left origin -> +y down)
    gl_Position = vec4(nx * 2.0 - 1.0, ny * 2.0 - 1.0, 0.0, 1.0);
    v_color = pc.color;
}
```

Create `modules/render_vulkan/shaders/ui.frag`:

```glsl
#version 450

layout(location = 0) in vec4 v_color;
layout(location = 0) out vec4 o_color;

void main() {
    o_color = vec4(v_color.rgb, v_color.a);
}
```

> NDC-y note: the swapchain pipeline uses a negative-height viewport (see the 36b `frontFace = CLOCKWISE` note), so `ny*2-1` maps top-left `y=0` to the top of the screen. Match whatever the screen-effect/decal pipelines assume for the viewport variant; if the HUD renders vertically flipped on hardware during maintainer check, flip `ny` to `1 - ny` for the affected variant. Depth test/write **off**, alpha blend **on** (copy the blend + depth state from the screen-effect pipeline).

- [ ] **Step 2: Wire the shader compile + confirm they build to SPIR-V**

In `modules/render_vulkan/CMakeLists.txt`, extend the `foreach(_stage ...)` list (line ~18) to include the two new stages:

```cmake
foreach(_stage cube.vert cube.frag particle.vert particle.frag ribbon.vert ribbon.frag decal.vert decal.frag screen_effect.vert screen_effect.frag ui.vert ui.frag)
```

Run: `cmake --build build --target mosaic_render_vulkan 2>&1 | tail -20`
Expected: `ui.vert`/`ui.frag` compile to SPIR-V with no glslang errors (the `draw_ui` override is still the default no-op at this point — link succeeds).

- [ ] **Step 3: Implement the pipeline + `draw_ui` override**

In `vulkan_render_device.cpp`, mirroring the screen-effect pipeline:

1. Add a push-constant struct near `ScreenEffectPush`:

```cpp
struct UiPush
{
    float rect[4];  // x, y, w, h
    float color[4]; // rgb + alpha
};
static_assert(sizeof(UiPush) == 32, "ui push constant must match the shader");
```

2. Add members near `screen_effect_pipeline_`:

```cpp
    VkPipeline       ui_pipeline_ = VK_NULL_HANDLE;
    VkPipeline       vp_ui_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout ui_pipeline_layout_ = VK_NULL_HANDLE;
```

3. Call `create_ui_pipeline();` right after `create_screen_effect_pipeline();` (~line 192).

4. Add `create_ui_pipeline()` as a near-copy of `create_screen_effect_pipeline()`: load `ui.vert.spv`/`ui.frag.spv`; push-constant range `size = sizeof(UiPush)`, stages `VERTEX | FRAGMENT`; topology `TRIANGLE_LIST`; **no** vertex input; depth test/write off; alpha blend on; create the swapchain variant into `ui_pipeline_` and the viewport variant into `vp_ui_pipeline_` (same two-`vkCreateGraphicsPipelines` shape as screen-effect).

5. Add the destroy calls next to the screen-effect ones (~line 280):

```cpp
            if (vp_ui_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, vp_ui_pipeline_, nullptr); }
            if (ui_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, ui_pipeline_, nullptr); }
            if (ui_pipeline_layout_ != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, ui_pipeline_layout_, nullptr); }
```

6. Override `draw_ui` (near `draw_screen_effect`):

```cpp
    void draw_ui(const std::vector<mosaic::engine::render::UiQuad>& quads) override
    {
        if (quads.empty()) { return; }
        VkPipeline pipe = viewport_pass_open_ ? vp_ui_pipeline_ : ui_pipeline_;
        vkCmdBindPipeline(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        for (const auto& q : quads)
        {
            UiPush pc{};
            pc.rect[0] = q.rect.x; pc.rect[1] = q.rect.y;
            pc.rect[2] = q.rect.w; pc.rect[3] = q.rect.h;
            pc.color[0] = q.color.x; pc.color[1] = q.color.y;
            pc.color[2] = q.color.z; pc.color[3] = q.alpha;
            vkCmdPushConstants(cmd_[frame_], ui_pipeline_layout_,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(UiPush), &pc);
            vkCmdDraw(cmd_[frame_], 6, 1, 0, 0); // 6 verts = one quad
        }
    }
```

> Copy exact viewport/scissor handling from `draw_screen_effect` if it sets them; use the same `cmd_[frame_]` / `viewport_pass_open_` names the file already uses.

- [ ] **Step 4: Build and run headless to verify no validation errors**

Run:
```bash
cmake --build build 2>&1 | tail -10
./build/app/mosaic run --window --ticks 3 2>&1 | tail -20; echo "exit: $?"
```
Expected: build succeeds; `mosaic run --window --ticks 3` exits 0 with **no** Vulkan validation errors (the HUD's on-screen appearance is the maintainer's hardware check).

> Sandbox note (per project memory `sandbox-no-gui-screenshots`): verify functionally — exit 0 + silent stderr. Do not attempt screenshots.

- [ ] **Step 5: Commit**

```bash
git add modules/render_vulkan/shaders/ui.vert modules/render_vulkan/shaders/ui.frag \
        modules/render_vulkan/vulkan_render_device.cpp modules/render_vulkan/CMakeLists.txt
git commit -m "feat(ui): Vulkan screen-space quad pipeline for draw_ui (swapchain + viewport)"
```

---

### Task 5: Demo HUD panel in the default scene

**Files:**
- Modify: `app/run/demo_scene.hpp` (attach a `UiPanel` to the camera entity)

**Interfaces:**
- Consumes: `ui::UiPanel` (Task 2), `ui::Anchor` (Task 1).

**Approach:** attach the demo `UiPanel` to the **existing camera entity** (exactly as `ScreenEffect` and `AudioListener` already are — see `demo_scene.hpp` ~line 343-347). This adds a component, **not** an entity, so the default-scene entity/transform counts are unchanged and the four count-asserting app tests stay green (mirrors the Audio pillar's "attached to existing entities, counts unchanged").

- [ ] **Step 1: Add the demo panel**

In `app/run/demo_scene.hpp`, next to the camera's `ScreenEffect` attach, add:

```cpp
#include "engine/ui/ui_panel.hpp"
#include "engine/ui/ui_rect.hpp"
```
```cpp
    // Demo HUD: a translucent bar anchored top-left, over everything.
    world.add_component<mosaic::engine::ui::UiPanel>(camera,
        mosaic::engine::ui::UiPanel{
            static_cast<int>(mosaic::engine::ui::Anchor::TopLeft),
            Vec2{0.02f, 0.02f}, Vec2{0.25f, 0.06f},
            Vec3{0.1f, 0.6f, 0.9f}, 0.6f, 0});
```

> Confirm the `UiPanel` aggregate-init order matches the struct: `{anchor, pos, size, color, alpha, order}`. If `Vec2` isn't already in scope in this file, qualify as `mosaic::foundation::math::Vec2` (the file already aliases `Vec3`).

- [ ] **Step 2: Build + run the full suite + headless run**

Run:
```bash
cmake --build build 2>&1 | tail -5
ctest --test-dir build --output-on-failure 2>&1 | tail -15
./build/app/mosaic run --window --ticks 3 2>&1 | tail -5; echo "exit: $?"
```
Expected: **all** tests PASS (the four default-scene count tests included — counts are unchanged because the panel rides on the camera entity); `mosaic run --window --ticks 3` exits 0.

> If any default-scene count test unexpectedly fails, it means the panel added an entity — re-check that it's attached to `camera`, not a new `create_entity()`.

- [ ] **Step 3: Commit**

```bash
git add app/run/demo_scene.hpp
git commit -m "feat(ui): demo HUD panel on the default-scene camera"
```

---

### Task 6: Update the build handoff (rides in this feature branch)

**Files:**
- Modify: `docs/project/next-steps.md`

Per project memory (`next-steps-in-feature-pr`), the next-steps update is committed on the feature branch, not its own PR.

- [ ] **Step 1: Update the handoff**

In `docs/project/next-steps.md`:
1. Update the **Last updated** line (date `2026-09-08`) with a one-paragraph "UI — U1 (screen-space HUD foundation) DONE" summary: reflected `UiPanel` (anchor + normalized pos/size + color/alpha/order), pure `resolve_rect` layout, `draw_ui` seam + RenderSystem topmost HUD pass, Vulkan screen-space quad pipeline, demo HUD bar on the camera; headless-tested, GPU visual maintainer-verified.
2. In the Phase-4 line, record the UI-pillar decomposition (U1 done; U2 text → U3 images → U4 interaction → U5 layout/hierarchy + world-space as follow-ups) and drop "UI" from *"Remaining Phase-4 subsystems"* (leaving networking), linking the spec + this plan.

- [ ] **Step 2: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "docs: mark UI U1 (screen-space HUD foundation) done; UI pillar decomposition"
```

---

## Self-Review

**Spec coverage:**
- `engine/ui/ui_rect.hpp` (Anchor/Rect/resolve_rect) → Task 1. ✓
- `engine/ui/ui_panel.hpp` reflected `UiPanel` + `"UiPanel"` registration → Task 2. ✓
- `draw_ui` seam + `UiQuad` → Task 3. ✓
- RenderSystem final HUD pass (skip transparent, order-sort, topmost after screen-effect) → Task 3. ✓
- Vulkan screen-space quad pipeline (swapchain + viewport, shader-generated quad, push constants) → Task 4. ✓
- Demo HUD panel → Task 5. ✓
- Tests: resolve_rect per anchor (Task 1), reflection round-trip + registration (Task 2), RenderSystem via FakeRenderDevice incl. skip-transparent + order-sort + empty (Task 3), default-scene counts (Task 5 — unchanged by design). ✓
- Documented limitation (aspect-correct sizing → U5) is carried in the `ui_rect.hpp` comment (Task 1). ✓
- Deviation from spec, noted intentionally: spec §Testing said "demo panel adds one entity → update the four count tests." Plan Task 5 instead attaches the panel to the camera entity (no new entity, counts unchanged), matching the `ScreenEffect` precedent — simpler and lower-risk. Same demo outcome, fewer test edits.

**Placeholder scan:** no TBD/TODO; every code step has real code. Vulkan Task 4 references the concrete `screen_effect` template with exact line anchors rather than reproducing the full ~80-line pipeline body verbatim — the established approach for GPU-pipeline tasks in this repo, with all names/structs specified.

**Type consistency:** `resolve_rect(Anchor, Vec2, Vec2) -> Rect`, `UiPanel{int anchor, Vec2 pos, Vec2 size, Vec3 color, float alpha, int order}`, `UiQuad{ui::Rect rect, Vec3 color, float alpha}`, `draw_ui(const std::vector<UiQuad>&)`, `UiPush{float rect[4]; float color[4]}` (32 bytes) — consistent across Tasks 1–5. Registration name `"UiPanel"` consistent (Tasks 2, 5).
