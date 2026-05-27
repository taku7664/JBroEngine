#pragma once

#include "Utillity/SafePtr.h"

class IPlatform;
class IRenderSurface;
class IRHIDevice;
class IAssetManager;
class IRenderer;
class IRenderScene;
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

struct EngineContext
{
	SafePtr<IPlatform>          Platform;
	SafePtr<IRenderSurface>     MainRenderSurface;
	SafePtr<IRHIDevice>         RHIDevice;
	SafePtr<IAssetManager>      AssetManager;
	SafePtr<IRenderer>          Renderer;
	SafePtr<IRenderScene>       RenderScene;
	SafePtr<CTime>              Time;
	SafePtr<CInput>             Input;
	SafePtr<CSceneManager>      SceneManager;
	SafePtr<CFileSystem>        FileSystem;
	SafePtr<CThreadService>     Thread;
	SafePtr<CReflectionRegistry> Reflection;
	SafePtr<CLogger>            Logger;
	SafePtr<CLocalizationManager> Localization;
	// Optional — null until CEngine::InitializeNetwork() is called.
	SafePtr<INetworkManager>    NetworkManager;
	// 2D debug draw.  Always valid after engine initialization.
	SafePtr<IDebugDraw2D>       DebugDraw2D;

	bool IsApplicationFocused = true;
	bool ApplicationFocusGained = false;
	bool ApplicationFocusLost = false;
};
