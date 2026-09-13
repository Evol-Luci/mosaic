#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "engine/ecs/system.hpp"
#include "engine/net/message.hpp"
#include "engine/net/network_event.hpp"
#include "engine/net/network_system.hpp"
#include "engine/world/world.hpp"

namespace mosaic::engine::net
{
// Client role: polls an injected provider each tick and sends each returned payload to the
// server as an Input frame. Server role: drains inbound Input frames and emits NetworkInput
// on the bus. Added to the World AFTER NetworkSystem so inbound frames are available each
// tick (server role) and the transport link to the server is established (client role).
class InputSystem : public mosaic::engine::ecs::System
{
public:
    InputSystem(Role role, NetworkSystem* net,
               std::function<std::vector<std::string>()> provider = nullptr)
        : role_(role), net_(net), provider_(std::move(provider)) {}

    void update(mosaic::engine::World& world, float /*dt*/) override
    {
        if (net_ == nullptr) { return; }
        if (role_ == Role::Client)
        {
            if (!provider_) { return; }
            for (const std::string& payload : provider_())
            {
                net_->send_to_server(Channel::Reliable, encode(MessageType::Input, payload));
            }
        }
        else
        {
            for (const InboundMessage& msg : net_->take_inbound())
            {
                if (msg.type == MessageType::Input)
                {
                    world.events().emit(NetworkInput{msg.peer, msg.payload});
                }
            }
        }
    }

private:
    Role role_;
    NetworkSystem* net_;
    std::function<std::vector<std::string>()> provider_;
};
} // namespace mosaic::engine::net
