#pragma once

#include <JBro/Network/Types.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

// Reliable-over-UDP 데이터그램 v2 코덱. 순수 인코드·디코드이고 소켓을 모른다. 기존 엔진 `UdpProto` 와 같은 와이어다.
//
// 가변 길이 헤더 - 필드는 플래그가 켜졌을 때만 있어서 비신뢰 데이터그램이 ack·조각 오버헤드를 지지 않는다.
//
// 와이어(LE):
//   [token:8][flags:1][channel:1][seq:4]                  고정 접두(14)
//   if Ack:      [ackBase:4][ackBits:4]                    (+8)
//   if Fragment: [msgSeq:4][fragIndex:2][fragCount:2]      (+8)
//   [msgId:2][payload...]                                  항상
namespace JBro::Network::UdpProto
{
    enum DatagramFlags : std::uint8_t
    {
        FlagNone = 0,
        // 재전송 대상. 받는 쪽이 ACK 해야 한다.
        FlagReliable = 1u << 0,
        // ack 필드가 있다(피기백이든 순수 ack 든).
        FlagAck = 1u << 1,
        // 조각 필드가 있고 페이로드는 조각이다.
        FlagFragment = 1u << 2
    };

    struct DatagramHeader
    {
        std::uint64_t token = 0;
        std::uint8_t flags = FlagNone;
        NetChannel channel = NetChannel::Unreliable;
        // 연결별 단조 증가 송신 순번.
        std::uint32_t seq = 0;
        // FlagAck 일 때만. ackBase 는 "이 미만은 전부 받았다", ackBits 는 ackBase+1..+32 의 선택 확인.
        std::uint32_t ackBase = 0;
        std::uint32_t ackBits = 0;
        // FlagFragment 일 때만.
        std::uint32_t msgSeq = 0;
        std::uint16_t fragIndex = 0;
        std::uint16_t fragCount = 0;
        MessageId msgId = RawMessageId;
    };

    inline constexpr std::uint32_t FixedPrefixBytes = 8 + 1 + 1 + 4;
    inline constexpr std::uint32_t AckFieldBytes = 4 + 4;
    inline constexpr std::uint32_t FragmentFieldBytes = 4 + 2 + 2;
    inline constexpr std::uint32_t MsgIdBytes = 2;
    inline constexpr std::uint32_t MaxHeaderBytes = FixedPrefixBytes + AckFieldBytes + FragmentFieldBytes + MsgIdBytes;
    // 데이터그램 하나가 싣는 최대 페이로드. IP 분할을 피할 여유다. 넘는 신뢰 메시지는 조각낸다.
    inline constexpr std::uint32_t MaxPayloadBytes = 1024;
    inline constexpr std::uint32_t MaxDatagramBytes = MaxHeaderBytes + MaxPayloadBytes;

    inline std::uint32_t HeaderBytes(std::uint8_t flags)
    {
        std::uint32_t size = FixedPrefixBytes;
        if (0 != (flags & FlagAck))
        {
            size += AckFieldBytes;
        }
        if (0 != (flags & FlagFragment))
        {
            size += FragmentFieldBytes;
        }
        return size + MsgIdBytes;
    }

    inline void WriteU16(std::uint8_t* at, std::uint16_t value)
    {
        at[0] = static_cast<std::uint8_t>(value & 0xFFu);
        at[1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    }

    inline void WriteU32(std::uint8_t* at, std::uint32_t value)
    {
        for (int index = 0; index < 4; ++index)
        {
            at[index] = static_cast<std::uint8_t>((value >> (index * 8)) & 0xFFu);
        }
    }

    inline void WriteU64(std::uint8_t* at, std::uint64_t value)
    {
        for (int index = 0; index < 8; ++index)
        {
            at[index] = static_cast<std::uint8_t>((value >> (index * 8)) & 0xFFu);
        }
    }

    inline std::uint16_t ReadU16(const std::uint8_t* at)
    {
        return static_cast<std::uint16_t>(at[0] | (static_cast<std::uint16_t>(at[1]) << 8));
    }

    inline std::uint32_t ReadU32(const std::uint8_t* at)
    {
        std::uint32_t value = 0;
        for (int index = 0; index < 4; ++index)
        {
            value |= static_cast<std::uint32_t>(at[index]) << (index * 8);
        }
        return value;
    }

    inline std::uint64_t ReadU64(const std::uint8_t* at)
    {
        std::uint64_t value = 0;
        for (int index = 0; index < 8; ++index)
        {
            value |= static_cast<std::uint64_t>(at[index]) << (index * 8);
        }
        return value;
    }

    // `out` 은 HeaderBytes(flags) + payloadSize 이상이어야 한다. 쓴 바이트 수를 돌려준다.
    inline std::uint32_t Encode(const DatagramHeader& header, const void* payload, std::uint32_t payloadSize, std::uint8_t* out)
    {
        std::uint32_t offset = 0;
        WriteU64(out + offset, header.token);
        offset += 8;
        out[offset++] = header.flags;
        out[offset++] = static_cast<std::uint8_t>(header.channel);
        WriteU32(out + offset, header.seq);
        offset += 4;
        if (0 != (header.flags & FlagAck))
        {
            WriteU32(out + offset, header.ackBase);
            offset += 4;
            WriteU32(out + offset, header.ackBits);
            offset += 4;
        }
        if (0 != (header.flags & FlagFragment))
        {
            WriteU32(out + offset, header.msgSeq);
            offset += 4;
            WriteU16(out + offset, header.fragIndex);
            offset += 2;
            WriteU16(out + offset, header.fragCount);
            offset += 2;
        }
        WriteU16(out + offset, header.msgId);
        offset += 2;
        if (payloadSize > 0 && nullptr != payload)
        {
            std::memcpy(out + offset, payload, payloadSize);
            offset += payloadSize;
        }
        return offset;
    }

    // 잘린 버퍼면 거짓이다. 페이로드는 버퍼 안을 가리킨다.
    inline bool Decode(const std::uint8_t* buffer, std::uint32_t size, DatagramHeader& outHeader,
        const std::uint8_t*& outPayload, std::uint32_t& outPayloadSize)
    {
        if (size < FixedPrefixBytes)
        {
            return false;
        }
        std::uint32_t offset = 0;
        outHeader.token = ReadU64(buffer + offset);
        offset += 8;
        outHeader.flags = buffer[offset++];
        outHeader.channel = static_cast<NetChannel>(buffer[offset++]);
        outHeader.seq = ReadU32(buffer + offset);
        offset += 4;
        if (0 != (outHeader.flags & FlagAck))
        {
            if (size < offset + AckFieldBytes)
            {
                return false;
            }
            outHeader.ackBase = ReadU32(buffer + offset);
            offset += 4;
            outHeader.ackBits = ReadU32(buffer + offset);
            offset += 4;
        }
        if (0 != (outHeader.flags & FlagFragment))
        {
            if (size < offset + FragmentFieldBytes)
            {
                return false;
            }
            outHeader.msgSeq = ReadU32(buffer + offset);
            offset += 4;
            outHeader.fragIndex = ReadU16(buffer + offset);
            offset += 2;
            outHeader.fragCount = ReadU16(buffer + offset);
            offset += 2;
        }
        if (size < offset + MsgIdBytes)
        {
            return false;
        }
        outHeader.msgId = ReadU16(buffer + offset);
        offset += 2;
        outPayload = buffer + offset;
        outPayloadSize = size - offset;
        return true;
    }
}
