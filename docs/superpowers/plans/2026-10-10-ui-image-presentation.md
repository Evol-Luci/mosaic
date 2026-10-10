# UI Image Presentation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a `UiImage` draw one region of a texture (a pixel sub-rect, for atlases) and preserve the image's aspect ratio (`Stretch` / `Fit` / `Fill`), with `Stretch` the default so every existing scene renders unchanged.

**Architecture:** A pure function `resolve_image_quad` in a new `engine/ui/ui_fit.hpp` does all the arithmetic and is tested with no world, no device and no fake. `TextureCache` starts caching the pixel dimensions it already loads. `RenderSystem` calls the function and forwards the result; the Vulkan backend gains a `uv[4]` push member mirroring `UiTextPush` and performs no arithmetic.

**Tech Stack:** C++20, Catch2, GLSL (glslc), CMake presets (`debug`), ctest.

**Spec:** `docs/superpowers/specs/2026-10-10-ui-image-presentation-design.md`

## Global Constraints

- Branch: `ui-image-presentation` (exists; spec committed as `b193b7e`, `3efb6d8`, `2af6320`).
- Build: `cmake --build build/debug`. Test: `ctest --test-dir build/debug`. One case: `./build/debug/tests/mosaic_unit_tests "<case name>"`.
- **No `Vec4`.** `foundation/math` has only `Vec2`, `Vec3`, `Mat4`, `Quat`. UV quadruples are spelled `float u0, v0, u1, v1` (as `UiGlyphQuad` and `TextQuad` already do); pixel rects are two `Vec2`s (as `UiImage`'s `pos`/`size` already are).
- **A comma in a `TEST_CASE` name is fine** — `CatchAddTests.cmake` escapes it. But a probe script invoking a case **by name** must escape it as `\,` or the filter matches nothing and exits non-zero, which reads as a failing test.
- **Never `git commit --amend` after the pre-commit hook aborts.** The hook regenerates `docs/index.html` / `docs/reference/*.md` and aborts; no commit of yours exists yet, so `--amend` rewrites the upstream tip. `git add` what it rewrote and re-run the full commit. Recover a misfire with `git reset --soft origin/main`.
- `mosaic_unit_tests` is ~270MB and intermittently fails at the LINK step under memory pressure. Grep for a real compiler diagnostic; if there is none, re-run.
- A full build is ~4 minutes. Batch mutation probes into one scripted run.
- Tier rule `app -> studio -> engine -> foundation`, enforced by `tools/ci/check_dependency_rules.py`. `engine/ui/ui_fit.hpp` may include `foundation/math/*` and its sibling `engine/ui/ui_rect.hpp`, nothing else.
- A new test file must be added to `tests/CMakeLists.txt` beside its neighbours, or its cases silently do not exist.
- `docs/project/next-steps.md` is updated on this branch, in this PR.

## Review Focus

1. **A texture whose dimensions are zero.** `TextureCache` will cache `{handle, w, h}`; the spec's invariant is that a non-zero handle implies non-zero dimensions. If an `Image` ever loads with `width == 0`, `resolve_image_quad` divides by zero and emits NaN UVs, which makes every hit test and every sampler read undefined. Task 1 Step 1 pins that the function is total — it must never return NaN for any input.
2. **A sub-rect larger than the texture, or offset past its edge.** Clamping is specified, but the clamp has two independent halves (position and size) and clamping only one still samples outside the texture — which in an atlas yields a neighbour's art. Task 1 pins both halves separately.
3. **`Fill` with a sub-rect.** The composition rule (crop *within* the sub-rect) is the spec's main correctness claim, and the natural wrong implementation — crop against the whole texture — is invisible unless the test asserts the resulting UVs lie inside the sub-rect's bounds. Task 1 Step 1 asserts containment, not just a value.
4. **A square screen.** Every aspect test must use a non-square screen aspect, or dropping the screen-aspect term passes. Task 1's cases use 2.0 deliberately; a reviewer should check no aspect case uses 1.0.
5. **An existing scene with a default `UiImage`.** The migration guarantee. `Stretch` plus a zero sub-rect size must emit exactly today's rect and full `0..1` UVs — and the demo's own top-right checker image is exactly this case, so a regression ships visibly. Task 3 pins it through the real `RenderSystem`.

---

### Task 1: `resolve_image_quad` — all the arithmetic, no dependencies

**Files:**
- Create: `engine/ui/ui_fit.hpp`
- Create: `tests/unit/ui/ui_fit_test.cpp`
- Modify: `tests/CMakeLists.txt` (register the new test file beside the other `unit/ui/` entries)

**Interfaces:**
- Consumes: `mosaic::engine::ui::Rect` from `engine/ui/ui_rect.hpp`; `Vec2` from `foundation/math/vec2.hpp`.
- Produces: `mosaic::engine::ui::ImageFitMode` (enum), `mosaic::engine::ui::ResolvedImageQuad{Rect rect; float u0, v0, u1, v1;}`, and
  `resolve_image_quad(std::uint32_t texture_w, std::uint32_t texture_h, const Rect& authored_rect, float screen_aspect, Vec2 sub_rect_pos_px, Vec2 sub_rect_size_px, int fit_mode) -> ResolvedImageQuad`.
  Tasks 2–4 use these exact names.

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/ui/ui_fit_test.cpp`. These are the whole arithmetic contract; they need no world, no device and no fake, which is the point of extracting the function.

```cpp
#include <cmath>
#include <cstdint>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/ui/ui_fit.hpp"

using Catch::Approx;
using mosaic::engine::ui::ImageFitMode;
using mosaic::engine::ui::Rect;
using mosaic::engine::ui::resolve_image_quad;
using mosaic::foundation::math::Vec2;

namespace
{
// The authored rect used by most cases: a fifth of the screen, offset from the origin so
// a dropped offset shows up rather than cancelling.
constexpr Rect kRect{0.1f, 0.2f, 0.2f, 0.2f};
constexpr Vec2 kWhole{0.0f, 0.0f}; // zero size == whole texture
} // namespace

TEST_CASE("Stretch with no sub-rect emits the authored rect and full UVs", "[ui][fit]")
{
    // The migration guarantee. A default-constructed UiImage lands here, so if this case
    // ever changes, every existing HUD in every scene changes with it.
    const auto q = resolve_image_quad(64, 32, kRect, 2.0f, Vec2{0, 0}, kWhole,
                                      static_cast<int>(ImageFitMode::Stretch));
    REQUIRE(q.rect.x == Approx(0.1f));
    REQUIRE(q.rect.y == Approx(0.2f));
    REQUIRE(q.rect.w == Approx(0.2f));
    REQUIRE(q.rect.h == Approx(0.2f));
    REQUIRE(q.u0 == Approx(0.0f));
    REQUIRE(q.v0 == Approx(0.0f));
    REQUIRE(q.u1 == Approx(1.0f));
    REQUIRE(q.v1 == Approx(1.0f));
}

TEST_CASE("a pixel sub-rect becomes normalized UVs", "[ui][fit]")
{
    // Every number asymmetric on purpose: a case with equal x/y or equal w/h passes with
    // the axes transposed, which is the single easiest mistake to make here.
    //
    // Deliberately STRONGER than the spec's worked example, which used size {16, 16}: with
    // equal width and height a transposed SIZE still passes, so only the position was
    // actually pinned. {16, 32} pins both. The expected UVs below differ from the spec's
    // numbers for that reason -- it is not drift.
    const auto q = resolve_image_quad(128, 128, kRect, 2.0f, Vec2{32, 64}, Vec2{16, 32},
                                      static_cast<int>(ImageFitMode::Stretch));
    REQUIRE(q.u0 == Approx(32.0f / 128.0f));   // 0.25
    REQUIRE(q.v0 == Approx(64.0f / 128.0f));   // 0.5
    REQUIRE(q.u1 == Approx(48.0f / 128.0f));   // 0.375
    REQUIRE(q.v1 == Approx(96.0f / 128.0f));   // 0.75
    // Stretch leaves the geometry alone even with a sub-rect.
    REQUIRE(q.rect.w == Approx(0.2f));
    REQUIRE(q.rect.h == Approx(0.2f));
}

TEST_CASE("a non-square texture is divided by its own axis", "[ui][fit]")
{
    // Catches dividing both axes by the width (or both by the height), which a square
    // texture cannot distinguish.
    const auto q = resolve_image_quad(200, 100, kRect, 2.0f, Vec2{50, 50}, Vec2{50, 50},
                                      static_cast<int>(ImageFitMode::Stretch));
    REQUIRE(q.u0 == Approx(0.25f)); // 50/200
    REQUIRE(q.v0 == Approx(0.5f));  // 50/100
    REQUIRE(q.u1 == Approx(0.5f));  // 100/200
    REQUIRE(q.v1 == Approx(1.0f));  // 100/100
}

TEST_CASE("Fit shrinks the rect using BOTH the texture and the screen aspect", "[ui][fit]")
{
    // THE sharp case, and the screen aspect is load-bearing by construction.
    //
    // A square texture in a rect of size {0.2, 0.2} on a screen of aspect 2.0. In PIXELS
    // that rect is 2:1, so preserving a 1:1 image means halving the width to 0.1 --
    // 0.1 * 2.0 : 0.2 == 1:1. Drop the screen-aspect term and the rect stays {0.2, 0.2},
    // rendering a square sprite as a 2:1 rectangle.
    //
    // A case on a SQUARE screen would pass either way, which is why none of these use 1.0.
    const auto q = resolve_image_quad(100, 100, kRect, 2.0f, Vec2{0, 0}, kWhole,
                                      static_cast<int>(ImageFitMode::Fit));
    REQUIRE(q.rect.w == Approx(0.1f));
    REQUIRE(q.rect.h == Approx(0.2f));
    // Centred inside the authored rect: 0.1 + (0.2 - 0.1) / 2.
    REQUIRE(q.rect.x == Approx(0.15f));
    REQUIRE(q.rect.y == Approx(0.2f));
    // Fit is geometry only -- the UVs stay full.
    REQUIRE(q.u0 == Approx(0.0f));
    REQUIRE(q.u1 == Approx(1.0f));
}

TEST_CASE("Fit letterboxes the other way when the image is wider than the rect", "[ui][fit]")
{
    // The opposite branch. Without this, an implementation that always shrinks width
    // passes the case above.
    //
    // Texture 200x100 (2:1). Rect {0.2, 0.2} on a screen aspect of 0.5 is 1:2 in pixels,
    // so the image is relatively wider: height must shrink. Target pixel ratio 2:1 means
    // w * 0.5 / h == 2, so h = w * 0.25 = 0.05.
    const auto q = resolve_image_quad(200, 100, kRect, 0.5f, Vec2{0, 0}, kWhole,
                                      static_cast<int>(ImageFitMode::Fit));
    REQUIRE(q.rect.w == Approx(0.2f));
    REQUIRE(q.rect.h == Approx(0.05f));
    REQUIRE(q.rect.y == Approx(0.2f + (0.2f - 0.05f) / 2.0f));
    REQUIRE(q.rect.x == Approx(0.1f));
}

TEST_CASE("Fill keeps the rect and crops the UVs instead", "[ui][fit]")
{
    // Fill is the mirror of Fit: geometry untouched, texture cropped. Same inputs as the
    // sharp Fit case, so the two cannot be implemented as the same branch.
    const auto q = resolve_image_quad(100, 100, kRect, 2.0f, Vec2{0, 0}, kWhole,
                                      static_cast<int>(ImageFitMode::Fill));
    REQUIRE(q.rect.w == Approx(0.2f));
    REQUIRE(q.rect.h == Approx(0.2f));
    REQUIRE(q.rect.x == Approx(0.1f));
    // The rect is 2:1 in pixels and the image 1:1, so half the texture's height is used,
    // centred: v from 0.25 to 0.75. u stays full.
    REQUIRE(q.u0 == Approx(0.0f));
    REQUIRE(q.u1 == Approx(1.0f));
    REQUIRE(q.v0 == Approx(0.25f));
    REQUIRE(q.v1 == Approx(0.75f));
}

TEST_CASE("Fill crops WITHIN the sub-rect and never outside it", "[ui][fit]")
{
    // The spec's main correctness claim, and the natural wrong implementation -- crop
    // against the whole texture -- is invisible unless containment is asserted.
    //
    // Sub-rect is the texture's bottom-right quarter: u,v in [0.5, 1.0]. Fill must crop
    // inside that box. Cropping against the whole texture would produce v around
    // [0.25, 0.75], which escapes the sub-rect and samples a neighbouring sprite.
    const auto q = resolve_image_quad(100, 100, kRect, 2.0f, Vec2{50, 50}, Vec2{50, 50},
                                      static_cast<int>(ImageFitMode::Fill));
    INFO("u=[" << q.u0 << "," << q.u1 << "] v=[" << q.v0 << "," << q.v1 << "]");
    REQUIRE(q.u0 >= 0.5f - 1e-6f);
    REQUIRE(q.u1 <= 1.0f + 1e-6f);
    REQUIRE(q.v0 >= 0.5f - 1e-6f);
    REQUIRE(q.v1 <= 1.0f + 1e-6f);
    // And it really did crop rather than passing the sub-rect through unchanged.
    REQUIRE(q.v1 - q.v0 < 0.5f - 1e-6f);
    REQUIRE(q.u1 - q.u0 == Approx(0.5f)); // the unconstrained axis keeps the full sub-rect
}

TEST_CASE("a sub-rect past the texture edge is clamped on both halves", "[ui][fit]")
{
    // Two independent halves -- position and size -- and clamping only one still samples
    // outside the texture, which in an atlas yields a neighbour's art. So both are
    // asserted, each by a case the other cannot satisfy.
    SECTION("size overruns the right and bottom edges")
    {
        const auto q = resolve_image_quad(64, 64, kRect, 2.0f, Vec2{32, 32}, Vec2{999, 999},
                                          static_cast<int>(ImageFitMode::Stretch));
        REQUIRE(q.u1 == Approx(1.0f));
        REQUIRE(q.v1 == Approx(1.0f));
        REQUIRE(q.u0 == Approx(0.5f));
    }
    SECTION("position starts past the edge")
    {
        const auto q = resolve_image_quad(64, 64, kRect, 2.0f, Vec2{999, 999}, Vec2{16, 16},
                                          static_cast<int>(ImageFitMode::Stretch));
        REQUIRE(q.u0 <= 1.0f);
        REQUIRE(q.u1 <= 1.0f + 1e-6f);
        REQUIRE(q.u1 >= q.u0);
    }
    SECTION("negative position and size clamp to zero rather than wrapping")
    {
        const auto q = resolve_image_quad(64, 64, kRect, 2.0f, Vec2{-10, -10}, Vec2{-5, -5},
                                          static_cast<int>(ImageFitMode::Stretch));
        REQUIRE(q.u0 >= 0.0f);
        REQUIRE(q.v0 >= 0.0f);
        REQUIRE(q.u1 >= q.u0);
        REQUIRE(q.v1 >= q.v0);
    }
}

TEST_CASE("resolve_image_quad is total and never returns NaN", "[ui][fit]")
{
    // The spec's invariant is that a non-zero texture handle implies non-zero dimensions,
    // so zero dimensions should be unreachable from RenderSystem. This asserts the
    // function is safe anyway: a divide by zero here would put NaN into every sampler
    // read and every emitted rect, which renders nothing and reports nothing.
    const int modes[] = {static_cast<int>(ImageFitMode::Stretch),
                         static_cast<int>(ImageFitMode::Fit),
                         static_cast<int>(ImageFitMode::Fill)};
    const std::uint32_t dims[] = {0u, 1u, 4096u};
    const float aspects[] = {0.0f, 0.5f, 1.0f, 2.0f};
    const Rect rects[] = {Rect{0, 0, 0, 0}, Rect{0.1f, 0.2f, 0.2f, 0.2f}};
    for (int mode : modes)
        for (std::uint32_t w : dims)
            for (std::uint32_t h : dims)
                for (float a : aspects)
                    for (const Rect& r : rects)
                    {
                        const auto q = resolve_image_quad(w, h, r, a, Vec2{3, 5},
                                                          Vec2{7, 11}, mode);
                        INFO("mode " << mode << " tex " << w << "x" << h << " aspect " << a);
                        REQUIRE(std::isfinite(q.rect.x));
                        REQUIRE(std::isfinite(q.rect.y));
                        REQUIRE(std::isfinite(q.rect.w));
                        REQUIRE(std::isfinite(q.rect.h));
                        REQUIRE(std::isfinite(q.u0));
                        REQUIRE(std::isfinite(q.v0));
                        REQUIRE(std::isfinite(q.u1));
                        REQUIRE(std::isfinite(q.v1));
                    }
}

TEST_CASE("an out-of-range fit_mode falls back to Stretch", "[ui][fit]")
{
    // fit_mode is an int on a reflected component, so a scene file can hold anything.
    // Matching how `anchor` already clamps out-of-range values to TopLeft rather than
    // indexing past an enum.
    for (int bad : {-1, 3, 99})
    {
        const auto q = resolve_image_quad(100, 100, kRect, 2.0f, Vec2{0, 0}, kWhole, bad);
        INFO("fit_mode " << bad);
        REQUIRE(q.rect.w == Approx(0.2f));
        REQUIRE(q.rect.h == Approx(0.2f));
        REQUIRE(q.u1 == Approx(1.0f));
    }
}
```

Register the file in `tests/CMakeLists.txt` beside the other `unit/ui/` entries (find them with `grep -n "unit/ui/" tests/CMakeLists.txt`), or none of these cases exist.

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build/debug 2>&1 | grep -m5 "error:"
```
Expected: FAIL at compile time — `engine/ui/ui_fit.hpp: No such file or directory`. A compile failure is the correct red; there is nothing to call yet.

- [ ] **Step 3: Implement `engine/ui/ui_fit.hpp`**

```cpp
#pragma once

#include <algorithm>
#include <cstdint>

#include "engine/ui/ui_rect.hpp"
#include "foundation/math/vec2.hpp"

namespace mosaic::engine::ui
{
using mosaic::foundation::math::Vec2;

// How a UiImage reconciles its texture's aspect ratio with its authored rect.
//
// Stretch is the default and is what shipped before this existed, so a
// default-constructed UiImage renders exactly as it always did.
enum class ImageFitMode
{
    Stretch = 0, // fill the rect, distorting if the aspects differ
    Fit     = 1, // whole image visible, rect shrunk and centred (letterbox)
    Fill    = 2, // rect fully covered, texture cropped (and cropped WITHIN the sub-rect)
};

// The rect to draw and the UVs to sample. The four UV floats are spelled exactly as
// UiGlyphQuad and TextQuad spell them -- foundation/math has no Vec4, and this slice is
// not the place to add a type the reflection layer has never seen.
struct ResolvedImageQuad
{
    Rect  rect;
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
};

// Resolve a UiImage's authored rect and pixel sub-rect into a quad to draw.
//
// `sub_rect_size_px` of zero on either axis means the whole texture, which is what a
// default-constructed UiImage carries.
//
// BOTH aspects matter. `authored_rect` is normalized screen space, which is not square: a
// rect of size {0.2, 0.2} is twice as wide as tall in PIXELS on a 2:1 display. So
// preserving the image's aspect means reconciling the texture's w/h against the rect's
// (w * screen_aspect) / h. Using only the texture aspect is the obvious mistake and is
// wrong on every non-square window, which is all of them.
//
// Total by construction: every divisor is guarded, so no input produces NaN. The caller's
// invariant (a non-zero texture handle implies non-zero dimensions) makes the degenerate
// paths unreachable in practice, but a NaN here would poison every sampler read and every
// emitted rect silently, so it is cheaper to be safe than to rely on the invariant.
inline ResolvedImageQuad resolve_image_quad(std::uint32_t texture_w, std::uint32_t texture_h,
                                            const Rect& authored_rect, float screen_aspect,
                                            Vec2 sub_rect_pos_px, Vec2 sub_rect_size_px,
                                            int fit_mode)
{
    ResolvedImageQuad out;
    out.rect = authored_rect;

    const float tw = static_cast<float>(texture_w);
    const float th = static_cast<float>(texture_h);

    // --- the sub-rect, in UV space -------------------------------------------------
    // Clamped on BOTH halves. Clamping only the size still lets a position past the edge
    // sample outside the texture, and in an atlas that is a neighbour's art -- which reads
    // as corrupt rather than as a bug.
    if (tw > 0.0f && th > 0.0f && sub_rect_size_px.x > 0.0f && sub_rect_size_px.y > 0.0f)
    {
        const float x0 = std::clamp(sub_rect_pos_px.x, 0.0f, tw);
        const float y0 = std::clamp(sub_rect_pos_px.y, 0.0f, th);
        const float x1 = std::clamp(x0 + sub_rect_size_px.x, x0, tw);
        const float y1 = std::clamp(y0 + sub_rect_size_px.y, y0, th);
        out.u0 = x0 / tw;
        out.v0 = y0 / th;
        out.u1 = x1 / tw;
        out.v1 = y1 / th;
    }
    // else: leave the full 0..1 range -- the whole-texture default.

    const ImageFitMode mode = (fit_mode == static_cast<int>(ImageFitMode::Fit))
                                  ? ImageFitMode::Fit
                                  : (fit_mode == static_cast<int>(ImageFitMode::Fill))
                                        ? ImageFitMode::Fill
                                        : ImageFitMode::Stretch; // also catches out of range
    if (mode == ImageFitMode::Stretch)
    {
        return out;
    }

    // --- the aspects ---------------------------------------------------------------
    // The IMAGE's aspect is the sub-rect's in pixels, not the whole texture's: with a
    // sub-rect in play, "the image" is that region.
    const float src_w_px = (out.u1 - out.u0) * tw;
    const float src_h_px = (out.v1 - out.v0) * th;
    if (!(src_w_px > 0.0f) || !(src_h_px > 0.0f) || !(screen_aspect > 0.0f)
        || !(authored_rect.w > 0.0f) || !(authored_rect.h > 0.0f))
    {
        return out; // degenerate: nothing meaningful to preserve, and no divide by zero
    }
    const float image_aspect = src_w_px / src_h_px;
    // The rect's aspect in PIXELS, which is where screen_aspect enters.
    const float rect_aspect = (authored_rect.w * screen_aspect) / authored_rect.h;

    if (mode == ImageFitMode::Fit)
    {
        // Shrink one axis of the GEOMETRY so the drawn rect matches the image's aspect,
        // then centre it inside the authored rect. UVs untouched.
        if (rect_aspect > image_aspect)
        {
            const float w = authored_rect.w * (image_aspect / rect_aspect);
            out.rect.x = authored_rect.x + (authored_rect.w - w) * 0.5f;
            out.rect.w = w;
        }
        else
        {
            const float h = authored_rect.h * (rect_aspect / image_aspect);
            out.rect.y = authored_rect.y + (authored_rect.h - h) * 0.5f;
            out.rect.h = h;
        }
        return out;
    }

    // Fill: GEOMETRY untouched, UVs cropped -- and cropped WITHIN the sub-rect, which is
    // what keeps an atlas sprite from bleeding into its neighbour.
    if (rect_aspect > image_aspect)
    {
        // Rect is relatively wider: use the full width, crop height, centred.
        const float keep = image_aspect / rect_aspect; // fraction of the sub-rect's height
        const float mid = (out.v0 + out.v1) * 0.5f;
        const float half = (out.v1 - out.v0) * keep * 0.5f;
        out.v0 = mid - half;
        out.v1 = mid + half;
    }
    else
    {
        const float keep = rect_aspect / image_aspect; // fraction of the sub-rect's width
        const float mid = (out.u0 + out.u1) * 0.5f;
        const float half = (out.u1 - out.u0) * keep * 0.5f;
        out.u0 = mid - half;
        out.u1 = mid + half;
    }
    return out;
}
} // namespace mosaic::engine::ui
```

- [ ] **Step 4: Run to verify it passes**

```bash
cmake --build build/debug && ./build/debug/tests/mosaic_unit_tests "[ui][fit]"
```
Expected: PASS, 10 cases.

- [ ] **Step 5: Confirm the tier rules still hold**

```bash
python3 tools/ci/check_dependency_rules.py
```
Expected: exit 0, no output. `engine/ui/ui_fit.hpp` includes only `engine/ui/ui_rect.hpp` (same tier) and `foundation/math/vec2.hpp` (allowed).

- [ ] **Step 6: Commit**

```bash
git add engine/ui/ui_fit.hpp tests/unit/ui/ui_fit_test.cpp tests/CMakeLists.txt
git commit -m "feat(ui): resolve_image_quad -- sub-rect UVs and Fit/Fill, pure and headless

All of U7's arithmetic in one pure function with no world, no device and no
fake, which is the reason it is extracted rather than inlined into
RenderSystem: the failure mode (using one of the two aspects) produces a
PLAUSIBLE image rather than a broken one, so only a unit test catches it.

BOTH aspects matter. A normalized rect is not square, so preserving the
image's aspect reconciles the texture's w/h against (rect.w * screen_aspect)
/ rect.h. Every aspect case uses a non-square screen deliberately -- on a
square screen, dropping the screen-aspect term passes.

Fill crops WITHIN the sub-rect, asserted by containment rather than by a
value, because cropping against the whole texture instead would sample a
neighbouring atlas sprite -- corrupt art, not an obvious bug."
```

---

### Task 2: `TextureCache` keeps the dimensions it already loads

**Files:**
- Modify: `engine/render/texture_cache.hpp`
- Test: `tests/unit/render/texture_cache_test.cpp` (exists)

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces: `mosaic::engine::render::TextureInfo{TextureHandle handle; std::uint32_t width; std::uint32_t height;}` and `TextureCache::get_info(id, registry, device, space) -> TextureInfo`. `get()` keeps its exact existing signature and return type. Task 3 calls `get_info`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/render/texture_cache_test.cpp`. Read the top of that file first for its existing fake-device and registry setup (`grep -n "struct\|class\|register" tests/unit/render/texture_cache_test.cpp | head -20`) and reuse it rather than building a second harness.

```cpp
TEST_CASE("TextureCache reports the pixel dimensions alongside the handle", "[render]")
{
    // The dimensions are already in hand when the texture is created -- get() loads the
    // Image to call create_texture and then throws them away. Aspect modes need them at
    // draw time, and loading the Image a second time per frame to recover a number the
    // cache already saw would be the wrong trade.
    //
    // Non-square and unequal on purpose: equal dimensions pass with width and height
    // transposed.
    FakeDevice device;
    AssetRegistry registry;
    const AssetID id = register_image(registry, /*w=*/64, /*h=*/32);

    TextureCache cache;
    const auto info = cache.get_info(id, registry, device);
    REQUIRE(info.handle != 0);
    REQUIRE(info.width == 64u);
    REQUIRE(info.height == 32u);
}

TEST_CASE("a non-zero handle always comes with non-zero dimensions", "[render]")
{
    // The invariant RenderSystem relies on, so it needs no "dimensions unknown" branch
    // and no frame can show the whole atlas while waiting for a load. RenderSystem
    // already skips unresolved textures with `if (tex == 0) return`, so this pairing is
    // what makes that skip sufficient.
    FakeDevice device;
    AssetRegistry registry;
    TextureCache cache;

    const auto missing = cache.get_info(AssetID{}, registry, device);
    REQUIRE(missing.handle == 0);

    const AssetID id = register_image(registry, 8, 4);
    const auto ok = cache.get_info(id, registry, device);
    REQUIRE(ok.handle != 0);
    REQUIRE(ok.width > 0u);
    REQUIRE(ok.height > 0u);
}

TEST_CASE("get_info uploads at most once per id and space", "[render]")
{
    // The caching contract get() already has, now on the path RenderSystem will call
    // every frame for every image. A regression here is a per-frame GPU upload, which
    // shows up as a frame-time cliff rather than as a wrong image.
    FakeDevice device;
    AssetRegistry registry;
    const AssetID id = register_image(registry, 16, 16);

    TextureCache cache;
    const auto a = cache.get_info(id, registry, device);
    const auto b = cache.get_info(id, registry, device);
    REQUIRE(a.handle == b.handle);
    REQUIRE(a.width == b.width);
    REQUIRE(device.create_texture_calls == 1);
}

TEST_CASE("get returns the same handle get_info does", "[render]")
{
    // get() keeps its signature because three of its four callers do not want
    // dimensions. This pins that it stays a thin view of the same cache rather than a
    // second code path that could drift.
    FakeDevice device;
    AssetRegistry registry;
    const AssetID id = register_image(registry, 16, 8);

    TextureCache cache;
    REQUIRE(cache.get(id, registry, device) == cache.get_info(id, registry, device).handle);
    REQUIRE(device.create_texture_calls == 1); // and still cached across both accessors
}
```

If the existing file has no `create_texture_calls` counter or no `register_image` helper, add them in this step — a counter on the fake device and a helper that registers an `Image` of the given size — rather than asserting something weaker.

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build/debug 2>&1 | grep -m5 "error:"
```
Expected: FAIL at compile time — `'get_info' is not a member of 'mosaic::engine::render::TextureCache'`.

- [ ] **Step 3: Implement**

In `engine/render/texture_cache.hpp`, add the struct above the class:

```cpp
// A resolved texture plus the pixel dimensions of the Image it came from.
//
// The dimensions ride along because TextureCache already loads the Image to create the
// texture; the aspect modes in engine/ui/ui_fit.hpp need them at draw time, and loading
// the Image again every frame to recover a number this cache already saw would be the
// wrong trade.
//
// INVARIANT: a non-zero `handle` always carries non-zero `width` and `height`. That is
// what lets RenderSystem's `if (tex == 0) return` double as a dimensions guard, so no
// caller needs a "dimensions unknown" branch.
struct TextureInfo
{
    TextureHandle handle = 0;
    std::uint32_t width  = 0;
    std::uint32_t height = 0;
};
```

Change the cached value from `TextureHandle` to `TextureInfo`, rename the existing body to `get_info`, and leave `get` as a thin wrapper:

```cpp
    // Unchanged signature: three of this class's four callers want only the handle.
    TextureHandle get(mosaic::foundation::serialization::AssetID id,
                      mosaic::engine::assets::AssetRegistry& registry, RenderDevice& device,
                      TextureColorSpace space = TextureColorSpace::Srgb)
    {
        return get_info(id, registry, device, space).handle;
    }

    TextureInfo get_info(mosaic::foundation::serialization::AssetID id,
                         mosaic::engine::assets::AssetRegistry& registry, RenderDevice& device,
                         TextureColorSpace space = TextureColorSpace::Srgb)
    {
        if (id == mosaic::foundation::serialization::AssetID{})
        {
            return TextureInfo{};
        }
        const Key key{id, space};
        auto it = cache_.find(key);
        if (it != cache_.end())
        {
            return it->second;
        }
        TextureInfo info;
        if (std::shared_ptr<Image> img = registry.load<Image>(id))
        {
            info.handle = device.create_texture(*img, space);
            // Recorded only alongside a successful upload, which is what keeps the
            // invariant above true: there is no path to a non-zero handle with zero
            // dimensions.
            if (info.handle != 0)
            {
                info.width = img->width;
                info.height = img->height;
            }
        }
        cache_.emplace(key, info);
        return info;
    }
```

and `std::unordered_map<Key, TextureInfo, KeyHash> cache_;`. Add `#include <cstdint>` if absent.

- [ ] **Step 4: Run to verify it passes**

```bash
cmake --build build/debug && ./build/debug/tests/mosaic_unit_tests "[render]" 2>&1 | tail -4
```
Expected: PASS. The three pre-existing `texture_cache_.get` callers in `render_system.hpp` compile unchanged, because `get`'s signature did not move.

- [ ] **Step 5: Commit**

```bash
git add engine/render/texture_cache.hpp tests/unit/render/texture_cache_test.cpp
git commit -m "feat(render): TextureCache keeps the pixel dimensions it already loads

get() loaded the Image to create the texture and threw the dimensions away.
The aspect modes need them at draw time, so the cache now holds
{handle, width, height} and get() becomes a thin wrapper -- three of its four
callers want only the handle, so its signature does not move.

Pins the invariant RenderSystem relies on: a non-zero handle always carries
non-zero dimensions, recorded only alongside a successful upload. That is what
lets the existing \`if (tex == 0) return\` double as a dimensions guard, so no
caller needs an unknown-dimensions branch and no frame can show the whole
atlas while waiting for a load."
```

---

### Task 3: Wire it through `UiImage`, `RenderSystem` and the quad

**Files:**
- Modify: `engine/ui/ui_image.hpp` (add `sub_rect_pos`, `sub_rect_size`, `fit_mode` + their `reflect` lines)
- Modify: `engine/render/render_device.hpp` (`UiImageQuad` gains `u0,v0,u1,v1`)
- Modify: `engine/render/render_system.hpp:437-447` (the HUD image pass)
- Test: `tests/unit/ui/ui_image_reflection_test.cpp`, `tests/unit/render/render_system_test.cpp`

**Interfaces:**
- Consumes: `resolve_image_quad` / `ImageFitMode` / `ResolvedImageQuad` (Task 1); `TextureCache::get_info` / `TextureInfo` (Task 2).
- Produces: `UiImageQuad` with `float u0, v0, u1, v1`; `UiImage` with `Vec2 sub_rect_pos`, `Vec2 sub_rect_size`, `int fit_mode`. Task 4 pins the GLSL that consumes the UVs.

- [ ] **Step 1: Write the failing tests**

Extend the `UiImage` reflection round-trip in `tests/unit/ui/ui_image_reflection_test.cpp` — set every new field to a non-default value, because a field left at its default round-trips whether or not it is serialised:

```cpp
    im.sub_rect_pos  = Vec2{32.0f, 64.0f};
    im.sub_rect_size = Vec2{16.0f, 8.0f};
    im.fit_mode      = 2; // Fill
```
and after the round-trip:
```cpp
    REQUIRE(back.sub_rect_pos.x == Approx(32.0f));
    REQUIRE(back.sub_rect_pos.y == Approx(64.0f));
    REQUIRE(back.sub_rect_size.x == Approx(16.0f));
    REQUIRE(back.sub_rect_size.y == Approx(8.0f));
    REQUIRE(back.fit_mode == 2);
```

Then append to `tests/unit/render/render_system_test.cpp`:

```cpp
TEST_CASE("a default UiImage emits the authored rect and full UVs", "[render]")
{
    // The migration guarantee, through the REAL RenderSystem rather than through
    // resolve_image_quad alone -- this is the wiring, not the arithmetic. The demo's own
    // top-right checker image is exactly this case, so a regression here ships visibly.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(2.0f); // non-square on purpose: a square screen hides a dropped term

    const AssetID tex = register_ui_texture(registry, 64, 32);
    const auto e = world.create_entity();
    mosaic::engine::ui::UiImage im;
    im.texture = tex;
    im.pos = Vec2{0.1f, 0.2f};
    im.size = Vec2{0.2f, 0.2f};
    world.add_component<mosaic::engine::ui::UiImage>(e, im);

    rs.render(world, device, registry);

    REQUIRE(device.ui_image_draws.size() == 1);
    const auto& q = device.ui_image_draws[0].quads.at(0);
    REQUIRE(q.u0 == Approx(0.0f));
    REQUIRE(q.v0 == Approx(0.0f));
    REQUIRE(q.u1 == Approx(1.0f));
    REQUIRE(q.v1 == Approx(1.0f));
    REQUIRE(q.rect.w == Approx(0.2f));
    REQUIRE(q.rect.h == Approx(0.2f));
}

TEST_CASE("RenderSystem passes the texture's real dimensions into the fit maths", "[render]")
{
    // The wiring the arithmetic tests cannot see: RenderSystem must hand the TEXTURE's
    // dimensions to resolve_image_quad, not a constant and not the rect's. Only the
    // dimensions get_info returned can tell it how much to letterbox.
    //
    // Worked through rather than guessed, because the first version of this case was
    // WRONG: a 50x100 texture (aspect 0.5) in rect {0.2, 0.2} at screen aspect 0.5 gives
    // rect_aspect (0.2 * 0.5) / 0.2 == 0.5 -- EQUAL to the image aspect, so a correct
    // implementation letterboxes nothing and the case would have failed against working
    // code.
    //
    // With screen aspect 2.0 the rect is 2:1 in pixels against a 1:2 image, so Fit shrinks
    // the width by image/rect == 0.5/2.0: w = 0.2 * 0.25 = 0.05, centred at
    // 0.1 + (0.2 - 0.05) / 2 = 0.175.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(2.0f);

    const AssetID tex = register_ui_texture(registry, 50, 100);
    const auto e = world.create_entity();
    mosaic::engine::ui::UiImage im;
    im.texture = tex;
    im.pos = Vec2{0.1f, 0.2f};
    im.size = Vec2{0.2f, 0.2f};
    im.fit_mode = static_cast<int>(mosaic::engine::ui::ImageFitMode::Fit);
    world.add_component<mosaic::engine::ui::UiImage>(e, im);

    rs.render(world, device, registry);

    REQUIRE(device.ui_image_draws.size() == 1);
    const auto& q = device.ui_image_draws[0].quads.at(0);
    INFO("rect " << q.rect.x << "," << q.rect.y << " " << q.rect.w << "x" << q.rect.h);
    REQUIRE(q.rect.w == Approx(0.05f));
    REQUIRE(q.rect.h == Approx(0.2f));
    REQUIRE(q.rect.x == Approx(0.175f)); // centred inside the authored rect
}

TEST_CASE("RenderSystem forwards a pixel sub-rect as UVs", "[render]")
{
    // Asymmetric numbers so a transposed axis fails.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(2.0f);

    const AssetID tex = register_ui_texture(registry, 128, 64);
    const auto e = world.create_entity();
    mosaic::engine::ui::UiImage im;
    im.texture = tex;
    im.sub_rect_pos  = Vec2{32.0f, 16.0f};
    im.sub_rect_size = Vec2{16.0f, 32.0f};
    world.add_component<mosaic::engine::ui::UiImage>(e, im);

    rs.render(world, device, registry);

    const auto& q = device.ui_image_draws[0].quads.at(0);
    REQUIRE(q.u0 == Approx(0.25f));  // 32/128
    REQUIRE(q.v0 == Approx(0.25f));  // 16/64
    REQUIRE(q.u1 == Approx(0.375f)); // 48/128
    REQUIRE(q.v1 == Approx(0.75f));  // 48/64
}
```

`register_ui_texture(registry, w, h)` is a helper for this file: register an `Image` of the given size under a fresh `AssetID` and return it. If `render_system_test.cpp` already has an image-registering helper (`grep -n "register_.*texture\|register_image" tests/unit/render/render_system_test.cpp`), reuse it and extend it to take dimensions rather than adding a second one.

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build/debug 2>&1 | grep -m5 "error:"
```
Expected: FAIL at compile time — `'struct mosaic::engine::ui::UiImage' has no member named 'sub_rect_pos'` and `'UiImageQuad' has no member named 'u0'`.

- [ ] **Step 3: Add the component fields**

In `engine/ui/ui_image.hpp`, after `size`:

```cpp
    // Sub-rect in TEXTURE PIXELS; a zero width or height means the whole texture, which is
    // what a default-constructed UiImage carries -- so every existing scene is unchanged.
    //
    // Two Vec2s because that is exactly how this component already spells pos/size, and
    // because foundation/math has no Vec4. Pixels rather than normalized UVs because that
    // is how atlas art is authored and how any tool reports it; hand-divided UVs also
    // break silently when an atlas is repacked.
    Vec2  sub_rect_pos  = Vec2{0.0f, 0.0f};
    Vec2  sub_rect_size = Vec2{0.0f, 0.0f};
    // ui::ImageFitMode as an int, matching how `anchor` is an int cast to ui::Anchor.
    // 0 = Stretch (the pre-existing behaviour), 1 = Fit, 2 = Fill; out of range -> Stretch.
    int   fit_mode = 0;
```
and in `reflect`, after the `size` line:
```cpp
    visitor.member("sub_rect_pos", i.sub_rect_pos);
    visitor.member("sub_rect_size", i.sub_rect_size);
    visitor.member("fit_mode", i.fit_mode);
```

- [ ] **Step 4: Add the UVs to the quad**

In `engine/render/render_device.hpp`, `UiImageQuad` becomes — field order and spelling matching `UiGlyphQuad` exactly, so the two read the same:

```cpp
// One textured HUD quad in normalized screen space; samples the bound texture x tint.
// u0..v1 select a sub-region, resolved on the CPU by engine/ui/ui_fit.hpp -- the backend
// does no arithmetic. Defaults are the whole texture.
struct UiImageQuad
{
    mosaic::engine::ui::Rect       rect;
    float                          u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    mosaic::foundation::math::Vec3 tint;
    float                          alpha = 1.0f;
};
```

- [ ] **Step 5: Wire the HUD image pass**

Replace the body of the image pass in `engine/render/render_system.hpp` (currently lines 437-447):

```cpp
            world.for_each<mosaic::engine::ui::UiImage>(
                [&](mosaic::foundation::EntityID e, mosaic::engine::ui::UiImage& im)
                {
                    if (im.alpha <= 0.0f) { return; }
                    // get_info rather than get: the aspect modes need the texture's pixel
                    // dimensions, and a non-zero handle guarantees they are non-zero.
                    const TextureInfo info = texture_cache_.get_info(im.texture, registry, device);
                    if (info.handle == 0) { return; }
                    const mosaic::engine::ui::Rect rect = mosaic::engine::ui::resolve_ui_rect(
                        world, e, to_anchor(im.anchor), im.pos, im.size);
                    // All the arithmetic lives in ui_fit.hpp, which is testable with no
                    // world and no device. aspect_ is the SCREEN aspect and is load-bearing:
                    // a normalized rect is not square, so the correction needs it as well as
                    // the texture's own aspect.
                    const auto q = mosaic::engine::ui::resolve_image_quad(
                        info.width, info.height, rect, aspect_, im.sub_rect_pos,
                        im.sub_rect_size, im.fit_mode);
                    device.draw_ui_image(
                        std::vector<UiImageQuad>{
                            UiImageQuad{q.rect, q.u0, q.v0, q.u1, q.v1, im.tint, im.alpha}},
                        info.handle);
                });
```

Add `#include "engine/ui/ui_fit.hpp"` to `render_system.hpp`'s includes.

- [ ] **Step 6: Run to verify it passes**

```bash
cmake --build build/debug && ./build/debug/tests/mosaic_unit_tests "[render]" 2>&1 | tail -4 && ./build/debug/tests/mosaic_unit_tests "[ui]" 2>&1 | tail -4
```
Expected: PASS for both tags.

- [ ] **Step 7: Run the whole suite**

```bash
ctest --test-dir build/debug 2>&1 | tail -4
```
Expected: all passing. The regenerated `docs/reference/components.md` will gain the three new `UiImage` fields; the pre-commit hook rewrites it, so `git add` it when the hook asks.

- [ ] **Step 8: Commit**

```bash
git add engine/ui/ui_image.hpp engine/render/render_device.hpp engine/render/render_system.hpp tests/
git commit -m "feat(ui): sub-rects and aspect modes on UiImage

UiImage gains sub_rect_pos/sub_rect_size (texture pixels) and fit_mode, all
reflected; UiImageQuad gains u0/v0/u1/v1 spelled exactly as UiGlyphQuad spells
them. RenderSystem calls get_info for the dimensions and resolve_image_quad for
the maths, then forwards the result -- the backend does no arithmetic.

Defaults are the pre-existing behaviour: Stretch with a zero sub-rect size
emits the authored rect and full 0..1 UVs, pinned through the real RenderSystem
because the demo's top-right checker image is exactly that case."
```

---

### Task 4: The Vulkan push constant and the shader

**Files:**
- Modify: `modules/render_vulkan/vulkan_render_device.cpp` (`UiImagePush`, its `static_assert`, `draw_ui_image`)
- Modify: `modules/render_vulkan/shaders/ui_image.vert`
- Test: `tests/unit/render/shader_specular_contract_test.cpp` (or a new `ui_shader_contract_test.cpp` if preferred — then register it in `tests/CMakeLists.txt`)

**Interfaces:**
- Consumes: `UiImageQuad`'s `u0,v0,u1,v1` (Task 3).
- Produces: nothing. Terminal task for the render path.

- [ ] **Step 1: Write the contract assertions**

Append to `tests/unit/render/shader_specular_contract_test.cpp` (it already has `read_shader` / `code_only` / `normalize_ws` in scope):

```cpp
TEST_CASE("ui_image.vert maps UVs through the push constant rather than the corner",
          "[render]")
{
    // Before sub-rects, ui_image.vert set `v_uv = c` -- the raw corner, i.e. always the
    // whole texture. That line is the one a sub-rect replaces, and leaving it would make
    // the UVs this slice computes arrive and do nothing: every image would keep rendering
    // the full texture, with the component fields, the cache, the maths and the push
    // constant all correct and all ignored.
    //
    // The same mix() form ui_text.vert already uses, so the two read alike.
    const std::string src = normalize_ws(code_only(read_shader("ui_image.vert")));
    INFO("If this fails, check that v_uv interpolates pc.uv rather than the raw corner.");
    REQUIRE(src.find("v_uv = vec2(mix(pc.uv.x, pc.uv.z, c.x), mix(pc.uv.y, pc.uv.w, c.y));")
            != std::string::npos);
    // And that the push block actually declares it.
    REQUIRE(src.find("vec4 uv;") != std::string::npos);
    // The raw-corner assignment must be GONE, not merely shadowed by a later line.
    REQUIRE(src.find("v_uv = c;") == std::string::npos);
}

TEST_CASE("the ui-image push constant copies all four UV floats", "[render]")
{
    // CI has no GPU, so this is the textual instrument the backend already uses elsewhere.
    // Honest limit, as with every pin of this kind: it asserts the lines exist, never that
    // they are correct -- which is exactly how a previous slice shipped a dead feature
    // whose pin asserted the presence of the call that broke it.
    const std::string src =
        mosaic::tests::normalize_ws(mosaic::tests::code_only(read_backend()));
    REQUIRE(src.find("pc.uv[0] = q.u0;") != std::string::npos);
    REQUIRE(src.find("pc.uv[1] = q.v0;") != std::string::npos);
    REQUIRE(src.find("pc.uv[2] = q.u1;") != std::string::npos);
    REQUIRE(src.find("pc.uv[3] = q.v1;") != std::string::npos);
}
```

`read_backend()` is already defined in `tests/unit/render/texture_colorspace_contract_test.cpp`; if it is not visible from the specular file, put these two cases in that file instead rather than duplicating the helper.

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build/debug && ./build/debug/tests/mosaic_unit_tests "ui_image.vert maps UVs through the push constant rather than the corner"
```
Expected: FAIL — the shader still has `v_uv = c;` and no `vec4 uv;`.

- [ ] **Step 3: Grow the push constant**

In `modules/render_vulkan/vulkan_render_device.cpp`:

```cpp
struct UiImagePush
{
    float rect[4];
    // Sub-region UVs: u0, v0, u1, v1, resolved on the CPU by engine/ui/ui_fit.hpp. Mirrors
    // UiTextPush, which has carried a uv[4] for glyph sub-rects since the text slice --
    // this is the same mechanism, not a new one.
    float uv[4];
    float tint[4];
};
static_assert(sizeof(UiImagePush) == 48, "ui-image push constant must match the shader");
```

and in `draw_ui_image`, after the `rect` copies:

```cpp
            pc.uv[0] = q.u0; pc.uv[1] = q.v0; pc.uv[2] = q.u1; pc.uv[3] = q.v1;
```

- [ ] **Step 4: Update the shader**

`modules/render_vulkan/shaders/ui_image.vert` — the push block gains `uv` **between** `rect` and `tint`, matching the C++ field order, and the body stops using the raw corner:

```glsl
layout(push_constant) uniform Push {
    vec4 rect;   // x, y, w, h in [0,1], top-left origin
    vec4 uv;     // u0, v0, u1, v1 -- a sub-region of the bound texture
    vec4 tint;   // rgb + alpha
} pc;
```
and
```glsl
    // Interpolate the sub-region rather than using the corner directly. Identical in form
    // to ui_text.vert's glyph UVs.
    v_uv = vec2(mix(pc.uv.x, pc.uv.z, c.x), mix(pc.uv.y, pc.uv.w, c.y));
```

- [ ] **Step 5: Run to verify it passes, and that the shader compiles**

```bash
cmake --build build/debug 2>&1 | grep -iE "error|ui_image" | head -5
ctest --test-dir build/debug 2>&1 | tail -4
```
Expected: `Compiling ui_image.vert -> SPIR-V` with no `error:` lines, and all tests passing. A GLSL type error here would be `pc.uv` used before the block declares it.

- [ ] **Step 6: Verify both window entry points**

```bash
timeout 180 ./build/debug/app/mosaic editor --ticks 90 2>&1 | tail -3; echo "exit=$?"
timeout 180 ./build/debug/app/mosaic run --window --ticks 90 2>&1 | tail -3; echo "exit=$?"
```
Expected: both exit 0 with no `VUID` / `validation` / `error` lines. Meaningful rather than ceremonial here: a push-constant size mismatch between the pipeline layout's declared range and the shader's block is exactly what Vulkan validation reports.

- [ ] **Step 7: Commit**

```bash
git add modules/render_vulkan/vulkan_render_device.cpp modules/render_vulkan/shaders/ui_image.vert tests/
git commit -m "feat(render): ui-image sub-region UVs through the push constant

UiImagePush gains uv[4] (32 -> 48) and ui_image.vert interpolates it with the
same mix() form ui_text.vert has used for glyph sub-rects since the text slice.

The contract test forbids the old \`v_uv = c;\` rather than only requiring the
new line: leaving the raw-corner assignment in place would make every UV this
slice computes arrive and do nothing, with the component fields, the cache, the
maths and the push constant all correct and all ignored."
```

---

### Task 5: Mutation probes, the demo, and the handoff

**Files:**
- Modify: `app/run/demo_scene.hpp` (give the existing top-right checker image a sub-rect and a fit mode, so the feature is visible)
- Modify: `docs/project/next-steps.md`, `docs/project/deferred-work-backlog.md`

**Interfaces:**
- Consumes: everything above. Terminal task.

- [ ] **Step 1: Run the mutation probes**

Derived from the **code**, statement by statement — not from the assertion list, which cannot find unasserted code. Script them into one run (each needs a rebuild), apply, rebuild, run `"[ui][fit]"` and `"[render]"`, confirm the expected red, revert, and **rebuild before judging the baseline** (a restored source with a stale binary reports the mutant's results).

| # | Mutation in `ui_fit.hpp` (unless noted) | Must turn red |
|---|---|---|
| 1 | `rect_aspect = authored_rect.w / authored_rect.h` (drop `screen_aspect`) | both Fit cases, the Fill case |
| 2 | `image_aspect = 1.0f` (drop the texture aspect) | both Fit cases, the Fill case |
| 3 | swap the `Fit` and `Fill` branch bodies | the Fit and Fill cases |
| 4 | in `Fill`, crop against `0..1` instead of `out.v0/out.v1` | "Fill crops WITHIN the sub-rect" |
| 5 | `out.u0 = y0 / th; out.v0 = x0 / tw;` (transpose the axes) | the sub-rect and non-square-texture cases |
| 6 | drop the `sub_rect_size_px.x > 0.0f` guard | "Stretch with no sub-rect" (and the NaN sweep) |
| 7 | drop the `std::clamp` on `x1`/`y1` | "size overruns the right and bottom edges" |
| 8 | drop the `std::clamp` on `x0`/`y0` | "position starts past the edge", "negative position" |
| 9 | `out.rect.x` left unchanged in the `Fit` width branch (no centring) | the Fit cases' `rect.x` assertion |
| 10 | `texture_cache.hpp`: record `width/height` even when `handle == 0` | "a non-zero handle always comes with non-zero dimensions" |
| 11 | `render_system.hpp`: pass `rect.w, rect.h` instead of `info.width, info.height` | "passes the texture's real dimensions into the fit maths" |
| 12 | `ui_image.vert`: restore `v_uv = c;` below the mix line | the `ui_image.vert` contract case |

Record each result. A probe that produces no red is a finding to write down, not a failure to hide.

- [ ] **Step 2: Make the feature visible in the demo**

The demo's top-right checker image (`app/run/demo_scene.hpp`, the `UiImage` on the camera entity, currently `pos {0.03,0.03}`, `size {0.12,0.12}`) is the natural place. Set `fit_mode` to `Fit` and leave the sub-rect at its default, with a comment saying what a human should see and why:

```cpp
    // Fit rather than Stretch so the aspect modes are visible in the running demo: the
    // checker texture is square, and this rect is square in NORMALIZED space, which means
    // it is NOT square in pixels on any non-square window -- so Stretch visibly distorted
    // the checks into rectangles and Fit letterboxes them back to squares. That makes the
    // whole point of the slice checkable by eye, and it changes the demo's appearance on
    // purpose.
```

Do **not** also set a sub-rect here: the checker is not an atlas, so a sub-rect would just crop it and the change would read as a mistake.

- [ ] **Step 3: Verify the suite and both entry points once more**

```bash
cmake --build build/debug && ctest --test-dir build/debug 2>&1 | tail -4
timeout 180 ./build/debug/app/mosaic run --window --ticks 90 2>&1 | tail -2; echo "exit=$?"
```
Expected: all tests passing, exit 0, no validation lines. Note the demo-scene entity-count tests are unaffected — no entity or component is added, only fields on an existing `UiImage`.

- [ ] **Step 4: Report what a human must check**

Screenshots are unavailable in this sandbox, so do not claim visual confirmation. Report:

> With `mosaic editor` or `mosaic run --window`: the top-right checker image should now be **square**, letterboxed inside its rect, rather than stretched into rectangles. Resize the window — it should stay square at every aspect ratio, which is the whole-slice check in one observation, since it can only hold if both the texture aspect and the screen aspect are being used.

- [ ] **Step 5: Retire the backlog entry and write the handoff**

In `docs/project/deferred-work-backlog.md`, strike through **U7 image presentation** in the UI pillar's decomposed list with `**SHIPPED 2026-10-10**` and what actually happened — keeping the original text. Note explicitly that 9-slice remains open and still depends on this slice's UVs, which now exist.

Add a `**Last updated:** 2026-10-10 (...)` entry at the top of `docs/project/next-steps.md`, matching the existing dense-parenthetical style, covering: what shipped; that the sub-rect mechanism was a near-copy of `ui_text`'s shipped `uv[4]` pattern rather than new design; that `TextureCache` already had the dimensions and threw them away; that `if (tex == 0) return` turned "what if dimensions are unknown" into an invariant rather than a fallback; why the maths is CPU-side behind a pure function and the stated condition for revisiting that (per-pixel work such as tiling or a shader-side 9-slice); the probe results including any that produced no red; the gate count; and that the demo now uses `Fit` so the feature is visible, with visual confirmation outstanding.

- [ ] **Step 6: Commit, push, open the PR**

```bash
git add app/run/demo_scene.hpp docs/project/next-steps.md docs/project/deferred-work-backlog.md
git commit -m "docs: U7 image presentation shipped"
git push -u origin ui-image-presentation
```
Then open a PR against `main`, and request review before merging.
