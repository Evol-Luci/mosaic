#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "foundation/logging/log.hpp"

namespace mlog = mosaic::foundation::log;

namespace
{
class CapturingSink : public mlog::Sink
{
public:
    struct Entry
    {
        mlog::Level  level;
        std::string category;
        std::string message;
    };

    std::vector<Entry> entries;

    void write(const mlog::Record& record) noexcept override
    {
        entries.push_back(
            {record.level, std::string(record.category), std::string(record.message)});
    }
};

// Installs a fresh capturing sink on the global logger and resets the min-level
// so each test is isolated from the shared default logger's prior state.
std::shared_ptr<CapturingSink> reset_global(mlog::Level min = mlog::Level::Trace)
{
    mlog::clear_sinks();
    mlog::set_min_level(min);
    auto sink = std::make_shared<CapturingSink>();
    mlog::add_sink(sink);
    return sink;
}
} // namespace

TEST_CASE("global level functions forward to the default logger", "[logging]")
{
    auto sink = reset_global();

    mlog::trace("ecs", "t");
    mlog::debug("ecs", "d");
    mlog::info("ecs", "i");
    mlog::warn("ecs", "w");
    mlog::error("ecs", "e");

    REQUIRE(sink->entries.size() == 5);
    REQUIRE(sink->entries[0].level == mlog::Level::Trace);
    REQUIRE(sink->entries[2].level == mlog::Level::Info);
    REQUIRE(sink->entries[4].level == mlog::Level::Error);
    REQUIRE(sink->entries[0].category == "ecs");
}

TEST_CASE("global min-level filtering drops lower levels", "[logging]")
{
    auto sink = reset_global(mlog::Level::Warn);

    mlog::info("x", "dropped");
    mlog::warn("x", "kept");

    REQUIRE(sink->entries.size() == 1);
    REQUIRE(sink->entries[0].message == "kept");
}

TEST_CASE("format arguments substitute into the message", "[logging]")
{
    auto sink = reset_global();

    mlog::info("render", "frame {} took {} ms", 7, 42);

    REQUIRE(sink->entries.size() == 1);
    REQUIRE(sink->entries[0].message == "frame 7 took 42 ms");
}

TEST_CASE("ConsoleSink formats [LEVEL] category: message to cerr", "[logging]")
{
    // Redirect std::cerr's buffer to capture the console sink's output.
    std::ostringstream captured;
    std::streambuf* previous = std::cerr.rdbuf(captured.rdbuf());

    mlog::ConsoleSink sink;
    sink.write(mlog::Record{mlog::Level::Warn, "assets", "disk full"});

    std::cerr.rdbuf(previous);

    REQUIRE(captured.str() == "[WARN] assets: disk full\n");
}
