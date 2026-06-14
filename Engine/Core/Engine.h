#pragma once

#include "Core/ScriptCore.h"
#include "Core/Platform/PlatformTypes.h"
#include "Core/RHI/RHITypes.h"
#include "GameFramework/Rendering/GameCamera.h"

#include <vector>

class CModule;
class IPlatform;
class IRenderSurface;
class IRHIDevice;
class IAssetManager;
class IAudioDevice;
class IRenderer;
class IRenderScene;
class IRenderResourceCache;
class CSceneManager;
class CNetworkManager;
class CDebugDraw2D;
class CDebug;
class CTime;
class CInput;
class CInputSystem;
class CFileSystem;
class CTaskManager;
class CRandomService;
class CMathService;
class CReflectionRegistry;
class CLogger;
class CLocalizationManager;
class CResourceRegistry;

class CEngine final : public EnableSafeFromThis<CEngine>
{
public:
	CEngine();
	~CEngine();
	CEngine(const CEngine&) = delete;
	CEngine& operator=(const CEngine&) = delete;
	CEngine(CEngine&&) = delete;
	CEngine& operator=(CEngine&&) = delete;

public:
	bool Initialize();
	bool Update();
	void Finalize();
	void SetPlatformDesc(const PlatformDesc& desc);
	void SetMainClearColor(const Color& color);
	void SetGameRenderCameras(std::vector<GameRenderCameraDesc> cameras);

	void InitializeModule(CModule& module, const char* moduleName);
	void FinalizeModule(CModule& module);

	// Optional subsystem — call after Initialize() to enable networking.
	// Safe to call multiple times; subsequent calls are no-ops.
	bool InitializeNetwork();

	const ScriptCore&       GetScriptCore()        const;
	SafePtr<IPlatform>      GetPlatform()          const;
	SafePtr<IRenderSurface> GetMainRenderSurface() const;
	RenderSurfaceSize       GetRenderTargetSize()  const;
	SafePtr<IRHIDevice>     GetRHIDevice()         const;
	SafePtr<IAssetManager>  GetAssetManager()      const;
	SafePtr<IRenderer>      GetRenderer()          const;
	SafePtr<IRenderScene>   GetRenderScene()       const;
	SafePtr<IAudioDevice>   GetAudioDevice()       const;

private:
	bool InitializePlatform();
	bool InitializeRHI();
	bool InitializeAssetManager();
	bool InitializeRenderer();
	bool InitializeAudio();
	bool InitializeCoreServices();
	void BeginFrame();
	void UpdateModules();
	void UpdateCoreServices();
	void PrepareRenderModules();
	void RenderFrame();
	void EndFrame();
	void FillRenderSurfaceDesc(RHIDesc& desc) const;
	void SyncScriptCore();

	// 메인 surface 윈도우 이벤트(포커스/리사이즈) → 활성 씬 스크립트로 전달.
	void OnSurfaceEvent(const SurfaceEvent& surfaceEvent);

private:
	OwnerPtr<IPlatform>           m_platform;
	OwnerPtr<IRHIDevice>          m_rhiDevice;
	OwnerPtr<IAssetManager>       m_assetManager;
	OwnerPtr<IRenderer>           m_renderer;
	OwnerPtr<IRenderScene>        m_renderScene;
	OwnerPtr<IRenderResourceCache> m_renderResourceCache;
	OwnerPtr<IAudioDevice>        m_audioDevice;
	OwnerPtr<CTime>               m_time;
	OwnerPtr<CInput>              m_input;
	OwnerPtr<CInputSystem>        m_inputSystem;
	OwnerPtr<CFileSystem>         m_fileSystem;
	OwnerPtr<CTaskManager>        m_taskManager;
	OwnerPtr<CRandomService>      m_randomService;
	OwnerPtr<CMathService>        m_mathService;
	OwnerPtr<CReflectionRegistry> m_reflectionRegistry;
	OwnerPtr<CLogger>             m_logger;
	OwnerPtr<CDebug>              m_debug;
#if JBRO_EDITOR
	OwnerPtr<CLocalizationManager> m_localization;
#endif
	OwnerPtr<CResourceRegistry>   m_resourceRegistry;
	OwnerPtr<CSceneManager>       m_sceneManager;
	OwnerPtr<CNetworkManager>     m_networkManager;   // null until InitializeNetwork()
	OwnerPtr<CDebugDraw2D>        m_debugDraw;
	std::vector<CModule*>         m_modules;
	std::vector<GameRenderCameraDesc> m_gameRenderCameras;
	PlatformDesc                  m_platformDesc;
	Color                         m_mainClearColor = Color{ 0.08f, 0.09f, 0.11f, 1.0f };
	bool                          m_isInitialized = false;

	// 메인 surface 윈도우 이벤트 구독 토큰(스크립트 전달용). Initialize 구독, Finalize 해지.
	SurfaceEventToken              m_surfaceEventToken = 0;

	// Track the last known surface size to detect window resize each frame.
	int m_lastSurfaceWidth  = 0;
	int m_lastSurfaceHeight = 0;

	// 입력 폴링/디스패치 게이트 — 메인 surface 포커스 상태(FocusGained/Lost 로 갱신).
	bool m_surfaceFocused = true;
};
