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

	bool IsApplicationFocused = true;
	bool ApplicationFocusGained = false;
	bool ApplicationFocusLost = false;

	// 프로젝트 Default PPU 의 런타임 캐시. 자산별 PPU 가 0(미지정) 일 때 폴백으로 사용.
	// 에디터는 LoadProject / ProjectSettings Apply 시 갱신, 빌드된 게임은 부팅 시 한 번 주입.
	float PixelsPerUnit = 100.0f;
};

extern EngineCore Engine;
