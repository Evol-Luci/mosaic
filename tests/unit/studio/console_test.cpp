#include <memory>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "foundation/logging/log.hpp"
#include "studio/console.hpp"

using mosaic::foundation::log::Level;
using mosaic::foundation::log::Logger;
using mosaic::foundation::log::Record;
using mosaic::studio::ConsoleBuffer;
using mosaic::studio::ConsoleLine;
using mosaic::studio::ConsoleSink;

TEST_CASE("ConsoleSink captures log records into the buffer", "[studio][console]")
{
    auto   buffer = std::make_shared<ConsoleBuffer>();
    Logger logger;
    logger.add_sink(std::make_shared<ConsoleSink>(buffer));

    logger.log(Level::Warn, "vulkan", "validation blip");

    const auto snap = buffer->snapshot();
    REQUIRE(snap.size() == 1);
    REQUIRE(snap[0].level == Level::Warn);
    REQUIRE(snap[0].category == "vulkan");
    REQUIRE(snap[0].message == "validation blip");
}

TEST_CASE("ConsoleBuffer copies transient views so they outlive the record", "[studio][console]")
{
    ConsoleBuffer buffer;
    {
        // category/message live only in this scope; push must copy them.
        std::string category = "scope";
        std::string message  = "ephemeral";
        buffer.push(Record{Level::Info, category, message});
    }
    const auto snap = buffer.snapshot();
    REQUIRE(snap.size() == 1);
    REQUIRE(snap[0].category == "scope");
    REQUIRE(snap[0].message == "ephemeral");
}

TEST_CASE("ConsoleBuffer caps capacity, dropping the oldest", "[studio][console]")
{
    ConsoleBuffer buffer(3);
    for (int i = 0; i < 5; ++i)
    {
        buffer.push(Record{Level::Info, "c", std::to_string(i)});
    }
    const auto snap = buffer.snapshot();
    REQUIRE(snap.size() == 3);
    REQUIRE(snap[0].message == "2"); // oldest kept
    REQUIRE(snap[1].message == "3");
    REQUIRE(snap[2].message == "4"); // newest

    buffer.clear();
    REQUIRE(buffer.size() == 0);
}
