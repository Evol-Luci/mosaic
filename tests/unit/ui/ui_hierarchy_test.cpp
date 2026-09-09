#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/ui/ui_hierarchy.hpp"
#include "engine/ui/ui_panel.hpp"
#include "engine/ui/ui_parent.hpp"
#include "engine/ui/ui_rect.hpp"
#include "engine/world/world.hpp"
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

TEST_CASE("resolve_parent_frame returns the screen when there is no UiParent", "[ui][layout]")
{
    mosaic::engine::World w;
    const auto e = w.create_entity();
    const Rect f = mosaic::engine::ui::resolve_parent_frame(w, e);
    REQUIRE(f.x == Approx(0.0f)); REQUIRE(f.y == Approx(0.0f));
    REQUIRE(f.w == Approx(1.0f)); REQUIRE(f.h == Approx(1.0f));
}

TEST_CASE("resolve_ui_rect places a child inside its parent panel", "[ui][layout]")
{
    mosaic::engine::World w;
    const auto parent = w.create_entity();
    mosaic::engine::ui::UiPanel pp;
    pp.anchor = static_cast<int>(Anchor::TopLeft);
    pp.pos = Vec2{0.5f, 0.5f}; pp.size = Vec2{0.4f, 0.4f};
    w.add_component<mosaic::engine::ui::UiPanel>(parent, pp);

    const auto child = w.create_entity();
    w.add_component<mosaic::engine::ui::UiParent>(child, mosaic::engine::ui::UiParent{parent});

    // Child TopLeft {0,0} size {0.5,0.5} within the parent frame {0.5,0.5,0.4,0.4}.
    const Rect r = mosaic::engine::ui::resolve_ui_rect(
        w, child, Anchor::TopLeft, Vec2{0.0f, 0.0f}, Vec2{0.5f, 0.5f});
    REQUIRE(r.x == Approx(0.5f));
    REQUIRE(r.y == Approx(0.5f));
    REQUIRE(r.w == Approx(0.2f));
    REQUIRE(r.h == Approx(0.2f));
}

TEST_CASE("resolve_parent_frame falls back to screen on invalid/cyclic parents", "[ui][layout]")
{
    mosaic::engine::World w;
    // A UiParent whose parent is nil -> screen.
    const auto orphan = w.create_entity();
    w.add_component<mosaic::engine::ui::UiParent>(
        orphan, mosaic::engine::ui::UiParent{mosaic::foundation::EntityID{}});
    const Rect f1 = mosaic::engine::ui::resolve_parent_frame(w, orphan);
    REQUIRE(f1.w == Approx(1.0f));

    // Self-parent cycle -> depth cap -> terminates (no infinite loop). Needs a UiPanel so
    // the walk recurses.
    const auto self = w.create_entity();
    mosaic::engine::ui::UiPanel sp;
    sp.pos = Vec2{0.1f, 0.1f}; sp.size = Vec2{0.5f, 0.5f};
    w.add_component<mosaic::engine::ui::UiPanel>(self, sp);
    w.add_component<mosaic::engine::ui::UiParent>(self, mosaic::engine::ui::UiParent{self});
    const Rect f2 = mosaic::engine::ui::resolve_parent_frame(w, self); // must terminate
    REQUIRE(f2.w > 0.0f);
}
