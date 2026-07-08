#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

namespace EditorSimulationGuard
{
	bool IsSimulationActive();
	bool CanSaveProject();
	bool CanBuildProject();
	const char* GetSaveBlockedMessage();
	const char* GetBuildBlockedMessage();
}

#endif
