#pragma once

#include "Utillity/Pointer/SafePtr.h"

class CDebug;
class CSceneManager;
class CTime;
class CInput;
class CFileSystem;
class IAssetManager;
class CTaskManager;
class CRandomService;
class CMathService;
class CReflectionRegistry;
class CLogger;
class CLocalizationManager;
class CResourceRegistry;
class INetworkManager;
class IDebugDraw2D;

struct Core
{
	inline static SafePtr<CDebug>               Debug;
	inline static SafePtr<CTime>                Time;
	inline static SafePtr<CInput>               Input;
	inline static SafePtr<CSceneManager>        SceneManager;
	inline static SafePtr<CFileSystem>          FileSystem;
	inline static SafePtr<IAssetManager>        AssetManager;
	inline static SafePtr<CTaskManager>         TaskManager;
	inline static SafePtr<CRandomService>       Random;
	inline static SafePtr<CMathService>         Math;
	inline static SafePtr<CReflectionRegistry>  Reflection;
	inline static SafePtr<CLogger>              Logger;
	inline static SafePtr<CLocalizationManager> Localization;
	// Resources/resources.yaml 기반 영구 리소스 매핑. Engine 초기화 시 생성.
	inline static SafePtr<CResourceRegistry>    ResourceRegistry;
	// Optional — null until CEngine::InitializeNetwork() is called.
	inline static SafePtr<INetworkManager>      Network;
	// 2D debug draw.  Always valid after engine initialization.
	inline static SafePtr<IDebugDraw2D>         DebugDraw2D;
};
