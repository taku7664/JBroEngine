#include "pch.h"
#include "EffectEditorWindow.h"

#include "Editor/Editor.h"
#include "Editor/Localization/EditorLocalizationKeys.h"
#include "Editor/Main/EditorAssetPickDialog.h"
#include "Engine/Core/Localization/LocalizationManager.h"
#include "Engine/Editor/ImEditor.h"
#include "Engine/Editor/ImGuiUtillity.h"
#include "Utillity/String/StringUtillity.h"
#include "ThirdParty/imgui/imgui.h"

void EffectEditorWindow::Open(const AssetGuid& guid, const std::string& title)
{
	if (false == Editor::ImEditor.IsValid() || false == Editor::RootDockWindow.IsValid())
	{
		return;
	}
	if (guid.IsNull())
	{
		return;
	}

	// 더블클릭/버튼 클릭은 윈도우 draw/순회 도중 발생. 여기서 바로 CreateImWindow 하면
	// m_imWindowVector 가 재할당되어 순회 중 반복자가 무효화돼 크래시한다.
	// → 생성을 다음 Update 끝(순회 이후)으로 지연. guid/title 은 by-value 캡처.
	Editor::ImEditor->QueueDeferred([guid, title]()
	{
		if (false == Editor::ImEditor.IsValid() || false == Editor::RootDockWindow.IsValid())
		{
			return;
		}

		const std::string guidStr  = guid.generic_string();
		const std::string dockKey  = std::string("EffectDock_")  + guidStr;
		const std::string panelKey = std::string("EffectPanel_") + guidStr;

		const ImGuiID dockId  = ImHashStr(dockKey.c_str());
		const ImGuiID panelId = ImHashStr(panelKey.c_str());

		// 이미 열려 있으면 새로 만들지 않고 Focus.
		if (SafePtr<CEffectEditorPanel> existing = DynamicSafePtrCast<CEffectEditorPanel>(Editor::ImEditor->FindImWindow(panelId)))
		{
			if (SafePtr<CImWindow> existingDock = DynamicSafePtrCast<CImWindow>(Editor::ImEditor->FindImWindow(dockId)))
			{
				existingDock->SetVisible(true);
			}
			existing->SetVisible(true);
			existing->Focus();
			return;
		}

		// 도킹 컨테이너(Root 자식) 생성.
		SafePtr<CEffectEditorDockWindow> dock =
			Editor::ImEditor->CreateImWindow<CEffectEditorDockWindow>(dockKey.c_str(), Editor::RootDockWindow->GetID());
		if (false == dock.IsValid())
		{
			if (SafePtr<CImWindow> d = DynamicSafePtrCast<CImWindow>(Editor::ImEditor->FindImWindow(dockId)))
			{
				d->Focus();
			}
			return;
		}
		// Dock 제목은 파일명이 아니라 "사운드 효과" 로 고정 (언어 전환도 반영).
		dock->SetLocalizedTitleKey(EditorLocKeys::InspectorEffectTitle);
		dock->SetSize(ImVec2(420.0f, 360.0f));

		// 내부 에디터 패널(dock 자식) 생성.
		SafePtr<CEffectEditorPanel> panel =
			Editor::ImEditor->CreateImWindow<CEffectEditorPanel>(panelKey.c_str(), dockId);
		if (panel.IsValid())
		{
			panel->SetTitle(title.c_str());
			panel->SetTargetGuid(guid);
			panel->Focus();
		}
	});
}

void CEffectEditorPanel::OnRenderStay()
{
	m_widget.Draw();
}

void CEffectEditorDockWindow::OnCreate()
{
	m_imguiFlags = ImGuiWindowFlags_MenuBar;
}

SafePtr<CEffectEditorPanel> CEffectEditorDockWindow::GetPanel()
{
	for (SafePtr<CImWindow>& child : m_childImWindowVector)
	{
		if (SafePtr<CEffectEditorPanel> panel = DynamicSafePtrCast<CEffectEditorPanel>(child))
		{
			return panel;
		}
	}
	return nullptr;
}

void CEffectEditorDockWindow::OnMenuBar()
{
	SafePtr<CEffectEditorPanel> panel = GetPanel();

	if (false == ImGui::BeginMenu(Loc::Text(EditorLocKeys::MenuFile)))
	{
		return;
	}

	if (ImGui::MenuItem(Loc::Text(EditorLocKeys::EffectEditorMenuOpen)))
	{
		// 필터 문자열은 raw 포인터로 넘어간다 — 다이얼로그가 닫힐 때까지 붙잡아 둔다.
		const std::wstring effectFilter = Utillity::U8ToWString(Loc::Text(EditorLocKeys::FileDialogFilterAudioEffect));

		AssetMetaData metaData;
		if (EditorAssetPick::PickRegisteredAsset(
			EditorLocKeys::FileDialogOpenEffectTitle,
			{ { effectFilter.c_str(), L"*.jfx" }, { L"All Files", L"*.*" } },
			EAssetType::AudioEffect,
			metaData))
		{
			// 효과 에디터는 **파일당 dock 하나**다 — 여는 순간 새 창이 뜨고 이 창은 그대로 남는다.
			EffectEditorWindow::Open(metaData.Guid, metaData.Path.filename().string());
		}
	}

	ImGui::Separator();

	const bool dirty = panel.IsValid() && panel->GetWidget().IsDirty();
	{
		ImGui::Utillity::DisableScope disable(false == dirty);
		if (ImGui::MenuItem(Loc::Text(EditorLocKeys::CommonSave)))
		{
			panel->GetWidget().Save();
		}
	}
	{
		ImGui::Utillity::DisableScope disable(false == panel.IsValid());
		if (ImGui::MenuItem(Loc::Text(EditorLocKeys::EffectEditorMenuReload)))
		{
			panel->GetWidget().Reload();
		}
	}

	ImGui::Separator();

	if (ImGui::MenuItem(Loc::Text(EditorLocKeys::CommonClose)))
	{
		SetVisible(false);
	}

	ImGui::EndMenu();
}
