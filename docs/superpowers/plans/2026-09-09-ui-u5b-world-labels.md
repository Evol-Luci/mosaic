# UI U5b — World-space Text Labels Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render floating camera-facing text at an entity's world position — a `WorldLabel` component (nameplates/labels), completing the UI pillar.

**Architecture:** A pure `build_world_text` reuses `layout_text` and places glyphs on a camera-facing billboard (`cam_right`/`cam_up`) as world-space `WorldGlyphVertex`es. `RenderSystem` runs a world-label pass (in the 3D scene, before the HUD overlay), submitting them via a new `draw_world_text` seam. A Vulkan pipeline modeled on the ribbon pipeline (CPU vertex buffer + `view_proj`) plus the atlas texture descriptor draws them, depth-tested.

**Tech Stack:** C++20, Catch2, CMake, Vulkan (module-walled), the U2 font atlas, reflection.

**Spec:** `docs/superpowers/specs/2026-09-09-ui-u5b-world-labels-design.md`

**Base branch:** `claude/ui-u5b-world-labels` (off `main` = U1–U5a; `cmake --preset debug` configured).

## Global Constraints

- **Module wall:** no `Vk*` above `modules/`. Neutral UI types are backend-free; the pipeline lives only in `modules/render_vulkan`.
- **Build convention:** presets into `build/debug/`. Build: `cmake --build --preset debug [--target <t>]`. Test binary: `./build/debug/tests/mosaic_unit_tests "[tag]"` (Catch2 tag; comma = OR). Full suite: `ctest --preset debug`. App: `./build/debug/app/mosaic`. Do **not** use `cmake --build build` or `./build/app/...`. **Transient link failures:** the ~170 MB test binary intermittently fails at LINK under memory pressure (no compiler error) — `grep -iE "error:|error C[0-9]|undefined reference"`; if empty, re-run (up to ~3×). If a build leaves a **0-byte** test binary (→ `ctest BAD_COMMAND` / "Permission denied"), `rm -f` it and rebuild. **After changing `demo_scene.hpp`, rebuild the `mosaic` target before any `mosaic run` check.**
- **Vertex type placement:** `WorldGlyphVertex` is defined in `engine/render/render_device.hpp` (where `RibbonVertex`/`UiGlyphQuad`/`UiQuad`/`DecalInstance` all live). `build_world_text` returns `std::vector<mosaic::engine::render::WorldGlyphVertex>`.
- **Billboard axes:** `cam_world = Mat4::inverse_affine(view)`; `cam_right = normalize({m[0],m[1],m[2]})`, `cam_up = normalize({m[4],m[5],m[6]})` (as the particle pass derives them).
- **WorldTransform position:** `{wt->matrix.m[12], m[13], m[14]}`.
- **Types:** `World::for_each<T>([](EntityID,T&){…})`, `world.get_component<T>(id)->T*`. `Anchor`, `Rect`, `layout_text`, `FontAtlas` from `engine/ui`. `EntityID`/`AssetID`/`std::string` reflect.
- **Pipeline state:** alpha blend ON; **depth test ON, depth write OFF**; topology TRIANGLE_LIST; atlas sampler at `set 0, binding 0` (`texture_set_layout_`); `view_proj` push constant (VERTEX stage).

---

### Task 1: `WorldLabel` reflected component

**Files:**
- Create: `engine/ui/world_label.hpp`
- Create: `tests/unit/ui/world_label_reflection_test.cpp`
- Modify: `engine/reflection/builtin_components.hpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `struct mosaic::engine::ui::WorldLabel { AssetID font; std::string text; float size; Vec3 color; float alpha; Vec3 offset; };` registered `"WorldLabel"`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/ui/world_label_reflection_test.cpp`:

```cpp
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/ui/world_label.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reference.hpp"
#include "foundation/serialization/reflect_value.hpp"

using Catch::Approx;
using mosaic::engine::ui::WorldLabel;
using mosaic::foundation::math::Vec3;
using mosaic::foundation::serialization::AssetID;

TEST_CASE("WorldLabel round-trips through reflection", "[ui][reflection]")
{
    WorldLabel l;
    l.font   = AssetID::from_string("00000000-0000-0000-0000-0000000000e5");
    l.text   = "Orbiter";
    l.size   = 0.5f;
    l.color  = Vec3{0.2f, 0.9f, 0.3f};
    l.alpha  = 0.8f;
    l.offset = Vec3{0.0f, 1.5f, 0.0f};

    const auto value = mosaic::foundation::serialization::to_value(l);
    WorldLabel back;
    mosaic::foundation::serialization::from_value(value, back);

    REQUIRE(back.font == l.font);
    REQUIRE(back.text == "Orbiter");
    REQUIRE(back.size == Approx(0.5f));
    REQUIRE(back.color.y == Approx(0.9f));
    REQUIRE(back.alpha == Approx(0.8f));
    REQUIRE(back.offset.y == Approx(1.5f));
}

TEST_CASE("WorldLabel is a registered built-in component", "[ui][reflection]")
{
    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    REQUIRE(types.has("WorldLabel"));
}
```

Add to `tests/CMakeLists.txt` (near the other `unit/ui/*` entries):

```cmake
  unit/ui/world_label_reflection_test.cpp
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -20`
Expected: FAIL — `engine/ui/world_label.hpp` not found / `types.has("WorldLabel")` false.

- [ ] **Step 3: Write the implementation**

Create `engine/ui/world_label.hpp`:

```cpp
#pragma once

#include <string>

#include "foundation/math/vec3.hpp"
#include "foundation/serialization/reference.hpp" // AssetID

namespace mosaic::engine::ui
{
using mosaic::foundation::math::Vec3;

// Camera-facing text floating at the entity's WorldTransform position (+ offset).
// size is the world-space line height. The entity needs a WorldTransform. font is a
// "font" AssetID (FontAtlas). Script-mutable (change `text`).
struct WorldLabel
{
    mosaic::foundation::serialization::AssetID font;
    std::string text;
    float size   = 1.0f;
    Vec3  color  = Vec3{1.0f, 1.0f, 1.0f};
    float alpha  = 1.0f;
    Vec3  offset = Vec3{0.0f, 0.0f, 0.0f};
};

template <class V>
void reflect(V& visitor, WorldLabel& l)
{
    visitor.member("font", l.font);
    visitor.member("text", l.text);
    visitor.member("size", l.size);
    visitor.member("color", l.color);
    visitor.member("alpha", l.alpha);
    visitor.member("offset", l.offset);
}
} // namespace mosaic::engine::ui
```

Register in `engine/reflection/builtin_components.hpp` — add the include near the other UI
includes and the registration after the `UiParent` line:

```cpp
#include "engine/ui/world_label.hpp"
```
```cpp
    types.register_component<mosaic::engine::ui::WorldLabel>(registry, "WorldLabel");
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -5 && ./build/debug/tests/mosaic_unit_tests "[ui][reflection]" 2>&1 | tail -6`
Expected: `[ui][reflection]` cases PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/ui/world_label.hpp engine/reflection/builtin_components.hpp \
        tests/unit/ui/world_label_reflection_test.cpp tests/CMakeLists.txt
git commit -m "feat(ui): reflected WorldLabel component (world-space text)"
```

---

### Task 2: `WorldGlyphVertex` + `build_world_text` (pure billboard builder)

**Files:**
- Modify: `engine/render/render_device.hpp` (`WorldGlyphVertex` struct)
- Create: `engine/ui/world_text.hpp`
- Create: `tests/unit/ui/world_text_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `layout_text`/`FontAtlas`/`Anchor`/`Rect` (U2), `read_ttf` (tests).
- Produces:
  - `struct mosaic::engine::render::WorldGlyphVertex { Vec3 position; float u, v; Vec3 color; float alpha; };`
  - `std::vector<render::WorldGlyphVertex> mosaic::engine::ui::build_world_text(const FontAtlas&, const std::string&, Vec3 anchor_world, Vec3 cam_right, Vec3 cam_up, float size, Vec3 color, float alpha);`

- [ ] **Step 1: Write the failing test**

Create `tests/unit/ui/world_text_test.cpp`:

```cpp
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/asset/font_cook.hpp"   // read_ttf (baked fixture)
#include "app/run/scene_io.hpp"      // load_scene_file
#include "engine/ui/font_atlas.hpp"
#include "engine/ui/world_text.hpp"
#include "foundation/math/vec3.hpp"

using Catch::Approx;
using mosaic::engine::ui::build_world_text;
using mosaic::engine::ui::FontAtlas;
using mosaic::foundation::math::Vec3;

namespace
{
FontAtlas fixture_font()
{
    const auto bytes = mosaic::app::load_scene_file(
        std::string(MOSAIC_TEST_DIR) + "/fixtures/LiberationSans-Regular.ttf");
    REQUIRE(bytes.has_value());
    const auto font = mosaic::app::read_ttf(*bytes, 48.0f);
    REQUIRE(font.has_value());
    return *font;
}
} // namespace

TEST_CASE("build_world_text emits camera-facing glyph triangles centered on the anchor", "[ui][world]")
{
    const FontAtlas font = fixture_font();
    const Vec3 anchor{0.0f, 0.0f, 5.0f};
    const auto verts = build_world_text(font, "AB", anchor, Vec3{1, 0, 0}, Vec3{0, 1, 0},
                                        1.0f, Vec3{1, 1, 1}, 1.0f);
    REQUIRE(verts.size() == 12); // 2 glyphs * 6 verts

    float min_x = 1e9f, max_x = -1e9f;
    for (const auto& v : verts)
    {
        REQUIRE(v.position.z == Approx(5.0f)); // cam_right=+X, cam_up=+Y -> z unchanged
        min_x = std::min(min_x, v.position.x);
        max_x = std::max(max_x, v.position.x);
    }
    REQUIRE(min_x < 0.0f); // centered on anchor.x = 0
    REQUIRE(max_x > 0.0f);
}

TEST_CASE("build_world_text returns nothing for empty text", "[ui][world]")
{
    const FontAtlas font = fixture_font();
    const auto verts = build_world_text(font, "", Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{0, 1, 0},
                                        1.0f, Vec3{1, 1, 1}, 1.0f);
    REQUIRE(verts.empty());
}
```

Add to `tests/CMakeLists.txt`:

```cmake
  unit/ui/world_text_test.cpp
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -20`
Expected: FAIL — `engine/ui/world_text.hpp` not found.

- [ ] **Step 3: Write the implementation**

In `engine/render/render_device.hpp`, add `WorldGlyphVertex` near `RibbonVertex`:

```cpp
// One world-space glyph vertex (camera-facing text). CPU-expanded; drawn by draw_world_text.
struct WorldGlyphVertex
{
    mosaic::foundation::math::Vec3 position; // world space
    float                          u = 0, v = 0;
    mosaic::foundation::math::Vec3 color;
    float                          alpha = 1.0f;
};
```

Create `engine/ui/world_text.hpp`:

```cpp
#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "engine/render/render_device.hpp" // WorldGlyphVertex
#include "engine/ui/font_atlas.hpp"
#include "engine/ui/text_layout.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::engine::ui
{
// Expand `text` into camera-facing world-space glyph triangles (6 verts/glyph, LIST),
// centered horizontally + vertically on anchor_world. cam_right/cam_up orient the
// billboard; size scales line-height units to world units. Reuses layout_text (local
// line-height units: frame {0,0,1,1}, aspect 1, height 1). Empty text -> no verts.
inline std::vector<mosaic::engine::render::WorldGlyphVertex> build_world_text(
    const FontAtlas& font, const std::string& text,
    mosaic::foundation::math::Vec3 anchor_world,
    mosaic::foundation::math::Vec3 cam_right, mosaic::foundation::math::Vec3 cam_up,
    float size, mosaic::foundation::math::Vec3 color, float alpha)
{
    std::vector<mosaic::engine::render::WorldGlyphVertex> out;
    const std::vector<TextQuad> quads = layout_text(
        font, text, Anchor::TopLeft, mosaic::foundation::math::Vec2{0.0f, 0.0f},
        1.0f /*height*/, 1.0f /*aspect*/, color, alpha);
    if (quads.empty()) { return out; }

    float max_x = 0.0f;
    for (const auto& q : quads) { max_x = std::max(max_x, q.rect.x + q.rect.w); }
    const float cx0 = max_x * 0.5f; // horizontal center
    const float cy0 = 0.5f;         // vertical center (local box ~1 line tall)

    auto emit = [&](float lx, float ly, float u, float vv)
    {
        const float ox = (lx - cx0) * size;  // along cam_right
        const float oy = (cy0 - ly) * size;  // along cam_up (local-down -> world-up)
        mosaic::engine::render::WorldGlyphVertex vert;
        vert.position = mosaic::foundation::math::Vec3{
            anchor_world.x + ox * cam_right.x + oy * cam_up.x,
            anchor_world.y + ox * cam_right.y + oy * cam_up.y,
            anchor_world.z + ox * cam_right.z + oy * cam_up.z};
        vert.u = u; vert.v = vv; vert.color = color; vert.alpha = alpha;
        out.push_back(vert);
    };

    for (const auto& q : quads)
    {
        const float x0 = q.rect.x, y0 = q.rect.y, x1 = q.rect.x + q.rect.w, y1 = q.rect.y + q.rect.h;
        emit(x0, y0, q.u0, q.v0); emit(x1, y0, q.u1, q.v0); emit(x0, y1, q.u0, q.v1);
        emit(x1, y0, q.u1, q.v0); emit(x1, y1, q.u1, q.v1); emit(x0, y1, q.u0, q.v1);
    }
    return out;
}
} // namespace mosaic::engine::ui
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -5 && ./build/debug/tests/mosaic_unit_tests "[ui][world]" 2>&1 | tail -6`
Expected: the 2 `[ui][world]` cases PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/render/render_device.hpp engine/ui/world_text.hpp \
        tests/unit/ui/world_text_test.cpp tests/CMakeLists.txt
git commit -m "feat(ui): WorldGlyphVertex + build_world_text (camera-facing world glyphs)"
```

---

### Task 3: `draw_world_text` seam + RenderSystem world-label pass

**Files:**
- Modify: `engine/render/render_device.hpp` (`draw_world_text` seam)
- Modify: `engine/render/render_system.hpp` (world-label pass)
- Modify: `tests/unit/render/render_system_test.cpp` (fake capture + test)

**Interfaces:**
- Consumes: `build_world_text` (Task 2), `WorldLabel` (Task 1), `FontAtlas`, `WorldTransform`, `font_textures_`.
- Produces: `virtual void RenderDevice::draw_world_text(const std::vector<WorldGlyphVertex>&, const Mat4& view_proj, TextureHandle atlas) {}`.

- [ ] **Step 1: Write the failing test**

In `tests/unit/render/render_system_test.cpp`, add to `FakeRenderDevice` (near the other ui captures):

```cpp
    struct WorldTextDrawCall { std::vector<mosaic::engine::render::WorldGlyphVertex> verts;
                               Mat4 view_proj;
                               mosaic::engine::render::TextureHandle atlas; };
    std::vector<WorldTextDrawCall> world_text_draws;
    void draw_world_text(const std::vector<mosaic::engine::render::WorldGlyphVertex>& verts,
                         const Mat4& view_proj,
                         mosaic::engine::render::TextureHandle atlas) override
    {
        world_text_draws.push_back({verts, view_proj, atlas});
    }
```

Add includes near the others:

```cpp
#include "engine/ui/world_label.hpp"
```

Add a case (reuse the `register_font` helper already in this file from the U2b text tests):

```cpp
TEST_CASE("RenderSystem emits world-text verts for a WorldLabel with a WorldTransform", "[render][ui]")
{
    using mosaic::engine::ui::WorldLabel;
    AssetRegistry registry;
    const auto font_id = register_font(registry);
    World world;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 5});

    auto e = world.create_entity();
    world.add_component<WorldTransform>(e, WorldTransform{Mat4::translation(Vec3{0, 0, 0})});
    WorldLabel l; l.font = font_id; l.text = "Hi"; l.size = 1.0f;
    world.add_component<WorldLabel>(e, l);

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.world_text_draws.size() == 1);
    REQUIRE(device.world_text_draws[0].verts.size() == 12); // "Hi" = 2 glyphs * 6
    REQUIRE(device.world_text_draws[0].atlas != 0);
}

TEST_CASE("RenderSystem skips a WorldLabel with no WorldTransform or empty text", "[render][ui]")
{
    using mosaic::engine::ui::WorldLabel;
    AssetRegistry registry;
    const auto font_id = register_font(registry);
    World world;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 5});

    auto no_wt = world.create_entity();
    WorldLabel a; a.font = font_id; a.text = "X"; world.add_component<WorldLabel>(no_wt, a);
    auto empty = world.create_entity();
    world.add_component<WorldTransform>(empty, WorldTransform{Mat4::translation(Vec3{0, 0, 0})});
    WorldLabel b; b.font = font_id; b.text = ""; world.add_component<WorldLabel>(empty, b);

    FakeRenderDevice device;
    rs.render(world, device, registry);
    REQUIRE(device.world_text_draws.empty());
}
```

> `register_font` is the helper the U2b render tests added (bakes the fixture font + registers a `FontAtlas`). If it is in an anonymous namespace earlier in the file, reuse it; if not found, copy it from the existing text-pass tests.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -20`
Expected: FAIL — `draw_world_text` / `world_text_draws` undefined.

- [ ] **Step 3: Write the implementation**

In `engine/render/render_device.hpp`, add the seam near `draw_ribbons`:

```cpp
    // Draw camera-facing world-space text glyphs (pre-expanded world verts), sampling the
    // font atlas. view_proj = projection * view. Default no-op (like draw_ribbons).
    virtual void draw_world_text(const std::vector<WorldGlyphVertex>& /*verts*/,
                                 const mosaic::foundation::math::Mat4& /*view_proj*/,
                                 TextureHandle /*atlas*/) {}
```

In `engine/render/render_system.hpp`, add includes:

```cpp
#include "engine/ui/world_label.hpp"
#include "engine/ui/world_text.hpp"
```

Add a **world-label pass** as its own block, after the billboard (particle/trail) section
and before the HUD passes (so labels are in the 3D scene, under the HUD overlay). It
derives its own camera axes so it does not depend on the particle guard:

```cpp
        // World-label pass: camera-facing 3D text. Part of the scene (depth-tested),
        // under the screen-space HUD passes.
        {
            const Mat4 cam_world = Mat4::inverse_affine(view);
            const Vec3 cam_right = mosaic::foundation::math::normalize(
                Vec3{cam_world.m[0], cam_world.m[1], cam_world.m[2]});
            const Vec3 cam_up = mosaic::foundation::math::normalize(
                Vec3{cam_world.m[4], cam_world.m[5], cam_world.m[6]});
            world.for_each<mosaic::engine::ui::WorldLabel>(
                [&](mosaic::foundation::EntityID e, mosaic::engine::ui::WorldLabel& l)
                {
                    if (l.alpha <= 0.0f || l.text.empty()) { return; }
                    const auto* wt = world.get_component<transform::WorldTransform>(e);
                    if (wt == nullptr) { return; }
                    std::shared_ptr<mosaic::engine::ui::FontAtlas> font =
                        registry.load<mosaic::engine::ui::FontAtlas>(l.font);
                    if (!font) { return; }
                    const TextureHandle atlas = font_textures_.get(l.font, registry, device);
                    if (atlas == 0) { return; }
                    const Vec3 pos{wt->matrix.m[12], wt->matrix.m[13], wt->matrix.m[14]};
                    const Vec3 anchor{pos.x + l.offset.x, pos.y + l.offset.y, pos.z + l.offset.z};
                    const auto verts = mosaic::engine::ui::build_world_text(
                        *font, l.text, anchor, cam_right, cam_up, l.size, l.color, l.alpha);
                    if (!verts.empty()) { device.draw_world_text(verts, projection * view, atlas); }
                });
        }
```

> Place this block inside `render(...)` where `view`, `projection`, `registry`, `device`,
> and `font_textures_` are in scope (the same scope as the existing HUD passes). `Vec3` is
> already aliased in `render()`; `transform::WorldTransform` is already used by the decal
> pass.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -5 && ./build/debug/tests/mosaic_unit_tests "[render],[ui]" 2>&1 | tail -6`
Expected: the new `[render][ui]` world-label cases PASS; existing render/ui tests still PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/render/render_device.hpp engine/render/render_system.hpp \
        tests/unit/render/render_system_test.cpp
git commit -m "feat(ui): draw_world_text seam + RenderSystem world-label pass"
```

---

### Task 4: Vulkan world-space glyph pipeline

**Files:**
- Create: `modules/render_vulkan/shaders/world_text.vert`, `world_text.frag`
- Modify: `modules/render_vulkan/vulkan_render_device.cpp`
- Modify: `modules/render_vulkan/CMakeLists.txt`

**Interfaces:**
- Consumes: `draw_world_text(const std::vector<WorldGlyphVertex>&, const Mat4&, TextureHandle)` (Task 3).

**Templates in the same file:** the **ribbon** pipeline gives the CPU vertex-buffer
streaming (`ribbon_vertices_[frame_]` `InstanceBuffer`, map/memcpy), the `RibbonPush`
`view_proj` push constant, the vertex-input binding/attrs, and the swapchain/viewport
variants + teardown. The **decal** pipeline / `draw_decals` give the atlas descriptor
(`texture_set_layout_` in the pipeline layout + `vkCmdBindDescriptorSets(...,
&textures_[tex_index].set, ...)`). Mirror ribbon, add the descriptor + a `uv` attribute.

- [ ] **Step 1: Write the shaders**

Create `modules/render_vulkan/shaders/world_text.vert`:

```glsl
#version 450

layout(push_constant) uniform Push { mat4 view_proj; } pc;

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_color;
layout(location = 3) in float in_alpha;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;

void main()
{
    gl_Position = pc.view_proj * vec4(in_pos, 1.0);
    v_uv = in_uv;
    v_color = vec4(in_color, in_alpha);
}
```

Create `modules/render_vulkan/shaders/world_text.frag`:

```glsl
#version 450

layout(set = 0, binding = 0) uniform sampler2D atlas;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 0) out vec4 o_color;

void main()
{
    float cov = texture(atlas, v_uv).a;
    o_color = vec4(v_color.rgb, cov * v_color.a);
}
```

- [ ] **Step 2: Wire the shader compile + confirm SPIR-V builds**

In `modules/render_vulkan/CMakeLists.txt`, add `world_text.vert world_text.frag` to the
`foreach(_stage ...)` shader list.

Run: `cmake --build --preset debug --target mosaic_render_vulkan 2>&1 | tail -20`
Expected: both shaders compile to SPIR-V with no glslang errors (the `draw_world_text`
override is still the default no-op — link succeeds).

- [ ] **Step 3: Implement the pipeline + `draw_world_text` override**

In `vulkan_render_device.cpp`, mirroring the ribbon + decal pipelines:

1. Push-constant struct near `RibbonPush` (if `RibbonPush` is `{ float view_proj[16]; }`,
   define an identical `WorldTextPush`), or reuse a shared push layout:

```cpp
struct WorldTextPush { float view_proj[16]; };
```

2. Members near `ribbon_pipeline_`:

```cpp
    VkPipeline       world_text_pipeline_ = VK_NULL_HANDLE;
    VkPipeline       vp_world_text_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout world_text_pipeline_layout_ = VK_NULL_HANDLE;
```

Plus a per-frame vertex buffer array like `ribbon_vertices_`:

```cpp
    std::array<InstanceBuffer, kFramesInFlight> world_text_vertices_{};
```

3. Call `create_world_text_pipeline();` right after `create_ribbon_pipeline();`.

4. `create_world_text_pipeline()` = a copy of `create_ribbon_pipeline()` with: the vertex
   input describing `WorldGlyphVertex` (binding stride = `sizeof(WorldGlyphVertex)`; attrs:
   loc0 `R32G32B32_SFLOAT` @ `offsetof(position)`, loc1 `R32G32_SFLOAT` @ `offsetof(u)`
   (uv is two contiguous floats `u`,`v`), loc2 `R32G32B32_SFLOAT` @ `offsetof(color)`,
   loc3 `R32_SFLOAT` @ `offsetof(alpha)`); the pipeline **layout** includes
   `texture_set_layout_` (`setLayoutCount = 1; pSetLayouts = &texture_set_layout_;`) plus a
   `WorldTextPush` push-constant range (VERTEX stage); **depth test ON, depth write OFF**;
   alpha blend ON; loads `world_text.vert.spv`/`world_text.frag.spv`; swapchain +
   viewport variants (`render_pass_` then `vp_render_pass_`).

5. Teardown next to the ribbon destroys (pipelines + layout), and free
   `world_text_vertices_` buffers wherever `ribbon_vertices_` are freed.

6. `draw_world_text` override (near `draw_ribbons`) — ribbon's vertex streaming + decal's
   descriptor bind:

```cpp
    void draw_world_text(const std::vector<mosaic::engine::render::WorldGlyphVertex>& verts,
                         const mosaic::foundation::math::Mat4& view_proj,
                         mosaic::engine::render::TextureHandle atlas) override
    {
        if (!viewport_pass_open_) { ensure_swapchain_pass(); }
        if (frame_skipped_ || verts.empty() || textures_.empty()) { return; }

        const VkDeviceSize bytes = verts.size() * sizeof(verts[0]);
        InstanceBuffer& vb = world_text_vertices_[frame_];
        if (vb.capacity < bytes)
        {
            if (vb.buf != VK_NULL_HANDLE) { vkDestroyBuffer(device_, vb.buf, nullptr); }
            if (vb.mem != VK_NULL_HANDLE) { vkFreeMemory(device_, vb.mem, nullptr); }
            create_buffer(bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          vb.buf, vb.mem);
            vb.capacity = bytes;
        }
        void* mapped = nullptr;
        vkMapMemory(device_, vb.mem, 0, bytes, 0, &mapped);
        std::memcpy(mapped, verts.data(), static_cast<size_t>(bytes));
        vkUnmapMemory(device_, vb.mem);

        const std::uint32_t tex_index = (atlas == 0 || atlas > textures_.size()) ? 0u : atlas - 1u;
        vkCmdBindPipeline(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                          viewport_pass_open_ ? vp_world_text_pipeline_ : world_text_pipeline_);
        vkCmdBindDescriptorSets(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                world_text_pipeline_layout_, 0, 1, &textures_[tex_index].set, 0, nullptr);
        WorldTextPush pc{};
        std::memcpy(pc.view_proj, view_proj.m.data(), sizeof(pc.view_proj));
        vkCmdPushConstants(cmd_[frame_], world_text_pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(pc), &pc);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd_[frame_], 0, 1, &vb.buf, &offset);
        vkCmdDraw(cmd_[frame_], static_cast<uint32_t>(verts.size()), 1, 0, 0);
    }
```

- [ ] **Step 4: Build + run headless to verify no validation errors**

Run:
```bash
cmake --build --preset debug 2>&1 | tail -10
./build/debug/app/mosaic run --window --ticks 3 2>&1 | tail -20; echo "exit: $?"
```
Expected: build succeeds; exit 0 with no Vulkan validation errors. (No `WorldLabel` in the
scene yet — Task 5 adds the demo. This confirms pipeline create/destroy is clean.) Do not
attempt screenshots (headless sandbox).

- [ ] **Step 5: Commit**

```bash
git add modules/render_vulkan/shaders/world_text.vert modules/render_vulkan/shaders/world_text.frag \
        modules/render_vulkan/vulkan_render_device.cpp modules/render_vulkan/CMakeLists.txt
git commit -m "feat(ui): Vulkan world-space glyph pipeline for draw_world_text"
```

---

### Task 5: Demo label + verify + next-steps

**Files:**
- Modify: `app/run/demo_scene.hpp` (demo `WorldLabel`)
- Modify: `docs/project/next-steps.md`

- [ ] **Step 1: Add the demo label**

In `app/run/demo_scene.hpp`, add the include near the other UI includes:

```cpp
#include "engine/ui/world_label.hpp"
```

Attach a `WorldLabel` to an existing 3D demo entity that has a `Transform`/`WorldTransform`
— the scripted **orbiter** cube is a good target (grep for where `orbiter` is created in
`build_default_scene`). Add after its other components:

```cpp
    world.add_component<mosaic::engine::ui::WorldLabel>(orbiter,
        mosaic::engine::ui::WorldLabel{
            kBuiltinFontId, "Orbiter", 0.4f,
            mosaic::foundation::math::Vec3{1.0f, 1.0f, 1.0f}, 1.0f,
            mosaic::foundation::math::Vec3{0.0f, 0.8f, 0.0f}});
```

> `WorldLabel` init order is `{font, text, size, color, alpha, offset}` (offset +0.8 Y so
> it floats above the cube). `kBuiltinFontId` is defined in this header. If the orbiter's
> entity variable has a different name, use that; it must be an entity with a `Transform`.
> Adds a component to an existing entity — no new entity, so default-scene counts are
> unchanged.

- [ ] **Step 2: Build + full suite + headless run**

```bash
cmake --build --preset debug 2>&1 | tail -5
ctest --preset debug 2>&1 | tail -4
./build/debug/app/mosaic run --window --ticks 3 2>&1 | tail -5; echo "exit: $?"
```
Expected: all tests PASS (counts unchanged); `mosaic run --window` exits 0 with no
validation errors. The label's on-screen appearance is the maintainer's hardware check.

- [ ] **Step 3: Update the handoff + commit**

In `docs/project/next-steps.md`:
1. Update the **Last updated** line (`2026-09-09`) with a "UI — U5b (world-space text labels) DONE — **UI pillar COMPLETE**" summary: a reflected `WorldLabel` + a pure `build_world_text` (camera-facing billboard glyphs via cam right/up) + a `draw_world_text` seam + a RenderSystem world-label pass (depth-tested, under the HUD) + a Vulkan world-space glyph pipeline (ribbon-style vertex stream + atlas descriptor) + a demo floating label. Note the **UI pillar (U1–U5) is now complete**: HUD panels, text, images, click interaction, layout/hierarchy, and world-space labels.
2. In the UI-decomposition line, mark **U5b done** and the **UI pillar COMPLETE**; note the remaining Phase-4 subsystem is **networking**. Link the U5b spec + plan.

```bash
git add app/run/demo_scene.hpp docs/project/next-steps.md
git commit -m "feat(ui): demo floating WorldLabel; docs: UI pillar complete (U5b done)"
```

---

## Self-Review

**Spec coverage:**
- `WorldLabel` reflected + `"WorldLabel"` registration → Task 1. ✓
- `WorldGlyphVertex` + pure `build_world_text` (reuses layout_text, camera-facing, centered) → Task 2. ✓
- `draw_world_text` seam + RenderSystem world-label pass (depth-tested, cam axes, WorldTransform) → Task 3. ✓
- Vulkan world-space glyph pipeline (ribbon vertex stream + atlas descriptor, depth test on/write off) → Task 4. ✓
- Demo floating label → Task 5. ✓
- Tests: reflection (Task 1); build_world_text count/centering/empty (Task 2); render pass incl. no-WorldTransform/empty skip (Task 3); counts unchanged (Task 5). ✓

**Placeholder scan:** no TBD/TODO; every code step has real code. Task 4 references the concrete ribbon + decal templates in-file (both present) rather than reproducing the full pipeline body, with all names/structs/attrs specified.

**Type consistency:** `WorldLabel{font,text,size,color,alpha,offset}` (Tasks 1,3,5); `WorldGlyphVertex{position,u,v,color,alpha}` (Tasks 2,3,4); `build_world_text(const FontAtlas&, const std::string&, Vec3, Vec3, Vec3, float, Vec3, float) -> vector<render::WorldGlyphVertex>` (Tasks 2,3); `draw_world_text(const vector<WorldGlyphVertex>&, const Mat4&, TextureHandle)` (Tasks 3,4); `WorldTextPush{float view_proj[16]}` (Task 4). Registration name `"WorldLabel"` consistent (Tasks 1,5). Vertex attrs match the struct field order/offsets (Task 4).
