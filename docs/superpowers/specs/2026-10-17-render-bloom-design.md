# Bloom — design

**Status:** spec, written 2026-10-17. The payoff for the HDR target and resolve
(#255, #256); the first thing in this renderer that needs light above display white.

## Why now

The backlog carried bloom as blocked: *"there is nowhere to read bright pixels back from
until that exists."* The HDR target shipped on 2026-10-16, so the block is gone. The demo's
particle and decal colours were retuned on 2026-10-17 to survive the tone curve, and in doing
so became **the first authored values in the project that exceed 1.0** — which makes them the
first thing a threshold bloom will find.

## Bloom is not the RenderGraph's trigger

The backlog predicted it would be: *"a mip chain's pass count varies with resolution, which
is the first genuinely dynamic need this renderer will have had."* **That prediction is
wrong, and this spec is where it gets corrected.**

The graph's actual bar, as the Rendering section states it, is *"conditional/reorderable
passes, not just one more fixed pass"*. A bloom chain is a **linear chain whose length is a
function of resolution**, recomputed on resize — the same lifecycle the HDR target already
has. There is no conditionality, no reordering, no data-dependent topology. In code it is
`for (i = 0; i < levels_; ++i)`: direct submission with a loop, not a graph.

**The sharper trigger, recorded in its place:** passes whose *existence or order* depends on
per-frame scene content — "run the reflection pass only if a reflective material is visible",
several cameras with inter-target dependencies — or a user-authorable post chain where the
order is data rather than code. Bloom is none of those.

This is the sixth slice in this arc to find its own backlog entry wrong, and for the usual
reason: the entry was written before the code existed to contradict it.

## The authored surface

A new component, `engine/vfx/bloom.hpp`, beside `ScreenEffect`, read from the camera entity:

```cpp
struct Bloom
{
    float intensity = 0.0f;  // 0 = off, and the default
    float threshold = 1.0f;  // display white -- light above this blooms
    float knee      = 0.5f;  // soft ramp below the threshold
};
```

Registered as `"Bloom"` in `engine/reflection/builtin_components.hpp`, which buys
serialisation, the Inspector, and scripting with no further code — the same three things
`ScreenEffect` gets.

**Per-camera and authored, not a renderer global.** This follows the precedent `Camera::
exposure` set: a render knob that belongs to the scene serialises with it, differs per
camera, and reaches the inspector through reflection. A `RenderSystem` setter would do none
of those. It is its own component rather than three more fields on `Camera` because `Camera`
is about the camera's optics and `ScreenEffect` already established where a post-process knob
on the camera entity lives.

**`intensity = 0.0f` by default**, matching `ScreenEffect`'s stated rule that defaults produce
no visible effect. Adding the component to a scene therefore changes nothing until someone
turns it up, and **every existing scene renders byte-identically** whether or not it has one.

**`threshold = 1.0f` is the load-bearing default.** 1.0 is display white, so out of the box
bloom picks up exactly the light that exceeds what the screen can show — which is the headroom
the HDR target was built to preserve. A threshold meaningfully below 1.0 would make bloom a
general glow over ordinary lit surfaces, which is a different effect.

**The soft knee is not optional polish.** A hard `max(colour - threshold, 0)` makes a specular
highlight drifting across the threshold pop in and out between frames on a moving object,
which reads as a bug rather than an effect. The knee is the ramp that makes it fade.

## The boundary

```
device.begin_hdr_scene_pass(clear)
    ... all world space ...
device.end_hdr_scene_pass()
device.build_bloom(BloomParams{threshold, knee})      <- NEW, only when intensity > 0
device.draw_hdr_resolve(exposure, bloom_intensity)    <- signature grows by one
    ... UI ...
```

`RenderSystem` reads the active camera's `Bloom`, calls `build_bloom` **once** when intensity
is above zero, and **always** passes intensity to the resolve. The backend owns the mip count,
the loop, and every intermediate target; none of that reaches the engine tier.

**One hook, not N.** The engine says "bloom, with these parameters" and the backend decides
how many passes that takes. Had `RenderSystem` driven the loop it would have had to know the
mip count, which is a backend fact about a backend resource, and `foundation` → `engine` →
`modules` would have carried it upward for nothing.

**Intensity goes to the resolve, not to `build_bloom`.** The same reason the HDR spec gave for
exposure: a parameter is testable where backend state is not. It also makes the skip case safe
by *arithmetic* rather than by bookkeeping — at intensity 0 the chain contributes 0 **whatever
it contains**, so a frame that skips `build_bloom` cannot show the previous frame's glow, and
no "is the chain stale?" flag has to be right.

### The layout hazard

Because the resolve samples the chain **unconditionally**, it would otherwise sample an image
in `UNDEFINED` layout on frame 1, and on any frame following a skip. **The chain is therefore
cleared and transitioned to `SHADER_READ_ONLY_OPTIMAL` once at creation**, beside the HDR
target. Stated here because it is invisible to the build and to the whole headless suite, and
appears only as a validation error on a real device.

## The Vulkan implementation

**One image**, `R16G16B16A16_SFLOAT`, `mipLevels = N`, usage `COLOR_ATTACHMENT | SAMPLED`, at
**half** the swapchain's resolution — plus one `VkImageView` and one `VkFramebuffer` per
level, because a framebuffer binds a specific view.

`N` runs until the smaller dimension reaches about 8 px, **clamped to 6**. At 1920×1080 that
is 960×540 down to 30×16. Beyond six levels each step costs a pass and contributes almost
nothing to the result.

**One render pass**, `loadOp = LOAD` with `SHADER_READ_ONLY_OPTIMAL` as both initial and final
layout. LOAD because the upsample *adds into* the level it targets; a readable final layout
because the next step samples what the last one wrote. Three pipelines against it:

| pipeline | samples | writes | blend |
|---|---|---|---|
| `bloom_prefilter` | the HDR scene target | mip 0 | off |
| `bloom_down` | mip *i* | mip *i*+1 | off |
| `bloom_up` | mip *i*+1 | mip *i* | additive (`ONE`, `ONE`) |

`1 + (N−1) + (N−1)` passes — **11 at N=6** — in one loop. Mip 0 is the finished glow.

The prefilter is also the first downsample rather than a separate full-resolution step: it
samples the HDR target and writes half-resolution mip 0, applying the threshold and knee on
the way. A full-resolution bright-pass would cost a 1920×1080 target for a result that is
immediately halved.

Descriptors: `N + 1` combined-image-sampler sets, allocated once from the chain's own pool —
one per distinct **source**, which is the HDR view plus every mip, 0..N−1. (Not N: the
downsample reads mips 0..N−2 and the upsample reads mips 1..N−1, so between them every level
is sampled at some point, and mip N−1 is a source even though nothing downsamples from it.)
Its own pool rather than `descriptor_pool_`, whose `maxSets` is budgeted exactly for
textures, materials and IBL.

### Three things the implementation must not rediscover

- **The descriptors must be rewritten on resize.** `recreate_swapchain` rebuilds the chain,
  which invalidates every view those `N + 1` sets point at. This is exactly the defect #256
  shipped and its review caught — a freed image view still bound — and the fix there
  (`write_hdr_descriptor`, called from `recreate_swapchain`) is the pattern to copy.
- **`hdr_resolve.vert` is renamed `fullscreen_triangle.vert`.** All three bloom pipelines want
  it verbatim: no UV output, UV derived from `gl_FragCoord`, the negative-height-viewport
  discipline living in exactly one place. Reusing a shader named for the resolve would be a
  smell; this is a deliberate small refactor of #256's work, not a drive-by.
- **`N` can be 1** on a very small window: prefilter only, no down or up steps. The loop must
  not assume `N ≥ 2`, and the `N−1` counts above must both reach zero cleanly.

### The resolve

```glsl
outColor = vec4(tonemap_aces((hdr + bloom * pc.bloom_intensity) * max(pc.exposure, 0.0)), 1.0);
```

Bloom is added **inside** the curve, and exposure scales it too, because bloom is *light*.
Added after the curve it would be untonemapped glow pasted onto a tone-mapped scene, which
clips and reads as a white smear.

## Testing

`FakeRenderDevice` records calls in order, so the part that matters is testable headlessly:
**the boundary**.

- `build_bloom` is called exactly once per frame, after `end_hdr_scene_pass` and before
  `draw_hdr_resolve`.
- It is **not** called when intensity is 0, nor when the camera has no `Bloom` at all.
- It **is** called when intensity is above 0.
- The threshold and knee reaching it are the **active** camera's, with several cameras present.
- The intensity reaching `draw_hdr_resolve` is the active camera's, and **0** with no component.
- A frame with **no camera** still runs the full sequence, resolving with intensity 0.
  `render()` has no early return for a missing camera, and that is the state every freshly
  created World is in.

**Mutation probes**, derived from the code: `build_bloom` moved before `end_hdr_scene_pass`;
made unconditional; intensity hardcoded at the resolve; threshold and knee swapped at the call.

**Textual contract tests**, the house idiom for shader-side guarantees since CI has no GPU:
bloom is added before the curve (the full expression pinned, as the exposure contract is);
all three bloom shaders derive UV from `gl_FragCoord`; the chain's one-time layout transition
is present in the backend.

## What this cannot verify

Everything above tests *ordering and wiring*. **Whether it looks like bloom needs a human at
the window**, and one risk deserves naming in advance.

The demo lights itself with a real outdoor HDR sky whose radiance genuinely exceeds 1.0, so
**at the default threshold of 1.0 the sky and the horizon will glow.** Physically that is
correct — a bright sky does bloom — but it may veil the image. If it does, the fix is raising
the *demo's* authored threshold, not changing the pipeline, and that is now a knob the scene
owns. Which side of that it lands on cannot be predicted from here, and the sandbox cannot
screenshot a GUI window to find out.

This goes on the next human verification pass, together with whether the retuned particle
colours — the first values in the project above 1.0, and so the first things to bloom — read
as glowing rather than smeared.

## Non-goals

- **The RenderGraph.** Deliberately not triggered; the sharper trigger is recorded above.
- **Lens dirt, anamorphic or streak bloom.** Separate effects with their own authored assets.
- **Per-object bloom masks.** Needs a channel or a stencil the renderer does not have.
- **Temporal stability / TAA.** The soft knee addresses threshold flicker; frame-to-frame
  jitter from sub-pixel highlights is a different problem needing history buffers.
- **A quality setting or configurable mip count.** `N` is derived from resolution and clamped
  at 6. A knob here needs a reason, and nothing has asked.
- **Retuning the demo's threshold.** Only if a human at the window says the sky veils.
