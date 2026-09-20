#pragma once

#include <JBro/Network/Internal/ByteRing.h>
#include <JBro/Network/Internal/ReliableEndpoint.h>
#include <JBro/Network/Socket.h>
#include <JBro/Network/Types.h>
#include <JBro/Types/Table.h>

#include <cstdint>

namespace JBro::Network
{
    // 사용자 `ReliableOrdered` 의 전송로. 두 전송로를 섞으면 순서가 깨지므로 연결마다 한 번만 확정한다 -
    // 준비되면 UDP, 일정 시간 안에 못 오면 WS. 확정 전 메시지는 백로그에 쌓인다.
    enum class OrderedRoute : std::uint8_t
    {
        Undecided,
        Udp,
        WebSocket
    };

    // 비신뢰 채널의 수신 손실률 표본. 순번은 연결별로 이어지므로 빈 곳이 유실 추정치다.
    struct UdpReceiveStats
    {
        bool hasSample = false;
        std::uint32_t firstSeq = 0;
        std::uint32_t maxSeq = 0;
        std::uint64_t received = 0;

        void Accumulate(std::uint32_t seq)
        {
            if (false == hasSample)
            {
                hasSample = true;
                firstSeq = seq;
                maxSeq = seq;
            }
            else if (seq > maxSeq)
            {
                maxSeq = seq;
            }
            ++received;
        }

        double LossRate() const
        {
            if (false == hasSample)
            {
                return -1.0;
            }
            const std::uint64_t expected = static_cast<std::uint64_t>(maxSeq - firstSeq) + 1u;
            if (received >= expected)
            {
                return 0.0;
            }
            return 1.0 - static_cast<double>(received) / static_cast<double>(expected);
        }
    };

    // 한 연결의 UDP 쪽 상태다. 신뢰 WS 연결 위에 얹히는 부가 데이터그램 경로(network-plan §1.2).
    // 서버는 연결마다 토큰을 발급해 WS 로 전하고, 그 토큰으로 인바운드 데이터그램을 연결에 맵핑하며 첫 수신의 출처로
    // 상대 엔드포인트를 배운다. 클라이언트는 토큰을 받은 뒤 서버 엔드포인트로 보낸다.
    struct UdpPeer
    {
        bool tokenSet = false;
        std::uint64_t token = 0;
        Endpoint endpoint;
        // 비신뢰 순번 공간(손실 지표·Sequenced).
        std::uint32_t sendSeq = 0;
        // 메시지 ID 별 최근 순번(UnreliableSequenced 의 역전 폐기).
        Table<MessageId, std::uint32_t> lastReceivedSeq;
        UdpReceiveStats stats;
        // 신뢰 순번 공간(재전송·ack·dedup).
        ReliableEndpoint reliable;
        OrderedRoute route = OrderedRoute::Undecided;
        double readyMilliseconds = 0.0;
        // 클라이언트: 서버에서 데이터그램이 하나라도 왔다. 그 전까지는 punch 를 되풀이한다.
        bool punchConfirmed = false;
        double lastPunchMilliseconds = 0.0;
        // 전송로 확정 전의 사용자 `ReliableOrdered`. [uint16 msgId][uint32 size][payload] 레코드다.
        ByteRing backlog;
        std::uint32_t backlogCount = 0;

        bool IsReady() const
        {
            return tokenSet && endpoint.IsValid();
        }
    };
}
