#pragma once

#include "Utillity/Pointer/SafePtr.h"

class CDebug;
class IPlatform;
class IRenderSurface;
class IRHIDevice;
class IAssetManager;
class IAudioDevice;
class IRenderer;
class IRenderScene;
class CFileSystem;
class CInput;
class CInputSystem;
class CLocalizationManager;
class CLogger;
class CReflectionRegistry;
class CResourceRegistry;
class CSceneManager;
class CTaskManager;
class CRandomService;
class CMathService;
class CTime;
class IDebugDraw2D;
class INetworkManager;
class IRenderResourceCache;


struct EngineCore
{
	SafePtr<IPlatform>            Platform;
	SafePtr<IRenderSurface>       MainRenderSurface;
	SafePtr<IRHIDevice>           RHIDevice;
	SafePtr<IAssetManager>        AssetManager;
	SafePtr<IAudioDevice>         Audio;
	SafePtr<IRenderer>            Renderer;
	SafePtr<IRenderScene>         RenderScene;
	SafePtr<IRenderResourceCache> RenderResourceCache;
	SafePtr<CDebug>               Debug;
	SafePtr<CTime>                Time;
	SafePtr<CInput>               Input;
	SafePtr<CInputSystem>         InputSystem;  // 엔진 내부 입력 관리자(스크립트 비공개)
	SafePtr<CSceneManager>        SceneManager;
	SafePtr<CFileSystem>          FileSystem;
	SafePtr<CTaskManager>         TaskManager;
	SafePtr<CRandomService>       Random;
	SafePtr<CMathService>         Math;
	SafePtr<CReflectionRegistry>  Reflection;
	SafePtr<CLogger>              Logger;
	SafePtr<CLocalizationManager> Localization;
	SafePtr<CResourceRegistry>    ResourceRegistry;
	SafePtr<INetworkManager>      Network;
	SafePtr<IDebugDraw2D>         DebugDraw2D;
};

extern EngineCore Engine;
