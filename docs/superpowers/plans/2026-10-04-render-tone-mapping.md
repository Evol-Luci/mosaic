# Tone Mapping Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop the renderer clipping bright highlights, by tone mapping `cube.frag`'s output with an ACES filmic curve.

**Architecture:** One shader change. `cube.frag` gains a `tonemap_aces()` function applied to the summed light contributions before `outColor`. No offscreen HDR target, because nothing in the renderer blends additively. No `Material`, push-constant, UBO, or C++ interface change.

**Tech Stack:** GLSL (Vulkan), the existing `glslc` compile step, Catch2 textual shader-contract tests.

**Spec:** `docs/superpowers/specs/2026-10-04-pbr-decomposition-and-tone-mapping-design.md` — read Part Two; Part One is the four-slice decomposition this is slice 1 of.

## Global Constraints

- **`cube.frag` ONLY.** It is the single shader whose output can exceed 1.0. `particle.frag`, `ribbon.frag`, `decal.frag` and `world_text.frag` are unlit — they output authored colour times alpha — and the four UI shaders are authored in display space. Tone mapping any of them would darken authored colour, because the curve maps 1.0 to about 0.8.
- **Do NOT add gamma correction.** The pipeline is already linear: the swapchain is `B8G8R8A8_SRGB` and textures are `R8G8B8A8_SRGB`, so the hardware converts at both ends. A manual `pow(c, 1.0/2.2)` would double-correct. No `2.2` constant belongs in any fragment shader.
- **The curve is the Narkowicz ACES approximation, with these exact constants:** `a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14`, as `clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0)`.
- **No exposure parameter.** The curve is hard-coded. Exposure is a camera concern with no current caller.
- **Do not introduce an offscreen HDR render target or a resolve pass.** Deferred deliberately; the trigger for revisiting is the addition of any additive-blending pipeline.
- Do not change `Material`, `PushConstants`, the `FrameLighting` UBO, or any C++ interface. This slice touches one `.frag` file and one test file.
- Build: `cmake --build --preset debug -j 20`. Test binary: `./build/debug/tests/mosaic_unit_tests`. Gate: `ctest --preset debug --output-on-failure` — **no `-E` exclusions**; they were removed on 2026-09-30 and must not come back.
- **Run only one build at a time** — concurrent `cmake --build` in this tree corrupts archives.
- Never `git add -A`: `docs/project/advancement-plans-and-evaluation.md` is modified and must never be staged. Stage explicit paths only.
- **Never `git commit --amend` without first checking `git log -1`** — a commit rejected by the pre-commit hook leaves HEAD on the previous commit, and amending then rewrites that one.
- The pre-commit hook regenerates `docs/index.html` and `docs/reference/*.md`. If it rejects a commit saying generated docs were stale, `git add` the regenerated files it names and commit again.
- Catch2 `TEST_CASE` names: no commas inside the name, no leading `--`, no unmatched `[`.
- Shaders are compiled by the build; a GLSL syntax error fails the build, not a test. Read build output, do not assume success.

## Review Focus

Failure modes the spec implies that no task's tests would otherwise exercise. Each gets a test in the task that owns it.

1. **A second `outColor` write added below the tone map** would silently bypass it — the function would still be present and still be called, so an "is `tonemap_aces` applied?" test passes while the attachment receives unmapped values. → Task 1.
2. **Someone "fixes" the inconsistency** by tone mapping the unlit scene shaders, darkening every particle, ribbon, decal and world-space label for no benefit. Decision 1 is a judgement a later reader will question, so it needs a test to argue with. → Task 1.
3. **Someone adds manual gamma correction** on top of the sRGB swapchain, double-correcting and washing the image out. This is the single most likely wrong "improvement" here, because tone mapping and gamma are routinely conflated. → Task 1.
4. **The curve constants get "simplified"** to Reinhard (`x / (1 + x)`) or mistyped. Reinhard is a plausible-looking edit that compiles, runs, and desaturates highlights. → Task 1.
5. **The existing specular-exponent clamp gets lost** while editing the same shader's tail. `shader_specular_contract_test.cpp` already pins it, so this needs no new test — it needs the existing suite run, which Task 1's steps do. → covered by Task 1 Step 6.

---

### Task 1: Tone map `cube.frag`

The whole slice. One shader change plus the contract tests that pin the four decisions a later reader is most likely to undo.

**Files:**
- Modify: `modules/render_vulkan/shaders/cube.frag` (the `tonemap_aces` function, and the `outColor` write at the end of `main`)
- Create: `tests/unit/render/shader_tonemap_contract_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces: nothing consumed by a later task. Slices 2–4 of the decomposition depend on tone mapping *existing*, not on any symbol from it.

- [ ] **Step 1: Write the failing contract tests**

Create `tests/unit/render/shader_tonemap_contract_test.cpp`. This follows the idiom of the existing `shader_specular_contract_test.cpp`: there is no GPU in CI, so shader guarantees are pinned textually against the source file. Note the `MOSAIC_TEST_DIR` path form — copy it exactly.

```cpp
// Pins the four tone-mapping decisions a later reader is most likely to undo.
//
// There is no GPU in CI, so these are textual contracts against the shader source --
// the same idiom as shader_specular_contract_test.cpp. They pin the WIRING. They
// cannot show that the curve looks right; that is a human check against
// `mosaic run --window`, and the spec says so rather than letting a green suite
// imply more.

#include <fstream>
#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace
{
std::string read_shader(const std::string& name)
{
    const std::string path = std::string(MOSAIC_TEST_DIR) + "/../modules/render_vulkan/shaders/" + name;
    std::ifstream in(path);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
} // namespace

TEST_CASE("cube.frag tone maps its output with the ACES approximation", "[render]")
{
    const std::string frag = read_shader("cube.frag");
    REQUIRE(frag.find("tonemap_aces") != std::string::npos);

    // Review Focus 4: Reinhard (x / (1 + x)) is a plausible-looking "simplification"
    // that compiles, runs, and desaturates highlights. Pin the actual constants so
    // swapping the curve has to argue with a test.
    REQUIRE(frag.find("2.51") != std::string::npos);
    REQUIRE(frag.find("2.43") != std::string::npos);
    REQUIRE(frag.find("0.59") != std::string::npos);
    REQUIRE(frag.find("0.14") != std::string::npos);
}

TEST_CASE("cube.frag writes only the tone-mapped colour to its attachment", "[render]")
{
    // Review Focus 1: the sharp edge of this slice. A second outColor write added
    // below the tone map bypasses it entirely -- tonemap_aces would still be present
    // and still be called, so a mere "is it applied?" check stays green while the
    // attachment receives unmapped values. Assert there is exactly ONE write, and
    // that it is the mapped one.
    const std::string frag = read_shader("cube.frag");

    std::size_t writes = 0;
    for (std::size_t i = frag.find("outColor ="); i != std::string::npos;
         i = frag.find("outColor =", i + 1))
    {
        ++writes;
    }
    INFO("cube.frag must assign outColor exactly once, and from tonemap_aces(). If you "
         "added a second write (a debug visualisation, an early-out), the tone map is "
         "being bypassed for those fragments.");
    REQUIRE(writes == 1);
    REQUIRE(frag.find("outColor = vec4(tonemap_aces(") != std::string::npos);
}

TEST_CASE("the unlit scene shaders are deliberately not tone mapped", "[render]")
{
    // Review Focus 2: decision 1 of the spec, and a judgement a later reader will
    // question. These four shaders output authored colour times alpha and cannot
    // exceed 1.0, so tone mapping them would DARKEN them -- the ACES curve maps 1.0
    // to about 0.8. A "consistency" edit here is a regression, so make it loud.
    for (const char* name : {"particle.frag", "ribbon.frag", "decal.frag", "world_text.frag"})
    {
        const std::string frag = read_shader(name);
        INFO(std::string("shader: ") + name);
        REQUIRE(frag.find("tonemap") == std::string::npos);
    }
}

TEST_CASE("no fragment shader gamma-corrects on top of the sRGB swapchain", "[render]")
{
    // Review Focus 3: the likeliest wrong "improvement" here, because tone mapping and
    // gamma are routinely conflated. The pipeline is ALREADY linear -- the swapchain is
    // B8G8R8A8_SRGB and textures are R8G8B8A8_SRGB, so the hardware converts at both
    // ends. A manual pow(c, 1.0/2.2) would double-correct and wash the image out.
    for (const char* name : {"cube.frag", "particle.frag", "ribbon.frag", "decal.frag",
                             "world_text.frag", "screen_effect.frag"})
    {
        const std::string frag = read_shader(name);
        INFO(std::string("shader: ") + name);
        REQUIRE(frag.find("2.2") == std::string::npos);
        REQUIRE(frag.find("0.4545") == std::string::npos); // 1/2.2, the other spelling
    }
}
```

Register it in `tests/CMakeLists.txt` immediately after the `unit/render/shader_specular_contract_test.cpp` line:

```
  unit/render/shader_tonemap_contract_test.cpp
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
./build/debug/tests/mosaic_unit_tests "[render]"
```

Expected: the first two cases FAIL (no `tonemap_aces` in `cube.frag`; the single `outColor` write is not the mapped one). The third and fourth cases should PASS already — the unlit shaders have no tone map and no shader has manual gamma today. Record that split: those two are *characterization* tests pinning existing behaviour, and a failure in either at this point means the premise is wrong and the spec needs correcting before proceeding.

- [ ] **Step 3: Add the tone-map function**

In `modules/render_vulkan/shaders/cube.frag`, add after the existing `blinn_spec()` function and before `main()`:

```glsl
// ACES filmic approximation (Narkowicz). Maps linear HDR to the linear display range
// so bright highlights roll off instead of clipping flat against the 8-bit target.
//
// This must NOT also gamma-correct: the swapchain is B8G8R8A8_SRGB, so the hardware
// encodes linear -> sRGB on write. A pow(c, 1.0/2.2) here would double-correct.
//
// Applied in cube.frag only. The other scene shaders are unlit -- they output authored
// colour times alpha and cannot exceed 1.0 -- so mapping them would darken authored
// colour, since this curve takes 1.0 to roughly 0.8.
vec3 tonemap_aces(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
```

- [ ] **Step 4: Apply it at the single output write**

In the same file, replace the last line of `main()`:

```glsl
    outColor = vec4(color, 1.0);
```

with:

```glsl
    outColor = vec4(tonemap_aces(color), 1.0);
```

Change nothing else. Leave the three light contributions, the `0.1` ambient, the shadow factor, and `blinn_spec`'s `max(shininess, 1.0)` clamp exactly as they are — that clamp is pinned by `shader_specular_contract_test.cpp` and dropping it would blow highlights to white with a green suite.

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
./build/debug/tests/mosaic_unit_tests "[render]"
```

Expected: PASS, all four new cases plus every pre-existing `[render]` case — including the two in `shader_specular_contract_test.cpp`, which is Review Focus 5's coverage.

A GLSL syntax error shows up as a **build** failure here, not a test failure. Read the build output rather than skipping to the test result.

- [ ] **Step 6: Run the full gate**

```bash
cmake --build --preset debug -j 20
```

Wait for it to exit, then:

```bash
ctest --preset debug --output-on-failure
```

Expected: PASS, with **no `-E` exclusions**.

- [ ] **Step 7: Prove the tone map is load-bearing**

Temporarily restore the unmapped write:

```glsl
    outColor = vec4(color, 1.0);   // MUTATION
```

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
./build/debug/tests/mosaic_unit_tests "[render]" 2>&1 | tail -5
```

Expected: FAIL on "writes only the tone-mapped colour to its attachment". Note that the *first* case still passes, because `tonemap_aces` is still defined — that asymmetry is exactly why Review Focus 1 needed its own test, and it is worth recording as a measured result.

Then run a second mutation: restore the mapped write and instead add a stray second write below it (`outColor = vec4(color, 1.0);` on the following line).

Expected: FAIL on the same case, now on `writes == 1`. This is the bypass the test exists for.

Restore the correct single mapped write, rebuild, and confirm `[render]` is green.

- [ ] **Step 8: Verify both entry points, and look at it**

```bash
timeout 40 ./build/debug/app/mosaic run --window --duration 10
timeout 40 ./build/debug/app/mosaic run --window --script-thread --duration 10
```

Expected for both: exit 0, **zero Vulkan validation errors**, no Python errors.

**Then stop and ask the human partner to look at the window.** The contract tests pin the wiring and cannot show the curve looks right. Two things to tell them to expect, so a correct result is not mistaken for a regression:

1. Bright specular highlights should roll off rather than clipping to flat white.
2. **The whole image gets slightly dimmer**, not only the bright parts — ACES maps 1.0 to about 0.8. This is correct; it is what makes headroom for the highlights.

- [ ] **Step 9: Commit**

```bash
git add modules/render_vulkan/shaders/cube.frag tests/unit/render/shader_tonemap_contract_test.cpp tests/CMakeLists.txt
git commit -m "feat(render): tone map the lit pass with an ACES filmic curve"
```
