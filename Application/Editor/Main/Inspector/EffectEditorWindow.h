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
//  창을 닫으면(X) CImWindow::SetVisible(false) 로 숨긴다.
//  같은 에셋을 다시 열면 기존 dock/panel 을 다시 표시하고 Focus 한다.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

// 실제 효과 에디터 UI 를 담는 내부 패널 (도킹 자식).
class CEffectEditorPanel final : public CImCustomWindow
{
public:
	using CImCustomWindow::CImCustomWindow;

	void SetTargetGuid(const AssetGuid& guid) { m_widget.SetTargetGuid(guid); }

	// 창 메뉴(저장·되돌리기)가 위젯 상태를 직접 다룬다.
	CEffectEditorWidget& GetWidget() { return m_widget; }

private:
	void OnRenderStay() override;

	CEffectEditorWidget m_widget;
};

// .jfx 더블클릭 시 띄우는 도킹 컨테이너. Root 의 자식.
class CEffectEditorDockWindow final : public CImDockWindow
{
public:
	using CImDockWindow::CImDockWindow;

private:
	void OnCreate() override;
	void OnMenuBar() override;

	// 이 dock 은 파일당 하나라 패널도 하나뿐이다 — 스프라이트 뷰어처럼 활성 탭을
	// 따질 필요가 없다.
	SafePtr<CEffectEditorPanel> GetPanel();
};

#include <string>

namespace EffectEditorWindow
{
	// 효과 에셋(guid) 전용 에디터 독윈도우를 연다. 이미 열려 있으면 Focus 만.
	// 윈도우 draw/순회 도중에도 안전하도록 생성을 ImEditor::QueueDeferred 로 지연한다.
	// AssetBrowser 더블클릭과 인스펙터 "에디터 열기" 버튼이 공용으로 사용.
	void Open(const AssetGuid& guid, const std::string& title);
}
