#include "TestCheck.h"

#include <JBro/Network/Peer/Signaling.h>
#include <JBro/Network/Testing/ManualClock.h>
#include <JBro/Network/Testing/MemorySocketProvider.h>
#include <JBro/Network/Transport.h>

#include <cstring>
#include <iostream>

using namespace JBro::Network;
using namespace JBro::Network::Testing;

namespace
{
    // 시그널링 서버 없이 시그널을 손으로 옮긴다. 트랜스포트의 피어 경로만 본다.
    void RelaySignals(Transport& a, ConnectionId aId, Transport& b, ConnectionId bId)
    {
        std::uint8_t signal[64];
        std::uint32_t size = 0;
        while ((size = a.TakePeerSignal(aId, signal, sizeof(signal))) > 0)
        {
            Check(b.PushPeerSignal(bId, signal, size), "the peer accepts the signal");
        }
        while ((size = b.TakePeerSignal(bId, signal, sizeof(signal))) > 0)
        {
            Check(a.PushPeerSignal(aId, signal, size), "the peer accepts the signal");
        }
    }

    // **피어형 연결: 시그널이 오가면 채널 넷이 열리고, 메시지가 채널 규율대로 오간다.** UDP 도 신뢰 엔진도 없이 데이터 채널이 지킨다.
    void TestPeersConnectAndExchangeOnFourChannels()
    {
        MemorySocketProvider provider;
        ManualClock clock;
        Transport host(provider, clock);
        Transport client(provider, clock);
        Check(host.HostPeers(), "the host takes the server role without listening on a socket");
        Check(host.IsListening(), "and counts as listening");
        const ConnectionId guest = host.AcceptPeer();
        Check(InvalidConnectionId != guest, "the host makes a peer that waits for an offer");
        Check(client.ConnectPeer(), "the client makes the offering peer");
        Check(host.GetConnectionKind(guest) == ConnectionKind::Peer && client.GetConnectionKind(ServerConnectionId) == ConnectionKind::Peer,
            "both are peer connections");

        bool ready = false;
        for (int round = 0; round < 40 && false == ready; ++round)
        {
            RelaySignals(client, ServerConnectionId, host, guest);
            host.Update();
            client.Update();
            clock.Advance(5.0);
            ready = host.GetConnectionState(guest) == ConnectionState::Connected
                && client.GetConnectionState(ServerConnectionId) == ConnectionState::Connected;
        }
        Check(ready, "hello crosses the data channel and both sides are Connected");
        NetworkEvent events[8];
        std::uint32_t got = host.TakeEvents(events, 8);
        Check(got == 1 && events[0].kind == NetworkEventKind::Connected, "the host saw Connected");
        got = client.TakeEvents(events, 8);
        Check(got == 1 && events[0].kind == NetworkEventKind::Connected, "the client saw Connected");

        const NetChannel channels[4] = { NetChannel::ReliableOrdered, NetChannel::ReliableUnordered, NetChannel::Unreliable,
            NetChannel::UnreliableSequenced };
        for (std::uint32_t index = 0; index < 4; ++index)
        {
            Check(client.Send(ServerConnectionId, static_cast<MessageId>(10 + index), &index, sizeof(index), channels[index]),
                "the client sends on each channel");
            Check(host.Send(guest, static_cast<MessageId>(20 + index), &index, sizeof(index), channels[index]),
                "the host sends on each channel");
        }
        host.Update();
        client.Update();
        MessageView views[8];
        got = host.TakeMessages(views, 8);
        Check(got == 4, "the host receives four");
        for (std::uint32_t index = 0; index < got; ++index)
        {
            const std::uint32_t which = views[index].messageId - 10;
            Check(which < 4 && views[index].channel == channels[which], "each on the channel it was sent on");
        }
        got = client.TakeMessages(views, 8);
        Check(got == 4, "the client receives four");
        for (std::uint32_t index = 0; index < got; ++index)
        {
            const std::uint32_t which = views[index].messageId - 20;
            Check(which < 4 && views[index].channel == channels[which], "each on the channel it was sent on");
        }

        // 순서 보장 300 개는 순서대로 전부.
        for (std::uint32_t value = 0; value < 300; ++value)
        {
            Check(client.Send(ServerConnectionId, 1, &value, sizeof(value)), "ordered send");
        }
        std::uint32_t expected = 0;
        for (int round = 0; round < 20 && expected < 300; ++round)
        {
            host.Update();
            client.Update();
            while ((got = host.TakeMessages(views, 8)) > 0)
            {
                for (std::uint32_t index = 0; index < got; ++index)
                {
                    std::uint32_t value = 0;
                    std::memcpy(&value, views[index].data, sizeof(value));
                    Check(value == expected, "ordered arrives in order");
                    ++expected;
                }
            }
        }
        Check(expected == 300, "all three hundred arrived");

        // keepalive 도 데이터 채널로 간다.
        clock.Advance(1000.0);
        host.Update();
        client.Update();
        clock.Advance(15.0);
        host.Update();
        const double rtt = host.GetRoundTripMilliseconds(guest);
        Check(rtt > 14.9 && rtt < 15.1, "ping and pong ride the reliable channel");

        // 닫기는 상대에게 보인다.
        client.Close();
        client.TakeEvents(events, 8);
        host.Update();
        got = host.TakeEvents(events, 8);
        Check(got == 1 && events[0].kind == NetworkEventKind::Disconnected, "the host sees the peer leave");
        Check(host.GetConnectionCount() == 0, "and forgets it");
    }

    // **비신뢰 채널은 유실을 복구하지 않고 신뢰 채널은 전부 도착한다.** 인메모리 피어가 비신뢰 채널만 떨어뜨린다.
    void TestUnreliableChannelsLoseAndReliableDoNot()
    {
        MemorySocketProvider provider;
        LossyConfig lossy;
        lossy.lossRate = 0.3;
        lossy.seed = 9;
        provider.SetLossy(lossy);
        ManualClock clock;
        Transport host(provider, clock);
        Transport client(provider, clock);
        Check(host.HostPeers(), "host");
        const ConnectionId guest = host.AcceptPeer();
        Check(client.ConnectPeer(), "connect");
        for (int round = 0; round < 40; ++round)
        {
            RelaySignals(client, ServerConnectionId, host, guest);
            host.Update();
            client.Update();
            clock.Advance(5.0);
        }
        Check(host.GetConnectionState(guest) == ConnectionState::Connected, "connected");
        std::uint32_t reliable = 0;
        std::uint32_t unreliable = 0;
        MessageView views[16];
        for (std::uint32_t value = 0; value < 200; ++value)
        {
            client.Send(ServerConnectionId, 1, &value, sizeof(value), NetChannel::ReliableUnordered);
            client.Send(ServerConnectionId, 2, &value, sizeof(value), NetChannel::Unreliable);
            host.Update();
            client.Update();
            std::uint32_t got = 0;
            while ((got = host.TakeMessages(views, 16)) > 0)
            {
                for (std::uint32_t index = 0; index < got; ++index)
                {
                    if (views[index].messageId == 1)
                    {
                        ++reliable;
                    }
                    else if (views[index].messageId == 2)
                    {
                        ++unreliable;
                    }
                }
            }
        }
        Check(reliable == 200, "every reliable message arrived");
        Check(unreliable > 80 && unreliable < 190, "about 30% of unreliable messages were lost");
    }

    // 다섯 트랜스포트와 시그널링 서버·클라이언트 둘을 한 라운드 돌린다.
    struct Room
    {
        MemorySocketProvider provider;
        ManualClock clock;
        Transport signalingServer;
        Transport hostSignaling;
        Transport hostPeers;
        Transport guestSignaling;
        Transport guestPeers;
        SignalingServer server;
        SignalingClient host;
        SignalingClient guest;

        Room()
            : signalingServer(provider, clock)
            , hostSignaling(provider, clock)
            , hostPeers(provider, clock)
            , guestSignaling(provider, clock)
            , guestPeers(provider, clock)
            , server(signalingServer)
            , host(hostSignaling, hostPeers)
            , guest(guestSignaling, guestPeers)
        {
            Check(signalingServer.Listen(7000), "the signaling server listens");
            Check(hostSignaling.Connect("memory", 7000), "the host reaches the signaling server");
            Check(guestSignaling.Connect("memory", 7000), "the guest reaches the signaling server");
        }

        void Round()
        {
            signalingServer.Update();
            server.Update();
            hostSignaling.Update();
            hostPeers.Update();
            host.Update();
            guestSignaling.Update();
            guestPeers.Update();
            guest.Update();
            clock.Advance(5.0);
        }
    };

    // **시그널링 서버를 거쳐 두 클라이언트가 방에서 만나고, 첫 사람이 호스트가 되어 피어로 이어진다.** 게임 데이터는 시그널링을 지나지 않는다.
    void TestSignalingRoomPairsHostAndGuest()
    {
        Room room;
        room.host.Join(42);
        for (int round = 0; round < 30 && false == room.host.HasRole(); ++round)
        {
            room.Round();
        }
        Check(room.host.HasRole() && room.host.IsHost(), "the first joiner is the host");
        Check(room.hostPeers.GetRole() == NetworkRole::Server && room.hostPeers.IsListening(), "and its peer transport hosts");
        room.guest.Join(42);
        bool connected = false;
        for (int round = 0; round < 80 && false == connected; ++round)
        {
            room.Round();
            connected = room.hostPeers.GetConnectionCount() == 1
                && room.hostPeers.GetConnectionState(room.hostPeers.GetConnectionAt(0)) == ConnectionState::Connected
                && room.guestPeers.GetConnectionState(ServerConnectionId) == ConnectionState::Connected;
        }
        Check(connected, "the guest's peer connects to the host through relayed signals");
        Check(room.guest.HasRole() && false == room.guest.IsHost() && room.guest.GetPeerId() == 1, "the guest is peer 1");
        Check(room.host.FindPeerConnection(1) == room.hostPeers.GetConnectionAt(0), "the host maps peer 1 to that connection");
        Check(room.server.GetRoomCount() == 1 && room.server.GetMemberCount(42) == 1, "the server holds one room with one member");

        const std::uint32_t value = 77;
        Check(room.guestPeers.Send(ServerConnectionId, 5, &value, sizeof(value), NetChannel::Unreliable), "the guest sends over the peer");
        room.Round();
        MessageView view;
        Check(room.hostPeers.TakeMessages(&view, 1) == 1 && view.messageId == 5 && view.channel == NetChannel::Unreliable,
            "the host receives it on the peer transport");
        MessageView leaked;
        Check(room.signalingServer.TakeMessages(&leaked, 1) == 0, "and nothing of it went through the signaling server");

        // 참가자가 떠난다.
        room.guestSignaling.Close();
        room.guestPeers.Close();
        bool gone = false;
        for (int round = 0; round < 40 && false == gone; ++round)
        {
            room.Round();
            gone = room.hostPeers.GetConnectionCount() == 0 && room.server.GetMemberCount(42) == 0;
        }
        Check(gone, "the host's peer transport and the room forget the guest");
        Check(room.host.GetPeerCount() == 0, "and the host's signaling client drops the mapping");
        Check(room.server.GetRoomCount() == 1, "the room stays while the host is in it");
    }

    // **WebRTC 가 없는 플랫폼에서는 피어를 만들 수 없고 트랜스포트는 그대로 조용하다.**
    void TestPeersUnavailable()
    {
        MemorySocketProvider provider;
        provider.SetPeerAvailable(false);
        ManualClock clock;
        Transport host(provider, clock);
        Check(host.HostPeers(), "hosting itself needs no stack");
        Check(host.AcceptPeer() == InvalidConnectionId, "but no peer can be made");
        Transport client(provider, clock);
        Check(false == client.ConnectPeer(), "nor can a client offer");
        Check(client.GetRole() == NetworkRole::None, "and the client keeps no role");
    }
}

int RunPeerTests()
{
    try
    {
        TestPeersConnectAndExchangeOnFourChannels();
        TestUnreliableChannelsLoseAndReliableDoNot();
        TestSignalingRoomPairsHostAndGuest();
        TestPeersUnavailable();
    }
    catch (const std::exception& error)
    {
        std::cout << "PeerTests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "PeerTests passed\n";
    return 0;
}
