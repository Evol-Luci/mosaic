#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/ui/ui_rect.hpp"
#include "foundation/math/vec2.hpp"

using Catch::Approx;
using mosaic::engine::ui::Anchor;
using mosaic::engine::ui::Rect;
using mosaic::engine::ui::resolve_in;
using mosaic::engine::ui::resolve_rect;
using mosaic::foundation::math::Vec2;

TEST_CASE("resolve_in maps an element into a sub-frame", "[ui][layout]")
{
    const Rect frame{0.5f, 0.25f, 0.4f, 0.4f};
    const Rect r = resolve_in(frame, Anchor::TopLeft, Vec2{0.0f, 0.0f}, Vec2{0.5f, 0.5f});
    REQUIRE(r.x == Approx(0.5f));            // frame.x + 0*frame.w
    REQUIRE(r.y == Approx(0.25f));           // frame.y + 0*frame.h
    REQUIRE(r.w == Approx(0.2f));            // 0.5 * frame.w
    REQUIRE(r.h == Approx(0.2f));            // 0.5 * frame.h
}

TEST_CASE("resolve_in with the screen frame equals resolve_rect", "[ui][layout]")
{
    const Rect screen{0.0f, 0.0f, 1.0f, 1.0f};
    const Rect a = resolve_in(screen, Anchor::BottomRight, Vec2{0.1f, 0.1f}, Vec2{0.2f, 0.3f});
    const Rect b = resolve_rect(Anchor::BottomRight, Vec2{0.1f, 0.1f}, Vec2{0.2f, 0.3f});
    REQUIRE(a.x == Approx(b.x));
    REQUIRE(a.y == Approx(b.y));
    REQUIRE(a.w == Approx(b.w));
    REQUIRE(a.h == Approx(b.h));
}
