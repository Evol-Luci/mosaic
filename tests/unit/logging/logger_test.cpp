#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "foundation/logging/log.hpp"

using mosaic::foundation::log::Level;
using mosaic::foundation::log::Logger;
using mosaic::foundation::log::Record;
using mosaic::foundation::log::Sink;

namespace
{
// Records every Record it receives, copying the string views into owned
// strings so the assertions can outlive the log call.
class CapturingSink : public Sink
{
public:
    struct Entry
    {
        Level       level;
        std::string category;
        std::string message;
    };

    std::vector<Entry> entries;

    void write(const Record& record) noexcept override
    {
        entries.push_back(
            {record.level, std::string(record.category), std::string(record.message)});
    }
};
} // namespace

TEST_CASE("messages at or above min-level route to the sink", "[logging]")
{
    Logger logger;
    auto sink = std::make_shared<CapturingSink>();
    logger.add_sink(sink);
    logger.set_min_level(Level::Trace);

    logger.log(Level::Trace, "ecs", "a");
    logger.log(Level::Info, "assets", "b");
    logger.log(Level::Error, "render", "c");

    REQUIRE(sink->entries.size() == 3);
    REQUIRE(sink->entries[0].level == Level::Trace);
    REQUIRE(sink->entries[0].category == "ecs");
    REQUIRE(sink->entries[0].message == "a");
    REQUIRE(sink->entries[1].category == "assets");
    REQUIRE(sink->entries[2].level == Level::Error);
}

TEST_CASE("min-level filter drops lower levels", "[logging]")
{
    Logger logger;
    auto sink = std::make_shared<CapturingSink>();
    logger.add_sink(sink);
    logger.set_min_level(Level::Warn);

    logger.log(Level::Trace, "c", "dropped");
    logger.log(Level::Debug, "c", "dropped");
    logger.log(Level::Info, "c", "dropped");
    logger.log(Level::Warn, "c", "kept");
    logger.log(Level::Error, "c", "kept");

    REQUIRE(sink->entries.size() == 2);
    REQUIRE(sink->entries[0].level == Level::Warn);
    REQUIRE(sink->entries[1].level == Level::Error);
}

TEST_CASE("every registered sink receives each record, in order", "[logging]")
{
    Logger logger;
    auto a = std::make_shared<CapturingSink>();
    auto b = std::make_shared<CapturingSink>();
    logger.add_sink(a);
    logger.add_sink(b);
    logger.set_min_level(Level::Info);

    logger.log(Level::Info, "x", "hello");

    REQUIRE(a->entries.size() == 1);
    REQUIRE(b->entries.size() == 1);
    REQUIRE(a->entries[0].message == "hello");
    REQUIRE(b->entries[0].message == "hello");
}

TEST_CASE("clear_sinks removes all sinks", "[logging]")
{
    Logger logger;
    auto sink = std::make_shared<CapturingSink>();
    logger.add_sink(sink);
    logger.clear_sinks();

    logger.log(Level::Error, "x", "after clear");

    REQUIRE(sink->entries.empty());
}

TEST_CASE("to_string renders each level tag", "[logging]")
{
    REQUIRE(mosaic::foundation::log::to_string(Level::Trace) == "TRACE");
    REQUIRE(mosaic::foundation::log::to_string(Level::Debug) == "DEBUG");
    REQUIRE(mosaic::foundation::log::to_string(Level::Info) == "INFO");
    REQUIRE(mosaic::foundation::log::to_string(Level::Warn) == "WARN");
    REQUIRE(mosaic::foundation::log::to_string(Level::Error) == "ERROR");
}
