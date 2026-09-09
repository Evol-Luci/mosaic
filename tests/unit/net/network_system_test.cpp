#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/net/loopback_transport.hpp"
#include "engine/net/message.hpp"
#include "engine/net/network_event.hpp"
#include "engine/net/network_system.hpp"
#include "engine/net/transport.hpp"
#include "engine/world/world.hpp"

using mosaic::engine::World;
using mosaic::engine::net::Channel;
using mosaic::engine::net::ConnectionId;
using mosaic::engine::net::encode;
using mosaic::engine::net::get_u32;
using mosaic::engine::net::make_loopback_pair;
using mosaic::engine::net::MessageType;
using mosaic::engine::net::NetEvent;
using mosaic::engine::net::NetworkEvent;
using mosaic::engine::net::NetworkSystem;
using mosaic::engine::net::Role;
using mosaic::engine::net::Transport;

namespace
{
// A test-only Transport: poll() returns injected events; send()/disconnect() are captured.
class FakeTransport : public Transport
{
public:
    std::vector<NetEvent> inbox;
    struct Sent { ConnectionId to; Channel channel; std::string bytes; };
    std::vector<Sent> sent;
    std::vector<ConnectionId> disconnected;

    void send(ConnectionId to, Channel ch, const std::string& b) override
    { sent.push_back({to, ch, b}); }
    std::vector<NetEvent> poll() override
    { std::vector<NetEvent> out; out.swap(inbox); return out; }
    void disconnect(ConnectionId w) override { disconnected.push_back(w); }

    // Helper: queue an incoming Message event.
    void inject_message(ConnectionId from, const std::string& frame)
    {
        NetEvent e; e.type = NetEvent::Type::Message; e.connection = from;
        e.channel = Channel::Reliable; e.data = frame; inbox.push_back(e);
    }
    void inject_disconnect(ConnectionId from)
    {
        NetEvent e; e.type = NetEvent::Type::Disconnected; e.connection = from;
        inbox.push_back(e);
    }
};
} // namespace

TEST_CASE("handshake completes end-to-end over the loopback", "[net]")
{
    auto [server_tx, client_tx] = make_loopback_pair();

    World server_world;
    auto& server = server_world.add_system<NetworkSystem>(Role::Server, server_tx);
    std::vector<NetworkEvent> server_events;
    server_world.events().subscribe<NetworkEvent>(
        [&](const NetworkEvent& e) { server_events.push_back(e); });

    World client_world;
    auto& client = client_world.add_system<NetworkSystem>(Role::Client, client_tx);
    std::vector<NetworkEvent> client_events;
    client_world.events().subscribe<NetworkEvent>(
        [&](const NetworkEvent& e) { client_events.push_back(e); });

    // Tick both a few times so Hello -> Welcome -> established completes.
    for (int i = 0; i < 4; ++i) { client_world.tick(0.016f); server_world.tick(0.016f); }

    REQUIRE(client.is_connected());
    REQUIRE(client.local_id() == 1u);
    REQUIRE(server.clients() == std::vector<std::uint32_t>{1u});

    bool server_joined = false, client_established = false;
    for (const auto& e : server_events)
        if (e.type == NetworkEvent::Type::PeerJoined && e.peer == 1u) server_joined = true;
    for (const auto& e : client_events)
        if (e.type == NetworkEvent::Type::SessionEstablished && e.peer == 1u) client_established = true;
    REQUIRE(server_joined);
    REQUIRE(client_established);
}

TEST_CASE("server assigns monotonic ids and replies Welcome per client", "[net]")
{
    auto fake = std::make_shared<FakeTransport>();
    World w;
    auto& server = w.add_system<NetworkSystem>(Role::Server, fake);

    fake->inject_message(10u, encode(MessageType::Hello, ""));
    fake->inject_message(11u, encode(MessageType::Hello, ""));
    w.tick(0.016f);

    REQUIRE(server.clients() == std::vector<std::uint32_t>{1u, 2u});
    REQUIRE(fake->sent.size() == 2);
    // Welcome(1) -> conn 10, Welcome(2) -> conn 11, both Reliable.
    REQUIRE(fake->sent[0].to == 10u);
    REQUIRE(fake->sent[0].channel == Channel::Reliable);
    MessageType t{}; std::string p;
    REQUIRE(mosaic::engine::net::decode(fake->sent[0].bytes, t, p));
    REQUIRE(t == MessageType::Welcome);
    REQUIRE(get_u32(p) == 1u);
    REQUIRE(fake->sent[1].to == 11u);
    REQUIRE(mosaic::engine::net::decode(fake->sent[1].bytes, t, p));
    REQUIRE(get_u32(p) == 2u);
}

TEST_CASE("server ignores a duplicate Hello from an existing session", "[net]")
{
    auto fake = std::make_shared<FakeTransport>();
    World w;
    auto& server = w.add_system<NetworkSystem>(Role::Server, fake);

    fake->inject_message(10u, encode(MessageType::Hello, ""));
    w.tick(0.016f);
    fake->inject_message(10u, encode(MessageType::Hello, "")); // duplicate
    w.tick(0.016f);

    REQUIRE(server.clients() == std::vector<std::uint32_t>{1u}); // not double-added
    REQUIRE(fake->sent.size() == 1);                             // no second Welcome
}

TEST_CASE("server drops a session on disconnect and emits PeerLeft", "[net]")
{
    auto fake = std::make_shared<FakeTransport>();
    World w;
    auto& server = w.add_system<NetworkSystem>(Role::Server, fake);
    std::vector<NetworkEvent> events;
    w.events().subscribe<NetworkEvent>([&](const NetworkEvent& e) { events.push_back(e); });

    fake->inject_message(10u, encode(MessageType::Hello, ""));
    w.tick(0.016f);
    fake->inject_disconnect(10u);
    w.tick(0.016f);

    REQUIRE(server.clients().empty());
    bool left = false;
    for (const auto& e : events)
        if (e.type == NetworkEvent::Type::PeerLeft && e.peer == 1u) left = true;
    REQUIRE(left);
}

TEST_CASE("no traffic emits no events", "[net]")
{
    auto fake = std::make_shared<FakeTransport>();
    World w;
    w.add_system<NetworkSystem>(Role::Server, fake);
    std::vector<NetworkEvent> events;
    w.events().subscribe<NetworkEvent>([&](const NetworkEvent& e) { events.push_back(e); });
    w.tick(0.016f);
    REQUIRE(events.empty());
}
