#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/net/input_system.hpp"
#include "engine/net/loopback_transport.hpp"
#include "engine/net/message.hpp"
#include "engine/net/network_event.hpp"
#include "engine/net/network_system.hpp"
#include "engine/net/transport.hpp"
#include "engine/world/world.hpp"

using mosaic::engine::World;
using mosaic::engine::net::Channel;
using mosaic::engine::net::ConnectionId;
using mosaic::engine::net::decode;
using mosaic::engine::net::encode;
using mosaic::engine::net::InputSystem;
using mosaic::engine::net::make_loopback_pair;
using mosaic::engine::net::MessageType;
using mosaic::engine::net::NetEvent;
using mosaic::engine::net::NetworkInput;
using mosaic::engine::net::NetworkSystem;
using mosaic::engine::net::Role;
using mosaic::engine::net::Transport;

namespace
{
// Mirrors the FakeTransport in network_system_test.cpp (kept local — no shared test header
// exists yet for this small helper).
class FakeTransport : public Transport
{
public:
    std::vector<NetEvent> inbox;
    struct Sent { ConnectionId to; Channel channel; std::string bytes; };
    std::vector<Sent> sent;

    void send(ConnectionId to, Channel ch, const std::string& b) override
    { sent.push_back({to, ch, b}); }
    std::vector<NetEvent> poll() override
    { std::vector<NetEvent> out; out.swap(inbox); return out; }
    void disconnect(ConnectionId) override {}
};
} // namespace

TEST_CASE("client InputSystem sends each provided payload as an Input frame", "[net][input]")
{
    auto transport = std::make_shared<FakeTransport>();
    World world;
    auto& net = world.add_system<NetworkSystem>(Role::Client, transport);
    world.add_system<InputSystem>(Role::Client, &net,
                                  []() { return std::vector<std::string>{"a", "b"}; });

    NetEvent connected;
    connected.type = NetEvent::Type::Connected;
    connected.connection = 9;
    transport->inbox.push_back(connected);

    world.tick(0.016f); // NetworkSystem handles Connected (sends Hello) + InputSystem sends a,b

    REQUIRE(transport->sent.size() == 3u); // Hello + "a" + "b"
    for (std::size_t i = 1; i < transport->sent.size(); ++i)
    {
        REQUIRE(transport->sent[i].to == 9u);
    }
    MessageType type{};
    std::string payload;
    REQUIRE(decode(transport->sent[1].bytes, type, payload));
    REQUIRE(type == MessageType::Input);
    REQUIRE(payload == "a");
    REQUIRE(decode(transport->sent[2].bytes, type, payload));
    REQUIRE(payload == "b");
}

TEST_CASE("client InputSystem with an empty provider result sends nothing", "[net][input]")
{
    auto transport = std::make_shared<FakeTransport>();
    World world;
    auto& net = world.add_system<NetworkSystem>(Role::Client, transport);
    world.add_system<InputSystem>(Role::Client, &net,
                                  []() { return std::vector<std::string>{}; });

    world.tick(0.016f);

    REQUIRE(transport->sent.empty());
}

TEST_CASE("server InputSystem emits NetworkInput for a received Input frame", "[net][input]")
{
    auto transport = std::make_shared<FakeTransport>();
    World world;
    auto& net = world.add_system<NetworkSystem>(Role::Server, transport);
    world.add_system<InputSystem>(Role::Server, &net);

    std::vector<NetworkInput> received;
    world.events().subscribe<NetworkInput>(
        [&](const NetworkInput& e) { received.push_back(e); });

    // Establish a session first (server assigns id 1 to connection 5 on Hello).
    NetEvent hello;
    hello.type = NetEvent::Type::Message;
    hello.connection = 5;
    hello.data = encode(MessageType::Hello, "");
    transport->inbox.push_back(hello);
    world.tick(0.016f);

    NetEvent input;
    input.type = NetEvent::Type::Message;
    input.connection = 5;
    input.data = encode(MessageType::Input, "move:1,0");
    transport->inbox.push_back(input);
    world.tick(0.016f);

    REQUIRE(received.size() == 1u);
    REQUIRE(received[0].session == 1u);
    REQUIRE(received[0].payload == "move:1,0");
}

TEST_CASE("input flows client to server over the loopback", "[net][input]")
{
    auto [server_tx, client_tx] = make_loopback_pair();

    World server_world;
    auto& server_net = server_world.add_system<NetworkSystem>(Role::Server, server_tx);
    server_world.add_system<InputSystem>(Role::Server, &server_net);
    std::vector<NetworkInput> received;
    server_world.events().subscribe<NetworkInput>(
        [&](const NetworkInput& e) { received.push_back(e); });

    World client_world;
    auto& client_net = client_world.add_system<NetworkSystem>(Role::Client, client_tx);
    int calls = 0;
    client_world.add_system<InputSystem>(Role::Client, &client_net,
        [&]() -> std::vector<std::string>
        {
            ++calls;
            return calls == 1 ? std::vector<std::string>{"move:1,0"}
                              : std::vector<std::string>{};
        });

    for (int i = 0; i < 4; ++i) { client_world.tick(0.016f); server_world.tick(0.016f); }

    REQUIRE(client_net.is_connected());
    REQUIRE(received.size() == 1u);
    REQUIRE(received[0].session == client_net.local_id());
    REQUIRE(received[0].payload == "move:1,0");
}
