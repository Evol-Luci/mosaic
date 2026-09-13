#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mosaic::studio
{
enum class LiveLinkState { Off, Listening, Connected };

constexpr std::uint16_t kDefaultLiveLinkPort = 6828;

// A small, Linux-only, non-blocking POSIX TCP listener for Blender Live-Link. Single
// connection at a time — a new accept() replaces any prior peer. Socket internals
// (file descriptors, POSIX headers) stay entirely inside live_link_server.cpp.
class LiveLinkServer
{
public:
    LiveLinkServer() = default;
    ~LiveLinkServer();
    LiveLinkServer(const LiveLinkServer&) = delete;
    LiveLinkServer& operator=(const LiveLinkServer&) = delete;

    // Binds and listens on `port`. Returns false on bind failure (e.g. port in use).
    bool start(std::uint16_t port);

    // Closes any listening/connected socket. state() -> Off. Safe to call when
    // already Off.
    void stop();

    LiveLinkState state() const { return state_; }

    // Non-blocking: accepts a pending connection if any, drains available bytes,
    // and returns every complete newline-terminated line received this call (the
    // trailing newline is stripped; an incomplete trailing line is buffered for
    // the next poll()). Call once per Studio frame. Empty when state() == Off.
    std::vector<std::string> poll();

    // Best-effort non-blocking send on the current connection. A no-op if state()
    // != Connected. A send that would block, or fails, is silently dropped — this
    // is a continuous per-tick stream, so a dropped line is superseded by the next
    // tick's send, never retried or queued.
    void send(const std::string& line);

private:
    int listen_fd_ = -1;
    int peer_fd_ = -1;
    LiveLinkState state_ = LiveLinkState::Off;
    std::string recv_buffer_;
};
} // namespace mosaic::studio
