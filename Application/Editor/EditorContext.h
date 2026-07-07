#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Utillity/Pointer/SafePtr.h"

class CGameScene;
class CProjectManager;
class IAssetManager;

namespace EditorContext
{
	SafePtr<CProjectManager> GetProjectManager();
	SafePtr<IAssetManager> GetAssetManager();
	SafePtr<CGameScene> GetActiveScene();
	CGameScene* TryGetActiveScene();
}

#endif
