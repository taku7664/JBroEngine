#include "pch.h"
#include "Engine.h"

#include "Core/EngineCore.h"
#include "Core/ScriptCore.h"
#include "Core/Debug/Debug.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Input/Input.h"
#include "Core/Input/InputSystem.h"
#include "Core/Logging/Logger.h"
#include "Core/Logging/LoggerInternal.h"
#include "Core/Localization/LocalizationManager.h"
#include "Core/Resource/ResourceRegistry.h"
#include "Core/Module/Module.h"
#include "Core/Asset/AssetManager.h"
#include "Core/Asset/FileAsset.h"
#include "Core/Asset/AudioAsset.h"
#include "Core/Asset/AudioEffectAsset.h"
#include "Core/Asset/MaterialAsset.h"
#include "Core/Asset/SpriteAsset.h"
#include "Core/Platform/IPlatform.h"
#include "Core/Platform/IRenderSurface.h"
#include "Core/Renderer/Forward2DRenderer.h"
#include "Core/Renderer/RenderResourceCache.h"
#include "Core/Renderer/RenderScene.h"
#include "Core/Platform/Mobile/MobilePlatform.h"
#include "Core/Platform/Windows/WindowsPlatform.h"
#include "Core/Platform/Web/WebPlatform.h"
#include "Core/RHI/IRHICommandContext.h"
#include "Core/RHI/IRHIDevice.h"
#include "Core/RHI/IRHISwapchain.h"
#include "Core/RHI/D3D11/D3D11RHIDevice.h"
#include "Core/RHI/WebGPU/WebGPURHIDevice.h"
#include "Core/RHI/Vulkan/VulkanRHIDevice.h"
#include "Core/RHI/EmptyRHIDevice.h"
#include "Core/Audio/IAudioDevice.h"
#include "Core/Audio/MiniAudio/MiniAudioDevice.h"
#include "Core/Audio/EmptyAudio/EmptyAudioDevice.h"
#include "Core/Task/TaskManager.h"
#include "Core/Random/RandomService.h"
#include "Core/Math/MathService.h"
#include "Core/Time/Time.h"
#include "Core/Network/NetworkManager.h"
#include "Core/Network/INetworkManager.h"
#include "Core/Debug/DebugDraw2D.h"
#if JBRO_PLATFORM_WINDOWS
#include "Core/Network/Windows/WinSockTransport.h"
#elif JBRO_PLATFORM_WEB
#include "Core/Network/Web/WebSocketTransport.h"
#endif
#include "GameFramework/Component/BuiltinComponentRegistry.h"
#include "GameFramework/Rendering/GameCamera.h"
#include "GameFramework/Reflection/ReflectionRegistry.h"
#include "GameFramework/Scene/Scene.h"
#include "GameFramework/Scene/SceneManager.h"

#include <algorithm>

CEngine::CEngine() = default;

CEngine::~CEngine() = default;

bool CEngine::Initialize()
{
	if (m_isInitialized)
	{
		return true;
	}

	if (false == InitializePlatform())
	{
		return false;
	}

	if (false == InitializeCoreServices())
	{
		return false;
	}

	if (false == InitializeRHI())
	{
		return false;
	}

	if (false == InitializeAssetManager())
	{
		return false;
	}

	// 오디오 디바이스 — 실패해도 엔진 자체는 계속 동작하도록 (조용한 게임).
	InitializeAudio();

	// AssetManager 가 준비된 직후 ResourceRegistry 를 부트스트랩.
	// Resources/resources.yaml 에 기술된 영구 리소스를 persistent 로 등록 + 즉시 로드한다.
	m_resourceRegistry = MakeOwnerPtr<CResourceRegistry>();
	if (m_resourceRegistry)
	{
		m_resourceRegistry->Initialize(File::Path("Resources"),
		                               File::Path("resources.yaml"),
		                               m_assetManager.GetSafePtr());
		Engine.ResourceRegistry = m_resourceRegistry.GetSafePtr();
	}

	if (false == InitializeRenderer())
	{
		return false;
	}

	// 메인 surface 윈도우 이벤트(포커스/리사이즈) 구독 → 활성 씬 스크립트로 전달.
	if (Engine.MainRenderSurface)
	{
		m_surfaceEventToken = Engine.MainRenderSurface->Subscribe(
			[this](const SurfaceEvent& surfaceEvent) { OnSurfaceEvent(surfaceEvent); });

		// 입력 시스템에 메인 surface 주입(커서 좌표 폴링용).
		if (m_inputSystem)
		{
			m_inputSystem->SetMainSurface(Engine.MainRenderSurface.TryGet());
		}
	}

	SyncScriptCore();
	m_isInitialized = true;
	return true;
}

void CEngine::OnSurfaceEvent(const SurfaceEvent& surfaceEvent)
{
	// 입력 폴링/디스패치 게이트 갱신 — 포커스 잃으면 InputSystem 이 디바이스 클리어 + dispatch 스킵.
	if (ESurfaceEventType::FocusGained == surfaceEvent.Type)
	{
		m_surfaceFocused = true;
	}
	else if (ESurfaceEventType::FocusLost == surfaceEvent.Type)
	{
		m_surfaceFocused = false;
	}

	// 윈도우 이벤트를 활성 씬의 스크립트 인스턴스들에 전달(재생 중에만 인스턴스 존재 → 자동 게이팅).
	if (m_sceneManager)
	{
		if (CScene* scene = m_sceneManager->GetActiveScene().TryGet())
		{
			scene->DispatchSurfaceEventToScripts(surfaceEvent);
		}
	}
}

bool CEngine::Update()
{
	PlatformEvent platformEvent;
	if (m_platform)
	{
		m_platform->PollEvents(platformEvent);
	}

	if (platformEvent.WantsExit)
	{
		return false;
	}

	BeginFrame();
	UpdateModules();
	UpdateCoreServices();
	RenderFrame();
	EndFrame();
	return true;
}

void CEngine::Finalize()
{
	// surface 파괴 전에 윈도우 이벤트 구독 해지(댕글링 핸들러 방지).
	if (0 != m_surfaceEventToken && Engine.MainRenderSurface)
	{
		Engine.MainRenderSurface->Unsubscribe(m_surfaceEventToken);
		m_surfaceEventToken = 0;
	}

	if (m_renderer)
	{
		m_renderer->Finalize();
		m_renderer.Reset();
	}
	m_renderScene.Reset();

	Engine.TaskManager = nullptr;
	if (m_taskManager)
	{
		m_taskManager->Finalize();
		m_taskManager.Reset();
	}

	// ResourceRegistry 가 AssetManager 보다 먼저 정리되어야 한다 — 보유 중인 SafePtr<IAsset>
	// 가 AssetManager 의 객체를 참조하기 때문.
	Engine.ResourceRegistry = nullptr;
	if (m_resourceRegistry)
	{
		m_resourceRegistry->Finalize();
		m_resourceRegistry.Reset();
	}

	Engine.AssetManager = nullptr;
	if (m_assetManager)
	{
		m_assetManager->Finalize();
		m_assetManager.Reset();
	}

	// AssetManager 가 unload 시 cache->Release 를 호출하므로 cache 는 그 이후에 폐기한다.
	// rhiDevice 가 살아있을 때 GPU 객체가 정상 폐기되도록 rhiDevice 보다 먼저 비운다.
	if (m_renderResourceCache)
	{
		m_renderResourceCache->Clear();
		m_renderResourceCache.Reset();
	}
	Engine = EngineCore{};
	Script = ScriptCore{};
	Engine.Network = nullptr;
	Engine.Debug = nullptr;
	if (m_networkManager)
	{
		m_networkManager->Finalize();
		m_networkManager.Reset();
	}

	// DebugDraw2D must be cleared before scene manager so any scene-system
	// destructors that draw debug primitives don't hit a dangling pointer.
	Engine.DebugDraw2D = nullptr;
	m_debugDraw.Reset();

	Engine.SceneManager = nullptr;
	m_sceneManager.Reset();
	Engine.Random = nullptr;
	m_randomService.Reset();
	Engine.Math = nullptr;
	m_mathService.Reset();
	Engine.Reflection = nullptr;
	m_reflectionRegistry.Reset();
	Engine.Logger = nullptr;
	m_logger.Reset();
	m_debug.Reset();
#if JBRO_EDITOR
	Engine.Localization = nullptr;
	if (m_localization)
	{
		m_localization->Finalize();
	}
	m_localization.Reset();
#else
	Engine.Localization = nullptr;
#endif
	Engine.FileSystem = nullptr;
	if (m_fileSystem)
	{
		m_fileSystem->Finalize();
		m_fileSystem.Reset();
	}
	Engine.InputSystem = nullptr;
	if (m_inputSystem)
	{
		m_inputSystem->Shutdown();
		m_inputSystem.Reset();
	}
	if (m_input)
	{
		m_input->BindSystem(nullptr);
	}
	Engine.Input = nullptr;
	m_input.Reset();
	Engine.Time = nullptr;
	m_time.Reset();

	if (m_audioDevice)
	{
		m_audioDevice->Finalize();
		m_audioDevice.Reset();
	}

	if (m_rhiDevice)
	{
		m_rhiDevice->Finalize();
		m_rhiDevice.Reset();
	}

	if (m_platform)
	{
		m_platform->Finalize();
		m_platform.Reset();
	}

	m_isInitialized = false;
}

void CEngine::InitializeModule(CModule& module, const char* moduleName)
{
	if (std::find(m_modules.begin(), m_modules.end(), &module) == m_modules.end())
	{
		m_modules.push_back(&module);
	}
	module.Initialize(moduleName);
}

void CEngine::FinalizeModule(CModule& module)
{
	module.Finalize();
	m_modules.erase(std::remove(m_modules.begin(), m_modules.end(), &module), m_modules.end());
}

void CEngine::SetPlatformDesc(const PlatformDesc& desc)
{
	if (m_isInitialized)
	{
		return;
	}
	m_platformDesc = desc;
}

void CEngine::SetMainClearColor(const Color& color)
{
	m_mainClearColor = color;
}

void CEngine::SetGameRenderCameras(std::vector<GameRenderCameraDesc> cameras)
{
	m_gameRenderCameras = std::move(cameras);
}

const ScriptCore& CEngine::GetScriptCore() const
{
	return Script;
}

SafePtr<IPlatform> CEngine::GetPlatform() const
{
	return m_platform.GetSafePtr();
}

SafePtr<IRenderSurface> CEngine::GetMainRenderSurface() const
{
	if (!m_platform)
	{
		return nullptr;
	}

	return m_platform->GetMainRenderSurface();
}

SafePtr<IRHIDevice> CEngine::GetRHIDevice() const
{
	return m_rhiDevice.GetSafePtr();
}

SafePtr<IAudioDevice> CEngine::GetAudioDevice() const
{
	return m_audioDevice.GetSafePtr();
}

SafePtr<IAssetManager> CEngine::GetAssetManager() const
{
	return m_assetManager.GetSafePtr();
}

SafePtr<IRenderer> CEngine::GetRenderer() const
{
	return m_renderer.GetSafePtr();
}

SafePtr<IRenderScene> CEngine::GetRenderScene() const
{
	return m_renderScene.GetSafePtr();
}

bool CEngine::InitializeCoreServices()
{
	m_time = MakeOwnerPtr<CTime>();
	m_input = MakeOwnerPtr<CInput>();
	m_inputSystem = MakeOwnerPtr<CInputSystem>();
	m_fileSystem = MakeOwnerPtr<CFileSystem>();
	m_taskManager = MakeOwnerPtr<CTaskManager>();
	m_randomService = MakeOwnerPtr<CRandomService>();
	m_mathService = MakeOwnerPtr<CMathService>();
	m_reflectionRegistry = MakeOwnerPtr<CReflectionRegistry>();
	m_logger = MakeOwnerPtr<CLogger>();
	m_debug = MakeOwnerPtr<CDebug>();
#if JBRO_EDITOR
	m_localization = MakeOwnerPtr<CLocalizationManager>();
#endif
	m_sceneManager = MakeOwnerPtr<CSceneManager>();
	if (!m_time || !m_input || !m_inputSystem || !m_fileSystem || !m_taskManager || !m_randomService || !m_mathService || !m_reflectionRegistry || !m_logger || !m_debug || !m_sceneManager)
	{
		return false;
	}
#if JBRO_EDITOR
	if (!m_localization)
	{
		return false;
	}
#endif

	m_time->Reset();
	if (false == m_fileSystem->Initialize(File::Path("Assets"), EFileSystemAccess::ReadWrite))
	{
		return false;
	}
	if (false == m_taskManager->Initialize())
	{
		return false;
	}

	m_debugDraw = MakeOwnerPtr<CDebugDraw2D>();

	m_inputSystem->Initialize();
	m_inputSystem->SetTaskManager(m_taskManager.Get()); // 진동 타이머 워커 스레드용
	m_input->BindSystem(m_inputSystem.Get());

	Engine.Time = m_time.GetSafePtr();
	Engine.Input = m_input.GetSafePtr();
	Engine.InputSystem = m_inputSystem.GetSafePtr();
	Engine.FileSystem = m_fileSystem.GetSafePtr();
	Engine.TaskManager = m_taskManager.GetSafePtr();
	Engine.Random = m_randomService.GetSafePtr();
	Engine.Math = m_mathService.GetSafePtr();
	Engine.Reflection = m_reflectionRegistry.GetSafePtr();
	Engine.Logger = m_logger.GetSafePtr();
	Engine.Debug = m_debug.GetSafePtr();
#if JBRO_EDITOR
	m_localization->Initialize(m_fileSystem.GetSafePtr());
	Engine.Localization = m_localization.GetSafePtr();
#else
	Engine.Localization = nullptr;
#endif
	Engine.SceneManager = m_sceneManager.GetSafePtr();
	Engine.DebugDraw2D = m_debugDraw.GetSafePtr();
	CSystemLog::Info("Core services initialized.");

	RegisterBuiltinComponents(*m_reflectionRegistry);
	return true;
}

bool CEngine::InitializeNetwork()
{
	if (m_networkManager)
	{
		return true; // Already initialized.
	}

	OwnerPtr<INetworkTransport> transport;
#if JBRO_PLATFORM_WINDOWS
	transport = MakeOwnerPtr<CWinSockTransport>();
#elif JBRO_PLATFORM_WEB
	transport = MakeOwnerPtr<CWebSocketTransport>();
#else
	CSystemLog::Warning("InitializeNetwork: unsupported platform.");
	return false;
#endif

	m_networkManager = MakeOwnerPtr<CNetworkManager>(std::move(transport));
	if (!m_networkManager || false == m_networkManager->Initialize())
	{
		m_networkManager.Reset();
		return false;
	}

	Engine.Network = m_networkManager.GetSafePtr();
	SyncScriptCore(); // 네트워크는 지연 초기화 → 스크립트 번들에 1회 반영(매 프레임 아님).
	CSystemLog::Info("Network initialized.");
	return true;
}

bool CEngine::InitializePlatform()
{
#if JBRO_PLATFORM_WEB
	m_platform = MakeOwnerPtr<CWebPlatform>();
#elif JBRO_PLATFORM_MOBILE
	m_platform = MakeOwnerPtr<CMobilePlatform>();
#elif JBRO_PLATFORM_WINDOWS
	m_platform = MakeOwnerPtr<CWindowsPlatform>();
#else
	return false;
#endif

	m_platformDesc.IsEditor = JBRO_EDITOR != 0;
	if (!m_platform || false == m_platform->Initialize(m_platformDesc))
	{
		return false;
	}

	Engine.Platform = m_platform.GetSafePtr();
	Engine.MainRenderSurface = GetMainRenderSurface();
	return true;
}

bool CEngine::InitializeRHI()
{
#if JBRO_PLATFORM_WEB
	m_rhiDevice = MakeOwnerPtr<CWebGPURHIDevice>();
#elif JBRO_PLATFORM_MOBILE
	m_rhiDevice = MakeOwnerPtr<CVulkanRHIDevice>();
#elif JBRO_PLATFORM_WINDOWS
	m_rhiDevice = MakeOwnerPtr<CD3D11RHIDevice>();
#else
	m_rhiDevice = MakeOwnerPtr<CEmptyRHIDevice>();
#endif

	RHIDesc desc;
#if JBRO_PLATFORM_WEB
	desc.Api = ERHIApi::WebGPU;
#elif JBRO_PLATFORM_MOBILE
	desc.Api = ERHIApi::Vulkan;
#elif JBRO_PLATFORM_WINDOWS
	desc.Api = ERHIApi::D3D11;
#else
	desc.Api = ERHIApi::None;
#endif
	FillRenderSurfaceDesc(desc);
#if defined(_DEBUG)
	desc.EnableDebugLayer = true;
#endif
	if (!m_rhiDevice || false == m_rhiDevice->Initialize(desc))
	{
		return false;
	}

	Engine.RHIDevice = m_rhiDevice.GetSafePtr();
	return true;
}

bool CEngine::InitializeAssetManager()
{
	m_assetManager = MakeOwnerPtr<CAssetManager>();
	AssetManagerDesc desc;
	desc.AssetRootPath = File::Path("Assets");
	if (!m_assetManager || false == m_assetManager->Initialize(desc))
	{
		return false;
	}

	m_assetManager->RegisterLoader(MakeOwnerPtr<CFileAssetLoader>(EAssetType::Scene));
	m_assetManager->RegisterLoader(MakeOwnerPtr<CFileAssetLoader>(EAssetType::Prefab));
	m_assetManager->RegisterLoader(MakeOwnerPtr<CFileAssetLoader>(EAssetType::Shader));
	m_assetManager->RegisterLoader(MakeOwnerPtr<CFileAssetLoader>(EAssetType::Script));
	m_assetManager->RegisterLoader(MakeOwnerPtr<CFileAssetLoader>(EAssetType::Custom));
	m_assetManager->RegisterLoader(MakeOwnerPtr<CSpriteAssetLoader>());
	m_assetManager->RegisterLoader(MakeOwnerPtr<CMaterialAssetLoader>());
	m_assetManager->RegisterLoader(MakeOwnerPtr<CAudioAssetLoader>());
	m_assetManager->RegisterLoader(MakeOwnerPtr<CAudioEffectAssetLoader>());

	Engine.AssetManager = m_assetManager.GetSafePtr();
	return true;
}

bool CEngine::InitializeAudio()
{
	AudioDeviceDesc desc;

#if defined(JBRO_HAS_MINIAUDIO) && JBRO_HAS_MINIAUDIO
	{
		OwnerPtr<CMiniAudioDevice> mini = MakeOwnerPtr<CMiniAudioDevice>();
		if (mini && mini->Initialize(desc))
		{
			m_audioDevice = std::move(mini);
			Engine.Audio = m_audioDevice.GetSafePtr();
			return true;
		}
	}
#endif

	// 폴백 — 빈 디바이스. 게임은 무음이 되지만 컴포넌트/시스템은 안전하게 동작.
	OwnerPtr<CEmptyAudioDevice> empty = MakeOwnerPtr<CEmptyAudioDevice>();
	if (empty)
	{
		empty->Initialize(desc);
		m_audioDevice = std::move(empty);
		Engine.Audio = m_audioDevice.GetSafePtr();
		return true;
	}
	return false;
}

bool CEngine::InitializeRenderer()
{
	m_renderScene = MakeOwnerPtr<CRenderScene>();
	m_renderer = MakeOwnerPtr<CForward2DRenderer>();
	if (!m_renderScene || !m_renderer)
	{
		return false;
	}

	m_renderResourceCache = MakeOwnerPtr<CRenderResourceCache>(GetRHIDevice());
	Engine.RenderResourceCache = m_renderResourceCache.GetSafePtr();
	Engine.RenderScene = m_renderScene.GetSafePtr();

	RendererDesc desc;
	desc.RHIDevice = GetRHIDevice();
	if (false == m_renderer->Initialize(desc))
	{
		return false;
	}

	Engine.Renderer = m_renderer.GetSafePtr();
	return true;
}

void CEngine::BeginFrame()
{
	// Clear debug draw commands accumulated in the previous frame.
	if (m_debugDraw)
	{
		m_debugDraw->Clear();
	}

	if (m_time)
	{
		m_time->BeginFrame();
	}
	if (m_inputSystem)
	{
		// 프레임 입력 갱신 + 레이어 순 핸들러 dispatch(포커스 게이트). 메시지가 아니라 프레임이 구동.
		m_inputSystem->Update(m_surfaceFocused);
	}
	if (m_taskManager)
	{
		m_taskManager->DrainMainThreadCallbacks();
	}

	for (CModule* module : m_modules)
	{
		if (module)
		{
			module->BeginFrame();
		}
	}
}

void CEngine::UpdateModules()
{
	for (CModule* module : m_modules)
	{
		if (module)
		{
			module->Update();
		}
	}
}

void CEngine::UpdateCoreServices()
{
	// 오디오 디바이스 tick — ended player GC, marker dispatch 등을 백엔드가 처리.
	if (m_audioDevice)
	{
		const float dt = m_time ? m_time->GetDeltaSeconds() : 0.0f;
		m_audioDevice->Tick(dt);
	}
	if (m_sceneManager)
	{
		m_sceneManager->Update();
	}
	if (m_networkManager)
	{
		m_networkManager->Update();
	}
}

void CEngine::PrepareRenderModules()
{
	for (CModule* module : m_modules)
	{
		if (module)
		{
			module->PrepareRender();
		}
	}
}

void CEngine::RenderFrame()
{
	if (!m_rhiDevice)
	{
		return;
	}

	// ── Window resize detection ────────────────────────────────────────────────
	// Query the actual client area size each frame.  When it differs from the
	// last known size, call HandleSurfaceResize so the swapchain buffers and the
	// command context's render-target view are updated before we render.
	if (SafePtr<IRenderSurface> surface = GetMainRenderSurface())
	{
		const RenderSurfaceSize currentSize = surface->GetSize();
		if (currentSize.Width  > 0 && currentSize.Height > 0 &&
		    (currentSize.Width  != m_lastSurfaceWidth ||
		     currentSize.Height != m_lastSurfaceHeight))
		{
			m_rhiDevice->HandleSurfaceResize(currentSize);
			m_lastSurfaceWidth  = currentSize.Width;
			m_lastSurfaceHeight = currentSize.Height;
		}
	}

	m_rhiDevice->BeginFrame();
	if (m_renderer)
	{
		m_renderer->BeginFrame();
		// surface pre-rotation(표시 방향 보정) — 스왑체인 transform 으로부터 매 프레임 갱신.
		// 런타임 회전 시 스왑체인 재생성으로 값이 바뀌면 자동 반영된다.
		float preRotCos = 1.0f;
		float preRotSin = 0.0f;
		if (SafePtr<IRHISwapchain> swapchain = m_rhiDevice->GetSwapchain())
		{
			preRotCos = swapchain->GetPreRotationCosR();
			preRotSin = swapchain->GetPreRotationSinR();
		}
		m_renderer->SetSurfacePreRotation(preRotCos, preRotSin);
	}
	PrepareRenderModules();

	SafePtr<IRHICommandContext> commandContext = m_rhiDevice->GetImmediateCommandContext();
	if (commandContext)
	{
		if (m_renderer && m_renderScene && false == m_gameRenderCameras.empty())
		{
			if (SafePtr<IRenderSurface> mainRenderSurface = GetMainRenderSurface())
			{
				RenderGameCameraStack(
					*commandContext,
					*m_renderer,
					*m_renderScene,
					m_gameRenderCameras,
					mainRenderSurface->GetSize());
				m_renderScene->Clear();
			}
		}
		else
		{
			RenderPassDesc renderPassDesc;
			renderPassDesc.ColorAttachment.LoadOp = ERHILoadOp::Clear;
			renderPassDesc.ColorAttachment.StoreOp = ERHIStoreOp::Store;
			renderPassDesc.ColorAttachment.ClearColor = m_mainClearColor;

			commandContext->BeginRenderPass(renderPassDesc);
			if (m_renderer && m_renderScene)
			{
				if (SafePtr<IRenderSurface> mainRenderSurface = GetMainRenderSurface())
				{
					m_renderer->SetRenderTargetSize(mainRenderSurface->GetSize());
				}
				m_renderer->Render(*m_renderScene);
				m_renderScene->Clear();
			}
			commandContext->EndRenderPass();
		}

		RenderPassDesc moduleRenderPassDesc;
		moduleRenderPassDesc.ColorAttachment.LoadOp = ERHILoadOp::Load;
		moduleRenderPassDesc.ColorAttachment.StoreOp = ERHIStoreOp::Store;
		commandContext->BeginRenderPass(moduleRenderPassDesc);
		for (CModule* module : m_modules)
		{
			if (module)
			{
				module->Render();
			}
		}
		commandContext->EndRenderPass();
	}

	m_rhiDevice->EndFrame();
}

void CEngine::EndFrame()
{
	for (CModule* module : m_modules)
	{
		if (module)
		{
			module->EndFrame();
		}
	}
}

RenderSurfaceSize CEngine::GetRenderTargetSize() const
{
	// 실제 렌더되는 픽셀 크기의 권위는 스왑체인(표시 방향 기준)이다. Android pre-rotation 시
	// 메인 surface 가 보고하는 네이티브 방향 크기와 다를 수 있으므로 스왑체인을 우선한다.
	if (m_rhiDevice)
	{
		if (SafePtr<IRHISwapchain> swapchain = m_rhiDevice->GetSwapchain())
		{
			const RenderSurfaceSize size = swapchain->GetSize();
			if (size.Width > 0 && size.Height > 0)
			{
				return size;
			}
		}
	}
	if (SafePtr<IRenderSurface> surface = GetMainRenderSurface())
	{
		return surface->GetSize();
	}
	return RenderSurfaceSize{ 0, 0 };
}

void CEngine::FillRenderSurfaceDesc(RHIDesc& desc) const
{
	SafePtr<IRenderSurface> mainRenderSurface = GetMainRenderSurface();
	if (!mainRenderSurface)
	{
		return;
	}

	desc.Surface.NativeHandle = mainRenderSurface->GetNativeSurfaceHandle();
	desc.Surface.Size = mainRenderSurface->GetSize();
}

void CEngine::SyncScriptCore()
{
	// 전역 `Engine`(EngineCore, 전체 API)에서 스크립트용 엄선 부분집합만 `Script` 로 복사한다.
	// ⚠ 1회성 동기화다(부팅/지연초기화 시). 매 프레임 호출 금지 — 서비스 포인터는 수명 동안
	//   불변이라 재복사할 이유가 없다. 렌더러/RHI/플랫폼은 스크립트에 노출하지 않는다.
	Script.AssetManager = Engine.AssetManager;
	Script.Audio        = Engine.Audio;
	Script.Debug        = Engine.Debug;
	Script.Time         = Engine.Time;
	Script.Input        = Engine.Input;
	Script.SceneManager = Engine.SceneManager;
	Script.FileSystem   = Engine.FileSystem;
	Script.Random       = Engine.Random;
	Script.Math         = Engine.Math;
	Script.Reflection   = Engine.Reflection;
	Script.Logger       = Engine.Logger;
	Script.Localization = Engine.Localization;
	Script.Network      = Engine.Network;
	Script.DebugDraw2D  = Engine.DebugDraw2D;
}
