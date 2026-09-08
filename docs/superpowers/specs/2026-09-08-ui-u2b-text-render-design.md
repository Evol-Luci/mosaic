# UI U2b — HUD text rendering — Design

## Goal (Phase 4, UI pillar — slice 2b)

Second half of **U2 (text)**: render **single-line HUD text**. A reflected `UiText`
component (a string + a `FontAtlas` `AssetID` + anchor/pos/height/color/alpha/order) is
laid out into textured glyph quads and drawn over the HUD, sampling the U2a font atlas.
Completes U2: **U2a** produced the loadable font asset; **U2b** puts words on screen.

Builds on **U1** (the `draw_ui` seam pattern, `resolve_rect`, the RenderSystem HUD pass)
and **U2a** (the `FontAtlas` resource). Aspect-correct (glyphs keep their true
proportions via `RenderSystem`'s existing `aspect_`). Single line only — `\n`/wrapping,
kerning, and non-ASCII are follow-ups.

Same GPU caveat as the other visual slices: headless layout/wiring + build +
`mosaic run --window` exit 0 is what the sandbox verifies; **the on-screen text is the
maintainer's hardware check**.

## Design

### `engine/ui/text_layout.hpp` — neutral layout (pure)

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
// One laid-out glyph: a normalized screen rect + its atlas UVs + color/alpha.
struct TextQuad
{
    Rect  rect;
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    mosaic::foundation::math::Vec3 color;
    float alpha = 1.0f;
};

// Lay out a single line, left-to-right, in normalized screen space.
//   height : line height as a fraction of screen height (drives the glyph scale).
//   aspect : screen width/height; corrects glyph width/advance in normalized-X so
//            glyphs are not stretched.
//   anchor/pos : place the measured text bounding box exactly like resolve_rect places
//                a rect of size {measured_width, height}.
// Codepoints absent from the atlas are skipped (no quad, no advance). Empty-quad glyphs
// (e.g. space) advance the pen but emit no TextQuad.
std::vector<TextQuad> layout_text(const FontAtlas& font, const std::string& text,
                                  Anchor anchor, mosaic::foundation::math::Vec2 pos,
                                  float height, float aspect,
                                  mosaic::foundation::math::Vec3 color, float alpha);
}
```

**Math** (`scale` maps atlas pixels → normalized-Y; `sx` → normalized-X):

```
scale = height / font.line_height
sx    = scale / aspect
W     = (Σ glyph.advance over present glyphs) * sx          // measured line width
box   = resolve_rect(anchor, pos, {W, height})              // top-left placement
pen_x = box.x
base  = box.y + font.ascent * scale                         // baseline Y (ascent > 0)
per glyph g present in the atlas:
  x = pen_x + g.x_off * sx
  y = base  + g.y_off * scale                               // g.y_off ≤ 0 → above baseline
  if g.width > 0 && g.height > 0:
    emit TextQuad{ {x, y, g.width*sx, g.height*scale}, g.u0,g.v0,g.u1,g.v1, color, alpha }
  pen_x += g.advance * sx
```

Pure → headless-testable: quad count for a known string, monotonic left-to-right x,
`sx` widening as `aspect` shrinks, right/center anchor placement, missing-glyph skip.

### `engine/ui/ui_text.hpp` — reflected component

```cpp
struct UiText
{
    mosaic::foundation::serialization::AssetID font;   // FontAtlas asset ("font" type)
    std::string text;                                  // ASCII, single line
    int   anchor = 0;                                  // ui::Anchor (0 = TopLeft)
    Vec2  pos    = Vec2{0, 0};                          // normalized offset from anchor
    float height = 0.05f;                               // line height, fraction of screen H
    Vec3  color  = Vec3{1, 1, 1};
    float alpha  = 1.0f;
    int   order  = 0;
};
```

Reflected + registered `"UiText"` (serializes, Inspector-editable, script-mutable — a
script can change `text`). `std::string` is a supported reflected field (same as
`ScriptComponent.source`). `anchor` clamps out-of-range → `TopLeft` (U1's `to_anchor`).

### `engine/ui/font_texture_cache.hpp` — atlas upload cache

`TextureCache` uploads a `render::Image` resolved by `AssetID`, but a `"font"` asset
resolves to a `FontAtlas` (whose `.atlas` **is** an `Image`). A parallel cache:

```cpp
class FontTextureCache
{
public:
    // Nil/missing/failed -> 0. Uploads font.atlas at most once per id.
    TextureHandle get(AssetID id, AssetRegistry& registry, RenderDevice& device);
private:
    std::unordered_map<AssetID, TextureHandle> cache_;
};
```

`get` does `registry.load<FontAtlas>(id)` → `device.create_texture(font->atlas)`, cached
by id — mirroring `TextureCache::get` exactly.

### `engine/render/render_device.hpp` — new seam

```cpp
struct UiGlyphQuad
{
    mosaic::engine::ui::Rect       rect;
    float                          u0, v0, u1, v1;
    mosaic::foundation::math::Vec3 color;
    float                          alpha = 1.0f;
};
// Draw alpha-blended textured glyph quads sampling `atlas` (coverage in alpha).
// Default no-op (like draw_ui / draw_decals).
virtual void draw_ui_text(const std::vector<UiGlyphQuad>& /*quads*/,
                          TextureHandle /*atlas*/) {}
```

### `engine/render/render_system.hpp` — text pass

A **text pass right after the panel (`draw_ui`) pass** — so text draws over panels
(unifying panels + text by a shared `order` is a documented follow-up). `RenderSystem`
gains a `FontTextureCache font_textures_` member. For each `UiText`:

```cpp
world.for_each<mosaic::engine::ui::UiText>([&](EntityID, ui::UiText& t) {
    if (t.alpha <= 0.0f || t.text.empty()) { return; }
    std::shared_ptr<ui::FontAtlas> font = registry.load<ui::FontAtlas>(t.font);
    if (!font) { return; }
    const TextureHandle atlas = font_textures_.get(t.font, registry, device);
    if (atlas == 0) { return; }
    std::vector<ui::TextQuad> tq = ui::layout_text(
        *font, t.text, to_anchor(t.anchor), t.pos, t.height, aspect_, t.color, t.alpha);
    if (tq.empty()) { return; }
    std::vector<UiGlyphQuad> quads;      // TextQuad -> UiGlyphQuad (same fields)
    quads.reserve(tq.size());
    for (const auto& q : tq)
        quads.push_back(UiGlyphQuad{q.rect, q.u0, q.v0, q.u1, q.v1, q.color, q.alpha});
    device.draw_ui_text(quads, atlas);
});
```

(`aspect_` is already set by the runtime and editor via `set_aspect`.) One
`draw_ui_text` call per `UiText` (each may use a different font); `order`-sorting across
multiple `UiText`s reuses the U1 pattern if needed — v1 draws them in iteration order.

### `modules/render_vulkan` — textured glyph pipeline

`ui_text.vert` / `ui_text.frag`, modeled on the U1 `ui` pipeline (shader-generated quad
from a push-constant rect) **plus** the decal pipeline's texture descriptor:

- **Push constant** per quad: `rect (vec4)` + `uv (vec4: u0,v0,u1,v1)` + `color (vec4:
  rgb + alpha)` = 48 bytes. Vertex shader builds the 6-vertex quad, maps rect→NDC (top-
  left origin, same as `ui.vert`), and interpolates UV between `(u0,v0)`→`(u1,v1)`.
- **Descriptor:** the atlas sampled via the existing `texture_set_layout_` (as decals do).
- **Fragment:** `float cov = texture(atlas, uv).a; out = vec4(color.rgb, cov * color.a);`
  standard src-alpha blend; depth test/write off (overlay).
- `draw_ui_text` binds the pipeline + the atlas descriptor set once, then loops quads
  (push constants + `vkCmdDraw(6,…)`), choosing the swapchain vs viewport variant on
  `viewport_pass_open_` — exactly like `draw_ui`.

### Demo — built-in font + `UiText` on the camera

The default scene has no font asset, so add a **built-in font** (analogous to the
built-in cube mesh / demo textures):

- Bundle a compact TTF in-repo (e.g. `assets/fonts/DemoSans.ttf`, a Liberation/OFL copy)
  and expose its directory to the app via a `MOSAIC_FONT_DIR` compile define (mirroring
  `MOSAIC_SHADER_DIR`).
- `register_builtin_font(AssetRegistry&)` (app tier, in the demo-scene setup) reads the
  bundled TTF via U2a's `read_ttf`, and `register_resource<FontAtlas>` under a fixed
  `kBuiltinFontId` (a stable `AssetID`).
- The default scene adds a `UiText` on the **existing camera entity** (like `UiPanel` /
  `ScreenEffect`), e.g. `text = "MOSAIC"`, top-left, `height ≈ 0.06`, so counts are
  unchanged.

## Testing (headless; GPU visual maintainer-verified)

- **`text_layout`** — a known short string (e.g. `"AB"`) at a fixed font/height/aspect:
  correct number of quads (present glyphs only), strictly increasing pen x, `sx`
  widening when `aspect < 1`, right-anchored text's box ending near `x = 1`, and a
  missing codepoint producing no quad. (Bake a `FontAtlas` in-test from the U2a fixture
  via `read_ttf`.)
- **`UiText` reflection** — round-trip (incl. `AssetID font` + `std::string text`) +
  registration under `"UiText"`.
- **`FontTextureCache`** — uploads once per id (second `get` returns the same handle,
  one `create_texture`); nil id → 0. (Reuse the render test's `FakeRenderDevice`.)
- **RenderSystem text pass** — a `FakeRenderDevice` capturing `draw_ui_text`: a `UiText`
  with a registered font produces glyph quads + a non-zero atlas handle; empty text and
  `alpha <= 0` produce no call; an unregistered font produces no call.
- **Default-scene counts** — unchanged (UiText on the camera). Confirm no registered-
  asset-count test breaks from the built-in font (adjust if one asserts an exact count).

Build + `mosaic run --window` exits 0 with no Vulkan validation errors is the
integration check; the on-screen text is the maintainer's hardware verification.

## Files

**New**
- `engine/ui/text_layout.hpp` — `TextQuad` + `layout_text` (pure).
- `engine/ui/ui_text.hpp` — reflected `UiText`.
- `engine/ui/font_texture_cache.hpp` — `FontTextureCache`.
- `modules/render_vulkan/shaders/ui_text.vert`, `ui_text.frag`.
- `assets/fonts/DemoSans.ttf` (+ its OFL license note).
- `tests/unit/ui/text_layout_test.cpp`, `tests/unit/ui/ui_text_reflection_test.cpp`
  (render-system + font-cache tests extend existing files).

**Modified**
- `engine/render/render_device.hpp` — `UiGlyphQuad` + `draw_ui_text` seam.
- `engine/render/render_system.hpp` — `FontTextureCache` member + text pass.
- `engine/reflection/builtin_components.hpp` — register `UiText`.
- `modules/render_vulkan/…` — glyph pipeline + `draw_ui_text` + shader-build wiring.
- `app/run/demo_scene.hpp` — `register_builtin_font` + `kBuiltinFontId` + demo `UiText`.
- `app/CMakeLists.txt` (or root) — `MOSAIC_FONT_DIR` define + bundled font.
- `tests/CMakeLists.txt` — new test sources.

## Non-goals / follow-ups

Multi-line + `\n` + word wrap; kerning pairs; non-ASCII/Unicode; multi-size/SDF crisp
scaling; unifying panels + text under a single `order`-sorted pass; a text-alignment
(left/center/right within a box) option distinct from anchor.
