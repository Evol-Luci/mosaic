#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/net/transport.hpp"
#include "modules/net_enet/enet_transport.hpp"

using mosaic::engine::net::Channel;
using mosaic::engine::net::ConnectionId;
using mosaic::engine::net::NetEvent;
using mosaic::engine::net::Transport;
using mosaic::net::enet::create_enet_client;
using mosaic::net::enet::create_enet_server;

namespace
{
// Call pred() up to max_attempts times (sleeping sleep_ms between) until it returns true.
// pred is responsible for its own polling/event-collection — this helper only paces the
// retries. Real UDP has real scheduling latency, unlike the in-memory LoopbackTransport
// tests elsewhere, so a fixed tick count won't do; but poll() is drain-once, so this helper
// must never poll on its own behalf, or it would steal events out from under pred() on the
// next iteration.
template <class Pred>
void poll_until(Pred pred, int max_attempts = 200, int sleep_ms = 5)
{
    for (int i = 0; i < max_attempts && !pred(); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }
}
} // namespace

TEST_CASE("ENet transport completes a connect handshake over loopback", "[net][enet]")
{
    constexpr std::uint16_t port = 34001;
    auto server = create_enet_server(port, 4);
    auto client = create_enet_client("127.0.0.1", port);
    REQUIRE(server != nullptr);
    REQUIRE(client != nullptr);

    std::vector<NetEvent> server_events;
    std::vector<NetEvent> client_events;
    poll_until([&]
    {
        for (auto& e : server->poll()) { server_events.push_back(e); }
        for (auto& e : client->poll()) { client_events.push_back(e); }
        return !server_events.empty() && !client_events.empty();
    });

    REQUIRE(server_events.size() == 1);
    REQUIRE(server_events[0].type == NetEvent::Type::Connected);
    REQUIRE(server_events[0].connection != 0u);

    REQUIRE(client_events.size() == 1);
    REQUIRE(client_events[0].type == NetEvent::Type::Connected);
    REQUIRE(client_events[0].connection != 0u);
}

TEST_CASE("ENet transport delivers a reliable message to the peer", "[net][enet]")
{
    constexpr std::uint16_t port = 34002;
    auto server = create_enet_server(port, 4);
    auto client = create_enet_client("127.0.0.1", port);

    ConnectionId server_side_client_id = 0;
    ConnectionId client_side_server_id = 0;
    poll_until([&]
    {
        for (auto& e : server->poll())
        {
            if (e.type == NetEvent::Type::Connected) { server_side_client_id = e.connection; }
        }
        for (auto& e : client->poll())
        {
            if (e.type == NetEvent::Type::Connected) { client_side_server_id = e.connection; }
        }
        return server_side_client_id != 0u && client_side_server_id != 0u;
    });

    client->send(client_side_server_id, Channel::Reliable, "ping");

    std::vector<NetEvent> received;
    poll_until([&]
    {
        client->poll(); // enet_peer_send only queues; the sender's own poll() flushes it
        for (auto& e : server->poll())
        {
            if (e.type == NetEvent::Type::Message) { received.push_back(e); }
        }
        return !received.empty();
    });

    REQUIRE(received.size() == 1);
    REQUIRE(received[0].connection == server_side_client_id);
    REQUIRE(received[0].channel == Channel::Reliable);
    REQUIRE(received[0].data == "ping");
}

TEST_CASE("ENet transport preserves the unreliable channel tag", "[net][enet]")
{
    constexpr std::uint16_t port = 34003;
    auto server = create_enet_server(port, 4);
    auto client = create_enet_client("127.0.0.1", port);

    ConnectionId client_side_server_id = 0;
    poll_until([&]
    {
        server->poll();
        for (auto& e : client->poll())
        {
            if (e.type == NetEvent::Type::Connected) { client_side_server_id = e.connection; }
        }
        return client_side_server_id != 0u;
    });

    client->send(client_side_server_id, Channel::Unreliable, "u");

    std::vector<NetEvent> received;
    poll_until([&]
    {
        client->poll();
        for (auto& e : server->poll())
        {
            if (e.type == NetEvent::Type::Message) { received.push_back(e); }
        }
        return !received.empty();
    });

    REQUIRE(received.size() == 1);
    REQUIRE(received[0].channel == Channel::Unreliable);
    REQUIRE(received[0].data == "u");
}
