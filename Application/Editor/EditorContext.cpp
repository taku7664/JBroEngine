#include "pch.h"
#include "EditorContext.h"

#include "Editor/Editor.h"
#include "Engine/Core/EngineCore.h"
#include "Engine/Editor/ImEditor.h"
#include "Engine/GameFramework/Scene/SceneManager.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

SafePtr<CProjectManager> EditorContext::GetProjectManager()
{
	return Editor::ImEditor ? Editor::ImEditor->GetProjectManager() : SafePtr<CProjectManager>();
}

SafePtr<CGameScene> EditorContext::GetActiveScene()
{
	return Engine.SceneManager.IsValid()
		? Engine.SceneManager->GetActiveScene()
		: SafePtr<CGameScene>();
}

CGameScene* EditorContext::TryGetActiveScene()
{
	return GetActiveScene().TryGet();
}

#endif
