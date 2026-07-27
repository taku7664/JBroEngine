#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Engine/Editor/ImWindow/ImWindow.h"

#include <cstdint>

class CGameCanvas;

// GPU 프로파일러 창.
//   좌: 레이어 목록(레이어별 GPU 시간).
//   우: 선택된 레이어의 드로우순서 오브젝트.
// 리스트 나열과 상세 표시를 별도 함수로 나눠, 데이터는 그대로 두고 레이아웃만 따로
// 수정할 수 있게 한다.
class CGpuProfilerWindow final : public CImWindow
{
public:
	using CImWindow::CImWindow;
	~CGpuProfilerWindow() override = default;

private:
	void OnCreate() override;
	void OnRenderStay() override;

	// 상단: 렌더타겟 진행 프리뷰. 선택(레이어/오브젝트)까지만 그린 부분 씬 최종 화면을 표시하고,
	// 매 프레임 ImEditor 에 그 컷오프로 프리뷰 렌더를 요청한다(opt-in).
	void DrawPreview(CGameCanvas& canvas);
	// 좌측: 레이어 목록. 항목을 선택하면 m_selectedLayerKey 를 갱신한다.
	void DrawLayerList(CGameCanvas& canvas);
	// 우측: 선택된 레이어의 상세(드로우순서 오브젝트). 선택이 없으면 안내만 띄운다.
	void DrawLayerDetail(CGameCanvas& canvas);

	// 선택된 레이어 식별용 안정 포인터(CGameLayer*). 프레임 사이에 유지한다.
	const void* m_selectedLayerKey = nullptr;
	// 프리뷰 컷오프용 오브젝트 드로우순서 인덱스. UINT32_MAX = 오브젝트 미선택(레이어 전체까지).
	// 레이어 선택이 바뀌면 UINT32_MAX 로 리셋한다.
	std::uint32_t m_previewObjectIndex = 0xFFFFFFFFu;
	// 좌(레이어 목록) / 우(상세) 분할 비율 — 사이의 드래그 스플리터로 조절, 프레임 사이 유지.
	float m_splitRatio = 0.4f;
};

#endif
