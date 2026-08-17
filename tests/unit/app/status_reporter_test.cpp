#include <sstream>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "app/run/status_reporter.hpp"

using mosaic::app::JsonStatusReporter;
using mosaic::app::RunSummary;

TEST_CASE("JsonStatusReporter emits one progress JSON object per line", "[app]")
{
    std::ostringstream out;
    JsonStatusReporter reporter(out);

    reporter.progress(300, 5.0);

    auto parsed = nlohmann::json::parse(out.str());
    REQUIRE(parsed.at("event") == "progress");
    REQUIRE(parsed.at("tick") == 300);
    REQUIRE(parsed.at("elapsed_s") == 5.0);
}

TEST_CASE("JsonStatusReporter emits a summary object with all fields", "[app]")
{
    std::ostringstream out;
    JsonStatusReporter reporter(out);

    reporter.summary(RunSummary{600, 10.0, 1.0 / 60.0, "demo.json", "ticks"});

    auto parsed = nlohmann::json::parse(out.str());
    REQUIRE(parsed.at("event") == "summary");
    REQUIRE(parsed.at("ticks") == 600);
    REQUIRE(parsed.at("elapsed_s") == 10.0);
    REQUIRE(parsed.at("fixed_dt").get<double>() == Catch::Approx(1.0 / 60.0));
    REQUIRE(parsed.at("scene") == "demo.json");
    REQUIRE(parsed.at("stopped_by") == "ticks");
}

TEST_CASE("JsonStatusReporter emits an error object", "[app]")
{
    std::ostringstream out;
    JsonStatusReporter reporter(out);

    reporter.error("scene not found: demo.json");

    auto parsed = nlohmann::json::parse(out.str());
    REQUIRE(parsed.at("event") == "error");
    REQUIRE(parsed.at("message") == "scene not found: demo.json");
}
