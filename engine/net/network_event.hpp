#pragma once

#include <cstdint>

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
} // namespace mosaic::engine::net
