#pragma once

#include "Core/Platform/PlatformDefines.h"

#if JBRO_PLATFORM_WINDOWS

#include "Core/RHI/IRHIGpuTimer.h"

#include <cstdint>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Query;

// D3D11 GPU 타이머. ID3D11Query(TIMESTAMP / TIMESTAMP_DISJOINT)로 구간 GPU 시간을 잰다.
//
// 프레임마다 disjoint 쿼리 하나 + 구간 경계마다 타임스탬프 쿼리 두 개를 커맨드 스트림에 꽂는다.
// FRAME_COUNT 개의 슬롯을 돌려쓰며, 어떤 슬롯을 다시 쓸 차례가 오면 그 슬롯은 이미 N프레임 전
// 것이라 결과가 GPU 에서 준비돼 있다(비동기 리드백, 스톨 없음). disjoint 는 클럭 변동을 알려줘
// 그 프레임 값이 유효한지 판정한다.
class CD3D11GpuTimer final : public IRHIGpuTimer
{
public:
	CD3D11GpuTimer(ID3D11Device* device, ID3D11DeviceContext* context);
	~CD3D11GpuTimer() override;

	bool IsSupported() const override { return m_supported; }
	void SetEnabled(bool enabled) override { m_enabled = enabled; }

	void BeginFrame() override;
	void EndFrame() override;
	std::uint32_t BeginScope(const void* key) override;
	void EndScope(std::uint32_t scopeId) override;
	const std::vector<GpuTimerResult>& GetResults() const override { return m_results; }

private:
	static constexpr int FRAME_COUNT = 3;

	struct ScopeRecord
	{
		const void*   Key = nullptr;
		std::uint32_t BeginIndex = INVALID_GPU_SCOPE;   // m_slots[i].Timestamps 인덱스
		std::uint32_t EndIndex = INVALID_GPU_SCOPE;
	};

	struct FrameSlot
	{
		ID3D11Query*              Disjoint = nullptr;
		std::vector<ID3D11Query*> Timestamps;   // 재사용 풀 — 프레임마다 앞에서부터 다시 쓴다
		std::vector<ScopeRecord>  Scopes;
		std::uint32_t             UsedTimestamps = 0;
		bool                      Pending = false;
	};

	// 이번 프레임 슬롯에서 다음 타임스탬프 쿼리를 확보한다(없으면 생성). 실패 시 nullptr.
	ID3D11Query* AcquireTimestamp(FrameSlot& slot, std::uint32_t& outIndex);
	// N프레임 전 슬롯의 결과를 읽어 m_results 에 반영한다(준비 안 됐거나 disjoint 면 버린다).
	void Collect(FrameSlot& slot);
	void ReleaseSlot(FrameSlot& slot);

	ID3D11Device*        m_device = nullptr;
	ID3D11DeviceContext* m_context = nullptr;
	bool                 m_supported = false;
	bool                 m_enabled = false;
	bool                 m_frameOpen = false;

	FrameSlot            m_slots[FRAME_COUNT];
	int                  m_currentSlot = 0;
	std::uint64_t        m_frameCounter = 0;

	std::vector<GpuTimerResult> m_results;
};

#endif
