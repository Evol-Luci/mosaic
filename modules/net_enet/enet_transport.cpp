#include "modules/net_enet/enet_transport.hpp"

#include <atomic>
#include <unordered_map>

#include <enet/enet.h>

namespace mosaic::net::enet
{
using mosaic::engine::net::Channel;
using mosaic::engine::net::ConnectionId;
using mosaic::engine::net::NetEvent;
using mosaic::engine::net::Transport;

namespace
{
std::atomic<int> g_enet_refs{0};
void acquire_enet()
{
    if (g_enet_refs.fetch_add(1) == 0) { enet_initialize(); }
}
void release_enet()
{
    if (g_enet_refs.fetch_sub(1) == 1) { enet_deinitialize(); }
}
} // namespace

class EnetTransport : public Transport
{
public:
    explicit EnetTransport(ENetHost* host) : host_(host) {}
    ~EnetTransport() override
    {
        enet_host_destroy(host_);
        release_enet();
    }

    void send(ConnectionId to, Channel channel, const std::string& bytes) override
    {
        auto it = id_to_peer_.find(to);
        if (it == id_to_peer_.end()) { return; } // unknown/stale id: no-op, no crash
        const bool reliable = channel == Channel::Reliable;
        ENetPacket* packet = enet_packet_create(
            bytes.data(), bytes.size(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
        enet_peer_send(it->second, reliable ? 0 : 1, packet);
    }

    std::vector<NetEvent> poll() override
    {
        std::vector<NetEvent> out;
        ENetEvent ev;
        while (enet_host_service(host_, &ev, 0) > 0)
        {
            if (ev.type == ENET_EVENT_TYPE_CONNECT)
            {
                const ConnectionId id = next_id_++;
                peer_to_id_[ev.peer] = id;
                id_to_peer_[id] = ev.peer;
                NetEvent out_ev;
                out_ev.type = NetEvent::Type::Connected;
                out_ev.connection = id;
                out.push_back(out_ev);
            }
            else if (ev.type == ENET_EVENT_TYPE_RECEIVE)
            {
                auto it = peer_to_id_.find(ev.peer);
                if (it != peer_to_id_.end())
                {
                    NetEvent out_ev;
                    out_ev.type = NetEvent::Type::Message;
                    out_ev.connection = it->second;
                    out_ev.channel = ev.channelID == 0 ? Channel::Reliable : Channel::Unreliable;
                    out_ev.data.assign(reinterpret_cast<char*>(ev.packet->data),
                                       ev.packet->dataLength);
                    out.push_back(out_ev);
                }
                enet_packet_destroy(ev.packet);
            }
            // DISCONNECT handled in Task 3.
        }
        return out;
    }

    void disconnect(ConnectionId /*who*/) override
    {
        // Implemented in Task 3.
    }

private:
    ENetHost* host_;
    std::unordered_map<ENetPeer*, ConnectionId> peer_to_id_;
    std::unordered_map<ConnectionId, ENetPeer*> id_to_peer_;
    ConnectionId next_id_ = 1; // 0 is reserved invalid (transport.hpp)
};

std::unique_ptr<Transport> create_enet_server(std::uint16_t port, std::size_t max_peers)
{
    acquire_enet();
    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = port;
    ENetHost* host = enet_host_create(&address, max_peers, /*channelLimit=*/2, 0, 0);
    if (host == nullptr)
    {
        release_enet();
        return nullptr;
    }
    return std::make_unique<EnetTransport>(host);
}

std::unique_ptr<Transport> create_enet_client(const std::string& host_str, std::uint16_t port)
{
    acquire_enet();
    ENetHost* host = enet_host_create(nullptr, 1, /*channelLimit=*/2, 0, 0);
    if (host == nullptr)
    {
        release_enet();
        return nullptr;
    }
    ENetAddress address;
    enet_address_set_host(&address, host_str.c_str());
    address.port = port;
    enet_host_connect(host, &address, /*channelCount=*/2, 0);
    return std::make_unique<EnetTransport>(host);
}
} // namespace mosaic::net::enet
