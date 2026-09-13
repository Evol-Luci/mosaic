#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/net/loopback_transport.hpp"
#include "engine/net/network_system.hpp"
#include "engine/net/net_replicated.hpp"
#include "engine/net/replication_system.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/render/material.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"

using mosaic::engine::World;
using mosaic::engine::net::make_loopback_pair;
using mosaic::engine::net::NetworkSystem;
using mosaic::engine::net::NetReplicated;
using mosaic::engine::net::ReplicationSystem;
using mosaic::engine::net::Role;
using mosaic::engine::render::Material;
using mosaic::engine::transform::Transform;
namespace refl = mosaic::engine::reflection;

TEST_CASE("client World mirrors the server over the session", "[net][replication]")
{
    // Shared component reflection (type metadata only — safe across both Worlds).
    mosaic::foundation::reflection::TypeRegistry registry;
    refl::ComponentReflection types;
    refl::register_builtin_components(registry, types);

    auto [server_tx, client_tx] = make_loopback_pair();

    World server_world;
    auto& server_net = server_world.add_system<NetworkSystem>(Role::Server, server_tx);
    server_world.add_system<ReplicationSystem>(Role::Server, &server_net, &types);

    World client_world;
    auto& client_net = client_world.add_system<NetworkSystem>(Role::Client, client_tx);
    client_world.add_system<ReplicationSystem>(Role::Client, &client_net, &types);

    // Server scene: one replicated entity + one unmarked entity.
    const auto se = server_world.create_entity();
    server_world.add_component<NetReplicated>(se, NetReplicated{1});
    Transform st; st.position = {1.0f, 2.0f, 3.0f};
    server_world.add_component<Transform>(se, st);
    Material sm; sm.base_color = {0.25f, 0.5f, 0.75f};
    server_world.add_component<Material>(se, sm);

    const auto unmarked = server_world.create_entity();
    server_world.add_component<Transform>(unmarked, Transform{});

    auto pump = [&]() {
        for (int i = 0; i < 8; ++i) { client_world.tick(0.016f); server_world.tick(0.016f); }
    };

    pump(); // handshake + first snapshot round-trip

    // Client mirrors exactly the one replicated entity.
    int count = 0;
    World::EntityID only;
    client_world.for_each_entity([&](World::EntityID e) { ++count; only = e; });
    REQUIRE(count == 1);

    const Transform* ct = client_world.get_component<Transform>(only);
    REQUIRE(ct != nullptr);
    REQUIRE(ct->position.x == 1.0f);
    REQUIRE(ct->position.y == 2.0f);
    REQUIRE(ct->position.z == 3.0f);
    const Material* cm = client_world.get_component<Material>(only);
    REQUIRE(cm != nullptr);
    REQUIRE(cm->base_color.x == 0.25f);
    REQUIRE_FALSE(client_world.has_component<NetReplicated>(only));

    // Move the server entity; the client updates in place, still exactly one entity.
    server_world.get_component<Transform>(se)->position = {5.0f, 6.0f, 7.0f};
    pump();

    count = 0;
    client_world.for_each_entity([&](World::EntityID e) { ++count; only = e; });
    REQUIRE(count == 1); // no duplicate from repeated per-tick snapshots
    REQUIRE(client_world.get_component<Transform>(only)->position.x == 5.0f);
}

TEST_CASE("client despawns an entity removed on the server", "[net][replication]")
{
    mosaic::foundation::reflection::TypeRegistry registry;
    refl::ComponentReflection types;
    refl::register_builtin_components(registry, types);

    auto [server_tx, client_tx] = make_loopback_pair();

    World server_world;
    auto& server_net = server_world.add_system<NetworkSystem>(Role::Server, server_tx);
    server_world.add_system<ReplicationSystem>(Role::Server, &server_net, &types);

    World client_world;
    auto& client_net = client_world.add_system<NetworkSystem>(Role::Client, client_tx);
    client_world.add_system<ReplicationSystem>(Role::Client, &client_net, &types);

    // Two replicated entities on the server.
    const auto a = server_world.create_entity();
    server_world.add_component<NetReplicated>(a, NetReplicated{1});
    Transform ta; ta.position = {1.0f, 0.0f, 0.0f};
    server_world.add_component<Transform>(a, ta);
    const auto b = server_world.create_entity();
    server_world.add_component<NetReplicated>(b, NetReplicated{2});
    server_world.add_component<Transform>(b, Transform{});

    auto pump = [&]() {
        for (int i = 0; i < 8; ++i) { client_world.tick(0.016f); server_world.tick(0.016f); }
    };

    pump();
    int count = 0;
    client_world.for_each_entity([&](World::EntityID) { ++count; });
    REQUIRE(count == 2); // client mirrors both

    // Remove B on the server; the client should drop exactly it.
    server_world.destroy_entity(b);
    pump();

    count = 0;
    World::EntityID only;
    client_world.for_each_entity([&](World::EntityID e) { ++count; only = e; });
    REQUIRE(count == 1);
    REQUIRE(client_world.get_component<Transform>(only)->position.x == 1.0f); // net 1 survived
}

TEST_CASE("server assigns a NetId for a zero-id entity and the client receives it", "[net][replication]")
{
    mosaic::foundation::reflection::TypeRegistry registry;
    refl::ComponentReflection types;
    refl::register_builtin_components(registry, types);

    auto [server_tx, client_tx] = make_loopback_pair();

    World server_world;
    auto& server_net = server_world.add_system<NetworkSystem>(Role::Server, server_tx);
    server_world.add_system<ReplicationSystem>(Role::Server, &server_net, &types);

    World client_world;
    auto& client_net = client_world.add_system<NetworkSystem>(Role::Client, client_tx);
    client_world.add_system<ReplicationSystem>(Role::Client, &client_net, &types);

    // Server entity created with id == 0 — no hand-picked id.
    const auto se = server_world.create_entity();
    server_world.add_component<NetReplicated>(se, NetReplicated{0});
    Transform st; st.position = {9.0f, 0.0f, 0.0f};
    server_world.add_component<Transform>(se, st);

    for (int i = 0; i < 8; ++i) { client_world.tick(0.016f); server_world.tick(0.016f); }

    // The server assigned a nonzero id in place.
    const std::uint32_t assigned = server_world.get_component<NetReplicated>(se)->id;
    REQUIRE(assigned != 0u);

    // The client mirrors exactly one entity, matching the server's Transform.
    int count = 0;
    World::EntityID only;
    client_world.for_each_entity([&](World::EntityID e) { ++count; only = e; });
    REQUIRE(count == 1);
    REQUIRE(client_world.get_component<Transform>(only)->position.x == 9.0f);
}
