#include "pch.h"
#include "EditorContext.h"

#include "Editor/Editor.h"
#include "Engine/Core/EngineCore.h"
#include "Engine/Editor/ImEditor.h"
#include "Engine/Editor/Project/ProjectManager.h"
#include "Engine/Core/Asset/IAssetManager.h"
#include "Engine/GameFramework/Canvas/CanvasManager.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

SafePtr<CProjectManager> EditorContext::GetProjectManager()
{
	return Editor::ImEditor ? Editor::ImEditor->GetProjectManager() : SafePtr<CProjectManager>();
}

SafePtr<CGameCanvas> EditorContext::GetActiveCanvas()
{
	return Engine.CanvasManager.IsValid()
		? Engine.CanvasManager->GetActiveCanvas()
		: SafePtr<CGameCanvas>();
}

SafePtr<IAssetManager> EditorContext::GetAssetManager()
{
	SafePtr<CProjectManager> projectManager = GetProjectManager();
	return projectManager.IsValid()
		? projectManager->GetAssetManager()
		: SafePtr<IAssetManager>();
}

CGameCanvas* EditorContext::TryGetActiveCanvas()
{
	return GetActiveCanvas().TryGet();
}

#endif
