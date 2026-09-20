#include "TestCheck.h"

#include <JBro/Network/Internal/ReliableEndpoint.h>
#include <JBro/Network/Internal/UdpDatagram.h>
#include <JBro/Network/Testing/ManualClock.h>
#include <JBro/Network/Testing/MemorySocketProvider.h>
#include <JBro/Network/Transport.h>

#include <cstring>
#include <iostream>

using namespace JBro::Network;
using namespace JBro::Network::Testing;

namespace
{
    // 서버·클라이언트 한 쌍. UDP 가 양쪽에서 준비될 때까지 돌려 놓는다.
    struct Pair
    {
        MemorySocketProvider provider;
        ManualClock clock;
        Transport server;
        Transport client;
        ConnectionId clientOnServer = InvalidConnectionId;
        std::uint32_t rounds = 0;

        explicit Pair(const LossyConfig* lossy = nullptr, const TransportConfig& serverConfig = {},
            const TransportConfig& clientConfig = {}, bool clientHasUdp = true)
            : server(provider, clock, serverConfig)
            , client(provider, clock, clientConfig)
        {
            if (nullptr != lossy)
            {
                provider.SetLossy(*lossy);
            }
            Check(server.Listen(4000), "listen");
            // 서버는 이미 UDP 소켓을 잡았다. 클라이언트는 토큰이 올 때 소켓을 만드므로 그때까지 이 상태가 유지돼야 한다.
            provider.SetDatagramAvailable(clientHasUdp);
            Check(client.Connect("memory", 4000), "connect");
        }

        void Round(double milliseconds = 5.0)
        {
            server.Update();
            client.Update();
            clock.Advance(milliseconds);
            ++rounds;
        }

        void ConnectBoth()
        {
            for (int round = 0; round < 40 && InvalidConnectionId == clientOnServer; ++round)
            {
                Round();
                if (server.GetConnectionCount() > 0
                    && server.GetConnectionState(server.GetConnectionAt(0)) == ConnectionState::Connected
                    && client.GetConnectionState(ServerConnectionId) == ConnectionState::Connected)
                {
                    clientOnServer = server.GetConnectionAt(0);
                }
            }
            Check(InvalidConnectionId != clientOnServer, "both sides connect");
            NetworkEvent events[8];
            server.TakeEvents(events, 8);
            client.TakeEvents(events, 8);
        }

        void WaitForUdp()
        {
            ReliableDiagnostics serverSide;
            ReliableDiagnostics clientSide;
            // 유실 아래에서는 첫 punch 가 사라질 수 있다. 되풀이 punch(100ms) 가 몇 번 돌 시간을 준다.
            for (int round = 0; round < 400; ++round)
            {
                Round();
                server.GetReliableDiagnostics(clientOnServer, serverSide);
                client.GetReliableDiagnostics(ServerConnectionId, clientSide);
                if (serverSide.route == OrderedRoute::Udp && clientSide.route == OrderedRoute::Udp)
                {
                    return;
                }
            }
            Check(false, "UDP becomes the ordered route on both sides");
        }
    };

    // **데이터그램 헤더는 플래그 조합마다 왕복하고 잘린 버퍼를 거절한다.**
    // 상대가 보낸 바이트를 믿지 않는 자리들. 여기 있는 것은 정상 상대라면 만들지 않는 데이터그램이다.
    struct CollectingReceiver final : public IReliableReceiver
    {
        std::uint32_t count = 0;
        std::uint32_t lastSize = 0;
        std::uint8_t last[UdpProto::MaxPayloadBytes * 4] = {};

        void Deliver(NetChannel channel, MessageId messageId, const std::uint8_t* payload, std::uint32_t size) override
        {
            (void)channel;
            (void)messageId;
            ++count;
            lastSize = size;
            if (size <= sizeof(last))
            {
                std::memcpy(last, payload, size);
            }
        }
    };

    UdpProto::DatagramHeader FragmentHeader(std::uint32_t seq, std::uint32_t msgSeq, std::uint16_t index, std::uint16_t count)
    {
        UdpProto::DatagramHeader header;
        header.flags = UdpProto::FlagReliable | UdpProto::FlagFragment;
        header.channel = NetChannel::ReliableOrdered;
        header.seq = seq;
        header.msgSeq = msgSeq;
        header.fragIndex = index;
        header.fragCount = count;
        header.msgId = 21;
        return header;
    }

    // **모르는 채널 바이트는 데이터그램째로 버린다.** 막지 않으면 범위 밖 enum 이 `MessageView::channel` 로 게임까지 간다.
    void TestUnknownChannelIsRejected()
    {
        std::uint8_t buffer[64];
        UdpProto::DatagramHeader header;
        header.channel = NetChannel::Unreliable;
        header.seq = 1;
        header.msgId = 7;
        const std::uint8_t body[] = { 1, 2, 3 };
        const std::uint32_t written = UdpProto::Encode(header, body, sizeof(body), buffer);
        Check(written > 0, "a well formed datagram encodes");

        UdpProto::DatagramHeader parsed;
        const std::uint8_t* payload = nullptr;
        std::uint32_t payloadSize = 0;
        Check(UdpProto::Decode(buffer, written, parsed, payload, payloadSize), "and decodes");

        // 채널 바이트는 토큰 8 바이트와 플래그 1 바이트 뒤다.
        buffer[9] = static_cast<std::uint8_t>(NetChannelCount);
        Check(false == UdpProto::Decode(buffer, written, parsed, payload, payloadSize), "an unknown channel is refused");
        buffer[9] = 0xFF;
        Check(false == UdpProto::Decode(buffer, written, parsed, payload, payloadSize), "and so is any other stray value");
    }

    // **마지막이 아닌 조각이 꽉 차 있지 않으면 받지 않는다.** 받아들이면 조립 버퍼 가운데가 비고, 슬롯을 다시 쓸 때
    // `have` 만 지우므로 그 자리에 앞 메시지의 바이트가 남은 채 위로 올라간다.
    void TestShortMiddleFragmentIsRefused()
    {
        ReliableConfig config;
        ReliableEndpoint endpoint;
        endpoint.Reset(config);
        CollectingReceiver receiver;

        // 먼저 온전한 두 조각 메시지 하나를 보내 슬롯에 0xAB 를 채운다.
        std::uint8_t full[UdpProto::MaxPayloadBytes];
        std::memset(full, 0xAB, sizeof(full));
        endpoint.OnReliableReceived(FragmentHeader(0, 1, 0, 2), full, sizeof(full), 0.0, receiver);
        endpoint.OnReliableReceived(FragmentHeader(1, 1, 1, 2), full, 16, 0.0, receiver);
        Check(receiver.count == 1, "the whole message arrives");
        Check(receiver.lastSize == UdpProto::MaxPayloadBytes + 16, "with the size its fragments say");

        // 이제 짧은 중간 조각으로 같은 짓을 시도한다.
        std::uint8_t shortPayload[16];
        std::memset(shortPayload, 0x11, sizeof(shortPayload));
        endpoint.OnReliableReceived(FragmentHeader(2, 2, 0, 2), shortPayload, sizeof(shortPayload), 0.0, receiver);
        endpoint.OnReliableReceived(FragmentHeader(3, 2, 1, 2), shortPayload, sizeof(shortPayload), 0.0, receiver);
        Check(receiver.count == 1, "nothing is delivered - the short middle fragment was refused");
    }

    // **순번은 32 비트를 한 바퀴 돌아도 이어진다.** 부호 있는 거리로 비교하지 않으면 랩 뒤의 모든 것이 중복으로 보여 멈춘다.
    void TestSequenceNumbersSurviveWraparound()
    {
        ReliableConfig config;
        ReliableEndpoint endpoint;
        endpoint.Reset(config);
        // 랩 직전으로 순번 공간을 옮긴다.
        const std::uint32_t start = 0xFFFFFFFEu;
        endpoint.SetSequenceOriginForTests(start);
        CollectingReceiver receiver;

        UdpProto::DatagramHeader header;
        header.flags = UdpProto::FlagReliable;
        header.channel = NetChannel::ReliableOrdered;
        header.msgId = 9;
        const std::uint8_t body[] = { 0x5A };
        header.seq = start;
        endpoint.OnReliableReceived(header, body, sizeof(body), 0.0, receiver);
        Check(receiver.count == 1, "the first one arrives");

        // 랩을 건너는 순번이 **역전되어** 온다. 부호 없는 비교로는 0 이 0xFFFFFFFF 보다 작아 보여 지난 것으로 버려진다.
        header.seq = start + 2;
        endpoint.OnReliableReceived(header, body, sizeof(body), 0.0, receiver);
        Check(receiver.count == 1, "the one after the wrap waits for the gap before it");
        header.seq = start + 1;
        endpoint.OnReliableReceived(header, body, sizeof(body), 0.0, receiver);
        Check(receiver.count == 3, "and when the gap fills, both come up in order");

        for (std::uint32_t step = 3; step < 6; ++step)
        {
            header.seq = start + step;
            endpoint.OnReliableReceived(header, body, sizeof(body), 0.0, receiver);
        }
        Check(receiver.count == 6, "the sequence keeps running past the wrap");
    }

    void TestDatagramCodec()
    {
        UdpProto::DatagramHeader header;
        header.token = 0x0123456789ABCDEFull;
        header.flags = UdpProto::FlagReliable | UdpProto::FlagAck | UdpProto::FlagFragment;
        header.channel = NetChannel::ReliableUnordered;
        header.seq = 77;
        header.ackBase = 70;
        header.ackBits = 0xF0F0;
        header.msgSeq = 9;
        header.fragIndex = 2;
        header.fragCount = 5;
        header.msgId = 321;
        const std::uint8_t payload[3] = { 1, 2, 3 };
        std::uint8_t wire[UdpProto::MaxDatagramBytes];
        const std::uint32_t written = UdpProto::Encode(header, payload, 3, wire);
        Check(written == UdpProto::HeaderBytes(header.flags) + 3, "the size is the header for these flags plus the payload");
        Check(written == 14 + 8 + 8 + 2 + 3, "which is 35 bytes here");

        UdpProto::DatagramHeader decoded;
        const std::uint8_t* body = nullptr;
        std::uint32_t bodySize = 0;
        Check(UdpProto::Decode(wire, written, decoded, body, bodySize), "it decodes");
        Check(decoded.token == header.token && decoded.flags == header.flags && decoded.channel == header.channel
                && decoded.seq == 77 && decoded.ackBase == 70 && decoded.ackBits == 0xF0F0 && decoded.msgSeq == 9
                && decoded.fragIndex == 2 && decoded.fragCount == 5 && decoded.msgId == 321,
            "with every field intact");
        Check(bodySize == 3 && 0 == std::memcmp(body, payload, 3), "and the payload in place");
        Check(false == UdpProto::Decode(wire, written - 4, decoded, body, bodySize) || bodySize < 3,
            "a truncated buffer decodes short or fails");
        Check(false == UdpProto::Decode(wire, 10, decoded, body, bodySize), "less than the prefix fails");

        UdpProto::DatagramHeader plain;
        plain.msgId = 5;
        const std::uint32_t plainSize = UdpProto::Encode(plain, nullptr, 0, wire);
        Check(plainSize == 16, "an unreliable datagram carries only the 14 byte prefix and the message id");
    }

    // **UDP 가 준비되면 양쪽의 순서 보장 전송로가 UDP 가 되고, 비신뢰 메시지는 비신뢰 채널로 도착한다.**
    void TestUdpBecomesTheRoute()
    {
        Pair pair;
        pair.ConnectBoth();
        pair.WaitForUdp();
        ReliableDiagnostics diagnostics;
        Check(pair.client.GetReliableDiagnostics(ServerConnectionId, diagnostics), "diagnostics exist");
        Check(diagnostics.udpReady, "the client knows the server's endpoint and token");
        Check(pair.server.GetReliableDiagnostics(pair.clientOnServer, diagnostics) && diagnostics.udpReady,
            "the server learned the client's endpoint from the punch");

        const std::uint32_t value = 42;
        Check(pair.client.Send(ServerConnectionId, 3, &value, sizeof(value), NetChannel::Unreliable), "send unreliable");
        Check(pair.client.Send(ServerConnectionId, 4, &value, sizeof(value), NetChannel::UnreliableSequenced), "send sequenced");
        Check(pair.client.Send(ServerConnectionId, 5, &value, sizeof(value), NetChannel::ReliableUnordered), "send unordered");
        Check(pair.client.Send(ServerConnectionId, 6, &value, sizeof(value), NetChannel::ReliableOrdered), "send ordered");
        for (int round = 0; round < 4; ++round)
        {
            pair.Round();
        }
        MessageView views[8];
        const std::uint32_t got = pair.server.TakeMessages(views, 8);
        Check(got == 4, "all four arrive");
        bool sawUnreliable = false;
        bool sawSequenced = false;
        bool sawUnordered = false;
        bool sawOrdered = false;
        for (std::uint32_t index = 0; index < got; ++index)
        {
            sawUnreliable = sawUnreliable || (views[index].messageId == 3 && views[index].channel == NetChannel::Unreliable);
            sawSequenced = sawSequenced || (views[index].messageId == 4 && views[index].channel == NetChannel::UnreliableSequenced);
            sawUnordered = sawUnordered || (views[index].messageId == 5 && views[index].channel == NetChannel::ReliableUnordered);
            sawOrdered = sawOrdered || (views[index].messageId == 6 && views[index].channel == NetChannel::ReliableOrdered);
        }
        Check(sawUnreliable && sawSequenced && sawUnordered && sawOrdered, "each on the channel it was sent on");
        Check(pair.server.GetUdpLossRate(pair.clientOnServer) == 0.0, "no loss on a clean wire");
    }

    struct OrderedOutcome
    {
        std::uint32_t rounds = 0;
        std::uint32_t dropped = 0;
        std::uint32_t duplicated = 0;
        std::uint32_t reordered = 0;
    };

    // 유실·중복·재정렬 아래에서 `count` 개의 순서 보장 메시지를 보내고, 순서대로 정확히 한 번 도착하는지 본다.
    OrderedOutcome RunOrderedUnderLoss(const LossyConfig& lossy, std::uint32_t count)
    {
        Pair pair(&lossy);
        pair.ConnectBoth();
        pair.WaitForUdp();
        std::uint32_t sent = 0;
        std::uint32_t expected = 0;
        MessageView views[64];
        for (std::uint32_t round = 0; round < 40000 && expected < count; ++round)
        {
            while (sent < count)
            {
                if (false == pair.client.Send(ServerConnectionId, 1, &sent, sizeof(sent)))
                {
                    break;
                }
                ++sent;
            }
            pair.Round();
            const std::uint32_t got = pair.server.TakeMessages(views, 64);
            for (std::uint32_t index = 0; index < got; ++index)
            {
                std::uint32_t value = 0;
                std::memcpy(&value, views[index].data, sizeof(value));
                Check(views[index].channel == NetChannel::ReliableOrdered, "ordered arrives as ordered");
                Check(value == expected, "ordered messages arrive in order, exactly once, with nothing skipped");
                ++expected;
            }
        }
        if (expected != count)
        {
            ReliableDiagnostics client;
            ReliableDiagnostics server;
            pair.client.GetReliableDiagnostics(ServerConnectionId, client);
            pair.server.GetReliableDiagnostics(pair.clientOnServer, server);
            std::cout << "stall: sent=" << sent << " expected=" << expected << " rounds=" << pair.rounds
                << " client{unacked=" << client.unacked << " queued=" << client.queued << " cwnd=" << client.congestionWindow
                << " rto=" << client.rtoMilliseconds << " srtt=" << client.smoothedRttMilliseconds << " route=" << static_cast<int>(client.route)
                << "} server{unacked=" << server.unacked << " queued=" << server.queued << " piggy=" << server.piggybackAcks
                << " standalone=" << server.standaloneAcks << "}\n";
        }
        Check(expected == count, "every ordered message arrived despite loss");
        OrderedOutcome outcome;
        outcome.rounds = pair.rounds;
        // 소켓 0 은 서버, 1 은 클라이언트다. 보내는 쪽은 클라이언트다.
        LossyDatagramSocket* clientSocket = pair.provider.GetLossySocketAt(1);
        Check(nullptr != clientSocket, "the client's datagram socket is the lossy one");
        outcome.dropped = clientSocket->DroppedCount();
        outcome.duplicated = clientSocket->DuplicatedCount();
        outcome.reordered = clientSocket->ReorderedCount();
        return outcome;
    }

    // **30% 유실·10% 중복·깊이 4 재정렬 아래에서 순서 보장 메시지가 순서대로 정확히 한 번 도착한다.**
    // 그리고 같은 시드는 같은 라운드 수로 끝난다 - 결정론이 있어야 실패를 재현할 수 있다.
    void TestOrderedSurvivesLossDeterministically()
    {
        LossyConfig lossy;
        lossy.lossRate = 0.30;
        lossy.duplicateRate = 0.10;
        lossy.reorderDepth = 4;
        lossy.seed = 7;
        const OrderedOutcome first = RunOrderedUnderLoss(lossy, 300);
        Check(first.dropped > 0, "the wire actually dropped datagrams");
        Check(first.duplicated > 0, "and duplicated some");
        Check(first.reordered > 0, "and reordered some");
        const OrderedOutcome second = RunOrderedUnderLoss(lossy, 300);
        Check(first.rounds == second.rounds && first.dropped == second.dropped, "the same seed gives the same run");
        lossy.seed = 8;
        const OrderedOutcome third = RunOrderedUnderLoss(lossy, 300);
        Check(third.dropped != first.dropped || third.rounds != first.rounds, "a different seed gives a different run");
    }

    // **순서 무관 신뢰는 유실 아래에서 정확히 한 번씩, 순서는 상관없이 전부 도착한다.**
    void TestUnorderedIsExactlyOnce()
    {
        LossyConfig lossy;
        lossy.lossRate = 0.25;
        lossy.duplicateRate = 0.15;
        lossy.reorderDepth = 3;
        lossy.seed = 11;
        Pair pair(&lossy);
        pair.ConnectBoth();
        pair.WaitForUdp();
        constexpr std::uint32_t Count = 200;
        std::uint8_t seen[Count] = {};
        std::uint32_t sent = 0;
        std::uint32_t received = 0;
        bool outOfOrder = false;
        std::uint32_t last = 0;
        MessageView views[64];
        for (std::uint32_t round = 0; round < 40000 && received < Count; ++round)
        {
            while (sent < Count && pair.client.Send(ServerConnectionId, 2, &sent, sizeof(sent), NetChannel::ReliableUnordered))
            {
                ++sent;
            }
            pair.Round();
            const std::uint32_t got = pair.server.TakeMessages(views, 64);
            for (std::uint32_t index = 0; index < got; ++index)
            {
                std::uint32_t value = 0;
                std::memcpy(&value, views[index].data, sizeof(value));
                Check(value < Count, "values are the ones sent");
                Check(0 == seen[value], "no message arrives twice");
                seen[value] = 1;
                if (received > 0 && value < last)
                {
                    outOfOrder = true;
                }
                last = value;
                ++received;
            }
        }
        Check(received == Count, "all unordered messages arrived");
        Check(outOfOrder, "and some arrived out of order - unordered does not wait for gaps");
    }

    // **큰 메시지는 조각으로 나뉘어 유실 아래에서 재조립되고, 순서 보장이면 순서도 지킨다.**
    void TestFragmentsReassembleUnderLoss()
    {
        LossyConfig lossy;
        lossy.lossRate = 0.20;
        lossy.duplicateRate = 0.05;
        lossy.reorderDepth = 6;
        lossy.seed = 3;
        Pair pair(&lossy);
        pair.ConnectBoth();
        pair.WaitForUdp();
        constexpr std::uint32_t Size = 20000;
        std::uint8_t payload[Size];
        for (std::uint32_t index = 0; index < Size; ++index)
        {
            payload[index] = static_cast<std::uint8_t>(index * 31 + 7);
        }
        std::uint32_t sent = 0;
        std::uint32_t received = 0;
        MessageView view;
        for (std::uint32_t round = 0; round < 40000 && received < 3; ++round)
        {
            while (sent < 3)
            {
                payload[0] = static_cast<std::uint8_t>(sent);
                if (false == pair.client.Send(ServerConnectionId, 9, payload, Size))
                {
                    break;
                }
                ++sent;
            }
            pair.Round();
            while (pair.server.TakeMessages(&view, 1) == 1)
            {
                Check(view.size == Size, "a fragmented message arrives whole");
                Check(view.data[0] == received, "in order");
                Check(0 == std::memcmp(view.data + 1, payload + 1, Size - 1), "with every byte intact");
                ++received;
            }
        }
        Check(received == 3, "all three large messages arrived");
    }

    // **혼잡 창이 인플라이트를 묶고, 인플라이트 순번 범위는 선택 ack 창(32) 안에 머문다.** 그래야 창 밖 순번이 영원히
    // 재전송되는 일이 없다. 큐에 쌓인 것은 창이 열리는 대로 나간다.
    void TestCongestionWindowBoundsInflight()
    {
        Pair pair;
        pair.ConnectBoth();
        pair.WaitForUdp();
        std::uint32_t sent = 0;
        while (sent < 500 && pair.client.Send(ServerConnectionId, 1, &sent, sizeof(sent)))
        {
            ++sent;
        }
        Check(sent == 500, "the send queue takes 500 small messages");
        ReliableDiagnostics diagnostics;
        Check(pair.client.GetReliableDiagnostics(ServerConnectionId, diagnostics), "diagnostics");
        Check(diagnostics.unacked <= diagnostics.congestionWindow, "in flight never exceeds the congestion window");
        Check(diagnostics.unacked <= ReliableEndpoint::AckWindow, "nor the ack window");
        Check(diagnostics.queued > 0, "the rest waits in the queue");
        std::uint32_t received = 0;
        std::uint32_t expected = 0;
        MessageView views[64];
        std::uint32_t largestWindow = 0;
        for (std::uint32_t round = 0; round < 4000 && received < 500; ++round)
        {
            pair.Round();
            pair.client.GetReliableDiagnostics(ServerConnectionId, diagnostics);
            Check(diagnostics.unacked <= ReliableEndpoint::AckWindow, "the ack window holds throughout");
            if (diagnostics.congestionWindow > largestWindow)
            {
                largestWindow = diagnostics.congestionWindow;
            }
            const std::uint32_t got = pair.server.TakeMessages(views, 64);
            for (std::uint32_t index = 0; index < got; ++index)
            {
                std::uint32_t value = 0;
                std::memcpy(&value, views[index].data, sizeof(value));
                Check(value == expected, "in order");
                ++expected;
                ++received;
            }
        }
        Check(received == 500, "everything drains");
        Check(largestWindow > 16, "the window grew on a clean wire");
        Check(diagnostics.smoothedRttMilliseconds > 0.0, "and the RTT was measured");
    }

    // **비신뢰는 복구하지 않는다 - 잃은 만큼 손실률에 보이고, Sequenced 는 역전된 것을 버린다.**
    void TestUnreliableLossIsMeasuredAndSequencedDropsStale()
    {
        LossyConfig lossy;
        lossy.lossRate = 0.30;
        lossy.duplicateRate = 0.0;
        lossy.reorderDepth = 5;
        lossy.seed = 5;
        Pair pair(&lossy);
        pair.ConnectBoth();
        pair.WaitForUdp();
        // 1) 순수 비신뢰: 잃은 것은 그대로 잃고, 도착률은 주입한 유실률 근처다.
        std::uint32_t plainReceived = 0;
        MessageView views[64];
        for (std::uint32_t value = 0; value < 500; ++value)
        {
            pair.client.Send(ServerConnectionId, 7, &value, sizeof(value), NetChannel::Unreliable);
            pair.Round();
            plainReceived += pair.server.TakeMessages(views, 64);
        }
        for (int round = 0; round < 8; ++round)
        {
            pair.Round();
            plainReceived += pair.server.TakeMessages(views, 64);
        }
        Check(plainReceived < 500, "unreliable messages were lost and not recovered");
        Check(plainReceived > 250 && plainReceived < 450, "about 70% got through, as the wire allows");
        const double loss = pair.server.GetUdpLossRate(pair.clientOnServer);
        Check(loss > 0.15 && loss < 0.45, "the measured loss rate is near the injected 30%");

        // 2) Sequenced: 재정렬로 뒤늦게 온 것은 버려져, 보이는 값은 언제나 커진다.
        std::uint32_t sequencedReceived = 0;
        std::uint32_t lastSequenced = 0;
        bool sequencedMonotonic = true;
        for (std::uint32_t value = 0; value < 500; ++value)
        {
            pair.client.Send(ServerConnectionId, 8, &value, sizeof(value), NetChannel::UnreliableSequenced);
            pair.Round();
            const std::uint32_t got = pair.server.TakeMessages(views, 64);
            for (std::uint32_t index = 0; index < got; ++index)
            {
                // 1) 의 마지막 몇 개가 재정렬 큐에 남아 있다가 지금 나온다. 이 채널의 메시지만 본다.
                if (views[index].messageId != 8)
                {
                    continue;
                }
                std::uint32_t seen = 0;
                std::memcpy(&seen, views[index].data, sizeof(seen));
                if (sequencedReceived > 0 && seen <= lastSequenced)
                {
                    sequencedMonotonic = false;
                }
                lastSequenced = seen;
                ++sequencedReceived;
            }
        }
        Check(sequencedReceived > 0 && sequencedReceived < 500, "sequenced also loses, and drops stale ones on top");
        Check(sequencedMonotonic, "sequenced never delivers a stale value");
    }

    // **UDP 가 없는 클라이언트(웹)는 기다리지 않고 WS 로 확정하고, 서버는 기다림이 끝나면 WS 로 확정한다.**
    // 확정 전에 보낸 순서 보장 메시지는 백로그에 있다가 순서대로 나간다.
    void TestWebLikeClientFallsBackToWebSocket()
    {
        Pair pair(nullptr, {}, {}, false);
        pair.ConnectBoth();
        ReliableDiagnostics clientSide;
        for (int round = 0; round < 4; ++round)
        {
            pair.Round();
        }
        Check(pair.client.GetReliableDiagnostics(ServerConnectionId, clientSide), "client diagnostics");
        Check(clientSide.route == OrderedRoute::WebSocket, "the client chose WS as soon as it found no UDP");
        ReliableDiagnostics serverSide;
        pair.server.GetReliableDiagnostics(pair.clientOnServer, serverSide);
        Check(serverSide.route == OrderedRoute::Undecided, "the server is still waiting for a punch");

        const std::uint32_t values[3] = { 10, 20, 30 };
        for (std::uint32_t value : values)
        {
            Check(pair.server.Send(pair.clientOnServer, 1, &value, sizeof(value)), "the server queues ordered messages");
        }
        MessageView views[8];
        Check(pair.client.TakeMessages(views, 8) == 0, "nothing arrives while the route is undecided");
        pair.clock.Advance(2001.0);
        for (int round = 0; round < 4; ++round)
        {
            pair.Round();
        }
        pair.server.GetReliableDiagnostics(pair.clientOnServer, serverSide);
        Check(serverSide.route == OrderedRoute::WebSocket, "after the wait the server settles on WS");
        const std::uint32_t got = pair.client.TakeMessages(views, 8);
        Check(got == 3, "the backlog is flushed");
        for (std::uint32_t index = 0; index < got; ++index)
        {
            std::uint32_t value = 0;
            std::memcpy(&value, views[index].data, sizeof(value));
            Check(value == values[index], "in the order it was queued");
            Check(views[index].channel == NetChannel::ReliableOrdered, "over the reliable WS channel");
        }
        Check(pair.client.GetUdpLossRate(ServerConnectionId) < 0.0, "there is no UDP loss to measure");
    }

    // **서버가 UDP 를 끄면 토큰이 없고, 클라이언트는 기다림 뒤 WS 로 간다.**
    void TestServerWithoutUdpUsesWebSocket()
    {
        TransportConfig noUdp;
        noUdp.udpEnabled = false;
        Pair pair(nullptr, noUdp, {});
        pair.ConnectBoth();
        const std::uint32_t value = 99;
        Check(pair.client.Send(ServerConnectionId, 1, &value, sizeof(value)), "the client queues an ordered message");
        MessageView view;
        for (int round = 0; round < 4; ++round)
        {
            pair.Round();
        }
        Check(pair.server.TakeMessages(&view, 1) == 0, "it waits for the route");
        pair.clock.Advance(2001.0);
        for (int round = 0; round < 4; ++round)
        {
            pair.Round();
        }
        Check(pair.server.TakeMessages(&view, 1) == 1 && view.messageId == 1, "then it arrives over WS");
        ReliableDiagnostics diagnostics;
        pair.client.GetReliableDiagnostics(ServerConnectionId, diagnostics);
        Check(diagnostics.route == OrderedRoute::WebSocket && false == diagnostics.udpReady, "the client settled on WS");
        Check(pair.server.Send(pair.clientOnServer, 2, &value, sizeof(value), NetChannel::Unreliable), "the server sends unreliable");
        for (int round = 0; round < 4; ++round)
        {
            pair.Round();
        }
        Check(pair.client.TakeMessages(&view, 1) == 1 && view.channel == NetChannel::ReliableOrdered,
            "which travels over WS and so arrives as reliable ordered");
    }
}

int RunReliableUdpTests()
{
    try
    {
        TestDatagramCodec();
        TestUnknownChannelIsRejected();
        TestShortMiddleFragmentIsRefused();
        TestSequenceNumbersSurviveWraparound();
        TestUdpBecomesTheRoute();
        TestOrderedSurvivesLossDeterministically();
        TestUnorderedIsExactlyOnce();
        TestFragmentsReassembleUnderLoss();
        TestCongestionWindowBoundsInflight();
        TestUnreliableLossIsMeasuredAndSequencedDropsStale();
        TestWebLikeClientFallsBackToWebSocket();
        TestServerWithoutUdpUsesWebSocket();
    }
    catch (const std::exception& error)
    {
        std::cout << "ReliableUdpTests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "ReliableUdpTests passed\n";
    return 0;
}
