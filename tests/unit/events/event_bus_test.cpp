#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "foundation/events/event_bus.hpp"

using mosaic::foundation::events::EventBus;

namespace
{
struct Ping { int value; };
struct Pong { std::string label; };
} // namespace

TEST_CASE("synchronous subscribers fire in-tick before emit returns", "[events]")
{
    EventBus bus;
    bool fired_during_emit = false;
    int seen = 0;

    bus.subscribe<Ping>([&](const Ping& p) {
        fired_during_emit = true;
        seen = p.value;
    });

    REQUIRE_FALSE(fired_during_emit);
    bus.emit(Ping{42});
    // The handler must have run before emit returned.
    REQUIRE(fired_during_emit);
    REQUIRE(seen == 42);
}

TEST_CASE("deferred subscribers do not fire mid-tick; they fire on the drain", "[events]")
{
    EventBus bus;
    std::vector<int> delivered;

    bus.subscribe_deferred<Ping>([&](const Ping& p) { delivered.push_back(p.value); });

    bus.emit(Ping{1});
    bus.emit(Ping{2});

    // Nothing delivered until the next-tick drain.
    REQUIRE(delivered.empty());

    bus.drain_deferred();

    // Delivered FIFO in emit order.
    REQUIRE(delivered == std::vector<int>{1, 2});
}

TEST_CASE("deferred delivery is one tick after emit; the drain clears the buffer", "[events]")
{
    EventBus bus;
    int count = 0;

    bus.subscribe_deferred<Ping>([&](const Ping&) { ++count; });

    bus.emit(Ping{7});
    bus.drain_deferred();
    REQUIRE(count == 1);

    // A second drain with no new emits delivers nothing (buffer was cleared).
    bus.drain_deferred();
    REQUIRE(count == 1);
}

TEST_CASE("deferred events across types preserve global FIFO emit order", "[events]")
{
    EventBus bus;
    std::vector<std::string> order;

    bus.subscribe_deferred<Ping>([&](const Ping& p) { order.push_back("ping:" + std::to_string(p.value)); });
    bus.subscribe_deferred<Pong>([&](const Pong& p) { order.push_back("pong:" + p.label); });

    bus.emit(Ping{1});
    bus.emit(Pong{"a"});
    bus.emit(Ping{2});

    bus.drain_deferred();

    REQUIRE(order == std::vector<std::string>{"ping:1", "pong:a", "ping:2"});
}

TEST_CASE("synchronous and deferred subscribers to one type are independent", "[events]")
{
    EventBus bus;
    int sync_hits = 0;
    int deferred_hits = 0;

    bus.subscribe<Ping>([&](const Ping&) { ++sync_hits; });
    bus.subscribe_deferred<Ping>([&](const Ping&) { ++deferred_hits; });

    bus.emit(Ping{1});
    // Sync fired in-tick; deferred is still pending.
    REQUIRE(sync_hits == 1);
    REQUIRE(deferred_hits == 0);

    bus.drain_deferred();
    REQUIRE(sync_hits == 1);
    REQUIRE(deferred_hits == 1);
}

TEST_CASE("a deferred handler that emits during the drain defers to the next tick", "[events]")
{
    EventBus bus;
    std::vector<int> delivered;

    bus.subscribe_deferred<Ping>([&](const Ping& p) {
        delivered.push_back(p.value);
        // Re-emitting during the drain must not deliver within this same drain.
        if (p.value == 1)
        {
            bus.emit(Ping{2});
        }
    });

    bus.emit(Ping{1});
    bus.drain_deferred();
    // Only the first tick's event delivered; the re-emit is queued for next tick.
    REQUIRE(delivered == std::vector<int>{1});

    bus.drain_deferred();
    REQUIRE(delivered == std::vector<int>{1, 2});
}
