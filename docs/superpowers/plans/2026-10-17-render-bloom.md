# Bloom Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Light above display white glows, via a thresholded mip chain built from
the HDR scene target and composited inside the tone curve.

**Architecture:** A new authored `Bloom` component on the camera entity carries
intensity, threshold and knee. `RenderSystem` reads the active camera's, calls a
single new device hook `build_bloom(BloomParams)` between `end_hdr_scene_pass`
and `draw_hdr_resolve`, and passes intensity to the resolve. The Vulkan backend
owns everything else: one half-resolution `R16G16B16A16_SFLOAT` image with N
mips, one render pass, three pipelines (prefilter, downsample, additive
upsample) driven by one loop, and `N + 1` descriptor sets.

**Tech Stack:** C++20, Vulkan 1.x, GLSL 450 compiled by `glslc`, Catch2 v3,
CMake + Ninja.

**Spec:** `docs/superpowers/specs/2026-10-17-render-bloom-design.md`

## Global Constraints

- **Tier rule.** `foundation/` → `engine/` → `studio/`/`app/`, plus `modules/`.
  Nothing in `engine/` may include from `modules/`. Enforced by
  `tools/ci/check_dependency_rules.py`; a violation fails CI.
- **`Bloom` defaults produce no visible effect:** `intensity = 0.0f`,
  `threshold = 1.0f`, `knee = 0.5f`. Every existing scene must render
  byte-identically whether or not it has the component.
- **`threshold = 1.0f` is load-bearing** — display white, so out of the box bloom
  picks up exactly the light that exceeds what the screen can show.
- **The mip count `N`** runs until the smaller dimension would fall below 8 px,
  **clamped to 6**. At 1920×1080 that is 960×540 down to 30×16.
- **The chain is half the swapchain's resolution** at mip 0.
- **Format `VK_FORMAT_R16G16B16A16_SFLOAT`**, usage
  `COLOR_ATTACHMENT | SAMPLED | TRANSFER_DST`.
- **One render pass** for the whole chain: `loadOp = VK_ATTACHMENT_LOAD_OP_LOAD`,
  `initialLayout` and `finalLayout` both
  `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`.
- **Bloom is added INSIDE the tone curve**, and exposure scales it too:
  `tonemap_aces((hdr + bloom * pc.bloom_intensity) * max(pc.exposure, 0.0))`.
- **CI has no GPU.** Nothing in this plan can be verified by running Vulkan. The
  house idiom for a GPU-side guarantee is a **textual contract test** that reads
  the shader or backend source and asserts the guard is present —
  `tests/unit/render/shader_source.hpp` provides `read_shader`, `read_backend`,
  `code_only`, `normalize_ws`, `count_assignments`.
- **Catch2 `TEST_CASE` names must not start with `--` and must not contain an
  unbalanced `[`** — either breaks CTest discovery.
- **Build:** `cmake --build build/debug -j4`. **Test:**
  `ctest --test-dir build/debug --output-on-failure`.
- **Commits** end with:
  ```
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_017SduWFXfqsJjE4EUC8vYat
  ```
- **The pre-commit hook aborts to regenerate `docs/index.html`.** When it does,
  re-run the **full** `git add -A docs/ && git commit` — never `--amend`.

## Review Focus

Five failure modes the spec implies that no task's happy path exercises. Each
line names the task that owns its test.

1. **`knee` authored at 0 or negative.** The prefilter's soft ramp divides by
   `4 * knee`; a zero there makes every bloomed pixel NaN, and NaN propagates
   through the whole chain and the add, so the screen goes to garbage. The
   inspector renders `knee` as an unbounded DragFloat, so 0 is one drag away.
   Expected: a zero knee degrades to a hard threshold, not to NaN. **Task 6.**
2. **A camera with no `WorldTransform`.** `find_active_camera` returns `false`
   without touching its out-parameters — the exact trap its existing `exposure`
   comment names. Uninitialised bloom parameters would ship whatever was on the
   stack: a huge intensity is a white screen. Expected: the component defaults.
   **Task 4.**
3. **`intensity` authored negative.** It multiplies the chain before the add, so
   a negative value *subtracts* light; `tonemap_aces` floors its input, so the
   result is a hard black clip around every bright object rather than a
   gradient — a confident, plausible, wrong image. Expected: floored at 0.
   **Task 4.**
4. **A window small enough that N clamps to 1.** The chain is then prefilter
   only: both the downsample and upsample loops run `N - 1 = 0` times and must
   not underflow an unsigned counter. Expected: a 1-level chain builds and
   resolves. **Task 2.**
5. **A `Bloom` on a non-camera entity, or on a second camera.** Only the active
   camera's is read, and the active camera is whichever `Camera` is found first.
   Expected: a `Bloom` elsewhere in the world changes nothing. **Task 4.**

---

## File Structure

| File | Responsibility |
|---|---|
| `engine/vfx/bloom.hpp` (new) | The `Bloom` component and its `reflect`. Nothing else. |
| `engine/reflection/builtin_components.hpp` (modify) | One registration line. |
| `modules/render_vulkan/vulkan_bloom_levels.hpp` / `.cpp` (new) | `bloom_level_count(width, height)` — pure, Vulkan-handle-free, and therefore the one piece of the backend's sizing logic a headless test can run. Mirrors the existing `vulkan_swapchain_support.hpp` precedent. |
| `engine/render/render_device.hpp` (modify) | `BloomParams`; `virtual void build_bloom(const BloomParams&)`; `draw_hdr_resolve` grows a second parameter. |
| `engine/render/render_system.hpp` (modify) | Reads the active camera's `Bloom`, calls `build_bloom`, passes intensity to the resolve. |
| `modules/render_vulkan/shaders/fullscreen_triangle.vert` (renamed from `hdr_resolve.vert`) | The one fullscreen triangle in the tree. Emits no UV, deliberately. |
| `modules/render_vulkan/shaders/bloom_prefilter.frag` (new) | Threshold + soft knee, HDR target → mip 0. |
| `modules/render_vulkan/shaders/bloom_blur.frag` (new) | 4-tap bilinear kernel. Serves BOTH the downsample and the upsample — they differ only in the pipeline's blend state, not in the shader. |
| `modules/render_vulkan/shaders/hdr_resolve.frag` (modify) | Samples the chain at set 1, adds it before the curve. |
| `modules/render_vulkan/vulkan_render_device.cpp` (modify) | The chain: resources, pass, pipelines, descriptors, `build_bloom`. |
| `modules/render_vulkan/CMakeLists.txt` (modify) | Shader compile list. |
| `app/run/demo_scene.hpp` (modify) | Authors a `Bloom` on the demo camera, so the feature is reachable by a human at the window. |

**Two shaders, three pipelines.** The spec's table names three pipelines; it
does not require three shaders. The downsample and the upsample run the same
4-tap kernel over a source that is, respectively, twice and half the
destination's size — which is exactly what makes one a box downsample and the
other a tent upsample. The only difference is blending. One shader is therefore
the honest implementation, and two copies of the same kernel would be a place
for them to drift.

---

### Task 1: The `Bloom` component

**Files:**
- Create: `engine/vfx/bloom.hpp`
- Modify: `engine/reflection/builtin_components.hpp`
- Create: `tests/unit/vfx/bloom_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces: `struct mosaic::engine::vfx::Bloom { float intensity; float threshold; float knee; };`
  with `reflect(V&, Bloom&)` visiting `"intensity"`, `"threshold"`, `"knee"` in
  that order. Registered under the reflection name `"Bloom"`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/vfx/bloom_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/reflection/type_registry.hpp"
#include "engine/vfx/bloom.hpp"
#include "foundation/ecs/world.hpp"

using mosaic::engine::vfx::Bloom;

TEST_CASE("Bloom defaults are off", "[vfx][bloom]")
{
    // The spec's load-bearing defaults. intensity 0 means adding the component to a
    // scene changes nothing until someone turns it up, so every existing scene renders
    // byte-identically whether or not it has one. threshold 1.0 is display white: out of
    // the box bloom picks up exactly the light the screen cannot show, which is the
    // headroom the HDR target was built to preserve.
    const Bloom b;
    REQUIRE(b.intensity == 0.0f);
    REQUIRE(b.threshold == 1.0f);
    REQUIRE(b.knee == 0.5f);
}

TEST_CASE("Bloom is a registered builtin component", "[vfx][bloom]")
{
    // Registration is what buys serialisation, the Studio inspector and scripting, with
    // no further code -- the same three things ScreenEffect gets. An unregistered
    // component is authorable only from C++, which is not what "authored per-camera"
    // means.
    mosaic::engine::reflection::TypeRegistry types;
    mosaic::foundation::ComponentRegistry    registry;
    mosaic::engine::reflection::register_builtin_components(types, registry);
    REQUIRE(types.has("Bloom"));
}

TEST_CASE("Bloom round-trips through reflection", "[vfx][bloom]")
{
    // Pins the member NAMES, not just their presence: a scene file written today has to
    // load tomorrow, and a rename is silent -- from_value simply leaves the field at its
    // default. Non-default values on all three, so a field read from the wrong member
    // cannot pass.
    mosaic::engine::reflection::TypeRegistry types;
    mosaic::foundation::ComponentRegistry    registry;
    mosaic::engine::reflection::register_builtin_components(types, registry);

    mosaic::foundation::World world;
    const auto               e = world.create_entity();
    world.add_component<Bloom>(e, Bloom{2.5f, 1.75f, 0.25f});

    const auto value = types.get("Bloom")->to_value(world, e);

    mosaic::foundation::World dst;
    const auto                d = dst.create_entity();
    types.get("Bloom")->from_value(value, dst, d);

    const Bloom* out = dst.get_component<Bloom>(d);
    REQUIRE(out != nullptr);
    REQUIRE(out->intensity == 2.5f);
    REQUIRE(out->threshold == 1.75f);
    REQUIRE(out->knee == 0.25f);
}
```

Then add the file to the test build. In `tests/CMakeLists.txt`, in the source
list, after the line `  unit/vfx/screen_effect_test.cpp`, add:

```cmake
  unit/vfx/bloom_test.cpp
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build/debug -j4 2>&1 | tail -20
```

Expected: FAIL at the compile step, with
`fatal error: engine/vfx/bloom.hpp: No such file or directory`.

> If instead the build fails at the LINK step for `mosaic_unit_tests` with no
> compiler diagnostic, that is the known flaky link under memory pressure — grep
> the output for a real compiler error, and if there is none, just run the build
> again.

- [ ] **Step 3: Write the component**

Create `engine/vfx/bloom.hpp`:

```cpp
#pragma once

namespace mosaic::engine::vfx
{
// Per-camera bloom: light above `threshold` is blurred and added back, which is the
// payoff for the HDR target (#255, #256) -- the first thing in this renderer that needs
// values above display white.
//
// ITS OWN COMPONENT rather than three more fields on Camera, and not a RenderSystem
// setter. Camera is about the camera's optics; ScreenEffect already established where a
// post-process knob on the camera entity lives. A component serialises with the scene,
// differs per camera, and reaches the inspector through reflection -- a setter would do
// none of those.
//
// Defaults produce no visible effect, the same rule ScreenEffect states: adding this to
// a scene changes nothing until someone turns intensity up.
struct Bloom
{
    // 0 = off, and the default. Multiplies the chain at the resolve rather than being
    // baked into it, which makes the skip case safe by ARITHMETIC: at 0 the chain
    // contributes 0 whatever it contains, so a frame that skips building it cannot show
    // the previous frame's glow and no "is the chain stale?" flag has to be right.
    float intensity = 0.0f;
    // Display white. Light above this blooms, which is exactly the headroom the HDR
    // target exists to preserve. A threshold meaningfully below 1.0 would make this a
    // general glow over ordinary lit surfaces -- a different effect.
    float threshold = 1.0f;
    // The soft ramp below the threshold, and NOT optional polish. A hard
    // max(colour - threshold, 0) makes a specular highlight drifting across the
    // threshold pop in and out between frames on a moving object, which reads as a bug
    // rather than an effect. This is the ramp that makes it fade.
    float knee = 0.5f;
};

template <class V>
void reflect(V& visitor, Bloom& b)
{
    visitor.member("intensity", b.intensity);
    visitor.member("threshold", b.threshold);
    visitor.member("knee", b.knee);
}
} // namespace mosaic::engine::vfx
```

In `engine/reflection/builtin_components.hpp`, add the include beside the other
vfx includes, and the registration line immediately after the `ScreenEffect`
one:

```cpp
#include "engine/vfx/bloom.hpp"
```

```cpp
    types.register_component<mosaic::engine::vfx::Bloom>(registry, "Bloom");
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build build/debug -j4 2>&1 | tail -5 && \
  ctest --test-dir build/debug --output-on-failure -R "Bloom" 2>&1 | tail -15
```

Expected: PASS, 3 tests.

- [ ] **Step 5: Run the whole suite**

```bash
ctest --test-dir build/debug --output-on-failure 2>&1 | tail -15
```

Expected: 100% tests passed. Registering a new component name does not change
any entity count, so the four `build_default_scene` count tests are unaffected.

> If `the threaded ScriptSystem applies script writes one tick later` or a
> physics raycast thread case fails, those are known order-dependent flakes, not
> this change. Re-run; if they pass in isolation, continue.

- [ ] **Step 6: Commit**

```bash
git add engine/vfx/bloom.hpp engine/reflection/builtin_components.hpp \
        tests/unit/vfx/bloom_test.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(vfx): the Bloom component

Authored per-camera, beside ScreenEffect, with defaults that produce no
visible effect. Registration buys serialisation, the inspector and
scripting with no further code.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017SduWFXfqsJjE4EUC8vYat
EOF
)"
```

---

### Task 2: `bloom_level_count`

**Files:**
- Create: `modules/render_vulkan/vulkan_bloom_levels.hpp`
- Create: `modules/render_vulkan/vulkan_bloom_levels.cpp`
- Modify: `modules/render_vulkan/CMakeLists.txt:2`
- Create: `tests/unit/render/bloom_levels_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces:
  ```cpp
  namespace mosaic::render::vk {
  constexpr std::uint32_t kBloomMaxLevels = 6;
  constexpr std::uint32_t kBloomMinExtent = 8;
  std::uint32_t bloom_level_count(std::uint32_t width, std::uint32_t height);
  }
  ```
  `width`/`height` are the **swapchain's**, not the chain's; mip 0 is half of
  them. Returns 0 only when either input is 0.

**Why this is its own file.** Nothing in the suite creates a `VkDevice`, so the
backend's sizing logic is unreachable from a test as long as it lives inside
`vulkan_render_device.cpp`. `modules/render_vulkan/vulkan_swapchain_support.hpp`
is the existing precedent: a pure helper in the module, compiled into
`mosaic_render_vulkan`, included directly by
`tests/unit/render/swapchain_support_test.cpp`. This is Review Focus #4's only
possible home.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/render/bloom_levels_test.cpp`:

```cpp
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "modules/render_vulkan/vulkan_bloom_levels.hpp"

using mosaic::render::vk::bloom_level_count;
using mosaic::render::vk::kBloomMaxLevels;

TEST_CASE("bloom_level_count clamps at six on a 1080p swapchain", "[render][bloom]")
{
    // The spec's worked example: mip 0 is half the swapchain, so 960x540, and halving
    // reaches 30x16 at level 6. Beyond six each step costs a render pass and contributes
    // almost nothing, so the clamp is a budget and not a limit of the math.
    REQUIRE(bloom_level_count(1920, 1080) == 6);
    REQUIRE(kBloomMaxLevels == 6);
}

TEST_CASE("bloom_level_count never returns zero for a drawable swapchain", "[render][bloom]")
{
    // Review Focus #4. A window this small gives a 1-level chain: prefilter only, with
    // the downsample and upsample loops running N-1 = 0 times. Both of those counters are
    // unsigned in the backend, so a zero here is the difference between "no blur" and
    // "loop 4294967295 times".
    REQUIRE(bloom_level_count(1, 1) == 1);
    REQUIRE(bloom_level_count(2, 2) == 1);
    REQUIRE(bloom_level_count(32, 24) == 1);
}

TEST_CASE("bloom_level_count stops before a mip falls under eight pixels", "[render][bloom]")
{
    // 64x64 -> mip0 32x32, mip1 16x16, mip2 8x8; 4x4 would be next and is refused.
    // A 4x4 mip blurred by a 4-tap kernel is one colour, which is a pass spent to learn
    // the average of the image.
    REQUIRE(bloom_level_count(64, 64) == 3);
    // The SMALLER dimension decides, so a letterboxed window does not get extra levels
    // from its long edge: 1024x64 -> mip0 512x32, mip1 256x16, mip2 128x8, stop.
    REQUIRE(bloom_level_count(1024, 64) == 3);
}

TEST_CASE("bloom_level_count returns zero for a zero extent", "[render][bloom]")
{
    // A minimised window. The backend skips creating the chain entirely rather than
    // creating a degenerate one, which matches what create_hdr_target already does.
    REQUIRE(bloom_level_count(0, 1080) == 0);
    REQUIRE(bloom_level_count(1920, 0) == 0);
    REQUIRE(bloom_level_count(0, 0) == 0);
}
```

In `tests/CMakeLists.txt`, beside `unit/render/swapchain_support_test.cpp`, add:

```cmake
  unit/render/bloom_levels_test.cpp
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build/debug -j4 2>&1 | tail -20
```

Expected: FAIL at compile, with
`fatal error: modules/render_vulkan/vulkan_bloom_levels.hpp: No such file or directory`.

- [ ] **Step 3: Write the implementation**

Create `modules/render_vulkan/vulkan_bloom_levels.hpp`:

```cpp
#pragma once

#include <cstdint>

namespace mosaic::render::vk
{

// Six levels, and the number is a budget rather than a limit of the math. At 1920x1080
// level 6 is already 30x16; each further step costs a render pass and contributes almost
// nothing to a result that is then multiplied by an intensity below 1.
constexpr std::uint32_t kBloomMaxLevels = 6;

// Stop before a mip's smaller dimension falls under this. A 4x4 mip blurred by a 4-tap
// kernel is a single colour -- a pass spent computing the average of the image.
constexpr std::uint32_t kBloomMinExtent = 8;

// How many mips a bloom chain gets for a swapchain of (width, height). Mip 0 is HALF of
// them, so the arguments are the swapchain's size and not the chain's.
//
// Returns 0 only for a zero extent (a minimised window), where the backend skips the
// chain entirely -- the same thing create_hdr_target does. Every drawable size returns at
// least 1, so the caller's N-1 loop counters are never asked to go negative while
// unsigned.
std::uint32_t bloom_level_count(std::uint32_t width, std::uint32_t height);

} // namespace mosaic::render::vk
```

Create `modules/render_vulkan/vulkan_bloom_levels.cpp`:

```cpp
#include "modules/render_vulkan/vulkan_bloom_levels.hpp"

namespace mosaic::render::vk
{

std::uint32_t bloom_level_count(std::uint32_t width, std::uint32_t height)
{
    if (width == 0 || height == 0)
    {
        return 0;
    }

    // Mip 0 is half resolution, floored at 1: a 1-pixel-wide window still gets a chain,
    // because the alternative is a special case in every caller.
    std::uint32_t w = width / 2 > 0 ? width / 2 : 1;
    std::uint32_t h = height / 2 > 0 ? height / 2 : 1;

    std::uint32_t levels = 1;
    while (levels < kBloomMaxLevels)
    {
        const std::uint32_t nw = w / 2;
        const std::uint32_t nh = h / 2;
        // The SMALLER dimension decides, so a letterboxed window does not earn extra
        // levels from its long edge.
        if (nw < kBloomMinExtent || nh < kBloomMinExtent)
        {
            break;
        }
        w = nw;
        h = nh;
        ++levels;
    }
    return levels;
}

} // namespace mosaic::render::vk
```

In `modules/render_vulkan/CMakeLists.txt`, add the source to the library, so
lines 1-4 become:

```cmake
add_library(mosaic_render_vulkan STATIC
  vulkan_swapchain_support.cpp
  vulkan_bloom_levels.cpp
  vulkan_render_device.cpp
)
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build build/debug -j4 2>&1 | tail -5 && \
  ctest --test-dir build/debug --output-on-failure -R "bloom_level_count" 2>&1 | tail -15
```

Expected: PASS, 4 tests.

- [ ] **Step 5: Commit**

```bash
git add modules/render_vulkan/vulkan_bloom_levels.hpp \
        modules/render_vulkan/vulkan_bloom_levels.cpp \
        modules/render_vulkan/CMakeLists.txt \
        tests/unit/render/bloom_levels_test.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(render): bloom_level_count, in its own testable file

Nothing in the suite creates a VkDevice, so sizing logic inside
vulkan_render_device.cpp is unreachable from a test. This follows the
vulkan_swapchain_support precedent: a pure helper in the module,
included directly by a unit test.

Pins the case the spec warned about -- a window small enough that N
clamps to 1, where both N-1 loops must reach zero without underflowing
an unsigned counter.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017SduWFXfqsJjE4EUC8vYat
EOF
)"
```

---

### Task 3: The device boundary

**Files:**
- Modify: `engine/render/render_device.hpp` (the `ScreenEffectParams` neighbourhood for the struct; the `--- offscreen HDR scene target ---` block near line 335 for the hooks)
- Modify: `tests/unit/render/render_device_hooks_test.cpp:67`
- Modify: `tests/unit/render/render_system_test.cpp` (`FakeRenderDevice`, around line 228)

**Interfaces:**
- Consumes: nothing from earlier tasks. (`Bloom` from Task 1 is read by
  `RenderSystem` in Task 4, not by the device — the device tier must not know
  about components.)
- Produces:
  ```cpp
  struct mosaic::engine::render::BloomParams { float threshold = 1.0f; float knee = 0.5f; };
  virtual void build_bloom(const BloomParams&) {}                        // default no-op
  virtual void draw_hdr_resolve(float exposure, float bloom_intensity) {} // default no-op
  ```
  `FakeRenderDevice` gains `std::vector<BloomParams> bloom_builds;` and
  `std::vector<float> bloom_intensities;`, logs `"bloom_build"` into
  `ordered_calls`, and still logs `"hdr_resolve"`.

**Why `draw_hdr_resolve` grows a parameter rather than the device caching
intensity.** The same argument the HDR spec made for exposure: a parameter is
testable where backend state is not, and `FakeRenderDevice` can see it. It also
makes the skip case safe by arithmetic — at intensity 0 the chain contributes 0
whatever it contains, so no staleness flag has to be right.

- [ ] **Step 1: Write the failing test**

In `tests/unit/render/render_device_hooks_test.cpp`, the existing case calls
`device.draw_hdr_resolve(1.0f);` at line 67 to prove the default hooks are
harmless no-ops. Replace that line with the two-argument form and add the new
hook beside it:

```cpp
    device.draw_hdr_resolve(1.0f, 0.0f);
    device.build_bloom(mosaic::engine::render::BloomParams{});
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build/debug -j4 2>&1 | tail -20
```

Expected: FAIL at compile — `no member named 'build_bloom'` and
`too many arguments to function call` on `draw_hdr_resolve`.

- [ ] **Step 3: Write the implementation**

In `engine/render/render_device.hpp`, immediately after the `ScreenEffectParams`
struct, add:

```cpp
// What the backend needs to build the bloom chain. The threshold and knee travel; the
// INTENSITY does not -- it goes to draw_hdr_resolve instead, so that a frame which skips
// building the chain still contributes exactly zero rather than the previous frame's
// glow. Safety by arithmetic rather than by a staleness flag that has to be right.
struct BloomParams
{
    float threshold = 1.0f; // display white
    float knee      = 0.5f; // the soft ramp below it
};
```

Then, inside the `--- offscreen HDR scene target ---` block, replace the single
line `virtual void draw_hdr_resolve(float /*exposure*/) {}` with:

```cpp
    // ONE HOOK, NOT N. The engine says "bloom, with these parameters" and the backend
    // decides how many passes that takes -- a mip chain's length is a backend fact about
    // a backend resource, and driving the loop from RenderSystem would have carried it
    // upward through foundation -> engine -> modules for nothing.
    //
    // Called between end_hdr_scene_pass and draw_hdr_resolve, and ONLY when the active
    // camera's Bloom::intensity is above zero. Default no-op, like the hooks around it.
    virtual void build_bloom(const BloomParams& /*params*/) {}
    // `bloom_intensity` is passed ALWAYS, including on frames where build_bloom was
    // skipped: at 0 the chain contributes 0 whatever it contains.
    virtual void draw_hdr_resolve(float /*exposure*/, float /*bloom_intensity*/) {}
```

In `tests/unit/render/render_system_test.cpp`, extend `FakeRenderDevice`.
Replace its existing `draw_hdr_resolve` override with:

```cpp
    std::vector<mosaic::engine::render::BloomParams> bloom_builds;
    std::vector<float>                               bloom_intensities;

    void build_bloom(const mosaic::engine::render::BloomParams& p) override
    {
        ordered_calls.push_back("bloom_build");
        bloom_builds.push_back(p);
    }
    void draw_hdr_resolve(float exposure, float bloom_intensity) override
    {
        ordered_calls.push_back("hdr_resolve");
        hdr_exposures.push_back(exposure);
        bloom_intensities.push_back(bloom_intensity);
    }
```

Finally, `engine/render/render_system.hpp` line 446 still calls the
one-argument form. Change it to:

```cpp
        device.draw_hdr_resolve(exposure, 0.0f);
```

This is deliberately a literal `0.0f` for now — Task 4 replaces it with the
camera's intensity. Doing it here keeps the tree building and makes Task 4's
first test fail for the right reason.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build build/debug -j4 2>&1 | tail -5 && \
  ctest --test-dir build/debug --output-on-failure 2>&1 | tail -15
```

Expected: 100% tests passed.

- [ ] **Step 5: Commit**

```bash
git add engine/render/render_device.hpp engine/render/render_system.hpp \
        tests/unit/render/render_device_hooks_test.cpp \
        tests/unit/render/render_system_test.cpp
git commit -m "$(cat <<'EOF'
feat(render): BloomParams and the build_bloom hook

One hook, not N: the engine says "bloom, with these parameters" and the
backend owns the mip count, the loop and every intermediate target.

Intensity goes to the resolve rather than to build_bloom, so a frame
that skips building the chain contributes exactly zero rather than the
previous frame's glow -- safety by arithmetic, not by a staleness flag.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017SduWFXfqsJjE4EUC8vYat
EOF
)"
```

---

### Task 4: `RenderSystem` reads the camera and drives the boundary

**Files:**
- Modify: `engine/render/render_system.hpp` (`render()` around lines 142-146 and
  442-446; `find_active_camera` around lines 714-740)
- Modify: `tests/unit/render/render_system_test.cpp` (append cases)

**Interfaces:**
- Consumes: `mosaic::engine::vfx::Bloom` (Task 1);
  `mosaic::engine::render::BloomParams`, `build_bloom`,
  `draw_hdr_resolve(float, float)` (Task 3).
- Produces: the call order
  `hdr_begin … hdr_end [bloom_build] hdr_resolve … ui`.

**`find_active_camera` grows the bloom out-parameters rather than getting its own
traversal**, for the reason its existing comment gives about `exposure`: the
active camera is whichever `Camera` is found first, so a second search could in
principle disagree with this one about which camera that is.

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/render/render_system_test.cpp`:

```cpp
TEST_CASE("build_bloom runs between the hdr pass and the resolve", "[render][bloom]")
{
    // The boundary the spec fixes: the chain is built from a finished HDR target, and
    // read by the resolve. Before end_hdr_scene_pass the target is still being written;
    // after draw_hdr_resolve it is too late to be composited.
    using mosaic::engine::vfx::Bloom;
    AssetRegistry registry;
    World         world;
    RenderSystem  rs;
    const auto    cam = add_camera(world, Vec3{0, 0, 5});
    world.add_component<Bloom>(cam, Bloom{1.0f, 1.0f, 0.5f});

    FakeRenderDevice device;
    rs.render(world, device, registry);

    const int end     = device.index_of("hdr_end");
    const int build   = device.index_of("bloom_build");
    const int resolve = device.index_of("hdr_resolve");
    REQUIRE(end >= 0);
    REQUIRE(build > end);
    REQUIRE(resolve > build);
    REQUIRE(device.bloom_builds.size() == 1); // exactly once per frame, not per camera
}

TEST_CASE("build_bloom is skipped at zero intensity", "[render][bloom]")
{
    // Bloom::intensity defaults to 0, so this is also what EVERY existing scene does.
    // Building a chain nobody reads would cost eleven render passes a frame to produce a
    // value that is then multiplied by zero.
    using mosaic::engine::vfx::Bloom;
    AssetRegistry registry;
    World         world;
    RenderSystem  rs;
    const auto    cam = add_camera(world, Vec3{0, 0, 5});
    world.add_component<Bloom>(cam, Bloom{});

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.index_of("bloom_build") == -1);
    // The resolve still runs, and still receives an intensity. It is the unconditional
    // half of the boundary: one code path, with the zero doing the work.
    REQUIRE(device.bloom_intensities.size() == 1);
    REQUIRE(device.bloom_intensities[0] == Approx(0.0f));
}

TEST_CASE("a camera with no Bloom component resolves at zero intensity", "[render][bloom]")
{
    // Every scene authored before this feature existed. Not merely "does not crash":
    // the resolve must receive 0, because it samples the chain unconditionally.
    AssetRegistry registry;
    World         world;
    RenderSystem  rs;
    add_camera(world, Vec3{0, 0, 5});

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.index_of("bloom_build") == -1);
    REQUIRE(device.bloom_intensities.size() == 1);
    REQUIRE(device.bloom_intensities[0] == Approx(0.0f));
}

TEST_CASE("the threshold and knee reaching build_bloom are the active camera's",
          "[render][bloom]")
{
    // Two cameras, each with a DIFFERENT Bloom. The active camera is whichever Camera is
    // found first, and the bloom parameters must come from that same entity -- a second
    // traversal could in principle disagree about which camera that is, which is why
    // find_active_camera carries them rather than a helper of its own.
    using mosaic::engine::vfx::Bloom;
    AssetRegistry registry;
    World         world;
    RenderSystem  rs;
    const auto    first  = add_camera(world, Vec3{0, 0, 5});
    const auto    second = add_camera(world, Vec3{0, 0, 9});
    world.add_component<Bloom>(first, Bloom{1.0f, 2.25f, 0.125f});
    world.add_component<Bloom>(second, Bloom{3.0f, 9.0f, 0.75f});

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.bloom_builds.size() == 1);
    REQUIRE(device.bloom_builds[0].threshold == Approx(2.25f));
    REQUIRE(device.bloom_builds[0].knee == Approx(0.125f));
    REQUIRE(device.bloom_intensities[0] == Approx(1.0f));
}

TEST_CASE("a Bloom on a non-camera entity is ignored", "[render][bloom]")
{
    // Review Focus #5. Bloom is a per-CAMERA knob; it is read from the camera entity and
    // nowhere else. A world.for_each<Bloom> would have picked this one up and bloomed a
    // scene whose camera never asked.
    using mosaic::engine::vfx::Bloom;
    AssetRegistry registry;
    World         world;
    RenderSystem  rs;
    add_camera(world, Vec3{0, 0, 5});
    const auto stray = world.create_entity();
    world.add_component<Bloom>(stray, Bloom{5.0f, 0.1f, 0.9f});

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.index_of("bloom_build") == -1);
    REQUIRE(device.bloom_intensities[0] == Approx(0.0f));
}

TEST_CASE("a camera with no WorldTransform falls back to the Bloom defaults",
          "[render][bloom]")
{
    // Review Focus #2. find_active_camera returns FALSE without touching its
    // out-parameters when the camera has no WorldTransform -- the exact trap its existing
    // comment names about exposure. Uninitialised bloom parameters would ship whatever was
    // on the stack, and a large intensity there is a white screen.
    //
    // A camera with no WorldTransform is the normal state of a freshly spawned entity: the
    // TransformSystem gives every Transform a WorldTransform on its first TICK, and the
    // editor opens stopped.
    using mosaic::engine::vfx::Bloom;
    AssetRegistry registry;
    World         world;
    RenderSystem  rs;
    const auto    cam = world.create_entity();
    world.add_component<mosaic::engine::render::Camera>(
        cam, mosaic::engine::render::Camera{});
    world.add_component<Bloom>(cam, Bloom{4.0f, 0.2f, 0.3f});

    FakeRenderDevice device;
    rs.render(world, device, registry);

    // No active camera, so NONE of its knobs are read -- not the exposure, not the bloom.
    REQUIRE(device.index_of("bloom_build") == -1);
    REQUIRE(device.bloom_intensities.size() == 1);
    REQUIRE(device.bloom_intensities[0] == Approx(0.0f));
    REQUIRE(device.hdr_exposures[0] == Approx(1.0f));
}

TEST_CASE("a negative bloom intensity is floored at zero", "[render][bloom]")
{
    // Review Focus #3. intensity multiplies the chain BEFORE the tone curve, so a
    // negative value subtracts light; tonemap_aces floors its own input, so the result is
    // a hard black clip around every bright object rather than a gradient -- a confident,
    // plausible, completely wrong image from one typed minus sign.
    //
    // Floored HERE and not only in the shader, so the fake device sees it: the shader
    // floor is unreachable from any headless test.
    using mosaic::engine::vfx::Bloom;
    AssetRegistry registry;
    World         world;
    RenderSystem  rs;
    const auto    cam = add_camera(world, Vec3{0, 0, 5});
    world.add_component<Bloom>(cam, Bloom{-2.0f, 1.0f, 0.5f});

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.bloom_intensities[0] == Approx(0.0f));
    // And a non-positive intensity means there is nothing to build, by the same test the
    // zero case uses.
    REQUIRE(device.index_of("bloom_build") == -1);
}

TEST_CASE("a world with no camera still resolves with zero bloom", "[render][bloom]")
{
    // The state every freshly created World is in. render() has no early return for a
    // missing camera, so the sequence is unconditional -- and the resolve samples the
    // chain unconditionally too, which is why the intensity must be a real 0 and not
    // whatever was on the stack.
    AssetRegistry registry;
    World         world;
    RenderSystem  rs;

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.index_of("hdr_resolve") >= 0);
    REQUIRE(device.index_of("bloom_build") == -1);
    REQUIRE(device.bloom_intensities.size() == 1);
    REQUIRE(device.bloom_intensities[0] == Approx(0.0f));
}
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build build/debug -j4 2>&1 | tail -5 && \
  ctest --test-dir build/debug --output-on-failure -R "bloom" 2>&1 | tail -30
```

Expected: the four `bloom_level_count` cases and the three Task 1 cases PASS;
the eight new cases FAIL. `build_bloom runs between the hdr pass and the resolve`
fails on `REQUIRE(build > end)` with `build == -1`, because nothing calls it yet.

- [ ] **Step 3: Write the implementation**

In `engine/render/render_system.hpp`, add the include beside the other vfx ones:

```cpp
#include "engine/vfx/bloom.hpp"
```

Replace the exposure initialisation and the `find_active_camera` call in
`render()` (currently lines 142-146) with:

```cpp
        // Initialised here, not inside find_active_camera: the fallback path below returns
        // false without touching them, and an uninitialised exposure would ship whatever was
        // on the stack -- a black frame at 0, a white one at a large value. The bloom knobs
        // are initialised for the same reason and have the sharper failure: a large
        // intensity on the stack is a white screen, and a zero knee is a NaN chain.
        float exposure = 1.0f;
        mosaic::engine::render::BloomParams bloom{};
        float bloom_intensity = 0.0f;
        if (!find_active_camera(world, view, projection, exposure, bloom, bloom_intensity))
        {
            view = fallback_view();
            projection = Mat4::perspective(kFallbackFovY, aspect_, kFallbackNear, kFallbackFar);
        }
```

Replace the two lines at the end of the world-space section (currently
`device.end_hdr_scene_pass();` through `device.draw_hdr_resolve(exposure, 0.0f);`)
with:

```cpp
        device.end_hdr_scene_pass();

        // Build the glow from the finished HDR target, BEFORE the resolve reads it. Only
        // when there is something to multiply it by: at zero intensity the chain would cost
        // eleven render passes a frame to produce a value the resolve then multiplies by 0.
        // That is also what every scene authored before this feature does.
        if (bloom_intensity > 0.0f)
        {
            device.build_bloom(bloom);
        }
        // A DRAW, not a pass, into whatever colour target is currently bound: the swapchain
        // in `mosaic run --window`, the offscreen viewport target in the editor. That is what
        // keeps both paths on one code path.
        //
        // The intensity is passed UNCONDITIONALLY, including on the frames above that
        // skipped the build. That is the whole reason it is a resolve parameter rather than
        // a build one: at 0 the chain contributes 0 whatever it contains, so a stale chain
        // cannot show through and no "is it stale?" flag has to be right.
        device.draw_hdr_resolve(exposure, bloom_intensity);
```

Replace `find_active_camera` (currently lines 714-740) with:

```cpp
    // `exposure` and the bloom knobs ride along rather than getting their own traversal:
    // the active camera is whichever Camera is found first, so a second search could in
    // principle disagree with this one about which camera that is.
    //
    // All four out-parameters are left UNTOUCHED when this returns false, which is why
    // every caller must initialise them first.
    bool find_active_camera(World& world, mosaic::foundation::math::Mat4& view,
                            mosaic::foundation::math::Mat4& projection,
                            float& exposure,
                            mosaic::engine::render::BloomParams& bloom,
                            float& bloom_intensity) const
    {
        using mosaic::foundation::math::Mat4;
        bool found = false;
        world.for_each<Camera>(
            [&](mosaic::foundation::EntityID e, Camera& cam)
            {
                if (found)
                {
                    return;
                }
                const auto* wt = world.get_component<transform::WorldTransform>(e);
                if (wt == nullptr)
                {
                    return;
                }
                view = Mat4::inverse_affine(wt->matrix);
                projection = Mat4::perspective(cam.fov_y_radians, aspect_, cam.near_z, cam.far_z);
                exposure = cam.exposure;
                // Read from the CAMERA ENTITY, the way ScreenEffect's knobs are authored on
                // it. A world.for_each<Bloom> would have found one anywhere in the scene and
                // bloomed a frame whose camera never asked for it.
                if (const auto* b = world.get_component<mosaic::engine::vfx::Bloom>(e))
                {
                    bloom.threshold = b->threshold;
                    bloom.knee = b->knee;
                    // Floored at 0 HERE, where the fake device can see it -- the shader's
                    // own floor is unreachable from any headless test. A negative intensity
                    // subtracts light before a curve that floors its input, which is a hard
                    // black clip around every bright object rather than a gradient.
                    bloom_intensity = b->intensity > 0.0f ? b->intensity : 0.0f;
                }
                found = true;
            });
        return found;
    }
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build build/debug -j4 2>&1 | tail -5 && \
  ctest --test-dir build/debug --output-on-failure -R "bloom" 2>&1 | tail -15
```

Expected: PASS, 15 tests.

- [ ] **Step 5: Run the whole suite**

```bash
ctest --test-dir build/debug --output-on-failure 2>&1 | tail -15
```

Expected: 100% tests passed.

- [ ] **Step 6: Run the mutation probes**

Five probes, derived from the code just written rather than from the assertion
list. For each one: make the edit, **`touch engine/render/render_system.hpp`**
(a restore by `mv` or `cp` preserves mtime, and ninja would keep the mutant
binary), rebuild, run `-R "bloom"`, confirm it goes RED, then restore and touch
again.

```bash
cp engine/render/render_system.hpp /tmp/rs.bak
# ... apply one mutation, then:
touch engine/render/render_system.hpp
cmake --build build/debug -j4 >/dev/null 2>&1
ctest --test-dir build/debug -R "bloom" 2>&1 | tail -3
cp /tmp/rs.bak engine/render/render_system.hpp && touch engine/render/render_system.hpp
```

| # | Mutation | Must turn RED |
|---|---|---|
| 1 | Move `device.build_bloom(bloom);` above `device.end_hdr_scene_pass();` | `build_bloom runs between the hdr pass and the resolve` |
| 2 | Delete the `if (bloom_intensity > 0.0f)` guard, calling `build_bloom` unconditionally | `build_bloom is skipped at zero intensity`, `a camera with no Bloom component resolves at zero intensity` |
| 3 | Pass a literal `0.0f` as `draw_hdr_resolve`'s second argument | `the threshold and knee reaching build_bloom are the active camera's` |
| 4 | Swap the two assignments: `bloom.threshold = b->knee; bloom.knee = b->threshold;` | `the threshold and knee reaching build_bloom are the active camera's` |
| 5 | Drop the floor: `bloom_intensity = b->intensity;` | `a negative bloom intensity is floored at zero` |

Expected: each probe turns at least its named case RED. A probe that stays GREEN
means the behaviour is unasserted — add the assertion before continuing.

```bash
rm -f /tmp/rs.bak
git diff --stat engine/render/render_system.hpp   # must show the real change only
```

- [ ] **Step 7: Commit**

```bash
git add engine/render/render_system.hpp tests/unit/render/render_system_test.cpp
git commit -m "$(cat <<'EOF'
feat(render): drive bloom from the active camera

find_active_camera carries the bloom knobs the way it already carries
exposure, for the reason its comment gives: the active camera is
whichever Camera is found first, so a second traversal could disagree
about which camera that is.

build_bloom runs only when intensity is above zero; the intensity itself
reaches the resolve unconditionally, so a skipped build contributes
exactly zero rather than the previous frame's glow.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017SduWFXfqsJjE4EUC8vYat
EOF
)"
```

---

### Task 5: Rename `hdr_resolve.vert` to `fullscreen_triangle.vert`

**Files:**
- Rename: `modules/render_vulkan/shaders/hdr_resolve.vert` →
  `modules/render_vulkan/shaders/fullscreen_triangle.vert`
- Modify: `modules/render_vulkan/CMakeLists.txt:18`
- Modify: `modules/render_vulkan/vulkan_render_device.cpp` (the `read_file` line
  in `create_resolve_pipeline`, around line 4221)
- Modify: `tests/unit/render/shader_tonemap_contract_test.cpp` (the case around
  line 211 that reads `hdr_resolve.vert`)

**Interfaces:**
- Consumes: nothing.
- Produces: the SPIR-V path `fullscreen_triangle.vert.spv`, which Task 8's three
  bloom pipelines load verbatim.

**Why now and not inside Task 8.** All three bloom pipelines want this shader
exactly as it is: no UV output, UV derived from `gl_FragCoord`, the
negative-height-viewport discipline living in one place. Reusing a shader named
for the resolve would be a smell, and a rename buried inside a 400-line Vulkan
task is a rename nobody reviews. It is its own commit so the diff reads as what
it is.

- [ ] **Step 1: Update the contract test first**

In `tests/unit/render/shader_tonemap_contract_test.cpp`, the case about the
Y-flip trap reads `read_shader("hdr_resolve.vert")`. Change that call to
`read_shader("fullscreen_triangle.vert")`, and update the `INFO` string in the
same case so it names the new filename. Add this case beside it:

```cpp
TEST_CASE("the tree has exactly one fullscreen triangle vertex shader", "[render]")
{
    // The three bloom pipelines and the resolve all want the same three vertices with no
    // UV output. A second copy is a place for one of them to grow an interpolated UV and
    // start sampling upside down under the negative-height viewport -- which is the bug
    // screen_effect.vert has and nobody noticed, because a vignette is radially symmetric.
    const std::string backend = mosaic::tests::read_backend();
    INFO("Every pipeline that draws a fullscreen triangle must load "
         "fullscreen_triangle.vert.spv. hdr_resolve.vert was renamed to it when bloom "
         "gained three more users; a reference to the old name is a stale path that fails "
         "at runtime, not at build time.");
    REQUIRE(backend.find("hdr_resolve.vert.spv") == std::string::npos);
    REQUIRE(backend.find("fullscreen_triangle.vert.spv") != std::string::npos);
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build/debug -j4 2>&1 | tail -5 && \
  ctest --test-dir build/debug --output-on-failure -R "fullscreen triangle" 2>&1 | tail -20
```

Expected: FAIL on `REQUIRE(backend.find("hdr_resolve.vert.spv") == std::string::npos)`
— the backend still names the old file.

> The other case will also fail, at `REQUIRE(in.good())` inside `read_shader`,
> because `fullscreen_triangle.vert` does not exist yet. That is the same
> finding.

- [ ] **Step 3: Do the rename**

```bash
git mv modules/render_vulkan/shaders/hdr_resolve.vert \
       modules/render_vulkan/shaders/fullscreen_triangle.vert
```

In `modules/render_vulkan/CMakeLists.txt` line 18, replace `hdr_resolve.vert`
with `fullscreen_triangle.vert` in the `foreach(_stage ...)` list.

In `modules/render_vulkan/vulkan_render_device.cpp`, in
`create_resolve_pipeline`, change:

```cpp
        std::vector<char> vert_code = read_file(std::string(MOSAIC_SHADER_DIR) + "/hdr_resolve.vert.spv");
```

to:

```cpp
        std::vector<char> vert_code = read_file(std::string(MOSAIC_SHADER_DIR) + "/fullscreen_triangle.vert.spv");
```

And in `modules/render_vulkan/shaders/fullscreen_triangle.vert`, replace the
comment block's final sentence (`... gl_FragCoord instead, which is framebuffer
space and so correct by construction.`) with:

```glsl
    // gl_FragCoord instead, which is framebuffer space and so correct by construction.
    // FOUR pipelines share this: the HDR resolve and bloom's prefilter, downsample and
    // upsample. One copy, so the discipline cannot be learned in one and forgotten in
    // another.
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build build/debug -j4 2>&1 | tail -5 && \
  ctest --test-dir build/debug --output-on-failure 2>&1 | tail -15
```

Expected: 100% tests passed.

- [ ] **Step 5: Verify the SPIR-V actually built under the new name**

```bash
ls build/debug/modules/render_vulkan/shaders/ | grep -E "fullscreen|hdr_resolve"
```

Expected: `fullscreen_triangle.vert.spv` and `hdr_resolve.frag.spv` are present.
A leftover `hdr_resolve.vert.spv` from an earlier build is harmless — the point
is that no source file names it.

- [ ] **Step 6: Commit**

```bash
git add -A modules/render_vulkan tests/unit/render/shader_tonemap_contract_test.cpp
git commit -m "$(cat <<'EOF'
refactor(render): hdr_resolve.vert becomes fullscreen_triangle.vert

Bloom's prefilter, downsample and upsample all want this shader
verbatim: no UV output, UV derived from gl_FragCoord, the
negative-height-viewport discipline in exactly one place. A shader named
for the resolve serving four pipelines would be a smell.

Its own commit so the rename reads as a rename rather than as noise
inside the backend change that needs it.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017SduWFXfqsJjE4EUC8vYat
EOF
)"
```

---

### Task 6: The bloom shaders and the composite

**Files:**
- Create: `modules/render_vulkan/shaders/bloom_prefilter.frag`
- Create: `modules/render_vulkan/shaders/bloom_blur.frag`
- Modify: `modules/render_vulkan/shaders/hdr_resolve.frag`
- Modify: `modules/render_vulkan/CMakeLists.txt:18`
- Create: `tests/unit/render/bloom_shader_contract_test.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `tests/unit/render/shader_tonemap_contract_test.cpp` (the two cases
  that pin the resolve's `outColor` expression, around lines 53-70 and 139-180,
  and the ten-shader sweep around line 120)

**Interfaces:**
- Consumes: `fullscreen_triangle.vert` (Task 5).
- Produces: the SPIR-V files `bloom_prefilter.frag.spv` and
  `bloom_blur.frag.spv`, and this push-constant block, shared byte-for-byte by
  both bloom shaders and mirrored by Task 8's C++ `BloomPush`:
  ```glsl
  layout(push_constant) uniform Push {
      vec2  inv_dest;   // 1 / the destination mip's size, for uv from gl_FragCoord
      vec2  inv_src;    // 1 / the source mip's size, for the kernel's tap offsets
      float threshold;  // prefilter only
      float knee;       // prefilter only
  } pc;
  ```
  The resolve's push block becomes
  `{ vec2 inv_dest; float exposure; float bloom_intensity; }`, and the resolve
  gains `layout(set = 1, binding = 0) uniform sampler2D bloomChain;`.

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/render/bloom_shader_contract_test.cpp`:

```cpp
// Textual contract tests for the bloom shaders.
//
// CI has no GPU, so the only way to pin a shader-side guard is to read the source and
// assert the guard is in it. These cases each name a specific way the chain goes wrong on
// a real device and nowhere else: a divide by zero, an upside-down sample, a composite on
// the wrong side of the tone curve.

#include <string>

#include <catch2/catch_test_macros.hpp>

#include "shader_source.hpp"

using mosaic::tests::code_only;
using mosaic::tests::normalize_ws;
using mosaic::tests::read_shader;

TEST_CASE("the bloom prefilter floors the knee before dividing by it", "[render][bloom]")
{
    // Review Focus #1, and the sharpest edge in this feature. The soft ramp divides by
    // 4 * knee. knee is a reflected float rendered by the Studio inspector as an unbounded
    // DragFloat, so 0 is one drag away -- and a zero there makes every bloomed pixel NaN,
    // which the chain then spreads across the whole image and the resolve adds in. NaN
    // survives the tone curve's clamp, so the result is not "too bright", it is garbage.
    const std::string frag = normalize_ws(code_only(read_shader("bloom_prefilter.frag")));
    INFO("bloom_prefilter.frag must floor pc.knee before using it as a divisor. A zero "
         "knee must degrade to a hard threshold, not to NaN.");
    REQUIRE(frag.find("max(pc.knee") != std::string::npos);
    // And nothing divides by the raw value.
    REQUIRE(frag.find("/ (4.0 * pc.knee") == std::string::npos);
    REQUIRE(frag.find("/ pc.knee") == std::string::npos);
}

TEST_CASE("the bloom prefilter floors its input colour at zero", "[render][bloom]")
{
    // The same trap tonemap_aces documents: DirectionalLight::color is a reflected Vec3
    // with no clamp anywhere, and the inspector renders it as three unbounded DragFloats.
    // A negative colour reaching the brightness max() picks the least-negative channel and
    // thresholds a dark pixel against it.
    const std::string frag = normalize_ws(code_only(read_shader("bloom_prefilter.frag")));
    INFO("bloom_prefilter.frag must floor the sampled colour at zero before computing "
         "brightness.");
    REQUIRE(frag.find("max(texture(hdrScene, uv).rgb, vec3(0.0))") != std::string::npos);
}

TEST_CASE("the bloom prefilter floors the threshold at zero", "[render][bloom]")
{
    // A negative threshold makes the contribution term (br + |t|) / br, which exceeds 1
    // and so AMPLIFIES rather than selecting -- a scene glowing brighter than it is lit,
    // from one typed minus sign.
    const std::string frag = normalize_ws(code_only(read_shader("bloom_prefilter.frag")));
    REQUIRE(frag.find("max(pc.threshold, 0.0)") != std::string::npos);
}

TEST_CASE("every bloom shader derives its uv from gl_FragCoord", "[render][bloom]")
{
    // The Y-flip trap, which hdr_resolve.frag documents at length. A fullscreen triangle
    // that derives its UV from an interpolated clip position samples upside down under
    // this backend's negative-height viewports; screen_effect.vert has exactly that bug
    // and nobody noticed, because a vignette is radially symmetric. A bloom chain built
    // upside down would composite a mirrored glow onto the scene -- visible, but as
    // "the blur looks wrong" rather than as an obvious flip.
    for (const char* name : {"bloom_prefilter.frag", "bloom_blur.frag"})
    {
        const std::string frag = normalize_ws(code_only(read_shader(name)));
        INFO(std::string(name) + " must take uv from gl_FragCoord, not from an "
             "interpolated input.");
        REQUIRE(frag.find("vec2 uv = gl_FragCoord.xy * pc.inv_dest;") != std::string::npos);
        // No interpolated input at all, so it cannot drift back to one.
        REQUIRE(frag.find("layout(location = 0) in") == std::string::npos);
    }
}

TEST_CASE("the bloom blur offsets its taps by the SOURCE texel size", "[render][bloom]")
{
    // One shader serves both the downsample and the upsample, and this is why it can:
    // the taps are offset by a source texel, so against a source twice the destination's
    // size it is a box downsample, and against one half the size it is a tent upsample.
    // Offsetting by the DESTINATION texel instead would make the upsample a point
    // resample -- a visibly blocky glow, and the single easiest thing to get backwards.
    const std::string frag = normalize_ws(code_only(read_shader("bloom_blur.frag")));
    INFO("bloom_blur.frag must offset its taps by pc.inv_src, never by pc.inv_dest.");
    REQUIRE(frag.find("vec2 t = pc.inv_src;") != std::string::npos);
    REQUIRE(frag.find("uv + vec2(-t.x, -t.y)") != std::string::npos);
    REQUIRE(frag.find("uv + vec2( t.x, t.y)") != std::string::npos);
    // The destination texel size is used ONLY for uv.
    REQUIRE(frag.find("vec2(-pc.inv_dest") == std::string::npos);
}

TEST_CASE("no bloom shader tone maps", "[render][bloom]")
{
    // hdr_resolve.frag holds the only copy of the curve in the tree, and the existing
    // tone-map contract sweeps the shader list to keep it that way. The two new ones join
    // that sweep as well as this case, so a reader of either file sees the rule.
    for (const char* name : {"bloom_prefilter.frag", "bloom_blur.frag"})
    {
        const std::string frag = code_only(read_shader(name));
        INFO(std::string(name) + " must not tone map -- the chain is linear light, and "
             "it is added INSIDE the curve by the resolve.");
        REQUIRE(frag.find("tonemap") == std::string::npos);
        REQUIRE(frag.find("2.2") == std::string::npos); // nor gamma-correct
    }
}

TEST_CASE("the resolve adds bloom inside the tone curve", "[render][bloom]")
{
    // The single most consequential line in the feature. Added AFTER the curve, bloom
    // would be untonemapped glow pasted onto a tone-mapped scene: it clips instantly and
    // reads as a white smear rather than as light. Exposure scales it too, because bloom
    // IS light -- an exposure change that moved the scene but not its glow would look
    // like a compositing error.
    const std::string frag = normalize_ws(code_only(read_shader("hdr_resolve.frag")));
    INFO("hdr_resolve.frag must composite as "
         "tonemap_aces((hdr + bloom * pc.bloom_intensity) * max(pc.exposure, 0.0)).");
    REQUIRE(frag.find("tonemap_aces((hdr + bloom * pc.bloom_intensity) * "
                      "max(pc.exposure, 0.0))") != std::string::npos);
}

TEST_CASE("the resolve samples the bloom chain from its own descriptor set",
          "[render][bloom]")
{
    // Set 0 is the HDR scene, set 1 is the chain. Both use the backend's one
    // single-combined-image-sampler layout, so the two must differ by SET and not by
    // binding -- a second binding in set 0 would need a second layout, and
    // texture_set_layout_ is shared with every texture in the engine.
    const std::string frag = normalize_ws(code_only(read_shader("hdr_resolve.frag")));
    REQUIRE(frag.find("layout(set = 0, binding = 0) uniform sampler2D hdrScene;")
            != std::string::npos);
    REQUIRE(frag.find("layout(set = 1, binding = 0) uniform sampler2D bloomChain;")
            != std::string::npos);
}

TEST_CASE("the resolve samples the chain unconditionally", "[render][bloom]")
{
    // No branch on bloom_intensity. A branch there would make the shader's behaviour
    // depend on a push constant, and the WHOLE skip-safety argument is that the multiply
    // by zero does the work -- which also means the chain must be in a readable layout on
    // every frame, including frame 1. That is what the backend's one-time clear and
    // transition is for.
    const std::string frag = normalize_ws(code_only(read_shader("hdr_resolve.frag")));
    REQUIRE(frag.find("if (pc.bloom_intensity") == std::string::npos);
    REQUIRE(frag.find("vec3 bloom = texture(bloomChain, uv).rgb;") != std::string::npos);
}
```

In `tests/CMakeLists.txt`, beside the other `unit/render/` entries, add:

```cmake
  unit/render/bloom_shader_contract_test.cpp
```

Then update the two existing cases in
`tests/unit/render/shader_tonemap_contract_test.cpp` that pin the resolve's
`outColor` expression verbatim. Both currently require the string
`outColor = vec4(tonemap_aces(hdr * max(pc.exposure, 0.0)), 1.0);`. Change both
occurrences to:

```cpp
        "outColor = vec4(tonemap_aces((hdr + bloom * pc.bloom_intensity) * "
        "max(pc.exposure, 0.0)), 1.0);"
```

Finally, in the same file, the sweep listing the shaders that must not tone map
gains the two new ones — extend that brace list with
`"bloom_prefilter.frag", "bloom_blur.frag"`.

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build build/debug -j4 2>&1 | tail -5 && \
  ctest --test-dir build/debug --output-on-failure -R "bloom" 2>&1 | tail -25
```

Expected: the new cases FAIL at `REQUIRE(in.good())` inside `read_shader`
(`bloom_prefilter.frag` does not exist), and the resolve cases FAIL on the
composite string.

- [ ] **Step 3: Write the shaders**

Create `modules/render_vulkan/shaders/bloom_prefilter.frag`:

```glsl
#version 450

// Bloom's first pass: the HDR scene target -> mip 0 of the chain, at HALF resolution,
// applying the threshold and the soft knee on the way.
//
// The prefilter IS the first downsample rather than a separate full-resolution
// bright-pass. A full-resolution pass would cost a 1920x1080 target for a result that is
// immediately halved.

layout(set = 0, binding = 0) uniform sampler2D hdrScene;

// Shared byte for byte with bloom_blur.frag and with the backend's BloomPush. The two
// vec2s come first: they have the larger alignment, so putting a float ahead of them
// would cost padding and, worse, make the layouts disagree the moment someone reorders
// one side.
layout(push_constant) uniform Push {
    vec2  inv_dest;  // 1 / this mip's size -- uv comes from gl_FragCoord, see below
    vec2  inv_src;   // 1 / the source's size. Unused here: the prefilter is a single tap.
    float threshold; // display white by default; light above this blooms
    float knee;      // the soft ramp below the threshold
} pc;

layout(location = 0) out vec4 outColor;

void main()
{
    // UV FROM gl_FragCoord, not from an interpolated clip position. Every render pass in
    // this backend sets a negative-height (Y-flipped) viewport, so a fullscreen triangle
    // that derives its UV from clip space samples upside down -- screen_effect.vert has
    // exactly that bug and nobody noticed, because a vignette is radially symmetric.
    // gl_FragCoord is framebuffer space and so correct by construction, which is also why
    // the bloom passes can use a POSITIVE-height viewport without any of this changing.
    vec2 uv = gl_FragCoord.xy * pc.inv_dest;

    // Floored for the reason tonemap_aces floors its own input: DirectionalLight::color is
    // a reflected Vec3 with no clamp anywhere, and the inspector renders it as three
    // unbounded DragFloats. A negative colour reaching the max() below picks the
    // least-negative channel and thresholds a dark pixel against it.
    vec3 c = max(texture(hdrScene, uv).rgb, vec3(0.0));

    // Brightness as the max channel, not luminance. A saturated red at (3, 0, 0) is above
    // display white and should bloom; its luminance is 0.64 and would not.
    float br = max(c.r, max(c.g, c.b));

    // A negative threshold makes the contribution term (br + |t|) / br, which exceeds 1
    // and AMPLIFIES rather than selecting -- a scene glowing brighter than it is lit, from
    // one typed minus sign.
    float th = max(pc.threshold, 0.0);

    // FLOORED, and this is the sharpest edge in the feature: kn is a divisor four lines
    // down. knee is a reflected float on an unbounded DragFloat, so 0 is one drag away,
    // and a zero divisor here makes every bloomed pixel NaN -- which the chain spreads
    // across the whole image and the resolve adds in. NaN survives the tone curve's clamp,
    // so the result is not "too bright", it is garbage. Floored rather than branched so a
    // zero knee degrades to exactly the hard threshold, which is what it means.
    float kn = max(pc.knee, 1e-4);

    // The soft knee (Karis). Below th - kn nothing passes; across the 2*kn window
    // straddling the threshold the contribution ramps quadratically; above th + kn it is
    // the hard br - th. NOT optional polish: with a hard knee a specular highlight
    // drifting across the threshold pops in and out between frames on a moving object,
    // which reads as a bug rather than as an effect.
    float soft = clamp(br - th + kn, 0.0, 2.0 * kn);
    soft = (soft * soft) / (4.0 * kn);

    // Scale the COLOUR by how much of its brightness passed, rather than subtracting the
    // threshold per channel. Subtracting per channel shifts hue -- it takes the same
    // amount from a channel at 3.0 and one at 1.1 -- and a hue shift in a glow reads as
    // chromatic aberration.
    float contrib = max(soft, br - th) / max(br, 1e-4);

    outColor = vec4(c * contrib, 1.0);
}
```

Create `modules/render_vulkan/shaders/bloom_blur.frag`:

```glsl
#version 450

// The 4-tap kernel, and it serves BOTH directions of the chain -- the downsample and the
// additive upsample are the same shader against different sources, differing only in the
// pipeline's blend state.
//
// That works because the taps are offset by a SOURCE texel. Against a source twice the
// destination's size the four taps straddle a 4x4 region: a box downsample. Against a
// source half the destination's size they straddle a region twice as wide in destination
// terms: a tent upsample, which is exactly what stops the upsampled glow looking blocky.
// Two copies of this would be two places for one of them to start offsetting by the
// destination instead.
//
// The sampler is LINEAR, so each tap is already a bilinear average of four source texels:
// a destination centre maps to a source texel CORNER, not a centre. Four taps is therefore
// a 16-texel kernel, not a 4-texel one.

layout(set = 0, binding = 0) uniform sampler2D src;

// Shared byte for byte with bloom_prefilter.frag and the backend's BloomPush. threshold
// and knee are declared and not read -- the layout is shared, and a block that differs
// between two shaders using one pipeline layout is a validation error at best.
layout(push_constant) uniform Push {
    vec2  inv_dest;
    vec2  inv_src;
    float threshold; // not read here
    float knee;      // not read here
} pc;

layout(location = 0) out vec4 outColor;

void main()
{
    // gl_FragCoord, for the negative-height-viewport reason bloom_prefilter.frag states.
    vec2 uv = gl_FragCoord.xy * pc.inv_dest;

    // SOURCE texel, never destination. Getting this backwards makes the upsample a point
    // resample and the glow visibly blocky -- the single easiest thing here to invert.
    vec2 t = pc.inv_src;

    vec3 s = texture(src, uv + vec2(-t.x, -t.y)).rgb
           + texture(src, uv + vec2( t.x, -t.y)).rgb
           + texture(src, uv + vec2(-t.x,  t.y)).rgb
           + texture(src, uv + vec2( t.x,  t.y)).rgb;

    // Alpha 1.0 and not the summed alpha: the upsample pipeline blends additively with
    // (ONE, ONE) on the colour, and the chain's alpha is never read by anything.
    outColor = vec4(s * 0.25, 1.0);
}
```

Now modify `modules/render_vulkan/shaders/hdr_resolve.frag`. Add the second
sampler immediately after the existing `hdrScene` declaration:

```glsl
// The bloom chain's mip 0, in its OWN descriptor set rather than a second binding in
// set 0. Both sets use the backend's one single-combined-image-sampler layout, which is
// shared with every texture in the engine; a second binding there would need a second
// layout for one caller.
//
// Sampled UNCONDITIONALLY, with no branch on bloom_intensity. That is what makes the skip
// case safe by arithmetic rather than by bookkeeping -- but it also means this image must
// be in a readable layout on EVERY frame, including frame 1 and any frame after a skipped
// build. The backend clears and transitions the chain once at creation for exactly that.
layout(set = 1, binding = 0) uniform sampler2D bloomChain;
```

In the push block, add the new member after `exposure`:

```glsl
    // The active camera's Bloom::intensity, already floored at 0 by RenderSystem. At 0 the
    // chain contributes exactly 0 whatever it contains, which is why a frame that skipped
    // build_bloom cannot show the previous frame's glow and no staleness flag has to be
    // right.
    float bloom_intensity;
```

And replace the body's final two statements:

```glsl
    vec3 hdr = texture(hdrScene, uv).rgb;
    // The chain is sampled with the SAME uv as the scene: it is a half-resolution image of
    // the same framing, so normalised coordinates line the two up with no extra scaling,
    // and the LINEAR sampler does the upscale.
    vec3 bloom = texture(bloomChain, uv).rgb;
    // BLOOM IS ADDED INSIDE THE CURVE, and exposure scales it too, because bloom is LIGHT.
    // Added after the curve it would be untonemapped glow pasted onto a tone-mapped scene:
    // it clips immediately and reads as a white smear rather than as something glowing.
    // Left outside the exposure multiply, an exposure change would move the scene but not
    // its glow, which looks like a compositing error.
    outColor = vec4(tonemap_aces((hdr + bloom * pc.bloom_intensity) * max(pc.exposure, 0.0)), 1.0);
```

In `modules/render_vulkan/CMakeLists.txt` line 18, add `bloom_prefilter.frag`
and `bloom_blur.frag` to the `foreach(_stage ...)` list.

- [ ] **Step 4: Verify the shaders compile to SPIR-V**

```bash
cmake --build build/debug -j4 2>&1 | grep -iE "error|bloom_" | head -20
```

Expected: `Compiling bloom_prefilter.frag -> SPIR-V` and
`Compiling bloom_blur.frag -> SPIR-V`, with no `error:` lines. `glslc` is a real
compiler, so this is the one genuine correctness check available without a GPU —
it catches a syntax error, an undeclared identifier, and a push block over the
128-byte guaranteed minimum.

- [ ] **Step 5: Run the tests to verify they pass**

```bash
ctest --test-dir build/debug --output-on-failure -R "bloom|tone map|resolve" 2>&1 | tail -20
```

Expected: PASS.

- [ ] **Step 6: Run the whole suite**

```bash
ctest --test-dir build/debug --output-on-failure 2>&1 | tail -15
```

Expected: 100% tests passed.

- [ ] **Step 7: Commit**

```bash
git add modules/render_vulkan/shaders modules/render_vulkan/CMakeLists.txt \
        tests/unit/render/bloom_shader_contract_test.cpp \
        tests/unit/render/shader_tonemap_contract_test.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(render): the bloom shaders, and the composite inside the curve

A prefilter with a Karis soft knee, and one 4-tap kernel that serves
both the downsample and the upsample -- they differ only in the
pipeline's blend state, so one shader is the honest implementation and
two copies would be two places to drift.

The resolve now adds the chain BEFORE tone mapping and scales it by
exposure, because bloom is light. Added after the curve it would be
untonemapped glow pasted onto a tone-mapped scene.

Contract tests pin the knee's floor (a zero divisor there is a NaN
chain), the tap offsets (source texel, not destination), and the
composite expression verbatim.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017SduWFXfqsJjE4EUC8vYat
EOF
)"
```

---

### Task 7: The chain's Vulkan resources

**Files:**
- Modify: `modules/render_vulkan/vulkan_render_device.cpp` — new members beside
  the `hdr_*` ones around line 5141; new methods beside `create_hdr_target` /
  `destroy_hdr_target` around lines 3003-3060; calls in the constructor (near
  lines 295-299), the destructor (near line 339) and `recreate_swapchain` (near
  line 4917)
- Create: `tests/unit/render/bloom_backend_contract_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `bloom_level_count` (Task 2).
- Produces, for Task 8:
  ```cpp
  VkRenderPass bloom_render_pass_;      // loadOp LOAD, SHADER_READ_ONLY both ends
  VkImage      bloom_image_;            // R16G16B16A16_SFLOAT, bloom_levels_ mips
  std::vector<VkImageView>   bloom_views_;   // one per mip, levelCount 1
  std::vector<VkFramebuffer> bloom_fbs_;     // one per mip
  std::vector<VkExtent2D>    bloom_extents_; // one per mip
  std::uint32_t bloom_levels_ = 0;
  void create_bloom_render_pass();
  void create_bloom_chain(uint32_t width, uint32_t height);  // the SWAPCHAIN's extent
  void destroy_bloom_chain();
  ```

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/render/bloom_backend_contract_test.cpp`:

```cpp
// Textual contract tests for the C++ half of the bloom chain.
//
// Nothing in the suite creates a VkDevice, and the validation layers check API legality
// rather than pixels -- so the gate this project trusts (ctest plus a validation-layer
// grep on both entry points) is silent on most of what follows. These cases pin the
// decisions whose failure mode is a freed handle, a wrong layout, or a leak: things that
// appear on a real device and in no test at all.

#include <string>

#include <catch2/catch_test_macros.hpp>

#include "shader_source.hpp"

using mosaic::tests::normalize_ws;
using mosaic::tests::read_backend;

namespace
{
// The body of a named method, from its signature to the next method at the same
// indentation. Lets a case assert about ONE method rather than about the whole 4900-line
// file, where a string can be present for an entirely unrelated reason.
std::string method_body(const std::string& src, const std::string& signature)
{
    const std::size_t at = src.find(signature);
    REQUIRE(at != std::string::npos);
    const std::size_t end = src.find("\n    void ", at + signature.size());
    return src.substr(at, (end == std::string::npos ? src.size() : end) - at);
}
} // namespace

TEST_CASE("the bloom chain is created and destroyed with the HDR target", "[render][bloom]")
{
    // The chain is swapchain-derived, exactly as the HDR target is, so it has the same
    // lifecycle: built at startup, rebuilt on every resize. A chain left at the old size
    // would be sampled with the new size's uv, which is a scaled glow -- and in the editor
    // that mismatch is invisible to every check this project runs.
    const std::string src = read_backend();
    const std::string rec = method_body(src, "void recreate_swapchain()");
    INFO("recreate_swapchain must destroy and recreate the bloom chain beside the HDR "
         "target.");
    REQUIRE(rec.find("destroy_bloom_chain()") != std::string::npos);
    REQUIRE(rec.find("create_bloom_chain(") != std::string::npos);
}

TEST_CASE("the bloom chain is cleared and made readable at creation", "[render][bloom]")
{
    // The layout hazard, and it is invisible to the build and to the whole headless
    // suite. hdr_resolve.frag samples the chain UNCONDITIONALLY, so on frame 1 -- and on
    // any frame following a skipped build -- it would otherwise read an image in UNDEFINED
    // layout. That is a validation error on a real device and nothing at all here.
    //
    // CLEARED as well as transitioned, which is not belt-and-braces: freshly allocated
    // device memory can read as NaN, and NaN * 0.0 is NaN, so the whole "intensity 0
    // contributes 0" argument needs the memory to actually be zero.
    const std::string body = method_body(read_backend(), "void create_bloom_chain(");
    INFO("create_bloom_chain must clear the image and leave every mip in "
         "SHADER_READ_ONLY_OPTIMAL.");
    REQUIRE(body.find("vkCmdClearColorImage") != std::string::npos);
    REQUIRE(body.find("VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL") != std::string::npos);
    // Every mip, not just mip 0 -- the upsample's destinations are mips 0..N-2 and the
    // bloom pass declares SHADER_READ_ONLY_OPTIMAL as its initialLayout for all of them.
    REQUIRE(body.find("levelCount = bloom_levels_") != std::string::npos);
}

TEST_CASE("the bloom image carries TRANSFER_DST so it can be cleared", "[render][bloom]")
{
    // The usage flag the clear above needs. Omitting it is a validation error at
    // vkCmdClearColorImage, which is to say: at startup, on a real device, and never here.
    const std::string body = method_body(read_backend(), "void create_bloom_chain(");
    REQUIRE(body.find("VK_IMAGE_USAGE_TRANSFER_DST_BIT") != std::string::npos);
    REQUIRE(body.find("VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT") != std::string::npos);
    REQUIRE(body.find("VK_IMAGE_USAGE_SAMPLED_BIT") != std::string::npos);
    REQUIRE(body.find("VK_FORMAT_R16G16B16A16_SFLOAT") != std::string::npos);
}

TEST_CASE("the bloom chain is half the swapchain's resolution", "[render][bloom]")
{
    // Half, and derived rather than passed: create_bloom_chain takes the SWAPCHAIN's
    // extent so that one place owns the halving. Two call sites each halving for
    // themselves is a place for one of them to stop.
    const std::string body = method_body(read_backend(), "void create_bloom_chain(");
    REQUIRE(body.find("bloom_level_count(width, height)") != std::string::npos);
    REQUIRE(body.find("width / 2") != std::string::npos);
    REQUIRE(body.find("height / 2") != std::string::npos);
}

TEST_CASE("the bloom render pass loads and leaves its attachment readable",
          "[render][bloom]")
{
    // LOAD because the upsample ADDS INTO the level it targets -- a CLEAR there would
    // discard the downsampled content it is adding to, and the glow would be only the
    // smallest mip. SHADER_READ_ONLY at both ends because the next step samples what this
    // one wrote, and because the resolve samples mip 0 after the whole chain is done.
    const std::string body = method_body(read_backend(), "void create_bloom_render_pass()");
    INFO("The bloom pass must use LOAD_OP_LOAD with SHADER_READ_ONLY_OPTIMAL as both "
         "initial and final layout.");
    REQUIRE(body.find("VK_ATTACHMENT_LOAD_OP_LOAD") != std::string::npos);
    REQUIRE(body.find("VK_ATTACHMENT_LOAD_OP_CLEAR") == std::string::npos);
    REQUIRE(body.find("color.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL")
            != std::string::npos);
    REQUIRE(body.find("color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL")
            != std::string::npos);
}

TEST_CASE("destroy_bloom_chain releases every per-mip handle", "[render][bloom]")
{
    // One framebuffer and one view PER MIP, because a framebuffer binds a specific view.
    // Destroying only the image would leak up to six of each on every single resize, and
    // a resize is something a user does by dragging a window edge -- so "every resize"
    // means continuously.
    const std::string body = method_body(read_backend(), "void destroy_bloom_chain()");
    REQUIRE(body.find("vkDestroyFramebuffer") != std::string::npos);
    REQUIRE(body.find("vkDestroyImageView") != std::string::npos);
    REQUIRE(body.find("vkDestroyImage(") != std::string::npos);
    REQUIRE(body.find("vkFreeMemory") != std::string::npos);
    // And the vectors are emptied, or the next create appends to them.
    REQUIRE(body.find("bloom_views_.clear()") != std::string::npos);
    REQUIRE(body.find("bloom_fbs_.clear()") != std::string::npos);
    REQUIRE(body.find("bloom_levels_ = 0") != std::string::npos);
}
```

In `tests/CMakeLists.txt`, add:

```cmake
  unit/render/bloom_backend_contract_test.cpp
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build build/debug -j4 2>&1 | tail -5 && \
  ctest --test-dir build/debug --output-on-failure -R "bloom chain|bloom render pass|bloom image|destroy_bloom" 2>&1 | tail -25
```

Expected: FAIL. The first failure is inside `method_body`'s
`REQUIRE(at != std::string::npos)` — `create_bloom_chain` does not exist.

- [ ] **Step 3: Write the implementation**

In `modules/render_vulkan/vulkan_render_device.cpp`, add the include at the top
beside the other module includes:

```cpp
#include "modules/render_vulkan/vulkan_bloom_levels.hpp"
```

Add the members immediately after the `hdr_*` block (around line 5141):

```cpp
    // --- bloom chain ---------------------------------------------------------------
    // ONE image with bloom_levels_ mips at HALF the swapchain's resolution, plus one view
    // and one framebuffer per mip -- a framebuffer binds a specific view, so the per-mip
    // vectors are not an optimisation, they are the API.
    VkRenderPass               bloom_render_pass_ = VK_NULL_HANDLE;
    VkImage                    bloom_image_ = VK_NULL_HANDLE;
    VkDeviceMemory             bloom_mem_ = VK_NULL_HANDLE;
    std::vector<VkImageView>   bloom_views_;
    std::vector<VkFramebuffer> bloom_fbs_;
    std::vector<VkExtent2D>    bloom_extents_;
    std::uint32_t              bloom_levels_ = 0;
```

Add the three methods beside `create_hdr_target`:

```cpp
    // The chain's one render pass, shared by all three pipelines.
    //
    // LOAD, not CLEAR, because the upsample ADDS INTO the level it targets: clearing
    // would discard the downsampled content it is adding to, and the result would be only
    // the smallest mip smeared over the image. SHADER_READ_ONLY_OPTIMAL at both ends
    // because the next step samples what this one wrote -- and because the resolve samples
    // mip 0 once the whole chain is done, with no barrier of its own.
    void create_bloom_render_pass()
    {
        VkAttachmentDescription color{};
        color.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference color_ref{};
        color_ref.attachment = 0;
        color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_ref;
        // NO depth attachment: every pass here is a fullscreen triangle with depth testing
        // off, and a depth buffer per mip would be six allocations for nothing.

        // The same two dependencies the HDR pass uses, and for the same reason: each step
        // in the chain samples what the previous one wrote, so the colour write must be
        // visible to the next fragment shader read.
        std::array<VkSubpassDependency, 2> deps{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass = 0;
        deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = 1;
        ci.pAttachments = &color;
        ci.subpassCount = 1;
        ci.pSubpasses = &subpass;
        ci.dependencyCount = static_cast<uint32_t>(deps.size());
        ci.pDependencies = deps.data();
        vk_check(vkCreateRenderPass(device_, &ci, nullptr, &bloom_render_pass_),
                 "vkCreateRenderPass(bloom)");
    }

    void destroy_bloom_chain()
    {
        // ONE framebuffer and ONE view per mip. Destroying only the image would leak up to
        // six of each on every resize -- and a resize is something a user does by dragging
        // a window edge, so "every resize" means continuously.
        for (VkFramebuffer fb : bloom_fbs_)
        {
            if (fb != VK_NULL_HANDLE) { vkDestroyFramebuffer(device_, fb, nullptr); }
        }
        bloom_fbs_.clear();
        for (VkImageView v : bloom_views_)
        {
            if (v != VK_NULL_HANDLE) { vkDestroyImageView(device_, v, nullptr); }
        }
        bloom_views_.clear();
        bloom_extents_.clear();
        if (bloom_image_ != VK_NULL_HANDLE)
        {
            vkDestroyImage(device_, bloom_image_, nullptr);
            bloom_image_ = VK_NULL_HANDLE;
        }
        if (bloom_mem_ != VK_NULL_HANDLE)
        {
            vkFreeMemory(device_, bloom_mem_, nullptr);
            bloom_mem_ = VK_NULL_HANDLE;
        }
        bloom_levels_ = 0;
    }

    // `width` and `height` are the SWAPCHAIN's, not the chain's: one place owns the
    // halving, so two call sites cannot disagree about whether they already halved.
    void create_bloom_chain(uint32_t width, uint32_t height)
    {
        bloom_levels_ = mosaic::render::vk::bloom_level_count(width, height);
        if (bloom_levels_ == 0)
        {
            return; // a minimised window; the same thing create_hdr_target does
        }

        uint32_t w = width / 2 > 0 ? width / 2 : 1;
        uint32_t h = height / 2 > 0 ? height / 2 : 1;

        VkImageCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.extent = {w, h, 1};
        ci.mipLevels = bloom_levels_;
        ci.arrayLayers = 1;
        ci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        // TRANSFER_DST is for the one-time clear below, and is not optional: without it
        // vkCmdClearColorImage is a validation error at startup, on a real device, and
        // never in CI.
        ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                   VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vk_check(vkCreateImage(device_, &ci, nullptr, &bloom_image_),
                 "vkCreateImage(bloom)");
        bind_image_memory(bloom_image_, bloom_mem_, "bloom");

        for (uint32_t i = 0; i < bloom_levels_; ++i)
        {
            VkImageViewCreateInfo vi{};
            vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vi.image = bloom_image_;
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = VK_FORMAT_R16G16B16A16_SFLOAT;
            vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vi.subresourceRange.baseMipLevel = i;
            // ONE level per view, deliberately. A view spanning the whole chain would let
            // the sampler pick a mip by its own lod calculation, and every tap here must
            // read the exact level the loop meant.
            vi.subresourceRange.levelCount = 1;
            vi.subresourceRange.baseArrayLayer = 0;
            vi.subresourceRange.layerCount = 1;
            VkImageView view = VK_NULL_HANDLE;
            vk_check(vkCreateImageView(device_, &vi, nullptr, &view),
                     "vkCreateImageView(bloom mip)");
            bloom_views_.push_back(view);

            VkFramebufferCreateInfo fb{};
            fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fb.renderPass = bloom_render_pass_;
            fb.attachmentCount = 1;
            fb.pAttachments = &view;
            fb.width = w;
            fb.height = h;
            fb.layers = 1;
            VkFramebuffer framebuffer = VK_NULL_HANDLE;
            vk_check(vkCreateFramebuffer(device_, &fb, nullptr, &framebuffer),
                     "vkCreateFramebuffer(bloom mip)");
            bloom_fbs_.push_back(framebuffer);
            bloom_extents_.push_back(VkExtent2D{w, h});

            w = w / 2 > 0 ? w / 2 : 1;
            h = h / 2 > 0 ? h / 2 : 1;
        }

        // THE LAYOUT HAZARD, and it is invisible to the build and to the whole headless
        // suite: hdr_resolve.frag samples this image UNCONDITIONALLY, so on frame 1 -- and
        // on any frame after a skipped build -- it would otherwise read an image in
        // UNDEFINED layout. That is a validation error on a real device and nothing here.
        //
        // CLEARED as well as transitioned, which is not belt-and-braces. Freshly allocated
        // device memory can read as NaN, and NaN * 0.0 is NaN -- so the whole "intensity 0
        // contributes 0" argument needs this memory to genuinely be zero, not merely
        // readable.
        VkCommandBuffer cmd = begin_single_time_commands();

        VkImageSubresourceRange all{};
        all.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        all.baseMipLevel = 0;
        all.levelCount = bloom_levels_;
        all.baseArrayLayer = 0;
        all.layerCount = 1;

        VkImageMemoryBarrier to_dst{};
        to_dst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        to_dst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        to_dst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_dst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_dst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_dst.image = bloom_image_;
        to_dst.subresourceRange = all;
        to_dst.srcAccessMask = 0;
        to_dst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                             1, &to_dst);

        VkClearColorValue black{};
        black.float32[0] = 0.0f; black.float32[1] = 0.0f;
        black.float32[2] = 0.0f; black.float32[3] = 1.0f;
        vkCmdClearColorImage(cmd, bloom_image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             &black, 1, &all);

        VkImageMemoryBarrier to_read = to_dst;
        to_read.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        // EVERY mip, not just mip 0: the bloom render pass declares
        // SHADER_READ_ONLY_OPTIMAL as its initialLayout, so a mip that arrives in any
        // other layout is a validation error the first time the loop targets it.
        to_read.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        to_read.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_read.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &to_read);

        end_single_time_commands(cmd);
    }
```

Wire the lifecycle. In the constructor, after `create_hdr_render_pass();` add
`create_bloom_render_pass();`, and after
`create_hdr_target(extent_.width, extent_.height);` add:

```cpp
        create_bloom_chain(extent_.width, extent_.height);
```

In the destructor, beside `destroy_hdr_target();` add:

```cpp
            destroy_bloom_chain();
            if (bloom_render_pass_ != VK_NULL_HANDLE)
            {
                vkDestroyRenderPass(device_, bloom_render_pass_, nullptr);
            }
```

In `recreate_swapchain()`, beside the existing
`destroy_hdr_target(); create_hdr_target(extent_.width, extent_.height);` pair,
add:

```cpp
        destroy_bloom_chain();
        create_bloom_chain(extent_.width, extent_.height);
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build build/debug -j4 2>&1 | tail -5 && \
  ctest --test-dir build/debug --output-on-failure -R "bloom" 2>&1 | tail -15
```

Expected: PASS.

- [ ] **Step 5: Run the whole suite**

```bash
ctest --test-dir build/debug --output-on-failure 2>&1 | tail -15
```

Expected: 100% tests passed.

- [ ] **Step 6: Commit**

```bash
git add modules/render_vulkan/vulkan_render_device.cpp \
        tests/unit/render/bloom_backend_contract_test.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(render): the bloom chain's Vulkan resources

One R16G16B16A16_SFLOAT image with N mips at half the swapchain's
resolution, one view and one framebuffer per mip, and one render pass
with loadOp LOAD and SHADER_READ_ONLY_OPTIMAL at both ends -- LOAD
because the upsample adds into the level it targets.

Cleared and transitioned once at creation. The resolve samples the chain
unconditionally, so on frame 1 it would otherwise read an image in
UNDEFINED layout; and the clear itself matters because fresh device
memory can read as NaN, and NaN * 0 is NaN.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017SduWFXfqsJjE4EUC8vYat
EOF
)"
```

---

### Task 8: The pipelines, the descriptors, and `build_bloom`

**Files:**
- Modify: `modules/render_vulkan/vulkan_render_device.cpp` — new members; a
  `create_bloom_pipelines()` beside `create_resolve_pipeline()`; a
  `write_bloom_descriptors()` beside `write_hdr_descriptor()`; `build_bloom` and
  `record_bloom_pass` beside `end_hdr_scene_pass`; `draw_hdr_resolve`'s
  signature, push struct and second descriptor set; `create_resolve_pipeline`'s
  layout
- Modify: `tests/unit/render/bloom_backend_contract_test.cpp` (append cases)

**Interfaces:**
- Consumes: `bloom_render_pass_`, `bloom_image_`, `bloom_views_`, `bloom_fbs_`,
  `bloom_extents_`, `bloom_levels_`, `create_bloom_chain`, `destroy_bloom_chain`
  (Task 7); `fullscreen_triangle.vert.spv` (Task 5);
  `bloom_prefilter.frag.spv`, `bloom_blur.frag.spv` (Task 6); `BloomParams`,
  `build_bloom`, `draw_hdr_resolve(float, float)` (Task 3).
- Produces: the finished feature.

**Three traps this task must not rediscover**, from the spec:

1. **The descriptors must be rewritten on resize.** `recreate_swapchain` rebuilds
   the chain, which invalidates every view the sets point at. This is exactly the
   defect #256 shipped and its review caught — a freed image view still bound —
   and `write_hdr_descriptor`, called from `recreate_swapchain`, is the pattern
   to copy. Here there are `N + 1` chances to forget rather than one.
2. **`N` can be 1.** Prefilter only; both `N - 1` loops run zero times.
   `bloom_levels_` is unsigned, so `for (i = bloom_levels_ - 2; ...)` wraps to
   4294967294 and indexes past every vector in the class.
3. **The chain may not exist at all** (`bloom_levels_ == 0`, a minimised window).
   `draw_hdr_resolve` still has to bind *something* to set 1, or it is a
   validation error and on some drivers a crash.

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/render/bloom_backend_contract_test.cpp`:

```cpp
TEST_CASE("the bloom descriptors are rewritten on resize", "[render][bloom]")
{
    // TRAP 1, and it is exactly the defect PR #256 shipped: recreate_swapchain rebuilt
    // the HDR target and left the resolve's descriptor pointing at the freed view. The fix
    // there -- write_hdr_descriptor, called from recreate_swapchain -- is the pattern, and
    // bloom has N+1 sets rather than one, so there are N+1 chances to forget.
    const std::string rec = method_body(read_backend(), "void recreate_swapchain()");
    INFO("recreate_swapchain must call write_bloom_descriptors() after rebuilding the "
         "chain -- every set points at a view destroy_bloom_chain just freed.");
    REQUIRE(rec.find("write_bloom_descriptors()") != std::string::npos);
    // And after, not before: the views it writes do not exist until create_bloom_chain ran.
    REQUIRE(rec.find("create_bloom_chain(") < rec.find("write_bloom_descriptors()"));
}

TEST_CASE("there is one bloom descriptor set per distinct source", "[render][bloom]")
{
    // N + 1 sets: the HDR view, plus EVERY mip 0..N-1. Not N -- the downsample reads mips
    // 0..N-2 and the upsample reads mips 1..N-1, so between them every level is a source
    // at some point, including the smallest, which nothing downsamples FROM.
    //
    // Allocated for the MAXIMUM chain, so a resize that grows the level count never has to
    // reallocate the pool.
    const std::string body = method_body(read_backend(), "void create_bloom_pipelines()");
    INFO("The bloom descriptor pool must be sized for kBloomMaxLevels + 1 sets.");
    REQUIRE(body.find("kBloomMaxLevels + 1") != std::string::npos);
    // Its OWN pool, not descriptor_pool_, whose maxSets is budgeted exactly for textures,
    // materials and IBL -- borrowing there would starve the last texture at some scene
    // size rather than failing here.
    REQUIRE(body.find("bloom_descriptor_pool_") != std::string::npos);
}

TEST_CASE("the bloom upsample pipeline blends additively", "[render][bloom]")
{
    // The one difference between the downsample and the upsample, since they share a
    // shader. Without (ONE, ONE) the upsample REPLACES the level it targets instead of
    // adding to it, and the finished glow is only the smallest mip scaled up -- a soft,
    // uniform wash with none of the chain's detail. It looks deliberate, which is what
    // makes it hard to spot.
    const std::string body = method_body(read_backend(), "void create_bloom_pipelines()");
    REQUIRE(body.find("VK_BLEND_FACTOR_ONE") != std::string::npos);
    REQUIRE(body.find("bloom_up_pipeline_") != std::string::npos);
    REQUIRE(body.find("bloom_down_pipeline_") != std::string::npos);
    REQUIRE(body.find("bloom_prefilter_pipeline_") != std::string::npos);
}

TEST_CASE("build_bloom runs outside every render pass", "[render][bloom]")
{
    // It opens bloom_levels_ render passes of its own, and vkCmdBeginRenderPass inside a
    // pass is a validation error. RenderSystem calls it between end_hdr_scene_pass and
    // draw_hdr_resolve, a window where nothing is recording -- but the viewport pass is
    // LAZILY opened, so "nothing is recording" is a property of today's call order rather
    // than of the API, and cheap to guarantee here.
    const std::string body = method_body(read_backend(), "void build_bloom(");
    INFO("build_bloom must bail out if any pass is already recording.");
    REQUIRE(body.find("in_hdr_pass_") != std::string::npos);
    REQUIRE(body.find("swapchain_pass_open_") != std::string::npos);
    REQUIRE(body.find("viewport_pass_open_") != std::string::npos);
    REQUIRE(body.find("frame_skipped_") != std::string::npos);
}

TEST_CASE("build_bloom's loops are written so a one-level chain is a no-op",
          "[render][bloom]")
{
    // TRAP 2. At N = 1 the chain is prefilter only and both loops run N-1 = 0 times.
    // bloom_levels_ is unsigned, so `for (i = bloom_levels_ - 2; i >= 0; --i)` would not
    // terminate -- it would wrap to 4294967294 and index past every vector in the class.
    // Written with `i + 1 < bloom_levels_` ascending and `i-- > 0` descending, both of
    // which are correct at 1 without a special case.
    const std::string body = normalize_ws(method_body(read_backend(), "void build_bloom("));
    INFO("Neither loop may compute `bloom_levels_ - 2` as an unsigned loop bound.");
    REQUIRE(body.find("bloom_levels_ - 2") == std::string::npos);
    REQUIRE(body.find("i + 1 < bloom_levels_") != std::string::npos);
    REQUIRE(body.find("i-- > 0") != std::string::npos);
}

TEST_CASE("the resolve binds a set to slot 1 even with no bloom chain", "[render][bloom]")
{
    // TRAP 3. A minimised window gives bloom_levels_ == 0 and no chain at all, but
    // hdr_resolve.frag still declares set 1 and samples it -- an unbound set is a
    // validation error, and on some drivers a crash. The HDR set is bound there as a
    // stand-in; the intensity is 0 on that path anyway, so what it reads is multiplied
    // away.
    const std::string body = method_body(read_backend(), "void draw_hdr_resolve(");
    INFO("draw_hdr_resolve must bind two descriptor sets, falling back to hdr_set_ for "
         "slot 1 when the chain does not exist.");
    REQUIRE(body.find("bloom_sets_.empty()") != std::string::npos);
    REQUIRE(body.find("0, 2, sets") != std::string::npos);
}

TEST_CASE("the resolve's push block matches hdr_resolve.frag", "[render][bloom]")
{
    // The two layouts are shared byte for byte, and a mismatch is silent: the shader reads
    // whatever bytes land at its offsets, so a wrong order shows up as an exposure that
    // behaves like an intensity. vec2 first because it has the larger alignment.
    const std::string src = read_backend();
    const std::size_t at = src.find("struct ResolvePush");
    REQUIRE(at != std::string::npos);
    const std::string decl = normalize_ws(src.substr(at, 220));
    REQUIRE(decl.find("float inv_dest[2]; float exposure; float bloom_intensity;")
            != std::string::npos);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build build/debug -j4 2>&1 | tail -5 && \
  ctest --test-dir build/debug --output-on-failure -R "bloom" 2>&1 | tail -25
```

Expected: FAIL inside `method_body` — `create_bloom_pipelines` and `build_bloom`
do not exist.

- [ ] **Step 3: Write the implementation**

Add the members beside the Task 7 bloom block:

```cpp
    VkDescriptorPool             bloom_descriptor_pool_ = VK_NULL_HANDLE;
    // bloom_sets_[0] is the HDR scene view; bloom_sets_[i + 1] is mip i. One per distinct
    // SOURCE: the downsample reads mips 0..N-2 and the upsample reads 1..N-1, so between
    // them every level is sampled -- including the smallest, which nothing downsamples
    // from.
    std::vector<VkDescriptorSet> bloom_sets_;
    VkPipelineLayout             bloom_pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline                   bloom_prefilter_pipeline_ = VK_NULL_HANDLE;
    VkPipeline                   bloom_down_pipeline_ = VK_NULL_HANDLE;
    VkPipeline                   bloom_up_pipeline_ = VK_NULL_HANDLE;
```

Extend `ResolvePush` and add `BloomPush` beside it:

```cpp
    struct ResolvePush
    {
        float inv_dest[2];
        float exposure;
        float bloom_intensity;
    };

    // Mirrors bloom_prefilter.frag and bloom_blur.frag byte for byte -- ONE block for all
    // three pipelines, so they share one pipeline layout. The two vec2s come first for the
    // alignment reason ResolvePush gives; threshold and knee are read only by the
    // prefilter, and declared by the blur so the layouts cannot disagree.
    struct BloomPush
    {
        float inv_dest[2];
        float inv_src[2];
        float threshold;
        float knee;
    };
```

Add `create_bloom_pipelines()` beside `create_resolve_pipeline()`:

```cpp
    void create_bloom_pipelines()
    {
        // ITS OWN POOL, sized for the MAXIMUM chain plus the HDR view, so a resize that
        // grows the level count never has to reallocate it. Not descriptor_pool_, whose
        // maxSets is budgeted exactly for textures, materials and IBL -- borrowing a set
        // there would starve the last texture at some scene size rather than fail here.
        VkDescriptorPoolSize pool_size{};
        pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_size.descriptorCount = mosaic::render::vk::kBloomMaxLevels + 1;
        VkDescriptorPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &pool_size;
        pool.maxSets = mosaic::render::vk::kBloomMaxLevels + 1;
        vk_check(vkCreateDescriptorPool(device_, &pool, nullptr, &bloom_descriptor_pool_),
                 "vkCreateDescriptorPool(bloom)");

        std::vector<VkDescriptorSetLayout> layouts(
            mosaic::render::vk::kBloomMaxLevels + 1, texture_set_layout_);
        bloom_sets_.resize(layouts.size());
        VkDescriptorSetAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc.descriptorPool = bloom_descriptor_pool_;
        alloc.descriptorSetCount = static_cast<uint32_t>(layouts.size());
        alloc.pSetLayouts = layouts.data();
        vk_check(vkAllocateDescriptorSets(device_, &alloc, bloom_sets_.data()),
                 "vkAllocateDescriptorSets(bloom)");
        write_bloom_descriptors();

        std::vector<char> vert_code =
            read_file(std::string(MOSAIC_SHADER_DIR) + "/fullscreen_triangle.vert.spv");
        std::vector<char> pre_code =
            read_file(std::string(MOSAIC_SHADER_DIR) + "/bloom_prefilter.frag.spv");
        std::vector<char> blur_code =
            read_file(std::string(MOSAIC_SHADER_DIR) + "/bloom_blur.frag.spv");
        VkShaderModule vert = create_shader_module(vert_code);
        VkShaderModule pre  = create_shader_module(pre_code);
        VkShaderModule blur = create_shader_module(blur_code);

        VkPipelineShaderStageCreateInfo vs{};
        vs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vs.stage = VK_SHADER_STAGE_VERTEX_BIT; vs.module = vert; vs.pName = "main";
        VkPipelineShaderStageCreateInfo fs{};
        fs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fs.stage = VK_SHADER_STAGE_FRAGMENT_BIT; fs.pName = "main";

        VkPipelineVertexInputStateCreateInfo vin{};
        vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1; vp.scissorCount = 1;

        VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2; dyn.pDynamicStates = dyn_states;

        VkPipelineRasterizationStateCreateInfo rast{};
        rast.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rast.polygonMode = VK_POLYGON_MODE_FILL; rast.lineWidth = 1.0f;
        rast.cullMode = VK_CULL_MODE_NONE;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable = VK_FALSE;
        depth.depthWriteEnable = VK_FALSE;
        depth.depthCompareOp = VK_COMPARE_OP_ALWAYS;

        VkPipelineColorBlendAttachmentState opaque{};
        opaque.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        opaque.blendEnable = VK_FALSE;
        VkPipelineColorBlendStateCreateInfo cb_opaque{};
        cb_opaque.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb_opaque.attachmentCount = 1; cb_opaque.pAttachments = &opaque;

        // THE ONE DIFFERENCE between the downsample and the upsample, which share a
        // shader. Without (ONE, ONE) the upsample REPLACES the level it targets instead of
        // adding to it, and the finished glow is only the smallest mip scaled up -- a
        // soft, uniform wash with none of the chain's detail, which looks deliberate.
        VkPipelineColorBlendAttachmentState additive = opaque;
        additive.blendEnable = VK_TRUE;
        additive.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        additive.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        additive.colorBlendOp = VK_BLEND_OP_ADD;
        // Alpha is never read from this image; kept at (ONE, ZERO) so it stays 1.0 rather
        // than accumulating to a number nothing means.
        additive.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        additive.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        additive.alphaBlendOp = VK_BLEND_OP_ADD;
        VkPipelineColorBlendStateCreateInfo cb_additive{};
        cb_additive.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb_additive.attachmentCount = 1; cb_additive.pAttachments = &additive;

        VkPushConstantRange push{};
        push.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        push.offset = 0; push.size = sizeof(BloomPush);

        VkPipelineLayoutCreateInfo layout{};
        layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout.setLayoutCount = 1; layout.pSetLayouts = &texture_set_layout_;
        layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &push;
        vk_check(vkCreatePipelineLayout(device_, &layout, nullptr, &bloom_pipeline_layout_),
                 "vkCreatePipelineLayout(bloom)");

        VkPipelineShaderStageCreateInfo stages[2] = {vs, fs};
        VkGraphicsPipelineCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pi.stageCount = 2; pi.pStages = stages;
        pi.pVertexInputState = &vin; pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp; pi.pRasterizationState = &rast;
        pi.pMultisampleState = &ms; pi.pDepthStencilState = &depth;
        pi.pDynamicState = &dyn;
        pi.layout = bloom_pipeline_layout_;
        // ONE render pass for all three, which is what lets a single loop drive the chain.
        pi.renderPass = bloom_render_pass_; pi.subpass = 0;

        stages[1].module = pre;
        pi.pColorBlendState = &cb_opaque;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr,
                                           &bloom_prefilter_pipeline_),
                 "vkCreateGraphicsPipelines(bloom prefilter)");

        stages[1].module = blur;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr,
                                           &bloom_down_pipeline_),
                 "vkCreateGraphicsPipelines(bloom down)");

        pi.pColorBlendState = &cb_additive;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr,
                                           &bloom_up_pipeline_),
                 "vkCreateGraphicsPipelines(bloom up)");

        vkDestroyShaderModule(device_, blur, nullptr);
        vkDestroyShaderModule(device_, pre, nullptr);
        vkDestroyShaderModule(device_, vert, nullptr);
    }

    // Points every bloom set at the view it samples. Called at creation and again after
    // every recreate_swapchain -- which rebuilds the chain, freeing every view these sets
    // were written with. That is EXACTLY the defect #256 shipped (a freed image view still
    // bound, found by a reading review after a run with zero validation errors), and here
    // there are N+1 chances to forget rather than one.
    void write_bloom_descriptors()
    {
        if (bloom_sets_.empty()) { return; }

        std::vector<VkDescriptorImageInfo> infos;
        std::vector<VkWriteDescriptorSet>  writes;
        infos.reserve(bloom_sets_.size());
        writes.reserve(bloom_sets_.size());

        // Set 0 is the HDR scene, which the prefilter samples.
        if (hdr_color_view_ != VK_NULL_HANDLE)
        {
            VkDescriptorImageInfo info{};
            info.sampler = hdr_sampler_;
            info.imageView = hdr_color_view_;
            info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            infos.push_back(info);
        }
        // Then one per mip, 0..N-1.
        for (uint32_t i = 0; i < bloom_levels_; ++i)
        {
            VkDescriptorImageInfo info{};
            info.sampler = hdr_sampler_; // LINEAR, CLAMP_TO_EDGE -- the chain wants both
            info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            info.imageView = bloom_views_[i];
            infos.push_back(info);
        }

        // pImageInfo is a POINTER, so `infos` must be fully grown before any write records
        // one -- a push_back that reallocates would leave earlier writes dangling. Hence
        // the two loops and the reserve above.
        for (std::size_t i = 0; i < infos.size(); ++i)
        {
            VkWriteDescriptorSet w{};
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = bloom_sets_[i];
            w.dstBinding = 0;
            w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w.pImageInfo = &infos[i];
            writes.push_back(w);
        }
        if (writes.empty()) { return; }
        vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
    }
```

Add `build_bloom` and `record_bloom_pass` beside `end_hdr_scene_pass`:

```cpp
    void build_bloom(const mosaic::engine::render::BloomParams& params) override
    {
        // Opens bloom_levels_ render passes of its own, and vkCmdBeginRenderPass inside a
        // pass is a validation error. RenderSystem calls this between end_hdr_scene_pass
        // and draw_hdr_resolve, a window where nothing is recording -- but the viewport
        // pass is LAZILY opened, so that is a property of today's call order rather than
        // of the API, and cheap to guarantee here.
        if (frame_skipped_ || in_hdr_pass_ || swapchain_pass_open_ || viewport_pass_open_)
        {
            return;
        }
        if (bloom_levels_ == 0 || bloom_prefilter_pipeline_ == VK_NULL_HANDLE)
        {
            return;
        }

        // Prefilter: the HDR scene -> mip 0, with the threshold and knee applied on the
        // way. It IS the first downsample; a full-resolution bright-pass would cost a
        // 1920x1080 target for a result that is immediately halved.
        record_bloom_pass(bloom_prefilter_pipeline_, /*dest=*/0, /*source_set=*/0,
                          /*src_extent=*/hdr_extent_, params);

        // Downsample: mip i -> mip i+1. Written `i + 1 < bloom_levels_` rather than
        // `i < bloom_levels_ - 1` because bloom_levels_ is UNSIGNED and can be 1, where
        // the subtraction wraps.
        for (uint32_t i = 0; i + 1 < bloom_levels_; ++i)
        {
            record_bloom_pass(bloom_down_pipeline_, /*dest=*/i + 1, /*source_set=*/i + 1,
                              bloom_extents_[i], params);
        }

        // Upsample, smallest first: mip i+1 -> mip i, ADDING into what the downsample left
        // there. Descending with `i-- > 0` so the unsigned counter never has to hold -1.
        for (uint32_t i = bloom_levels_ - 1; i-- > 0;)
        {
            record_bloom_pass(bloom_up_pipeline_, /*dest=*/i, /*source_set=*/i + 2,
                              bloom_extents_[i + 1], params);
        }
        // Mip 0 is now the finished glow, and the resolve's set 1 points at it.
    }

    // One step of the chain: open the pass on `dest`'s framebuffer, bind the pipeline and
    // the source set, push the two reciprocal extents, draw the triangle, close.
    //
    // `source_set` indexes bloom_sets_, where 0 is the HDR view and i+1 is mip i.
    void record_bloom_pass(VkPipeline pipe, uint32_t dest, uint32_t source_set,
                           VkExtent2D src_extent,
                           const mosaic::engine::render::BloomParams& params)
    {
        const VkExtent2D dst_extent = bloom_extents_[dest];

        // POSITIVE height, unlike every other pass in this backend. Nothing here uses a
        // projection matrix, and every bloom shader takes its uv from gl_FragCoord --
        // which is framebuffer space and unaffected by the viewport's sign. The Y-flip
        // would therefore change nothing, and a negative height that changes nothing is a
        // line the next reader has to prove harmless.
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(dst_extent.width);
        viewport.height = static_cast<float>(dst_extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd_[frame_], 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = dst_extent;
        vkCmdSetScissor(cmd_[frame_], 0, 1, &scissor);

        VkRenderPassBeginInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = bloom_render_pass_;
        rp.framebuffer = bloom_fbs_[dest];
        rp.renderArea.offset = {0, 0};
        rp.renderArea.extent = dst_extent;
        // No clear values: the pass is LOAD_OP_LOAD, because the upsample adds into what
        // is already there.
        vkCmdBeginRenderPass(cmd_[frame_], &rp, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        vkCmdBindDescriptorSets(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                bloom_pipeline_layout_, 0, 1, &bloom_sets_[source_set],
                                0, nullptr);

        BloomPush pc{};
        // inv_dest turns gl_FragCoord into uv; inv_src sizes the kernel's tap offsets.
        // Reciprocals rather than the extents, because both are used per fragment and the
        // division is not.
        pc.inv_dest[0] = 1.0f / static_cast<float>(dst_extent.width);
        pc.inv_dest[1] = 1.0f / static_cast<float>(dst_extent.height);
        pc.inv_src[0] = 1.0f / static_cast<float>(src_extent.width);
        pc.inv_src[1] = 1.0f / static_cast<float>(src_extent.height);
        pc.threshold = params.threshold;
        pc.knee = params.knee;
        vkCmdPushConstants(cmd_[frame_], bloom_pipeline_layout_,
                           VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

        vkCmdDraw(cmd_[frame_], 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd_[frame_]);
    }
```

Change `draw_hdr_resolve`'s signature:

```cpp
    void draw_hdr_resolve(float exposure, float bloom_intensity) override
```

Inside it, replace the single `vkCmdBindDescriptorSets` call with:

```cpp
        // TWO sets: the scene at 0, the bloom chain's mip 0 at 1. hdr_resolve.frag samples
        // both unconditionally, so slot 1 must be bound even when there is no chain -- a
        // minimised window gives bloom_levels_ == 0, and an unbound set is a validation
        // error and on some drivers a crash. The HDR set stands in there; the intensity is
        // 0 on that path anyway, so what it reads is multiplied away.
        const VkDescriptorSet bloom_src =
            (bloom_sets_.empty() || bloom_levels_ == 0) ? hdr_set_ : bloom_sets_[1];
        const VkDescriptorSet sets[2] = {hdr_set_, bloom_src};
        vkCmdBindDescriptorSets(cmd_[frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                resolve_pipeline_layout_, 0, 2, sets, 0, nullptr);
```

and set the new push member beside `pc.exposure`:

```cpp
        pc.bloom_intensity = bloom_intensity;
```

In `create_resolve_pipeline`, the layout must now declare two sets. Replace the
`layout.setLayoutCount = 1; layout.pSetLayouts = &texture_set_layout_;` line
with:

```cpp
        // TWO sets, both the one-combined-image-sampler layout: the scene at 0, the bloom
        // chain at 1. A second BINDING in set 0 would have needed a second set layout, and
        // texture_set_layout_ is shared with every texture in the engine.
        const VkDescriptorSetLayout resolve_layouts[2] = {texture_set_layout_,
                                                          texture_set_layout_};
        layout.setLayoutCount = 2; layout.pSetLayouts = resolve_layouts;
```

Wire the lifecycle. In the constructor, after `create_resolve_pipeline();` add
`create_bloom_pipelines();`. In the destructor, beside the resolve's teardown:

```cpp
            if (bloom_prefilter_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, bloom_prefilter_pipeline_, nullptr); }
            if (bloom_down_pipeline_ != VK_NULL_HANDLE)      { vkDestroyPipeline(device_, bloom_down_pipeline_, nullptr); }
            if (bloom_up_pipeline_ != VK_NULL_HANDLE)        { vkDestroyPipeline(device_, bloom_up_pipeline_, nullptr); }
            if (bloom_pipeline_layout_ != VK_NULL_HANDLE)    { vkDestroyPipelineLayout(device_, bloom_pipeline_layout_, nullptr); }
            // Frees every bloom set with it -- the pool owns them.
            if (bloom_descriptor_pool_ != VK_NULL_HANDLE)    { vkDestroyDescriptorPool(device_, bloom_descriptor_pool_, nullptr); }
```

In `recreate_swapchain()`, immediately after the `create_bloom_chain(...)` call
added in Task 7, add:

```cpp
        write_bloom_descriptors();
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build build/debug -j4 2>&1 | tail -5 && \
  ctest --test-dir build/debug --output-on-failure -R "bloom" 2>&1 | tail -20
```

Expected: PASS.

- [ ] **Step 5: Run the whole suite**

```bash
ctest --test-dir build/debug --output-on-failure 2>&1 | tail -15
```

Expected: 100% tests passed.

- [ ] **Step 6: Run both entry points under the validation layers**

The suite cannot see any of this. This is the only check in the plan that runs
real Vulkan.

```bash
MOSAIC_HEADLESS=0 timeout 20 ./build/debug/app/mosaic run --window 2>&1 \
  | tee /tmp/bloom-window.log | tail -5
grep -ciE "validation|VUID|error" /tmp/bloom-window.log
```

Expected: exits without a crash, and a grep count of **0**.

> The sandbox cannot screenshot a GUI window, so a clean run means "the API was
> used legally", NOT "it looks like bloom". That distinction is the whole reason
> this feature ends at a human verification pass.

Repeat for the editor, which is the path the standard gate is blind to:

```bash
MOSAIC_HEADLESS=0 timeout 20 ./build/debug/app/mosaic edit 2>&1 \
  | tee /tmp/bloom-editor.log | tail -5
grep -ciE "validation|VUID|error" /tmp/bloom-editor.log
```

Expected: 0. If either run reports a VUID, read it — a layout or descriptor VUID
here names exactly which of the three traps was not avoided.

- [ ] **Step 7: Commit**

```bash
git add modules/render_vulkan/vulkan_render_device.cpp \
        tests/unit/render/bloom_backend_contract_test.cpp
git commit -m "$(cat <<'EOF'
feat(render): build the bloom chain

Three pipelines against one render pass, driven by one loop: prefilter
into mip 0, downsample to the smallest, then upsample back with additive
blending. N+1 descriptor sets -- the HDR view plus every mip, because
between the two directions every level is a source.

The three traps the spec named are handled explicitly: the descriptors
are rewritten from recreate_swapchain (the defect #256 shipped), both
loops are written so an unsigned level count of 1 is a clean no-op, and
the resolve binds a stand-in to set 1 when there is no chain at all.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017SduWFXfqsJjE4EUC8vYat
EOF
)"
```

---

### Task 9: Author it in the demo, and record what only a human can check

**Files:**
- Modify: `app/run/demo_scene.hpp` (the camera entity, around line 674)
- Modify: `tests/unit/app/demo_scene_test.cpp` (append one case)
- Modify: `docs/project/next-steps.md`
- Modify: `docs/project/deferred-work-backlog.md`

**Interfaces:**
- Consumes: `mosaic::engine::vfx::Bloom` (Task 1), and the whole feature.
- Produces: nothing other tasks read.

**Why the demo gets a `Bloom`.** The default is off, so without this the feature
ships unreachable: `mosaic run --window` would render byte-identically and the
verification pass would have nothing to look at. The demo's particle and decal
colours were retuned on 2026-10-17 to live above 1.0, which makes them the first
things in the project a threshold bloom can find.

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/app/demo_scene_test.cpp` — the existing home of every
assertion about the demo — adding `#include "engine/vfx/bloom.hpp"` at the top:

```cpp
TEST_CASE("the demo scene authors a visible bloom on its camera", "[vfx][bloom]")
{
    // Bloom::intensity defaults to 0, so a demo without this renders byte-identically to
    // one built before the feature existed -- the feature would ship unreachable, and the
    // human verification pass would have nothing to look at.
    //
    // On the CAMERA, because that is where RenderSystem reads it; a Bloom anywhere else in
    // the scene is deliberately ignored.
    using mosaic::engine::vfx::Bloom;
    mosaic::engine::World world;
    build_default_scene(world);

    int   with_bloom = 0;
    float intensity  = 0.0f;
    world.for_each<mosaic::engine::render::Camera>(
        [&](mosaic::foundation::EntityID e, mosaic::engine::render::Camera&)
        {
            if (const auto* b = world.get_component<Bloom>(e))
            {
                ++with_bloom;
                intensity = b->intensity;
            }
        });
    REQUIRE(with_bloom == 1);
    REQUIRE(intensity > 0.0f);
}
```

> `build_default_scene` takes the world alone — no `AssetRegistry` — and the
> file already has `using mosaic::app::build_default_scene;` at the top. If the
> `Camera` include is missing, add `#include "engine/render/camera.hpp"`.

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build/debug -j4 2>&1 | tail -5 && \
  ctest --test-dir build/debug --output-on-failure -R "demo scene authors" 2>&1 | tail -10
```

Expected: FAIL on `REQUIRE(with_bloom == 1)` with `with_bloom == 0`.

- [ ] **Step 3: Author it**

In `app/run/demo_scene.hpp`, add `#include "engine/vfx/bloom.hpp"` beside the
other vfx includes, and immediately after the `ScreenEffect` line on the camera
entity add:

```cpp
    // Threshold at display white, so the demo blooms exactly what exceeds what the screen
    // can show -- which, since the 2026-10-17 retune, is the fountain's orange, the
    // trail's green, the puffer's cyan and the decal's warm tint. They are the first
    // authored values in this project above 1.0, and so the first things to glow.
    //
    // Intensity well below 1: the chain is ADDED to the scene before the tone curve, so
    // 1.0 would roughly double the brightness of every bright thing. 0.35 reads as a glow
    // rather than as a second exposure.
    //
    // THE SKY IS THE RISK HERE. The demo lights itself with a real outdoor HDR environment
    // whose radiance genuinely exceeds 1.0, so the sky and horizon will glow too.
    // Physically that is correct; whether it veils the image is a question only a human at
    // the window can answer, and if it does the fix is THIS number, not the pipeline.
    world.add_component<mosaic::engine::vfx::Bloom>(camera,
        mosaic::engine::vfx::Bloom{0.35f, 1.0f, 0.5f});
```

> `Bloom` is brace-initialised positionally here, like every other component in
> this file. Adding a field to `Bloom` later re-binds these three values
> silently, and the compiler only catches it when the types differ — all three
> are `float`, so it would not. If `Bloom` ever grows a member, re-read this
> call site.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build build/debug -j4 2>&1 | tail -5 && \
  ctest --test-dir build/debug --output-on-failure 2>&1 | tail -15
```

Expected: 100% tests passed. Adding a component to an existing entity does not
change the entity count, so the four `build_default_scene` count tests are
unaffected — but if one of them fails, read it: it may assert a component count
rather than an entity count.

- [ ] **Step 5: Run both entry points once more**

```bash
MOSAIC_HEADLESS=0 timeout 20 ./build/debug/app/mosaic run --window 2>&1 \
  | grep -ciE "validation|VUID|error"
MOSAIC_HEADLESS=0 timeout 20 ./build/debug/app/mosaic edit 2>&1 \
  | grep -ciE "validation|VUID|error"
```

Expected: 0 and 0. This is the first run where the chain is actually built —
every earlier run had intensity 0 and skipped `build_bloom` entirely, so the
eleven render passes, the per-mip framebuffers and the additive blend have never
executed before this step.

- [ ] **Step 6: Update the project docs**

In `docs/project/next-steps.md`, replace the "next rendering slice" entry with
bloom's shipped state and the three verification items from the closing section
of this plan.

In `docs/project/deferred-work-backlog.md`, mark the **Bloom** entry in the
Rendering section as shipped, in the existing
`~~struck~~ — **SHIPPED <date>.**` style the HDR entries use, keeping its
original text after `Original entry:`. The **six** deferral entries written
alongside the spec and this plan stay open — the five from the spec's Non-goals,
plus *"Every swapchain-derived target is rebuilt on each resize, including
mid-drag"*, which this plan surfaced and which bloom makes six times worse
without introducing.

- [ ] **Step 7: Commit**

```bash
git add -A app docs tests
git commit -m "$(cat <<'EOF'
feat(demo): author bloom on the demo camera

Intensity 0.35 at display white. Without this the feature ships
unreachable -- the default is off, so the demo would render
byte-identically and the verification pass would have nothing to look
at.

The retuned particle and decal colours are the first authored values in
this project above 1.0, and so the first things to glow. The sky is the
open risk: it genuinely exceeds 1.0, so it will bloom too, and whether
that veils the image is for a human at the window.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017SduWFXfqsJjE4EUC8vYat
EOF
)"
```

> If the pre-commit hook aborts to regenerate `docs/index.html`, re-run the
> **full** `git add -A docs/ && git commit` above. Never `--amend`: it silently
> rewrites the previous commit, and `--no-verify --amend` hides that it did.

---

## What this plan cannot verify

Every test above checks **ordering, wiring and the presence of a guard**.
Whether it looks like bloom needs a human at the window, and the spec names the
risk in advance: at a threshold of 1.0 the demo's real outdoor HDR sky will glow,
which is physically correct and may still veil the image. If it does, the fix is
`app/run/demo_scene.hpp`'s authored threshold — a knob the scene owns — and not
the pipeline.

Three items for the next human verification pass, each with its pass condition
anchored on an authored constant rather than on an impression:

1. **The fountain, the trail and the puffer glow; the ground does not.** Their
   colours are authored at 2.85, 1.75 and 2.85 in their brightest channel, all
   above the demo's threshold of 1.0; the ground is lit, never authored above 1,
   and so must stay crisp. A glow on the ground means the threshold is reaching
   the shader as something other than 1.0.
2. **The HUD bar is unaffected.** It is drawn after `draw_hdr_resolve`, on the
   resolved image, so no amount of bloom can touch it. A soft HUD means the UI
   draws moved inside the HDR pass.
3. **Drag the editor's Viewport panel edge while the demo runs.** The glow must
   stay locked to the objects making it, at the same apparent size relative to
   them. The chain is sized from the **swapchain**, not the panel, and the
   resolve samples it with the same normalised uv as the scene — so a glow that
   drifts or rescales as the panel changes means the chain and the scene
   disagree about their framing, which is the same class of bug the HDR resolve
   shipped in #256. This item exists because `ctest` plus a validation-layer grep
   is structurally blind to anything that differs between `vp_extent_` and
   `extent_`.

**Note for item 1:** the editor opens **stopped**, so no tick runs and the
particles never spawn. Run `mosaic run --window`, or press Play in the editor
first.

---

## Corrections, written after execution (2026-10-17)

The plan above is left as it was written, because a plan that is quietly edited
to match what happened stops being a record of what was predicted. These are the
four places it was **wrong**, so that nobody follows them:

1. **Task 1's test code used an API that does not exist.** The real one is
   `mosaic::foundation::reflection::TypeRegistry registry` plus
   `mosaic::engine::reflection::ComponentReflection types`, with **registry
   first**, and the round-trip goes through
   `mosaic::foundation::serialization::to_value` / `from_value` on the struct.
   `tests/unit/vfx/screen_effect_test.cpp` is the sibling to copy.
2. **Task 3 had to widen the Vulkan backend's `draw_hdr_resolve` signature too.**
   The plan deferred that to Task 8, which leaves the tree not compiling —
   `error: marked 'override', but does not override` — and so makes every later
   task's "run the suite" step a lie.
3. **Task 8/9's validation command was `mosaic edit`, which is not a
   subcommand.** It is **`mosaic editor`**. The wrong one exits 106 with **zero**
   validation errors, which is indistinguishable from a clean run. For a GUI run
   under `timeout 20`, the PASS condition is **exit 124** — `timeout` killing a
   window that cannot close itself.
4. **Task 7 put `create_bloom_chain` beside `create_hdr_target`, which
   segfaults.** The chain's one-time clear goes through
   `begin_single_time_commands`, so it must be created **after**
   `create_command_pool_and_buffers()`, beside `create_default_ibl_resources()`
   — and because that is after the pipelines, it needs a
   `write_bloom_descriptors()` call following it. `create_hdr_target` sits above
   the pool only because it submits nothing.

And two things the whole-branch review then found, both fixed in `b39ddbd`: the
bloom pass's `deps[0]` named only a shader-read source scope when the upsample
loads what the downsample *wrote*, and `write_bloom_descriptors` paired sets to a
packed vector by position. Neither was visible to any test; see
`tests/unit/render/bloom_backend_contract_test.cpp`.

**The pattern worth carrying forward:** every one of these six was found by
reading or by a real-device run, and **none** by the 1406-test suite. Three of
them (3, 4, and the `deps[0]` scope) are invisible to it by construction.
