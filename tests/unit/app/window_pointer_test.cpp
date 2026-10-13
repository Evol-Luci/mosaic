#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/run/window_pointer.hpp"

using Catch::Approx;
using mosaic::app::window_pointer_state;

TEST_CASE("a cursor inside the window reports a normalized position", "[app][pointer]")
{
    const auto p = window_pointer_state(400.0, 300.0, 800, 600, false);
    REQUIRE(p.inside);
    REQUIRE(p.x == Approx(0.5f));
    REQUIRE(p.y == Approx(0.5f));
    REQUIRE_FALSE(p.down);
}

TEST_CASE("a cursor outside the window is not present", "[app][pointer]")
{
    // THE WHOLE POINT OF THIS SLICE. Until 2026-10-12 the standalone provider left `inside`
    // at its default true and merely clamped x/y, so a cursor on the desktop still read as a
    // pointer resting against the window edge -- which hovered an edge-anchored button, and
    // made drag_cancel unreachable outside the editor. The editor's provider has always
    // gated presence on the cursor being over its panel; this is the same rule.
    REQUIRE_FALSE(window_pointer_state(-5.0, 300.0, 800, 600, false).inside);
    REQUIRE_FALSE(window_pointer_state(400.0, -5.0, 800, 600, false).inside);
    REQUIRE_FALSE(window_pointer_state(801.0, 300.0, 800, 600, false).inside);
    REQUIRE_FALSE(window_pointer_state(400.0, 601.0, 800, 600, false).inside);
}

TEST_CASE("the window edges count as inside", "[app][pointer]")
{
    // Inclusive, matching topmost_hit's inclusive containment: a pointer exactly on the edge
    // is a pointer on the surface, and an edge-anchored button is hit there.
    REQUIRE(window_pointer_state(0.0, 0.0, 800, 600, false).inside);
    REQUIRE(window_pointer_state(800.0, 600.0, 800, 600, false).inside);
}

TEST_CASE("the button is only down while the cursor is inside", "[app][pointer]")
{
    // The rule that makes drag_cancel reachable in the standalone runtime. GLFW keeps
    // reporting a held button while the cursor is outside a focused window, so without this
    // a drag off the window edge ends as a drag_end carrying a CLAMPED coordinate -- a point
    // the user never pointed at. Dropping `down` with presence turns it into the cancel it
    // actually is.
    REQUIRE(window_pointer_state(400.0, 300.0, 800, 600, true).down);
    REQUIRE_FALSE(window_pointer_state(900.0, 300.0, 800, 600, true).down);
}

TEST_CASE("a position outside the window still clamps into range", "[app][pointer]")
{
    // x/y stay in [0,1] even when absent, so a consumer that reads them without checking
    // `inside` gets a sane number rather than one off the end of the screen. `inside` is what
    // says the position means anything.
    const auto p = window_pointer_state(-50.0, 900.0, 800, 600, false);
    REQUIRE(p.x == Approx(0.0f));
    REQUIRE(p.y == Approx(1.0f));
}

TEST_CASE("a degenerate window reports no pointer at all", "[app][pointer]")
{
    // Minimised, or queried before the window has a size. Returning presence here would
    // divide by zero on the way to a position.
    REQUIRE_FALSE(window_pointer_state(10.0, 10.0, 0, 600, true).inside);
    REQUIRE_FALSE(window_pointer_state(10.0, 10.0, 800, 0, true).inside);
    REQUIRE_FALSE(window_pointer_state(10.0, 10.0, 0, 600, true).down);
}
