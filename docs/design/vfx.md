# Visual Effects (VFX)

The visual effects subsystem provides dynamic visual elements complementing the
core geometry renderer: particle systems, trails and ribbons, world decals, and
screen-space post-processing overlays.

## Structure

```text
VFX Subsystem
   ├── ParticleSystem    (billboard emitters, curves, easing, bursts)
   ├── TrailSystem       (position sampling, ring buffer, lifetime)
   ├── RibbonGenerator   (camera-facing triangle strip synthesis)
   ├── Decals            (projected world-space planar textures)
   └── ScreenEffects     (fullscreen composite: color tint & radial vignette)
```

## 1. Trails & Ribbons

Trails visualize entity motion over time:

- **Component:** `TrailRenderer` defines `color`, `start_size`/`end_size`, `start_alpha`/`end_alpha`, `lifetime`, and `sample_interval`.
- **Simulation:** `TrailSystem` samples world positions into a ring buffer at fixed intervals and ages out samples beyond `lifetime`.
- **Connected Ribbon Meshing:** `build_ribbon()` constructs a continuous camera-facing triangle strip from sample points:
  $$\mathbf{side} = \text{normalize}(\mathbf{segment\_dir} \times \mathbf{camera\_dir})$$
  Tapering both width and alpha smoothly from head to tail.
- **Rendering:** Submitted via `RenderDevice::draw_ribbons()` and rendered using a specialized alpha-blended Vulkan ribbon pipeline.

## 2. World Decals

Decals place persistent or transient textures onto surfaces (such as tire marks, scorches, or footprint markers):

- **Component:** `Decal` defines `texture` (`AssetID`), `color` tint, and `alpha`.
- **Placement:** Position, rotation, and scale are driven directly by the entity's `Transform` / `WorldTransform`.
- **Rendering:** Submitted via `RenderDevice::draw_decals()`. Quads are generated directly in the vertex shader from `gl_VertexIndex` with alpha blending over scene geometry.

## 3. Screen Effects

Screen effects provide global camera overlays:

- **Component:** `ScreenEffect` attached to a Camera entity configures `tint` (Vec3), `tint_strength` (float), and `vignette` (float).
- **Rendering:** Submitted via `RenderDevice::draw_screen_effect()` as the final overlay pass. A single fullscreen triangle is rasterized over the viewport, blending uniform color tint with a smooth radial vignette falloff:
  $$r = \|\mathbf{uv} - 0.5\|, \quad \text{attenuation} = 1.0 - \text{smoothstep}(0.3, 0.8, r) \times \text{vignette}$$
