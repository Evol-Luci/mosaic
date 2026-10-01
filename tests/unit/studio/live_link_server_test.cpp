#include <chrono>
#include <cstring>
#include <thread>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <catch2/catch_test_macros.hpp>

#include "studio/live_link_server.hpp"

using mosaic::studio::LiveLinkServer;
using mosaic::studio::LiveLinkState;

namespace
{
// A minimal blocking client socket connected to 127.0.0.1:port, for test use only.
class TestClient
{
public:
    explicit TestClient(std::uint16_t port)
    {
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        connected_ = connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
    }

    // Whether connect() succeeded. Worth asserting rather than ignoring: send_text() on a
    // socket that never connected raises SIGPIPE and takes the whole test binary down with
    // it, so a failed connect used to surface as a bare exit 141 with no named assertion.
    // That is exactly how a hard-coded-port collision would have presented on CI.
    bool connected() const { return connected_; }
    ~TestClient() { if (fd_ >= 0) { close(fd_); } }

    void send_text(const std::string& s) { send(fd_, s.data(), s.size(), 0); }
    std::string recv_text()
    {
        char buf[4096];
        const ssize_t n = recv(fd_, buf, sizeof(buf), MSG_DONTWAIT);
        if (n <= 0) { return {}; }
        return std::string(buf, static_cast<std::size_t>(n));
    }
    void close_now() { close(fd_); fd_ = -1; }

private:
    int  fd_ = -1;
    bool connected_ = false;
};

// poll() is non-blocking; give the kernel a moment to deliver connect()/send()
// bytes on loopback before asserting. This is same-host TCP, not the cross-process
// real-UDP-over-a-real-network-stack delay networking N6/N7 found in this sandbox —
// no multi-second margin needed, but a short bounded retry keeps the test robust
// against scheduling jitter.
template <class Pred>
void poll_until(LiveLinkServer& server, Pred pred, std::vector<std::string>* out = nullptr,
                int max_attempts = 200, int sleep_ms = 5)
{
    for (int i = 0; i < max_attempts; ++i)
    {
        auto lines = server.poll();
        if (out != nullptr)
        {
            for (auto& l : lines) { out->push_back(std::move(l)); }
        }
        if (pred()) { return; }
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }
}
} // namespace

TEST_CASE("LiveLinkServer starts Off and start() moves it to Listening", "[studio]")
{
    LiveLinkServer server;
    REQUIRE(server.state() == LiveLinkState::Off);
    REQUIRE(server.start(0));
    REQUIRE(server.state() == LiveLinkState::Listening);
    server.stop();
    REQUIRE(server.state() == LiveLinkState::Off);
}

TEST_CASE("LiveLinkServer transitions to Connected when a client connects", "[studio]")
{
    LiveLinkServer server;
    REQUIRE(server.start(0));

    TestClient client(server.port());
    REQUIRE(client.connected());
    poll_until(server, [&] { return server.state() == LiveLinkState::Connected; });

    REQUIRE(server.state() == LiveLinkState::Connected);
}

TEST_CASE("LiveLinkServer poll() returns a complete line sent in one write", "[studio]")
{
    LiveLinkServer server;
    REQUIRE(server.start(0));
    TestClient client(server.port());
    REQUIRE(client.connected());
    poll_until(server, [&] { return server.state() == LiveLinkState::Connected; });

    client.send_text("hello\n");

    std::vector<std::string> received;
    poll_until(server, [&] { return !received.empty(); }, &received);

    REQUIRE(received.size() == 1);
    REQUIRE(received[0] == "hello");
}

TEST_CASE("LiveLinkServer buffers a line split across two sends", "[studio]")
{
    LiveLinkServer server;
    REQUIRE(server.start(0));
    TestClient client(server.port());
    REQUIRE(client.connected());
    poll_until(server, [&] { return server.state() == LiveLinkState::Connected; });

    client.send_text("hel");
    std::vector<std::string> received;
    poll_until(server, [&] { server.poll(); return true; }, nullptr, 20); // let the partial arrive
    client.send_text("lo\n");
    poll_until(server, [&] { return !received.empty(); }, &received);

    REQUIRE(received.size() == 1);
    REQUIRE(received[0] == "hello");
}

TEST_CASE("LiveLinkServer returns to Listening when the client disconnects", "[studio]")
{
    LiveLinkServer server;
    REQUIRE(server.start(0));
    TestClient client(server.port());
    REQUIRE(client.connected());
    poll_until(server, [&] { return server.state() == LiveLinkState::Connected; });

    client.close_now();
    poll_until(server, [&] { return server.state() == LiveLinkState::Listening; });

    REQUIRE(server.state() == LiveLinkState::Listening);
}

TEST_CASE("LiveLinkServer start() fails on an already-bound port", "[studio]")
{
    // Against a port we KNOW is bound, rather than hoping a hard-coded number is free --
    // which is how this very case failed a CI run on 2026-09-29 and passed on re-run.
    LiveLinkServer server1;
    REQUIRE(server1.start(0));
    LiveLinkServer server2;
    REQUIRE_FALSE(server2.start(server1.port()));
}

TEST_CASE("LiveLinkServer::send delivers a line to the connected client", "[studio]")
{
    LiveLinkServer server;
    REQUIRE(server.start(0));
    TestClient client(server.port());
    REQUIRE(client.connected());
    poll_until(server, [&] { return server.state() == LiveLinkState::Connected; });

    server.send("hello");

    std::string received;
    for (int i = 0; i < 200 && received.empty(); ++i)
    {
        received += client.recv_text();
        if (received.empty()) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    }
    REQUIRE(received == "hello\n");
}

TEST_CASE("LiveLinkServer::send is a silent no-op when there is no connected peer", "[studio]")
{
    LiveLinkServer server;
    server.send("hello"); // Off: must not throw or crash
    REQUIRE(server.start(0));
    server.send("hello"); // Listening, no peer yet: must not throw or crash
    REQUIRE(server.state() == LiveLinkState::Listening);
}

TEST_CASE("LiveLinkServer::port reports the kernel-assigned port after start(0)", "[studio]")
{
    // start(0) asks the kernel for an ephemeral port. Without port() there is no way to
    // learn which one, which is why every test in this file used to hard-code a number --
    // and why one of them failed a real CI run when 34906 was not free.
    LiveLinkServer server;
    REQUIRE(server.port() == 0); // Off: nothing is bound
    REQUIRE(server.start(0));
    REQUIRE(server.port() != 0);
    server.stop();
    REQUIRE(server.port() == 0); // stop() must not leave a stale port behind
}

TEST_CASE("LiveLinkServer::port reports an explicitly requested port verbatim", "[studio]")
{
    // Guards against port() being a function that merely returns something plausible:
    // with a port we chose, it must return that exact number and not the kernel's idea
    // of one. Bound via start(0) first so the number is known-free rather than hoped-free.
    LiveLinkServer probe;
    REQUIRE(probe.start(0));
    const std::uint16_t known_free = probe.port();
    probe.stop();

    LiveLinkServer server;
    REQUIRE(server.start(known_free));
    REQUIRE(server.port() == known_free);
}
