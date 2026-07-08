#include "pch.h"
#include "EditorSessionPersistence.h"

#include "Editor/Editor.h"
#include "Editor/EditorContext.h"
#include "Editor/Main/SceneView/SceneViewTool.h"
#include "Editor/Path/EditorPathUtils.h"
#include "Engine/Core/EngineCore.h"
#include "Engine/Editor/Project/ProjectManager.h"
#include "Engine/GameFramework/Scene/SceneSerializer.h"
#include "ThirdParty/imgui/imgui.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

namespace
{
	bool SaveActiveScene(std::string& outError)
	{
		SafePtr<CGameScene> scene = EditorContext::GetActiveScene();
		const File::Path& scenePath = Editor::GetActiveScenePath();
		if (false == scene.IsValid() || scenePath.empty())
		{
			return true;
		}

		CSceneSerializer serializer;
		if (ESceneSerializeResult::Success != serializer.SaveToFile(*scene, scenePath))
		{
			outError = "Failed to save the active scene.";
			return false;
		}

		Editor::CommandManager.MarkSaved();
		return true;
	}

	void CaptureProjectState(CProjectManager& projectManager)
	{
		if (Editor::SceneView)
		{
			const Vector2 cameraPosition = Editor::SceneView->GetEditorCameraPos();
			projectManager.SetSceneViewCamera(
				cameraPosition.x,
				cameraPosition.y,
				Editor::SceneView->GetEditorCameraSize());
		}

		const File::Path& activeScenePath = Editor::GetActiveScenePath();
		if (false == activeScenePath.empty() && false == projectManager.GetAssetPath().empty())
		{
			File::Path relativePath;
			if (EditorPathUtils::TryMakeRelativeSubPath(
				activeScenePath,
				projectManager.GetAssetPath(),
				relativePath))
			{
				projectManager.SetLastOpenedScenePath(relativePath.generic_string());
			}
		}

		if (Engine.Localization.IsValid())
		{
			projectManager.SetEditorLocaleCode(Engine.Localization->GetCurrentLocale());
		}

		std::size_t iniSize = 0;
		const char* ini = ImGui::SaveIniSettingsToMemory(&iniSize);
		projectManager.SetImGuiIniSettings(
			(nullptr != ini && iniSize > 0) ? std::string(ini, iniSize) : std::string());
	}
}

bool EditorSessionPersistence::Save(std::string& outError)
{
	outError.clear();
	if (false == EditorSimulationGuard::CanSaveProject())
	{
		outError = EditorSimulationGuard::GetSaveBlockedMessage();
		return false;
	}

	if (false == SaveActiveScene(outError))
	{
		return false;
	}

	SafePtr<CProjectManager> projectManager = EditorContext::GetProjectManager();
	if (false == projectManager.IsValid() || false == projectManager->IsProjectLoaded())
	{
		outError = "Project is not loaded.";
		return false;
	}

	CaptureProjectState(*projectManager);
	return projectManager->SaveProject(&outError);
}

#endif
