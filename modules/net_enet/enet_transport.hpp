#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "engine/net/transport.hpp"

namespace mosaic::net::enet
{
// Real UDP transport over ENet. Listens on `port` for up to `max_peers` clients. Returns
// nullptr if the host could not bind (e.g. port already in use).
std::unique_ptr<mosaic::engine::net::Transport> create_enet_server(
    std::uint16_t port, std::size_t max_peers);

// Real UDP transport over ENet. Initiates a connection to `host:port`; the Connected
// NetEvent arrives from poll() once the handshake completes. Returns nullptr only if the
// local ENet host itself could not be created (never on connection failure/timeout — that
// surfaces later as an absent Connected event, mirroring how a real socket connect is
// asynchronous).
std::unique_ptr<mosaic::engine::net::Transport> create_enet_client(
    const std::string& host, std::uint16_t port);
} // namespace mosaic::net::enet
