# UI U3 — HUD images / sprites — Design

## Goal (Phase 4, UI pillar — slice 3)

Render **HUD images** (sprites): a reflected `UiImage` component draws a textured,
tinted, alpha-blended quad in screen space — an icon, logo, health bar, or crosshair.
`UiImage` is essentially U1's `UiPanel` plus a texture `AssetID` and a tint: it reuses
the same anchor + normalized `pos`/`size` placement (`resolve_rect`), stretching the
texture to fill the resolved rect.

Builds on **U1** (`resolve_rect`, the `draw_ui` HUD pass pattern) and the existing
`TextureCache` (materials/decals). Independent of U2b (text). Base branch: `main`
(U1 + U2a merged).

Same GPU caveat as the other visual slices: headless wiring + build +
`mosaic run --window` exit 0 is what the sandbox verifies; **the on-screen sprite is the
maintainer's hardware check**.

## Design

### `engine/ui/ui_image.hpp` — reflected component

```cpp
#pragma once

#include "foundation/math/vec2.hpp"
#include "foundation/math/vec3.hpp"
#include "foundation/serialization/reference.hpp" // AssetID

namespace mosaic::engine::ui
{
using mosaic::foundation::math::Vec2;
using mosaic::foundation::math::Vec3;

// A screen-space HUD sprite: a texture stretched to an anchored rect, tinted and
// alpha-blended. anchor is an int cast to ui::Anchor (clamped to TopLeft out of range).
// pos is a normalized offset from the anchor; size is a fraction of the screen. tint
// multiplies the texture rgb; alpha multiplies the texture alpha. A normal component:
// scripts can swap `texture`, animate `alpha`/`tint`/`pos`.
struct UiImage
{
    mosaic::foundation::serialization::AssetID texture;
    int   anchor = 0;
    Vec2  pos    = Vec2{0.0f, 0.0f};
    Vec2  size   = Vec2{0.1f, 0.1f};
    Vec3  tint   = Vec3{1.0f, 1.0f, 1.0f};
    float alpha  = 1.0f;
    int   order  = 0;
};

template <class V>
void reflect(V& visitor, UiImage& i)
{
    visitor.member("texture", i.texture);
    visitor.member("anchor", i.anchor);
    visitor.member("pos", i.pos);
    visitor.member("size", i.size);
    visitor.member("tint", i.tint);
    visitor.member("alpha", i.alpha);
    visitor.member("order", i.order);
}
} // namespace mosaic::engine::ui
```

Registered `"UiImage"` in `builtin_components.hpp` (serializes, Inspector-editable,
add/remove via the generic Inspector, script-mutable). The `AssetID texture` field
mirrors `Decal.texture`.

### `engine/render/render_device.hpp` — new seam

```cpp
struct UiImageQuad
{
    mosaic::engine::ui::Rect       rect;   // normalized screen rect (top-left origin)
    mosaic::foundation::math::Vec3 tint;
    float                          alpha = 1.0f;
};
// Draw alpha-blended textured HUD quads sampling `texture` (full RGBA × tint).
// Default no-op (like draw_ui). Drawn after draw_ui (panels), so images sit over panels.
virtual void draw_ui_image(const std::vector<UiImageQuad>& /*quads*/,
                           TextureHandle /*texture*/) {}
```

### `engine/render/render_system.hpp` — image pass

An **image pass right after the panel (`draw_ui`) pass**, before any future text pass —
so images draw over panels. Reuses the existing `texture_cache_` member. For each
`UiImage`:

```cpp
world.for_each<mosaic::engine::ui::UiImage>(
    [&](EntityID, mosaic::engine::ui::UiImage& im) {
        if (im.alpha <= 0.0f) { return; }
        const TextureHandle tex = texture_cache_.get(im.texture, registry, device);
        if (tex == 0) { return; }                       // nil / unresolved texture
        const ui::Rect rect =
            ui::resolve_rect(to_anchor(im.anchor), im.pos, im.size);
        device.draw_ui_image(std::vector<UiImageQuad>{ UiImageQuad{rect, im.tint, im.alpha} }, tex);
    });
```

v1 emits one `draw_ui_image` call per image (each may use a different texture); the
seam takes a vector so same-texture batching is possible later. Cross-component
`order`-unification (panels vs images vs text under one sorted pass) is a documented
follow-up — v1 orders by pass (panels → images).

### `modules/render_vulkan` — screen-space textured pipeline

`ui_image.vert` / `ui_image.frag`, modeled on the U1 `ui` pipeline (shader-generated
quad from a push-constant rect) **plus** the decal texture descriptor
(`texture_set_layout_`):

- **Push constant** per quad: `rect (vec4)` + `tint (vec4: rgb + alpha)` = 32 bytes.
  Vertex shader builds the 6-vertex quad, maps rect→NDC (top-left origin, as `ui.vert`),
  and emits UV `0..1` across the quad (whole texture).
- **Descriptor:** the texture sampled via the existing `texture_set_layout_` (set 0,
  binding 0 — same as decal).
- **Fragment:** `vec4 t = texture(tex, uv); out = vec4(t.rgb * tint.rgb, t.a * tint.a);`
  standard src-alpha blend; depth test/write off.
- `draw_ui_image` binds the pipeline + the texture descriptor set once, then loops
  quads (push constants + `vkCmdDraw(6,…)`), choosing the swapchain vs viewport variant
  on `viewport_pass_open_` — exactly like `draw_ui` / `draw_decals`.
- Swapchain + viewport variants.

### Demo — `UiImage` on the camera

Attach a `UiImage` to the **existing camera entity** (like `UiPanel`), using an existing
procedural demo texture (e.g. `kCheckerTextureId`), small, in a corner (e.g. anchor
`TopRight`, `size {0.12, 0.12}`) — so default-scene counts are unchanged.

## Testing (headless; GPU visual maintainer-verified)

- **`UiImage` reflection** — round-trip (incl. `AssetID texture` + `Vec2`/`Vec3`
  fields) + registration under `"UiImage"`.
- **RenderSystem image pass** — a `FakeRenderDevice` capturing `draw_ui_image`: a
  `UiImage` with a registered texture produces one quad (correct resolved rect + tint +
  alpha) and a non-zero texture handle; `alpha <= 0` and a nil/unregistered texture
  produce no call. (Reuse the render test's `FakeRenderDevice`; its `create_texture`
  returns a fresh non-zero handle for a non-empty `Image`, and the test registers a demo
  `Image` in the registry — mirror the decal test's texture setup.)
- **Default-scene counts** — unchanged (`UiImage` on the camera entity).

Build + `mosaic run --window` exits 0 with no Vulkan validation errors is the
integration check; the on-screen sprite is the maintainer's hardware verification.

## Files

**New**
- `engine/ui/ui_image.hpp` — reflected `UiImage`.
- `modules/render_vulkan/shaders/ui_image.vert`, `ui_image.frag`.
- `tests/unit/ui/ui_image_reflection_test.cpp` (render-system tests extend the existing
  render test file).

**Modified**
- `engine/render/render_device.hpp` — `UiImageQuad` + `draw_ui_image` seam.
- `engine/render/render_system.hpp` — image pass (after the panel pass).
- `engine/reflection/builtin_components.hpp` — register `UiImage`.
- `modules/render_vulkan/…` — the screen-space textured pipeline + `draw_ui_image` +
  shader-build wiring.
- `app/run/demo_scene.hpp` — demo `UiImage` on the camera.
- `tests/CMakeLists.txt` — the new test source.

## Non-goals / follow-ups

Aspect-preserving fit/fill modes; 9-slice scaling; texture atlas sub-rects (source UV
region); unifying panels + images + text under one `order`-sorted HUD pass; a
`UiImage` importer for HUD-specific sprite assets (uses existing image assets today).
