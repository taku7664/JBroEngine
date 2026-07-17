#include "pch.h"
#include "EditorSimulationGuard.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Engine/Core/EngineCore.h"
#include "Engine/GameFramework/Canvas/CanvasManager.h"

bool EditorSimulationGuard::IsSimulationActive()
{
	return Engine.CanvasManager.IsValid()
		&& (Engine.CanvasManager->IsSimulationPlaying() || Engine.CanvasManager->IsSimulationPaused());
}

bool EditorSimulationGuard::CanSaveProject()
{
	return false == IsSimulationActive();
}

bool EditorSimulationGuard::CanBuildProject()
{
	return false == IsSimulationActive();
}

const char* EditorSimulationGuard::GetSaveBlockedMessage()
{
	return Loc::Text(EditorLocKeys::EditorSimulationSaveBlocked);
}

const char* EditorSimulationGuard::GetBuildBlockedMessage()
{
	return Loc::Text(EditorLocKeys::EditorSimulationBuildBlocked);
}

#endif
