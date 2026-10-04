# Rendering — PBR Decomposition, and Tone Mapping (slice 1) (design)

**Date:** 2026-10-04
**Status:** approved (design), pending implementation plan
**Branch:** `render-tone-mapping`, cut from `main` at `3a62ab9`

This document does two things. Part One decomposes the 🔴-tier "full PBR
material model" into four independently shippable slices, because the
backlog item explicitly says it "likely [needs] its own sub-decomposition,
not a single slice". Part Two is the design for slice 1 only. Slices 2–4
get their own specs when their turn comes.

---

# Part One — The decomposition

## Where the renderer actually is

Measured, not recalled:

- **Lighting:** Lambert diffuse plus Blinn-Phong specular, in `cube.frag`.
  One directional, one point, one spot light — one of each, never
  accumulated. Flat `0.1` ambient. Shadows for the directional light only.
- **Shading space:** world space, since the 2026-09-27 UBO migration.
  Lighting lives in a per-frame uniform buffer (descriptor set 2) carrying
  `camera_position` and `light_view_proj`. Push constants are 208 bytes
  with 48 bytes of headroom.
- **Material:** `base_color`, `texture`, `shininess` (raw Phong exponent),
  `specular_strength`.
- **Colour pipeline:** already linear. The swapchain is
  `B8G8R8A8_SRGB` and textures are `R8G8B8A8_SRGB`, so the hardware does
  sRGB→linear on sample and linear→sRGB on write. **No gamma work is
  needed, and none should be added** — a manual `pow(c, 1/2.2)` on top of
  an sRGB target would double-correct.
- **Tone mapping:** none. Values above 1.0 clip against the 8-bit target.

## The four slices

**1. Tone mapping.** Designed in Part Two below.

**2. Metallic/roughness and a GGX Cook-Torrance BRDF.** `Material` loses
`shininess`/`specular_strength` and gains `metallic`/`roughness`; the
shader gets a real microfacet BRDF with Schlick Fresnel and energy
conservation. This **absorbs two existing backlog items**: the "perceptual
`0..1` gloss remap" dissolves entirely, because roughness *is* that
parameter — which is why the backlog deferred the remap to "belong with
PBR's roughness parameter" rather than shipping a second gloss scale — and
"Fresnel / energy conservation" is already recorded as folded into PBR
rather than tracked independently.

**3. Texture maps.** Metallic, roughness, and normal maps. Needs two or
three further texture bindings plus an asset-pipeline answer — how a map
is authored, imported, and referenced — which has nothing to do with the
shading math. Depends on nothing; simply its own larger question.

**4. Image-based lighting.** Cubemap capture, prefiltering, a BRDF LUT.
The largest by a wide margin, and the ambient term stays a flat `0.1`
until it lands. Already recorded as "a pillar of the PBR work", not an
add-on.

## Why tone mapping comes first, and not the BRDF

This is the decomposition's one non-obvious ordering claim, so it is
argued rather than asserted.

An energy-conserving BRDF produces values above 1.0 routinely —
that is the point of energy conservation. The current Blinn-Phong tolerates
an 8-bit target because `specular_strength` is an authored fudge factor
that an author turns down until it stops looking wrong. Replace it with a
physically derived specular over three lights and highlights clip flat.

So shipping slice 2 first would make the renderer look **worse** than it
does today, and leave it that way until slice 1 landed. Tone mapping first
is also independently valuable now: specular already clips on a bright
surface, so the slice pays for itself before any PBR work exists.

## What the decomposition deliberately excludes

- **Multi-light accumulation.** The renderer handles one light of each
  kind. That is a real limitation and an orthogonal one: PBR v1 with one
  of each light is coherent, and accumulation is a change to light
  gathering rather than to the BRDF.
- **Per-light specular colour.** Already recorded as a deliberate
  omission: real renderers almost never separate it, and it would grow
  the lighting UBO for a knob nobody has asked for.
- **Exposure as a camera parameter.** See Part Two.

## The migration decision, settled

Slice 2 removes `shininess` and `specular_strength`. There is a committed
`scene.json`, and four test files reference those fields
(`shader_specular_contract_test.cpp`, `lighting_reflection_test.cpp`,
`render_system_test.cpp`, `demo_scene_test.cpp`).

**Clean break**, confirmed with the user: remove the old fields, add the
new ones, and update `scene.json` and the four test files in the same
slice. The engine is pre-1.0 with one committed scene and no external
consumers, so there is no installed base to protect. The rejected
alternative — keeping both parameter sets with the old deprecated — would
put two gloss parameterisations in `Material` simultaneously, which is
*precisely* the situation the backlog cited when it deferred the gloss
remap, and it replaces the question "how do we migrate?" with the worse
question "which wins when both are set?". A versioned scene migration was
also rejected: no versioning exists in the serializer today, and inventing
a migration framework as a side effect of a shading change is
disproportionate.

The cost, stated: anyone holding an uncommitted local scene file loses
those two fields silently on load.

---

# Part Two — Slice 1: tone mapping

## Intent

Stop the renderer clipping. Bright specular already flattens against the
8-bit sRGB target, and every slice after this one makes that worse.

**Success criteria**

- A bright highlight rolls off instead of clipping to flat white.
- Unlit content is unchanged — not darkened.
- No material, push-constant, UBO, or interface change.

## Decisions

1. **`cube.frag` only.** It is the only shader whose output can exceed
   1.0. Verified: of the nine colour-producing fragment shaders,
   `particle.frag`, `ribbon.frag`, `decal.frag` and `world_text.frag` are
   unlit — they output authored colour times alpha — and the four UI
   shaders are authored in display space. Tone mapping an unlit shader
   would *darken* it for no benefit, because every reasonable curve maps
   1.0 to roughly 0.8.

2. **In-shader, not an offscreen HDR target with a resolve pass.** The
   real objection to per-shader tone mapping is that
   `tonemap(a) + tonemap(b) != tonemap(a + b)` — and that objection is
   about **additive** compositing. Checked every pipeline in
   `vulkan_render_device.cpp`: all are `SRC_ALPHA` alpha blending, with
   `VK_BLEND_FACTOR_ONE` appearing only on the alpha channel. **Nothing
   blends additively.** Under alpha blending the composite lerps between
   two tone-mapped values, which is a defensible approximation rather than
   a mathematical error.

   So the `R16G16B16A16_SFLOAT` target plus fullscreen resolve buys very
   little here while costing a render target, a render pass, a pipeline,
   and an answer to where UI draws relative to the resolve. It is the
   right move when bloom arrives — bloom genuinely needs the HDR buffer —
   and is recorded as the deferred follow-up rather than built now.

3. **ACES filmic (the Narkowicz approximation).** Five constants and one
   `clamp`. Reinhard was considered and rejected: it is simpler but
   desaturates highlights visibly and reads as washed out. ACES is the
   near-universal default and the difference is about four lines of
   shader.

4. **No exposure parameter.** Hard-code the curve. Exposure is a camera
   concern, not a material or lighting one, and nobody has asked for it.
   Adding a knob now would mean deciding where it lives (camera
   component? lighting UBO? push constant?) for no current caller.

## Design

At the end of `cube.frag`, after the three light contributions sum:

```glsl
// ACES filmic approximation (Narkowicz). The scene is already linear -- the
// swapchain is B8G8R8A8_SRGB, so the hardware encodes on write -- so this maps
// linear HDR to linear display range and must NOT also gamma-correct.
vec3 tonemap_aces(vec3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
```

and `outColor = vec4(tonemap_aces(color), 1.0);` in place of the current
direct write.

Nothing else changes. No C++ beyond the existing shader-compile step.

## Testing

This repo has no GPU in CI, and its established idiom for shader testing is
a **textual contract test** — `tests/unit/render/shader_specular_contract_test.cpp`
asserts things like `frag.find("max(shininess, 1.0)")`. Slice 1 follows
that idiom:

- `cube.frag` defines `tonemap_aces` and applies it to the final colour.
- `outColor` is not assigned the unmapped sum — i.e. the mapped value is
  what reaches the attachment. This is the assertion that would catch a
  future edit adding a second `outColor` write below the tone map.
- The four unlit scene shaders (`particle`, `ribbon`, `decal`,
  `world_text`) do **not** reference a tone-map function, pinning decision
  1 so a later "consistency" edit has to argue with a test.
- No manual gamma anywhere: no `pow(`...`1.0/2.2)` or `2.2` constant in
  any fragment shader, pinning the "the pipeline is already sRGB" fact so
  nobody double-corrects.

**The honest limit, stated rather than implied.** A textual contract test
cannot show that the curve looks right, or that highlights actually roll
off. That requires running `mosaic run --window` and looking at it. The
tests pin the *wiring*; the visual verification is a human step and the
spec says so rather than letting a green suite imply more.

## Risks

**Tone mapping darkens mid-tones slightly.** ACES maps 1.0 to about 0.8,
so a surface currently at full brightness becomes marginally dimmer. This
is correct — it is what makes headroom for highlights — but it is a
visible change to every lit surface in the demo scene, not only to
clipping ones. Anyone comparing screenshots before and after should expect
the whole image to shift, not just the bright parts.

**The approximation is not reference ACES.** Narkowicz's fit is a curve
match, not the full ACES RRT/ODT transform. It is the standard real-time
choice and nobody should expect colour-grading accuracy from it.

**Per-shader tone mapping becomes wrong if additive blending is ever
added.** Decision 2 rests on a measured fact about the current pipelines.
A future additive pipeline — bloom, additive particles — invalidates the
reasoning, not just the aesthetics. That is the trigger for the HDR-target
follow-up, and it is recorded in the non-goals so the dependency is
explicit rather than discovered.

## Non-goals

To be recorded in `docs/project/deferred-work-backlog.md`:

- **An offscreen HDR render target with a fullscreen resolve pass.** The
  correct-compositing form of tone mapping, and a prerequisite for bloom.
  Deferred because nothing blends additively today, so it would buy
  little for a render target, a pass, a pipeline, and a UI-ordering
  decision. **Becomes necessary the moment any additive pipeline is
  added** — that is the trigger, not a vague "if we need it".
- **Exposure control.** See decision 4.
- **Tone mapping the unlit scene shaders.** Deliberate; see decision 1.
  They cannot exceed 1.0, so it would darken authored colour for nothing.
- **Bloom.** Depends on the HDR target above.
- **Slices 2–4 of the decomposition.** Each gets its own spec.
