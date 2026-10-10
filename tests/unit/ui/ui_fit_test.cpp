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

TEST_CASE("Fill crops the other axis when the image is wider than the rect", "[ui][fit]")
{
    // The mirror of the Fit pair above, and it was missing: both Fill cases used a square
    // image on a 2:1 rect, so both took the v-cropping branch and the u-cropping branch
    // was entered only by the totality sweep, which asserts nothing but isfinite. Found in
    // review -- the same argument the second Fit case's own comment makes.
    //
    // Texture 200x100 (image_aspect 2.0); rect {0.2, 0.2} at screen aspect 0.5 is 1:2 in
    // pixels (rect_aspect 0.5), so the image is relatively wider and u must crop:
    // keep = 0.5 / 2.0 = 0.25, centred on 0.5 -> u in [0.375, 0.625].
    const auto q = resolve_image_quad(200, 100, kRect, 0.5f, Vec2{0, 0}, kWhole,
                                      static_cast<int>(ImageFitMode::Fill));
    REQUIRE(q.u0 == Approx(0.375f));
    REQUIRE(q.u1 == Approx(0.625f));
    // v untouched, and the geometry untouched -- that is what makes it Fill and not Fit.
    REQUIRE(q.v0 == Approx(0.0f));
    REQUIRE(q.v1 == Approx(1.0f));
    REQUIRE(q.rect.w == Approx(0.2f));
    REQUIRE(q.rect.h == Approx(0.2f));
}

TEST_CASE("Fill crops WITHIN the sub-rect on the u axis too", "[ui][fit]")
{
    // Containment on the branch the existing containment case does not reach. A 100x50
    // sub-rect of a 200x100 texture is u,v in [0, 0.5] with image_aspect 2.0; rect_aspect
    // 0.5 takes the u branch: keep = 0.25, mid = 0.25, half = 0.0625 -> u in
    // [0.1875, 0.3125]. Cropping against the whole texture would centre on 0.5 instead and
    // escape the sub-rect entirely.
    const auto q = resolve_image_quad(200, 100, kRect, 0.5f, Vec2{0, 0}, Vec2{100, 50},
                                      static_cast<int>(ImageFitMode::Fill));
    INFO("u=[" << q.u0 << "," << q.u1 << "] v=[" << q.v0 << "," << q.v1 << "]");
    REQUIRE(q.u0 >= 0.0f);
    REQUIRE(q.u1 <= 0.5f + 1e-6f);
    REQUIRE(q.u0 == Approx(0.1875f));
    REQUIRE(q.u1 == Approx(0.3125f));
    REQUIRE(q.u1 - q.u0 < 0.5f - 1e-6f); // it really cropped
    REQUIRE(q.v1 - q.v0 == Approx(0.5f)); // unconstrained axis keeps the full sub-rect
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
    SECTION("a negative position clamps to zero rather than sampling off the left edge")
    {
        // The size must be POSITIVE here. An earlier version of this section passed a
        // negative size too, which fails the `sub_rect_size_px.x > 0.0f` gate -- so the
        // whole sub-rect block, including the two position clamps, never executed and the
        // assertions were satisfied by the untouched 0,0,1,1 defaults. It was a tautology,
        // and the clamp's lower bound was pinned by nothing in the suite. Found in review.
        //
        // Exact values, not bounds: without the lower clamp this is u0 = -10/64 = -0.156
        // and u1 = 6/64 = 0.094, which with clamp-to-edge sampling replaces most of the
        // sprite with a smear of the texture's leftmost column, and with a repeat mode
        // shows a neighbour's art.
        const auto q = resolve_image_quad(64, 64, kRect, 2.0f, Vec2{-10, -10}, Vec2{16, 16},
                                          static_cast<int>(ImageFitMode::Stretch));
        REQUIRE(q.u0 == Approx(0.0f));
        REQUIRE(q.v0 == Approx(0.0f));
        REQUIRE(q.u1 == Approx(0.25f)); // 16/64, measured from the clamped origin
        REQUIRE(q.v1 == Approx(0.25f));
    }
    SECTION("a negative size falls back to the whole texture")
    {
        // The other half of what the old section conflated: a non-positive size is not
        // clamped, it means "whole texture", the same as a zero size.
        const auto q = resolve_image_quad(64, 64, kRect, 2.0f, Vec2{8, 8}, Vec2{-5, -5},
                                          static_cast<int>(ImageFitMode::Stretch));
        REQUIRE(q.u0 == Approx(0.0f));
        REQUIRE(q.v0 == Approx(0.0f));
        REQUIRE(q.u1 == Approx(1.0f));
        REQUIRE(q.v1 == Approx(1.0f));
    }
}

TEST_CASE("a zero sub-rect size on EITHER axis means the whole texture", "[ui][fit]")
{
    // The spec names one axis being zero, but every other case passes {0, 0} -- with both
    // predicates false, mutating the guard's `&&` to `||` left the whole suite green.
    // Under that mutation {64, 0} enters the block and yields v0 == v1, a zero-height UV
    // span: the sprite drawn as a single stretched texel row. Found in review.
    SECTION("zero height")
    {
        const auto q = resolve_image_quad(128, 64, kRect, 2.0f, Vec2{32, 16}, Vec2{64, 0},
                                          static_cast<int>(ImageFitMode::Stretch));
        REQUIRE(q.u0 == Approx(0.0f));
        REQUIRE(q.v0 == Approx(0.0f));
        REQUIRE(q.u1 == Approx(1.0f));
        REQUIRE(q.v1 == Approx(1.0f));
    }
    SECTION("zero width")
    {
        const auto q = resolve_image_quad(128, 64, kRect, 2.0f, Vec2{32, 16}, Vec2{0, 32},
                                          static_cast<int>(ImageFitMode::Stretch));
        REQUIRE(q.u0 == Approx(0.0f));
        REQUIRE(q.v0 == Approx(0.0f));
        REQUIRE(q.u1 == Approx(1.0f));
        REQUIRE(q.v1 == Approx(1.0f));
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
