#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Utillity/Pointer/SafePtr.h"

class CGameScene;
class CProjectManager;

namespace EditorContext
{
	SafePtr<CProjectManager> GetProjectManager();
	SafePtr<CGameScene> GetActiveScene();
	CGameScene* TryGetActiveScene();
}

#endif
