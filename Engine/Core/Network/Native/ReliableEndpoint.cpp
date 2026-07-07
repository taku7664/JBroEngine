#include "pch.h"
#include "ReliableEndpoint.h"

#if !JBRO_PLATFORM_WEB

#include "Core/Network/NetworkTypes.h" // ENetChannel

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
	constexpr double kMinRtoMs = 50.0;   // 하한 — 로컬에서도 스팸 방지.
	constexpr double kMaxRtoMs = 2000.0; // 상한 — 죽은 연결은 상위 타임아웃이 처리.

	// 재전송 지수 백오프 배수(Sends 기준). Sends 1→×1, 2→×2, ... 상한 ×64.
	double BackoffFactor(std::uint32_t sends)
	{
		const std::uint32_t shift = std::min<std::uint32_t>(sends > 0 ? sends - 1 : 0, 6);
		return static_cast<double>(1u << shift);
	}

	// 신뢰 헤더 기본 채움(Token 은 emit 콜백이 세팅).
	UdpProto::UdpDatagramHeader MakeReliableHeader(std::uint32_t seq, std::uint16_t msgId)
	{
		UdpProto::UdpDatagramHeader header;
		header.Flags   = UdpProto::Flag_Reliable;
		header.Channel = ENetChannel::ReliableOrdered;
		header.Seq     = seq;
		header.MsgId   = msgId;
		return header;
	}
}

// ── 송신 ─────────────────────────────────────────────────────────────────────────

void CReliableEndpoint::SendReliable(
	std::uint16_t msgId, const void* data, std::uint32_t size, double nowMs, const FEmit& emit)
{
	const std::uint32_t seq = m_nextSeq++;

	Outbound outbound;
	outbound.MsgId      = msgId;
	outbound.LastSentMs = nowMs;
	outbound.Sends      = 1;
	outbound.Payload.resize(size);
	if (size > 0 && nullptr != data)
	{
		std::memcpy(outbound.Payload.data(), data, size);
	}

	UdpProto::UdpDatagramHeader header = MakeReliableHeader(seq, msgId);
	emit(header, outbound.Payload.data(), size);

	m_unacked.emplace(seq, std::move(outbound));
}

// ── 수신 ─────────────────────────────────────────────────────────────────────────

bool CReliableEndpoint::OnReliableReceived(std::uint32_t seq)
{
	m_ackPending = true; // 신규든 중복이든 상대에게 수신 사실을 알린다(재전송 억제).

	if (seq < m_recvNext)
	{
		return false; // 누적 경계 아래 — 이미 전달됨.
	}
	if (0 != m_recvAhead.count(seq))
	{
		return false; // gap 위에서 이미 받은 것 — 중복.
	}

	if (seq == m_recvNext)
	{
		// 누적 경계 전진 + 뒤이어 붙은 연속분 흡수.
		++m_recvNext;
		while (0 != m_recvAhead.erase(m_recvNext))
		{
			++m_recvNext;
		}
	}
	else
	{
		m_recvAhead.insert(seq); // gap 위 — 선택 ack 로 보고.
	}
	return true; // 최초 수신 — 상위로 전달.
}

void CReliableEndpoint::FillAck(UdpProto::UdpDatagramHeader& header) const
{
	// ackPending 일 때만 호출 → m_recvNext >= 1 보장(무언가 받았음).
	header.Flags  |= UdpProto::Flag_Ack;
	header.AckBase = m_recvNext - 1; // 누적: 이 seq 이하 전부 수신.
	std::uint32_t bits = 0;
	for (std::uint32_t i = 0; i < 32; ++i)
	{
		// gap(=m_recvNext) 위 후보: m_recvNext+1+i. 받았으면 비트 세팅.
		if (0 != m_recvAhead.count(m_recvNext + 1u + i))
		{
			bits |= (1u << i);
		}
	}
	header.AckBits = bits;
}

// ── ACK 처리(송신측) ─────────────────────────────────────────────────────────────

void CReliableEndpoint::OnAck(std::uint32_t ackBase, std::uint32_t ackBits, double nowMs)
{
	double rttSample = -1.0;

	// 누적: seq <= ackBase 전부 해제.
	for (auto it = m_unacked.begin(); it != m_unacked.end() && it->first <= ackBase; )
	{
		if (1 == it->second.Sends) // Karn: 재전송 안 된 패킷만 RTT 표본.
		{
			rttSample = nowMs - it->second.LastSentMs;
		}
		it = m_unacked.erase(it);
	}

	// 선택: ackBase+2+i (gap=ackBase+1 은 아직 미수신).
	for (std::uint32_t i = 0; i < 32; ++i)
	{
		if (0 == (ackBits & (1u << i)))
		{
			continue;
		}
		const std::uint32_t seq = ackBase + 2u + i;
		const auto it = m_unacked.find(seq);
		if (m_unacked.end() != it)
		{
			if (1 == it->second.Sends)
			{
				rttSample = nowMs - it->second.LastSentMs;
			}
			m_unacked.erase(it);
		}
	}

	if (rttSample >= 0.0)
	{
		UpdateRtt(rttSample);
	}
}

void CReliableEndpoint::UpdateRtt(double sampleMs)
{
	if (false == m_haveRtt)
	{
		m_srtt    = sampleMs;
		m_rttvar  = sampleMs * 0.5;
		m_haveRtt = true;
	}
	else
	{
		m_rttvar = 0.75 * m_rttvar + 0.25 * std::fabs(m_srtt - sampleMs);
		m_srtt   = 0.875 * m_srtt + 0.125 * sampleMs;
	}
	m_rto = std::min(kMaxRtoMs, std::max(kMinRtoMs, m_srtt + 4.0 * m_rttvar));
}

// ── 주기 틱: 재전송 + ack flush ──────────────────────────────────────────────────

void CReliableEndpoint::Tick(double nowMs, const FEmit& emit)
{
	// 1) RTO 만료분 재전송(패킷별 지수 백오프). 전역 RTO 는 UpdateRtt 로만 변한다.
	for (auto& entry : m_unacked)
	{
		Outbound& outbound = entry.second;
		const double timeout = m_rto * BackoffFactor(outbound.Sends);
		if (nowMs - outbound.LastSentMs < timeout)
		{
			continue;
		}

		UdpProto::UdpDatagramHeader header = MakeReliableHeader(entry.first, outbound.MsgId);
		emit(header, outbound.Payload.data(), static_cast<std::uint32_t>(outbound.Payload.size()));
		outbound.LastSentMs = nowMs;
		++outbound.Sends;
	}

	// 2) 대기 중 ack 를 독립 데이터그램으로 flush(피기백 최적화는 후속).
	if (m_ackPending)
	{
		UdpProto::UdpDatagramHeader header;
		header.Channel = ENetChannel::ReliableOrdered;
		FillAck(header);
		emit(header, nullptr, 0);
		m_ackPending = false;
	}
}

void CReliableEndpoint::Reset()
{
	m_nextSeq = 0;
	m_unacked.clear();
	m_srtt = 0.0; m_rttvar = 0.0; m_rto = 250.0; m_haveRtt = false;
	m_recvNext = 0;
	m_recvAhead.clear();
	m_ackPending = false;
}

#endif // !JBRO_PLATFORM_WEB
