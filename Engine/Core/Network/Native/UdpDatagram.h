#pragma once

#include "Core/Platform/PlatformDefines.h"
#if !JBRO_PLATFORM_WEB

#include "Core/Network/NetworkTypes.h" // ENetChannel

#include <cstddef>
#include <cstdint>
#include <cstring>

// Reliable-over-UDP datagram v2 코덱(순수 인코드/디코드, 소켓 무관).
//
// 가변 길이 헤더 — 필드는 플래그가 설정됐을 때만 존재해 비신뢰 데이터그램이
// ack/frag 오버헤드를 지지 않는다. 필드 순서는 고정: prefix → ack → frag → msgId.
//
// 와이어(LE):
//   [token:8][flags:1][channel:1][seq:4]                  고정 prefix (14)
//   if Flag_Ack:      [ackBase:4][ackBits:4]              (+8) 3b 재전송
//   if Flag_Fragment: [msgSeq:4][fragIndex:2][fragCount:2](+8) 3d 프래그먼트
//   [msgId:2][payload...]                                 항상 msgId
//
// 하위호환 없음(미배포 in-dev). 비신뢰/punch 는 flags=0 으로 이 포맷을 탄다.
namespace UdpProto
{
	enum EDatagramFlags : std::uint8_t
	{
		Flag_None     = 0,
		Flag_Reliable = 1u << 0, // 재전송 대상 — 수신측이 ACK 해야 함(3b).
		Flag_Ack      = 1u << 1, // ack 필드 존재(피기백 또는 순수 ack, 3b).
		Flag_Fragment = 1u << 2, // frag 필드 존재; payload 는 조각(3d).
	};

	struct UdpDatagramHeader
	{
		std::uint64_t Token   = 0;
		std::uint8_t  Flags   = Flag_None;
		ENetChannel   Channel = ENetChannel::Unreliable;
		std::uint32_t Seq     = 0; // 전송 시퀀스(연결별 단조 증가).

		// Flag_Ack 일 때만 유효.
		std::uint32_t AckBase = 0; // 누적 ack — 이 seq 이하 전부 수신했음.
		std::uint32_t AckBits = 0; // AckBase-1..-32 선택 ack 비트필드.

		// Flag_Fragment 일 때만 유효.
		std::uint32_t MsgSeq    = 0; // 재조립 대상 메시지 식별자.
		std::uint16_t FragIndex = 0; // 조각 번호 0..FragCount-1.
		std::uint16_t FragCount = 0; // 총 조각 수.

		std::uint16_t MsgId = 0; // 항상.
	};

	constexpr std::size_t kFixedPrefixSize = 8u + 1u + 1u + 4u; // = 14
	constexpr std::size_t kAckFieldSize    = 4u + 4u;           // = 8
	constexpr std::size_t kFragFieldSize   = 4u + 2u + 2u;      // = 8
	constexpr std::size_t kMsgIdSize       = 2u;

	// 데이터그램 1개가 싣는 최대 payload(IP 분할 회피 여유). 초과 신뢰 메시지는 프래그먼트 분할.
	constexpr std::uint32_t kMaxPayload = 1024u;

	// 플래그로 결정되는 헤더 총 길이(payload 제외).
	inline std::size_t HeaderSize(std::uint8_t flags)
	{
		std::size_t size = kFixedPrefixSize;
		if (0 != (flags & Flag_Ack))      { size += kAckFieldSize; }
		if (0 != (flags & Flag_Fragment)) { size += kFragFieldSize; }
		size += kMsgIdSize;
		return size;
	}

	// ── LE 직렬화 헬퍼 ──
	inline void WriteU16LE(std::uint8_t* p, std::uint16_t v)
	{
		p[0] = static_cast<std::uint8_t>(v & 0xFFu);
		p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
	}
	inline void WriteU32LE(std::uint8_t* p, std::uint32_t v)
	{
		for (int i = 0; i < 4; ++i) { p[i] = static_cast<std::uint8_t>((v >> (i * 8)) & 0xFFu); }
	}
	inline void WriteU64LE(std::uint8_t* p, std::uint64_t v)
	{
		for (int i = 0; i < 8; ++i) { p[i] = static_cast<std::uint8_t>((v >> (i * 8)) & 0xFFu); }
	}
	inline std::uint16_t ReadU16LE(const std::uint8_t* p)
	{
		return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
	}
	inline std::uint32_t ReadU32LE(const std::uint8_t* p)
	{
		std::uint32_t v = 0;
		for (int i = 0; i < 4; ++i) { v |= static_cast<std::uint32_t>(p[i]) << (i * 8); }
		return v;
	}
	inline std::uint64_t ReadU64LE(const std::uint8_t* p)
	{
		std::uint64_t v = 0;
		for (int i = 0; i < 8; ++i) { v |= static_cast<std::uint64_t>(p[i]) << (i * 8); }
		return v;
	}

	// 헤더+payload 를 out 에 기록. out 은 HeaderSize(h.Flags)+payloadSize 이상이어야 함.
	// 반환 = 기록한 총 바이트 수.
	inline std::size_t Encode(const UdpDatagramHeader& h,
		const void* payload, std::uint32_t payloadSize, std::uint8_t* out)
	{
		std::size_t o = 0;
		WriteU64LE(out + o, h.Token);                      o += 8;
		out[o++] = h.Flags;
		out[o++] = static_cast<std::uint8_t>(h.Channel);
		WriteU32LE(out + o, h.Seq);                        o += 4;

		if (0 != (h.Flags & Flag_Ack))
		{
			WriteU32LE(out + o, h.AckBase);                o += 4;
			WriteU32LE(out + o, h.AckBits);                o += 4;
		}
		if (0 != (h.Flags & Flag_Fragment))
		{
			WriteU32LE(out + o, h.MsgSeq);                 o += 4;
			WriteU16LE(out + o, h.FragIndex);              o += 2;
			WriteU16LE(out + o, h.FragCount);              o += 2;
		}
		WriteU16LE(out + o, h.MsgId);                      o += 2;

		if (payloadSize > 0 && nullptr != payload)
		{
			std::memcpy(out + o, payload, payloadSize);    o += payloadSize;
		}
		return o;
	}

	// buf(size) 를 헤더+payload 로 해석. 성공 시 true 반환 + outHeader 채움 +
	// outPayload/outPayloadSize 설정(payload 는 buf 내부를 가리킴, 복사 없음).
	// 잘린 버퍼(필드 미달)면 false.
	inline bool Decode(const std::uint8_t* buf, std::size_t size,
		UdpDatagramHeader& outHeader, const std::uint8_t*& outPayload, std::uint32_t& outPayloadSize)
	{
		if (size < kFixedPrefixSize)
		{
			return false;
		}
		std::size_t o = 0;
		outHeader.Token   = ReadU64LE(buf + o);            o += 8;
		outHeader.Flags   = buf[o++];
		outHeader.Channel = static_cast<ENetChannel>(buf[o++]);
		outHeader.Seq     = ReadU32LE(buf + o);            o += 4;

		if (0 != (outHeader.Flags & Flag_Ack))
		{
			if (size < o + kAckFieldSize) { return false; }
			outHeader.AckBase = ReadU32LE(buf + o);        o += 4;
			outHeader.AckBits = ReadU32LE(buf + o);        o += 4;
		}
		if (0 != (outHeader.Flags & Flag_Fragment))
		{
			if (size < o + kFragFieldSize) { return false; }
			outHeader.MsgSeq    = ReadU32LE(buf + o);      o += 4;
			outHeader.FragIndex = ReadU16LE(buf + o);      o += 2;
			outHeader.FragCount = ReadU16LE(buf + o);      o += 2;
		}
		if (size < o + kMsgIdSize) { return false; }
		outHeader.MsgId = ReadU16LE(buf + o);              o += 2;

		outPayload     = buf + o;
		outPayloadSize = static_cast<std::uint32_t>(size - o);
		return true;
	}
}

#endif // !JBRO_PLATFORM_WEB
