#include <catch2/catch_test_macros.hpp>

#include "engine/net/net_replicated.hpp"
#include "engine/net/replication.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/render/material.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"

using mosaic::engine::World;
using mosaic::engine::net::apply_snapshot;
using mosaic::engine::net::NetIdMap;
using mosaic::engine::net::NetReplicated;
using mosaic::engine::net::snapshot;
using mosaic::engine::render::Material;
using mosaic::engine::transform::Transform;
namespace refl = mosaic::engine::reflection;

namespace
{
// Build a fresh TypeRegistry + ComponentReflection with all builtins registered.
struct Types
{
    mosaic::foundation::reflection::TypeRegistry registry;
    refl::ComponentReflection types;
    Types() { refl::register_builtin_components(registry, types); }
};
} // namespace

TEST_CASE("single NetReplicated entity replicates to a fresh client", "[net][replication]")
{
    Types t;

    World server;
    const auto se = server.create_entity();
    server.add_component<NetReplicated>(se, NetReplicated{1});
    Transform st;
    st.position = {1.0f, 2.0f, 3.0f};
    server.add_component<Transform>(se, st);
    Material sm;
    sm.base_color = {0.25f, 0.5f, 0.75f};
    server.add_component<Material>(se, sm);

    const std::string blob = snapshot(server, t.types);

    World client;
    NetIdMap ids;
    REQUIRE(apply_snapshot(blob, client, t.types, ids));

    // Exactly one client entity, matching Transform + Material, no NetReplicated.
    int count = 0;
    World::EntityID only;
    client.for_each_entity([&](World::EntityID e) { ++count; only = e; });
    REQUIRE(count == 1);

    const Transform* ct = client.get_component<Transform>(only);
    REQUIRE(ct != nullptr);
    REQUIRE(ct->position.x == 1.0f);
    REQUIRE(ct->position.y == 2.0f);
    REQUIRE(ct->position.z == 3.0f);

    const Material* cm = client.get_component<Material>(only);
    REQUIRE(cm != nullptr);
    REQUIRE(cm->base_color.x == 0.25f);

    REQUIRE_FALSE(client.has_component<NetReplicated>(only));
}

TEST_CASE("apply_snapshot is idempotent and upserts in place", "[net][replication]")
{
    Types t;

    World server;
    const auto se = server.create_entity();
    server.add_component<NetReplicated>(se, NetReplicated{7});
    Transform st;
    st.position = {0.0f, 0.0f, 0.0f};
    server.add_component<Transform>(se, st);

    World client;
    NetIdMap ids;

    const std::string blob1 = snapshot(server, t.types);
    REQUIRE(apply_snapshot(blob1, client, t.types, ids));
    // Re-apply the same blob: still one entity.
    REQUIRE(apply_snapshot(blob1, client, t.types, ids));

    int count = 0;
    World::EntityID only;
    client.for_each_entity([&](World::EntityID e) { ++count; only = e; });
    REQUIRE(count == 1);

    // Move the server entity, re-snapshot, re-apply: same entity updates.
    server.get_component<Transform>(se)->position = {5.0f, 6.0f, 7.0f};
    const std::string blob2 = snapshot(server, t.types);
    REQUIRE(apply_snapshot(blob2, client, t.types, ids));

    count = 0;
    client.for_each_entity([&](World::EntityID e) { ++count; only = e; });
    REQUIRE(count == 1); // still one — map hit, no duplicate
    REQUIRE(client.get_component<Transform>(only)->position.x == 5.0f);
}

TEST_CASE("only NetReplicated entities are replicated", "[net][replication]")
{
    Types t;

    World server;
    const auto a = server.create_entity();
    server.add_component<NetReplicated>(a, NetReplicated{1});
    server.add_component<Transform>(a, Transform{});
    const auto b = server.create_entity();
    server.add_component<NetReplicated>(b, NetReplicated{2});
    server.add_component<Transform>(b, Transform{});
    // An unmarked entity — must NOT be replicated.
    const auto c = server.create_entity();
    server.add_component<Transform>(c, Transform{});

    const std::string blob = snapshot(server, t.types);

    World client;
    NetIdMap ids;
    REQUIRE(apply_snapshot(blob, client, t.types, ids));

    int count = 0;
    client.for_each_entity([&](World::EntityID) { ++count; });
    REQUIRE(count == 2);
    REQUIRE(ids.map.size() == 2u);
}

TEST_CASE("apply_snapshot rejects malformed input without throwing", "[net][replication]")
{
    Types t;
    World client;
    NetIdMap ids;

    REQUIRE_FALSE(apply_snapshot("not json", client, t.types, ids));
    REQUIRE_FALSE(apply_snapshot("{}", client, t.types, ids)); // no "entities"

    // A snapshot of an empty server world applies cleanly (zero entities).
    World empty_server;
    const std::string blob = snapshot(empty_server, t.types);
    REQUIRE(apply_snapshot(blob, client, t.types, ids));
    int count = 0;
    client.for_each_entity([&](World::EntityID) { ++count; });
    REQUIRE(count == 0);
}

TEST_CASE("absent NetId is despawned on the next snapshot", "[net][replication]")
{
    Types t;

    // Server with two replicated entities.
    World server;
    const auto a = server.create_entity();
    server.add_component<NetReplicated>(a, NetReplicated{1});
    Transform ta; ta.position = {1.0f, 0.0f, 0.0f};
    server.add_component<Transform>(a, ta);
    const auto b = server.create_entity();
    server.add_component<NetReplicated>(b, NetReplicated{2});
    server.add_component<Transform>(b, Transform{});

    World client;
    NetIdMap ids;
    REQUIRE(apply_snapshot(snapshot(server, t.types), client, t.types, ids));
    REQUIRE(ids.map.size() == 2u);

    // Server removes entity B; its destruction commits on the server tick.
    server.destroy_entity(b);
    server.tick(0.016f);

    // The new snapshot has only net 1. Applying it despawns net 2 on the client.
    REQUIRE(apply_snapshot(snapshot(server, t.types), client, t.types, ids));
    REQUIRE(ids.map.size() == 1u);       // map updated immediately (erase is not deferred)
    REQUIRE(ids.map.count(1u) == 1u);

    // destroy_entity is deferred: before a client tick, the entity is still present.
    int before = 0;
    client.for_each_entity([&](World::EntityID) { ++before; });
    REQUIRE(before == 2);

    // Committing the deferred destruction leaves exactly the net-1 entity.
    client.tick(0.016f);
    int after = 0;
    World::EntityID only;
    client.for_each_entity([&](World::EntityID e) { ++after; only = e; });
    REQUIRE(after == 1);
    REQUIRE(client.get_component<Transform>(only)->position.x == 1.0f); // net 1 survived
}

TEST_CASE("re-applying the same snapshot removes nothing", "[net][replication]")
{
    Types t;
    World server;
    const auto a = server.create_entity();
    server.add_component<NetReplicated>(a, NetReplicated{1});
    server.add_component<Transform>(a, Transform{});
    const auto b = server.create_entity();
    server.add_component<NetReplicated>(b, NetReplicated{2});
    server.add_component<Transform>(b, Transform{});

    World client;
    NetIdMap ids;
    const std::string blob = snapshot(server, t.types);
    REQUIRE(apply_snapshot(blob, client, t.types, ids));
    REQUIRE(apply_snapshot(blob, client, t.types, ids)); // re-apply the identical snapshot
    REQUIRE(ids.map.size() == 2u);

    client.tick(0.016f);
    int count = 0;
    client.for_each_entity([&](World::EntityID) { ++count; });
    REQUIRE(count == 2); // nothing despawned
}
