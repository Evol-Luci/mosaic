# UI U2b — HUD Text Rendering Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render single-line HUD text — a reflected `UiText` component laid out into textured glyph quads that sample the U2a font atlas, drawn over the HUD.

**Architecture:** A pure `layout_text` turns a string + `FontAtlas` into normalized, aspect-corrected glyph `TextQuad`s. `RenderSystem` runs a text pass (after the U1 panel pass) that resolves each `UiText`'s font (atlas texture via a new `FontTextureCache`; metrics via `load<FontAtlas>`), lays it out with the existing `aspect_`, and submits `UiGlyphQuad`s through a new `draw_ui_text` seam. A Vulkan textured-quad pipeline (U1 `ui` pipeline + the decal texture descriptor) draws them. A built-in font baked at startup drives a demo `UiText`.

**Tech Stack:** C++20, Catch2, CMake, Vulkan (module-walled), stb_truetype (via U2a's `read_ttf`), reflection.

**Spec:** `docs/superpowers/specs/2026-09-08-ui-u2b-text-render-design.md`

**Base branch:** `claude/ui-u2b-text` (already contains U1 + U2a merged; `cmake --preset debug` configured, 536 tests green).

## Global Constraints

- **Module wall:** no `Vk*`/stb above `modules/`. Neutral UI types are backend-free. The Vulkan pipeline lives only in `modules/render_vulkan`.
- **Build convention:** presets into `build/debug/`. Build: `cmake --build --preset debug [--target <t>]`. Test binary: `./build/debug/tests/mosaic_unit_tests "[tag]"` (Catch2 tag; comma = OR). Full suite: `ctest --preset debug`. App: `./build/debug/app/mosaic`. Do **not** use `cmake --build build` or `./build/app/...`. Retry a build once if it fails only at the LINK step (the ~170 MB test binary occasionally fails to link under memory pressure — transient, not a code error). **After changing a header used by the app (e.g. `asset_io.hpp`, `demo_scene.hpp`, `render_system.hpp`), rebuild the `mosaic` target too before any `mosaic run` check — a stale binary misleads.**
- **Coordinate convention:** normalized screen space, origin top-left, x right, y down, `[0,1]` (U1).
- **Font metrics:** `FontAtlas.ascent > 0`, `descent < 0` (stb), `line_height` px; `Glyph.y_off ≤ 0` is above the baseline. Atlas coverage is in the **alpha** channel of an RGBA8 `Image`.
- **Aspect:** `RenderSystem::aspect_` (set via `set_aspect`, already wired in `app/main.cpp` + `editor.cpp`) corrects glyph normalized-X: `sx = scale / aspect`.
- **Test fixture:** `MOSAIC_TEST_DIR` is already defined on `mosaic_unit_tests` (U2a); the Liberation TTF fixture is at `tests/fixtures/LiberationSans-Regular.ttf`. `read_ttf` is declared in `app/asset/font_cook.hpp` (linked into tests via `mosaic_app`).
- **Reflection field types:** `AssetID` (like `Decal.texture`) and `std::string` (like `ScriptComponent.source`) are both supported reflected members.

---

### Task 1: `layout_text` — pure text layout

**Files:**
- Create: `engine/ui/text_layout.hpp`
- Create: `tests/unit/ui/text_layout_test.cpp`
- Modify: `tests/CMakeLists.txt` (add the test source)

**Interfaces:**
- Consumes: `ui::FontAtlas`/`ui::Glyph` (U2a), `ui::Anchor`/`ui::Rect`/`resolve_rect` (U1), `read_ttf` (U2a, in tests only).
- Produces:
  - `struct mosaic::engine::ui::TextQuad { Rect rect; float u0,v0,u1,v1; Vec3 color; float alpha; };`
  - `std::vector<TextQuad> mosaic::engine::ui::layout_text(const FontAtlas&, const std::string&, Anchor, Vec2 pos, float height, float aspect, Vec3 color, float alpha);`

- [ ] **Step 1: Write the failing test**

Create `tests/unit/ui/text_layout_test.cpp`:

```cpp
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/asset/font_cook.hpp"   // read_ttf (baked fixture)
#include "app/run/scene_io.hpp"      // load_scene_file
#include "engine/ui/font_atlas.hpp"
#include "engine/ui/text_layout.hpp"

using Catch::Approx;
using mosaic::engine::ui::Anchor;
using mosaic::engine::ui::FontAtlas;
using mosaic::engine::ui::layout_text;
using mosaic::engine::ui::TextQuad;
using mosaic::foundation::math::Vec2;
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
const Vec3 kWhite{1, 1, 1};
} // namespace

TEST_CASE("layout_text emits a quad per visible glyph, left to right", "[ui][text]")
{
    const FontAtlas font = fixture_font();
    const auto quads = layout_text(font, "AB", Anchor::TopLeft, Vec2{0, 0}, 0.1f, 1.0f,
                                   kWhite, 1.0f);
    REQUIRE(quads.size() == 2);            // 'A' and 'B' both have ink
    REQUIRE(quads[1].rect.x > quads[0].rect.x); // B is to the right of A
    REQUIRE(quads[0].rect.w > 0.0f);
    REQUIRE(quads[0].rect.h > 0.0f);
    REQUIRE(quads[0].u1 > quads[0].u0);
    REQUIRE(quads[0].color.x == Approx(1.0f));
    REQUIRE(quads[0].alpha == Approx(1.0f));
}

TEST_CASE("layout_text skips a space's empty quad but still advances", "[ui][text]")
{
    const FontAtlas font = fixture_font();
    const auto ab  = layout_text(font, "AB",  Anchor::TopLeft, Vec2{0, 0}, 0.1f, 1.0f, kWhite, 1.0f);
    const auto asb = layout_text(font, "A B", Anchor::TopLeft, Vec2{0, 0}, 0.1f, 1.0f, kWhite, 1.0f);
    REQUIRE(asb.size() == 2);                       // space emits no quad
    REQUIRE(asb[1].rect.x > ab[1].rect.x);          // the space pushed B further right
}

TEST_CASE("layout_text narrows glyphs in X as aspect grows", "[ui][text]")
{
    const FontAtlas font = fixture_font();
    const auto wide = layout_text(font, "A", Anchor::TopLeft, Vec2{0, 0}, 0.1f, 2.0f, kWhite, 1.0f);
    const auto sq   = layout_text(font, "A", Anchor::TopLeft, Vec2{0, 0}, 0.1f, 1.0f, kWhite, 1.0f);
    REQUIRE(wide[0].rect.w == Approx(sq[0].rect.w / 2.0f)); // sx = scale/aspect
    REQUIRE(wide[0].rect.h == Approx(sq[0].rect.h));        // height unaffected by aspect
}

TEST_CASE("layout_text right-anchors the measured box against the right edge", "[ui][text]")
{
    const FontAtlas font = fixture_font();
    const auto quads = layout_text(font, "AB", Anchor::TopRight, Vec2{0, 0}, 0.1f, 1.0f, kWhite, 1.0f);
    REQUIRE(!quads.empty());
    // The last glyph's right edge sits at/inside the screen's right edge.
    const auto& last = quads.back();
    REQUIRE(last.rect.x + last.rect.w <= Approx(1.0f).margin(0.02f));
    REQUIRE(last.rect.x + last.rect.w > 0.85f);
}

TEST_CASE("layout_text ignores codepoints absent from the atlas", "[ui][text]")
{
    const FontAtlas font = fixture_font();
    // '\t' (0x09) is outside ASCII 32..126, so it is not in the atlas.
    const auto quads = layout_text(font, "A\tB", Anchor::TopLeft, Vec2{0, 0}, 0.1f, 1.0f, kWhite, 1.0f);
    REQUIRE(quads.size() == 2); // only A and B
}
```

Add to `tests/CMakeLists.txt` (near the other `unit/ui/*` entries):

```cmake
  unit/ui/text_layout_test.cpp
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -20`
Expected: FAIL — `engine/ui/text_layout.hpp` not found.

- [ ] **Step 3: Write the implementation**

Create `engine/ui/text_layout.hpp`:

```cpp
#pragma once

#include <string>
#include <vector>

#include "engine/ui/font_atlas.hpp"
#include "engine/ui/ui_rect.hpp"
#include "foundation/math/vec2.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::engine::ui
{
struct TextQuad
{
    Rect  rect;
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    mosaic::foundation::math::Vec3 color;
    float alpha = 1.0f;
};

// Single-line, left-to-right layout in normalized screen space. See the U2b spec.
inline std::vector<TextQuad> layout_text(const FontAtlas& font, const std::string& text,
                                         Anchor anchor, mosaic::foundation::math::Vec2 pos,
                                         float height, float aspect,
                                         mosaic::foundation::math::Vec3 color, float alpha)
{
    std::vector<TextQuad> out;
    if (font.line_height <= 0.0f || aspect <= 0.0f) { return out; }

    const float scale = height / font.line_height; // atlas px -> normalized Y
    const float sx    = scale / aspect;            // atlas px -> normalized X

    // Measure total advance width (present glyphs only).
    float total_adv = 0.0f;
    for (unsigned char c : text)
    {
        const auto it = font.glyphs.find(static_cast<std::uint32_t>(c));
        if (it != font.glyphs.end()) { total_adv += it->second.advance; }
    }
    const float w = total_adv * sx;

    const Rect box = resolve_rect(anchor, pos, mosaic::foundation::math::Vec2{w, height});
    float pen_x = box.x;
    const float baseline = box.y + font.ascent * scale;

    for (unsigned char c : text)
    {
        const auto it = font.glyphs.find(static_cast<std::uint32_t>(c));
        if (it == font.glyphs.end()) { continue; } // skip unknown codepoints entirely
        const Glyph& g = it->second;
        if (g.width > 0.0f && g.height > 0.0f)
        {
            TextQuad q;
            q.rect = Rect{pen_x + g.x_off * sx, baseline + g.y_off * scale,
                          g.width * sx, g.height * scale};
            q.u0 = g.u0; q.v0 = g.v0; q.u1 = g.u1; q.v1 = g.v1;
            q.color = color;
            q.alpha = alpha;
            out.push_back(q);
        }
        pen_x += g.advance * sx;
    }
    return out;
}
} // namespace mosaic::engine::ui
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -5 && ./build/debug/tests/mosaic_unit_tests "[ui][text]" 2>&1 | tail -8`
Expected: the 5 `[ui][text]` cases PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/ui/text_layout.hpp tests/unit/ui/text_layout_test.cpp tests/CMakeLists.txt
git commit -m "feat(ui): pure single-line text layout (aspect-correct glyph quads)"
```

---

### Task 2: `UiText` reflected component

**Files:**
- Create: `engine/ui/ui_text.hpp`
- Create: `tests/unit/ui/ui_text_reflection_test.cpp`
- Modify: `engine/reflection/builtin_components.hpp` (register `UiText`)
- Modify: `tests/CMakeLists.txt` (add the test source)

**Interfaces:**
- Consumes: `ui` namespace, `AssetID`, `Vec2`/`Vec3`.
- Produces: `struct mosaic::engine::ui::UiText { AssetID font; std::string text; int anchor; Vec2 pos; float height; Vec3 color; float alpha; int order; };` registered `"UiText"`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/ui/ui_text_reflection_test.cpp` (modeled on `ui_panel_reflection_test.cpp` + `decal_test.cpp` for the `AssetID` field):

```cpp
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/ui/ui_text.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reference.hpp"
#include "foundation/serialization/reflect_value.hpp"

using Catch::Approx;
using mosaic::engine::ui::UiText;
using mosaic::foundation::math::Vec2;
using mosaic::foundation::math::Vec3;
using mosaic::foundation::serialization::AssetID;

TEST_CASE("UiText round-trips through reflection", "[ui][reflection]")
{
    UiText t;
    t.font   = AssetID::from_string("00000000-0000-0000-0000-0000000000f7");
    t.text   = "Score: 10";
    t.anchor = 2; // TopRight
    t.pos    = Vec2{0.02f, 0.03f};
    t.height = 0.07f;
    t.color  = Vec3{0.9f, 0.8f, 0.1f};
    t.alpha  = 0.75f;
    t.order  = 3;

    const auto value = mosaic::foundation::serialization::to_value(t);
    UiText back;
    mosaic::foundation::serialization::from_value(value, back);

    REQUIRE(back.font == t.font);
    REQUIRE(back.text == "Score: 10");
    REQUIRE(back.anchor == 2);
    REQUIRE(back.pos.y == Approx(0.03f));
    REQUIRE(back.height == Approx(0.07f));
    REQUIRE(back.color.x == Approx(0.9f));
    REQUIRE(back.alpha == Approx(0.75f));
    REQUIRE(back.order == 3);
}

TEST_CASE("UiText is a registered built-in component", "[ui][reflection]")
{
    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    REQUIRE(types.has("UiText"));
}
```

Add to `tests/CMakeLists.txt`:

```cmake
  unit/ui/ui_text_reflection_test.cpp
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -20`
Expected: FAIL — `engine/ui/ui_text.hpp` not found / `types.has("UiText")` false.

- [ ] **Step 3: Write the implementation**

Create `engine/ui/ui_text.hpp`:

```cpp
#pragma once

#include <string>

#include "foundation/math/vec2.hpp"
#include "foundation/math/vec3.hpp"
#include "foundation/serialization/reference.hpp" // AssetID

namespace mosaic::engine::ui
{
using mosaic::foundation::math::Vec2;
using mosaic::foundation::math::Vec3;

// A single line of screen-space HUD text. `font` is a "font" AssetID (FontAtlas).
// anchor is an int cast to ui::Anchor (clamped to TopLeft if out of range). pos is a
// normalized offset from the anchor; height is the line height as a fraction of screen
// height. A normal component: scripts can change `text`, `color`, `alpha`, `pos`.
struct UiText
{
    mosaic::foundation::serialization::AssetID font;
    std::string text;
    int   anchor = 0;
    Vec2  pos    = Vec2{0.0f, 0.0f};
    float height = 0.05f;
    Vec3  color  = Vec3{1.0f, 1.0f, 1.0f};
    float alpha  = 1.0f;
    int   order  = 0;
};

template <class V>
void reflect(V& visitor, UiText& t)
{
    visitor.member("font", t.font);
    visitor.member("text", t.text);
    visitor.member("anchor", t.anchor);
    visitor.member("pos", t.pos);
    visitor.member("height", t.height);
    visitor.member("color", t.color);
    visitor.member("alpha", t.alpha);
    visitor.member("order", t.order);
}
} // namespace mosaic::engine::ui
```

Register it in `engine/reflection/builtin_components.hpp` — add the include near the
other UI include and the registration after the `UiPanel` line:

```cpp
#include "engine/ui/ui_text.hpp"
```
```cpp
    types.register_component<mosaic::engine::ui::UiText>(registry, "UiText");
```

> If `AssetID` reflection needs the same include the `Decal` component uses, check
> `engine/vfx/decal.hpp` — `UiText` mirrors its `AssetID` member exactly.

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -5 && ./build/debug/tests/mosaic_unit_tests "[ui][reflection]" 2>&1 | tail -8`
Expected: the `[ui][reflection]` cases (incl. the U1 `UiPanel` ones) PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/ui/ui_text.hpp engine/reflection/builtin_components.hpp \
        tests/unit/ui/ui_text_reflection_test.cpp tests/CMakeLists.txt
git commit -m "feat(ui): reflected UiText component, registered as \"UiText\""
```

---

### Task 3: `draw_ui_text` seam + `FontTextureCache` + RenderSystem text pass

**Files:**
- Create: `engine/render/font_texture_cache.hpp`
- Modify: `engine/render/render_device.hpp` (`UiGlyphQuad` + `draw_ui_text`)
- Modify: `engine/render/render_system.hpp` (text pass + cache member)
- Modify: `tests/unit/render/render_system_test.cpp` (fake `draw_ui_text` + tests)

**Interfaces:**
- Consumes: `layout_text` (Task 1), `UiText` (Task 2), `FontAtlas` (U2a), `TextureCache` pattern.
- Produces:
  - `struct mosaic::engine::render::UiGlyphQuad { ui::Rect rect; float u0,v0,u1,v1; Vec3 color; float alpha; };`
  - `virtual void RenderDevice::draw_ui_text(const std::vector<UiGlyphQuad>&, TextureHandle atlas) {}`
  - `class mosaic::engine::render::FontTextureCache { TextureHandle get(AssetID, AssetRegistry&, RenderDevice&); };`

> Note: `FontTextureCache` lives in `engine/render/` (next to `texture_cache.hpp`), not
> `engine/ui/` as the spec sketched — this keeps the dependency direction render→ui
> (render already includes `engine/ui/*`), avoiding ui→render. Same API.

- [ ] **Step 1: Write the failing tests**

In `tests/unit/render/render_system_test.cpp`, add to `FakeRenderDevice` (near the `ui_draws` capture from U1):

```cpp
    struct UiTextDrawCall { std::vector<mosaic::engine::render::UiGlyphQuad> quads;
                            mosaic::engine::render::TextureHandle atlas; };
    std::vector<UiTextDrawCall> ui_text_draws;
    void draw_ui_text(const std::vector<mosaic::engine::render::UiGlyphQuad>& quads,
                      mosaic::engine::render::TextureHandle atlas) override
    {
        ui_text_draws.push_back({quads, atlas});
    }
```

Add includes near the others:

```cpp
#include "app/asset/font_cook.hpp"   // read_ttf for a real FontAtlas
#include "app/run/scene_io.hpp"
#include "engine/render/font_texture_cache.hpp"
#include "engine/ui/font_atlas.hpp"
#include "engine/ui/ui_text.hpp"
```

Add these cases (register a real baked `FontAtlas` in the registry under an id, attach a
`UiText` referencing it):

```cpp
namespace
{
mosaic::foundation::serialization::AssetID register_font(AssetRegistry& registry)
{
    const auto bytes = mosaic::app::load_scene_file(
        std::string(MOSAIC_TEST_DIR) + "/fixtures/LiberationSans-Regular.ttf");
    REQUIRE(bytes.has_value());
    auto font = mosaic::app::read_ttf(*bytes, 48.0f);
    REQUIRE(font.has_value());
    const auto id = mosaic::foundation::serialization::AssetID::from_string(
        "00000000-0000-0000-0000-0000000000fa");
    registry.register_resource<mosaic::engine::ui::FontAtlas>(
        mosaic::engine::assets::AssetMetadata{id, "font"},
        std::make_shared<mosaic::engine::ui::FontAtlas>(*font));
    return id;
}
} // namespace

TEST_CASE("RenderSystem emits glyph quads for a UiText with a registered font", "[render][ui]")
{
    using mosaic::engine::ui::UiText;
    AssetRegistry registry;
    const auto font_id = register_font(registry);
    World world;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 5});

    auto e = world.create_entity();
    UiText t; t.font = font_id; t.text = "AB"; t.height = 0.1f;
    world.add_component<UiText>(e, t);

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.ui_text_draws.size() == 1);
    REQUIRE(device.ui_text_draws[0].quads.size() == 2); // A, B
    REQUIRE(device.ui_text_draws[0].atlas != 0);        // atlas uploaded
}

TEST_CASE("RenderSystem skips UiText that is empty, transparent, or unresolved", "[render][ui]")
{
    using mosaic::engine::ui::UiText;
    AssetRegistry registry;
    const auto font_id = register_font(registry);
    World world;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 5});

    auto empty = world.create_entity();
    UiText a; a.font = font_id; a.text = ""; world.add_component<UiText>(empty, a);
    auto clear = world.create_entity();
    UiText b; b.font = font_id; b.text = "X"; b.alpha = 0.0f; world.add_component<UiText>(clear, b);
    auto nofont = world.create_entity();
    UiText c; c.text = "X"; world.add_component<UiText>(nofont, c); // nil font

    FakeRenderDevice device;
    rs.render(world, device, registry);
    REQUIRE(device.ui_text_draws.empty());
}

TEST_CASE("FontTextureCache uploads a font atlas at most once", "[render][ui]")
{
    AssetRegistry registry;
    const auto font_id = register_font(registry);
    FakeRenderDevice device;
    mosaic::engine::render::FontTextureCache cache;

    const auto h1 = cache.get(font_id, registry, device);
    const auto h2 = cache.get(font_id, registry, device);
    REQUIRE(h1 != 0);
    REQUIRE(h1 == h2);
    REQUIRE(cache.get(mosaic::foundation::serialization::AssetID{}, registry, device) == 0);
}
```

> The `FakeRenderDevice::create_texture` in this file already returns a fresh non-zero
> handle for a non-empty `Image` (used by the decal tests); the atlas is non-empty, so it
> yields a stable handle — no fake change needed beyond the `draw_ui_text` override.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -20`
Expected: FAIL — `font_texture_cache.hpp` / `UiGlyphQuad` / `draw_ui_text` undefined.

- [ ] **Step 3: Write the implementation**

Create `engine/render/font_texture_cache.hpp` (mirrors `texture_cache.hpp`):

```cpp
#pragma once

#include <unordered_map>

#include "engine/assets/asset_registry.hpp"
#include "engine/render/render_device.hpp"
#include "engine/ui/font_atlas.hpp"
#include "foundation/serialization/reference.hpp"

namespace mosaic::engine::render
{
// Resolves a "font" AssetID to a device TextureHandle by uploading the FontAtlas's
// RGBA8 atlas at most once per id. Nil/missing/failed -> 0. Mirrors TextureCache.
class FontTextureCache
{
public:
    TextureHandle get(mosaic::foundation::serialization::AssetID id,
                      mosaic::engine::assets::AssetRegistry& registry, RenderDevice& device)
    {
        if (id == mosaic::foundation::serialization::AssetID{}) { return 0; }
        auto it = cache_.find(id);
        if (it != cache_.end()) { return it->second; }
        TextureHandle handle = 0;
        if (std::shared_ptr<mosaic::engine::ui::FontAtlas> font =
                registry.load<mosaic::engine::ui::FontAtlas>(id))
        {
            handle = device.create_texture(font->atlas);
        }
        cache_.emplace(id, handle);
        return handle;
    }

private:
    std::unordered_map<mosaic::foundation::serialization::AssetID, TextureHandle> cache_;
};
} // namespace mosaic::engine::render
```

In `engine/render/render_device.hpp`, add includes + `UiGlyphQuad` near `UiQuad` (U1),
and the seam near `draw_ui`:

```cpp
// One textured glyph quad in normalized screen space; samples the bound font atlas.
struct UiGlyphQuad
{
    mosaic::engine::ui::Rect       rect;
    float                          u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    mosaic::foundation::math::Vec3 color;
    float                          alpha = 1.0f;
};
```
```cpp
    // Draw alpha-blended textured glyph quads sampling `atlas` (coverage in alpha).
    // Default no-op (like draw_ui). Called in the HUD pass after draw_ui.
    virtual void draw_ui_text(const std::vector<UiGlyphQuad>& /*quads*/,
                              TextureHandle /*atlas*/) {}
```

In `engine/render/render_system.hpp`, add includes:

```cpp
#include "engine/render/font_texture_cache.hpp"
#include "engine/ui/text_layout.hpp"
#include "engine/ui/ui_text.hpp"
```

Add a member to `RenderSystem` (near any existing caches):

```cpp
    FontTextureCache font_textures_;
```

Add the text pass **immediately after** the U1 panel (`draw_ui`) block:

```cpp
        // HUD text pass: single-line UiText, drawn over panels (topmost). Uses aspect_.
        {
            world.for_each<mosaic::engine::ui::UiText>(
                [&](mosaic::foundation::EntityID, mosaic::engine::ui::UiText& t)
                {
                    if (t.alpha <= 0.0f || t.text.empty()) { return; }
                    std::shared_ptr<mosaic::engine::ui::FontAtlas> font =
                        registry.load<mosaic::engine::ui::FontAtlas>(t.font);
                    if (!font) { return; }
                    const TextureHandle atlas = font_textures_.get(t.font, registry, device);
                    if (atlas == 0) { return; }
                    const std::vector<mosaic::engine::ui::TextQuad> tq =
                        mosaic::engine::ui::layout_text(*font, t.text, to_anchor(t.anchor),
                                                        t.pos, t.height, aspect_, t.color, t.alpha);
                    if (tq.empty()) { return; }
                    std::vector<UiGlyphQuad> quads;
                    quads.reserve(tq.size());
                    for (const auto& q : tq)
                    {
                        quads.push_back(UiGlyphQuad{q.rect, q.u0, q.v0, q.u1, q.v1, q.color, q.alpha});
                    }
                    device.draw_ui_text(quads, atlas);
                });
        }
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build --preset debug --target mosaic_unit_tests 2>&1 | tail -5 && ./build/debug/tests/mosaic_unit_tests "[render],[ui]" 2>&1 | tail -8`
Expected: the new `[render][ui]` text cases PASS; existing render/ui tests still PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/render/font_texture_cache.hpp engine/render/render_device.hpp \
        engine/render/render_system.hpp tests/unit/render/render_system_test.cpp
git commit -m "feat(ui): draw_ui_text seam + FontTextureCache + RenderSystem text pass"
```

---

### Task 4: Vulkan textured glyph pipeline (`draw_ui_text`)

**Files:**
- Create: `modules/render_vulkan/shaders/ui_text.vert`, `ui_text.frag`
- Modify: `modules/render_vulkan/vulkan_render_device.cpp` (pipeline + `draw_ui_text`)
- Modify: `modules/render_vulkan/CMakeLists.txt` (compile the shaders)

**Interfaces:**
- Consumes: `draw_ui_text(const std::vector<UiGlyphQuad>&, TextureHandle)` (Task 3).

**Templates in the same file:** the U1 `ui` pipeline (`UiPush`, `create_ui_pipeline()`,
the `draw_ui` override, `ui_pipeline_`/`vp_ui_pipeline_`/`ui_pipeline_layout_` members +
teardown) gives the push-constant-rect + shader-quad + swapchain/viewport structure; the
**decal** pipeline (`create_decal_pipeline()`, `decal_pipeline_layout_` built with
`texture_set_layout_`, and the `vkCmdBindDescriptorSets(..., &textures_[i].set, ...)`
loop in `draw_decals`) gives the atlas-texture descriptor. Mirror both with `ui_text_`
names.

- [ ] **Step 1: Write the shaders**

Create `modules/render_vulkan/shaders/ui_text.vert`:

```glsl
#version 450

layout(push_constant) uniform Push {
    vec4 rect;   // x, y, w, h in [0,1], top-left origin
    vec4 uv;     // u0, v0, u1, v1
    vec4 color;  // rgb + alpha
} pc;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;

vec2 corner(int i) {
    if (i == 0) return vec2(0.0, 0.0);
    if (i == 1) return vec2(1.0, 0.0);
    if (i == 2) return vec2(0.0, 1.0);
    if (i == 3) return vec2(1.0, 0.0);
    if (i == 4) return vec2(1.0, 1.0);
    return vec2(0.0, 1.0);
}

void main() {
    vec2 c = corner(gl_VertexIndex);
    float nx = pc.rect.x + c.x * pc.rect.z;
    float ny = pc.rect.y + c.y * pc.rect.w;
    gl_Position = vec4(nx * 2.0 - 1.0, ny * 2.0 - 1.0, 0.0, 1.0);
    v_uv = vec2(mix(pc.uv.x, pc.uv.z, c.x), mix(pc.uv.y, pc.uv.w, c.y));
    v_color = pc.color;
}
```

Create `modules/render_vulkan/shaders/ui_text.frag`:

```glsl
#version 450

layout(set = 0, binding = 0) uniform sampler2D atlas;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 0) out vec4 o_color;

void main() {
    float cov = texture(atlas, v_uv).a; // coverage in the atlas alpha channel
    o_color = vec4(v_color.rgb, cov * v_color.a);
}
```

> The atlas descriptor's `set`/`binding` must match `texture_set_layout_` (what the decal
> shader uses). If the decal frag samples at a different `set`/`binding`, copy that exact
> layout qualifier — grep `modules/render_vulkan/shaders/decal.frag`.

- [ ] **Step 2: Wire the shader compile + confirm SPIR-V builds**

In `modules/render_vulkan/CMakeLists.txt`, extend the `foreach(_stage ...)` list to add
`ui_text.vert ui_text.frag`.

Run: `cmake --build --preset debug --target mosaic_render_vulkan 2>&1 | tail -20`
Expected: `ui_text.vert`/`ui_text.frag` compile to SPIR-V with no glslang errors (the
`draw_ui_text` override is still the default no-op — link succeeds).

- [ ] **Step 3: Implement the pipeline + `draw_ui_text` override**

In `vulkan_render_device.cpp`, mirroring the `ui` + `decal` pipelines:

1. Push-constant struct near `UiPush`:

```cpp
struct UiTextPush
{
    float rect[4];
    float uv[4];
    float color[4];
};
static_assert(sizeof(UiTextPush) == 48, "ui-text push constant must match the shader");
```

2. Members near `ui_pipeline_`:

```cpp
    VkPipeline       ui_text_pipeline_ = VK_NULL_HANDLE;
    VkPipeline       vp_ui_text_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout ui_text_pipeline_layout_ = VK_NULL_HANDLE;
```

3. Call `create_ui_text_pipeline();` right after `create_ui_pipeline();`.

4. `create_ui_text_pipeline()` = a copy of `create_ui_pipeline()` with two changes: the
   pipeline layout includes `texture_set_layout_` (`setLayoutCount = 1; pSetLayouts =
   &texture_set_layout_;` — copy from `create_decal_pipeline()`) alongside the
   `UiTextPush` push-constant range; and it loads `ui_text.vert.spv`/`ui_text.frag.spv`.
   Same alpha blend on / depth off / no vertex input / TRIANGLE_LIST / swapchain +
   viewport variants (`render_pass_` then `vp_render_pass_`).

5. Teardown next to the `ui_` destroys:

```cpp
            if (vp_ui_text_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, vp_ui_text_pipeline_, nullptr); }
            if (ui_text_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, ui_text_pipeline_, nullptr); }
            if (ui_text_pipeline_layout_ != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, ui_text_pipeline_layout_, nullptr); }
```

6. `draw_ui_text` override (near `draw_ui`), binding the atlas descriptor like
   `draw_decals`:

```cpp
    void draw_ui_text(const std::vector<mosaic::engine::render::UiGlyphQuad>& quads,
                      mosaic::engine::render::TextureHandle atlas) override
    {
        if (quads.empty()) { return; }
        if (!viewport_pass_open_) { ensure_swapchain_pass(); }
        if (frame_skipped_ || textures_.empty()) { return; }

        const std::uint32_t tex_index =
            (atlas == 0 || atlas > textures_.size()) ? 0u : atlas - 1u;
        VkPipeline pipe = viewport_pass_open_ ? vp_ui_text_pipeline_ : ui_text_pipeline_;
        vkCmdBindPipeline(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        vkCmdBindDescriptorSets(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                ui_text_pipeline_layout_, 0, 1, &textures_[tex_index].set, 0, nullptr);
        for (const auto& q : quads)
        {
            UiTextPush pc{};
            pc.rect[0] = q.rect.x; pc.rect[1] = q.rect.y; pc.rect[2] = q.rect.w; pc.rect[3] = q.rect.h;
            pc.uv[0] = q.u0; pc.uv[1] = q.v0; pc.uv[2] = q.u1; pc.uv[3] = q.v1;
            pc.color[0] = q.color.x; pc.color[1] = q.color.y; pc.color[2] = q.color.z; pc.color[3] = q.alpha;
            vkCmdPushConstants(cmd_[frame_], ui_text_pipeline_layout_,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(UiTextPush), &pc);
            vkCmdDraw(cmd_[frame_], 6, 1, 0, 0);
        }
    }
```

- [ ] **Step 4: Build + run headless to verify no validation errors**

Run:
```bash
cmake --build --preset debug 2>&1 | tail -10
./build/debug/app/mosaic run --window --ticks 3 2>&1 | tail -20; echo "exit: $?"
```
Expected: build succeeds; exit 0 with no Vulkan validation errors. (No `UiText` in the
scene yet — Task 5 adds the demo. This confirms the pipeline is created/destroyed
cleanly.) Do not attempt screenshots (headless sandbox).

- [ ] **Step 5: Commit**

```bash
git add modules/render_vulkan/shaders/ui_text.vert modules/render_vulkan/shaders/ui_text.frag \
        modules/render_vulkan/vulkan_render_device.cpp modules/render_vulkan/CMakeLists.txt
git commit -m "feat(ui): Vulkan textured glyph pipeline for draw_ui_text (atlas sampler)"
```

---

### Task 5: Built-in font + demo `UiText`

**Files:**
- Create: `assets/fonts/DemoSans.ttf` (+ `assets/fonts/DemoSans-License.txt`)
- Modify: `app/CMakeLists.txt` (`MOSAIC_FONT_DIR` define on `mosaic_app`)
- Modify: `app/run/demo_scene.hpp` (`kBuiltinFontId`, `register_builtin_font`, demo `UiText`)

**Interfaces:**
- Consumes: `read_ttf` (U2a), `register_resource<FontAtlas>`, `UiText` (Task 2).

- [ ] **Step 1: Bundle the demo font + the compile define**

```bash
mkdir -p assets/fonts
cp /usr/share/fonts/liberation/LiberationSans-Regular.ttf assets/fonts/DemoSans.ttf
```

Create `assets/fonts/DemoSans-License.txt`:

```text
DemoSans.ttf — Liberation Sans, © Red Hat, Inc., SIL Open Font License 1.1.
Full license: https://scripts.sil.org/OFL
Bundled as the built-in HUD demo font.
```

In `app/CMakeLists.txt`, add after the `mosaic_app` target is defined (so all TUs that
compile `demo_scene.hpp`, including the `mosaic` executable that links `mosaic_app`, see
it — use PUBLIC):

```cmake
target_compile_definitions(mosaic_app PUBLIC
  MOSAIC_FONT_DIR="${CMAKE_SOURCE_DIR}/assets/fonts")
```

- [ ] **Step 2: Add the built-in font id + registration + demo text**

In `app/run/demo_scene.hpp`:

Add the includes near the other UI/asset includes:

```cpp
#include "app/asset/font_cook.hpp"
#include "app/run/scene_io.hpp"
#include "engine/ui/ui_text.hpp"
```

Add a stable built-in font id near `kStripesTextureId`, using the **same
`std::array<std::uint8_t,16>` fixed-id form** the demo textures use (a unique lead byte
`0xF0`, distinct from the textures' `0xA1`/`0xB2`/checker and the tone's `0xA0`):

```cpp
inline const mosaic::foundation::serialization::AssetID kBuiltinFontId{
    std::array<std::uint8_t, 16>{0xF0}};
```

Add a helper that bakes + registers the built-in font, called from the same place the
demo textures/meshes are registered in `setup_runtime` (right after
`register_builtin_meshes(registry);` / the `register_texture(...)` block):

```cpp
inline void register_builtin_font(mosaic::engine::assets::AssetRegistry& registry)
{
    const std::optional<std::string> bytes =
        mosaic::app::load_scene_file(std::string(MOSAIC_FONT_DIR) + "/DemoSans.ttf");
    if (!bytes) { return; } // no demo text if the bundled font is missing
    const std::optional<mosaic::engine::ui::FontAtlas> font =
        mosaic::app::read_ttf(*bytes, 48.0f);
    if (!font) { return; }
    registry.register_resource<mosaic::engine::ui::FontAtlas>(
        mosaic::engine::assets::AssetMetadata{kBuiltinFontId, "font"},
        std::make_shared<mosaic::engine::ui::FontAtlas>(*font));
}
```

Call it in `setup_runtime` next to `register_builtin_meshes(registry);`:

```cpp
    register_builtin_font(registry);
```

Attach a demo `UiText` to the **existing camera entity** in `build_default_scene` (next
to the `UiPanel`/`ScreenEffect` attaches, so no new entity):

```cpp
    world.add_component<mosaic::engine::ui::UiText>(camera,
        mosaic::engine::ui::UiText{
            kBuiltinFontId, "MOSAIC",
            static_cast<int>(mosaic::engine::ui::Anchor::TopLeft),
            mosaic::foundation::math::Vec2{0.03f, 0.10f}, 0.06f,
            mosaic::foundation::math::Vec3{1.0f, 1.0f, 1.0f}, 1.0f, 1});
```

> Confirm the `UiText` aggregate-init order matches the struct
> `{font, text, anchor, pos, height, color, alpha, order}`. `kBuiltinFontId` is defined
> in this header, so it is in scope. Place the panel below the HUD bar (pos.y 0.10) so
> they don't overlap.

- [ ] **Step 3: Build (app + tests) + full suite + headless run**

Run:
```bash
cmake --build --preset debug 2>&1 | tail -5
ctest --preset debug 2>&1 | tail -4
./build/debug/app/mosaic run --window --ticks 3 2>&1 | tail -5; echo "exit: $?"
```
Expected: all tests PASS (default-scene count tests unchanged — `UiText` rides on the
camera entity, no new entity; if any test asserts an exact *registered-asset* count,
update it for the one new built-in font asset); `mosaic run --window` exits 0 with no
validation errors. The `mosaic` binary is rebuilt above, so the demo text is live.

> If a default-scene test fails on an entity count, re-check the `UiText` is attached to
> `camera`, not a new `create_entity()`.

- [ ] **Step 4: Commit**

```bash
git add assets/fonts/DemoSans.ttf assets/fonts/DemoSans-License.txt \
        app/CMakeLists.txt app/run/demo_scene.hpp
git commit -m "feat(ui): built-in demo font + UiText \"MOSAIC\" on the default-scene camera"
```

---

### Task 6: Verify full suite + update the build handoff

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
1. Update the **Last updated** line (`2026-09-08`) with a "UI — U2b (HUD text rendering) DONE" summary: reflected `UiText` + a pure aspect-correct `layout_text` + a `draw_ui_text` seam + a RenderSystem text pass (over panels) + a Vulkan textured glyph pipeline (atlas sampler) + a built-in demo font (`UiText "MOSAIC"` on the camera). Note **U2 (text) is now complete** (U2a asset + U2b render). GPU visual maintainer-verified.
2. In the UI-decomposition line, mark **U2 done** and point at U3 (images/sprites) as next; link the U2b spec + this plan.

- [ ] **Step 3: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "docs: mark UI U2b (HUD text) done; U2 complete, U3 next"
```

---

## Self-Review

**Spec coverage:**
- `text_layout.hpp` pure `layout_text` (aspect-correct, single-line, skip-missing) → Task 1. ✓
- `ui_text.hpp` reflected `UiText` + `"UiText"` registration → Task 2. ✓
- `UiGlyphQuad` + `draw_ui_text` seam → Task 3. ✓
- `FontTextureCache` (upload-once) → Task 3 (in `engine/render/`, noted deviation from spec's `engine/ui/`). ✓
- RenderSystem text pass after panels, using `aspect_` → Task 3. ✓
- Vulkan textured glyph pipeline (push rect+uv+color, atlas descriptor, swapchain+viewport) → Task 4. ✓
- Built-in font via `MOSAIC_FONT_DIR` baked at startup + demo `UiText` on the camera → Task 5. ✓
- Tests: layout math (Task 1), reflection+registration (Task 2), render pass + font-cache upload-once (Task 3), default-scene counts (Task 5). ✓

**Placeholder scan:** no TBD/TODO; every code step has real code. Task 4 references the concrete `ui`+`decal` templates in-file (both present on this branch) rather than reproducing the full pipeline body — the established approach for GPU-pipeline tasks here, with all names/structs given.

**Type consistency:** `TextQuad{rect,u0,v0,u1,v1,color,alpha}` (ui) and `UiGlyphQuad{rect,u0,v0,u1,v1,color,alpha}` (render) are field-parallel and converted 1:1 in Task 3. `layout_text(const FontAtlas&, const std::string&, Anchor, Vec2, float height, float aspect, Vec3, float) -> vector<TextQuad>` consistent (Tasks 1, 3). `draw_ui_text(const vector<UiGlyphQuad>&, TextureHandle)` consistent (Tasks 3, 4). `FontTextureCache::get(AssetID, AssetRegistry&, RenderDevice&) -> TextureHandle` consistent (Tasks 3). `UiText{font,text,anchor,pos,height,color,alpha,order}` consistent (Tasks 2, 3, 5). `kBuiltinFontId` + `"font"` asset type consistent (Task 5 + U2a). `UiTextPush` 48 bytes matches the shader's 3×vec4 (Task 4).
