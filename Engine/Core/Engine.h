#pragma once

#include "Core/ScriptCore.h"
#include "Core/Platform/PlatformTypes.h"
#include "Core/RHI/RHITypes.h"
#include "GameFramework/Rendering/GameCamera.h"
#include "Utillity/Types/FrameSectionProfiler.h"
#include "Core/Debug/GpuProfiler.h"
#include "Core/Debug/CpuProfiler.h"

#include <functional>
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
class CCanvasManager;
class CNetworkManager;
class CDebugDraw2D;
class CPrefabSpawner;
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
	// 매 프레임 도는 스냅샷 교체 — 값 전달(move)이 아니라 **swap** 이다. move 면 호출자의
	// 버퍼가 빈 채 남아 다음 프레임에 다시 할당한다. swap 이면 두 버퍼가 자리를 바꿔
	// 양쪽 용량이 살아남는다(호출자는 스크래치 멤버를 그대로 다음 프레임에 재사용).
	void SwapGameRenderViewports(std::vector<GameRenderViewportDesc>& viewports);
	void SwapGameRenderLights(std::vector<GameRenderLightDesc>& lights);
	void SwapGameRenderLayers(std::vector<GameRenderLayerDesc>& layers);
	// 캔버스 바탕색 — 컴포짓 맨 아래에 깔린다.
	void SetGameRenderBackgroundColor(const float color[4]);
	// 렌더 직전(리사이즈 반영 후, 카메라 스택 사용 전) 훅 — 카메라/라이트 스냅샷을
	// 시뮬레이션 *이후* 상태로 수집하기 위한 것. OnPreTick 수집은 1프레임 지연을 만든다.
	void SetPreRenderCallback(std::function<void()> callback);

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

	// 네이티브 렌더 버퍼(스왑체인) 크기. 표시 방향과 다를 수 있다(모바일 회전).
	RenderSurfaceSize GetNativeRenderBufferSize() const;
	// 콘텐츠 보정 회전(0/90/180/270). desired orientation(빌드설정)이 권위.
	// Auto 면 플랫폼 디스플레이 회전(JNI), 그 외엔 desired-vs-버퍼방향 비교.
	int GetEffectiveDisplayRotation() const;

	// 메인 surface 윈도우 이벤트(포커스/리사이즈) → 활성 캔버스 스크립트로 전달.
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
	OwnerPtr<CFrameSectionProfiler> m_frameProfiler;
	OwnerPtr<CGpuProfiler>        m_gpuProfiler;
	OwnerPtr<CCpuProfiler>        m_cpuProfiler;
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
	OwnerPtr<CCanvasManager>       m_canvasManager;
	OwnerPtr<CNetworkManager>     m_networkManager;   // null until InitializeNetwork()
	OwnerPtr<CDebugDraw2D>        m_debugDraw;
	OwnerPtr<CPrefabSpawner>      m_prefabSpawner;
	std::vector<CModule*>         m_modules;
	std::vector<GameRenderViewportDesc> m_gameRenderViewports;
	std::vector<GameRenderLightDesc> m_gameRenderLights;
	std::vector<GameRenderLayerDesc> m_gameRenderLayers;
	float m_gameRenderBackgroundColor[4] = { 0.08f, 0.09f, 0.11f, 1.0f };
	std::function<void()>         m_preRenderCallback;
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
