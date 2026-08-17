#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "app/cli/options.hpp"

using mosaic::app::Options;
using mosaic::app::parse;

namespace
{
// Builds an argv from a vector of args (argv[0] is the program name) and calls
// parse. The char pointers are valid for the duration of the parse call.
mosaic::app::ParseResult parse_args(std::vector<std::string> args)
{
    std::vector<char*> argv;
    for (auto& a : args)
    {
        argv.push_back(a.data());
    }
    return parse(static_cast<int>(argv.size()), argv.data());
}
} // namespace

TEST_CASE("run subcommand with no flags yields defaults", "[cli]")
{
    auto result = parse_args({"mosaic", "run"});
    REQUIRE_FALSE(result.exit_code.has_value());
    REQUIRE_FALSE(result.options.scene.has_value());
    REQUIRE_FALSE(result.options.duration_s.has_value());
    REQUIRE_FALSE(result.options.ticks.has_value());
    REQUIRE(result.options.json == false);
    REQUIRE(result.options.fps == 60.0);
    REQUIRE(result.options.fixed_hz == 60.0);
}

TEST_CASE("run flags populate the Options struct", "[cli]")
{
    auto result = parse_args(
        {"mosaic", "run", "--scene", "demo.json", "--duration", "2.5",
         "--ticks", "120", "--fps", "30", "--fixed-hz", "50", "--json"});
    REQUIRE_FALSE(result.exit_code.has_value());
    REQUIRE(result.options.scene == "demo.json");
    REQUIRE(result.options.duration_s == 2.5);
    REQUIRE(result.options.ticks == std::uint64_t{120});
    REQUIRE(result.options.fps == 30.0);
    REQUIRE(result.options.fixed_hz == 50.0);
    REQUIRE(result.options.json == true);
}

TEST_CASE("an unknown flag sets a non-zero exit code", "[cli]")
{
    auto result = parse_args({"mosaic", "run", "--bogus"});
    REQUIRE(result.exit_code.has_value());
    REQUIRE(*result.exit_code != 0);
}

TEST_CASE("missing subcommand sets a non-zero exit code", "[cli]")
{
    auto result = parse_args({"mosaic"});
    REQUIRE(result.exit_code.has_value());
    REQUIRE(*result.exit_code != 0);
}
