#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "foundation/logging/log.hpp"

namespace log = mosaic::foundation::log;

namespace
{
class CapturingSink : public log::Sink
{
public:
    struct Entry
    {
        log::Level  level;
        std::string category;
        std::string message;
    };

    std::vector<Entry> entries;

    void write(const log::Record& record) noexcept override
    {
        entries.push_back(
            {record.level, std::string(record.category), std::string(record.message)});
    }
};

// Installs a fresh capturing sink on the global logger and resets the min-level
// so each test is isolated from the shared default logger's prior state.
std::shared_ptr<CapturingSink> reset_global(log::Level min = log::Level::Trace)
{
    log::clear_sinks();
    log::set_min_level(min);
    auto sink = std::make_shared<CapturingSink>();
    log::add_sink(sink);
    return sink;
}
} // namespace

TEST_CASE("global level functions forward to the default logger", "[logging]")
{
    auto sink = reset_global();

    log::trace("ecs", "t");
    log::debug("ecs", "d");
    log::info("ecs", "i");
    log::warn("ecs", "w");
    log::error("ecs", "e");

    REQUIRE(sink->entries.size() == 5);
    REQUIRE(sink->entries[0].level == log::Level::Trace);
    REQUIRE(sink->entries[2].level == log::Level::Info);
    REQUIRE(sink->entries[4].level == log::Level::Error);
    REQUIRE(sink->entries[0].category == "ecs");
}

TEST_CASE("global min-level filtering drops lower levels", "[logging]")
{
    auto sink = reset_global(log::Level::Warn);

    log::info("x", "dropped");
    log::warn("x", "kept");

    REQUIRE(sink->entries.size() == 1);
    REQUIRE(sink->entries[0].message == "kept");
}

TEST_CASE("format arguments substitute into the message", "[logging]")
{
    auto sink = reset_global();

    log::info("render", "frame {} took {} ms", 7, 42);

    REQUIRE(sink->entries.size() == 1);
    REQUIRE(sink->entries[0].message == "frame 7 took 42 ms");
}

TEST_CASE("ConsoleSink formats [LEVEL] category: message to cerr", "[logging]")
{
    // Redirect std::cerr's buffer to capture the console sink's output.
    std::ostringstream captured;
    std::streambuf* previous = std::cerr.rdbuf(captured.rdbuf());

    log::ConsoleSink sink;
    sink.write(log::Record{log::Level::Warn, "assets", "disk full"});

    std::cerr.rdbuf(previous);

    REQUIRE(captured.str() == "[WARN] assets: disk full\n");
}
