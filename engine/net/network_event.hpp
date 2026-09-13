#pragma once

#include <cstdint>
#include <string>

namespace mosaic::engine::net
{
// Emitted on the World event bus by NetworkSystem. Server: PeerJoined/PeerLeft carry a
// client session id. Client: SessionEstablished carries its own assigned id; Disconnected
// on link loss.
struct NetworkEvent
{
    enum class Type { PeerJoined, PeerLeft, SessionEstablished, Disconnected };
    Type          type = Type::PeerJoined;
    std::uint32_t peer = 0;
};

// Emitted on the World event bus by InputSystem (server role) for each received client
// input frame. Carries the sender's session id and the opaque payload — the engine does not
// interpret it; game/script logic subscribes and decodes it.
struct NetworkInput
{
    std::uint32_t session = 0;
    std::string   payload;
};
} // namespace mosaic::engine::net
