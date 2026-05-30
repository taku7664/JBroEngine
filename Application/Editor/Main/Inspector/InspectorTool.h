#pragma once

#include "Engine/Editor/ImWindow/ImWindow.h"

class CInspectorTool : public CImWindow
{
public:
	using CImWindow::CImWindow;
	virtual ~CInspectorTool() = default;

	// 우측 패널에 현재 표시 중인 컴포넌트의 C++ 타입 이름을 반환.
	// 엔티티 미선택·빈 목록 등 표시 중인 컴포넌트가 없으면 nullptr.
	// 반환 포인터는 리플렉션 레지스트리의 정적 문자열이므로 수명이 보장됨.
	const char* GetActiveComponentTypeName() const { return m_activeComponentTypeName; }

private:
	void OnCreate() override;
	void OnDestroy() override;
	void OnUpdate() override;
	void OnRenderStay() override;

private:
	// 좌측 리스트에서 선택된 항목 인덱스 (0 = "정보" 그룹, 1+ = 개별 컴포넌트)
	int   m_selectedTabIndex = 0;
	// 좌측 리스트 너비 비율 (0~1), 초기값 0.4 (4:6)
	float m_splitRatio       = 0.4f;
	// 우측 패널에 현재 표시 중인 컴포넌트 타입 이름 (매 프레임 OnRenderStay에서 갱신)
	const char* m_activeComponentTypeName = nullptr;
};

