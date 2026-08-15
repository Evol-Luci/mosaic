#include <catch2/catch_test_macros.hpp>

TEST_CASE("build harness runs", "[bootstrap]")
{
    REQUIRE(1 + 1 == 2);
}
