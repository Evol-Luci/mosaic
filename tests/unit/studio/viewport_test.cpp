#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "studio/viewport.hpp"

using Catch::Approx;
using mosaic::studio::ViewportPanel;
using mosaic::studio::ViewportSize;

TEST_CASE("ViewportPanel starts wanting a resize with a 1x1 min size", "[studio][viewport]")
{
    ViewportPanel panel;
    REQUIRE(panel.size().width == 1u);
    REQUIRE(panel.size().height == 1u);
    REQUIRE(panel.needs_resize()); // nothing applied yet
    REQUIRE(panel.aspect() == Approx(1.0f));
}

TEST_CASE("ViewportPanel records measured size and detects change", "[studio][viewport]")
{
    ViewportPanel panel;
    panel.set_measured_size(800.0f, 600.0f);
    REQUIRE(panel.size().width == 800u);
    REQUIRE(panel.size().height == 600u);
    REQUIRE(panel.aspect() == Approx(800.0f / 600.0f));
    REQUIRE(panel.needs_resize());

    panel.mark_applied();
    REQUIRE_FALSE(panel.needs_resize());

    panel.set_measured_size(800.0f, 600.0f); // same
    REQUIRE_FALSE(panel.needs_resize());

    panel.set_measured_size(1024.0f, 768.0f); // changed
    REQUIRE(panel.needs_resize());
}

TEST_CASE("ViewportPanel clamps degenerate and fractional sizes", "[studio][viewport]")
{
    ViewportPanel panel;
    panel.set_measured_size(0.0f, 0.0f);
    REQUIRE(panel.size().width == 1u);
    REQUIRE(panel.size().height == 1u);

    panel.set_measured_size(-5.0f, 3.9f);
    REQUIRE(panel.size().width == 1u);   // negative -> 1
    REQUIRE(panel.size().height == 3u);  // truncated toward zero

    panel.set_measured_size(100.9f, 50.2f);
    REQUIRE(panel.size().width == 100u);
    REQUIRE(panel.size().height == 50u);
}
