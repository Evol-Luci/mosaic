# Rendering — Image-Based Lighting (design)

**Date:** 2026-10-06
**Status:** approved (design), pending implementation plan
**Branch:** `render-image-based-lighting`, cut from `main` at `fd252aa`

IBL sub-slice **4c**, and the last of the PBR arc. The decomposition lives in
`docs/project/deferred-work-backlog.md` under "Ambient/environment specular
(IBL)". Sub-slice 4a (mipmaps) shipped in #222 and 4b (cubemaps, the HDR path
and a skybox) in #223. This document specifies the lighting itself.

## Intent

Replace the flat `0.1` ambient term with image-based lighting: a diffuse
irradiance lookup and a specular prefiltered-environment lookup, combined
through the split-sum approximation.

**Success criteria**

- The demo's metal sphere **reflects its surroundings** instead of reading
  near-black. With a bright environment the reflection is obvious; with the
  committed space environment it is faint, which is correct and is why a
  bright one is needed for the visual check (see Risks).
- A dielectric surface picks up ambient colour from the environment rather
  than a uniform grey wash.
- A scene with **no** environment renders exactly as it does today.
- **The furnace test passes**, or fails with a number we understand. This is
  the criterion that matters most; see its own section.

## What scoping measured

- **The ambient term to replace is three lines** —
  `modules/render_vulkan/shaders/cube.frag:199-201`,
  `albedo * 0.1 * kD_amb` with `F0_amb`/`kD_amb` computed just above. `kD_amb`
  is already zero for a metal, which is exactly why the demo's sphere reads
  black today.
- **The lit pipeline binds three descriptor sets** — set 0 the material texture
  pair (two bindings), set 1 the shadow map, set 2 the `FrameLighting` uniform
  buffer (one binding, visible to both stages). The IBL products are **per
  frame, not per material**, so they belong in set 2 and the pipeline layout
  stays at three sets.
- **`create_cubemap` takes exactly one mip level.** 4b's spec said why: the
  prefiltered cube is written by convolution rather than blitted down. That is
  now precisely the thing that needs levels.

## What is computed at import

Both environment-dependent products are convolved on the CPU at import, from
the cubemap 4b already produces, and written to a new `MOSAICIBL1` blob beside
it.

**Irradiance — 32² × 6 faces.** For each texel, the cosine-weighted integral of
incoming radiance over the hemisphere about that direction. 32² because
irradiance is inherently low-frequency: it is a hemisphere average, so detail
beyond a handful of texels per face is averaged away by the integral itself.

**Prefiltered specular — 128² × 6 faces × 5 mip levels.** Level `m` holds the
environment convolved with the GGX lobe at `roughness = m / 4`, so the five
levels are `0, 0.25, 0.5, 0.75, 1.0` and the shader indexes them with
`roughness * 4` through `textureLod`'s own interpolation.

**Mip 0 is a copy, not a convolution.** Roughness 0 is a mirror, whose GGX lobe
is a delta function — convolving it is a very expensive way to reproduce the
input. Taking it as a straight resample removes the most costly level outright,
since cost falls with resolution at each subsequent level.

The alternatives were convolving on the GPU at load (the conventional choice,
fast and runtime-tunable, but it needs render-to-cubemap-face machinery this
engine still does not have, and **none of the integrals would be covered by any
test**) and convolving on the CPU at startup without cooking (same testability,
no new asset format, but it recomputes an identical answer on every run and
costs seconds of startup forever). Import-time was chosen for the same reason
4b's equirect conversion was: it puts the error-prone arithmetic somewhere CI
can check it.

## The BRDF LUT, computed at startup

A 256² two-channel table — the split-sum's scale and bias terms — integrated
when the engine starts, uploaded as an ordinary 2D texture.

**It is deliberately not an asset.** The integral is of the GGX BRDF over
roughness and viewing angle and does not depend on the environment at all, so
cooking it beside an environment would bake an identical copy into every one.
It costs milliseconds and is testable directly against known values.

Rejected: replacing it with Karis's analytic approximation, which needs no
texture and no startup work but diverges from the reference split-sum at
grazing angles and high roughness — exactly where this slice's own furnace test
will be looking.

## What this requires that does not exist

**`create_cubemap` must accept a mip chain.** It currently takes
`std::array<Image, 6>` and creates one level. It gains an overload taking the
levels explicitly, and **uploads** them — it does not generate them, so sub-slice
4a's blit ladder is untouched and the two mechanisms stay separate.

**Set 2 gains three bindings:** the irradiance cube, the prefiltered cube and
the BRDF LUT, all fragment-stage combined image samplers, alongside the
existing `FrameLighting` buffer at binding 0.

## The shading change

```glsl
vec3 irradiance = texture(irradianceMap, n).rgb;
vec3 diffuseIBL = kD_amb * albedo * irradiance;

vec3 R = reflect(-v, n);
vec3 prefiltered = textureLod(prefilteredMap, R, roughness * 4.0).rgb;
vec2 ab = texture(brdfLut, vec2(NdotV, roughness)).rg;
vec3 specularIBL = prefiltered * (F0_amb * ab.x + ab.y);

vec3 ambient = diffuseIBL + specularIBL;
```

`F0_amb` and `kD_amb` already exist at that point in `cube.frag` and keep their
current definitions — this changes what multiplies them, not how they are
derived.

**The flat `0.1` ambient is kept as the no-environment fallback.** Every scene
without one must render exactly as it does today, so the old expression is not
deleted; it becomes the other branch of a uniform the renderer sets. This is
also what keeps the engine usable without an environment asset at all.

## The furnace test

This is the slice's most important deliverable and the reason the convolution
being C++ matters.

A furnace test places a surface inside a **uniform white environment**. A
correct energy-conserving BRDF then returns exactly the input radiance, for
every roughness and every viewing angle — the surface disappears into the
background. Too dark means the model loses energy; too bright means it invents
it.

Because the integrals are ordinary C++ here rather than shader code, **this runs
in CI with no GPU**:

- Feed a uniform environment of radiance `L` through the irradiance integral:
  every output texel must be `L`, for every face and every direction.
- Feed it through the prefiltered integral at each of the five roughness
  levels: every output texel must be `L`, at every level.
- Integrate the BRDF LUT's split-sum terms and assert the analytically known
  values at the corners of the roughness/angle domain.

Slice 2's spec recorded as its honest limit that *"no test here can show the
BRDF is correct — only that it is wired, guarded, and round-trips"*, and named
the furnace test as what would, once IBL existed. This is that test.

**It is expected to find something.** Single-scatter GGX demonstrably loses
energy at high roughness — the backlog already carries a deferred multi-scatter
compensation item for exactly this. If the prefiltered integral comes back
short at roughness 1.0, that is the known limitation showing up, not a defect
in this slice; the test should be written to pin the *measured* shortfall with
its reason, rather than to assert a tolerance loose enough to hide it.

## Testing

- **The furnace test**, above, in three parts.
- **Irradiance against an analytic case:** a single bright direction in an
  otherwise black environment produces a cosine falloff about that direction,
  which is the integral's defining shape.
- **Prefiltered monotonicity:** increasing roughness must not increase the
  variance between neighbouring texels — a blur cannot sharpen.
- **BRDF LUT bounds:** both channels in `[0, 1]`, and the scale term
  approaching 1 as roughness approaches 0, where the Fresnel term dominates.
- **The cooked `MOSAICIBL1` round trip**, including a corrupt and a truncated
  blob.
- **The no-environment fallback** produces byte-identical output to today.
- **Source contracts** for the three new set-2 bindings, the `roughness * 4.0`
  LOD mapping, and that the fallback branch still exists.
- **Vulkan validation** on both `--window` entry points.

**The honest limit.** The furnace test shows energy conservation, not that the
image looks right; a correct BRDF with the environment rotated or the faces
crossed still passes it. A human looking at the window remains the final check.

## Risks

**Import becomes slow** — seconds rather than milliseconds, dominated by the
prefiltered levels. Mip 0 being a copy removes the worst of it; the remaining
cost is real and is the price of testable integrals.

**The committed space environment will make this slice look like nothing
happened.** It is 0.2% mean radiance, so there is almost no light to gather; the
metal sphere will go from near-black to slightly-less-near-black. This is
correct physics and is already recorded in `assets/backgrounds/README.md`. A
bright outdoor or studio environment is needed for the visual check, and
obtaining one is part of this slice's work rather than a follow-up.

**The split-sum is an approximation by construction.** It assumes `N = V = R`
when prefiltering, so reflections at grazing angles are wrong in a way no
amount of correct implementation fixes. Stated here so a later reader does not
file it as a bug.

**A wrong roughness-to-LOD mapping is invisible as a defect.** `roughness * 4.0`
against five levels is correct; `roughness * 5.0` or indexing a four-level chain
produces a material that merely looks too shiny or too blurry.

## Non-goals

To be recorded in `docs/project/deferred-work-backlog.md`:

- **Multi-scatter / energy-compensation for GGX** — already in the backlog, and
  this slice's furnace test is what will quantify it.
- **Specular occlusion** — IBL applied without regard to local occlusion makes
  cavities too bright; the fix needs an AO term, which needs AO maps.
- **Parallax-corrected or box-projected probes** — the environment is treated as
  infinitely distant, so reflections do not respond to position within a room.
- **Dynamic or captured environments** — already deferred in 4b.
- **A runtime quality knob** for the convolution sizes — fixed at 32² and
  128²×5, changeable only by re-import, the same decision 4b made for face size.
- **Rotating the environment** relative to the scene.
