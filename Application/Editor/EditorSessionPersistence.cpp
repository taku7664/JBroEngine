#include "pch.h"
#include "EditorSessionPersistence.h"

#include "Editor/Editor.h"
#include "Editor/EditorContext.h"
#include "Editor/Main/CanvasView/CanvasViewTool.h"
#include "Editor/Path/EditorPathUtils.h"
#include "Engine/Core/EngineCore.h"
#include "Engine/Editor/Project/ProjectManager.h"
#include "Engine/GameFramework/Canvas/CanvasSerializer.h"
#include "ThirdParty/imgui/imgui.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

namespace
{
	bool SaveActiveCanvas(std::string& outError)
	{
		SafePtr<CGameCanvas> canvas = EditorContext::GetActiveCanvas();
		const File::Path& canvasPath = Editor::GetActiveCanvasPath();
		if (false == canvas.IsValid() || canvasPath.empty())
		{
			return true;
		}

		CCanvasSerializer serializer;
		if (ECanvasSerializeResult::Success != serializer.SaveToFile(*canvas, canvasPath))
		{
			outError = "Failed to save the active canvas.";
			return false;
		}

		Editor::CommandManager.MarkSaved();
		return true;
	}

	void CaptureProjectState(CProjectManager& projectManager)
	{
		if (Editor::CanvasView)
		{
			const Vector2 cameraPosition = Editor::CanvasView->GetEditorCameraPos();
			projectManager.SetCanvasViewCamera(
				cameraPosition.x,
				cameraPosition.y,
				Editor::CanvasView->GetEditorCameraSize());
		}

		const File::Path& activeCanvasPath = Editor::GetActiveCanvasPath();
		if (false == activeCanvasPath.empty() && false == projectManager.GetAssetPath().empty())
		{
			File::Path relativePath;
			if (EditorPathUtils::TryMakeRelativeSubPath(
				activeCanvasPath,
				projectManager.GetAssetPath(),
				relativePath))
			{
				projectManager.SetLastOpenedCanvasPath(relativePath.generic_string());
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

	if (false == SaveActiveCanvas(outError))
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
