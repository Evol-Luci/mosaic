#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/ui/ui_rect.hpp"
#include "foundation/math/vec2.hpp"

using mosaic::engine::ui::Anchor;
using mosaic::engine::ui::Rect;
using mosaic::engine::ui::resolve_rect;
using mosaic::foundation::math::Vec2;
using Catch::Approx;

namespace
{
// pos is a normalized offset from the anchor toward the screen interior;
// size is a fraction of the screen (w, h).
constexpr Vec2 kPos{0.10f, 0.20f};
constexpr Vec2 kSize{0.30f, 0.15f};
} // namespace

TEST_CASE("resolve_rect anchors to the top-left corner", "[ui][layout]")
{
    const Rect r = resolve_rect(Anchor::TopLeft, kPos, kSize);
    REQUIRE(r.x == Approx(0.10f)); // offset in from the left
    REQUIRE(r.y == Approx(0.20f)); // offset down from the top
    REQUIRE(r.w == Approx(0.30f));
    REQUIRE(r.h == Approx(0.15f));
}

TEST_CASE("resolve_rect anchors to the bottom-right corner", "[ui][layout]")
{
    const Rect r = resolve_rect(Anchor::BottomRight, kPos, kSize);
    // pos pushes toward the interior: in from the right / up from the bottom.
    REQUIRE(r.x == Approx(1.0f - 0.30f - 0.10f)); // 0.60
    REQUIRE(r.y == Approx(1.0f - 0.15f - 0.20f)); // 0.65
    REQUIRE(r.w == Approx(0.30f));
    REQUIRE(r.h == Approx(0.15f));
}

TEST_CASE("resolve_rect centers on the middle", "[ui][layout]")
{
    const Rect r = resolve_rect(Anchor::Center, Vec2{0.0f, 0.0f}, kSize);
    REQUIRE(r.x == Approx(0.5f - 0.30f / 2.0f)); // 0.35
    REQUIRE(r.y == Approx(0.5f - 0.15f / 2.0f)); // 0.425
}

TEST_CASE("resolve_rect edge anchors mix an edge and a centered axis", "[ui][layout]")
{
    // Top: horizontally centered, offset down from the top edge.
    const Rect top = resolve_rect(Anchor::Top, Vec2{0.0f, 0.05f}, kSize);
    REQUIRE(top.x == Approx(0.5f - 0.30f / 2.0f)); // centered X
    REQUIRE(top.y == Approx(0.05f));               // in from top

    // Right: vertically centered, offset in from the right edge.
    const Rect right = resolve_rect(Anchor::Right, Vec2{0.05f, 0.0f}, kSize);
    REQUIRE(right.x == Approx(1.0f - 0.30f - 0.05f)); // in from right
    REQUIRE(right.y == Approx(0.5f - 0.15f / 2.0f));  // centered Y
}
