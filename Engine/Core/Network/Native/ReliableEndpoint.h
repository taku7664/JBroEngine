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
// 3d = 프래그먼트: kMaxPayload 초과 메시지를 조각으로 나눠 각각 신뢰 전달 후 재조립.
// 3e = 최소 혼잡: 인플라이트를 혼잡 윈도우(cwnd, AIMD)로 제한, 초과분은 송신 큐에 대기.
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

	// 신뢰 메시지를 송신 큐에 넣고 혼잡 윈도우 여유만큼 즉시 송출. kMaxPayload 초과면 프래그먼트 분할.
	void SendReliable(ENetChannel channel, std::uint16_t msgId, const void* data, std::uint32_t size,
		double nowMs, const FEmit& emit);

	// 수신한 신뢰 데이터그램 처리. dedup + (프래그먼트면 재조립) 후 채널 규율대로 deliver. 항상 ack 예약.
	void OnReliableReceived(const UdpProto::UdpDatagramHeader& header,
		const std::uint8_t* payload, std::uint32_t size, const FDeliver& deliver);

	// 수신 데이터그램의 ack 필드 처리 → unacked 해제 + RTT 갱신 + 혼잡 윈도우 증가.
	void OnAck(std::uint32_t ackBase, std::uint32_t ackBits, double nowMs);

	// 주기 틱: RTO 만료분 재전송(지수 백오프) + 송신 큐 배수 + 대기 중 ack flush. 매 Poll 호출.
	void Tick(double nowMs, const FEmit& emit);

	void Reset();

	// 진단.
	std::size_t   UnackedCount() const { return m_unacked.size(); }
	std::size_t   QueuedCount()  const { return m_sendQueue.size(); }
	double        CurrentRtoMs() const { return m_rto; }
	double        SmoothedRttMs() const { return m_srtt; }
	std::uint32_t CongestionWindow() const { return m_cwnd; }

private:
	void UpdateRtt(double sampleMs);
	void FillAck(UdpProto::UdpDatagramHeader& header) const; // Flag_Ack + AckBase/AckBits 세팅.
	void DrainSendQueue(double nowMs, const FEmit& emit); // 창(cwnd + seq범위) 여유만큼 큐→와이어.

private:
	// ── 송신 큐(혼잡 제어) ──
	// 전송 단위 = 데이터그램 1개(작은 메시지 전체 또는 프래그먼트 1조각). seq 는 실제 송신 때 할당해
	// 연속성을 보장하고, 인플라이트 seq 범위를 ACK 창 이하로 유지(모든 인플라이트가 ACK 가능).
	struct SendUnit
	{
		ENetChannel               Channel   = ENetChannel::ReliableOrdered;
		std::uint16_t             MsgId     = 0;
		bool                      Fragment  = false;
		std::uint32_t             MsgSeq    = 0;
		std::uint16_t             FragIndex = 0;
		std::uint16_t             FragCount = 0;
		std::vector<std::uint8_t> Payload;
	};
	std::vector<SendUnit> m_sendQueue; // FIFO(앞이 오래된 것).

	// ── 송신측(인플라이트) ──
	std::uint32_t m_nextSeq    = 0;
	std::uint32_t m_nextMsgSeq = 0; // 프래그먼트 메시지 재조립 식별자.
	struct Outbound
	{
		std::vector<std::uint8_t> Payload;
		std::uint16_t             MsgId      = 0;
		ENetChannel               Channel    = ENetChannel::ReliableOrdered; // 재전송 시 원 채널 보존.
		double                    LastSentMs = 0.0;
		std::uint32_t             Sends      = 0; // 총 송신 횟수(1=최초). Karn: 1 일 때만 RTT 표본.
		// 프래그먼트(3d): Fragment 면 재전송 헤더에 아래를 실어야 한다.
		bool                      Fragment   = false;
		std::uint32_t             MsgSeq     = 0;
		std::uint16_t             FragIndex  = 0;
		std::uint16_t             FragCount  = 0;
	};
	std::map<std::uint32_t, Outbound> m_unacked; // seq 오름차순 — 누적 ack 스캔에 유리.

	// ── 혼잡 윈도우(3e, AIMD) ──
	std::uint32_t m_cwnd = 16; // 인플라이트 패킷 상한. ack 마다 +1, 재전송(유실) 시 반감.

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

	// 프래그먼트 재조립 버퍼(3d): MsgSeq → 조각 모음.
	struct Reassembly
	{
		ENetChannel                            Channel   = ENetChannel::ReliableOrdered;
		std::uint16_t                          MsgId     = 0;
		std::uint16_t                          FragCount = 0;
		std::uint16_t                          HaveCount = 0;
		std::uint32_t                          LastSeq   = 0; // 최대 조각 seq = 순서 방출 키.
		std::vector<std::vector<std::uint8_t>> Parts;         // [FragCount].
	};
	std::map<std::uint32_t, Reassembly> m_reassembly;
};

#endif // !JBRO_PLATFORM_WEB
