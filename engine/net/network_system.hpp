#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine/ecs/system.hpp"
#include "engine/net/message.hpp"
#include "engine/net/network_event.hpp"
#include "engine/net/transport.hpp"
#include "engine/world/world.hpp"

namespace mosaic::engine::net
{
enum class Role { Server, Client };

// Drives the connection lifecycle over an injected Transport. Server: assigns a monotonic
// session id per client on Hello and replies Welcome; Client: sends Hello on connect and
// adopts the id from Welcome. Emits NetworkEvent on the World bus. Writes no ECS state.
class NetworkSystem : public mosaic::engine::ecs::System
{
public:
    NetworkSystem(Role role, std::shared_ptr<Transport> transport)
        : role_(role), transport_(std::move(transport)) {}

    void update(mosaic::engine::World& world, float /*dt*/) override
    {
        if (!transport_) { return; }
        for (const NetEvent& ev : transport_->poll())
        {
            if (role_ == Role::Server) { handle_server(world, ev); }
            else                       { handle_client(world, ev); }
        }
    }

    Role role() const { return role_; }
    bool is_connected() const { return connected_; }          // client
    std::uint32_t local_id() const { return local_id_; }      // client
    const std::vector<std::uint32_t>& clients() const { return clients_; } // server

private:
    void handle_server(mosaic::engine::World& world, const NetEvent& ev)
    {
        if (ev.type == NetEvent::Type::Message)
        {
            MessageType type{}; std::string payload;
            if (!decode(ev.data, type, payload) || type != MessageType::Hello) { return; }
            if (sessions_.count(ev.connection) != 0) { return; } // duplicate Hello -> ignore
            const std::uint32_t id = next_session_id_++;
            sessions_.emplace(ev.connection, id);
            clients_.push_back(id);
            transport_->send(ev.connection, Channel::Reliable, encode(MessageType::Welcome, put_u32(id)));
            world.events().emit(NetworkEvent{NetworkEvent::Type::PeerJoined, id});
        }
        else if (ev.type == NetEvent::Type::Disconnected)
        {
            auto it = sessions_.find(ev.connection);
            if (it == sessions_.end()) { return; }
            const std::uint32_t id = it->second;
            sessions_.erase(it);
            clients_.erase(std::remove(clients_.begin(), clients_.end(), id), clients_.end());
            world.events().emit(NetworkEvent{NetworkEvent::Type::PeerLeft, id});
        }
        // Connected on the server is a no-op: the session is created on Hello.
    }

    void handle_client(mosaic::engine::World& world, const NetEvent& ev)
    {
        if (ev.type == NetEvent::Type::Connected)
        {
            transport_->send(ev.connection, Channel::Reliable, encode(MessageType::Hello, ""));
        }
        else if (ev.type == NetEvent::Type::Message)
        {
            MessageType type{}; std::string payload;
            if (!decode(ev.data, type, payload) || type != MessageType::Welcome) { return; }
            local_id_ = get_u32(payload);
            connected_ = true;
            world.events().emit(NetworkEvent{NetworkEvent::Type::SessionEstablished, local_id_});
        }
        else if (ev.type == NetEvent::Type::Disconnected)
        {
            connected_ = false;
            world.events().emit(NetworkEvent{NetworkEvent::Type::Disconnected, local_id_});
        }
    }

    Role role_;
    std::shared_ptr<Transport> transport_;
    // server
    std::unordered_map<ConnectionId, std::uint32_t> sessions_;
    std::vector<std::uint32_t> clients_;
    std::uint32_t next_session_id_ = 1;
    // client
    bool connected_ = false;
    std::uint32_t local_id_ = 0;
};
} // namespace mosaic::engine::net
