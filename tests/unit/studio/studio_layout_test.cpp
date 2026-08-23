#include <catch2/catch_test_macros.hpp>

#include "studio/studio_layout.hpp"

using mosaic::studio::StudioLayout;

TEST_CASE("StudioLayout default has the three panels, all open", "[studio]")
{
    StudioLayout layout;
    REQUIRE(layout.panels().size() == 3);
    REQUIRE(layout.is_open("Hierarchy"));
    REQUIRE(layout.is_open("Inspector"));
    REQUIRE(layout.is_open("Console"));
}

TEST_CASE("StudioLayout toggle flips a panel's open state", "[studio]")
{
    StudioLayout layout;
    layout.toggle("Inspector");
    REQUIRE_FALSE(layout.is_open("Inspector"));
    layout.toggle("Inspector");
    REQUIRE(layout.is_open("Inspector"));
}

TEST_CASE("StudioLayout exit request defaults false and can be set", "[studio]")
{
    StudioLayout layout;
    REQUIRE_FALSE(layout.exit_requested());
    layout.request_exit();
    REQUIRE(layout.exit_requested());
}

TEST_CASE("StudioLayout is_open is false for an unknown panel", "[studio]")
{
    StudioLayout layout;
    REQUIRE_FALSE(layout.is_open("Nope"));
}
