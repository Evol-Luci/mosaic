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
        connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    }
    ~TestClient() { if (fd_ >= 0) { close(fd_); } }

    void send_text(const std::string& s) { send(fd_, s.data(), s.size(), 0); }
    void close_now() { close(fd_); fd_ = -1; }

private:
    int fd_ = -1;
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
    REQUIRE(server.start(34901));
    REQUIRE(server.state() == LiveLinkState::Listening);
    server.stop();
    REQUIRE(server.state() == LiveLinkState::Off);
}

TEST_CASE("LiveLinkServer transitions to Connected when a client connects", "[studio]")
{
    LiveLinkServer server;
    REQUIRE(server.start(34902));

    TestClient client(34902);
    poll_until(server, [&] { return server.state() == LiveLinkState::Connected; });

    REQUIRE(server.state() == LiveLinkState::Connected);
}

TEST_CASE("LiveLinkServer poll() returns a complete line sent in one write", "[studio]")
{
    LiveLinkServer server;
    REQUIRE(server.start(34903));
    TestClient client(34903);
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
    REQUIRE(server.start(34904));
    TestClient client(34904);
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
    REQUIRE(server.start(34905));
    TestClient client(34905);
    poll_until(server, [&] { return server.state() == LiveLinkState::Connected; });

    client.close_now();
    poll_until(server, [&] { return server.state() == LiveLinkState::Listening; });

    REQUIRE(server.state() == LiveLinkState::Listening);
}

TEST_CASE("LiveLinkServer start() fails on an already-bound port", "[studio]")
{
    LiveLinkServer server1;
    REQUIRE(server1.start(34906));
    LiveLinkServer server2;
    REQUIRE_FALSE(server2.start(34906));
}
