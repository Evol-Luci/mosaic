# Rendering — Multi-Scatter Energy Compensation (design)

**Date:** 2026-10-06
**Status:** approved design, not yet implemented
**Backlog item:** "A multi-scatter / energy-compensation term for the GGX specular"
(`docs/project/deferred-work-backlog.md`, Rendering), deferred 2026-10-04 during the
metallic/roughness design and measured by IBL slice 4c's furnace test.

## Intent

Single-scatter GGX drops the light that would have bounced between microfacets, so a
rough surface comes out darker than the light it receives. IBL slice 4c's white furnace
test measured exactly how much: a white surface in a uniform environment of radiance 1
returns **0.786** at roughness 0.51 and **0.31** at roughness 1 — losing roughly two
thirds at the top end. This slice adds the Fdez-Agüera multiple-scattering correction so
that surface returns 1.0, and closes the one correctness gap the PBR arc recorded against
itself.

The slice is unusual in having its target written down before it starts. That is the whole
point of 4c's decision to convolve on the CPU: the integrals are C++, so the improvement
is checkable in CI with no GPU.

**Scope:** the IBL path only. The direct-lighting path in `cube.frag`'s `brdf()` loses the
same energy, and is deliberately deferred — see Non-goals.

## What the baseline actually is

Correcting the handoff that fed this slice, because the numbers matter and two of them
were wrong. The figures **0.896** at roughness 0.5 and **0.656** at 0.76 are the
*pre-4c, buggy* curve produced by the `k = alpha^2/2` geometry remap. `ibl_bake_test.cpp`
is explicit about this: an earlier version of that test pinned 0.896 with its labels
inverted, so the test written to catch the bug was enforcing it.

The real single-scatter baseline, from `bake_brdf_lut(64)` with the corrected
`k = alpha/2`:

| texel (x,y) | NdotV | roughness | `scale + bias` |
|---|---|---|---|
| 32, 0 | 0.508 | 0.000 | 1.0000 |
| 40, 32 | 0.633 | 0.508 | 0.7865 |
| 40, 48 | 0.633 | 0.762 | 0.5690 |
| 63, 63 | 0.992 | 1.000 | 0.3085 |

Only the first, the 0.786, and a loose `0.25 < x < 0.40` at roughness 1 are currently
pinned as assertions; the rest are measurements made while scoping this slice.

## The LUT is not touched

`bake_brdf_lut` keeps producing single-scatter `scale`/`bias`. The correction is applied
on top, at reconstruction time.

Two consequences, both wanted. The existing furnace test at `ibl_bake_test.cpp:352` and
its recorded baseline stay exactly as they are, still green, still the honest statement
about what single-scatter GGX does — the new assertions sit beside it rather than
overwriting the number they were written to beat. And it answers why the correction cannot
simply be baked into the table: it depends on `F0`, which is per-material, while the LUT
is indexed by `(NdotV, roughness)` alone.

## The new seam

`app/asset/ibl_bake.hpp`, beside `bake_brdf_lut` — it is the split-sum's other half, and
the furnace test that exercises it already lives in `tests/unit/app/`:

```cpp
// The two weights of the Fdez-Agüera multiple-scattering reconstruction.
struct EnergyCompensation
{
    Vec3 specular;  // FssEss      -> multiplies the prefiltered radiance
    Vec3 diffuse;   // FmsEms + kD -> multiplies the irradiance
};

EnergyCompensation energy_compensate(const Vec3& f0, const Vec3& diffuse_color,
                                     float scale, float bias);
```

It takes `diffuse_color` — already metallic-weighted — rather than `albedo` plus
`metallic`, so the signature states exactly what it depends on and names the same thing
the shader's local does.

Considered and rejected: `engine/render/`. Nothing in the engine tier needs it, and the
tier rules allow `app -> engine` only, so placing it there would widen the engine's
surface for a function with one caller.

## The shading change

`cube.frag`'s environment branch, replacing
`ambient = diffuseIBL + prefiltered * (F0_amb * ab.x + ab.y)`:

```glsl
vec3  FssEss = F0_amb * ab.x + ab.y;
float Ems    = 1.0 - (ab.x + ab.y);
vec3  Favg   = F0_amb + (vec3(1.0) - F0_amb) / 21.0;
vec3  FmsEms = Ems * FssEss * Favg / (vec3(1.0) - Favg * Ems);
vec3  diffuseColor = albedo * (1.0 - metallic);
vec3  kD     = diffuseColor * (vec3(1.0) - FssEss - FmsEms);
ambient = FssEss * prefiltered + (FmsEms + kD) * irradiance;
```

Two things in there are load-bearing, and both fail silently.

**The multi-scatter energy rides the irradiance, not the prefiltered radiance.** `FmsEms`
is grouped with `kD` against `irradiance`, because many bounces between microfacets
randomize direction, making the escaping energy diffuse-like. Grouping it with
`prefiltered` instead passes **every furnace assertion in this spec** — in a uniform
environment the two maps are identical by construction — and is wrong in every real
scene, appearing as multi-scatter energy that incorrectly mirrors the environment.

**`(1.0 - metallic)` stays, moved onto `diffuseColor`.** Today's
`kD_amb = (1 - F0_amb)(1 - metallic)` is replaced by the energy residual, and it is
tempting to let the residual handle metals unaided: at `metallic = 1`, `F0 = albedo`, so a
channel with `albedo = 1` yields `kD = 0` for free. That holds only for *bright* metals. A
dark metal (`albedo = 0.5`) would get `kD = 0.5(1 - FssEss - FmsEms) != 0` — a diffuse
lobe on a conductor. The reference formulation uses `diffuseColor = baseColor(1 -
metallic)` for exactly this reason, and keeping it preserves the current invariant for all
metals rather than for white ones.

**The no-environment fallback (`albedo * 0.1 * kD_amb`) is unchanged**, so a scene with no
environment renders byte-identically. `kD_amb` therefore remains, used only by that
branch.

`shader_specular_contract_test.cpp:165` pins the literal
`prefiltered * (F0_amb * ab.x + ab.y)`, so this edit cannot ship silently — that test goes
red first, by design, and is updated to pin the new form.

## Why the furnace test is not enough, and what covers the gap

**The compensated furnace is exact, not approximate.** At `F0 = 1`, `Favg` collapses to 1,
so `Fms = Ess/Ess = 1` and the total is `Ess + 1(1 - Ess) = 1` — algebraically, at every
roughness and every viewing angle. Verified numerically across the curve at 1024 samples:
1.000000 at roughnesses 0, 0.508, 0.762 and 1.000.

**And that exactness makes the furnace structurally blind to `Favg`.** `Favg = F0 + (1 -
F0)/21` equals 1 whenever `F0 = 1`, so `/21`, `/5` and a bare `Favg = 1.0` are
indistinguishable to it. Dropping to a dielectric does not help: `kD` is defined as the
energy *residual* `1 - FssEss - FmsEms`, so with a white albedo the three terms sum to 1
for **any** `Favg` whatsoever. The furnace cannot fail on the one new constant this slice
introduces.

This is the sixth instance of the pattern 4c catalogued — a guard that guards nothing —
and it is predicted here rather than discovered in review.

**What covers it.** Call `energy_compensate` with `diffuse_color = 0` and `f0 = 0.04` — a
black dielectric. `kD` vanishes, so the returned `diffuse` weight **is** `FmsEms` in
isolation, with no widening of the API. Pin it against independently computed references:

| texel | NdotV | roughness | `FmsEms` | with `Favg = 1` | ratio |
|---|---|---|---|---|---|
| 40, 32 | 0.633 | 0.508 | 0.0007129 | 0.0103821 | 14.6x |
| 40, 48 | 0.633 | 0.762 | 0.0009897 | 0.0195420 | 19.7x |
| 20, 40 | 0.320 | 0.635 | 0.0016032 | 0.0283515 | 17.7x |
| 63, 63 | 0.992 | 1.000 | 0.0007799 | 0.0277466 | 35.6x |

A margin of 5e-5 on these fails `Favg = 1` by 15-36x and `/5` comfortably.

## Testing

In `tests/unit/app/ibl_bake_test.cpp`:

1. **The compensated furnace, swept.** For `f0 = 1`, `diffuse_color = 0`, `specular +
   diffuse == 1.0` at **every** texel of a 64x64 `bake_brdf_lut`, margin 1e-5. The sweep
   rather than a sample is 4c's lesson applied directly: the wrong `k` remap survived
   because every assertion sat at roughness 0 and 1, where the two remaps coincide. The
   tight margin is deliberate — the result is algebraic, so a percentage tolerance would
   itself be a signal that something is wrong.
2. **The `Favg` isolation case**, with the four references in the table above and the
   reason recorded in the test, including that the furnace cannot substitute for it.
3. **Compensation never loses energy.** `specular + diffuse >= scale + bias` at every
   texel, strictly greater wherever `scale + bias < 1`. This catches a correction that is
   present, wired and multiplied by zero — the mutation that passed four independent
   token searches in PBR slice 3.
4. **`diffuse_color = 0` implies `diffuse == FmsEms` exactly** — the C++ half of the metal
   invariant, and the property test 2 relies on.

In `tests/unit/render/shader_specular_contract_test.cpp`:

5. **The whole final expression**, whitespace-normalized:
   `FssEss * prefiltered + (FmsEms + kD) * irradiance`. This is the only assertion that
   catches `FmsEms` grouped against `prefiltered`, because every furnace case passes under
   that mutation.
6. `Favg` carrying `/ 21.0`; `diffuseColor = albedo * (1.0 - metallic)`; and `kD` computed
   **from `diffuseColor`**, not from a bare `albedo` beside an unused local — the
   "clamped-but-unused local" shape 4c found.
7. The existing `prefiltered * (F0_amb * ab.x + ab.y)` assertion is replaced, not deleted.

**Mutation probes**, each run and each confirmed red before the slice closes, against a
green baseline: `Favg -> vec3(1.0)`; `/21 -> /5`; `FmsEms` moved onto `prefiltered`;
`(1.0 - metallic)` dropped from `diffuseColor`; `FmsEms` dropped from the irradiance
group; `FssEss -> Ess`.

**Honest limit, stated rather than discovered.** Assertions 5-7 are token searches over
shader source. They pin that the GLSL carries the same terms as the tested C++; they
cannot execute it. The C++ and GLSL copies can still diverge in a way only a human reading
catches — the same limit `tonemap_aces`'s duplicate-plus-contract-test carries, and the
reason the final verification is still a human looking at the window.

## Also folded in

`app/asset/ibl_bake.hpp` documents the 4c bug as the design: *"the geometry term uses the
IBL remap k = alpha^2 / 2, NOT the direct-lighting k = alpha / 2 that cube.frag uses"*,
against an implementation (`ibl_bake.cpp:358`) that is `alpha / 2` and whose own comment
explains at length why `alpha^2/2` was wrong. The header kept the pre-fix wording.
Corrected here: a doc that describes the bug is how the bug comes back.

## Risks

- **The denominator `1 - Favg * Ems`.** At `F0 = 1` it reduces to `Ess`, which bottoms out
  near 0.31 across the real table, so it cannot approach zero and **no epsilon guard is
  added**. Adding an unneeded one is precisely how 4c's renormalisation came to hide a
  real integration error for two slices. The reasoning is recorded in a comment so a later
  "defensive" edit has to argue with it.
- **`Ess` marginally above 1 from finite sampling** makes `Ems` slightly negative at
  roughness 0. The algebra stays well-conditioned there (`FmsEms` is then a small negative
  number multiplied by `FssEss`), and test 1's sweep covers those texels, so this is
  bounded rather than guarded.
- **The correction is invisible where the demo is currently most interesting.** At
  roughness 0, `Ems = 0` and the correction is identically zero, so smooth surfaces and
  the metal sphere's mirror bands do not change at all. A reviewer looking at the smooth
  end will conclude nothing happened.

## Expected visual change

Rough dielectrics get brighter — most visibly the demo sphere's high-roughness bands,
where `Ess` falls to about 0.31. Metals are unchanged (their ambient diffuse was and
remains zero) and so are smooth surfaces. **No scene file changes and no migration:**
nothing authored moves, because the correction adds energy the renderer was dropping
rather than reinterpreting a material parameter.

## Non-goals

- **Multi-scatter compensation for the direct-lighting path** (`cube.frag`'s `brdf()`).
  It loses the same energy for the same reason, but the furnace test structurally cannot
  measure it: that test works by feeding a uniform environment through the C++ convolution
  integrals, and `brdf()` is shader code. Taking it would mean shipping the half with
  nothing but a token-search contract test behind it — the "wired, not correct" limitation
  slice 2 recorded and 4c existed to fix. Recorded in the backlog with what it depends on
  (a C++ mirror of `brdf()`, or GPU-capable CI).
- **A runtime quality knob** to switch the correction off. Already carried in the backlog;
  the correction is a few ALU operations on a path that already does four texture lookups,
  so there is nothing to trade yet.
- **Baking the correction into the BRDF LUT.** Impossible as stated: the correction depends
  on per-material `F0` and the LUT is indexed by `(NdotV, roughness)`.
- **Anisotropic roughness, material layers, specular occlusion, parallax-corrected probes,
  environment rotation.** All carried in the backlog, all independent of this.
