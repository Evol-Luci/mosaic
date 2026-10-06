# Multi-Scatter Energy Compensation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the Fdez-Agüera multiple-scattering correction to Mosaic's IBL path so a white surface in a uniform environment returns the light it receives instead of 31% of it at roughness 1.

**Architecture:** The single-scatter BRDF LUT is left untouched; the correction is applied on top at reconstruction time. A new pure C++ function `energy_compensate` in `app/asset/ibl_bake.hpp` carries the math so it can be tested in CI with no GPU, and `cube.frag`'s environment branch carries the GLSL twin, pinned by textual contract tests.

**Tech Stack:** C++20, Catch2, GLSL (compiled by glslc), CMake presets (`debug`), ctest.

**Spec:** `docs/superpowers/specs/2026-10-06-render-multi-scatter-energy-compensation-design.md`

## Global Constraints

- Branch: `render-multi-scatter-energy-compensation` (already created; spec committed as `d2b8b61`).
- Build: `cmake --preset debug && cmake --build build/debug`. Test: `ctest --test-dir build/debug`.
- Single test binary: `build/debug/tests/mosaic_unit_tests`. Run one case with `./build/debug/tests/mosaic_unit_tests "<case name>"`.
- Catch2 test names must not begin with `--` and must contain no unbalanced `[` — both break ctest discovery.
- `mosaic_unit_tests` is ~170MB and intermittently fails at the **link** step under memory pressure. If the build fails, grep the output for an actual compiler diagnostic; if there is none, just re-run the build.
- The `.githooks/pre-commit` hook regenerates `docs/index.html` and `docs/reference/*.md`. If it reports stale generated docs, `git add` what it rewrote and commit again.
- The tier rule `app -> engine -> foundation` holds. `foundation/math/vec3.hpp` has **no** componentwise `Vec3 * Vec3` or `Vec3 / Vec3`; do not add one. Write the math per channel.
- The BRDF LUT is stored `Rgba16F`, so `scale`/`bias` read back half-rounded. Measured drift on the pinned `FmsEms` references is at most 5e-7, so the 5e-5 margins below are safe.
- `docs/project/next-steps.md` is updated on this branch, in this PR — it does not get its own PR.

## Review Focus

1. **`Ess` marginally above 1 at roughness 0.** Half-rounding gives `scale + bias = 1.000076` at texel (32,0), so `Ems` is slightly negative and a naive `total >= scale + bias` assertion fails by 7.6e-5 on correct code. Task 2 Step 1 pins this with a 1e-3 slack and a comment saying why.
2. **A metal whose albedo is not 1.** `diffuse_color = albedo * (1 - metallic)` is the only thing keeping a dark conductor (`albedo = 0.5`, `metallic = 1`) from growing a diffuse lobe; the energy residual alone zeroes `kD` for white metals only. Task 2 Step 1 pins `diffuse_color = 0 => diffuse == FmsEms`, Task 4 pins the GLSL token.
3. **`FmsEms` grouped against `prefiltered` instead of `irradiance`.** Passes every furnace assertion, because the two maps are identical in a uniform environment. Only Task 4's whole-expression assertion catches it.
4. **`f0 = 0` (a perfectly black dielectric).** `Favg = 1/21`, `FssEss = bias`, and the correction must stay finite and non-negative rather than producing a NaN from `1 - Favg*Ems`. Task 2 Step 1 pins it.
5. **`scale = bias = 0`** (a LUT texel that integrated to nothing, or `bake_brdf_lut(0)`'s empty image). `Ess = 0`, `Ems = 1`, so the denominator is `1 - Favg`, which is 20/21 for a dielectric and **0 for `f0 = 1`** — the one input where the division genuinely degenerates. Task 2 Step 1 pins that `f0 = 1, scale = bias = 0` returns finite zeros rather than NaN.

---

### Task 1: The C++ seam — `energy_compensate`

**Files:**
- Modify: `app/asset/ibl_bake.hpp` (add `EnergyCompensation` + `energy_compensate` after `bake_brdf_lut`'s declaration, around line 60; and fix the stale `k = alpha^2/2` comment above it)
- Modify: `app/asset/ibl_bake.cpp` (add the implementation after `bake_brdf_lut`, which ends at line 388)
- Test: `tests/unit/app/ibl_bake_test.cpp` (already registered in `tests/CMakeLists.txt:82`)

**Interfaces:**
- Consumes: `mosaic::app::bake_brdf_lut(std::uint32_t)` and the file-local `lut_at(const Image&, x, y) -> std::pair<float,float>` helper at `ibl_bake_test.cpp:306`.
- Produces: `mosaic::app::EnergyCompensation{Vec3 specular; Vec3 diffuse;}` and
  `mosaic::app::energy_compensate(const Vec3& f0, const Vec3& diffuse_color, float scale, float bias) -> EnergyCompensation`.
  Task 2 asserts against these; Task 4 pins the GLSL that mirrors them.

- [ ] **Step 1: Write the failing test — the compensated furnace, swept**

Add to `tests/unit/app/ibl_bake_test.cpp`, after the existing `"the white furnace: measuring how much energy single-scatter GGX loses"` case. Do **not** modify that case: its pinned 0.786 and ~0.31 are the single-scatter baseline this slice is measured against, and the LUT it reads is unchanged.

```cpp
TEST_CASE("the compensated furnace: multi-scatter returns all the light it receives",
          "[asset]")
{
    // The payoff of the slice, and the case the backlog entry was written to enable.
    //
    // Same setup as the single-scatter furnace above -- a white surface (F0 = 1, so
    // every ray reflects) in a uniform environment of radiance 1 -- but reconstructed
    // through energy_compensate instead of through F0 * scale + bias alone. A surface
    // that reflects everything and absorbs nothing must return exactly 1.
    //
    // It is EXACT, not approximate, and the algebra is worth knowing because it is also
    // this test's weakness. At F0 = 1, Favg collapses to 1, so
    // FmsEms = Ems * Ess / (1 - Ems) = (1 - Ess) * Ess / Ess = 1 - Ess, and the total is
    // Ess + (1 - Ess) = 1 for ANY value of Ess. That cancellation is why the margin
    // below is 1e-5 rather than a percentage: a loose tolerance here would be a signal
    // that something is wrong, not slack worth having.
    //
    // It also means this case cannot see the LUT's half-precision, and -- see the Favg
    // case below -- cannot see Favg at all. It is swept across the WHOLE grid anyway,
    // because the slice-4c review found the wrong geometry remap survived a suite whose
    // every assertion sat at roughness 0 and 1, exactly where the two remaps coincide.
    const auto lut = mosaic::app::bake_brdf_lut(64);
    const Vec3 white{1.0f, 1.0f, 1.0f};
    const Vec3 no_diffuse{0.0f, 0.0f, 0.0f}; // metallic = 1 => diffuse_color = 0

    for (std::uint32_t y = 0; y < 64; ++y)
    {
        for (std::uint32_t x = 0; x < 64; ++x)
        {
            const auto [scale, bias] = lut_at(lut, x, y);
            const auto c = mosaic::app::energy_compensate(white, no_diffuse, scale, bias);
            const double total = static_cast<double>(c.specular.x)
                               + static_cast<double>(c.diffuse.x);
            INFO("texel " << x << "," << y << " scale " << scale << " bias " << bias
                          << " -> " << total);
            REQUIRE(total == Approx(1.0).margin(1e-5));
        }
    }
}
```

`Vec3` needs a using-declaration beside the existing ones near the top of the file:

```cpp
using mosaic::foundation::math::Vec3;
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build/debug 2>&1 | tail -20
```

Expected: FAIL at compile time — `'energy_compensate' is not a member of 'mosaic::app'` and `'EnergyCompensation' has not been declared`. A compile failure is the correct red here; there is nothing to call yet.

- [ ] **Step 3: Declare the seam in `app/asset/ibl_bake.hpp`**

Insert after the `bake_brdf_lut` declaration (currently ending `mosaic::engine::render::Image bake_brdf_lut(std::uint32_t size);`):

```cpp
// The two weights of the Fdez-Agüera multiple-scattering reconstruction, which corrects
// the energy single-scatter GGX drops. `specular` multiplies the prefiltered radiance;
// `diffuse` multiplies the irradiance.
//
// THE GROUPING IS THE POINT. The multi-scatter term (FmsEms) is returned inside
// `diffuse`, not `specular`, because many bounces between microfacets randomize
// direction and the escaping energy is diffuse-like. Moving it to `specular` passes
// every furnace test in the suite -- in a uniform environment the two maps are
// identical -- and is wrong in every real scene.
//
// `diffuse_color` is the already-metallic-weighted albedo (`albedo * (1 - metallic)`),
// not the raw base colour. Passing raw albedo gives a dark conductor a diffuse lobe:
// the energy residual zeroes kD on its own only where albedo is 1.
//
// `scale` and `bias` are one texel of bake_brdf_lut, which is NOT modified by this --
// the correction is applied on top, at reconstruction time, because it depends on the
// per-material F0 while the table is indexed by (NdotV, roughness) alone.
struct EnergyCompensation
{
    mosaic::foundation::math::Vec3 specular; // FssEss      -> prefiltered radiance
    mosaic::foundation::math::Vec3 diffuse;  // FmsEms + kD -> irradiance
};

EnergyCompensation energy_compensate(const mosaic::foundation::math::Vec3& f0,
                                     const mosaic::foundation::math::Vec3& diffuse_color,
                                     float scale, float bias);
```

- [ ] **Step 4: Correct the stale remap comment in the same header**

The comment above `bake_brdf_lut` documents the bug slice 4c fixed. Replace:

```
// Note the geometry term uses the IBL remap k = alpha^2 / 2, NOT the direct-lighting
// k = alpha / 2 that cube.frag uses. The two look identical and using the wrong one is a
// silent energy error with no visible signature beyond "slightly off".
```

with:

```
// Note the geometry term uses the IBL remap k = alpha / 2, where alpha is roughness
// squared -- the reference writes it as `float a = roughness; k = (a*a)/2`, whose `a` is
// ROUGHNESS. This file's `a` is already alpha, so squaring it again gives alpha^2/2, and
// that is exactly the bug that shipped until the slice-4c review: a silent energy error
// with no visible signature beyond "slightly off". ibl_bake.cpp's own comment at the
// remap carries the measurements. This comment used to describe the BUG as the design,
// which is how a bug comes back.
```

- [ ] **Step 5: Implement in `app/asset/ibl_bake.cpp`**

Add a file-local helper and the public function after `bake_brdf_lut` (which ends at line 388). The math is written **per channel** because `foundation/math/vec3.hpp` has no componentwise multiply or divide, and adding one for a single caller is scope the engine does not need. Every operation is componentwise, so this is identical to the vec3 form `cube.frag` uses.

```cpp
namespace
{
struct ChannelWeights
{
    float specular;
    float diffuse;
};

ChannelWeights compensate_channel(float f0, float diffuse_color, float scale, float bias)
{
    const float FssEss = f0 * scale + bias;
    const float Ems = 1.0f - (scale + bias);
    // The 1/21 is Fdez-Agüera's average-Fresnel fit. It is the one new constant this
    // function introduces and the furnace test CANNOT see it: at f0 = 1, Favg is 1
    // whatever the divisor, and the total cancels to exactly 1 regardless. The case
    // "the Favg term ... " in ibl_bake_test.cpp is what pins it, by asking for FmsEms
    // in isolation at f0 = 0.04.
    const float Favg = f0 + (1.0f - f0) / 21.0f;
    const float denom = 1.0f - Favg * Ems;
    // No epsilon. For any real LUT texel Ess >= 0.3, so at f0 = 1 (where denom reduces
    // to Ess) the divisor cannot approach zero, and 4c's renormalisation is the standing
    // lesson in what an unneeded guard costs: it hid a real integration error for two
    // slices. The ONE degenerate input is scale = bias = 0 with f0 = 1 -- an empty table
    // -- where Favg = 1 and Ems = 1 make denom exactly 0; that is zero energy in, so
    // zero out is the right answer and the branch says so rather than dividing.
    const float FmsEms = (denom != 0.0f) ? (Ems * FssEss * Favg / denom) : 0.0f;
    const float kD = diffuse_color * (1.0f - FssEss - FmsEms);
    return {FssEss, FmsEms + kD};
}
} // namespace

EnergyCompensation energy_compensate(const Vec3& f0, const Vec3& diffuse_color,
                                     float scale, float bias)
{
    const ChannelWeights r = compensate_channel(f0.x, diffuse_color.x, scale, bias);
    const ChannelWeights g = compensate_channel(f0.y, diffuse_color.y, scale, bias);
    const ChannelWeights b = compensate_channel(f0.z, diffuse_color.z, scale, bias);
    return EnergyCompensation{Vec3{r.specular, g.specular, b.specular},
                              Vec3{r.diffuse, g.diffuse, b.diffuse}};
}
```

If `ibl_bake.cpp` already closed its anonymous namespace before this point, open a fresh `namespace { ... }` as written above; two anonymous namespaces in one translation unit are the same namespace and this is the existing file's own idiom.

- [ ] **Step 6: Run the test to verify it passes**

```bash
cmake --build build/debug 2>&1 | tail -5 && \
./build/debug/tests/mosaic_unit_tests "the compensated furnace: multi-scatter returns all the light it receives"
```

Expected: PASS, 4096 assertions.

- [ ] **Step 7: Commit**

```bash
git add app/asset/ibl_bake.hpp app/asset/ibl_bake.cpp tests/unit/app/ibl_bake_test.cpp
git commit -m "feat(render): energy_compensate, the Fdez-Aguera multi-scatter weights

The compensated furnace test returns exactly 1.0 at every texel of the
64x64 LUT, against the 0.786 / 0.31 single-scatter baseline the previous
case pins. Exact rather than approximate: at F0 = 1 the total cancels to
Ess + (1 - Ess) algebraically, which is also why that case cannot see
Favg -- pinned separately next.

Also corrects ibl_bake.hpp's header comment, which still documented the
k = alpha^2/2 remap slice 4c removed as though it were the design."
```

---

### Task 2: Pin what the furnace cannot see

**Files:**
- Test: `tests/unit/app/ibl_bake_test.cpp`

**Interfaces:**
- Consumes: `mosaic::app::energy_compensate` and `EnergyCompensation` from Task 1; `lut_at` and `bake_brdf_lut`.
- Produces: nothing new. This task is assertions only — no production code changes, and it must pass against Task 1's implementation unmodified.

- [ ] **Step 1: Write the tests**

Four cases, appended after Task 1's. The first is the important one; the spec calls it the only non-tautological pin on `Favg`.

```cpp
TEST_CASE("the Favg term, pinned in isolation because the furnace cannot see it",
          "[asset]")
{
    // The furnace case above is structurally blind to Favg, and this is the sixth
    // instance in this arc of a guard that guards nothing -- predicted while scoping
    // rather than found in review, which is the only new thing about it.
    //
    // WHY IT IS BLIND, twice over. Favg = F0 + (1 - F0)/21 equals 1 whenever F0 = 1,
    // so /21, /5 and a bare 1.0 are indistinguishable to it. And dropping to a
    // dielectric does not help: kD is DEFINED as the energy residual
    // (1 - FssEss - FmsEms), so with a white albedo the three terms sum to 1 for any
    // Favg whatsoever. Energy conservation is tautological here; it cannot be the test.
    //
    // So ask for FmsEms ALONE. Passing diffuse_color = 0 -- a black dielectric -- makes
    // kD vanish, so the returned `diffuse` weight IS FmsEms, with no extra field on the
    // struct to make it reachable.
    //
    // The expected values are computed independently of this implementation, from the
    // published formula against the same LUT integral, and verified to survive the
    // table's half-precision storage (drift at most 5e-7, against a 5e-5 margin). The
    // right-hand column is what Favg = 1 produces: 15x to 36x out.
    const auto lut = mosaic::app::bake_brdf_lut(64);
    const Vec3 dielectric{0.04f, 0.04f, 0.04f};
    const Vec3 black{0.0f, 0.0f, 0.0f};

    struct Expectation
    {
        std::uint32_t x, y;
        double fms_ems;   // correct Favg = F0 + (1 - F0)/21
        double if_favg_1; // the degenerate value, for the failure message
    };
    const Expectation expected[] = {
        {40, 32, 0.0007129, 0.0103889}, // NdotV 0.633, roughness 0.508
        {40, 48, 0.0009897, 0.0195395}, // NdotV 0.633, roughness 0.762
        {20, 40, 0.0016032, 0.0283361}, // NdotV 0.320, roughness 0.635
        {63, 63, 0.0007799, 0.0277513}, // NdotV 0.992, roughness 1.000
    };

    for (const auto& e : expected)
    {
        const auto [scale, bias] = lut_at(lut, e.x, e.y);
        const auto c = mosaic::app::energy_compensate(dielectric, black, scale, bias);
        INFO("texel " << e.x << "," << e.y << ": FmsEms " << c.diffuse.x << ", expected "
                      << e.fms_ems << " (a bare Favg = 1.0 gives " << e.if_favg_1 << ")");
        REQUIRE(static_cast<double>(c.diffuse.x) == Approx(e.fms_ems).margin(5e-5));
    }
}

TEST_CASE("compensation only ever adds energy, and adds some wherever there is loss",
          "[asset]")
{
    // Catches the mutation that has now slipped through this project's token searches
    // twice: a correction that is present, named, wired, and multiplied by zero. PBR
    // slice 3's colour-space contract fell to exactly this.
    //
    // The 1e-3 slack on the lower bound is not tolerance for the model -- it is the
    // LUT's half-precision. At roughness 0 the stored scale + bias reads 1.000076, so a
    // strict `total >= scale + bias` fails by 7.6e-5 on correct code.
    const auto lut = mosaic::app::bake_brdf_lut(64);
    const Vec3 white{1.0f, 1.0f, 1.0f};
    const Vec3 no_diffuse{0.0f, 0.0f, 0.0f};

    int improved = 0;
    for (std::uint32_t y = 0; y < 64; ++y)
    {
        for (std::uint32_t x = 0; x < 64; ++x)
        {
            const auto [scale, bias] = lut_at(lut, x, y);
            const double single = static_cast<double>(scale) + static_cast<double>(bias);
            const auto c = mosaic::app::energy_compensate(white, no_diffuse, scale, bias);
            const double total = static_cast<double>(c.specular.x)
                               + static_cast<double>(c.diffuse.x);
            INFO("texel " << x << "," << y << " single " << single << " -> " << total);
            REQUIRE(total >= single - 1e-3);
            if (single < 0.99) { REQUIRE(total > single + 0.01); ++improved; }
        }
    }
    // And that the band where there IS loss is most of the table, so the case above is
    // not quietly asserting nothing. A compensation applied on three texels would pass
    // every assertion in this loop.
    INFO("texels with a measurable single-scatter shortfall: " << improved);
    REQUIRE(improved > 3000);
}

TEST_CASE("a zero diffuse colour leaves the multi-scatter term alone", "[asset]")
{
    // The C++ half of the metal invariant, and the property the Favg case relies on:
    // diffuse_color = 0 must make the returned `diffuse` weight exactly FmsEms, with no
    // kD contribution smuggled in. A metal gets no ambient diffuse lobe -- but it must
    // still receive the multi-scatter energy, which is light that genuinely left the
    // surface.
    //
    // The other half -- that diffuse_color is albedo * (1 - metallic) and not raw albedo
    // -- lives in cube.frag and is pinned textually in shader_specular_contract_test.
    const auto lut = mosaic::app::bake_brdf_lut(64);
    const auto [scale, bias] = lut_at(lut, 40, 48);
    const Vec3 f0{0.04f, 0.04f, 0.04f};

    const auto black = mosaic::app::energy_compensate(f0, Vec3{0.0f, 0.0f, 0.0f}, scale, bias);
    const auto grey = mosaic::app::energy_compensate(f0, Vec3{0.5f, 0.5f, 0.5f}, scale, bias);

    // Specular is independent of the diffuse colour entirely.
    REQUIRE(black.specular.x == Approx(grey.specular.x).margin(1e-7));
    // And the grey surface's diffuse weight is the black one's PLUS its own kD, so the
    // black one carries no kD at all.
    const float kD = 0.5f * (1.0f - black.specular.x - black.diffuse.x);
    REQUIRE(grey.diffuse.x == Approx(black.diffuse.x + kD).margin(1e-6));
    REQUIRE(kD > 0.0f);
}

TEST_CASE("each colour channel is compensated from its own F0", "[asset]")
{
    // Every other assertion in this file reads channel .x, so a per-channel
    // implementation that computes green and blue from f0.x would pass all of them --
    // and F0 is per-channel for exactly one reason: a metal's F0 IS its base colour,
    // which is what distinguishes gold from silver. A red metal compensated from its red
    // channel throughout comes out grey-ish and wrong, with no test objecting.
    const auto lut = mosaic::app::bake_brdf_lut(64);
    const auto [scale, bias] = lut_at(lut, 40, 48);

    // A red conductor: F0 = base colour, and diffuse_color = 0 because metallic = 1.
    const Vec3 red{0.9f, 0.1f, 0.1f};
    const auto c = mosaic::app::energy_compensate(red, Vec3{0.0f, 0.0f, 0.0f}, scale, bias);

    // Each channel must equal what that channel's F0 alone produces, which the scalar
    // relation FssEss = f0 * scale + bias states directly.
    REQUIRE(c.specular.x == Approx(0.9f * scale + bias).margin(1e-6));
    REQUIRE(c.specular.y == Approx(0.1f * scale + bias).margin(1e-6));
    REQUIRE(c.specular.z == Approx(0.1f * scale + bias).margin(1e-6));
    // Green and blue agree with each other (same input) but NOT with red -- which is the
    // assertion that fails if every channel is computed from f0.x.
    REQUIRE(c.specular.y == Approx(c.specular.z).margin(1e-7));
    REQUIRE(c.specular.x != Approx(c.specular.y).margin(1e-3));
    REQUIRE(c.diffuse.x != Approx(c.diffuse.y).margin(1e-5));
}

TEST_CASE("the degenerate inputs stay finite", "[asset]")
{
    // Three inputs the shader will never produce but the function must not blow up on,
    // because a NaN here propagates to the whole ambient term and renders black rather
    // than failing loudly.
    const Vec3 black{0.0f, 0.0f, 0.0f};

    SECTION("a perfectly black dielectric, f0 = 0")
    {
        // Favg becomes 1/21 and FssEss becomes bias alone. Nothing degenerates, but the
        // arithmetic is nowhere near the f0 = 1 path every other case exercises.
        const auto c = mosaic::app::energy_compensate(black, black, 0.5f, 0.01f);
        REQUIRE(std::isfinite(c.specular.x));
        REQUIRE(std::isfinite(c.diffuse.x));
        REQUIRE(c.specular.x == Approx(0.01f).margin(1e-6));
        REQUIRE(c.diffuse.x >= 0.0f);
    }
    SECTION("an empty table texel with a white surface -- the one real degeneracy")
    {
        // scale = bias = 0 gives Ess = 0 and Ems = 1; at f0 = 1 Favg is also 1, so
        // 1 - Favg * Ems is exactly 0. Zero energy in must mean zero out, not NaN.
        // bake_brdf_lut(0) returns an empty image, so this input is reachable.
        const Vec3 white{1.0f, 1.0f, 1.0f};
        const auto c = mosaic::app::energy_compensate(white, black, 0.0f, 0.0f);
        REQUIRE(std::isfinite(c.specular.x));
        REQUIRE(std::isfinite(c.diffuse.x));
        REQUIRE(c.specular.x == Approx(0.0f).margin(1e-7));
        REQUIRE(c.diffuse.x == Approx(0.0f).margin(1e-7));
    }
    SECTION("the same empty texel with a dielectric stays finite too")
    {
        const Vec3 f0{0.04f, 0.04f, 0.04f};
        const auto c = mosaic::app::energy_compensate(f0, Vec3{1.0f, 1.0f, 1.0f}, 0.0f, 0.0f);
        REQUIRE(std::isfinite(c.specular.x));
        REQUIRE(std::isfinite(c.diffuse.x));
    }
}
```

- [ ] **Step 2: Run them**

```bash
cmake --build build/debug 2>&1 | tail -5 && \
./build/debug/tests/mosaic_unit_tests "[asset]" 2>&1 | tail -15
```

Expected: PASS. These pin behaviour Task 1 already implements, so they are green on arrival — the red for them is produced by the mutation probes in Step 3, which is what makes them meaningful.

- [ ] **Step 3: Prove each case can fail — the mutation probes**

The project's standing practice, and the reason is on the record: across slice 4c six separate guards turned out to guard nothing, every one found by mutation rather than by reading. Apply each edit to `app/asset/ibl_bake.cpp`, rebuild, run `"[asset]"`, confirm the expected case goes **red**, then revert before the next.

| # | Mutation in `compensate_channel` | Must turn red |
|---|---|---|
| 1 | `const float Favg = 1.0f;` | the Favg isolation case |
| 2 | `(1.0f - f0) / 5.0f` instead of `/ 21.0f` | the Favg isolation case |
| 3 | `return {FssEss + FmsEms, kD};` (FmsEms moved to specular) | **nothing in C++** — expected, and exactly why Task 4 exists. Record it. |
| 4 | `const float FmsEms = 0.0f;` | the compensated furnace, and "only ever adds energy" |
| 5 | `return {scale + bias, FmsEms + kD};` (`FssEss -> Ess`) | the Favg isolation case and "a zero diffuse colour" |
| 6 | `const float kD = diffuse_color;` (residual dropped) | "a zero diffuse colour" |
| 7 | `compensate_channel(f0.x, diffuse_color.x, ...)` for all three channels in `energy_compensate` | "each colour channel is compensated from its own F0" |

Probe 3 producing no red is a finding, not a failure: it is the mutation the C++ cannot see, and it is the one Task 4's whole-expression assertion is built for. Note it in the commit message.

- [ ] **Step 4: Verify the baseline is green again**

```bash
git diff --stat app/asset/ibl_bake.cpp
```

Expected: empty — every mutation reverted.

```bash
./build/debug/tests/mosaic_unit_tests "[asset]"
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add tests/unit/app/ibl_bake_test.cpp
git commit -m "test(render): pin Favg, which the furnace test cannot see

The compensated furnace returns 1.0 for ANY Favg -- at F0 = 1 the term is
1 whatever the divisor, and with a white albedo kD is the energy residual
so the three terms sum to 1 regardless. Asking for FmsEms in isolation
(diffuse_color = 0, f0 = 0.04) against independently computed references
fails a bare Favg = 1.0 by 15x to 36x.

Seven mutation probes run. Six produce the expected red. The sixth --
moving FmsEms from the irradiance group to the prefiltered one -- produces
NO red in C++, because a uniform environment makes the two maps identical.
That mutation is only reachable by the shader contract test."
```

---

### Task 3: The shading change in `cube.frag`

**Files:**
- Modify: `modules/render_vulkan/shaders/cube.frag:212-247` (the `F0_amb`/`kD_amb` pair and the `if (fl.camera_position.w > 0.5)` branch)

**Interfaces:**
- Consumes: the existing locals `albedo`, `metallic`, `roughness`, `n`, `v`, and the samplers `irradianceMap`, `prefilteredMap`, `brdfLut` (set 3, bindings 0-2).
- Produces: the GLSL identifiers `FssEss`, `Ems`, `Favg`, `FmsEms`, `diffuseColor`, `kD` and the final `ambient` expression — all of which Task 4 pins textually.

- [ ] **Step 1: Run the contract test to see it fail first**

```bash
./build/debug/tests/mosaic_unit_tests "cube.frag indexes the prefiltered chain with roughness times four"
```

Expected: PASS, before the edit. This is the guard that makes the shader change impossible to ship silently — `shader_specular_contract_test.cpp:165` pins the literal `prefiltered * (F0_amb * ab.x + ab.y)`. Note that it passes now, so its going red in Step 3 is caused by this edit and nothing else.

- [ ] **Step 2: Rewrite the ambient block**

Leave `vec3 F0_amb = mix(vec3(0.04), albedo, metallic);` exactly as it is. Leave `vec3 kD_amb = (vec3(1.0) - F0_amb) * (1.0 - metallic);` too — the no-environment fallback below still uses it, and that branch must stay byte-identical. Replace the body of the `if (fl.camera_position.w > 0.5)` branch:

```glsl
    vec3 ambient;
    if (fl.camera_position.w > 0.5)
    {
        vec3 irradiance = texture(irradianceMap, n).rgb;

        // Specular: the split-sum approximation. The prefiltered chain holds FIVE levels
        // at roughness 0, .25, .5, .75, 1, so the LOD is roughness * 4 and textureLod
        // interpolates between them. A * 5.0, or a chain of a different length, yields a
        // material that merely looks too shiny or too blurry.
        vec3 R = reflect(-v, n);
        vec3 prefiltered = textureLod(prefilteredMap, R, roughness * 4.0).rgb;
        // textureLod with an explicit 0, NOT texture(). create_texture gives every
        // image a mip chain, and texture() would pick a level from screen-space
        // derivatives of (NdotV, roughness) -- but those are TABLE COORDINATES, not
        // surface UVs, so the derivative means nothing. On the demo sphere's banded
        // roughness map a 0.2 jump across one pixel is ~51 texels of gradient, selecting
        // a level around 8x reduced: a visible seam of wrong scale/bias down every band
        // boundary, on the one object this slice exists to show off.
        vec2 ab = textureLod(brdfLut, vec2(max(dot(n, v), 1e-4), roughness), 0.0).rg;

        // The Fdez-Agüera multiple-scattering correction. Single-scatter GGX drops the
        // light that would have bounced BETWEEN microfacets, and the loss grows with
        // roughness: a white surface in a uniform environment returned 0.786 at
        // roughness 0.5 and 0.31 at roughness 1, measured by the furnace test in
        // ibl_bake_test.cpp. These four lines put it back; energy_compensate in
        // app/asset/ibl_bake.cpp is the C++ twin the furnace test actually runs.
        //
        // The /21 is the average-Fresnel fit, and NO test in this suite can see it from
        // the furnace side: at F0 = 1 Favg is 1 whatever the divisor. It is pinned by
        // asking for FmsEms in isolation at F0 = 0.04, where the wrong value is 15x to
        // 36x out.
        vec3  FssEss = F0_amb * ab.x + ab.y;
        float Ems    = 1.0 - (ab.x + ab.y);
        vec3  Favg   = F0_amb + (vec3(1.0) - F0_amb) / 21.0;
        vec3  FmsEms = Ems * FssEss * Favg / (vec3(1.0) - Favg * Ems);

        // diffuseColor, NOT albedo. The (1.0 - metallic) is what keeps a DARK conductor
        // from growing a diffuse lobe: kD below is the energy residual, which zeroes
        // itself for a metal only where albedo is 1, because F0 = albedo there. A metal
        // with albedo 0.5 would otherwise get kD = 0.5 * (1 - FssEss - FmsEms).
        vec3 diffuseColor = albedo * (1.0 - metallic);
        vec3 kD = diffuseColor * (vec3(1.0) - FssEss - FmsEms);

        // FmsEms rides the IRRADIANCE, grouped with kD -- not the prefiltered radiance.
        // Many bounces between microfacets randomize direction, so the escaping energy
        // is diffuse-like. Moving it into the FssEss group passes every furnace test
        // there is, because a uniform environment makes the two maps identical, and is
        // wrong in every real scene: the multi-scatter energy would mirror the
        // environment. shader_specular_contract_test pins this whole line for that
        // reason.
        ambient = FssEss * prefiltered + (FmsEms + kD) * irradiance;
    }
```

The `else` branch and everything after it are untouched.

- [ ] **Step 3: Run the contract test to verify it now fails**

```bash
cmake --build build/debug 2>&1 | tail -5 && \
./build/debug/tests/mosaic_unit_tests "cube.frag indexes the prefiltered chain with roughness times four"
```

Expected: FAIL on `REQUIRE(frag.find("prefiltered * (F0_amb * ab.x + ab.y)") != std::string::npos)`. That is the old reconstruction's contract correctly objecting; Task 4 replaces it.

- [ ] **Step 4: Verify the shader still compiles**

The build above runs `glslc`. Confirm there is no SPIR-V diagnostic:

```bash
cmake --build build/debug 2>&1 | grep -i "cube.frag\|error" | head
```

Expected: no `error:` lines. A GLSL type error here would be `Ems` (a `float`) multiplying a `vec3` — which is legal and intended.

- [ ] **Step 5: Commit**

```bash
git add modules/render_vulkan/shaders/cube.frag
git commit -m "feat(render): apply multi-scatter compensation in cube.frag

The IBL ambient becomes FssEss * prefiltered + (FmsEms + kD) * irradiance.
FmsEms rides the irradiance because multi-bounce energy is diffuse-like;
grouping it with the prefiltered radiance passes every furnace test and is
wrong in every real scene. kD comes from albedo * (1 - metallic), which is
what keeps a dark conductor from growing a diffuse lobe.

The no-environment fallback and kD_amb are untouched, so a scene with no
environment still renders byte-identically.

shader_specular_contract_test is red at this commit, pinning the old
reconstruction. Updated next."
```

---

### Task 4: The shader contract, including the one mutation C++ cannot see

**Files:**
- Modify: `tests/unit/render/shader_specular_contract_test.cpp` (replace the assertion at line 165, add a new case)

**Interfaces:**
- Consumes: `mosaic::tests::read_shader`, `code_only`, `normalize_ws` from `tests/unit/render/shader_source.hpp`.
- Produces: nothing. Terminal task for the contract.

- [ ] **Step 1: Replace the stale assertion and add the new case**

In the existing case `"cube.frag indexes the prefiltered chain with roughness times four"`, replace:

```cpp
    // And the split-sum reconstruction itself...
    REQUIRE(frag.find("prefiltered * (F0_amb * ab.x + ab.y)") != std::string::npos);
```

with:

```cpp
    // And the split-sum reconstruction itself. The single-scatter form
    // `prefiltered * (F0_amb * ab.x + ab.y)` this used to pin is now the FssEss term of
    // the multi-scatter reconstruction; the whole expression is pinned in its own case
    // below, because the grouping is what matters and a substring cannot see it.
    REQUIRE(frag.find("vec3  FssEss = F0_amb * ab.x + ab.y;") != std::string::npos);
```

Then add, after that case:

```cpp
TEST_CASE("cube.frag groups the multi-scatter term with the irradiance, not the radiance",
          "[render]")
{
    // THE assertion of the multi-scatter slice, and the only one in the suite that can
    // catch its sharpest failure.
    //
    // FmsEms is the energy that bounced between microfacets before escaping. Many
    // bounces randomize direction, so it is diffuse-like and belongs against the
    // irradiance, grouped with kD. Moving it into the FssEss group -- against the
    // prefiltered radiance -- passes EVERY furnace test in ibl_bake_test.cpp, because a
    // uniform environment makes the irradiance and prefiltered maps identical by
    // construction, and the furnace is the only thing that can execute this math in CI.
    // The mutation was run: it produced no red anywhere in C++. In a real scene it makes
    // the multi-scatter energy mirror the environment.
    //
    // So the whole expression is pinned, whitespace-normalized, rather than any
    // substring of it. A substring search for "FmsEms" is satisfied by the broken form.
    const std::string frag = normalize_ws(code_only(read_shader("cube.frag")));
    INFO("If this fails, check the GROUPING before assuming the test is stale: "
         "FssEss multiplies prefiltered, and (FmsEms + kD) multiplies irradiance.");
    REQUIRE(frag.find("ambient = FssEss * prefiltered + (FmsEms + kD) * irradiance;")
            != std::string::npos);

    // The average-Fresnel fit, whose divisor no executable test in this project can
    // see from the furnace side -- at F0 = 1, Favg is 1 whatever it is. ibl_bake_test's
    // isolation case pins the value; this pins that the shader uses the same one.
    REQUIRE(frag.find("Favg = F0_amb + (vec3(1.0) - F0_amb) / 21.0;") != std::string::npos);

    // Ems is built from the LUT's OWN two channels. A constant here -- the mutation that
    // slipped past four token searches in PBR slice 3 -- silently discards the whole
    // correction while every identifier above stays present and spelled correctly.
    REQUIRE(frag.find("Ems = 1.0 - (ab.x + ab.y);") != std::string::npos);

    // And kD comes from the metallic-weighted colour, not from raw albedo. This is the
    // one guard for a DARK conductor: the energy residual zeroes kD on its own only
    // where albedo is 1, so a metal with albedo 0.5 would grow a diffuse lobe. Both
    // lines are required, and the second is what ties them together -- a correctly
    // spelled diffuseColor sitting unused beside `kD = albedo * (...)` is the
    // "clamped-but-unused local" shape the slice-4c review found.
    REQUIRE(frag.find("diffuseColor = albedo * (1.0 - metallic);") != std::string::npos);
    REQUIRE(frag.find("kD = diffuseColor * (vec3(1.0) - FssEss - FmsEms);")
            != std::string::npos);
}
```

- [ ] **Step 2: Run the render contract tests**

```bash
cmake --build build/debug 2>&1 | tail -5 && \
./build/debug/tests/mosaic_unit_tests "[render]" 2>&1 | tail -10
```

Expected: PASS, including the previously-red `"cube.frag indexes the prefiltered chain with roughness times four"`.

- [ ] **Step 3: Prove the new case can fail**

Each edit to `modules/render_vulkan/shaders/cube.frag`, rebuild, run `"[render]"`, confirm red, revert.

| # | Mutation | Must turn red |
|---|---|---|
| 1 | `ambient = (FssEss + FmsEms) * prefiltered + kD * irradiance;` | the grouping case — **this is probe 3 from Task 2, which produced no red in C++** |
| 2 | `/ 21.0` → `/ 5.0` | the grouping case (`Favg` assertion) |
| 3 | `vec3 diffuseColor = albedo;` | the grouping case (`diffuseColor` assertion) |
| 4 | `vec3 kD = albedo * (vec3(1.0) - FssEss - FmsEms);` with `diffuseColor` left declared above | the grouping case (`kD` assertion) — the unused-local shape |
| 5 | `float Ems = 1.0 - 0.5;` | the grouping case (`Ems` assertion) |

- [ ] **Step 4: Confirm the baseline and the full suite**

```bash
git diff --stat modules/render_vulkan/shaders/cube.frag   # expect empty
cmake --build build/debug && ctest --test-dir build/debug 2>&1 | tail -15
```

Expected: every test passing. The suite was **1151/1151** at the close of slice 4c; it should now be 1151 plus the cases added here.

Note: the case `"threaded ScriptSystem applies script writes one tick later"` SIGSEGVs intermittently under full-suite ordering, from embedded-Python GIL teardown. It is pre-existing and unrelated to this work — if it is the only failure, re-run to confirm, and do not chase it.

- [ ] **Step 5: Commit**

```bash
git add tests/unit/render/shader_specular_contract_test.cpp
git commit -m "test(render): pin the multi-scatter grouping, which C++ cannot see

Task 2's probe 3 -- moving FmsEms from the irradiance group onto the
prefiltered radiance -- produced no red anywhere in the C++ suite, because
a uniform environment makes the two maps identical and the furnace test is
the only thing that executes this math in CI. This case catches it, by
pinning the whole normalized expression rather than any substring of it.

Also pins Favg's /21, that Ems is built from the LUT's own channels rather
than a constant, and that kD comes from diffuseColor rather than a bare
albedo beside an unused local. Five probes, five reds."
```

---

### Task 5: Visual confirmation and the handoff

**Files:**
- Modify: `docs/project/next-steps.md` (new `**Last updated:**` entry at the top, after the preamble ending at line 5)
- Modify: `docs/project/deferred-work-backlog.md` (mark the multi-scatter item shipped)

**Interfaces:**
- Consumes: nothing. Terminal task.
- Produces: the handoff a worker picking up Mosaic next reads first.

- [ ] **Step 1: Confirm both window entry points run clean**

The final check is a human looking at the window, because no test in CI executes the shader. Screenshots are not available in this sandbox — verify functionally (exit 0, no Vulkan validation errors) and report what a human should look for.

```bash
./build/debug/app/mosaic run --window --ticks 120 2>&1 | tail -20; echo "exit=$?"
```

Expected: exit 0, no `VUID` or `validation` lines, and the `"image-based lighting active (5 prefiltered levels)"` log line present.

```bash
./build/debug/app/mosaic editor --ticks 120 2>&1 | tail -20; echo "exit=$?"
```

Expected: exit 0, no validation errors.

- [ ] **Step 2: Say what changed visually, and what did not**

Report to the user, for their own look at the window:

> Rough dielectrics are brighter — most visibly the demo sphere's high-roughness bands, where the single-scatter shortfall was largest (`Ess` ≈ 0.31). **Unchanged:** smooth surfaces and the metal sphere's mirror bands, because at roughness 0 `Ems = 0` and the correction is identically zero. A reviewer looking at the smooth end will correctly see no difference — that is not the correction failing to apply.

Do not claim visual confirmation yourself; the sandbox cannot capture the window.

- [ ] **Step 3: Mark the backlog item shipped**

In `docs/project/deferred-work-backlog.md`, strike through the heading of the entry beginning `**A multi-scatter / energy-compensation term for the GGX specular.**` and replace its body's final sentences with the shipped note. Follow the file's existing convention for shipped items (`~~**...**~~ — **SHIPPED** <date>`, keeping the original reasoning and adding what actually happened). Record:

- the measured before/after (0.786 → 1.000000 at roughness 0.508; 0.3085 → 1.000000 at roughness 1);
- that the furnace test's single-scatter baseline case was **kept**, not rewritten, because the LUT is unchanged;
- that the furnace test turned out to be structurally blind to `Favg`, and what covers it.

Leave the new **direct-lighting** multi-scatter entry below it open — this slice does not touch it.

- [ ] **Step 4: Write the next-steps entry**

Add a new `**Last updated:** 2026-10-06 (...)` block at the top of `docs/project/next-steps.md`, immediately after the preamble that ends at line 5 and **above** the existing IBL slice 4c entry. Match the existing entries' style: one dense parenthetical, leading with what shipped and then the findings that are worth carrying forward. Cover:

- what shipped, and the before/after numbers;
- **the headline finding:** the furnace test, the thing this slice existed to satisfy, could not validate the one new constant the slice introduced — predicted during scoping rather than found in review, which is the only new thing about the sixth instance of this pattern;
- that one of the six mutation probes produced no red anywhere in C++ (`FmsEms` onto `prefiltered`) and that the shader contract test exists for exactly it;
- that the handoff this slice started from quoted the **pre-4c buggy** curve (0.896 / 0.656), and that `ibl_bake.hpp`'s header comment was still documenting that bug as the design;
- the gate count and both `--window` entry points' status;
- deferred: the direct-lighting half, with the reason the furnace cannot measure it.

- [ ] **Step 5: Commit**

```bash
git add docs/project/next-steps.md docs/project/deferred-work-backlog.md
git commit -m "docs: multi-scatter energy compensation shipped

0.786 -> 1.000000 at roughness 0.508 and 0.3085 -> 1.000000 at roughness 1,
against the baseline slice 4c pinned. The single-scatter furnace case is
kept rather than rewritten, because the LUT is unchanged.

The finding worth carrying: the furnace test -- the thing this slice existed
to satisfy -- is structurally blind to Favg, the one new constant it
introduced, and so is a dielectric furnace, because kD is the energy
residual. Predicted while scoping rather than found in review."
```

If the pre-commit hook rewrites `docs/index.html` or `docs/reference/*.md`, `git add` them and commit again.

- [ ] **Step 6: Request review before merging**

Do not merge. Hand off per `superpowers:requesting-code-review`, then `superpowers:finishing-a-development-branch`.
