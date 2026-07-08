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

	constexpr std::uint32_t kMinCwnd = 4u;   // 혼잡 윈도우 하한(진행 보장).
	constexpr std::uint32_t kMaxCwnd = 256u; // 상한(버스트 억제).

	// 선택 ACK 는 누적 경계 위 32 seq 만 비트로 확인 가능. 인플라이트 seq 범위를 이 값 이하로
	// 유지해야 모든 인플라이트가 ACK 가능해진다(안 그러면 창 밖 seq 가 영영 재전송 = livelock).
	constexpr std::uint32_t kAckWindow = 32u;

	// 재전송 지수 백오프 배수(Sends 기준). Sends 1→×1, 2→×2, ... 상한 ×64.
	double BackoffFactor(std::uint32_t sends)
	{
		const std::uint32_t shift = std::min<std::uint32_t>(sends > 0 ? sends - 1 : 0, 6);
		return static_cast<double>(1u << shift);
	}

	// 신뢰 헤더 기본 채움(Token 은 emit 콜백이 세팅). 채널은 원 송신 채널을 보존해 전달.
	UdpProto::UdpDatagramHeader MakeReliableHeader(std::uint32_t seq, std::uint16_t msgId, ENetChannel channel)
	{
		UdpProto::UdpDatagramHeader header;
		header.Flags   = UdpProto::Flag_Reliable;
		header.Channel = channel;
		header.Seq     = seq;
		header.MsgId   = msgId;
		return header;
	}
}

// ── 송신 ─────────────────────────────────────────────────────────────────────────

void CReliableEndpoint::SendReliable(
	ENetChannel channel, std::uint16_t msgId, const void* data, std::uint32_t size, double nowMs, const FEmit& emit)
{
	// 전송 단위(데이터그램 1개)로 쪼개 큐에 넣는다 — seq 는 실제 송신 때 할당(연속성 + 창 제어).
	const std::uint8_t* bytes = static_cast<const std::uint8_t*>(data);

	if (size <= UdpProto::kMaxPayload)
	{
		SendUnit unit;
		unit.Channel = channel;
		unit.MsgId   = msgId;
		unit.Payload.assign(bytes, bytes + size);
		m_sendQueue.push_back(std::move(unit));
	}
	else
	{
		// 프래그먼트 분할(3d) — 조각마다 전송 단위 1개. MsgSeq 로 재조립 그룹화.
		const std::uint32_t count  = (size + UdpProto::kMaxPayload - 1u) / UdpProto::kMaxPayload;
		const std::uint32_t msgSeq = m_nextMsgSeq++;
		for (std::uint32_t i = 0; i < count; ++i)
		{
			const std::uint32_t off   = i * UdpProto::kMaxPayload;
			const std::uint32_t chunk = std::min(UdpProto::kMaxPayload, size - off);

			SendUnit unit;
			unit.Channel   = channel;
			unit.MsgId     = msgId;
			unit.Fragment  = true;
			unit.MsgSeq    = msgSeq;
			unit.FragIndex = static_cast<std::uint16_t>(i);
			unit.FragCount = static_cast<std::uint16_t>(count);
			unit.Payload.assign(bytes + off, bytes + off + chunk);
			m_sendQueue.push_back(std::move(unit));
		}
	}

	DrainSendQueue(nowMs, emit);
}

// 창(cwnd + 인플라이트 seq 범위 ≤ kAckWindow) 여유만큼 큐 앞에서부터 실제 송출.
// seq 를 여기서 할당해 연속 유지 → 모든 인플라이트가 선택 ACK 범위 안에 들어온다.
void CReliableEndpoint::DrainSendQueue(double nowMs, const FEmit& emit)
{
	std::size_t sent = 0;
	while (sent < m_sendQueue.size())
	{
		if (m_unacked.size() >= m_cwnd)
		{
			break; // 혼잡 윈도우 가득.
		}
		if (false == m_unacked.empty() &&
		    (m_nextSeq - m_unacked.begin()->first) >= kAckWindow)
		{
			break; // 인플라이트 seq 범위가 ACK 창에 도달 — 더 보내면 창 밖 seq 발생.
		}

		SendUnit& unit = m_sendQueue[sent];
		const std::uint32_t seq = m_nextSeq++;

		Outbound outbound;
		outbound.MsgId      = unit.MsgId;
		outbound.Channel    = unit.Channel;
		outbound.LastSentMs = nowMs;
		outbound.Sends      = 1;
		outbound.Fragment   = unit.Fragment;
		outbound.MsgSeq     = unit.MsgSeq;
		outbound.FragIndex  = unit.FragIndex;
		outbound.FragCount  = unit.FragCount;
		outbound.Payload    = std::move(unit.Payload);

		UdpProto::UdpDatagramHeader header = MakeReliableHeader(seq, unit.MsgId, unit.Channel);
		if (unit.Fragment)
		{
			header.Flags    |= UdpProto::Flag_Fragment;
			header.MsgSeq    = unit.MsgSeq;
			header.FragIndex = unit.FragIndex;
			header.FragCount = unit.FragCount;
		}
		MaybePiggybackAck(header); // 나가는 데이터에 대기 ack 편승.
		emit(header, outbound.Payload.data(), static_cast<std::uint32_t>(outbound.Payload.size()));

		m_unacked.emplace(seq, std::move(outbound));
		++sent;
	}
	if (sent > 0)
	{
		m_sendQueue.erase(m_sendQueue.begin(), m_sendQueue.begin() + static_cast<std::ptrdiff_t>(sent));
	}
}

// ── 수신 ─────────────────────────────────────────────────────────────────────────

void CReliableEndpoint::OnReliableReceived(const UdpProto::UdpDatagramHeader& header,
	const std::uint8_t* payload, std::uint32_t size, double nowMs, const FDeliver& deliver)
{
	const std::uint32_t seq     = header.Seq;
	const ENetChannel   channel = header.Channel;
	const std::uint16_t msgId   = header.MsgId;

	if (false == m_ackPending)
	{
		m_ackPendingSinceMs = nowMs; // 대기 시작 — 이 시점부터 지연 창을 잰다.
	}
	m_ackPending = true; // 신규든 중복이든 상대에게 수신 사실을 알린다(재전송 억제).

	// dedup — 이미 받은 seq 는 폐기(전달 안 함). 재전송된 조각도 여기서 걸러진다.
	if (seq < m_recvNext || 0 != m_recvAhead.count(seq))
	{
		return;
	}

	// 수신 워터마크 갱신.
	if (seq == m_recvNext)
	{
		++m_recvNext;
		while (0 != m_recvAhead.erase(m_recvNext)) // 뒤이어 붙은 연속분 흡수.
		{
			++m_recvNext;
		}
	}
	else
	{
		m_recvAhead.insert(seq); // gap 위 — 선택 ack 로 보고.
	}

	// ── 프래그먼트(3d): 재조립 완료 전엔 전달 안 함 ──
	if (0 != (header.Flags & UdpProto::Flag_Fragment))
	{
		// 방어: 잘못된 조각 수/번호는 버림(워터마크·ack 는 이미 반영).
		if (0 == header.FragCount || header.FragIndex >= header.FragCount)
		{
			return;
		}

		Reassembly& ra = m_reassembly[header.MsgSeq];
		if (ra.Parts.empty())
		{
			ra.Channel   = channel;
			ra.MsgId     = msgId;
			ra.FragCount = header.FragCount;
			ra.Parts.resize(header.FragCount);
			ra.LastSeq   = seq;
		}
		if (ra.Parts[header.FragIndex].empty()) // dedup 이 seq 를 걸러도, 슬롯 이중기록 방어.
		{
			ra.Parts[header.FragIndex].assign(payload, payload + size);
			++ra.HaveCount;
		}
		if (seq > ra.LastSeq)
		{
			ra.LastSeq = seq;
		}

		if (ra.HaveCount < ra.FragCount)
		{
			return; // 아직 조각 부족.
		}

		// 완성 — 전체 페이로드 조립.
		std::vector<std::uint8_t> full;
		for (const auto& part : ra.Parts)
		{
			full.insert(full.end(), part.begin(), part.end());
		}
		const ENetChannel   raChannel = ra.Channel;
		const std::uint16_t raMsgId   = ra.MsgId;
		const std::uint32_t raLastSeq = ra.LastSeq;
		m_reassembly.erase(header.MsgSeq);

		if (ENetChannel::ReliableUnordered == raChannel)
		{
			deliver(raMsgId, full.data(), static_cast<std::uint32_t>(full.size())); // 즉시.
		}
		else
		{
			Buffered buffered;
			buffered.MsgId   = raMsgId;
			buffered.Payload = std::move(full);
			m_orderedPending.emplace(raLastSeq, std::move(buffered)); // 마지막 조각 seq 로 순서 배치.
		}
	}
	// ── 단일 데이터그램 메시지 ──
	else if (ENetChannel::ReliableUnordered == channel)
	{
		deliver(msgId, payload, size); // 순서 무관 — 즉시.
	}
	else
	{
		Buffered buffered;
		buffered.MsgId = msgId;
		buffered.Payload.assign(payload, payload + size);
		m_orderedPending.emplace(seq, std::move(buffered));
	}

	// Ordered flush: seq < m_recvNext(=앞선 seq 전부 수신) 인 것만 오름차순 방출.
	for (auto it = m_orderedPending.begin();
	     it != m_orderedPending.end() && it->first < m_recvNext; )
	{
		deliver(it->second.MsgId, it->second.Payload.data(),
			static_cast<std::uint32_t>(it->second.Payload.size()));
		it = m_orderedPending.erase(it);
	}
}

void CReliableEndpoint::FillAck(UdpProto::UdpDatagramHeader& header) const
{
	// AckBase = 다음 기대 seq(= 이 미만 seq 전부 수신). m_recvNext=0(아무 것도 연속 수신 못함,
	// 예: seq0 유실+상위 도착)일 때도 언더플로우 없이 "누적 없음"을 정확히 표현한다.
	header.Flags  |= UdpProto::Flag_Ack;
	header.AckBase = m_recvNext;
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

// 대기 중 ack 가 있으면 나가는 데이터그램에 얹어 별도 ack 패킷을 아낀다(피기백). 1회 소비.
void CReliableEndpoint::MaybePiggybackAck(UdpProto::UdpDatagramHeader& header)
{
	if (m_ackPending)
	{
		FillAck(header);
		m_ackPending = false;
		++m_piggybackAcks;
	}
}

// ── ACK 처리(송신측) ─────────────────────────────────────────────────────────────

void CReliableEndpoint::OnAck(std::uint32_t ackBase, std::uint32_t ackBits, double nowMs)
{
	double rttSample = -1.0;
	bool   acked     = false;

	// 누적: seq < ackBase 전부 해제(ackBase = 수신측 다음 기대 seq).
	for (auto it = m_unacked.begin(); it != m_unacked.end() && it->first < ackBase; )
	{
		if (1 == it->second.Sends) // Karn: 재전송 안 된 패킷만 RTT 표본.
		{
			rttSample = nowMs - it->second.LastSentMs;
		}
		it = m_unacked.erase(it);
		acked = true;
	}

	// 선택: ackBase+1+i (gap=ackBase 는 아직 미수신).
	for (std::uint32_t i = 0; i < 32; ++i)
	{
		if (0 == (ackBits & (1u << i)))
		{
			continue;
		}
		const std::uint32_t seq = ackBase + 1u + i;
		const auto it = m_unacked.find(seq);
		if (m_unacked.end() != it)
		{
			if (1 == it->second.Sends)
			{
				rttSample = nowMs - it->second.LastSentMs;
			}
			m_unacked.erase(it);
			acked = true;
		}
	}

	if (rttSample >= 0.0)
	{
		UpdateRtt(rttSample);
	}
	if (acked) // AIMD 가산 증가: 진행이 확인되면 윈도우를 조금 넓힌다.
	{
		m_cwnd = std::min(kMaxCwnd, m_cwnd + 1u);
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
	bool retransmitted = false;
	for (auto& entry : m_unacked)
	{
		Outbound& outbound = entry.second;
		const double timeout = m_rto * BackoffFactor(outbound.Sends);
		if (nowMs - outbound.LastSentMs < timeout)
		{
			continue;
		}

		UdpProto::UdpDatagramHeader header = MakeReliableHeader(entry.first, outbound.MsgId, outbound.Channel);
		if (outbound.Fragment) // 프래그먼트 헤더 필드 복원.
		{
			header.Flags    |= UdpProto::Flag_Fragment;
			header.MsgSeq    = outbound.MsgSeq;
			header.FragIndex = outbound.FragIndex;
			header.FragCount = outbound.FragCount;
		}
		MaybePiggybackAck(header); // 재전송에도 대기 ack 편승.
		emit(header, outbound.Payload.data(), static_cast<std::uint32_t>(outbound.Payload.size()));
		outbound.LastSentMs = nowMs;
		++outbound.Sends;
		retransmitted = true;
	}

	// 2) 유실 신호(재전송 발생) → AIMD 곱셈 감소(틱당 1회).
	if (retransmitted)
	{
		m_cwnd = std::max(kMinCwnd, m_cwnd / 2u);
	}

	// 3) ack 로 인플라이트가 빠졌으면 대기 큐를 cwnd 여유만큼 배수.
	DrainSendQueue(nowMs, emit);

	// 4) 지연 창(kAckDelayMs)을 넘도록 편승 못한 ack 만 독립 데이터그램으로 flush.
	//    그 안이면 다음 나가는 데이터에 편승할 기회를 준다(delayed ack). 창 < 최소 RTO 라
	//    송신측 재전송 전에 ack 가 도착한다.
	constexpr double kAckDelayMs = 25.0;
	if (m_ackPending && (nowMs - m_ackPendingSinceMs) >= kAckDelayMs)
	{
		UdpProto::UdpDatagramHeader header;
		header.Channel = ENetChannel::ReliableOrdered;
		FillAck(header);
		emit(header, nullptr, 0);
		m_ackPending = false;
		++m_standaloneAcks;
	}
}

void CReliableEndpoint::Reset()
{
	m_sendQueue.clear();
	m_nextSeq = 0; m_nextMsgSeq = 0;
	m_unacked.clear();
	m_cwnd = 16;
	m_srtt = 0.0; m_rttvar = 0.0; m_rto = 250.0; m_haveRtt = false;
	m_recvNext = 0;
	m_recvAhead.clear();
	m_ackPending = false;
	m_ackPendingSinceMs = 0.0;
	m_piggybackAcks = 0; m_standaloneAcks = 0;
	m_orderedPending.clear();
	m_reassembly.clear();
}

#endif // !JBRO_PLATFORM_WEB
