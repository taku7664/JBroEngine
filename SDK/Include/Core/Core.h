#pragma once

#include "Utillity/SafePtr.h"

class CDebug;
class CSceneManager;
class CTime;
class CInput;
class CFileSystem;
class CThreadService;
class CReflectionRegistry;
class CLogger;
class CLocalizationManager;
class INetworkManager;
class IDebugDraw2D;

struct Core
{
	inline static SafePtr<CDebug>               Debug;
	inline static SafePtr<CTime>                Time;
	inline static SafePtr<CInput>               Input;
	inline static SafePtr<CSceneManager>        SceneManager;
	inline static SafePtr<CFileSystem>          FileSystem;
	inline static SafePtr<CThreadService>       Thread;
	inline static SafePtr<CReflectionRegistry>  Reflection;
	inline static SafePtr<CLogger>              Logger;
	inline static SafePtr<CLocalizationManager> Localization;
	// Optional — null until CEngine::InitializeNetwork() is called.
	inline static SafePtr<INetworkManager>      Network;
	// 2D debug draw.  Always valid after engine initialization.
	inline static SafePtr<IDebugDraw2D>         DebugDraw2D;
};
