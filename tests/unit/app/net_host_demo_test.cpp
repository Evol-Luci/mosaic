#include <catch2/catch_test_macros.hpp>

#include "app/run/net_host_demo.hpp"
#include "engine/net/net_replicated.hpp"
#include "engine/net/network_event.hpp"
#include "engine/render/material.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"

using mosaic::app::wire_networked_demo_host;
using mosaic::engine::World;
using mosaic::engine::net::NetReplicated;
using mosaic::engine::net::NetworkEvent;
using mosaic::engine::net::NetworkInput;
using mosaic::engine::transform::Transform;

TEST_CASE("PeerJoined spawns exactly one NetReplicated avatar", "[app][net]")
{
    World world;
    wire_networked_demo_host(world);

    int before = 0;
    world.for_each<NetReplicated>([&](World::EntityID, NetReplicated&) { ++before; });
    REQUIRE(before == 0);

    world.events().emit(NetworkEvent{NetworkEvent::Type::PeerJoined, 1u});

    int after = 0;
    world.for_each<NetReplicated>([&](World::EntityID, NetReplicated&) { ++after; });
    REQUIRE(after == 1);
}

TEST_CASE("NetworkInput moves the sender's avatar by the parsed delta", "[app][net]")
{
    World world;
    wire_networked_demo_host(world);
    world.events().emit(NetworkEvent{NetworkEvent::Type::PeerJoined, 1u});

    World::EntityID avatar;
    Transform before;
    world.for_each<NetReplicated>([&](World::EntityID e, NetReplicated&)
    {
        avatar = e;
        before = *world.get_component<Transform>(e);
    });

    world.events().emit(NetworkInput{1u, "0.5,-1.5"});

    const Transform* after = world.get_component<Transform>(avatar);
    REQUIRE(after->position.x == before.position.x + 0.5f);
    REQUIRE(after->position.z == before.position.z - 1.5f);
}

TEST_CASE("a malformed NetworkInput payload is silently ignored", "[app][net]")
{
    World world;
    wire_networked_demo_host(world);
    world.events().emit(NetworkEvent{NetworkEvent::Type::PeerJoined, 1u});

    World::EntityID avatar;
    Transform before;
    world.for_each<NetReplicated>([&](World::EntityID e, NetReplicated&)
    {
        avatar = e;
        before = *world.get_component<Transform>(e);
    });

    world.events().emit(NetworkInput{1u, "not-a-delta"});

    const Transform* after = world.get_component<Transform>(avatar);
    REQUIRE(after->position.x == before.position.x);
    REQUIRE(after->position.z == before.position.z);
}

TEST_CASE("PeerLeft despawns that session's avatar", "[app][net]")
{
    World world;
    wire_networked_demo_host(world);
    world.events().emit(NetworkEvent{NetworkEvent::Type::PeerJoined, 1u});

    int before = 0;
    world.for_each<NetReplicated>([&](World::EntityID, NetReplicated&) { ++before; });
    REQUIRE(before == 1);

    world.events().emit(NetworkEvent{NetworkEvent::Type::PeerLeft, 1u});
    world.tick(0.016f); // destroy_entity is deferred; commit it

    int after = 0;
    world.for_each<NetReplicated>([&](World::EntityID, NetReplicated&) { ++after; });
    REQUIRE(after == 0);
}

TEST_CASE("NetworkInput for a session with no avatar (e.g. after PeerLeft) is a no-op", "[app][net]")
{
    World world;
    wire_networked_demo_host(world);
    world.events().emit(NetworkEvent{NetworkEvent::Type::PeerJoined, 1u});
    world.events().emit(NetworkEvent{NetworkEvent::Type::PeerLeft, 1u});
    world.tick(0.016f);

    // No crash, and no entity springs into existence.
    world.events().emit(NetworkInput{1u, "1.0,1.0"});
    int count = 0;
    world.for_each<NetReplicated>([&](World::EntityID, NetReplicated&) { ++count; });
    REQUIRE(count == 0);
}
