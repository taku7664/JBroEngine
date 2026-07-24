#include "pch.h"
#include "D3D11GpuTimer.h"

#if JBRO_PLATFORM_WINDOWS

#include <d3d11.h>

CD3D11GpuTimer::CD3D11GpuTimer(ID3D11Device* device, ID3D11DeviceContext* context)
	: m_device(device)
	, m_context(context)
{
	// disjoint 쿼리 생성 성공 여부로 타임스탬프 지원을 판정한다(FL10+ 지원, 일부 저사양 미지원).
	if (m_device)
	{
		D3D11_QUERY_DESC desc = {};
		desc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
		ID3D11Query* probe = nullptr;
		if (SUCCEEDED(m_device->CreateQuery(&desc, &probe)) && nullptr != probe)
		{
			m_supported = true;
			probe->Release();
		}
	}
}

CD3D11GpuTimer::~CD3D11GpuTimer()
{
	for (FrameSlot& slot : m_slots)
	{
		ReleaseSlot(slot);
	}
}

void CD3D11GpuTimer::ReleaseSlot(FrameSlot& slot)
{
	if (slot.Disjoint)
	{
		slot.Disjoint->Release();
		slot.Disjoint = nullptr;
	}
	for (ID3D11Query* query : slot.Timestamps)
	{
		if (query)
		{
			query->Release();
		}
	}
	slot.Timestamps.clear();
	slot.Scopes.clear();
	slot.UsedTimestamps = 0;
	slot.Pending = false;
}

ID3D11Query* CD3D11GpuTimer::AcquireTimestamp(FrameSlot& slot, std::uint32_t& outIndex)
{
	outIndex = slot.UsedTimestamps;
	if (outIndex >= slot.Timestamps.size())
	{
		D3D11_QUERY_DESC desc = {};
		desc.Query = D3D11_QUERY_TIMESTAMP;
		ID3D11Query* query = nullptr;
		if (FAILED(m_device->CreateQuery(&desc, &query)) || nullptr == query)
		{
			return nullptr;
		}
		slot.Timestamps.push_back(query);
	}
	++slot.UsedTimestamps;
	return slot.Timestamps[outIndex];
}

void CD3D11GpuTimer::BeginFrame()
{
	if (false == m_supported || false == m_enabled)
	{
		// 꺼져 있으면 아무것도 재지 않고 결과도 비운다(끈 순간 옛 값이 남지 않도록).
		m_frameOpen = false;
		if (false == m_results.empty())
		{
			m_results.clear();
		}
		return;
	}

	m_currentSlot = static_cast<int>(m_frameCounter % FRAME_COUNT);
	FrameSlot& slot = m_slots[m_currentSlot];

	// 이 슬롯은 N프레임 전 것 — GPU 가 이미 끝냈으므로 스톨 없이 읽힌다.
	if (slot.Pending)
	{
		Collect(slot);
	}

	slot.Scopes.clear();
	slot.UsedTimestamps = 0;

	if (nullptr == slot.Disjoint)
	{
		D3D11_QUERY_DESC desc = {};
		desc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
		if (FAILED(m_device->CreateQuery(&desc, &slot.Disjoint)) || nullptr == slot.Disjoint)
		{
			m_frameOpen = false;
			return;
		}
	}

	m_context->Begin(slot.Disjoint);
	m_frameOpen = true;
	// 암묵 구간은 두지 않는다 — 무엇을 잴지는 호출자가 BeginScope 로 정한다(게임뷰 총합=nullptr 키).
	// 그래야 게임뷰가 안 그려지면 타임스탬프가 하나도 안 꽂혀 결과가 0 이 된다.
}

void CD3D11GpuTimer::EndFrame()
{
	if (false == m_frameOpen)
	{
		return;
	}

	FrameSlot& slot = m_slots[m_currentSlot];
	m_context->End(slot.Disjoint);
	slot.Pending = true;

	m_frameOpen = false;
	++m_frameCounter;
}

std::uint32_t CD3D11GpuTimer::BeginScope(const void* key)
{
	if (false == m_frameOpen)
	{
		return INVALID_GPU_SCOPE;
	}

	FrameSlot& slot = m_slots[m_currentSlot];
	std::uint32_t index = 0;
	ID3D11Query* query = AcquireTimestamp(slot, index);
	if (nullptr == query)
	{
		return INVALID_GPU_SCOPE;
	}

	m_context->End(query);   // TIMESTAMP 쿼리는 End() 시점의 GPU 시각을 기록한다.

	const std::uint32_t scopeId = static_cast<std::uint32_t>(slot.Scopes.size());
	ScopeRecord record;
	record.Key = key;
	record.BeginIndex = index;
	record.EndIndex = INVALID_GPU_SCOPE;
	slot.Scopes.push_back(record);
	return scopeId;
}

void CD3D11GpuTimer::EndScope(std::uint32_t scopeId)
{
	if (INVALID_GPU_SCOPE == scopeId || false == m_frameOpen)
	{
		return;
	}

	FrameSlot& slot = m_slots[m_currentSlot];
	if (scopeId >= slot.Scopes.size())
	{
		return;
	}

	std::uint32_t index = 0;
	ID3D11Query* query = AcquireTimestamp(slot, index);
	if (nullptr == query)
	{
		return;
	}

	m_context->End(query);
	slot.Scopes[scopeId].EndIndex = index;
}

void CD3D11GpuTimer::Collect(FrameSlot& slot)
{
	slot.Pending = false;

	D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint = {};
	// DONOTFLUSH — N프레임 전 것이라 이미 준비돼 있어 플러시 불필요(스톨 방지). 혹시 안 됐으면 버린다.
	if (S_OK != m_context->GetData(slot.Disjoint, &disjoint, sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH))
	{
		return;
	}
	if (disjoint.Disjoint || 0 == disjoint.Frequency)
	{
		// 클럭 변동 등으로 이 프레임 타임스탬프는 신뢰 불가 — 이전 결과를 유지한다.
		return;
	}

	std::vector<GpuTimerResult> results;
	results.reserve(slot.Scopes.size());
	for (const ScopeRecord& record : slot.Scopes)
	{
		if (INVALID_GPU_SCOPE == record.EndIndex
			|| record.BeginIndex >= slot.Timestamps.size()
			|| record.EndIndex >= slot.Timestamps.size())
		{
			continue;
		}

		UINT64 begin = 0;
		UINT64 end = 0;
		if (S_OK != m_context->GetData(slot.Timestamps[record.BeginIndex], &begin, sizeof(begin), D3D11_ASYNC_GETDATA_DONOTFLUSH)
			|| S_OK != m_context->GetData(slot.Timestamps[record.EndIndex], &end, sizeof(end), D3D11_ASYNC_GETDATA_DONOTFLUSH))
		{
			continue;
		}
		if (end < begin)
		{
			continue;
		}

		GpuTimerResult result;
		result.Key = record.Key;
		result.Milliseconds = static_cast<double>(end - begin) * 1000.0 / static_cast<double>(disjoint.Frequency);
		results.push_back(result);
	}

	m_results.swap(results);
}

#endif
