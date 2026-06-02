#include "pch.h"
#include "Engine.h"

#include "Core/Core.h"
#include "Core/EngineCore.h"
#include "Core/Debug/Debug.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Input/Input.h"
#include "Core/Logging/Logger.h"
#include "Core/Logging/LoggerInternal.h"
#include "Core/Localization/LocalizationManager.h"
#include "Core/Resource/ResourceRegistry.h"
#include "Core/Module/Module.h"
#include "Core/Asset/AssetManager.h"
#include "Core/Asset/FileAsset.h"
#include "Core/Asset/AudioAsset.h"
#include "Core/Asset/MaterialAsset.h"
#include "Core/Asset/SpriteAsset.h"
#include "Core/Platform/IPlatform.h"
#include "Core/Platform/IRenderSurface.h"
#include "Core/Renderer/Forward2DRenderer.h"
#include "Core/Renderer/RenderResourceCache.h"
#include "Core/Renderer/RenderScene.h"
#include "Core/Platform/Windows/WindowsPlatform.h"
#include "Core/Platform/Web/WebPlatform.h"
#include "Core/RHI/IRHICommandContext.h"
#include "Core/RHI/IRHIDevice.h"
#include "Core/RHI/D3D11/D3D11RHIDevice.h"
#include "Core/RHI/WebGPU/WebGPURHIDevice.h"
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
		                               m_assetManager.GetSafePtr(),
		                               m_rhiDevice.GetSafePtr());
		Core::ResourceRegistry   = m_resourceRegistry.GetSafePtr();
		Engine.ResourceRegistry  = Core::ResourceRegistry;
	}

	if (false == InitializeRenderer())
	{
		return false;
	}

	SyncEngineCore();
	m_isInitialized = true;
	return true;
}

bool CEngine::Update()
{
	PlatformEvent platformEvent;
	if (m_platform)
	{
		m_platform->PollEvents(platformEvent);
	}

	m_isApplicationFocused = platformEvent.IsFocused;
	m_applicationFocusGained = platformEvent.FocusGained;
	m_applicationFocusLost = platformEvent.FocusLost;
	SyncEngineCore();

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
	if (m_renderer)
	{
		m_renderer->Finalize();
		m_renderer.Reset();
	}
	m_renderScene.Reset();

	Core::TaskManager = nullptr;
	if (m_taskManager)
	{
		m_taskManager->Finalize();
		m_taskManager.Reset();
	}

	// ResourceRegistry 가 AssetManager 보다 먼저 정리되어야 한다 — 보유 중인 SafePtr<IAsset>
	// 가 AssetManager 의 객체를 참조하기 때문.
	Core::ResourceRegistry = nullptr;
	if (m_resourceRegistry)
	{
		m_resourceRegistry->Finalize();
		m_resourceRegistry.Reset();
	}

	Core::AssetManager = nullptr;
	if (m_assetManager)
	{
		m_assetManager->Finalize();
		m_assetManager.Reset();
	}

	Engine = EngineCore{};
	Core::Network = nullptr;
	Core::Debug = nullptr;
	if (m_networkManager)
	{
		m_networkManager->Finalize();
		m_networkManager.Reset();
	}

	// DebugDraw2D must be cleared before scene manager so any scene-system
	// destructors that draw debug primitives don't hit a dangling pointer.
	Core::DebugDraw2D = nullptr;
	m_debugDraw.Reset();

	Core::SceneManager = nullptr;
	m_sceneManager.Reset();
	Core::Random = nullptr;
	m_randomService.Reset();
	Core::Math = nullptr;
	m_mathService.Reset();
	Core::Reflection = nullptr;
	m_reflectionRegistry.Reset();
	Core::Logger = nullptr;
	m_logger.Reset();
	m_debug.Reset();
#if JBRO_EDITOR
	Core::Localization = nullptr;
	if (m_localization)
	{
		m_localization->Finalize();
	}
	m_localization.Reset();
#else
	Core::Localization = nullptr;
#endif
	Core::FileSystem = nullptr;
	if (m_fileSystem)
	{
		m_fileSystem->Finalize();
		m_fileSystem.Reset();
	}
	Core::Input = nullptr;
	m_input.Reset();
	Core::Time = nullptr;
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

	SyncEngineCore();
	m_isInitialized = false;
}

void CEngine::InitializeModule(CModule& module, const char* moduleName)
{
	SyncEngineCore();
	if (std::find(m_modules.begin(), m_modules.end(), &module) == m_modules.end())
	{
		m_modules.push_back(&module);
	}
	module.Initialize(moduleName, Engine);
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

const EngineCore& CEngine::GetEngineCore() const
{
	return Engine;
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
	if (!m_time || !m_input || !m_fileSystem || !m_taskManager || !m_randomService || !m_mathService || !m_reflectionRegistry || !m_logger || !m_debug || !m_sceneManager)
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

	Core::Time = m_time.GetSafePtr();
	Core::Input = m_input.GetSafePtr();
	Core::FileSystem = m_fileSystem.GetSafePtr();
	Core::TaskManager = m_taskManager.GetSafePtr();
	Core::Random = m_randomService.GetSafePtr();
	Core::Math = m_mathService.GetSafePtr();
	Core::Reflection = m_reflectionRegistry.GetSafePtr();
	Core::Logger = m_logger.GetSafePtr();
	Core::Debug = m_debug.GetSafePtr();
#if JBRO_EDITOR
	m_localization->Initialize(m_fileSystem.GetSafePtr());
	Core::Localization = m_localization.GetSafePtr();
#else
	Core::Localization = nullptr;
#endif
	Core::SceneManager = m_sceneManager.GetSafePtr();
	Core::DebugDraw2D = m_debugDraw.GetSafePtr();
	Engine.Debug = Core::Debug;
	Engine.Time = Core::Time;
	Engine.Input = Core::Input;
	Engine.FileSystem = Core::FileSystem;
	Engine.TaskManager = Core::TaskManager;
	Engine.Random = Core::Random;
	Engine.Math = Core::Math;
	Engine.Reflection = Core::Reflection;
	Engine.Logger = Core::Logger;
	Engine.Localization = Core::Localization;
	Engine.SceneManager = Core::SceneManager;
	Engine.DebugDraw2D = Core::DebugDraw2D;
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

	Core::Network = m_networkManager.GetSafePtr();
	Engine.Network = Core::Network;
	SyncEngineCore();
	CSystemLog::Info("Network initialized.");
	return true;
}

bool CEngine::InitializePlatform()
{
#if JBRO_PLATFORM_WEB
	m_platform = MakeOwnerPtr<CWebPlatform>();
#elif JBRO_PLATFORM_WINDOWS
	m_platform = MakeOwnerPtr<CWindowsPlatform>();
#else
	return false;
#endif

	m_platformDesc.IsEditor = JBRO_EDITOR != 0;
	return m_platform && m_platform->Initialize(m_platformDesc);
}

bool CEngine::InitializeRHI()
{
#if JBRO_PLATFORM_WEB
	m_rhiDevice = MakeOwnerPtr<CWebGPURHIDevice>();
#elif JBRO_PLATFORM_WINDOWS
	m_rhiDevice = MakeOwnerPtr<CD3D11RHIDevice>();
#else
	m_rhiDevice = MakeOwnerPtr<CEmptyRHIDevice>();
#endif

	RHIDesc desc;
#if JBRO_PLATFORM_WEB
	desc.Api = ERHIApi::WebGPU;
#elif JBRO_PLATFORM_WINDOWS
	desc.Api = ERHIApi::D3D11;
#else
	desc.Api = ERHIApi::None;
#endif
	FillRenderSurfaceDesc(desc);
#if defined(_DEBUG)
	desc.EnableDebugLayer = true;
#endif
	return m_rhiDevice && m_rhiDevice->Initialize(desc);
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

	RendererDesc desc;
	desc.RHIDevice = GetRHIDevice();
	return m_renderer->Initialize(desc);
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
	if (m_input)
	{
		m_input->BeginFrame();
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
	if (m_input)
	{
		m_input->EndFrame();
	}
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

void CEngine::SyncEngineCore()
{
	Engine.Platform = GetPlatform();
	Engine.MainRenderSurface = GetMainRenderSurface();
	Engine.RHIDevice = GetRHIDevice();
	Engine.AssetManager = GetAssetManager();
	Engine.Audio = GetAudioDevice();
	Engine.Renderer = GetRenderer();
	Engine.RenderScene = GetRenderScene();
	Engine.RenderResourceCache = m_renderResourceCache ? m_renderResourceCache.GetSafePtr() : nullptr;
	Engine.Debug = m_debug ? m_debug.GetSafePtr() : nullptr;
	Engine.Time = m_time ? m_time.GetSafePtr() : nullptr;
	Engine.Input = m_input ? m_input.GetSafePtr() : nullptr;
	Engine.SceneManager = m_sceneManager ? m_sceneManager.GetSafePtr() : nullptr;
	Engine.FileSystem = m_fileSystem ? m_fileSystem.GetSafePtr() : nullptr;
	Engine.TaskManager = m_taskManager ? m_taskManager.GetSafePtr() : nullptr;
	Engine.Random = m_randomService ? m_randomService.GetSafePtr() : nullptr;
	Engine.Math = m_mathService ? m_mathService.GetSafePtr() : nullptr;
	Engine.Reflection = m_reflectionRegistry ? m_reflectionRegistry.GetSafePtr() : nullptr;
	Engine.Logger = m_logger ? m_logger.GetSafePtr() : nullptr;
#if JBRO_EDITOR
	Engine.Localization = m_localization ? m_localization.GetSafePtr() : nullptr;
#else
	Engine.Localization = nullptr;
#endif
	Engine.ResourceRegistry = m_resourceRegistry ? m_resourceRegistry.GetSafePtr() : nullptr;
	Engine.Network = m_networkManager ? m_networkManager.GetSafePtr() : nullptr;
	Engine.DebugDraw2D = m_debugDraw ? m_debugDraw.GetSafePtr() : nullptr;
	Engine.IsApplicationFocused = m_isApplicationFocused;
	Engine.ApplicationFocusGained = m_applicationFocusGained;
	Engine.ApplicationFocusLost = m_applicationFocusLost;

	Core::Time = Engine.Time;
	Core::Input = Engine.Input;
	Core::SceneManager = Engine.SceneManager;
	Core::FileSystem = Engine.FileSystem;
	Core::AssetManager = Engine.AssetManager;
	Core::TaskManager = Engine.TaskManager;
	Core::Random = Engine.Random;
	Core::Math = Engine.Math;
	Core::Reflection = Engine.Reflection;
	Core::Logger = Engine.Logger;
	Core::Localization = Engine.Localization;
	Core::ResourceRegistry = Engine.ResourceRegistry;
	Core::Network = Engine.Network;
	Core::DebugDraw2D = Engine.DebugDraw2D;
	Core::Debug = Engine.Debug;
}
