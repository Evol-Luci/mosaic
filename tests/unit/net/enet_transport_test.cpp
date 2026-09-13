#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "engine/net/transport.hpp"
#include "modules/net_enet/enet_transport.hpp"

using mosaic::engine::net::Channel;
using mosaic::engine::net::NetEvent;
using mosaic::engine::net::Transport;
using mosaic::net::enet::create_enet_client;
using mosaic::net::enet::create_enet_server;

namespace
{
// Poll `t` up to max_attempts times (sleeping sleep_ms between) until pred() is true. Real
// UDP has real scheduling latency, unlike the in-memory LoopbackTransport tests elsewhere —
// this bounds the wait instead of assuming a fixed tick count suffices.
template <class Pred>
void poll_until(Transport& t, Pred pred, int max_attempts = 200, int sleep_ms = 5)
{
    for (int i = 0; i < max_attempts && !pred(); ++i)
    {
        t.poll();
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
    poll_until(*server, [&]
    {
        for (auto& e : server->poll()) { server_events.push_back(e); }
        return !server_events.empty();
    });
    std::vector<NetEvent> client_events;
    poll_until(*client, [&]
    {
        for (auto& e : client->poll()) { client_events.push_back(e); }
        return !client_events.empty();
    });

    REQUIRE(server_events.size() == 1);
    REQUIRE(server_events[0].type == NetEvent::Type::Connected);
    REQUIRE(server_events[0].connection != 0u);

    REQUIRE(client_events.size() == 1);
    REQUIRE(client_events[0].type == NetEvent::Type::Connected);
    REQUIRE(client_events[0].connection != 0u);
}
