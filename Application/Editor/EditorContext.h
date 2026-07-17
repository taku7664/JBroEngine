#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Utillity/Pointer/SafePtr.h"

class CGameCanvas;
class CProjectManager;
class IAssetManager;

namespace EditorContext
{
	SafePtr<CProjectManager> GetProjectManager();
	SafePtr<IAssetManager> GetAssetManager();
	SafePtr<CGameCanvas> GetActiveCanvas();
	CGameCanvas* TryGetActiveScene();
}

#endif
