#include <JBro/Network/Internal/ReliableEndpoint.h>

#include <cmath>
#include <cstring>

namespace JBro::Network
{
    namespace
    {
        // RTO 하한은 로컬에서도 스팸을 막고, 상한 너머의 죽은 연결은 세션 타임아웃이 처리한다.
        constexpr double MinRtoMilliseconds = 50.0;
        constexpr double MaxRtoMilliseconds = 2000.0;
        constexpr std::uint32_t MinCwnd = 4;
        constexpr std::uint32_t MaxCwnd = 256;
        // 편승 못한 ack 를 따로 내보내기까지 기다리는 창. 최소 RTO 보다 짧아 재전송 전에 도착한다.
        constexpr double AckDelayMilliseconds = 25.0;
        // 새로 받은 뒤 standalone ack 를 되풀이하는 횟수. 30% 유실에서 ack 셋이 다 사라질 확률은 3% 다.
        constexpr std::uint32_t AckRepeats = 2;
        // 재전송 백오프 상한(2^3 = 8 배). 기존 엔진의 64 배는 죽은 링크에는 알맞지만 유실이 이어지는 링크에서는 복구를 수십 초로
        // 늘였다 - 죽은 링크는 세션 타임아웃이 끊는다.
        constexpr std::uint32_t MaxBackoffShift = 3;

        double BackoffFactor(std::uint32_t sends)
        {
            const std::uint32_t shift = sends > 0 ? (sends - 1 < MaxBackoffShift ? sends - 1 : MaxBackoffShift) : 0;
            return static_cast<double>(1u << shift);
        }

        UdpProto::DatagramHeader MakeReliableHeader(std::uint32_t seq, MessageId messageId, NetChannel channel)
        {
            UdpProto::DatagramHeader header;
            header.flags = UdpProto::FlagReliable;
            header.channel = channel;
            header.seq = seq;
            header.msgId = messageId;
            return header;
        }
    }

    // ── 수명 ────────────────────────────────────────────────────────────────────────────────────

    void ReliableEndpoint::Reset(const ReliableConfig& config)
    {
        m_config = config;
        m_sendQueue.Reset(config.sendQueueBytes);
        m_unacked.Resize(AckWindow);
        m_reassembly.Resize(config.reassemblySlots);
        const std::uint32_t maxFragments = (config.maxMessageBytes + UdpProto::MaxPayloadBytes - 1) / UdpProto::MaxPayloadBytes;
        for (Reassembly& slot : m_reassembly)
        {
            slot.bytes.Resize(config.maxMessageBytes);
            slot.have.Resize(maxFragments);
        }
        m_ordered.Resize(AckWindow + config.reassemblySlots);
        m_ready = true;
        Clear();
    }

    void ReliableEndpoint::Clear()
    {
        m_sendQueue.Clear();
        m_queuedUnits = 0;
        for (Outbound& slot : m_unacked)
        {
            slot.used = false;
        }
        m_unackedCount = 0;
        m_nextSeq = 0;
        m_nextMsgSeq = 0;
        m_cwnd = 16;
        m_srtt = 0.0;
        m_rttvar = 0.0;
        m_rto = 250.0;
        m_haveRtt = false;
        m_recvNext = 0;
        m_aheadBits = 0;
        m_ackPending = false;
        m_ackPendingSinceMilliseconds = 0.0;
        m_ackRepeatsLeft = 0;
        m_piggybackAcks = 0;
        m_standaloneAcks = 0;
        for (Reassembly& slot : m_reassembly)
        {
            slot.used = false;
        }
        for (OrderedEntry& entry : m_ordered)
        {
            entry.used = false;
        }
    }

    bool ReliableEndpoint::IsReady() const
    {
        return m_ready;
    }

    // ── 송신 ────────────────────────────────────────────────────────────────────────────────────

    bool ReliableEndpoint::PushUnit(const QueuedUnit& unit, const std::uint8_t* payload)
    {
        if (m_sendQueue.Free() < sizeof(QueuedUnit) + unit.size)
        {
            return false;
        }
        m_sendQueue.Write(&unit, sizeof(QueuedUnit));
        if (unit.size > 0)
        {
            m_sendQueue.Write(payload, unit.size);
        }
        ++m_queuedUnits;
        return true;
    }

    bool ReliableEndpoint::SendReliable(NetChannel channel, MessageId messageId, const void* data, std::uint32_t size,
        double nowMilliseconds, IDatagramEmitter& emitter)
    {
        if (false == m_ready || size > m_config.maxMessageBytes)
        {
            return false;
        }
        const std::uint8_t* bytes = static_cast<const std::uint8_t*>(data);
        if (size <= UdpProto::MaxPayloadBytes)
        {
            QueuedUnit unit;
            unit.channel = channel;
            unit.messageId = messageId;
            unit.size = size;
            if (false == PushUnit(unit, bytes))
            {
                return false;
            }
        }
        else
        {
            // 조각마다 전송 단위 하나. 전부 들어갈 자리를 먼저 본다 - 반만 넣으면 상대가 영원히 기다린다.
            const std::uint32_t count = (size + UdpProto::MaxPayloadBytes - 1) / UdpProto::MaxPayloadBytes;
            if (m_sendQueue.Free() < count * sizeof(QueuedUnit) + size)
            {
                return false;
            }
            const std::uint32_t msgSeq = m_nextMsgSeq++;
            for (std::uint32_t index = 0; index < count; ++index)
            {
                const std::uint32_t offset = index * UdpProto::MaxPayloadBytes;
                const std::uint32_t chunk = (size - offset < UdpProto::MaxPayloadBytes) ? (size - offset) : UdpProto::MaxPayloadBytes;
                QueuedUnit unit;
                unit.channel = channel;
                unit.messageId = messageId;
                unit.fragment = true;
                unit.msgSeq = msgSeq;
                unit.fragIndex = static_cast<std::uint16_t>(index);
                unit.fragCount = static_cast<std::uint16_t>(count);
                unit.size = chunk;
                PushUnit(unit, bytes + offset);
            }
        }
        DrainSendQueue(nowMilliseconds, emitter);
        return true;
    }

    ReliableEndpoint::Outbound& ReliableEndpoint::SlotFor(std::uint32_t seq)
    {
        return m_unacked[seq % AckWindow];
    }

    const ReliableEndpoint::Outbound* ReliableEndpoint::OldestUnacked() const
    {
        const Outbound* oldest = nullptr;
        for (const Outbound& slot : m_unacked)
        {
            if (slot.used && (nullptr == oldest || slot.seq < oldest->seq))
            {
                oldest = &slot;
            }
        }
        return oldest;
    }

    // 창(cwnd, 그리고 인플라이트 순번 범위 < AckWindow) 여유만큼 큐 앞에서부터 내보낸다. 순번은 여기서 붙인다 -
    // 그래야 연속이 유지되고 모든 인플라이트가 선택 ack 범위 안에 든다.
    void ReliableEndpoint::DrainSendQueue(double nowMilliseconds, IDatagramEmitter& emitter)
    {
        while (m_queuedUnits > 0)
        {
            if (m_unackedCount >= m_cwnd)
            {
                break;
            }
            const Outbound* oldest = OldestUnacked();
            if (nullptr != oldest && m_nextSeq - oldest->seq >= AckWindow)
            {
                break;
            }
            QueuedUnit unit;
            m_sendQueue.Read(&unit, sizeof(QueuedUnit));
            const std::uint32_t seq = m_nextSeq++;
            Outbound& outbound = SlotFor(seq);
            outbound.used = true;
            outbound.seq = seq;
            outbound.messageId = unit.messageId;
            outbound.channel = unit.channel;
            outbound.lastSentMilliseconds = nowMilliseconds;
            outbound.sends = 1;
            outbound.fastRetransmit = false;
            outbound.fragment = unit.fragment;
            outbound.msgSeq = unit.msgSeq;
            outbound.fragIndex = unit.fragIndex;
            outbound.fragCount = unit.fragCount;
            outbound.size = unit.size;
            m_sendQueue.Read(outbound.payload, unit.size);
            --m_queuedUnits;
            ++m_unackedCount;

            UdpProto::DatagramHeader header = MakeReliableHeader(seq, unit.messageId, unit.channel);
            if (unit.fragment)
            {
                header.flags |= UdpProto::FlagFragment;
                header.msgSeq = unit.msgSeq;
                header.fragIndex = unit.fragIndex;
                header.fragCount = unit.fragCount;
            }
            MaybePiggybackAck(header);
            emitter.Emit(header, outbound.payload, outbound.size);
        }
    }

    // ── 수신 ────────────────────────────────────────────────────────────────────────────────────

    ReliableEndpoint::Reassembly* ReliableEndpoint::FindReassembly(std::uint32_t msgSeq)
    {
        for (Reassembly& slot : m_reassembly)
        {
            if (slot.used && slot.msgSeq == msgSeq)
            {
                return &slot;
            }
        }
        return nullptr;
    }

    ReliableEndpoint::Reassembly* ReliableEndpoint::FreeReassembly()
    {
        for (Reassembly& slot : m_reassembly)
        {
            if (false == slot.used)
            {
                return &slot;
            }
        }
        return nullptr;
    }

    ReliableEndpoint::OrderedEntry* ReliableEndpoint::FreeOrderedEntry()
    {
        for (OrderedEntry& entry : m_ordered)
        {
            if (false == entry.used)
            {
                return &entry;
            }
        }
        return nullptr;
    }

    std::uint32_t ReliableEndpoint::FreeOrderedCount() const
    {
        std::uint32_t count = 0;
        for (const OrderedEntry& entry : m_ordered)
        {
            if (false == entry.used)
            {
                ++count;
            }
        }
        return count;
    }

    void ReliableEndpoint::ReleaseReassembly(std::int32_t slot)
    {
        if (slot >= 0 && static_cast<std::size_t>(slot) < m_reassembly.Size())
        {
            m_reassembly[static_cast<std::size_t>(slot)].used = false;
        }
    }

    void ReliableEndpoint::OnReliableReceived(const UdpProto::DatagramHeader& header, const std::uint8_t* payload,
        std::uint32_t size, double nowMilliseconds, IReliableReceiver& receiver)
    {
        if (false == m_ready || size > UdpProto::MaxPayloadBytes)
        {
            return;
        }
        const std::uint32_t seq = header.seq;
        // 창 밖 순번은 보내는 쪽 규칙 위반이다. ack 도 하지 않는다.
        if (seq >= m_recvNext && seq - m_recvNext >= AckWindow)
        {
            return;
        }
        // dedup. 이미 받은 것은 전달하지 않지만 상대에게 받았다고는 알린다(재전송 억제).
        const bool duplicate = seq < m_recvNext || (seq > m_recvNext && 0 != (m_aheadBits & (1u << (seq - m_recvNext - 1))));
        if (duplicate)
        {
            if (false == m_ackPending)
            {
                m_ackPendingSinceMilliseconds = nowMilliseconds;
            }
            m_ackPending = true;
            return;
        }

        // 받아 둘 자리를 먼저 확인한다. 없으면 ack 없이 버린다 - 그것이 역압이다.
        const bool isFragment = 0 != (header.flags & UdpProto::FlagFragment);
        const bool ordered = header.channel != NetChannel::ReliableUnordered;
        Reassembly* reassembly = nullptr;
        if (isFragment)
        {
            if (0 == header.fragCount || header.fragIndex >= header.fragCount
                || header.fragCount > m_reassembly[0].have.Size())
            {
                return;
            }
            reassembly = FindReassembly(header.msgSeq);
            if (nullptr == reassembly)
            {
                reassembly = FreeReassembly();
                if (nullptr == reassembly)
                {
                    return;
                }
            }
        }
        if (ordered && 0 == FreeOrderedCount())
        {
            return;
        }

        // 받아들인다. ack 예약과 워터마크 갱신. 새 데이터의 ack 는 몇 번 되풀이한다.
        if (false == m_ackPending)
        {
            m_ackPendingSinceMilliseconds = nowMilliseconds;
        }
        m_ackPending = true;
        m_ackRepeatsLeft = AckRepeats;
        if (seq == m_recvNext)
        {
            ++m_recvNext;
            // 비트 i 가 recvNext+1+i 였으니 이제 비트 0 이 새 recvNext 다. 연속분을 흡수한다.
            while (0 != (m_aheadBits & 1u))
            {
                m_aheadBits >>= 1;
                ++m_recvNext;
            }
            m_aheadBits >>= 1;
        }
        else
        {
            m_aheadBits |= 1u << (seq - m_recvNext - 1);
        }

        if (isFragment)
        {
            if (false == reassembly->used)
            {
                reassembly->used = true;
                reassembly->msgSeq = header.msgSeq;
                reassembly->channel = header.channel;
                reassembly->messageId = header.msgId;
                reassembly->fragCount = header.fragCount;
                reassembly->haveCount = 0;
                reassembly->lastSeq = seq;
                reassembly->totalSize = 0;
                std::memset(reassembly->have.Data(), 0, reassembly->have.Size());
            }
            if (0 == reassembly->have[header.fragIndex])
            {
                const std::uint32_t offset = static_cast<std::uint32_t>(header.fragIndex) * UdpProto::MaxPayloadBytes;
                if (offset + size > reassembly->bytes.Size())
                {
                    return;
                }
                std::memcpy(reassembly->bytes.Data() + offset, payload, size);
                reassembly->have[header.fragIndex] = 1;
                ++reassembly->haveCount;
                if (header.fragIndex + 1 == header.fragCount)
                {
                    reassembly->totalSize = offset + size;
                }
            }
            if (seq > reassembly->lastSeq)
            {
                reassembly->lastSeq = seq;
            }
            if (reassembly->haveCount < reassembly->fragCount)
            {
                return;
            }
            // 완성. 순서 무관이면 바로, 아니면 마지막 조각의 순번으로 줄에 세운다.
            const std::int32_t slotIndex = static_cast<std::int32_t>(reassembly - m_reassembly.Data());
            if (reassembly->channel == NetChannel::ReliableUnordered)
            {
                receiver.Deliver(reassembly->channel, reassembly->messageId, reassembly->bytes.Data(), reassembly->totalSize);
                reassembly->used = false;
            }
            else
            {
                OrderedEntry* entry = FreeOrderedEntry();
                entry->used = true;
                entry->seq = reassembly->lastSeq;
                entry->messageId = reassembly->messageId;
                entry->channel = reassembly->channel;
                entry->reassemblySlot = slotIndex;
                entry->size = reassembly->totalSize;
            }
        }
        else if (false == ordered)
        {
            receiver.Deliver(header.channel, header.msgId, payload, size);
        }
        else
        {
            OrderedEntry* entry = FreeOrderedEntry();
            entry->used = true;
            entry->seq = seq;
            entry->messageId = header.msgId;
            entry->channel = header.channel;
            entry->reassemblySlot = -1;
            entry->size = size;
            std::memcpy(entry->payload, payload, size);
        }
        FlushOrdered(receiver);
    }

    // 워터마크 아래(앞선 순번을 전부 받은) 것만 순번 오름차순으로 내보낸다.
    void ReliableEndpoint::FlushOrdered(IReliableReceiver& receiver)
    {
        while (true)
        {
            OrderedEntry* next = nullptr;
            for (OrderedEntry& entry : m_ordered)
            {
                if (entry.used && entry.seq < m_recvNext && (nullptr == next || entry.seq < next->seq))
                {
                    next = &entry;
                }
            }
            if (nullptr == next)
            {
                return;
            }
            if (next->reassemblySlot >= 0)
            {
                Reassembly& reassembly = m_reassembly[static_cast<std::size_t>(next->reassemblySlot)];
                receiver.Deliver(next->channel, next->messageId, reassembly.bytes.Data(), next->size);
                ReleaseReassembly(next->reassemblySlot);
            }
            else
            {
                receiver.Deliver(next->channel, next->messageId, next->payload, next->size);
            }
            next->used = false;
        }
    }

    void ReliableEndpoint::FillAck(UdpProto::DatagramHeader& header) const
    {
        header.flags |= UdpProto::FlagAck;
        header.ackBase = m_recvNext;
        header.ackBits = m_aheadBits;
    }

    void ReliableEndpoint::MaybePiggybackAck(UdpProto::DatagramHeader& header)
    {
        if (m_ackPending)
        {
            FillAck(header);
            m_ackPending = false;
            ++m_piggybackAcks;
        }
    }

    // ── ACK ─────────────────────────────────────────────────────────────────────────────────────

    void ReliableEndpoint::OnAck(std::uint32_t ackBase, std::uint32_t ackBits, double nowMilliseconds)
    {
        if (false == m_ready)
        {
            return;
        }
        // RTT 표본은 이 ack 가 새로 덮은 것 가운데 **가장 최근에 보낸(순번이 가장 큰)** 첫 송신 패킷에서만 뜬다(Karn 에 더한 규칙).
        // 앞선 패킷의 ack 가 유실된 뒤 뒤따르는 누적 ack 가 그것까지 덮으면, 앞선 패킷의 "왕복" 은 유실된 ack 를 기다린 시간이다.
        // 그것을 표본으로 삼으면 RTO 가 커지고, 커진 RTO 가 더 큰 표본을 허용하는 되먹임으로 상한까지 달린다 - 30% 유실에서 그렇게 멈췼다.
        double sampleSent = -1.0;
        std::uint32_t sampleSeq = 0;
        bool acked = false;
        for (Outbound& slot : m_unacked)
        {
            if (false == slot.used)
            {
                continue;
            }
            bool covered = slot.seq < ackBase;
            if (false == covered && slot.seq > ackBase && slot.seq - ackBase - 1 < AckWindow)
            {
                covered = 0 != (ackBits & (1u << (slot.seq - ackBase - 1)));
            }
            if (false == covered)
            {
                continue;
            }
            if (1 == slot.sends && (sampleSent < 0.0 || slot.seq > sampleSeq))
            {
                sampleSent = slot.lastSentMilliseconds;
                sampleSeq = slot.seq;
            }
            slot.used = false;
            --m_unackedCount;
            acked = true;
        }
        if (sampleSent >= 0.0)
        {
            UpdateRtt(nowMilliseconds - sampleSent);
        }
        if (acked)
        {
            m_cwnd = (m_cwnd + 1 < MaxCwnd) ? m_cwnd + 1 : MaxCwnd;
        }
        // 빠른 재전송: 이 ack 가 확인한 가장 큰 순번보다 앞선 것이 아직 남아 있으면 그것은 잃었을 가능성이 크다(TCP 의 중복 ack 판정).
        // RTO 를 기다리지 않고 다음 틱에 다시 보낸다. 한 번만 - 그 뒤는 RTO 가 맡는다.
        std::uint32_t highestCovered = ackBase;
        bool anyCovered = ackBase > 0;
        for (std::uint32_t bit = 0; bit < AckWindow; ++bit)
        {
            if (0 != (ackBits & (1u << bit)))
            {
                highestCovered = ackBase + 1 + bit;
                anyCovered = true;
            }
        }
        if (anyCovered)
        {
            for (Outbound& slot : m_unacked)
            {
                if (slot.used && slot.seq < highestCovered && 1 == slot.sends)
                {
                    slot.fastRetransmit = true;
                }
            }
        }
    }

    void ReliableEndpoint::UpdateRtt(double sampleMilliseconds)
    {
        if (false == m_haveRtt)
        {
            m_srtt = sampleMilliseconds;
            m_rttvar = sampleMilliseconds * 0.5;
            m_haveRtt = true;
        }
        else
        {
            m_rttvar = 0.75 * m_rttvar + 0.25 * std::fabs(m_srtt - sampleMilliseconds);
            m_srtt = 0.875 * m_srtt + 0.125 * sampleMilliseconds;
        }
        double rto = m_srtt + 4.0 * m_rttvar;
        if (rto < MinRtoMilliseconds)
        {
            rto = MinRtoMilliseconds;
        }
        if (rto > MaxRtoMilliseconds)
        {
            rto = MaxRtoMilliseconds;
        }
        m_rto = rto;
    }

    // ── 틱 ──────────────────────────────────────────────────────────────────────────────────────

    void ReliableEndpoint::Tick(double nowMilliseconds, IDatagramEmitter& emitter)
    {
        if (false == m_ready)
        {
            return;
        }
        bool retransmitted = false;
        for (Outbound& slot : m_unacked)
        {
            if (false == slot.used)
            {
                continue;
            }
            const double timeout = m_rto * BackoffFactor(slot.sends);
            if (false == slot.fastRetransmit && nowMilliseconds - slot.lastSentMilliseconds < timeout)
            {
                continue;
            }
            slot.fastRetransmit = false;
            UdpProto::DatagramHeader header = MakeReliableHeader(slot.seq, slot.messageId, slot.channel);
            if (slot.fragment)
            {
                header.flags |= UdpProto::FlagFragment;
                header.msgSeq = slot.msgSeq;
                header.fragIndex = slot.fragIndex;
                header.fragCount = slot.fragCount;
            }
            MaybePiggybackAck(header);
            emitter.Emit(header, slot.payload, slot.size);
            slot.lastSentMilliseconds = nowMilliseconds;
            ++slot.sends;
            retransmitted = true;
        }
        if (retransmitted)
        {
            m_cwnd = (m_cwnd / 2 > MinCwnd) ? m_cwnd / 2 : MinCwnd;
        }
        DrainSendQueue(nowMilliseconds, emitter);
        if (m_ackPending && nowMilliseconds - m_ackPendingSinceMilliseconds >= AckDelayMilliseconds)
        {
            UdpProto::DatagramHeader header;
            header.channel = NetChannel::ReliableOrdered;
            FillAck(header);
            emitter.Emit(header, nullptr, 0);
            m_ackPending = false;
            ++m_standaloneAcks;
            // 빈 곳이 남아 있거나 되풀이가 남았으면 ack 를 다시 예약한다. 이 ack 가 유실되면 보내는 쪽은 RTO 까지 모른다 -
            // TCP 의 중복 ack 가 하는 일을 여기서는 이 되풀이가 한다.
            if (0 != m_aheadBits || m_ackRepeatsLeft > 0)
            {
                if (m_ackRepeatsLeft > 0)
                {
                    --m_ackRepeatsLeft;
                }
                m_ackPending = true;
                m_ackPendingSinceMilliseconds = nowMilliseconds;
            }
        }
    }

    // ── 진단 ────────────────────────────────────────────────────────────────────────────────────

    std::uint32_t ReliableEndpoint::UnackedCount() const
    {
        return m_unackedCount;
    }

    std::uint32_t ReliableEndpoint::QueuedCount() const
    {
        return m_queuedUnits;
    }

    double ReliableEndpoint::CurrentRtoMilliseconds() const
    {
        return m_rto;
    }

    double ReliableEndpoint::SmoothedRttMilliseconds() const
    {
        return m_srtt;
    }

    std::uint32_t ReliableEndpoint::CongestionWindow() const
    {
        return m_cwnd;
    }

    std::uint32_t ReliableEndpoint::PiggybackAcks() const
    {
        return m_piggybackAcks;
    }

    std::uint32_t ReliableEndpoint::StandaloneAcks() const
    {
        return m_standaloneAcks;
    }
}
