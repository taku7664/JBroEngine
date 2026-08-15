#include "pch.h"
#include "EditorAssetPickDialog.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Editor/Editor.h"
#include "Editor/EditorContext.h"
#include "Editor/Localization/EditorLocalizationKeys.h"
#include "Editor/Path/EditorPathUtils.h"
#include "Engine/Core/Asset/IAssetManager.h"
#include "Engine/Core/Asset/IAssetRegistry.h"
#include "Engine/Core/Localization/LocalizationManager.h"
#include "Engine/Editor/ImEditor.h"
#include "Engine/Editor/ImGuiUtillity.h"
#include "Engine/Editor/Project/ProjectManager.h"
#include "Utillity/String/StringUtillity.h"
#include "ThirdParty/imgui/imgui.h"

#include <string>

namespace
{
	// 안내 팝업 본문. 모달이라 동시에 하나만 뜨므로 파일 스코프 하나로 충분하다.
	std::string g_pickErrorMessage;

	void OpenPickErrorPopup(const char* bodyLocKey)
	{
		if (false == Editor::ImEditor.IsValid())
		{
			return;
		}

		g_pickErrorMessage = Loc::Text(bodyLocKey);

		ImPopupDesc desc;
		desc.Title           = Loc::Text(EditorLocKeys::AssetPickErrorTitle);
		desc.Id              = "##asset_pick_error_popup";
		desc.Kind            = EImPopupKind::Modal;
		desc.InitSize        = ImVec2(420.0f, 0.0f);
		desc.ShowCloseButton = true;
		desc.OnRenderStayFunc = [](IImPopupWindow& popup)
		{
			ImGui::TextWrapped("%s", g_pickErrorMessage.c_str());
			ImGui::Spacing();
			if (ImGui::Button(Loc::Text(EditorLocKeys::CommonOk), ImVec2(120.0f, 0.0f)))
			{
				popup.Close();
			}
		};
		Editor::ImEditor->OpenPopup(desc);
	}
}

bool EditorAssetPick::PickRegisteredAsset(
	const char* titleLocKey,
	std::vector<File::FileDialogFilter> filters,
	EAssetType expectedType,
	AssetMetaData& outMetaData)
{
	SafePtr<CProjectManager> projectManager = EditorContext::GetProjectManager();
	if (false == projectManager.IsValid() || false == projectManager->IsProjectLoaded())
	{
		OpenPickErrorPopup(EditorLocKeys::AssetPickErrorNoProject);
		return false;
	}

	const File::Path assetRoot = projectManager->GetAssetPath();

	// 다이얼로그는 자산 폴더에서 시작한다 — 대부분의 선택이 그 안에서 끝난다.
	const std::wstring title       = Utillity::U8ToWString(Loc::Text(titleLocKey));
	const std::wstring initialDir  = assetRoot.wstring();

	File::Path pickedPath;
	if (false == File::ShowOpenFileDialog(
		ImGui::Utillity::GetDialogOwnerHandle(),
		title.c_str(),
		initialDir.c_str(),
		std::move(filters),
		pickedPath))
	{
		return false;   // 취소 — 조용히 끝낸다.
	}

	// 뷰어/에디터는 guid 로 돈다. 자산 폴더 밖 파일은 등록될 수 없으니 여기서 끊는다.
	File::Path relativePath;
	if (false == EditorPathUtils::TryMakeRelativeSubPath(pickedPath, assetRoot, relativePath))
	{
		OpenPickErrorPopup(EditorLocKeys::AssetPickErrorOutsideAssets);
		return false;
	}

	SafePtr<IAssetManager> assetManager = EditorContext::GetAssetManager();
	if (false == assetManager.IsValid()
		|| false == assetManager->GetRegistry().TryGetAssetByPath(relativePath, outMetaData))
	{
		OpenPickErrorPopup(EditorLocKeys::AssetPickErrorNotRegistered);
		return false;
	}

	if (expectedType != outMetaData.Type)
	{
		OpenPickErrorPopup(EditorLocKeys::AssetPickErrorTypeMismatch);
		return false;
	}

	return true;
}

#endif
