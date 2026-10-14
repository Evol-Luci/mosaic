# Offscreen HDR target and fullscreen resolve — design

**Status:** spec, written 2026-10-13. The prerequisite for bloom; not bloom itself.

## Why now

The backlog has carried this as a 🟡 item with an explicit trigger: *"the moment any
additive-blending pipeline is added, in-shader tone mapping stops being defensible and this
becomes necessary."* Bloom is wanted soon, which fires it.

## What is wrong with tone mapping where it is

**It is per-object, not per-frame.** Only `cube.frag` and `skybox.frag` call `tonemap_aces`.
Decals, particles, ribbons and world text never tone map at all — they alpha-blend against a
buffer whose contents have *already* been through the curve.

That is incoherent in a way bloom makes fatal. A "bright" particle cannot be bright: it is
written into an LDR buffer where 1.0 is the ceiling, so there is no headroom for a bloom pass
to find. Bloom needs the scene in linear HDR, with values above 1.0 surviving to the point
where something can detect them.

## The boundary

**Everything world-space renders into the HDR target and is tone-mapped as one image. The UI
draws onto the resolved result.**

```
HDR target:  opaque meshes → skybox → decals → particles + ribbons
             → screen effect (vignette) → world text
   resolve:  fullscreen tone map, exposure applied
LDR target:  UI panels → UI images → UI text
```

**Why the UI is outside.** A `UiPanel`'s colour is authored, not captured: `{0.1, 0.6, 0.9}`
is a value someone typed and expects to see. Pushing it through ACES makes what appears on
screen a function of how bright the scene behind it is, so the HUD would dim whenever the
world got bright. UI is already the one subsystem whose passes are fixed and ordered
(#241), and this keeps it predictable.

**Why the vignette is inside.** It attenuates light, which is what a real lens does before
exposure. Outside the resolve it would be a post-hoc darkening of the final image — also
defensible, and recorded as the alternative — but inside is the physically coherent place and
costs nothing extra.

## The appearance shift is accepted

Decals, particles, ribbons and world text begin to be tone-mapped. **Their current look is an
accident of bypassing the curve, not a design.** If the demo's particles read wrong
afterwards, the fix is to retune their authored colours, not to special-case the pipeline:
a compensation step that pre-inverts the curve would be physically meaningless, would need
maintaining, and would fight bloom later — a "bright" particle would once again not be bright
in HDR, which is the whole problem being solved.

Checking this needs a human at the window, and it is the one claim this slice cannot make
for itself.

## Design

### 1. Three device hooks, defaulting to no-op

Mirroring the viewport hooks `RenderDevice` already carries from 37d:

```cpp
virtual void begin_hdr_scene_pass(const Color& clear) {}
virtual void end_hdr_scene_pass() {}
virtual void draw_hdr_resolve(float exposure) {}
```

**`draw_hdr_resolve` is a DRAW, not a pass**, and that is what keeps the editor and the
window on one code path. The resolve is a fullscreen triangle into *whatever colour target is
currently bound* — the swapchain in `mosaic run --window`, the viewport offscreen target in
the editor. Neither caller needs to know which:

```
window:  begin_hdr → world draws → end_hdr → [swapchain pass] resolve → UI
editor:  begin_hdr → world draws → end_hdr → [viewport pass]  resolve → UI → ImGui composites
```

Had the resolve been a pass of its own, it would have needed to know its destination, and the
two paths would have diverged exactly where 37d worked to keep them together.

**A device that implements none of them renders untone-mapped.** Acceptable because the
Vulkan backend is the only device that produces pixels; every other implementation in the
tree (`FakeRenderDevice`, `MinimalDevice`, the recording devices) draws nothing and only
records calls. Stated so it is a known consequence rather than a discovered one.

### 2. Tone mapping moves out of the object shaders

`tonemap_aces` and the exposure multiply leave `cube.frag` (3 sites) and `skybox.frag` (2)
and move into the resolve's fragment shader. The object shaders then write **linear HDR**,
which is the point.

Exposure is passed to `draw_hdr_resolve` explicitly rather than read from the lighting UBO.
It is a property of the resolve now, and a parameter is testable where a UBO field read
inside the backend is not.

### 3. `RenderSystem` owns the boundary

The pass order already lives in `RenderSystem::render`. It gains three calls at the right
seams and nothing else moves. The boundary is visible in one function, which is where a
reader looking for "what is in HDR" will go.

### 4. The Vulkan backend reuses what exists

`create_offscreen_render_pass` / `destroy_offscreen_target` already build an offscreen colour
target for the editor viewport, `VK_FORMAT_R16G16B16A16_SFLOAT` is already used elsewhere in
the file, and `screen_effect.vert` is a fullscreen-triangle vertex shader to copy. The HDR
target follows the viewport target's lifecycle, including recreation on resize.

### 5. Direct submission again, not the RenderGraph

The graph's trigger was re-stated in the backlog as a **dynamic** pass need —
"conditional/reorderable passes, not just one more fixed pass" — after the editor's viewport
path reached three direct-submission passes and the recorded decision was to extend direct
submission anyway. This slice adds one more *fixed* pass, so it does not meet that bar.

**Bloom is the likely real trigger**, since a mip chain's pass count varies with resolution,
which is the first genuinely dynamic need the renderer will have had. Recorded so the next
slice reaches the question already framed.

## Non-goals

- **Bloom.** This is its prerequisite. No bright-pass, no blur, no mip chain.
- **Compensating the four effects** so they look as they do today. Decided against above.
- **The RenderGraph.** Deferred again, with bloom named as the probable trigger.
- **A configurable tone-mapping curve.** ACES stays; swapping curves is its own question.
- **HDR output to the display.** The resolve targets an LDR swapchain, as now. True HDR
  presentation is a swapchain-format and OS question, not this.
- **Retuning the demo's particle colours.** Only if a human looking at the window says they
  need it; the slice does not guess.

## Testing

`FakeRenderDevice` records calls in order, which makes the part that matters testable
headlessly: **the boundary**.

- `begin_hdr_scene_pass` is called before any world-space draw.
- `end_hdr_scene_pass` is called after the last world-space draw and before the resolve.
- `draw_hdr_resolve` is called exactly once per frame, after `end_hdr_scene_pass`.
- Every UI draw (`draw_ui`, `draw_ui_image`, `draw_ui_text`) happens **after** the resolve.
- The vignette (`draw_screen_effect`) and world text happen **before** `end_hdr_scene_pass`.
- The exposure reaching `draw_hdr_resolve` is the active camera's.
- A scene with no UI still resolves — the resolve is not conditional on there being UI.
- A frame with **no camera** still runs the full sequence exactly once. `render()` has no
  early return for a missing camera; it falls back to a default view and projection, so the
  pass structure is unconditional and the resolve receives the default exposure rather than
  being skipped. Worth pinning, because "no camera" is the state every freshly created World
  is in and a conditional resolve would make it a special case.

**Mutation probes**, derived from the code: resolve moved before `end_hdr_scene_pass`; a UI
draw moved inside the HDR pass; the resolve emitted per UI pass rather than once; exposure
hardcoded; `end_hdr_scene_pass` omitted.

## What this cannot verify

Everything above tests *ordering*. That the image looks right — that the scene is not darker
or washed out, that the four newly-tone-mapped effects read acceptably, that the vignette
still works — needs a human at the window, and is the whole reason the appearance shift was
accepted as a decision rather than asserted as a fact. It goes on the next verification pass
together with U6's drag feel and the standalone hover change, which is what the batching
cadence is for.
