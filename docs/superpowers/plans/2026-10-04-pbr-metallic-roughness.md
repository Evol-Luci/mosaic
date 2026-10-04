# Metallic/Roughness and Cook-Torrance BRDF Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace Lambert-plus-Blinn-Phong with a GGX Cook-Torrance BRDF driven by `metallic` and `roughness`.

**Architecture:** `Material` and `DrawParams` swap `shininess`/`specular_strength` for `metallic`/`roughness`; `cube.frag` swaps `blinn_spec` for a microfacet BRDF with Schlick Fresnel and energy-conserving diffuse. Task 1 makes the model work and leaves the demo visibly dark (diffuse now divides by π); Task 2 re-authors the lights to compensate and makes one surface metal.

**Tech Stack:** C++20, GLSL (Vulkan, `#version 450`), Catch2 including textual shader-contract tests.

**Spec:** `docs/superpowers/specs/2026-10-04-pbr-metallic-roughness-design.md` — slice 2 of the decomposition in `2026-10-04-pbr-decomposition-and-tone-mapping-design.md`. Read both.

## Global Constraints

- **The field rename has no compiling intermediate state.** `DrawParams`' layout must match `cube.frag`'s push-constant block byte-for-byte, so `Material`, `DrawParams`, `RenderSystem`, the shader, and every test referencing the old names change together in Task 1. This is why Task 1 is large and is not split further.
- **`roughness` is clamped in the SHADER, not the engine.** `α = roughness²` and `α = 0` makes the GGX denominator zero at `NdotH = 1`, so `D` becomes `inf` and the pixel goes white. Clamp to `max(roughness, 0.045)` in `cube.frag`. The engine deliberately does not clamp, matching the existing decision that `Material` round-trips byte-for-byte — the same reason `shininess` was never clamped in C++.
- **`NdotL` and `NdotV` are clamped away from zero** before use in the `4·NdotL·NdotV` denominator.
- **Diffuse divides by π.** `kD · albedo / 3.14159265` with `kD = (1 - F)(1 - metallic)`. Do not drop the π to keep the demo bright; that is the rejected alternative and it removes the property this slice exists for.
- **`F0 = mix(vec3(0.04), base_color, metallic)`.** 0.04 is the dielectric constant, not a tunable.
- **The flat `0.1` ambient stays, multiplied by `kD`** so metals get no ambient diffuse. A principled ambient needs IBL, which is slice 4.
- **Defaults are `metallic = 0.0f`, `roughness = 0.5f`**, and `RenderSystem`'s no-`Material` fallback must match them — its own comment requires it.
- **Light re-authoring values, exactly:** `DirectionalLight.color` `{1,1,1}` → `{3.142, 3.142, 3.142}`; `PointLight.intensity` 3.0 → 9.425; `SpotLight.intensity` 6.0 → 18.850. `scene.json`'s single `PointLight` intensity 3.0 → 9.425.
- **`scene.json` needs no material edits.** Its `Material` entries serialize only non-defaults and carry neither old field.
- **Roughness conversions, computed not estimated** — `sqrt(2/(shininess+2))`: 8 → 0.447, 16 → 0.333, 32 → 0.243, 64 → 0.174, 256 → 0.088.
- Slice 1 must not regress: the tone-map contract tests stay green, including the exactly-one-`outColor`-write assertion.
- Build: `cmake --build --preset debug -j 20`. Gate: `ctest --preset debug --output-on-failure` — **no `-E` exclusions**.
- **Run only one build at a time** — concurrent `cmake --build` corrupts archives in this tree.
- Never `git add -A`: `docs/project/advancement-plans-and-evaluation.md` must never be staged. Stage explicit paths.
- **Never `git commit --amend` without checking `git log -1` first.**
- The pre-commit hook regenerates `docs/index.html` and `docs/reference/*.md`; if it rejects a commit, `git add` the files it names and commit again.
- Catch2 `TEST_CASE` names: no commas, no leading `--`, no unmatched `[`.
- A GLSL error fails the **build**, not a test. Read build output.

## Review Focus

Failure modes the spec implies that no task's tests would otherwise exercise.

1. **`roughness = 0` authored in a scene** — `α² = 0` makes `D` infinite at the highlight centre and the surface blows to white. The engine deliberately does not clamp, so only the shader stands between an author and a white pixel. → Task 1.
2. **A grazing-angle fragment** where `NdotV → 0` divides by ~0 in the specular denominator. Silhouette edges hit this on every frame. → Task 1.
3. **`metallic = 1` must kill diffuse entirely.** `kD = (1-F)(1-metallic)` is one token away from being lost, and losing it turns every metal into shiny plastic — the exact failure the slice exists to prevent. → Task 1.
4. **The no-`Material` draw path must still match `Material`'s defaults.** Two fields renamed in three places is precisely where that silently drifts, and `render_system.hpp`'s own comment states the requirement. → Task 1.
5. **A scene authored before this slice looks dark.** The π change alters what "intensity" means; `scene.json` is the one committed instance and must be migrated in the same change, or the repo ships a scene that renders wrong. → Task 2.

---

### Task 1: Swap the parameters and the BRDF

Large by necessity: the rename crosses a push-constant boundary, so there is no state where half of it compiles. Ends with the engine and shader coherent and the suite green — and the demo visibly dark, which Task 2 fixes.

**Files:**
- Modify: `engine/render/material.hpp`
- Modify: `engine/render/render_device.hpp:62-72` (`DrawParams`)
- Modify: `engine/render/render_system.hpp:152-178`
- Modify: `modules/render_vulkan/shaders/cube.frag` (push block; `blinn_spec` → BRDF; `main`'s three light terms)
- Modify: `tests/unit/render/render_system_test.cpp:1352-1372` and the defaults case following it
- Modify: `tests/unit/reflection/lighting_reflection_test.cpp:50-51`
- Modify: `tests/unit/render/shader_specular_contract_test.cpp` (rewrite the clamp case)
- Modify: `app/run/demo_scene.hpp` (six `Material` initialisers — minimum to compile and stay meaningful)
- Modify: `tests/unit/app/demo_scene_test.cpp:180-205`

**Interfaces:**
- Consumes: nothing.
- Produces: `Material{ Vec3 base_color; AssetID texture; float metallic; float roughness; }` and `DrawParams{ ..., Vec3 base_color; float metallic; float roughness; }`, in that field order. Task 2 relies on those names.

- [ ] **Step 1: Write the failing shader-contract tests**

Replace the body of `tests/unit/render/shader_specular_contract_test.cpp`'s first case and add two more. Keep the file's existing `read_shader` helper and its second case (the `FrameLighting` block equality) untouched.

Replace:

```cpp
TEST_CASE("cube.frag clamps the specular exponent because the engine deliberately does not", "[render]")
{
    const std::string frag = read_shader("cube.frag");
    INFO("If this fails, either restore the clamp in blinn_spec() or move the clamp "
         "into the engine AND relax the tests that guarantee a negative shininess "
         "reaches the GPU unchanged. The two halves must not disagree.");
    REQUIRE(frag.find("max(shininess, 1.0)") != std::string::npos);
}
```

with:

```cpp
TEST_CASE("cube.frag clamps roughness because the engine deliberately does not", "[render]")
{
    // Review Focus 1. Same contract as the shininess clamp this replaces: Material
    // round-trips byte-for-byte, so a roughness of 0 authored in a scene file reaches
    // the GPU unchanged. alpha = roughness * roughness, and alpha == 0 makes the GGX
    // denominator (NdotH*NdotH*(alpha2-1) + 1) zero at NdotH == 1 -- so D is inf and
    // the highlight centre blows to white. Nothing else pins this: the C++ cannot see
    // shader source, and inf is not a crash.
    const std::string frag = read_shader("cube.frag");
    INFO("If this fails, either restore the roughness clamp in cube.frag or move it "
         "into the engine AND relax the tests that guarantee an authored roughness "
         "reaches the GPU unchanged. The two halves must not disagree.");
    REQUIRE(frag.find("max(roughness, 0.045)") != std::string::npos);
}

TEST_CASE("cube.frag guards the specular denominator against grazing angles", "[render]")
{
    // Review Focus 2: NdotL and NdotV appear in 4 * NdotL * NdotV. At a silhouette
    // edge NdotV approaches zero, which happens on every frame of every scene, so this
    // is the ordinary case rather than an edge case.
    const std::string frag = read_shader("cube.frag");
    REQUIRE(frag.find("max(dot(n, v), 1e-4)") != std::string::npos);
    REQUIRE(frag.find("max(dot(n, L), 1e-4)") != std::string::npos);
}

TEST_CASE("cube.frag gives a fully metallic surface no diffuse", "[render]")
{
    // Review Focus 3, and the single assertion that separates "metal" from "shiny
    // plastic". kD = (1 - F) * (1 - metallic): drop the (1 - metallic) factor and every
    // metal gains a diffuse lobe, which is the exact failure this slice exists to
    // prevent. It is one token wide.
    const std::string frag = read_shader("cube.frag");
    REQUIRE(frag.find("(1.0 - metallic)") != std::string::npos);
    // And the dielectric F0 constant, which is physical rather than tunable.
    REQUIRE(frag.find("mix(vec3(0.04), albedo, metallic)") != std::string::npos);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20 2>&1 | grep -E "error" | head -5
```

Expected: a **compile** error, not a test failure — `tests/unit/render/render_system_test.cpp` and the other test files still reference `mat.shininess`, which `Material` will not have until Step 3. That is the red state for a rename. Record the first error message.

- [ ] **Step 3: Swap the engine-side fields**

In `engine/render/material.hpp`, replace the two fields and their `reflect` lines:

```cpp
    // Metallic/roughness workflow. base_color above serves two roles: albedo for a
    // dielectric, and the specular colour F0 for a metal -- which is why it is "base"
    // colour rather than albedo. roughness is perceptually linear across 0..1, which
    // the raw Phong exponent it replaces was not.
    //
    // Neither is clamped here: the shader clamps roughness to >= 0.045, deliberately,
    // so a scene file round-trips byte-for-byte. Same contract shininess had.
    float metallic = 0.0f;  // 0 = dielectric, 1 = metal
    float roughness = 0.5f; // 0 = mirror, 1 = fully rough
```

```cpp
    v.member("metallic", m.metallic);
    v.member("roughness", m.roughness);
```

In `engine/render/render_device.hpp`'s `DrawParams`, replace the two floats and their comment:

```cpp
    // Metallic/roughness. Defaults match Material's, so the no-Material draw path and
    // a default-constructed DrawParams agree. roughness is NOT clamped here; the shader
    // clamps it to >= 0.045.
    float metallic = 0.0f;
    float roughness = 0.5f;
```

In `engine/render/render_system.hpp`, rename the three uses — the locals, the copies from `Material`, and the `DrawParams` initialiser:

```cpp
                float metallic = 0.0f;
                float roughness = 0.5f;
```
```cpp
                    metallic = mat->metallic;
                    roughness = mat->roughness;
```
```cpp
                            DrawParams{projection * view * wt->matrix, wt->matrix,
                                       normal_matrix, base_color, metallic, roughness});
```

- [ ] **Step 4: Replace the shader's push block and shading math**

In `modules/render_vulkan/shaders/cube.frag`, change the push block's two floats:

```glsl
    float metallic;
    float roughness;
} pc;
```

Delete `blinn_spec()` entirely — including its comment, whose claim that specular must never be tinted by albedo is correct for Blinn-Phong and **wrong** for PBR, since a metal's specular *is* its base colour. Replace it with:

```glsl
const float PI = 3.14159265;

// GGX / Trowbridge-Reitz normal distribution.
float distribution_ggx(float NdotH, float alpha)
{
    float a2 = alpha * alpha;
    float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-7);
}

// Smith geometry, Schlick-GGX, with the direct-lighting remap k = alpha / 2.
float geometry_smith(float NdotV, float NdotL, float alpha)
{
    float k  = alpha * 0.5;
    float gv = NdotV / (NdotV * (1.0 - k) + k);
    float gl = NdotL / (NdotL * (1.0 - k) + k);
    return gv * gl;
}

// Schlick Fresnel.
vec3 fresnel_schlick(float cos_theta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0);
}

// One light's Cook-Torrance contribution. L points FROM the fragment TOWARD the light
// and is unit length; the caller multiplies the result by that light's own
// shadow/attenuation/cone factor and by its colour.
//
// Returns diffuse + specular together, because kD depends on F and so the two terms
// cannot be computed independently -- that coupling IS the energy conservation.
vec3 brdf(vec3 n, vec3 v, vec3 L, vec3 albedo, float metallic, float roughness)
{
    // Review Focus 1: alpha == 0 makes D infinite at NdotH == 1. The engine does not
    // clamp, by design, so this is the only guard.
    float alpha = max(roughness, 0.045);
    alpha = alpha * alpha;

    // Review Focus 2: both appear in the 4 * NdotV * NdotL denominator, and NdotV goes
    // to zero at every silhouette edge.
    float NdotV = max(dot(n, v), 1e-4);
    float NdotL = max(dot(n, L), 1e-4);

    vec3 hv = v + L;
    // v == -L gives a zero-length half-vector and normalize() would return NaN, which
    // propagates to the output as an undiagnosable pixel. Carried over from blinn_spec.
    vec3  h = (dot(hv, hv) > 1e-8) ? normalize(hv) : n;
    float NdotH = max(dot(n, h), 0.0);
    float HdotV = max(dot(h, v), 0.0);

    vec3  F0 = mix(vec3(0.04), albedo, metallic);
    vec3  F  = fresnel_schlick(HdotV, F0);
    float D  = distribution_ggx(NdotH, alpha);
    float G  = geometry_smith(NdotV, NdotL, alpha);

    vec3 specular = (D * G) * F / max(4.0 * NdotV * NdotL, 1e-7);

    // Review Focus 3: (1 - metallic) is what makes a metal read as metal rather than as
    // shiny plastic. Metals have no diffuse lobe.
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;

    // The step() gate kills contributions on geometry facing away from the light, which
    // the half-vector alone would happily produce. Carried over from blinn_spec.
    return (diffuse + specular) * NdotL * step(0.0, dot(n, L));
}
```

Then replace `main`'s shading tail. The three light terms become three `brdf()` calls, each still multiplied by its own factor and its own light colour:

```glsl
    vec3 dir_term  = brdf(n, v, -l, albedo, pc.metallic, pc.roughness) * shadow;
    vec3 pt_term   = brdf(n, v, pointL, albedo, pc.metallic, pc.roughness) * atten;
    vec3 spot_term = brdf(n, v, spotLdir, albedo, pc.metallic, pc.roughness)
                   * spotAtten * coneAtten;

    // Ambient stays a flat hack until IBL (slice 4), but is kD-weighted so metals get
    // no ambient diffuse.
    vec3 F0_amb = mix(vec3(0.04), albedo, pc.metallic);
    vec3 kD_amb = (vec3(1.0) - F0_amb) * (1.0 - pc.metallic);
    vec3 color  = albedo * 0.1 * kD_amb
                + dir_term * fl.light_color.xyz
                + pt_term * fl.point_color.xyz
                + spot_term * fl.spot_color.xyz;
    outColor = vec4(tonemap_aces(color), 1.0);
```

Keep `tonemap_aces` and the single `outColor` write exactly as they are — the slice-1 contract tests assert there is precisely one write and that it is the mapped one.

- [ ] **Step 5: Update the three C++ test files and the demo materials**

`tests/unit/reflection/lighting_reflection_test.cpp`, replacing the two assignments and whatever assertions read them back:

```cpp
    mat.metallic = 1.0f;
    mat.roughness = 0.25f;
```

`tests/unit/render/render_system_test.cpp`, the marshalling case:

```cpp
    Material mat{Vec3{0.2f, 0.4f, 0.6f}};
    mat.metallic = 1.0f;
    mat.roughness = 0.25f;
    world.add_component<Material>(mesh, mat);

    rs.render(world, device, registry);

    REQUIRE(device.draws.size() == 1);
    const DrawParams& p = device.draws[0].params;
    REQUIRE(p.metallic == Approx(1.0f));
    REQUIRE(p.roughness == Approx(0.25f));
```

and rename the defaults case to match, asserting `metallic == Approx(0.0f)` and `roughness == Approx(0.5f)` both with and without a `Material` — that is Review Focus 4.

`app/run/demo_scene.hpp`, the six `Material` initialisers. These are positional, so leaving them would silently reinterpret a shininess as a metallic. Converted values per the Global Constraints:

| Line | Surface | Was | Becomes |
|---|---|---|---|
| ~389 | Ground | `16.0f, 0.15f` | `0.0f, 0.333f` |
| ~408 | Textured box | `32.0f, 0.0f` | `0.0f, 0.243f` |
| ~423 | Red sphere | `64.0f, 0.9f` | `0.0f, 0.174f` |
| ~442 | Capsule | `8.0f, 0.6f` | `0.0f, 0.447f` |
| ~451 | Second box | `32.0f, 0.5f` | `0.0f, 0.243f` |
| ~541 | Green box | `256.0f, 0.35f` | `0.0f, 0.088f` |

The red sphere becomes metallic in **Task 2**, not here — Task 1 keeps every surface dielectric so that Task 2's visual change is one variable, not two.

`tests/unit/app/demo_scene_test.cpp:180-205`. The old assertions were "6 materials, ≥1 with `specular_strength == 0`, and a shininess spread from ≤16 to ≥128". `specular_strength == 0` has **no PBR equivalent** — it was a fudge factor, and the nearest concept is high roughness. So the matte assertion is replaced by a roughness spread, which is what it was really testing:

```cpp
    std::vector<float> roughness;
    std::vector<float> metallic;
    world.for_each<mosaic::engine::render::Material>(
        [&](World::EntityID, mosaic::engine::render::Material& m)
        {
            roughness.push_back(m.roughness);
            metallic.push_back(m.metallic);
        });

    REQUIRE(roughness.size() == 6);

    // A genuine spread rather than six copies of the default. The old form of this
    // test also asserted "at least one fully matte material" via
    // specular_strength == 0, which has no PBR equivalent -- it was an authored fudge
    // factor, and the nearest concept is simply high roughness. The spread below is
    // what that assertion was really checking.
    float lo = roughness[0], hi = roughness[0];
    for (float r : roughness) { lo = (r < lo) ? r : lo; hi = (r > hi) ? r : hi; }
    REQUIRE(hi >= 0.4f);  // a broad, soft highlight
    REQUIRE(lo <= 0.1f);  // a tight, obvious one
```

- [ ] **Step 6: Run the tests to verify they pass**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
./build/debug/tests/mosaic_unit_tests "[render]"
./build/debug/tests/mosaic_unit_tests "[app]"
```

Expected: PASS, including slice 1's tone-map contracts and the `FrameLighting` block-equality case.

- [ ] **Step 7: Prove the three guards are load-bearing**

One mutation each, restoring after every one. The point is that none of these crashes — they produce a wrong image with a green suite, which is why they are pinned textually.

```bash
# (a) roughness clamp: change max(roughness, 0.045) to roughness
# (b) grazing guard:   change max(dot(n, v), 1e-4) to dot(n, v)
# (c) metal diffuse:   change (1.0 - metallic) to 1.0
cmake --build --preset debug --target mosaic_unit_tests -j 20
./build/debug/tests/mosaic_unit_tests "[render]" 2>&1 | tail -4
```

Expected: each mutation fails its own case and only its own case. Record which case each one broke; if a mutation fails nothing, that guard is unpinned and the test needs fixing before proceeding.

- [ ] **Step 8: Run the full gate**

```bash
cmake --build --preset debug -j 20
```

Wait for it to exit, then:

```bash
ctest --preset debug --output-on-failure
```

Expected: PASS, no `-E` exclusions.

- [ ] **Step 9: Commit**

```bash
git add engine/render/material.hpp engine/render/render_device.hpp engine/render/render_system.hpp modules/render_vulkan/shaders/cube.frag app/run/demo_scene.hpp tests/unit/render/render_system_test.cpp tests/unit/render/shader_specular_contract_test.cpp tests/unit/reflection/lighting_reflection_test.cpp tests/unit/app/demo_scene_test.cpp
git commit -m "feat(render): metallic/roughness parameters and a GGX Cook-Torrance BRDF"
```

---

### Task 2: Re-author the lights, and make one surface metal

Task 1 leaves the demo dark, because diffuse now divides by π while the lights still carry their old values. This task restores the exposure and gives the `metallic` parameter something to look at.

**Files:**
- Modify: `app/run/demo_scene.hpp` (three light initialisers; the red sphere's `metallic`)
- Modify: `scene.json` (one `PointLight` intensity)
- Modify: `tests/unit/app/demo_scene_test.cpp` (a metal assertion)

**Interfaces:**
- Consumes: `Material`'s `metallic`/`roughness` from Task 1.
- Produces: nothing.

- [ ] **Step 1: Write the failing test**

Append to the demo-scene material case in `tests/unit/app/demo_scene_test.cpp`, inside the same `TEST_CASE` that now collects `roughness` and `metallic`:

```cpp
    // The metallic parameter needs a visible user, or the slice ships a knob nothing
    // exercises. Exactly one metal keeps Task 2's visual change to one variable --
    // and seeing metal look like metal is the one thing a human check is for, since no
    // test here can show the BRDF is correct.
    int metals = 0;
    for (float m : metallic) { if (m >= 0.5f) { ++metals; } }
    REQUIRE(metals == 1);
```

- [ ] **Step 2: Run it to verify it fails**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
./build/debug/tests/mosaic_unit_tests "[app]" 2>&1 | tail -4
```

Expected: FAIL with `0 == 1` — Task 1 left every demo surface dielectric.

- [ ] **Step 3: Make the red sphere metal, and re-author the lights**

In `app/run/demo_scene.hpp`, the red sphere (~line 423) becomes:

```cpp
        render::Material{Vec3{0.75f, 0.18f, 0.18f}, {}, 1.0f, 0.174f});
```

The three lights, with the π compensation from the Global Constraints:

```cpp
        render::DirectionalLight{Vec3{-0.5f, -1.0f, -0.3f}, Vec3{3.142f, 3.142f, 3.142f}});
```
```cpp
    world.add_component<render::PointLight>(point, render::PointLight{Vec3{1, 0.85f, 0.6f}, 9.425f, 8.0f});
```
```cpp
        render::SpotLight{Vec3{0.5f, 0.7f, 1.0f}, 18.850f, 12.0f, 0.35f, 0.55f});
```

There is a comment block above the spot light (~lines 470-472) reasoning about intensity 4 and 6 against attenuation. Update its numbers to the new scale, or it documents a scene that no longer exists.

In `scene.json`, the single `PointLight`'s `"intensity": 3.0` becomes `9.425`. Its `Material` entries need no edit — they carry no specular fields.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
./build/debug/tests/mosaic_unit_tests "[app]"
./build/debug/tests/mosaic_unit_tests "[render]"
```

Expected: PASS.

- [ ] **Step 5: Prove the scene.json migration matters**

Review Focus 5. Revert only `scene.json`'s intensity to 3.0 and load it:

```bash
git stash push scene.json && timeout 40 ./build/debug/app/mosaic run --window --scene scene.json --duration 8; git stash pop
```

Expected: the scene renders visibly darker than with 9.425. `--scene` is confirmed to exist — `mosaic run --help` documents it as "Scene JSON file to load before running", mutually exclusive with the positional `path`. This is a visual comparison, so record what you saw rather than inferring it from an exit code; if the difference is not apparent, say so, because that would mean either the migration does not matter or the scene's one light is not what lights it.

- [ ] **Step 6: Run the full gate**

```bash
cmake --build --preset debug -j 20
```

then:

```bash
ctest --preset debug --output-on-failure
```

Expected: PASS, no `-E` exclusions.

- [ ] **Step 7: Verify both entry points**

```bash
timeout 40 ./build/debug/app/mosaic run --window --duration 10
timeout 40 ./build/debug/app/mosaic run --window --script-thread --duration 10
```

Expected for both: exit 0, zero Vulkan validation errors, no Python errors.

- [ ] **Step 8: Stop and ask the human partner to look**

No test in this plan can show the BRDF is correct — only that it is wired, guarded, and round-trips. Tell them what to expect so a correct result is not mistaken for a regression:

1. **The red sphere should read as metal** — tinted specular, no diffuse lobe, darker where it is not catching a highlight.
2. **The image will look different, not identical-with-better-highlights.** Three things changed at once: diffuse ÷π, intensities ×π, and the lobe shape Phong → GGX. The first two cancel on average but not per-pixel, and GGX has a longer tail, so highlights read broader and softer at the same nominal roughness.
3. **Overall brightness should be roughly comparable** to before. Noticeably darker or blown out means the π compensation is off.

- [ ] **Step 9: Commit**

```bash
git add app/run/demo_scene.hpp scene.json tests/unit/app/demo_scene_test.cpp
git commit -m "feat(render): re-author the demo lights for energy-conserving diffuse, and add a metal"
```
