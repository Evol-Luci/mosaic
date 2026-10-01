#include "studio/live_link_server.hpp"

#include <cerrno>
#include <cstring>

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace mosaic::studio
{
namespace
{
void set_nonblocking(int fd)
{
    const int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
} // namespace

LiveLinkServer::~LiveLinkServer() { stop(); }

bool LiveLinkServer::start(std::uint16_t port)
{
    stop();

    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) { return false; }

    const int reuse = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    if (listen(listen_fd_, /*backlog=*/1) != 0)
    {
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    set_nonblocking(listen_fd_);

    // Ask the socket what it actually bound. Identical to `port` when that was non-zero,
    // but with port 0 the kernel chose for us and this is the only way to find out.
    //
    // A failure here fails start(), rather than listening on a port we cannot name: port()
    // promises a non-zero value whenever state() != Off, and swallowing this would quietly
    // break that for every caller. Not reachable in practice on a freshly bound, freshly
    // listening fd, and there is no test for it -- a syscall that cannot be made to fail
    // from a unit test gets a stated invariant, not a pretend one.
    sockaddr_in bound{};
    socklen_t   bound_len = sizeof(bound);
    if (getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&bound), &bound_len) != 0)
    {
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    port_ = ntohs(bound.sin_port);

    state_ = LiveLinkState::Listening;
    return true;
}

void LiveLinkServer::stop()
{
    if (peer_fd_ >= 0) { close(peer_fd_); peer_fd_ = -1; }
    if (listen_fd_ >= 0) { close(listen_fd_); listen_fd_ = -1; }
    port_ = 0;
    recv_buffer_.clear();
    state_ = LiveLinkState::Off;
}

std::vector<std::string> LiveLinkServer::poll()
{
    std::vector<std::string> out;
    if (state_ == LiveLinkState::Off) { return out; }

    // A new connection always replaces any prior peer.
    const int accepted = accept(listen_fd_, nullptr, nullptr);
    if (accepted >= 0)
    {
        if (peer_fd_ >= 0) { close(peer_fd_); }
        peer_fd_ = accepted;
        set_nonblocking(peer_fd_);
        recv_buffer_.clear();
        state_ = LiveLinkState::Connected;
    }

    if (peer_fd_ < 0) { return out; }

    char buf[4096];
    for (;;)
    {
        const ssize_t n = recv(peer_fd_, buf, sizeof(buf), 0);
        if (n > 0)
        {
            recv_buffer_.append(buf, static_cast<std::size_t>(n));
            continue;
        }
        if (n == 0 || (errno != EWOULDBLOCK && errno != EAGAIN))
        {
            close(peer_fd_);
            peer_fd_ = -1;
            recv_buffer_.clear();
            state_ = LiveLinkState::Listening;
        }
        break;
    }

    std::size_t start = 0;
    for (;;)
    {
        const std::size_t nl = recv_buffer_.find('\n', start);
        if (nl == std::string::npos) { break; }
        out.push_back(recv_buffer_.substr(start, nl - start));
        start = nl + 1;
    }
    recv_buffer_.erase(0, start);

    return out;
}

void LiveLinkServer::send(const std::string& line)
{
    if (peer_fd_ < 0) { return; }
    const std::string with_newline = line + "\n";
    ::send(peer_fd_, with_newline.data(), with_newline.size(), MSG_NOSIGNAL);
}
} // namespace mosaic::studio
