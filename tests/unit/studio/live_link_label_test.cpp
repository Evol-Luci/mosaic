#include <string>

#include <catch2/catch_test_macros.hpp>

#include "studio/studio_app.hpp"

using mosaic::studio::live_link_label;
using mosaic::studio::LiveLinkState;

TEST_CASE("live_link_label describes each state", "[studio]")
{
    REQUIRE(std::string(live_link_label(LiveLinkState::Off)) == "Live Link: Off");
    REQUIRE(std::string(live_link_label(LiveLinkState::Listening)) == "Live Link: Listening");
    REQUIRE(std::string(live_link_label(LiveLinkState::Connected)) == "Live Link: Connected");
}
