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

struct EngineContext
{
	SafePtr<IPlatform> Platform;
	SafePtr<IRenderSurface> MainRenderSurface;
	SafePtr<IRHIDevice> RHIDevice;
	SafePtr<IAssetManager> AssetManager;
	SafePtr<IRenderer> Renderer;
	SafePtr<IRenderScene> RenderScene;
	SafePtr<CTime> Time;
	SafePtr<CInput> Input;
	SafePtr<CSceneManager> SceneManager;
	SafePtr<CFileSystem> FileSystem;
	SafePtr<CThreadService> Thread;
	SafePtr<CReflectionRegistry> Reflection;
};
