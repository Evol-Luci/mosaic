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

TEST_CASE("fixed-hz zero is rejected with a non-zero exit code", "[cli]")
{
    auto result = parse_args({"mosaic", "run", "--fixed-hz", "0"});
    REQUIRE(result.exit_code.has_value());
    REQUIRE(*result.exit_code != 0);
}

TEST_CASE("fps zero is rejected with a non-zero exit code", "[cli]")
{
    auto result = parse_args({"mosaic", "run", "--fps", "0"});
    REQUIRE(result.exit_code.has_value());
    REQUIRE(*result.exit_code != 0);
}

TEST_CASE("parse selects the run command by default subcommand", "[cli]")
{
    auto pr = parse_args({"mosaic", "run"});
    REQUIRE_FALSE(pr.exit_code.has_value());
    REQUIRE(pr.options.command == mosaic::app::Command::Run);
}

TEST_CASE("parse selects the editor command", "[cli]")
{
    auto pr = parse_args({"mosaic", "editor"});
    REQUIRE_FALSE(pr.exit_code.has_value());
    REQUIRE(pr.options.command == mosaic::app::Command::Editor);
}

TEST_CASE("editor accepts a bounded --duration", "[cli]")
{
    auto pr = parse_args({"mosaic", "editor", "--duration", "1"});
    REQUIRE_FALSE(pr.exit_code.has_value());
    REQUIRE(pr.options.command == mosaic::app::Command::Editor);
    REQUIRE(pr.options.duration_s.has_value());
    REQUIRE(pr.options.duration_s.value() == 1.0);
}

TEST_CASE("editor accepts a bounded --ticks", "[cli]")
{
    auto pr = parse_args({"mosaic", "editor", "--ticks", "42"});
    REQUIRE_FALSE(pr.exit_code.has_value());
    REQUIRE(pr.options.command == mosaic::app::Command::Editor);
    REQUIRE(pr.options.ticks.has_value());
    REQUIRE(pr.options.ticks.value() == std::uint64_t{42});
}

TEST_CASE("editor accepts --scene", "[cli]")
{
    auto pr = parse_args({"mosaic", "editor", "--scene", "level.json"});
    REQUIRE_FALSE(pr.exit_code.has_value());
    REQUIRE(pr.options.command == mosaic::app::Command::Editor);
    REQUIRE(pr.options.scene.has_value());
    REQUIRE(*pr.options.scene == "level.json");
}

TEST_CASE("create subcommand captures the project name", "[cli]")
{
    auto pr = parse_args({"mosaic", "create", "mygame"});
    REQUIRE_FALSE(pr.exit_code.has_value());
    REQUIRE(pr.options.command == mosaic::app::Command::Create);
    REQUIRE(pr.options.create_path.has_value());
    REQUIRE(*pr.options.create_path == "mygame");
}

TEST_CASE("editor accepts a positional project/scene path", "[cli]")
{
    auto pr = parse_args({"mosaic", "editor", "mygame"});
    REQUIRE_FALSE(pr.exit_code.has_value());
    REQUIRE(pr.options.command == mosaic::app::Command::Editor);
    REQUIRE(pr.options.open_path.has_value());
    REQUIRE(*pr.options.open_path == "mygame");
}

TEST_CASE("run accepts a positional path", "[cli]")
{
    auto pr = parse_args({"mosaic", "run", "level.json"});
    REQUIRE_FALSE(pr.exit_code.has_value());
    REQUIRE(pr.options.open_path.has_value());
    REQUIRE(*pr.options.open_path == "level.json");
}

TEST_CASE("a positional path together with --scene is rejected", "[cli]")
{
    auto pr = parse_args({"mosaic", "editor", "mygame", "--scene", "x.json"});
    REQUIRE(pr.exit_code.has_value());
    REQUIRE(*pr.exit_code != 0);
}

TEST_CASE("a run positional path together with --scene is rejected", "[cli]")
{
    auto pr = parse_args({"mosaic", "run", "level.json", "--scene", "x.json"});
    REQUIRE(pr.exit_code.has_value());
    REQUIRE(*pr.exit_code != 0);
}
