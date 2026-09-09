#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/net/loopback_transport.hpp"
#include "engine/net/transport.hpp"

using mosaic::engine::net::Channel;
using mosaic::engine::net::make_loopback_pair;
using mosaic::engine::net::NetEvent;
using mosaic::engine::net::Transport;

TEST_CASE("loopback pair sees a Connected event on the first poll", "[net]")
{
    auto [server, client] = make_loopback_pair();

    const auto se = server->poll();
    REQUIRE(se.size() == 1);
    REQUIRE(se[0].type == NetEvent::Type::Connected);
    REQUIRE(se[0].connection == 2u); // client id, from the server's side

    const auto ce = client->poll();
    REQUIRE(ce.size() == 1);
    REQUIRE(ce[0].type == NetEvent::Type::Connected);
    REQUIRE(ce[0].connection == 1u); // server id, from the client's side

    REQUIRE(server->poll().empty()); // no repeat, no traffic
    REQUIRE(client->poll().empty());
}

TEST_CASE("loopback delivers a reliable message to the peer, not the sender", "[net]")
{
    auto [server, client] = make_loopback_pair();
    server->poll(); client->poll(); // drain the Connected events

    client->send(/*to server*/ 1u, Channel::Reliable, "ping");

    const auto se = server->poll();
    REQUIRE(se.size() == 1);
    REQUIRE(se[0].type == NetEvent::Type::Message);
    REQUIRE(se[0].connection == 2u);           // from the client
    REQUIRE(se[0].channel == Channel::Reliable);
    REQUIRE(se[0].data == "ping");

    REQUIRE(client->poll().empty());           // sender does not receive its own message

    server->send(/*to client*/ 2u, Channel::Reliable, "pong");
    const auto ce = client->poll();
    REQUIRE(ce.size() == 1);
    REQUIRE(ce[0].data == "pong");
    REQUIRE(ce[0].connection == 1u);           // from the server
}

TEST_CASE("loopback preserves the unreliable channel tag", "[net]")
{
    auto [server, client] = make_loopback_pair();
    server->poll(); client->poll();

    client->send(1u, Channel::Unreliable, "u");
    const auto se = server->poll();
    REQUIRE(se.size() == 1);
    REQUIRE(se[0].channel == Channel::Unreliable);
    REQUIRE(se[0].data == "u");
}

TEST_CASE("loopback drains messages FIFO", "[net]")
{
    auto [server, client] = make_loopback_pair();
    server->poll(); client->poll();

    client->send(1u, Channel::Reliable, "a");
    client->send(1u, Channel::Reliable, "b");
    client->send(1u, Channel::Reliable, "c");

    const auto se = server->poll();
    REQUIRE(se.size() == 3);
    REQUIRE(se[0].data == "a");
    REQUIRE(se[1].data == "b");
    REQUIRE(se[2].data == "c");
}

TEST_CASE("loopback disconnect surfaces on the peer and drops later sends", "[net]")
{
    auto [server, client] = make_loopback_pair();
    server->poll(); client->poll();

    client->disconnect(1u); // client disconnects from the server

    const auto se = server->poll();
    REQUIRE(se.size() == 1);
    REQUIRE(se[0].type == NetEvent::Type::Disconnected);
    REQUIRE(se[0].connection == 2u);

    // Sends after disconnect are silently dropped (no throw, no delivery).
    server->send(2u, Channel::Reliable, "late");
    REQUIRE(client->poll().empty());
}

TEST_CASE("loopback drops a send to an unknown connection", "[net]")
{
    auto [server, client] = make_loopback_pair();
    server->poll(); client->poll();

    client->send(/*not the server*/ 99u, Channel::Reliable, "nowhere");
    REQUIRE(server->poll().empty());
}
