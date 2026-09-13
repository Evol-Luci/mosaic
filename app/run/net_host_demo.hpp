#pragma once

#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>

#include "engine/net/net_replicated.hpp"
#include "engine/net/network_event.hpp"
#include "engine/render/material.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/logging/log.hpp"

namespace mosaic::app
{
// Host-only demo glue: spawns a NetReplicated avatar cube for each connecting client (id
// auto-assigned by N4b), moves it from that client's NetworkInput ("dx,dz" opaque payload —
// this is the one place that DECODES it; the engine itself never interprets NetworkInput),
// and despawns it on disconnect (N4's despawn then propagates the removal to every client).
// Pure World/event-bus logic — no CLI, no Transport — so it's headless-testable exactly like
// the N5/N6 integration tests.
inline void wire_networked_demo_host(mosaic::engine::World& world)
{
    namespace net = mosaic::engine::net;
    using mosaic::engine::render::Material;
    using mosaic::engine::transform::Transform;

    auto avatars = std::make_shared<
        std::unordered_map<std::uint32_t, mosaic::engine::World::EntityID>>();

    world.events().subscribe<net::NetworkEvent>(
        [&world, avatars](const net::NetworkEvent& e)
        {
            if (e.type == net::NetworkEvent::Type::PeerJoined)
            {
                const auto entity = world.create_entity();
                world.add_component<net::NetReplicated>(entity, net::NetReplicated{0});
                Transform t;
                t.position = {static_cast<float>(e.peer % 5) - 2.0f, 1.0f, -3.0f};
                world.add_component<Transform>(entity, t);
                Material m;
                m.base_color = {0.2f, 0.6f, 0.9f};
                world.add_component<Material>(entity, m);
                (*avatars)[e.peer] = entity;
                mosaic::foundation::log::info("net", "peer {} joined; spawned avatar", e.peer);
            }
            else if (e.type == net::NetworkEvent::Type::PeerLeft)
            {
                auto it = avatars->find(e.peer);
                if (it != avatars->end())
                {
                    world.destroy_entity(it->second);
                    avatars->erase(it);
                    mosaic::foundation::log::info("net", "peer {} left; despawned avatar", e.peer);
                }
            }
        });

    world.events().subscribe<net::NetworkInput>(
        [&world, avatars](const net::NetworkInput& e)
        {
            auto it = avatars->find(e.session);
            if (it == avatars->end()) { return; }
            float dx = 0.0f, dz = 0.0f;
            if (std::sscanf(e.payload.c_str(), "%f,%f", &dx, &dz) != 2) { return; }
            if (Transform* t = world.get_component<Transform>(it->second))
            {
                t->position.x += dx;
                t->position.z += dz;
            }
        });
}
} // namespace mosaic::app
