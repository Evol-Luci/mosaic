# VFX V4 — screen effects (fullscreen overlay) — Design

## Goal (Phase 4, VFX pillar — slice 4, final)

Add **screen effects**: a `ScreenEffect` component drives a fullscreen overlay for
**fades** (to black/white/any color), a held **color tint**, and **vignette** (edge
darkening). This completes the VFX pillar (trails → decals → screen effects).

Overlay-based v1: a fullscreen triangle blended over the finished frame — **no
render-graph restructure** and no scene-texture sampling. Effects that must sample
the rendered scene (bloom, blur, DOF) are a documented follow-up.

Same GPU caveat: headless geometry/wiring + build + `mosaic run --window` exit 0;
**the visual is the maintainer's hardware check.**

## Design

### `engine/vfx/screen_effect.hpp` — reflected component

```cpp
struct ScreenEffect
{
    Vec3  tint          = Vec3{0, 0, 0}; // color the screen blends toward
    float tint_strength = 0.0f;          // 0 = none, 1 = fully `tint` (fade/flash/grade)
    float vignette      = 0.0f;          // 0 = none, 1 = strong edge darkening
};
```

Reflected + registered as `"ScreenEffect"` (serializes, shows in the Inspector).
Defaults = no visible effect. A normal component, so a script can animate
`tint_strength` for fades (fade-in/out, hit-flash) or hold a `tint` for grading —
the same component-mutation path scripts already use.

**Coverage:** fade-to-black = `tint {0,0,0}`, `tint_strength` 0→1; fade-to-white =
`tint {1,1,1}`; red hit-flash = `tint {1,0,0}`, pulsed strength; vignette =
`vignette > 0`; combinations compose.

### `engine/render/render_device.hpp` — new seam

```cpp
struct ScreenEffectParams
{
    mosaic::foundation::math::Vec3 tint;
    float                          tint_strength = 0.0f;
    float                          vignette      = 0.0f;
};
```

`RenderDevice` gains a default-no-op `draw_screen_effect(const ScreenEffectParams&)`.

### `engine/render/render_system.hpp` — final overlay pass

At the very end of `render` (after meshes, decals, particles, ribbons), find the
**first** `ScreenEffect` component (iterate with a "done" flag, like the audio
listener). If it exists and `tint_strength > 0 || vignette > 0`, call
`device.draw_screen_effect(ScreenEffectParams{ se.tint, se.tint_strength,
se.vignette })`. An all-zero effect is skipped (no draw).

### `modules/render_vulkan` — fullscreen-overlay pipeline

- `shaders/screen_effect.vert`: a shader-generated **fullscreen triangle** (3 verts,
  `gl_VertexIndex`, clip-space `(-1,-1)/(3,-1)/(-1,3)`), outputs a `[0,1]` UV. No
  push, no vertex buffer.
- `shaders/screen_effect.frag`: push constant `{ vec4 tint /*w=strength*/, vec4 params
  /*x=vignette*/ }`. Compute `vig = smoothstep(0.5, 1.0, distance(uv,0.5)/0.7071) *
  vignette`. Composite two contributions — `tint` weighted by `tint_strength`, black
  weighted by `vig` — into one premultiplied blend: `a = clamp(tint_strength + vig,
  0, 1)`; `rgb = a > 0 ? (tint*tint_strength + vec3(0)*vig) / a : tint`; `outColor =
  vec4(rgb, a)`. (So tint-only, vignette-only, and both all read correctly under
  standard src-alpha blending.)
- `ScreenEffectPush { float tint[4]; float params[4]; }` (32 bytes; tint.w unused,
  params.x = vignette). Push stage: fragment.
- Pipeline: no descriptor sets, no vertex input; `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST`
  (draw 3); alpha blend (same factors as particles); **depth test + write off** (the
  overlay is always on top); `VK_CULL_MODE_NONE`; two variants (swapchain
  `screen_effect_pipeline_` + viewport `vp_screen_effect_pipeline_`). Modeled on the
  particle pipeline.
- `draw_screen_effect` impl: bind the pipeline (swapchain vs viewport), push the
  params, `vkCmdDraw(3, 1, 0, 0)`. No buffers.
- Shaders added to the glslc `foreach`; pipelines destroyed in teardown.

### Wiring & demo

- `register_builtin_components` — register `"ScreenEffect"`.
- No new system (the overlay is a `RenderSystem` pass).
- Demo (`build_default_scene`): a subtle `ScreenEffect{ vignette = 0.4 }` on the
  **camera** entity (existing entity → default-scene counts unchanged).

## Testing (headless)

`tests/unit/vfx/screen_effect_test.cpp` (`[vfx]`) + render extension.
1. **Reflection round-trip** — `ScreenEffect` fields survive `to_value` →
   `from_value`.
2. **Builtin registration** — `types.has("ScreenEffect")`.
3. **Render draws when active** (`[render]`) — a `ScreenEffect{ tint_strength = 0.5 }`
   → one `draw_screen_effect` call recorded by `FakeRenderDevice` with the right
   tint/strength/vignette.
4. **Render skips when inactive** — a default `ScreenEffect` (all zero) → no
   `draw_screen_effect` call.
5. **Build + smoke** — glslc compiles the shaders; `mosaic run --window --ticks N`
   exits 0, no new validation errors.

## Out of scope / follow-ups

- **Scene-sampling post** — bloom, blur, DOF, tonemapping (render the scene to an
  offscreen target, then resolve with a sampling shader). A real render-graph change.
- Animated/eased effect helpers (today a script animates `tint_strength` directly).
- Multiple stacked effects (v1 uses the first `ScreenEffect`).
- Per-camera effects in the editor viewport beyond the shared component.

---

**This completes the VFX pillar (trails, decals, screen effects).** The wrap-up
`docs/project/next-steps.md` update ships in this slice's PR.
