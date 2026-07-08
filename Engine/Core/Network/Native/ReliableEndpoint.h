#pragma once

#include "Core/Platform/PlatformDefines.h"
#if !JBRO_PLATFORM_WEB

#include "Core/Network/Native/UdpDatagram.h"

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <vector>

// 한 연결(서버-피어 하나 또는 클라-서버)의 신뢰 전달 엔진.
// UDP 위에 재전송/ACK/dedup 을 얹어 신뢰 데이터그램을 유실이 있어도 정확히 한 번 전달한다.
//
// 전달 규율은 채널별:
//   ReliableUnordered = 최초 수신 즉시 전달(HOL 회피).
//   ReliableOrdered   = 재정렬 버퍼 — 앞선 seq 가 전부 수신될 때까지 보류 후 순서대로 방출.
// 두 채널은 단일 신뢰 seq 공간을 공유한다. 순서 판정은 수신 워터마크(m_recvNext)로 한다:
// seq S 의 Ordered 메시지는 seq<=S 가 전부 수신됐을 때(=m_recvNext>S) 방출 가능.
//
// 3b = 유실 복구(재전송) + 정확히 한 번(dedup) + RTT 기반 RTO. 3c = 위 순서 규율.
//
// 소켓을 직접 만지지 않는다 — 송출은 FEmit 콜백(UdpChannel 이 Token 세팅 + 엔드포인트로 SendTo).
// 시간은 호출측이 밀리초로 주입(단일 시계 = UdpChannel::NowMs).
class CReliableEndpoint
{
public:
	// 신뢰 데이터그램 송출. header 는 Token 을 제외한 전 필드가 채워진 상태로 넘어온다.
	using FEmit = std::function<void(UdpProto::UdpDatagramHeader& header, const void* payload, std::uint32_t size)>;

	// 상위로 메시지를 전달(즉시 또는 순서 흐름에서). (msgId, payload, size).
	using FDeliver = std::function<void(std::uint16_t msgId, const std::uint8_t* payload, std::uint32_t size)>;

	// 신뢰 메시지 큐잉 + 즉시 1회 송신. size 는 UDP_MAX_PAYLOAD 이하 가정(프래그먼트는 3d).
	void SendReliable(ENetChannel channel, std::uint16_t msgId, const void* data, std::uint32_t size,
		double nowMs, const FEmit& emit);

	// 수신한 신뢰 데이터그램 처리. dedup 후 채널 규율대로 deliver 콜백 호출(즉시/순서). 항상 ack 예약.
	void OnReliableReceived(std::uint32_t seq, ENetChannel channel,
		std::uint16_t msgId, const std::uint8_t* payload, std::uint32_t size, const FDeliver& deliver);

	// 수신 데이터그램의 ack 필드 처리 → unacked 해제 + RTT 갱신.
	void OnAck(std::uint32_t ackBase, std::uint32_t ackBits, double nowMs);

	// 주기 틱: RTO 만료분 재전송(지수 백오프) + 대기 중 ack flush. 매 Poll 호출.
	void Tick(double nowMs, const FEmit& emit);

	void Reset();

	// 진단.
	std::size_t   UnackedCount() const { return m_unacked.size(); }
	double        CurrentRtoMs() const { return m_rto; }
	double        SmoothedRttMs() const { return m_srtt; }

private:
	void UpdateRtt(double sampleMs);
	void FillAck(UdpProto::UdpDatagramHeader& header) const; // Flag_Ack + AckBase/AckBits 세팅.

private:
	// ── 송신측 ──
	std::uint32_t m_nextSeq = 0;
	struct Outbound
	{
		std::vector<std::uint8_t> Payload;
		std::uint16_t             MsgId      = 0;
		ENetChannel               Channel    = ENetChannel::ReliableOrdered; // 재전송 시 원 채널 보존.
		double                    LastSentMs = 0.0;
		std::uint32_t             Sends      = 0; // 총 송신 횟수(1=최초). Karn: 1 일 때만 RTT 표본.
	};
	std::map<std::uint32_t, Outbound> m_unacked; // seq 오름차순 — 누적 ack 스캔에 유리.

	// ── RTT/RTO (ms, Jacobson/Karels) ──
	double m_srtt    = 0.0;
	double m_rttvar  = 0.0;
	double m_rto     = 250.0; // 초기 RTO(표본 전).
	bool   m_haveRtt = false;

	// ── 수신측 ──
	std::uint32_t           m_recvNext = 0;     // 이 미만 seq 는 전부 수신됨(누적 경계 = 순서 워터마크).
	std::set<std::uint32_t> m_recvAhead;        // gap 위로 수신한 seq(dedup + 선택 ack 비트).
	bool                    m_ackPending = false;

	// Ordered 재정렬 버퍼: 아직 순서가 안 된 Ordered 메시지를 seq 기준 보류.
	struct Buffered { std::uint16_t MsgId = 0; std::vector<std::uint8_t> Payload; };
	std::map<std::uint32_t, Buffered> m_orderedPending;
};

#endif // !JBRO_PLATFORM_WEB
