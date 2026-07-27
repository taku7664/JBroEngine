#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Engine/Editor/ImWindow/ImWindow.h"

class CGameCanvas;

// CPU 프로파일러 창 (구 EditorStatistics 흡수).
//   상단: 일반 통계(FPS/프레임/오브젝트·풀 카운트/선택/Undo·Redo·Dirty) + 엔진 프레임 구간.
//   좌:   컴포넌트 풀(시스템) 당 순회시간 목록.
//   우:   선택 풀 상세 — Script 풀은 오브젝트(인스턴스)별 Update 시간(선택 시에만 계측),
//         그 외 풀은 총 순회시간(Update/FixedUpdate).
// 통계/리스트/상세를 함수로 나눠 데이터는 그대로 두고 레이아웃만 따로 손볼 수 있게 한다.
class CCpuProfilerWindow final : public CImWindow
{
public:
	using CImWindow::CImWindow;
	~CCpuProfilerWindow() override = default;

private:
	void OnCreate() override;
	void OnRenderStay() override;

	// 상단: 일반 통계 + 엔진 프레임 구간(흡수).
	void DrawGeneralStats(CGameCanvas* canvas);
	// 좌측: 컴포넌트 풀(캔버스 시스템) 순회시간. 항목 선택 시 m_selectedPoolLabel 갱신.
	void DrawPoolList(CGameCanvas* canvas);
	// 우측: 선택 풀 상세. Script 풀이면 오브젝트별, 그 외면 풀 총합.
	void DrawPoolDetail(CGameCanvas* canvas);

	// 선택된 풀 식별 — 프레임 구간 라벨 포인터. 정적 문자열(리터럴/typeid)이라 프레임 사이 안정.
	const char* m_selectedPoolLabel = nullptr;
	// 좌(풀 목록) / 우(상세) 분할 비율 — 사이의 드래그 스플리터로 조절, 프레임 사이 유지.
	float m_splitRatio = 0.45f;
};

#endif
