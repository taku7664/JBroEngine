#pragma once

#include <cstdint>
#include <vector>

#include "Core/RHI/IRHIGpuTimer.h"

// 어떤 배치에도 속하지 않는 드로우순서 항목(컬링되어 그려지지 않음)의 그룹 값.
inline constexpr std::uint32_t INVALID_DRAW_GROUP = 0xFFFFFFFFu;

// 한 레이어의 드로우순서 오브젝트 하나. 키는 그 프레임 안에서만 유효한 오브젝트 주소(CGameObject*).
struct GpuDrawOrderItem
{
	const void*   Object = nullptr;
	bool          Culled = false;   // 프러스텀 컬링으로 그려지지 않았는가(순서 자체는 유지).
	// 같은 값 = 같은 인스턴싱 드로우콜(하나의 배치). 그룹 크기≥2 면 여러 오브젝트가 한 번에 그려진 것.
	// 컬링 항목은 INVALID_DRAW_GROUP(어떤 드로우콜에도 안 들어감).
	std::uint32_t Group  = INVALID_DRAW_GROUP;
};

// 한 레이어의 드로우순서 목록. 게임뷰 렌더 때 채운다.
struct GpuLayerDrawOrder
{
	std::uint16_t                 LayerIndex = 0;
	std::vector<GpuDrawOrderItem> Items;
};

// GPU 프레임 프로파일러 (에디터 진단용).
//
// 레이어/패스 단위 GPU 시간을 GPU 타임스탬프로 수집한다. CPU 쪽 CFrameSectionProfiler 와 달리
// GPU 타임스탬프는 비동기라, 커맨드 스트림에 꽂은 뒤 결과가 몇 프레임 지연되어 읽힌다
// (프레임인플라이트 링버퍼는 이후 단계에서 구현). 계측 자체가 에디터 전용이므로(JBRO_EDITOR)
// 게임 빌드에서는 켜질 일이 없다.
//
// 지금 단계는 사용 토글과 백엔드 지원 여부만 든다. 실제 타임스탬프 수집·결과 모델은 이후
// 단계(RHI 배관 → GameCamera 계측 → 드로우순서 수집)에서 채운다. 이 오브젝트가 플래그의
// 최종 소유처라, UI 체크박스와 GameCamera 계측이 같은 상태를 참조한다(임시 플래그 회피).
class CGpuProfiler
{
public:
	void SetEnabled(bool enabled) { m_enabled = enabled; }
	bool IsEnabled() const { return m_enabled; }

	// RHI 백엔드가 GPU 타임스탬프 쿼리를 지원하는지. 일부 웹 환경 등은 불가할 수 있어,
	// 이후 RHI 초기화가 실제 지원 여부로 설정한다. 미지원이면 UI 가 안내만 띄운다.
	void SetBackendSupported(bool supported) { m_backendSupported = supported; }
	bool IsBackendSupported() const { return m_backendSupported; }

	// RHI 타이머가 해소한 구간별 결과를 매 프레임 여기로 복사한다(UI 는 이 값을 읽어 RHI 와
	// 직접 결합하지 않는다). 키는 호출자가 준 불투명 포인터(현재는 프레임 전체=nullptr).
	void SetResults(const std::vector<GpuTimerResult>& results) { m_results = results; }
	const std::vector<GpuTimerResult>& GetResults() const { return m_results; }

	// 특정 키의 GPU 시간 합(ms). 프레임 전체는 key=nullptr, 레이어는 GpuLayerKey(index).
	// 같은 키가 여러 번(멀티 뷰포트) 나오면 합산한다.
	double GetMillisecondsForKey(const void* key) const
	{
		double total = 0.0;
		for (const GpuTimerResult& result : m_results)
		{
			if (result.Key == key)
			{
				total += result.Milliseconds;
			}
		}
		return total;
	}

	// ── 레이어별 드로우순서 오브젝트(진단) ──────────────────────────────────────
	// 게임뷰 렌더가 씬이 살아있는 동안 레이어마다 채운다. 키는 그 프레임 안에서만 유효한
	// 불투명 포인터(CGameObject*) — UI 가 같은 프레임에 이름으로 역해석한다.
	// 버퍼는 프레임 간 재사용한다(논리적 개수만 리셋 → 핫패스 힙 할당 없음).
	void ClearDrawOrder() { m_drawOrderCount = 0; }

	// 레이어 하나의 드로우순서 캡처를 시작한다. 항목은 RecordDrawOrderItem 으로 순서대로 넣는다.
	void BeginLayerDrawOrder(std::uint16_t layerIndex)
	{
		if (m_drawOrderCount >= m_drawOrder.size())
		{
			m_drawOrder.emplace_back();
		}
		m_activeDrawOrder = m_drawOrderCount++;
		GpuLayerDrawOrder& entry = m_drawOrder[m_activeDrawOrder];
		entry.LayerIndex = layerIndex;
		entry.Items.clear();
	}

	void RecordDrawOrderItem(const void* object, bool culled, std::uint32_t group)
	{
		if (m_activeDrawOrder >= m_drawOrder.size())
		{
			return;
		}
		m_drawOrder[m_activeDrawOrder].Items.push_back(GpuDrawOrderItem{ object, culled, group });
	}

	const std::vector<GpuDrawOrderItem>* GetLayerDrawOrder(std::uint16_t layerIndex) const
	{
		for (std::size_t i = 0; i < m_drawOrderCount; ++i)
		{
			if (m_drawOrder[i].LayerIndex == layerIndex)
			{
				return &m_drawOrder[i].Items;
			}
		}
		return nullptr;
	}

private:
	bool m_enabled = false;
	bool m_backendSupported = false;
	std::vector<GpuTimerResult> m_results;
	std::vector<GpuLayerDrawOrder> m_drawOrder;
	std::size_t m_drawOrderCount = 0;
	std::size_t m_activeDrawOrder = 0;
};
