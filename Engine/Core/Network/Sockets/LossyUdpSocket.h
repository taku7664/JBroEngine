#pragma once

#include "Core/Platform/PlatformDefines.h"
#if !JBRO_PLATFORM_WEB

#include "Core/Network/Sockets/IUdpSocket.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

// 테스트 전용 데코레이터. 내부 IUdpSocket 위에 결정론적 패킷 유실/중복/재정렬을
// 주입한다. 루프백은 사실상 무손실이라 Reliable-over-UDP 엔진(재전송/dedup/재정렬
// 버퍼)이 실제로 도는지 검증하려면 인위적 유실이 필요하다.
//
// 헤더 온리. Engine.vcxproj 에 등록하지 않는다 — 프로덕션 빌드 영향 0, 테스트가
// 직접 include 할 때만 컴파일된다. 시드 기반 PRNG 라 같은 시드 = 같은 유실 패턴
// (실네트워크의 랜덤성과 달리 재현 가능한 디버깅).

struct LossyUdpConfig
{
	double        LossRate     = 0.0; // SendTo 당 폐기 확률 [0,1).
	double        DupRate      = 0.0; // 추가 사본 송신 확률 [0,1).
	std::uint32_t ReorderDepth = 0;   // 홀드백 큐 깊이. >0 이면 순서 뒤섞음.
	std::uint32_t Seed         = 1;   // PRNG 시드(0 은 1 로 승격).
};

class CLossyUdpSocket final : public IUdpSocket
{
public:
	explicit CLossyUdpSocket(OwnerPtr<IUdpSocket> inner, const LossyUdpConfig& config)
		: m_inner(std::move(inner))
		, m_config(config)
		, m_rngState(0 == config.Seed ? 1u : config.Seed)
	{
	}

	~CLossyUdpSocket() override
	{
		Flush();
	}

	// ── 위임(무변경) ──
	bool Open() override                                { return m_inner && m_inner->Open(); }
	bool Bind(std::uint16_t port) override              { return m_inner && m_inner->Bind(port); }
	bool IsValid() const override                       { return m_inner && m_inner->IsValid(); }

	bool Resolve(const char* host, std::uint16_t port, NetUdpEndpoint& outEndpoint) override
	{
		return m_inner && m_inner->Resolve(host, port, outEndpoint);
	}

	// 수신은 손대지 않는다 — 송신측 유실만으로 와이어 유실을 완전히 모델링한다.
	ESocketIo RecvFrom(void* buffer, std::size_t bufferSize, std::size_t& outBytes, NetUdpEndpoint& outFrom) override
	{
		return m_inner ? m_inner->RecvFrom(buffer, bufferSize, outBytes, outFrom)
		               : ESocketIo::Error;
	}

	void Close() override
	{
		Flush();
		if (m_inner)
		{
			m_inner->Close();
		}
	}

	// ── 유실/중복/재정렬 주입 ──
	ESocketIo SendTo(const NetUdpEndpoint& to, const void* data, std::size_t size) override
	{
		if (!m_inner)
		{
			return ESocketIo::Error;
		}

		// 유실: 스택은 정상 수락(Ok), 패킷은 와이어에서 사라짐.
		if (NextUnit() < m_config.LossRate)
		{
			++m_dropped;
			return ESocketIo::Ok;
		}

		Enqueue(to, data, size);

		// 중복: 같은 데이터그램을 한 번 더 큐에 넣는다.
		if (NextUnit() < m_config.DupRate)
		{
			Enqueue(to, data, size);
			++m_duped;
		}

		// 재정렬: 큐가 깊이를 초과하면 하나를 방출한다. depth 0 이면 FIFO(순서 유지),
		// depth>0 이면 큐 내 임의 원소를 골라 방출해 순서를 뒤섞는다.
		while (m_pending.size() > m_config.ReorderDepth)
		{
			const std::size_t index =
				(0 == m_config.ReorderDepth) ? 0 : NextIndex(m_pending.size());
			FlushOne(index);
		}

		return ESocketIo::Ok;
	}

	// 남은 홀드백을 FIFO 로 모두 방출한다(테스트 종료 시 드레인).
	void Flush()
	{
		while (!m_pending.empty())
		{
			FlushOne(0);
		}
	}

	// ── 검증용 카운터 ──
	std::uint32_t DroppedCount()   const { return m_dropped; }
	std::uint32_t DuplicatedCount() const { return m_duped; }
	std::uint32_t ReorderedCount() const { return m_reordered; }

private:
	struct Pending
	{
		NetUdpEndpoint            To;
		std::vector<std::uint8_t> Bytes;
	};

	void Enqueue(const NetUdpEndpoint& to, const void* data, std::size_t size)
	{
		Pending pending;
		pending.To = to;
		pending.Bytes.resize(size);
		if (0 != size)
		{
			std::memcpy(pending.Bytes.data(), data, size);
		}
		m_pending.push_back(std::move(pending));
	}

	void FlushOne(std::size_t index)
	{
		if (index >= m_pending.size())
		{
			return;
		}
		if (0 != index)
		{
			++m_reordered; // 도착 순서가 큐 진입 순서와 다름.
		}

		const Pending& pending = m_pending[index];
		m_inner->SendTo(pending.To, pending.Bytes.data(), pending.Bytes.size());
		m_pending.erase(m_pending.begin() + static_cast<std::ptrdiff_t>(index));
	}

	// xorshift32 — 결정론적, 시드로 완전 재현.
	std::uint32_t NextU32()
	{
		std::uint32_t x = m_rngState;
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		m_rngState = x;
		return x;
	}

	double NextUnit() // [0,1)
	{
		return static_cast<double>(NextU32() >> 8) * (1.0 / 16777216.0);
	}

	std::size_t NextIndex(std::size_t count)
	{
		return static_cast<std::size_t>(NextU32() % static_cast<std::uint32_t>(count));
	}

private:
	OwnerPtr<IUdpSocket> m_inner;
	LossyUdpConfig       m_config;
	std::uint32_t        m_rngState = 1;

	std::vector<Pending> m_pending;

	std::uint32_t m_dropped    = 0;
	std::uint32_t m_duped      = 0;
	std::uint32_t m_reordered  = 0;
};

#endif // !JBRO_PLATFORM_WEB
