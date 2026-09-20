#pragma once

#include <JBro/Network/Socket.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/SafePtr.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace JBro::Network::Testing
{
    struct LossyConfig
    {
        // SendTo 하나가 사라질 확률 [0,1).
        double lossRate = 0.0;
        // 사본을 한 번 더 보낼 확률 [0,1).
        double duplicateRate = 0.0;
        // 홀드백 큐 깊이. 0 이면 FIFO(순서 유지), 크면 큐 안의 임의 원소를 내보내 순서를 뒤섞는다.
        std::uint32_t reorderDepth = 0;
        // 0 은 1 로 올린다. 같은 시드는 같은 유실 무늬다.
        std::uint32_t seed = 1;
    };

    // 테스트 전용 데코레이터. 안쪽 `IDatagramSocket` 위에 결정론적 유실·중복·재정렬을 얹는다. 루프백은 사실상 무손실이라
    // 신뢰 엔진(재전송·dedup·재정렬 버퍼)이 실제로 도는지 보려면 인위적 유실이 필요하다. 기존 엔진 `CLossyUdpSocket` 을 잇는다.
    // 수신은 손대지 않는다 - 송신 쪽 유실만으로 와이어 유실을 온전히 흉내 낸다.
    class LossyDatagramSocket final : public IDatagramSocket
    {
    public:
        static constexpr std::uint32_t MaxDatagramBytes = 1500;

        LossyDatagramSocket(OwnerPtr<IDatagramSocket> inner, const LossyConfig& config)
            : m_inner(std::move(inner))
            , m_config(config)
            , m_state(0 == config.seed ? 1u : config.seed)
        {
            m_pending.Resize(config.reorderDepth + 2);
        }

        ~LossyDatagramSocket() override
        {
            Flush();
        }

        bool Open() override
        {
            return nullptr != m_inner.Get() && m_inner->Open();
        }

        bool Bind(std::uint16_t port) override
        {
            return nullptr != m_inner.Get() && m_inner->Bind(port);
        }

        bool Resolve(const char* host, std::uint16_t port, Endpoint& outEndpoint) override
        {
            return nullptr != m_inner.Get() && m_inner->Resolve(host, port, outEndpoint);
        }

        SocketIo ReceiveFrom(void* buffer, std::size_t capacity, std::size_t& outReceived, Endpoint& outFrom) override
        {
            if (nullptr == m_inner.Get())
            {
                return SocketIo::Error;
            }
            return m_inner->ReceiveFrom(buffer, capacity, outReceived, outFrom);
        }

        void Close() override
        {
            Flush();
            if (nullptr != m_inner.Get())
            {
                m_inner->Close();
            }
        }

        bool IsOpen() const override
        {
            return nullptr != m_inner.Get() && m_inner->IsOpen();
        }

        SocketIo SendTo(const Endpoint& to, const void* data, std::size_t size) override
        {
            if (nullptr == m_inner.Get() || size > MaxDatagramBytes)
            {
                return SocketIo::Error;
            }
            // 유실: 스택은 받아들이고(Ok) 패킷은 와이어에서 사라진다.
            if (NextUnit() < m_config.lossRate)
            {
                ++m_dropped;
                return SocketIo::Ok;
            }
            Enqueue(to, data, size);
            if (NextUnit() < m_config.duplicateRate)
            {
                Enqueue(to, data, size);
                ++m_duplicated;
            }
            // 깊이를 넘는 만큼 내보낸다. 깊이 0 이면 앞에서부터, 아니면 임의 원소다.
            while (m_pendingCount > m_config.reorderDepth)
            {
                const std::uint32_t index = (0 == m_config.reorderDepth) ? 0 : NextIndex(m_pendingCount);
                FlushOne(index);
            }
            return SocketIo::Ok;
        }

        // 남은 홀드백을 순서대로 다 내보낸다.
        void Flush()
        {
            while (m_pendingCount > 0)
            {
                FlushOne(0);
            }
        }

        std::uint32_t DroppedCount() const
        {
            return m_dropped;
        }

        std::uint32_t DuplicatedCount() const
        {
            return m_duplicated;
        }

        std::uint32_t ReorderedCount() const
        {
            return m_reordered;
        }

    private:
        struct Pending
        {
            Endpoint to;
            std::uint32_t size = 0;
            std::uint8_t bytes[MaxDatagramBytes] = {};
        };

        void Enqueue(const Endpoint& to, const void* data, std::size_t size)
        {
            if (m_pendingCount >= m_pending.Size())
            {
                FlushOne(0);
            }
            Pending& pending = m_pending[m_pendingCount++];
            pending.to = to;
            pending.size = static_cast<std::uint32_t>(size);
            if (size > 0)
            {
                std::memcpy(pending.bytes, data, size);
            }
        }

        void FlushOne(std::uint32_t index)
        {
            if (index >= m_pendingCount)
            {
                return;
            }
            if (0 != index)
            {
                ++m_reordered;
            }
            const Pending& pending = m_pending[index];
            m_inner->SendTo(pending.to, pending.bytes, pending.size);
            for (std::uint32_t at = index; at + 1 < m_pendingCount; ++at)
            {
                m_pending[at] = m_pending[at + 1];
            }
            --m_pendingCount;
        }

        std::uint32_t NextU32()
        {
            std::uint32_t x = m_state;
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            m_state = x;
            return x;
        }

        double NextUnit()
        {
            return static_cast<double>(NextU32() >> 8) * (1.0 / 16777216.0);
        }

        std::uint32_t NextIndex(std::uint32_t count)
        {
            return NextU32() % count;
        }

        OwnerPtr<IDatagramSocket> m_inner;
        LossyConfig m_config;
        std::uint32_t m_state = 1;
        Array<Pending> m_pending;
        std::uint32_t m_pendingCount = 0;
        std::uint32_t m_dropped = 0;
        std::uint32_t m_duplicated = 0;
        std::uint32_t m_reordered = 0;
    };
}
