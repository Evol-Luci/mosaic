# UI — Image Presentation: Sub-Rects and Aspect Modes (design)

**Date:** 2026-10-10
**Status:** approved design, not yet implemented
**Backlog item:** **U7 image presentation**, from
`docs/superpowers/specs/2026-10-07-ui-pillar-decomposition.md` — described there as
independent of the other five sub-projects and the cheapest real group.

## Intent

`UiImage` stretches its texture to fill an anchored rect, unconditionally. Two
consequences, both visible in any real HUD: a non-square image is distorted, and there is
no way to draw one sprite out of an atlas — every sprite needs its own texture and its own
upload.

This slice adds a **pixel sub-rect** (draw a region of a texture) and **aspect modes**
(`Stretch` / `Fit` / `Fill`). `Stretch` stays the default, so every existing scene renders
unchanged.

## Scope: 9-slice is split out, and not arbitrarily

U7 grouped three features, but they are not independent: **9-slice needs nine different
sub-rects of one texture**, so it is built *on top of* the UV plumbing here rather than
beside it. Bundling would put UV plumbing, an aspect enum, and a **nine-fold draw-count
change** in one diff — and the draw count is the only part of U7 with a performance
dimension, so a combined review would have to find it amongst two unrelated features.
9-slice is recorded in the backlog with its dependency on this slice.

## What scoping measured

Four findings, each of which changed the design rather than confirming it.

1. **The sub-rect mechanism already exists, shipped, in the sibling path.**
   `UiTextPush` already carries `uv[4]`, and `ui_text.vert` already does
   `v_uv = vec2(mix(pc.uv.x, pc.uv.z, c.x), mix(pc.uv.y, pc.uv.w, c.y))`. So image
   sub-rects are a near-copy of a proven pattern, not new design. `UiImagePush` goes
   32 → 48 bytes and `ui_image.vert` gains the same line.

2. **`TextureCache` already has the dimensions and throws them away.** `get()` loads the
   `Image` to call `create_texture`, then caches only the handle. Aspect modes need the
   pixel dimensions at draw time, so the cache starts holding `{handle, width, height}`.

3. **Unresolved textures are already skipped.** `RenderSystem`'s image pass does
   `if (tex == 0) { return; }`. Combined with (2), that gives an invariant worth pinning
   rather than a fallback worth inventing: **a non-zero handle implies non-zero
   dimensions**, so no caller needs a "dimensions unknown" branch and no frame can show
   the whole atlas while waiting for a load.

4. **`aspect_` is already available on `RenderSystem` and already used for exactly this.**
   The text pass passes it to `layout_text`, and the comment above that pass says so. The
   precedent for aspect-correcting a UI rect exists; this follows it.

## Where the work happens, and why not the shader

Fit/Fill is computed **on the CPU in `RenderSystem`**, which adjusts the rect and the UVs
before emitting the quad.

The alternative was to pass the texture aspect into `ui_image.vert` and letterbox there.
Rejected for one decisive reason: **CPU-side arithmetic is headlessly testable**, because
`RenderSystem` already has a fake-device harness that captures emitted quads, while shader
logic can only be pinned by token searches. This session has twice shipped shader-side
behaviour that no test could execute — once a dead feature that thirteen green tests could
not see — so verifiability outweighs elegance here. The shader change stays to one line.

It would also have needed the *screen* aspect passed in anyway (see below), so the GPU
route is more plumbing for less evidence.

## The change

**`engine/render/texture_cache.hpp`** — `get()` returns, or a sibling accessor exposes,
`{TextureHandle handle; std::uint32_t width; std::uint32_t height;}`. The `Image` is
already in hand at creation; nothing new is loaded.

**`engine/ui/ui_image.hpp`** — two reflected fields:

```cpp
// Sub-rect in TEXTURE PIXELS: {x, y, w, h}. A zero w or h means the whole texture, which
// is what a default-constructed UiImage gets -- so every existing scene is unchanged.
//
// Pixels rather than normalized UVs because that is how atlas art is authored and how any
// tool reports it: a human can read a sprite sheet and type the numbers. Hand-divided UVs
// also break silently when an atlas is resized or repacked. Feasible only because this
// slice caches the texture's dimensions for the aspect modes, so the conversion is free.
Vec4  sub_rect = Vec4{0, 0, 0, 0};

// 0 = Stretch (current behaviour), 1 = Fit, 2 = Fill. An int cast to an enum, matching how
// `anchor` already works on this component, and defaulting to the existing behaviour.
int   fit_mode = 0;
```

**`engine/render/render_device.hpp`** — `UiImageQuad` gains a UV rect, so the backend
receives resolved UVs and performs no arithmetic.

**`modules/render_vulkan/vulkan_render_device.cpp`** — `UiImagePush` gains `uv[4]`
(32 → 48, assertion updated); `draw_ui_image` copies it.

**`modules/render_vulkan/shaders/ui_image.vert`** — the `uv` push member and the `mix`
line, identical in shape to `ui_text.vert`'s.

## The two rules that are load-bearing

**Fill is UV cropping; Fit is geometry. So Fill composes with the sub-rect.** The sub-rect
defines *what the image is*; Fit and Fill then apply **to that**, never outside it. Fit
shrinks the drawn rect inside the authored rect, centred. Fill keeps the authored rect and
crops the UVs **within the sub-rect's bounds**.

Getting the order wrong lets Fill sample neighbouring sprites in an atlas, which reads as
corrupt art rather than as a bug — so it is asserted, not merely intended.

**Aspect correction needs BOTH aspects.** The rect is normalized screen space, which is
not square: `size = {0.2, 0.2}` is twice as wide as tall in pixels on a 2:1 display. So
"preserve the image's aspect" means reconciling the texture's `w/h` against the rect's
`w/h × screen aspect`. Using only the texture aspect is the obvious mistake and is wrong
on every non-square window, which is all of them.

## Testing

**The sharp case, constructed so the screen aspect is load-bearing.** A square texture
(100×100), `size = {0.2, 0.2}`, screen aspect 2.0. The rect is 2:1 in pixels, so `Fit`
must shrink the width to `{0.1, 0.2}` — 1:1 in pixels. Ignore the screen aspect and it
stays `{0.2, 0.2}`, rendering a square texture as a 2:1 rectangle. **A test on a square
screen would pass either way**, which is exactly the shape of guard this project keeps
finding to be worthless, so the case uses a non-square screen deliberately.

Headless, against `RenderSystem`'s existing fake-device harness:

1. **Stretch is byte-identical to today** — default component, same rect, full `0..1` UVs.
   The migration guarantee, and the thing whose breakage would silently change every HUD.
2. **Sub-rect pixels → UVs.** `{32, 64, 16, 16}` on a 128×128 texture →
   `{0.25, 0.5, 0.375, 0.625}`. Asymmetric on purpose: equal values pass with x and y
   transposed.
3. **A zero `w` or `h` means the whole texture.**
4. **Fit letterboxes and centres**, including the both-aspects case above.
5. **Fill crops, and crops *within* the sub-rect** — the resulting UVs must stay inside
   the sub-rect's bounds.
6. **`TextureCache`'s invariant:** a non-zero handle implies non-zero dimensions.
7. **Degenerate inputs stay sane:** a 1×1 texture; a zero-size rect; a sub-rect larger
   than the texture (clamped to its bounds); negative sub-rect values (clamped to zero).
   Clamped rather than wrapped, because sampling outside an atlas region yields a
   neighbour's art.

**Shader contract**, extending the existing UI contract tests: the `mix(...)` line and the
48-byte push assertion. **Stated limit:** these assert the GLSL carries the right terms,
never that it executes correctly — the lesson from this session's inverted pin.

**Mutation probes derived from the CODE, not from the assertion list:** drop the screen
aspect from the correction; drop the texture aspect; swap Fit and Fill; let Fill crop
against the whole texture instead of the sub-rect; transpose x/y in the pixel→UV
conversion; drop the zero-`w` whole-texture branch; neuter the `uv` push copy.

## Non-goals

- **9-slice scaling.** Split out at the start of this slice and recorded in the backlog,
  with its dependency on these UVs and the three decisions it will need.
- **Tiling / repeat.** Needs a sampler address mode decision and, for a sub-rect, either a
  separate atlas page or shader-side wrapping within the sub-rect. Its own slice.
- **UV flip or rotation.** Cheap to add later on this machinery; nothing asks for it.
- **Batching the image pass.** This path issues **one draw call per image** today. Fine at
  current scale, and stated here rather than after the fact because it is the number
  9-slice will multiply by nine.
- **Deriving an authored aspect from the asset.** `Fit`/`Fill` read the texture's real
  dimensions; an override field would be a second source of truth for the same fact.
