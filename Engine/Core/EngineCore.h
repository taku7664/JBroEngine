#pragma once

#include "Utillity/SafePtr.h"

class CDebug;
class CFileSystem;
class CInput;
class CLocalizationManager;
class CLogger;
class CReflectionRegistry;
class CSceneManager;
class CThreadService;
class CTime;
class IDebugDraw2D;
class INetworkManager;

// 스크립트 공개용
struct EngineCore
{
	SafePtr<CDebug>               Debug;
	SafePtr<CTime>                Time;
	SafePtr<CInput>               Input;
	SafePtr<CSceneManager>        SceneManager;
	SafePtr<CFileSystem>          FileSystem;
	SafePtr<CThreadService>       Thread;
	SafePtr<CReflectionRegistry>  Reflection;
	SafePtr<CLogger>              Logger;
	SafePtr<CLocalizationManager> Localization;
	SafePtr<INetworkManager>      Network;
	SafePtr<IDebugDraw2D>         DebugDraw2D;
};

inline EngineCore Engine;

void BindEngineCore(const EngineCore* hostEngine);
void UnbindEngineCore();
