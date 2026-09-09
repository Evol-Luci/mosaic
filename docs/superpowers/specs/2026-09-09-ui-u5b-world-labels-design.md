# UI U5b — World-space text labels — Design

## Goal (Phase 4, UI pillar — slice 5b, final)

Render **floating text in 3D**: a `WorldLabel` component draws camera-facing text at an
entity's world position — nameplates, damage numbers, object labels. This completes the
UI pillar (U1 HUD panels → U2 text → U3 images → U4 interaction → U5a hierarchy →
**U5b world-space labels**).

The distinctive, not-yet-covered capability: decals already give transform-oriented world
textured quads and particles give camera-facing billboards, but nothing renders **text**
in world space. U5b reuses the U2 font atlas + `layout_text`, places the glyphs on a
camera-facing billboard via the camera's right/up axes, and draws them with a world-space
glyph pipeline modeled on the ribbon pipeline (CPU-expanded world vertices + `view_proj`).

Base branch: `main` (U1–U5a merged). **Scope (YAGNI):** camera-facing only (no
transform-oriented), single line, single font, no background panel, no interaction, no
occlusion fade. Same GPU caveat: headless geometry/wiring + build + `mosaic run --window`
exit 0 is what the sandbox verifies; **the on-screen label is the maintainer's hardware
check**.

## Design

### `engine/ui/world_label.hpp` — reflected component

```cpp
#pragma once

#include <string>

#include "foundation/math/vec3.hpp"
#include "foundation/serialization/reference.hpp" // AssetID

namespace mosaic::engine::ui
{
using mosaic::foundation::math::Vec3;

// Camera-facing text floating at the entity's WorldTransform position (+ offset).
// size is the world-space line height. The entity needs a WorldTransform. Script-mutable
// (change `text`). Renders via the U2 font atlas (font AssetID of type "font").
struct WorldLabel
{
    mosaic::foundation::serialization::AssetID font;
    std::string text;
    float size   = 1.0f;                 // world-space line height (world units)
    Vec3  color  = Vec3{1.0f, 1.0f, 1.0f};
    float alpha  = 1.0f;
    Vec3  offset = Vec3{0.0f, 0.0f, 0.0f}; // world offset from the entity position
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

Registered `"WorldLabel"` in `builtin_components.hpp`.

### `engine/ui/world_text.hpp` — pure billboard builder

```cpp
struct WorldGlyphVertex
{
    mosaic::foundation::math::Vec3 position; // world space
    float u = 0, v = 0;                      // atlas UV
    mosaic::foundation::math::Vec3 color;
    float alpha = 1.0f;
};

// Expand `text` into camera-facing world-space glyph triangles (6 verts/glyph, LIST).
// Centered horizontally + vertically on anchor_world. cam_right/cam_up orient the
// billboard; size scales line-height to world units. Codepoints absent from the atlas
// are skipped. Empty / zero-metric fonts -> no verts.
std::vector<WorldGlyphVertex> build_world_text(
    const FontAtlas& font, const std::string& text,
    mosaic::foundation::math::Vec3 anchor_world,
    mosaic::foundation::math::Vec3 cam_right, mosaic::foundation::math::Vec3 cam_up,
    float size, mosaic::foundation::math::Vec3 color, float alpha);
```

**Implementation** reuses `layout_text` to get local glyph rects in "line-height units":

```
const auto quads = layout_text(font, text, Anchor::TopLeft, {0,0}, /*height*/1.0f,
                               /*aspect*/1.0f, color, alpha, Rect{0,0,1,1});
// Measure the text's local width/height to center on the anchor.
float max_x = 0; for q: max_x = max(max_x, q.rect.x + q.rect.w);   // ~ total width in units
const float cx0 = max_x * 0.5f;   // horizontal center
const float cy0 = 0.5f;           // vertical center (local box height ≈ 1 line)
for each quad q, for each of its 4 corners (lx, ly) (two triangles, 6 verts):
    // local (lx, ly): x right, y down, in line-height units
    const float ox = (lx - cx0) * size;   // right axis offset
    const float oy = (cy0 - ly) * size;   // up axis offset (flip y: local-down -> world-up)
    WorldGlyphVertex{ anchor_world + ox*cam_right + oy*cam_up, uv(lx,ly), color, alpha }
```

Pure → headless-testable (vertex count, centering, displacement along cam axes).

### `engine/render/render_device.hpp` — new seam

`RenderDevice` gains a default-no-op:

```cpp
// Draw camera-facing world-space text glyphs (pre-expanded world verts), sampling the
// font atlas. view_proj = projection * view. Default no-op (like draw_ribbons).
virtual void draw_world_text(const std::vector<mosaic::engine::ui::WorldGlyphVertex>& /*verts*/,
                             const mosaic::foundation::math::Mat4& /*view_proj*/,
                             TextureHandle /*atlas*/) {}
```

### `engine/render/render_system.hpp` — world-label pass

In the billboard section (which already derives `cam_right`/`cam_up` from
`Mat4::inverse_affine(view)`), after particles/trails, before the screen-space HUD passes
(so labels are part of the 3D scene, under the HUD overlay). For each `WorldLabel`:

```cpp
world.for_each<mosaic::engine::ui::WorldLabel>(
    [&](mosaic::foundation::EntityID e, mosaic::engine::ui::WorldLabel& l)
    {
        if (l.alpha <= 0.0f || l.text.empty()) { return; }
        const auto* wt = world.get_component<transform::WorldTransform>(e);
        if (wt == nullptr) { return; }
        std::shared_ptr<ui::FontAtlas> font = registry.load<ui::FontAtlas>(l.font);
        if (!font) { return; }
        const TextureHandle atlas = font_textures_.get(l.font, registry, device);
        if (atlas == 0) { return; }
        const Vec3 pos{wt->matrix.m[12], wt->matrix.m[13], wt->matrix.m[14]};
        const auto verts = ui::build_world_text(*font, l.text, pos + l.offset,
                                                cam_right, cam_up, l.size, l.color, l.alpha);
        if (!verts.empty()) { device.draw_world_text(verts, projection * view, atlas); }
    });
```

`cam_right`/`cam_up` are already computed in that block for particles; if the block is
currently gated on `particles != nullptr || trails != nullptr`, widen the guard to also
run when any `WorldLabel` exists (or lift the cam-axis computation so the label pass
always has it).

### `modules/render_vulkan` — world-space glyph pipeline

`world_text.vert` / `world_text.frag`, modeled on the **ribbon** pipeline (a CPU vertex
buffer streamed per frame + a `view_proj` push constant) plus the **atlas descriptor**
(`texture_set_layout_`, as `ui_text`/decal use):

- **Vertex input:** `WorldGlyphVertex` (pos `vec3`, uv `vec2`, color `vec3`, alpha
  `float`) — a per-frame uploaded vertex buffer (mirror the ribbon buffer handling).
- **Vertex shader:** `gl_Position = view_proj * vec4(pos, 1.0)`; pass uv + `vec4(color,
  alpha)`.
- **Fragment:** `float cov = texture(atlas, uv).a; out = vec4(color.rgb, cov * alpha);`
- **State:** alpha blend **on**; **depth test ON, depth write OFF** (labels are occluded
  by nearer world geometry but don't occlude each other oddly); topology TRIANGLE_LIST.
- Swapchain + viewport variants; `view_proj` push constant (like `RibbonDrawParams`).
- `draw_world_text` uploads the verts, binds the pipeline + atlas descriptor + push
  constant, and draws — mirroring `draw_ribbons` plus the descriptor bind from
  `draw_decals`/`draw_ui_text`.

### Demo — a floating label on a 3D object

Attach a `WorldLabel` to an existing 3D demo entity that has a `WorldTransform` (e.g. the
orbiter cube), `offset` +Y (above it), `text = "Orbiter"`, `size` a small world value,
using `kBuiltinFontId`. Adds a component to an existing entity — no new entity, so
default-scene counts are unchanged.

## Testing (headless; GPU visual maintainer-verified)

- **`WorldLabel` reflection** — round-trip (`AssetID font`, `std::string text`, `Vec3`s) +
  registration under `"WorldLabel"`.
- **`build_world_text`** — `"AB"` at a known anchor with `cam_right = +X`, `cam_up = +Y`:
  12 verts (2 glyphs × 6); all verts lie in the anchor's X/Y plane (z == anchor.z); the
  text is centered (some verts left of anchor.x, some right); empty text → no verts; a
  missing codepoint is skipped.
- **RenderSystem world-label pass** — a `FakeRenderDevice` capturing `draw_world_text`: a
  `WorldLabel` on an entity with a `WorldTransform` + registered font produces verts and a
  non-zero atlas handle; empty text / `alpha<=0` / no `WorldTransform` / unregistered font
  → no call.
- **Default-scene counts** — unchanged (`WorldLabel` on an existing entity).

`mosaic run --window` exits 0 with no Vulkan validation errors is the integration check.

## Files

**New**
- `engine/ui/world_label.hpp` — reflected `WorldLabel`.
- `engine/ui/world_text.hpp` — `WorldGlyphVertex` + `build_world_text` (pure).
- `modules/render_vulkan/shaders/world_text.vert`, `world_text.frag`.
- `tests/unit/ui/world_text_test.cpp`, `tests/unit/ui/world_label_reflection_test.cpp`.

**Modified**
- `engine/render/render_device.hpp` — `draw_world_text` seam (+ include `world_text.hpp`).
- `engine/render/render_system.hpp` — world-label pass.
- `engine/reflection/builtin_components.hpp` — register `WorldLabel`.
- `modules/render_vulkan/…` — the world-space glyph pipeline + `draw_world_text` + shader
  build wiring.
- `app/run/demo_scene.hpp` — demo `WorldLabel` on a 3D entity.
- `tests/unit/render/render_system_test.cpp` — world-label pass case.
- `tests/CMakeLists.txt` — new test sources.

## Non-goals / follow-ups

Transform-oriented (non-billboard) labels; background panel behind the text; multi-line;
distance-based scaling / fade; always-on-top (no-depth) mode; occlusion test against
geometry; right/left alignment options; world-space interactive UI.
