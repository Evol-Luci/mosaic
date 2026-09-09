#pragma once

#include <deque>
#include <memory>
#include <utility>

#include "engine/net/transport.hpp"

namespace mosaic::engine::net
{
namespace detail
{
// Shared state for one loopback pair. Two endpoints reference the same Link. Each
// endpoint drains its own inbound queue; sending pushes onto the peer's inbound.
struct LoopLink
{
    std::deque<NetEvent> to_server; // messages/events the SERVER will poll()
    std::deque<NetEvent> to_client; // messages/events the CLIENT will poll()
    bool                 server_connected_pending = true;
    bool                 client_connected_pending = true;
    bool                 closed = false;
};
} // namespace detail

// One endpoint of a loopback pair. `is_server_` picks which queue is "mine" (inbound) vs
// the peer's (outbound), and which stable ids apply (server sees client=2; client sees
// server=1).
class LoopbackTransport : public Transport
{
public:
    LoopbackTransport(std::shared_ptr<detail::LoopLink> link, bool is_server)
        : link_(std::move(link)), is_server_(is_server) {}

    // self id = the id the PEER uses to address me; peer id = the id I use to address them.
    ConnectionId self_id() const { return is_server_ ? 1u : 2u; }
    ConnectionId peer_id() const { return is_server_ ? 2u : 1u; }

    void send(ConnectionId to, Channel channel, const std::string& bytes) override
    {
        if (link_->closed || to != peer_id()) { return; } // closed / unknown -> drop
        NetEvent ev;
        ev.type = NetEvent::Type::Message;
        ev.connection = self_id(); // the peer sees the message as coming from me
        ev.channel = channel;
        ev.data = bytes;
        peer_inbound().push_back(std::move(ev));
    }

    std::vector<NetEvent> poll() override
    {
        std::vector<NetEvent> out;
        // Latch the once-only Connected first.
        bool& connected_pending = is_server_ ? link_->server_connected_pending
                                             : link_->client_connected_pending;
        if (connected_pending)
        {
            connected_pending = false;
            NetEvent ev; ev.type = NetEvent::Type::Connected; ev.connection = peer_id();
            out.push_back(std::move(ev));
        }
        std::deque<NetEvent>& in = my_inbound();
        while (!in.empty()) { out.push_back(std::move(in.front())); in.pop_front(); }
        return out;
    }

    void disconnect(ConnectionId who) override
    {
        if (link_->closed || who != peer_id()) { return; }
        link_->closed = true;
        NetEvent ev; ev.type = NetEvent::Type::Disconnected; ev.connection = self_id();
        peer_inbound().push_back(std::move(ev)); // the peer learns I left
    }

private:
    std::deque<NetEvent>& my_inbound()
    { return is_server_ ? link_->to_server : link_->to_client; }
    std::deque<NetEvent>& peer_inbound()
    { return is_server_ ? link_->to_client : link_->to_server; }

    std::shared_ptr<detail::LoopLink> link_;
    bool                              is_server_;
};

// A connected server+client pair sharing one Link. Both first poll()s yield Connected.
inline std::pair<std::shared_ptr<Transport>, std::shared_ptr<Transport>> make_loopback_pair()
{
    auto link = std::make_shared<detail::LoopLink>();
    auto server = std::make_shared<LoopbackTransport>(link, /*is_server=*/true);
    auto client = std::make_shared<LoopbackTransport>(link, /*is_server=*/false);
    return {server, client};
}
} // namespace mosaic::engine::net
