#pragma once

#include <JBro/Network/Internal/ByteRing.h>
#include <JBro/Network/Internal/UdpDatagram.h>
#include <JBro/Network/Types.h>
#include <JBro/Types/Array.h>

#include <cstddef>
#include <cstdint>

namespace JBro::Network
{
    // 신뢰 엔드포인트 하나의 고정 예산이다. `Reset` 이 한 번 잡고 그 뒤로는 할당하지 않는다.
    struct ReliableConfig
    {
        // 조각으로 나눠 보낼 수 있는 메시지의 상한. 재조립 슬롯 하나의 크기다.
        std::uint32_t maxMessageBytes = 64 * 1024;
        // 아직 창에 들지 못해 기다리는 송신 바이트.
        std::uint32_t sendQueueBytes = 128 * 1024;
        // 동시에 재조립할 수 있는 조각 메시지 수. 보내는 쪽이 FIFO 이므로 실제로는 두셋을 넘기 어렵다.
        std::uint32_t reassemblySlots = 4;
    };

    // 32 비트 순번은 돌아온다. 뺄셈을 부호 있는 수로 보면 창 안에서는 언제나 맞는 비교가 된다(RFC1982 와 같은 생각).
    inline std::int32_t SeqDistance(std::uint32_t later, std::uint32_t earlier)
    {
        return static_cast<std::int32_t>(later - earlier);
    }

    inline bool SeqLess(std::uint32_t left, std::uint32_t right)
    {
        return SeqDistance(left, right) < 0;
    }

    // 신뢰 데이터그램을 실제로 내보내는 곳. `header.token` 은 여기서 채운다.
    class IDatagramEmitter
    {
    public:
        virtual ~IDatagramEmitter() = default;
        virtual void Emit(UdpProto::DatagramHeader& header, const std::uint8_t* payload, std::uint32_t size) = 0;
    };

    // 정확히 한 번, 채널 규율대로 위로 올라오는 메시지.
    class IReliableReceiver
    {
    public:
        virtual ~IReliableReceiver() = default;
        virtual void Deliver(NetChannel channel, MessageId messageId, const std::uint8_t* payload, std::uint32_t size) = 0;
    };

    // 한 연결(서버-피어 하나 또는 클라-서버)의 신뢰 전달 엔진. UDP 위에 재전송·ACK·dedup 을 얹어 유실이 있어도
    // 정확히 한 번 전달한다. 기존 엔진 `CReliableEndpoint` 의 알고리즘을 그대로 잇고 컨테이너만 고정 구조로 바꿨다.
    //
    // 전달 규율은 채널별이다. `ReliableUnordered` 는 처음 받는 즉시, `ReliableOrdered` 는 앞선 순번이 전부 올 때까지
    // 보류한 뒤 순서대로. 두 채널은 한 순번 공간을 쓰고, 순서 판정은 수신 워터마크(`m_recvNext`)다.
    //
    // 선택 ACK 는 워터마크 위 32 순번만 비트로 확인할 수 있다. 그래서 보내는 쪽은 인플라이트 순번 범위를 32 아래로 묶는다 -
    // 그 덕에 인플라이트 슬롯은 `seq % 32` 로 바로 찍고, 받는 쪽의 "앞서 받은 순번" 도 32 비트 하나다.
    //
    // 소켓을 직접 만지지 않는다. 송출은 `IDatagramEmitter`, 시간은 호출측이 밀리초로 준다.
    class ReliableEndpoint final
    {
    public:
        static constexpr std::uint32_t AckWindow = 32;

        // 예산을 잡고 상태를 비운다. UDP 가 이 연결에 붙을 때 한 번 부른다.
        void Reset(const ReliableConfig& config);
        // 상태만 비운다. 예산은 그대로다.
        void Clear();
        // 테스트 전용. 순번 공간의 시작점을 옮겨 랩어라운드 근처를 밟아 본다 - 4 억 개를 보낼 수는 없다.
        void SetSequenceOriginForTests(std::uint32_t origin);
        bool IsReady() const;

        // 큐에 넣고 창 여유만큼 바로 내보낸다. 큐가 차거나 너무 크면 거짓이고 아무것도 넣지 않는다.
        bool SendReliable(NetChannel channel, MessageId messageId, const void* data, std::uint32_t size, double nowMilliseconds,
            IDatagramEmitter& emitter);
        // 받은 신뢰 데이터그램. dedup 과 재조립 뒤 채널 규율대로 전달하고 ack 를 예약한다.
        // 받아 둘 자리가 없으면 **ack 하지 않고** 버린다 - 보내는 쪽이 다시 보낸다. 그것이 수신 쪽 역압이다.
        void OnReliableReceived(const UdpProto::DatagramHeader& header, const std::uint8_t* payload, std::uint32_t size,
            double nowMilliseconds, IReliableReceiver& receiver);
        // 받은 데이터그램의 ack 필드. 인플라이트를 풀고 RTT 와 혼잡 창을 갱신한다.
        void OnAck(std::uint32_t ackBase, std::uint32_t ackBits, double nowMilliseconds);
        // 매 폴. RTO 만료분 재전송(지수 백오프), 큐 배수, 지연된 ack 방출.
        void Tick(double nowMilliseconds, IDatagramEmitter& emitter);

        // 진단.
        std::uint32_t UnackedCount() const;
        std::uint32_t QueuedCount() const;
        double CurrentRtoMilliseconds() const;
        double SmoothedRttMilliseconds() const;
        std::uint32_t CongestionWindow() const;
        std::uint32_t PiggybackAcks() const;
        std::uint32_t StandaloneAcks() const;

    private:
        struct QueuedUnit
        {
            NetChannel channel = NetChannel::ReliableOrdered;
            MessageId messageId = RawMessageId;
            bool fragment = false;
            std::uint32_t msgSeq = 0;
            std::uint16_t fragIndex = 0;
            std::uint16_t fragCount = 0;
            std::uint32_t size = 0;
        };

        struct Outbound
        {
            bool used = false;
            std::uint32_t seq = 0;
            MessageId messageId = RawMessageId;
            NetChannel channel = NetChannel::ReliableOrdered;
            double lastSentMilliseconds = 0.0;
            // 총 송신 횟수. 1 이면 재전송된 적이 없어 RTT 표본으로 쓸 수 있다(Karn).
            std::uint32_t sends = 0;
            // 선택 ack 가 이보다 뒤 순번을 확인했다 - 이것은 잃었을 가능성이 크다. 다음 틱에 RTO 를 기다리지 않고 다시 보낸다.
            bool fastRetransmit = false;
            bool fragment = false;
            std::uint32_t msgSeq = 0;
            std::uint16_t fragIndex = 0;
            std::uint16_t fragCount = 0;
            std::uint32_t size = 0;
            std::uint8_t payload[UdpProto::MaxPayloadBytes] = {};
        };

        struct Reassembly
        {
            bool used = false;
            std::uint32_t msgSeq = 0;
            NetChannel channel = NetChannel::ReliableOrdered;
            MessageId messageId = RawMessageId;
            std::uint16_t fragCount = 0;
            std::uint16_t haveCount = 0;
            // 가장 큰 조각 순번. 순서 배치의 키다.
            std::uint32_t lastSeq = 0;
            std::uint32_t totalSize = 0;
            Array<std::uint8_t> bytes;
            Array<std::uint8_t> have;
        };

        struct OrderedEntry
        {
            bool used = false;
            std::uint32_t seq = 0;
            MessageId messageId = RawMessageId;
            NetChannel channel = NetChannel::ReliableOrdered;
            // -1 이면 `payload` 에 있고, 아니면 그 재조립 슬롯이 본문이다.
            std::int32_t reassemblySlot = -1;
            std::uint32_t size = 0;
            std::uint8_t payload[UdpProto::MaxPayloadBytes] = {};
        };

        void DrainSendQueue(double nowMilliseconds, IDatagramEmitter& emitter);
        bool PushUnit(const QueuedUnit& unit, const std::uint8_t* payload);
        Outbound& SlotFor(std::uint32_t seq);
        const Outbound* OldestUnacked() const;
        void UpdateRtt(double sampleMilliseconds);
        void FillAck(UdpProto::DatagramHeader& header) const;
        void MaybePiggybackAck(UdpProto::DatagramHeader& header);
        Reassembly* FindReassembly(std::uint32_t msgSeq);
        Reassembly* FreeReassembly();
        OrderedEntry* FreeOrderedEntry();
        std::uint32_t FreeOrderedCount() const;
        void FlushOrdered(IReliableReceiver& receiver);
        void ReleaseReassembly(std::int32_t slot);

        ReliableConfig m_config;
        bool m_ready = false;

        // 송신 큐. [QueuedUnit][payload] 레코드가 고리에 이어진다.
        ByteRing m_sendQueue;
        std::uint32_t m_queuedUnits = 0;

        // 인플라이트. `seq % AckWindow` 슬롯.
        Array<Outbound> m_unacked;
        std::uint32_t m_unackedCount = 0;
        std::uint32_t m_nextSeq = 0;
        std::uint32_t m_nextMsgSeq = 0;
        // 혼잡 창(AIMD). ack 마다 +1, 재전송이 난 틱마다 반감.
        std::uint32_t m_cwnd = 16;

        // RTT/RTO(Jacobson/Karels).
        double m_srtt = 0.0;
        double m_rttvar = 0.0;
        double m_rto = 250.0;
        bool m_haveRtt = false;

        // 수신. 이 미만은 전부 받았다. 비트 i 는 recvNext+1+i 를 받았다는 뜻이다.
        std::uint32_t m_recvNext = 0;
        std::uint32_t m_aheadBits = 0;
        bool m_ackPending = false;
        double m_ackPendingSinceMilliseconds = 0.0;
        // 새로 받은 뒤 standalone ack 를 이만큼 더 되풀이한다. ack 하나가 유실돼도 보내는 쪽이 RTO 까지 모르는 일을 줄인다.
        std::uint32_t m_ackRepeatsLeft = 0;
        std::uint32_t m_piggybackAcks = 0;
        std::uint32_t m_standaloneAcks = 0;

        Array<Reassembly> m_reassembly;
        Array<OrderedEntry> m_ordered;
    };
}
