#pragma once

#include "Engine/Editor/ImWindow/ImDockWindow.h"
#include "Engine/Editor/ImWindow/ImCustomWindow.h"
#include "Engine/Core/Asset/AssetTypes.h"   // AssetGuid
#include "EffectEditorWidget.h"

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  사운드 효과(.jfx) 전용 에디터 독윈도우
//
//  AssetBrowser 에서 .jfx 더블클릭 시 생성:
//    Root → CEffectEditorDockWindow → CEffectEditorPanel(child, 위젯 보유)
//
//  창을 닫으면(X) CImWindow::m_bIsAlive 가 false 가 되어 ImEditor 가 자동 정리한다.
//  Dock 부모가 닫히면 소멸자가 child 도 Destroy 한다.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

// 실제 효과 에디터 UI 를 담는 내부 패널 (도킹 자식).
class CEffectEditorPanel final : public CImCustomWindow
{
public:
	using CImCustomWindow::CImCustomWindow;

	void SetTargetGuid(const AssetGuid& guid) { m_widget.SetTargetGuid(guid); }

private:
	void OnRenderStay() override;

	CEffectEditorWidget m_widget;
};

// .jfx 더블클릭 시 띄우는 도킹 컨테이너. Root 의 자식.
class CEffectEditorDockWindow final : public CImDockWindow
{
public:
	using CImDockWindow::CImDockWindow;
};

#include <string>

namespace EffectEditorWindow
{
	// 효과 에셋(guid) 전용 에디터 독윈도우를 연다. 이미 열려 있으면 Focus 만.
	// 윈도우 draw/순회 도중에도 안전하도록 생성을 ImEditor::QueueDeferred 로 지연한다.
	// AssetBrowser 더블클릭과 인스펙터 "에디터 열기" 버튼이 공용으로 사용.
	void Open(const AssetGuid& guid, const std::string& title);
}
