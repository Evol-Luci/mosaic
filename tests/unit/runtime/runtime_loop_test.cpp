#include <catch2/catch_test_macros.hpp>

#include "engine/ecs/system.hpp"
#include "engine/runtime/runtime_loop.hpp"
#include "engine/world/world.hpp"

using mosaic::engine::World;
using mosaic::engine::runtime::RuntimeLoop;

namespace
{
// Records how many times it was updated and the last dt it received.
class TickCounter : public mosaic::engine::ecs::System
{
public:
    int updates = 0;
    float last_dt = -1.0f;
    void update(World&, float dt) override
    {
        ++updates;
        last_dt = dt;
    }
};
} // namespace

TEST_CASE("advance dispatches fixed ticks and passes fixed_dt", "[runtime]")
{
    World world;
    TickCounter& counter = world.add_system<TickCounter>();
    RuntimeLoop loop(0.25f);
    loop.start();

    REQUIRE(loop.advance(world, 1.0f) == 4); // 1.0 / 0.25
    REQUIRE(counter.updates == 4);
    REQUIRE(counter.last_dt == 0.25f);
    REQUIRE(loop.tick_count() == 4);
}

TEST_CASE("advance remainder carries to the next advance", "[runtime]")
{
    World world;
    world.add_system<TickCounter>();
    RuntimeLoop loop(0.25f);
    loop.start();

    REQUIRE(loop.advance(world, 0.1f) == 0); // below one step
    REQUIRE(loop.advance(world, 0.2f) == 1); // 0.1 + 0.2 >= 0.25
}

TEST_CASE("stop pauses and ignores fed time; start resumes", "[runtime]")
{
    World world;
    world.add_system<TickCounter>();
    RuntimeLoop loop(0.25f);
    loop.start();
    loop.stop();

    REQUIRE_FALSE(loop.is_running());
    REQUIRE(loop.advance(world, 1.0f) == 0);   // ignored while stopped
    REQUIRE(loop.accumulator() == 0.0f);       // not banked

    loop.start();
    REQUIRE(loop.advance(world, 0.25f) == 1);
}

TEST_CASE("step runs exactly one tick regardless of state", "[runtime]")
{
    World world;
    TickCounter& counter = world.add_system<TickCounter>();
    RuntimeLoop loop(0.25f); // never started

    REQUIRE(loop.step(world) == 1);
    REQUIRE(counter.updates == 1);
    REQUIRE(loop.tick_count() == 1);
    REQUIRE_FALSE(loop.is_running());
}

TEST_CASE("same fixed_dt and dt schedule yield equal tick counts", "[runtime]")
{
    World a_world;
    a_world.add_system<TickCounter>();
    World b_world;
    b_world.add_system<TickCounter>();
    RuntimeLoop a(0.02f);
    RuntimeLoop b(0.02f);
    a.start();
    b.start();

    const float schedule[] = {0.033f, 0.017f, 0.05f, 0.001f, 0.1f};
    for (float dt : schedule)
    {
        a.advance(a_world, dt);
        b.advance(b_world, dt);
    }
    REQUIRE(a.tick_count() == b.tick_count());
}

TEST_CASE("advance is capped by max_steps_per_advance", "[runtime]")
{
    World world;
    world.add_system<TickCounter>();
    RuntimeLoop loop(0.01f, 4);
    loop.start();

    REQUIRE(loop.advance(world, 100.0f) == 4); // huge dt clamped to the cap
    REQUIRE(loop.tick_count() == 4);
}
