#include <limits>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/ui/ui_button.hpp"
#include "engine/ui/ui_button_rects.hpp"
#include "engine/ui/ui_clip_rect.hpp"
#include "engine/ui/ui_hierarchy.hpp"
#include "engine/ui/ui_panel.hpp"
#include "engine/ui/ui_parent.hpp"
#include "engine/ui/ui_rect.hpp"
#include "engine/ui/ui_scroll.hpp"
#include "engine/ui/ui_scroll_input.hpp"
#include "engine/ui/ui_stack.hpp"
#include "engine/world/world.hpp"

using Catch::Approx;
using mosaic::engine::World;
using mosaic::engine::ui::Anchor;
using mosaic::engine::ui::apply_wheel_scroll;
using mosaic::engine::ui::kScrollNotch;
using mosaic::engine::ui::PointerState;
using mosaic::engine::ui::Rect;
using mosaic::engine::ui::resolve_clip;
using mosaic::engine::ui::resolve_parent_frame;
using mosaic::engine::ui::resolve_ui_rect;
using mosaic::engine::ui::scroll_max_offset;
using mosaic::engine::ui::scroll_offset_of;
using mosaic::engine::ui::to_anchor;
using mosaic::engine::ui::UiClipRect;
using mosaic::engine::ui::UiPanel;
using mosaic::engine::ui::UiParent;
using mosaic::engine::ui::UiScroll;
using mosaic::engine::ui::UiStack;
using mosaic::foundation::EntityID;

namespace
{
UiPanel panel_at(float x, float y, float w, float h)
{
    UiPanel p;
    p.anchor = static_cast<int>(Anchor::TopLeft);
    p.pos    = {x, y};
    p.size   = {w, h};
    return p;
}

// A scroll region at {0.2, 0.1, 0.4, 0.2} of the screen with one child, so every number
// below is checkable by hand: container height 0.2, so one container-height of offset is
// 0.2 of normalized Y.
struct Fixture
{
    World    world;
    EntityID region{};
    EntityID child{};

    explicit Fixture(float content)
    {
        region = world.create_entity();
        world.add_component<UiPanel>(region, panel_at(0.2f, 0.1f, 0.4f, 0.2f));
        world.add_component<UiClipRect>(region, UiClipRect{});
        world.add_component<UiScroll>(region, UiScroll{0.0f, content});

        child = world.create_entity();
        world.add_component<UiPanel>(child, panel_at(0.0f, 0.5f, 1.0f, 0.25f));
        world.add_component<UiParent>(child, UiParent{region});
    }

    void set_offset(float v) { world.get_component<UiScroll>(region)->offset = v; }

    Rect child_rect()
    {
        const UiPanel* p = world.get_component<UiPanel>(child);
        return resolve_ui_rect(world, child, to_anchor(p->anchor), p->pos, p->size);
    }
};
} // namespace

// ------------------------------------------------------------------- O1: the conservation law

TEST_CASE("O1 a scroll translates a child in Y and leaves x, w and h bit-identical",
          "[ui][scroll]")
{
    // THE SLICE'S CONSERVATION LAW, and the reason it is bit-exact rather than approximate:
    // resolve_in derives x, w and h from the frame's x and w, which the scroll subtraction
    // never writes. A tolerance here would hide a scroll that scaled as well as translated.
    Fixture f(2.0f);
    const Rect at0 = f.child_rect();
    f.set_offset(0.5f);
    const Rect at_half = f.child_rect();

    REQUIRE(at_half.x == at0.x); // bit-identical, not Approx
    REQUIRE(at_half.w == at0.w);
    REQUIRE(at_half.h == at0.h);
    // Container height 0.2, offset 0.5 container-heights => 0.1 of normalized Y, upward.
    REQUIRE(at_half.y == Approx(at0.y - 0.1f).margin(1e-6));
}

TEST_CASE("O1 every child of a scrolled region moves by the same delta", "[ui][scroll]")
{
    // A scroll is a RIGID translation. If two siblings move by different amounts, something
    // in the path is scaling rather than translating -- which a single-child test cannot see.
    Fixture f(3.0f);
    const EntityID second = f.world.create_entity();
    f.world.add_component<UiPanel>(second, panel_at(0.1f, 0.05f, 0.5f, 0.1f));
    f.world.add_component<UiParent>(second, UiParent{f.region});

    auto rect_of = [&](EntityID e) {
        const UiPanel* p = f.world.get_component<UiPanel>(e);
        return resolve_ui_rect(f.world, e, to_anchor(p->anchor), p->pos, p->size);
    };

    const Rect a0 = rect_of(f.child);
    const Rect b0 = rect_of(second);
    f.set_offset(1.25f);
    const Rect a1 = rect_of(f.child);
    const Rect b1 = rect_of(second);

    REQUIRE((a1.y - a0.y) == Approx(b1.y - b0.y).margin(1e-7));
    REQUIRE((a1.y - a0.y) == Approx(-1.25f * 0.2f).margin(1e-6));
}

TEST_CASE("O1 the scroll step is measured against the container, not a stack slot",
          "[ui][scroll]")
{
    // `offset` is defined in CONTAINER-heights, so one unit of offset must move a child the
    // same distance whether the container divides its rect into slots or not. Against the
    // slot height the step would silently shrink with the slot count -- and a region that
    // both stacks and scrolls is the obvious authoring.
    Fixture plain(2.0f);
    const Rect plain0 = plain.child_rect();
    plain.set_offset(0.5f);
    const float plain_delta = plain.child_rect().y - plain0.y;

    Fixture stacked(2.0f);
    stacked.world.add_component<UiStack>(stacked.region, UiStack{});
    const Rect stacked0 = stacked.child_rect();
    stacked.set_offset(0.5f);
    const float stacked_delta = stacked.child_rect().y - stacked0.y;

    REQUIRE(stacked_delta == Approx(plain_delta).margin(1e-7));
}

// ------------------------------------------------------------- O2: the clip does not move

TEST_CASE("O2 the resolved clip is bit-identical across every offset", "[ui][scroll]")
{
    // The one gate the decomposition states in these words. Bit-exact BY CONSTRUCTION: the
    // container's own rect is resolved from its ancestors, which know nothing about its
    // offset. Including the out-of-range value, because a scene file can author one.
    Fixture f(2.0f);
    const auto base = resolve_clip(f.world, f.child);
    REQUIRE(base.has_value());

    for (const float o : {0.0f, 0.25f, 0.9f, 1.0f, 5.0f, -3.0f})
    {
        f.set_offset(o);
        const auto c = resolve_clip(f.world, f.child);
        REQUIRE(c.has_value());
        REQUIRE(c->x == base->x);
        REQUIRE(c->y == base->y);
        REQUIRE(c->w == base->w);
        REQUIRE(c->h == base->h);
    }
}

// -------------------------------------------------- O3: offset zero is the pre-feature engine

TEST_CASE("O3 a region at offset zero resolves exactly as no region at all", "[ui][scroll]")
{
    // What makes the feature free for every scene that does not use it: the subtraction is of
    // a literal 0.0f, which is the float identity, so the frame is bit-identical rather than
    // merely close. Asserted against a world with NO UiScroll, not against itself.
    Fixture with(2.0f);
    Fixture without(2.0f);
    without.world.remove_component<UiScroll>(without.region);

    const Rect a = with.child_rect();
    const Rect b = without.child_rect();
    REQUIRE(a.x == b.x);
    REQUIRE(a.y == b.y);
    REQUIRE(a.w == b.w);
    REQUIRE(a.h == b.h);
}

TEST_CASE("O3 content that fits does not scroll at all", "[ui][scroll]")
{
    // `content <= 1` means the region is inert rather than being a special case downstream.
    // A NaN or negative content takes the same path, by the same `!(x > 1)` comparison.
    REQUIRE(scroll_max_offset(UiScroll{0.0f, 1.0f}) == 0.0f);
    REQUIRE(scroll_max_offset(UiScroll{0.0f, 0.3f}) == 0.0f);
    REQUIRE(scroll_max_offset(UiScroll{0.0f, -2.0f}) == 0.0f);
    REQUIRE(scroll_max_offset(UiScroll{0.0f, 2.5f}) == Approx(1.5f));

    Fixture f(1.0f);
    const Rect at0 = f.child_rect();
    f.set_offset(0.75f); // authored, but unreachable
    const Rect after = f.child_rect();
    REQUIRE(after.y == at0.y);
}

TEST_CASE("O3 an out-of-range authored offset is clamped on READ", "[ui][scroll]")
{
    // `offset` is a reflected float: a scene file or an unbounded inspector drag can set it
    // without the wheel ever running, so the clamp cannot live only where the wheel writes.
    REQUIRE(scroll_offset_of(UiScroll{5.0f, 2.0f}) == Approx(1.0f));  // max is content - 1
    REQUIRE(scroll_offset_of(UiScroll{-4.0f, 2.0f}) == 0.0f);
    const float nan_v = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(scroll_offset_of(UiScroll{nan_v, 2.0f}) == 0.0f); // !(NaN > 0) is true
}

// ----------------------------------------------------- the wheel path, and the no-region case

TEST_CASE("a wheel notch scrolls the region under the pointer by one notch", "[ui][scroll]")
{
    Fixture f(2.0f);
    PointerState p;
    p.x = 0.3f; p.y = 0.15f; p.inside = true; // inside {0.2,0.1,0.4,0.2}
    p.wheel = -1.0f;                           // toward the user => look further down
    apply_wheel_scroll(f.world, p);
    REQUIRE(f.world.get_component<UiScroll>(f.region)->offset == Approx(kScrollNotch));

    // Positive is away from the user, which is back toward the top.
    p.wheel = 1.0f;
    apply_wheel_scroll(f.world, p);
    REQUIRE(f.world.get_component<UiScroll>(f.region)->offset == Approx(0.0f));
}

TEST_CASE("the wheel clamps at both ends of a scroll region", "[ui][scroll]")
{
    Fixture f(2.0f); // max offset 1.0
    PointerState p;
    p.x = 0.3f; p.y = 0.15f; p.inside = true;
    p.wheel = -50.0f; // far past the end in one notch
    apply_wheel_scroll(f.world, p);
    REQUIRE(f.world.get_component<UiScroll>(f.region)->offset == Approx(1.0f));
    p.wheel = 50.0f;
    apply_wheel_scroll(f.world, p);
    REQUIRE(f.world.get_component<UiScroll>(f.region)->offset == Approx(0.0f));
}

TEST_CASE("a wheel delta outside the region, or with no pointer, scrolls nothing",
          "[ui][scroll]")
{
    Fixture f(2.0f);
    PointerState p;
    p.wheel = -5.0f;

    p.inside = true; p.x = 0.9f; p.y = 0.9f; // outside the region
    apply_wheel_scroll(f.world, p);
    REQUIRE(f.world.get_component<UiScroll>(f.region)->offset == 0.0f);

    // Inside the rect but NO POINTER PRESENT. x/y are clamped even when absent, so without
    // the `inside` gate a wheel event delivered while the cursor sat on the desktop would
    // scroll whichever region contains the clamped position.
    p.inside = false; p.x = 0.3f; p.y = 0.15f;
    apply_wheel_scroll(f.world, p);
    REQUIRE(f.world.get_component<UiScroll>(f.region)->offset == 0.0f);
}

TEST_CASE("a wheel delta with no scroll region present changes nothing and does not crash",
          "[ui][scroll]")
{
    // U9c shipped PointerState::wheel with NO consumer; this slice is the first one, so the
    // no-region path is a regression U9d can introduce. A region-less world must be
    // untouched by any delta.
    World world;
    const EntityID lone = world.create_entity();
    world.add_component<UiPanel>(lone, panel_at(0.1f, 0.1f, 0.5f, 0.5f));
    const Rect before = resolve_parent_frame(world, lone);

    PointerState p;
    p.x = 0.2f; p.y = 0.2f; p.inside = true; p.wheel = -7.5f;
    apply_wheel_scroll(world, p);

    const Rect after = resolve_parent_frame(world, lone);
    REQUIRE(after.x == before.x);
    REQUIRE(after.y == before.y);
    REQUIRE(after.w == before.w);
    REQUIRE(after.h == before.h);
}

TEST_CASE("a scroll region without a UiPanel is inert rather than a crash", "[ui][scroll]")
{
    // resolve_parent_frame returns the whole SCREEN for a parent with no UiPanel, so there is
    // no rect to scroll within -- the same rule UiStack states for itself.
    World world;
    const EntityID region = world.create_entity();
    world.add_component<UiScroll>(region, UiScroll{0.0f, 3.0f});
    PointerState p;
    p.x = 0.5f; p.y = 0.5f; p.inside = true; p.wheel = -2.0f;
    apply_wheel_scroll(world, p);
    REQUIRE(world.get_component<UiScroll>(region)->offset == 0.0f);
}

// ------------------------------------------------- O4: what is scrolled out is not clickable

TEST_CASE("O4 a button scrolled out of its clip stops being a hit candidate", "[ui][scroll]")
{
    // THE U8b BUG CLASS, which shipped once as a faded visual over a fully live button and
    // was caught by a human looking at a screenshot rather than by any test. U9b built the
    // filter; this is the first time it is driven by a REAL SCROLL rather than by an
    // authored clip, which is what the decomposition asked U9d to exercise.
    World world;
    const EntityID region = world.create_entity();
    world.add_component<UiPanel>(region, panel_at(0.2f, 0.1f, 0.4f, 0.2f));
    world.add_component<UiClipRect>(region, UiClipRect{});
    world.add_component<UiScroll>(region, UiScroll{0.0f, 3.0f});

    // A button in the lower part of the region's content: visible at offset 0, carried off
    // the top as the offset grows.
    const EntityID button = world.create_entity();
    mosaic::engine::ui::UiButton b;
    b.anchor = static_cast<int>(Anchor::TopLeft);
    b.pos    = {0.0f, 0.1f};
    b.size   = {1.0f, 0.2f};
    b.event  = "scrolled_button";
    world.add_component<mosaic::engine::ui::UiButton>(button, b);
    world.add_component<UiParent>(button, UiParent{region});

    auto is_candidate = [&] {
        for (const auto& c : mosaic::engine::ui::button_hit_candidates(world))
        {
            if (c.e == button) { return true; }
        }
        return false;
    };

    // Not vacuous: it really is clickable before the scroll. Without this the test would
    // pass just as well against a filter that dropped everything.
    REQUIRE(is_candidate());

    world.get_component<UiScroll>(region)->offset = 2.0f; // two container-heights up
    REQUIRE_FALSE(is_candidate());

    // And it comes back, so the filter is a function of position rather than a latch.
    world.get_component<UiScroll>(region)->offset = 0.0f;
    REQUIRE(is_candidate());
}
