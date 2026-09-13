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

// A received application frame (non-control), surfaced to other systems via take_inbound().
struct InboundMessage
{
    std::uint32_t peer;      // server: sender's session id; client: 0 ("the server")
    MessageType   type;
    std::string   payload;
};

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

    // Drain buffered non-control frames received since the last call (drain-once).
    std::vector<InboundMessage> take_inbound()
    {
        std::vector<InboundMessage> out = std::move(inbound_);
        inbound_.clear();
        return out;
    }

    // Server: send `bytes` to every connected session. Zero sessions -> no-op.
    void broadcast(Channel channel, const std::string& bytes)
    {
        if (!transport_) { return; }
        for (const auto& [conn, id] : sessions_)
        {
            (void)id;
            transport_->send(conn, channel, bytes);
        }
    }

    // Client: send `bytes` to the server. No-op if not yet connected at the transport level
    // (no Connected event observed) — distinct from is_connected(), which tracks the
    // post-Welcome session.
    void send_to_server(Channel channel, const std::string& bytes)
    {
        if (!transport_ || server_conn_ == 0) { return; }
        transport_->send(server_conn_, channel, bytes);
    }

private:
    void handle_server(mosaic::engine::World& world, const NetEvent& ev)
    {
        if (ev.type == NetEvent::Type::Message)
        {
            MessageType type{}; std::string payload;
            if (!decode(ev.data, type, payload)) { return; }
            if (type == MessageType::Hello)
            {
                if (sessions_.count(ev.connection) != 0) { return; } // duplicate Hello
                const std::uint32_t id = next_session_id_++;
                sessions_.emplace(ev.connection, id);
                clients_.push_back(id);
                transport_->send(ev.connection, Channel::Reliable,
                                 encode(MessageType::Welcome, put_u32(id)));
                world.events().emit(NetworkEvent{NetworkEvent::Type::PeerJoined, id});
            }
            else
            {
                // Non-control frame: buffer only from an established session.
                auto it = sessions_.find(ev.connection);
                if (it != sessions_.end())
                {
                    inbound_.push_back({it->second, type, payload});
                }
            }
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
            server_conn_ = ev.connection;
            transport_->send(ev.connection, Channel::Reliable, encode(MessageType::Hello, ""));
        }
        else if (ev.type == NetEvent::Type::Message)
        {
            MessageType type{}; std::string payload;
            if (!decode(ev.data, type, payload)) { return; }
            if (type == MessageType::Welcome)
            {
                local_id_ = get_u32(payload);
                connected_ = true;
                world.events().emit(NetworkEvent{NetworkEvent::Type::SessionEstablished, local_id_});
            }
            else
            {
                inbound_.push_back({0u, type, payload});
            }
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
    ConnectionId server_conn_ = 0; // transport link to the server, set on Connected

    std::vector<InboundMessage> inbound_;
};
} // namespace mosaic::engine::net
