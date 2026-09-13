#pragma once

#include <cstdint>
#include <string>

namespace mosaic::engine::net
{
enum class MessageType : std::uint8_t
{
    Hello    = 1, // client -> server on connect (payload: empty)
    Welcome  = 2, // server -> client (payload: u32 assigned session id)
    Snapshot = 3, // server -> client (payload: JSON replication snapshot from N3)
    Input    = 4, // client -> server (payload: opaque, app-defined bytes; N5)
};

// Frame = [1 byte type][payload bytes].
inline std::string encode(MessageType type, const std::string& payload)
{
    std::string out;
    out.push_back(static_cast<char>(static_cast<std::uint8_t>(type)));
    out += payload;
    return out;
}

// Decode a frame. false on an empty buffer or an unknown type.
inline bool decode(const std::string& frame, MessageType& type, std::string& payload)
{
    if (frame.empty()) { return false; }
    const auto raw = static_cast<std::uint8_t>(frame[0]);
    if (raw != static_cast<std::uint8_t>(MessageType::Hello) &&
        raw != static_cast<std::uint8_t>(MessageType::Welcome) &&
        raw != static_cast<std::uint8_t>(MessageType::Snapshot) &&
        raw != static_cast<std::uint8_t>(MessageType::Input))
    {
        return false;
    }
    type = static_cast<MessageType>(raw);
    payload = frame.substr(1);
    return true;
}

inline std::string put_u32(std::uint32_t v)
{
    std::string s;
    s.push_back(static_cast<char>(v & 0xFF));
    s.push_back(static_cast<char>((v >> 8) & 0xFF));
    s.push_back(static_cast<char>((v >> 16) & 0xFF));
    s.push_back(static_cast<char>((v >> 24) & 0xFF));
    return s;
}
inline std::uint32_t get_u32(const std::string& s, std::size_t off = 0)
{
    if (off + 4 > s.size()) { return 0; }
    return static_cast<std::uint32_t>(static_cast<unsigned char>(s[off])) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(s[off + 1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(s[off + 2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(s[off + 3])) << 24);
}
} // namespace mosaic::engine::net
