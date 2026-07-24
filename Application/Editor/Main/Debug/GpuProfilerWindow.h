#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Engine/Editor/ImWindow/ImWindow.h"

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

	// 좌측: 레이어 목록. 항목을 선택하면 m_selectedLayerKey 를 갱신한다.
	void DrawLayerList(CGameCanvas& canvas);
	// 우측: 선택된 레이어의 상세(드로우순서 오브젝트). 선택이 없으면 안내만 띄운다.
	void DrawLayerDetail(CGameCanvas& canvas);

	// 선택된 레이어 식별용 안정 포인터(CGameLayer*). 프레임 사이에 유지한다.
	const void* m_selectedLayerKey = nullptr;
};

#endif
