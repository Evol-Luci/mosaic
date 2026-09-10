#pragma once

#include <string>
#include <vector>

#include "engine/ecs/system.hpp"
#include "engine/net/message.hpp"
#include "engine/net/network_system.hpp"
#include "engine/net/replication.hpp"
#include "engine/net/transport.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/world/world.hpp"

namespace mosaic::engine::net
{
// Wires N3 replication over the N2 session. Server: broadcast a full snapshot each tick
// (when clients are connected). Client: apply received snapshots into the local World.
// Added to the World AFTER NetworkSystem so inbound frames are available each tick.
class ReplicationSystem : public mosaic::engine::ecs::System
{
public:
    ReplicationSystem(Role role, NetworkSystem* net,
                      const mosaic::engine::reflection::ComponentReflection* types)
        : role_(role), net_(net), types_(types) {}

    void update(mosaic::engine::World& world, float /*dt*/) override
    {
        if (net_ == nullptr || types_ == nullptr) { return; }
        if (role_ == Role::Server)
        {
            if (net_->clients().empty()) { return; } // nothing to serialize/send
            net_->broadcast(Channel::Reliable,
                            encode(MessageType::Snapshot, snapshot(world, *types_)));
        }
        else
        {
            for (const InboundMessage& msg : net_->take_inbound())
            {
                if (msg.type == MessageType::Snapshot)
                {
                    apply_snapshot(msg.payload, world, *types_, ids_);
                }
            }
        }
    }

private:
    Role role_;
    NetworkSystem* net_;
    const mosaic::engine::reflection::ComponentReflection* types_;
    NetIdMap ids_; // client-held stable NetId -> local EntityID map
};
} // namespace mosaic::engine::net
