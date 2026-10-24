#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "engine/net/transport.hpp"

namespace mosaic::net::enet
{
// An ENet server endpoint: a Transport plus the one thing a UDP listener has that the
// backend-free Transport interface deliberately should not know about -- the port it is
// actually bound to. `port()` lives HERE rather than on Transport because a port is
// meaningless for LoopbackTransport, and widening a general interface to suit one backend
// is the wrong trade.
class EnetServer : public mosaic::engine::net::Transport
{
public:
    // The port this server is listening on. With 0 requested, this is the port the kernel
    // chose -- and asking is the only way to learn it. Never 0 for a live server: ENet's
    // enet_host_create() queries the socket back into host->address after binding
    // (enet/host.c, `enet_socket_get_address (host -> socket, & host -> address)`).
    virtual std::uint16_t port() const = 0;
};

// Real UDP transport over ENet. Listens on `port` for up to `max_peers` clients. Returns
// nullptr if the host could not bind (e.g. port already in use).
//
// PASS 0 TO BIND AN EPHEMERAL PORT and read it back with port(). Prefer that to a fixed
// port anywhere a specific number is not required: a hard-coded port is one another
// process can already hold, and that has failed a real CI run in this repo
// (tests/unit/studio/live_link_server_test.cpp:185, port 34906).
std::unique_ptr<EnetServer> create_enet_server(std::uint16_t port, std::size_t max_peers);

// Real UDP transport over ENet. Initiates a connection to `host:port`; the Connected
// NetEvent arrives from poll() once the handshake completes. Returns nullptr only if the
// local ENet host itself could not be created (never on connection failure/timeout — that
// surfaces later as an absent Connected event, mirroring how a real socket connect is
// asynchronous).
std::unique_ptr<mosaic::engine::net::Transport> create_enet_client(
    const std::string& host, std::uint16_t port);
} // namespace mosaic::net::enet
