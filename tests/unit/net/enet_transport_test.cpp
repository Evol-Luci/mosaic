#include <algorithm>
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
// Every case binds port 0 and reads the kernel's choice back with server->port(). There
// are no fixed ports in this file: a fixed port is one another process can already hold,
// which failed a real CI run here (live_link_server_test.cpp:185, port 34906). Moving the
// ports below the ephemeral range in #273 was a mitigation; this is the fix.
//
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
    auto server = create_enet_server(0, 4);
    REQUIRE(server != nullptr); // before port(): a null server must fail, not segfault
    auto client = create_enet_client("127.0.0.1", server->port());
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
    auto server = create_enet_server(0, 4);
    REQUIRE(server != nullptr); // before port(): a null server must fail, not segfault
    auto client = create_enet_client("127.0.0.1", server->port());

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
    auto server = create_enet_server(0, 4);
    REQUIRE(server != nullptr); // before port(): a null server must fail, not segfault
    auto client = create_enet_client("127.0.0.1", server->port());

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

TEST_CASE("ENet transport surfaces a Disconnected event on the peer", "[net][enet]")
{
    auto server = create_enet_server(0, 4);
    REQUIRE(server != nullptr); // before port(): a null server must fail, not segfault
    auto client = create_enet_client("127.0.0.1", server->port());

    ConnectionId server_side_client_id = 0;
    poll_until([&]
    {
        for (auto& e : server->poll())
        {
            if (e.type == NetEvent::Type::Connected) { server_side_client_id = e.connection; }
        }
        client->poll();
        return server_side_client_id != 0u;
    });

    server->disconnect(server_side_client_id);

    std::vector<NetEvent> client_events;
    poll_until([&]
    {
        server->poll(); // flush the queued disconnect command
        for (auto& e : client->poll()) { client_events.push_back(e); }
        return std::any_of(client_events.begin(), client_events.end(),
            [](const NetEvent& e) { return e.type == NetEvent::Type::Disconnected; });
    });

    REQUIRE(std::any_of(client_events.begin(), client_events.end(),
        [](const NetEvent& e) { return e.type == NetEvent::Type::Disconnected; }));
}

TEST_CASE("ENet transport drops a send to an unknown connection id", "[net][enet]")
{
    auto server = create_enet_server(0, 4);
    REQUIRE(server != nullptr); // before port(): a null server must fail, not segfault
    auto client = create_enet_client("127.0.0.1", server->port());

    client->send(/*never-seen id*/ 999u, Channel::Reliable, "nowhere"); // no crash expected

    poll_until([&] { server->poll(); client->poll(); return true; }, /*max_attempts=*/40);
    SUCCEED("send to an unknown id did not crash");
}

TEST_CASE("ENet server assigns distinct ids to multiple clients", "[net][enet]")
{
    auto server = create_enet_server(0, 4);
    REQUIRE(server != nullptr); // before port(): a null server must fail, not segfault
    auto client_a = create_enet_client("127.0.0.1", server->port());
    auto client_b = create_enet_client("127.0.0.1", server->port());

    std::vector<ConnectionId> ids;
    poll_until([&]
    {
        for (auto& e : server->poll())
        {
            if (e.type == NetEvent::Type::Connected) { ids.push_back(e.connection); }
        }
        client_a->poll();
        client_b->poll();
        return ids.size() == 2;
    }, /*max_attempts=*/400);

    REQUIRE(ids.size() == 2);
    REQUIRE(ids[0] != ids[1]);
}

TEST_CASE("ENet server binds port 0 and reports the kernel's choice", "[net][enet]")
{
    // The structural fix for hard-coded test ports. Fixed ports in the ephemeral range
    // have already failed a real CI run here (live_link_server_test.cpp:185, port 34906);
    // moving them below that range in #273 was a mitigation, not a fix -- a fixed port is
    // still a port something else can hold. Binding 0 and asking the kernel what it gave
    // us removes the class, exactly as LiveLinkServer::port() already does.
    auto server = create_enet_server(0, 4);
    REQUIRE(server != nullptr);

    // Non-zero is necessary but NOT sufficient: a port() that returned any plausible
    // number would pass this and still be useless. The handshake below is what proves
    // the number is the port the server is actually listening on.
    REQUIRE(server->port() != 0);

    auto client = create_enet_client("127.0.0.1", server->port());
    REQUIRE(client != nullptr);

    bool connected = false;
    poll_until(
        [&]
        {
            for (const auto& e : server->poll())
            {
                if (e.type == NetEvent::Type::Connected) { connected = true; }
            }
            client->poll();
            return connected;
        });
    REQUIRE(connected);
}

TEST_CASE("ENet server reports an explicitly requested port verbatim", "[net][enet]")
{
    // Guards against port() being a function that merely returns something plausible:
    // with a port we chose, it must return that exact number. Bound via 0 first so the
    // number is known-free rather than hoped-free -- the same construction
    // live_link_server_test.cpp uses for the same reason.
    auto probe = create_enet_server(0, 1);
    REQUIRE(probe != nullptr);
    const std::uint16_t known_free = probe->port();
    REQUIRE(known_free != 0);
    probe.reset();

    auto server = create_enet_server(known_free, 4);
    REQUIRE(server != nullptr);
    REQUIRE(server->port() == known_free);
}
