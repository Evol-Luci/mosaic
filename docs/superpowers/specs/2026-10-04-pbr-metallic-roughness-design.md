# Rendering — Metallic/Roughness and a Cook-Torrance BRDF (design)

**Date:** 2026-10-04
**Status:** approved (design), pending implementation plan
**Branch:** `render-pbr-metallic-roughness`, cut from `main` at `3f04237`

Slice 2 of four. The decomposition and the migration decision live in
`docs/superpowers/specs/2026-10-04-pbr-decomposition-and-tone-mapping-design.md`;
this document specifies the BRDF itself. Slice 1 (tone mapping) shipped in
#219 and is a hard prerequisite: an energy-conserving BRDF produces values
above 1.0 routinely, and without the ACES curve they would clip flat.

## Intent

Replace Lambert-plus-Blinn-Phong with a physically based microfacet model,
so that a material's appearance follows from two meaningful parameters
rather than from a Phong exponent and an authored fudge factor.

**Success criteria**

- A `metallic = 1` surface reads as metal — tinted specular, no diffuse —
  not as shiny plastic.
- `roughness` is perceptually usable across its `0..1` range, which the
  raw Phong exponent was not.
- Diffuse and specular are energy-conserving relative to each other.
- Highlights still roll off rather than clipping (slice 1's job, verified
  not to regress).

## What replaces what

`Material` loses `shininess` and `specular_strength`, and gains:

```cpp
float metallic = 0.0f;   // 0 = dielectric, 1 = metal
float roughness = 0.5f;  // 0 = mirror, 1 = fully rough
```

`base_color` keeps its name and **gains a second role**: albedo for
dielectrics, and the specular colour `F0` for metals. That dual meaning is
the defining feature of the metallic/roughness workflow, and it is why the
parameter is called *base* colour rather than albedo.

**`specular_strength` dissolves rather than migrating.** Dielectric `F0`
is a fixed 0.04 — a physical constant, not an authored knob. The backlog
already recorded the reasoning: "a Schlick Fresnel term only makes sense
once the BRDF around it is physically based; doing it inside Blinn-Phong
would be half a PBR model with none of its coherence."

**`shininess`'s perceptual problem dissolves too.** The deferred
"perceptual `0..1` gloss remap" item **is** `roughness`, which is why the
backlog deferred the remap to "belong with PBR's roughness parameter"
rather than shipping a second gloss scale.

### A comment whose reasoning inverts

`blinn_spec` currently carries:

> *the caller multiplies by that light's own attenuation/shadow/cone factor
> and by the LIGHT's color — never by albedo, which would make every
> surface read as painted metal.*

That is **correct for Blinn-Phong and wrong for PBR**. A metal's specular
*is* tinted by its base colour; that is what distinguishes gold from
silver. The comment goes with the function, and this paragraph exists so
that nobody reinstates it later as a bug fix.

## The BRDF

Cook-Torrance, three standard terms, computed per light inside the
existing light loop. The loop structure does not change — each light is
still gated by its own shadow, attenuation and cone factors — only what is
computed inside it.

- **D** — GGX / Trowbridge-Reitz normal distribution, with
  `α = roughness²`.
- **G** — Smith geometry with the Schlick-GGX approximation, using the
  direct-lighting remap `k = α/2`.
- **F** — Schlick Fresnel, with `F0 = mix(vec3(0.04), base_color, metallic)`.

Specular is `D·G·F / (4·NdotL·NdotV)`. Diffuse is `kD · albedo/π`, with
`kD = (1 - F)(1 - metallic)` — so metals receive no diffuse, which is what
makes them read as metal rather than as shiny plastic.

The flat `0.1` ambient stays, multiplied by `kD` so that metals do not
receive ambient diffuse. A principled ambient needs image-based lighting,
which is slice 4; the decomposition already records that the ambient term
remains flat until then.

## The π, and why the lights get re-authoring

Today's diffuse is `albedo × NdotL × lightColor`. Energy-conserving
diffuse is `albedo/π × NdotL × lightColor`. Dividing by π darkens every
lit surface by about 3.14, on top of slice 1's tone curve already mapping
1.0 to roughly 0.8.

**The division stays and the light intensities are re-authored to
compensate.** Confirmed with the user. The demo scene's values are
multiplied by π:

| Light | Before | After |
|---|---|---|
| Directional | 1.0 (implicit) | 3.14 |
| Point | 3.0 | 9.42 |
| Spot | 6.0 | 18.85 |

`scene.json`'s lights get the same treatment.

The rejected alternative was to skip the `/π` and document that intensity
implicitly absorbs it. That keeps the demo looking identical and requires
no re-authoring — but it leaves diffuse π× too strong *relative to
specular*, and the diffuse-to-specular ratio is precisely what energy
conservation buys. It would be PBR in name with its central property
removed. A third option — changing the light components' default
intensities centrally — was rejected because the demo's explicitly-set
values would still need re-authoring, so it hides where the constant went
without avoiding the work.

**The cost, stated:** after this slice, "intensity" means something
different than it did before. Any scene authored against the old meaning
looks dark until its lights are scaled by π. There is one committed scene
and one demo, both updated here.

## Converting the six demo materials

Mechanically, via `roughness ≈ sqrt(2 / (shininess + 2))`, the standard
Phong-exponent-to-roughness approximation:

| Surface | shininess | roughness |
|---|---|---|
| Ground | 16 | 0.33 |
| Textured box | 32 | 0.24 |
| Red sphere | 64 | 0.174 |
| Capsule | 8 | 0.447 |
| Second box | 32 | 0.24 |
| Green trail-emitter box | 256 | 0.088 |

All six are dielectric, so `metallic = 0` throughout — which means the
slice would ship a `metallic` parameter that nothing exercises visually.
**So the red sphere becomes metallic** (`metallic = 1`, `roughness = 0.174`,
`base_color` unchanged). The one thing a human visual check is for is
seeing metal look like metal, and with every surface dielectric there
would be nothing to look at.

## Numerical guards

The existing shader earned three guards the hard way — the `1e-8`
half-vector length check, the `max(shininess, 1.0)` exponent clamp, and
the `step()` back-face gate — and two of those are pinned by
`shader_specular_contract_test.cpp`. GGX needs its own, carried across
rather than rediscovered:

- **`roughness` floored** before squaring. `α = 0` makes the GGX
  distribution a delta function: the denominator
  `(NdotH²(α²-1) + 1)²` goes to zero at `NdotH = 1` and `D` becomes `inf`,
  which propagates to a white pixel. Clamp to `0.045` (so `α² ≥ ~4e-6`)
  in the shader, deliberately **not** in the engine — matching the
  existing decision that `Material` round-trips byte-for-byte and the
  shader does the clamping.
- **`NdotL` and `NdotV` clamped** away from zero, because they appear in
  the `4·NdotL·NdotV` denominator. A grazing-angle fragment otherwise
  divides by ~0.
- **The half-vector degenerate case**, already handled: `v == -L` gives a
  zero-length half-vector and `normalize()` would produce NaN. The
  existing `dot(hv,hv) > 1e-8` guard carries over.

## Files this touches

- `engine/render/material.hpp` — the two fields and `reflect`.
- `engine/render/render_device.hpp` — `DrawParams`' two floats.
- `engine/render/render_system.hpp` — the three places the old names are
  copied from `Material` into `DrawParams`, plus the no-`Material`
  defaults, which the file's own comment requires to match `Material`'s.
- `modules/render_vulkan/shaders/cube.frag` — the push-constant block and
  the shading math.
- `app/run/demo_scene.hpp` — six materials and three light intensities.
- `scene.json` — materials and lights.
- Tests: `render_system_test.cpp`, `shader_specular_contract_test.cpp`,
  `lighting_reflection_test.cpp`, `demo_scene_test.cpp`.

## Testing

- **Reflection round-trip** for `metallic`/`roughness`, matching how the
  old fields were covered.
- **The no-`Material` default path** still agrees with `Material`'s own
  defaults. `render_system.hpp`'s comment states that requirement, and it
  is the kind of thing that silently drifts when two fields are renamed in
  three places.
- **The shader contract test is rewritten, not deleted.** It currently
  pins `max(shininess, 1.0)`; it will pin the roughness floor and the
  `NdotL`/`NdotV` denominator clamps instead. Same purpose — the C++
  cannot see shader source, so nothing else can catch the loss of a guard
  whose absence produces `inf` rather than a crash.
- **Metals get no diffuse**: a shader contract pinning `(1.0 - metallic)`
  in the `kD` expression. This is the single assertion that separates
  "metal" from "shiny plastic", and it is a one-token edit away from being
  lost.
- **Slice 1 must not regress:** the tone-map contract tests stay green,
  including the exactly-one-`outColor`-write assertion.

**The honest limit.** No test here can show the BRDF is *correct* — only
that it is wired, guarded, and round-trips. The real check is a furnace
test: a white uniform environment, where a correct energy-conserving BRDF
returns exactly the input radiance for any roughness. That needs image-
based lighting, which is slice 4. Until then, correctness rests on the
formulae matching their published forms and on a human looking at the
result. This document says so rather than letting a green suite imply
more.

## Risks

**The demo will look different, and "different" is not "wrong."** Three
things change at once: diffuse divides by π, light intensities multiply by
π, and the specular lobe shape changes from Phong to GGX. The first two
cancel on average but not per-pixel, and GGX has a longer tail than Phong
— so highlights will look broader and softer at the same nominal
roughness. Anyone comparing screenshots should expect a different image,
not an identical one with better highlights.

**One metallic surface is a thin sample.** Making the red sphere metal
exercises the `metallic = 1` path visually, but nothing exercises
intermediate values, and intermediate metallic is physically meaningless
anyway (it exists for texture-map blending, which is slice 3). A single
metal is the right amount of demo change for this slice; it is not
coverage.

**`roughness ≈ sqrt(2/(shininess+2))` is an approximation.** The converted
demo materials will not look pixel-identical to their Blinn-Phong
versions, and should not be expected to. The conversion exists to put each
surface in the right perceptual neighbourhood, not to preserve appearance.

## Non-goals

To be recorded in `docs/project/deferred-work-backlog.md`:

- **Texture maps for metallic/roughness/normal** — slice 3, and the reason
  intermediate `metallic` values exist at all.
- **Image-based lighting** — slice 4, and the prerequisite for both a
  principled ambient term and the furnace test that would actually verify
  this BRDF.
- **A multi-scatter / energy-compensation term.** Single-scatter GGX loses
  energy at high roughness. The correction (Fdez-Agüera or a LUT) is a
  refinement on top of a working BRDF, not part of standing one up.
- **Anisotropic roughness.** One scalar `roughness` assumes isotropic
  microfacets. Anisotropy needs a tangent frame, which the vertex format
  does not carry.
- **Clear-coat, sheen, transmission, or any other material layer.** The
  metallic/roughness workflow is the base; layers are separate features
  with their own parameters.
- **Re-authoring any scene other than the demo and `scene.json`.** There
  are no others in-repo; this is recorded so the π change's blast radius
  is explicit rather than assumed.
