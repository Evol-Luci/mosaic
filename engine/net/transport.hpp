#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mosaic::engine::net
{
using ConnectionId = std::uint32_t; // opaque; 0 == invalid/none

// Reliable-ordered vs UDP-style unreliable. The loopback delivers both losslessly and in
// order (tag preserved, no loss simulation); the tag matters for a real backend.
enum class Channel { Reliable, Unreliable };

// One event drained from an endpoint's poll(). data/channel are meaningful only for
// Message; connection identifies the peer for all three types.
struct NetEvent
{
    enum class Type { Connected, Disconnected, Message };
    Type         type = Type::Message;
    ConnectionId connection = 0;
    Channel      channel = Channel::Reliable;
    std::string  data;
};

// One network endpoint. Backend-free: concrete transports (loopback here; a real UDP
// backend later) live behind this seam and are created via factory functions. poll() is
// the per-tick, non-blocking event drain.
class Transport
{
public:
    virtual ~Transport() = default;
    virtual void send(ConnectionId to, Channel channel, const std::string& bytes) = 0;
    virtual std::vector<NetEvent> poll() = 0;
    virtual void disconnect(ConnectionId who) = 0;
};
} // namespace mosaic::engine::net
