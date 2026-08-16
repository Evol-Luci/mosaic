#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/ecs/system.hpp"
#include "engine/world/world.hpp"

using mosaic::engine::World;
using mosaic::engine::ecs::System;

namespace
{
struct Ping { int value; };

// Records its id into a shared vector each update, to observe run order.
class Recorder final : public System
{
public:
    Recorder(std::vector<int>& order, int id) : order_(order), id_(id) {}
    void update(World&, float) override { order_.push_back(id_); }

private:
    std::vector<int>& order_;
    int id_;
};

// Captures the dt it was ticked with.
class DtProbe final : public System
{
public:
    float last_dt = -1.0f;
    void update(World&, float dt) override { last_dt = dt; }
};

// Emits a deferred Ping during its update.
class Emitter final : public System
{
public:
    void update(World& world, float) override { world.events().emit(Ping{9}); }
};
} // namespace

TEST_CASE("systems run in registration order each tick", "[world]")
{
    World world;
    std::vector<int> order;
    world.add_system<Recorder>(order, 1);
    world.add_system<Recorder>(order, 2);
    world.add_system<Recorder>(order, 3);

    world.tick(0.016f);
    REQUIRE(order == std::vector<int>{1, 2, 3});
}

TEST_CASE("tick passes dt through to system update", "[world]")
{
    World world;
    DtProbe& probe = world.add_system<DtProbe>();
    world.tick(0.25f);
    REQUIRE(probe.last_dt == 0.25f);
}

TEST_CASE("tick drains deferred events at the start of the tick", "[world]")
{
    World world;
    std::vector<int> got;
    world.events().subscribe_deferred<Ping>([&](const Ping& p) { got.push_back(p.value); });

    world.events().emit(Ping{5});
    REQUIRE(got.empty());        // not delivered mid-emit

    world.tick(0.0f);
    REQUIRE(got == std::vector<int>{5});
}

TEST_CASE("events emitted during a tick surface on the next tick's drain", "[world]")
{
    World world;
    std::vector<int> got;
    world.events().subscribe_deferred<Ping>([&](const Ping& p) { got.push_back(p.value); });
    world.add_system<Emitter>();

    world.tick(0.0f);            // Emitter emits Ping{9}; drain ran first (empty)
    REQUIRE(got.empty());

    world.tick(0.0f);            // now the queued Ping{9} drains
    REQUIRE(got == std::vector<int>{9});
}
