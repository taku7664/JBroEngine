#include "pch.h"
#include "Engine.h"

#include "Core/Core.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Input/Input.h"
#include "Core/Module/Module.h"
#include "Core/Asset/AssetManager.h"
#include "Core/Asset/FileAsset.h"
#include "Core/Asset/MaterialAsset.h"
#include "Core/Asset/SpriteAsset.h"
#include "Core/Asset/TextureAsset.h"
#include "Core/Platform/IPlatform.h"
#include "Core/Platform/IRenderSurface.h"
#include "Core/Renderer/Forward2DRenderer.h"
#include "Core/Renderer/RenderScene.h"
#include "Core/Platform/Windows/WindowsPlatform.h"
#include "Core/Platform/Web/WebPlatform.h"
#include "Core/RHI/IRHICommandContext.h"
#include "Core/RHI/IRHIDevice.h"
#include "Core/RHI/D3D11/D3D11RHIDevice.h"
#include "Core/RHI/WebGPU/WebGPURHIDevice.h"
#include "Core/RHI/EmptyRHIDevice.h"
#include "Core/Thread/ThreadService.h"
#include "Core/Time/Time.h"
#include "GameFramework/Component/BuiltinComponentRegistry.h"
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

	if (false == InitializeRenderer())
	{
		return false;
	}

	SyncContext();
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

	if (m_assetManager)
	{
		m_assetManager->Finalize();
		m_assetManager.Reset();
	}

	Core::SceneManager = nullptr;
	m_sceneManager.Reset();
	Core::Thread = nullptr;
	if (m_threadService)
	{
		m_threadService->Finalize();
		m_threadService.Reset();
	}
	Core::Reflection = nullptr;
	m_reflectionRegistry.Reset();
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

	SyncContext();
	m_isInitialized = false;
}

void CEngine::InitializeModule(CModule& module, const char* moduleName)
{
	SyncContext();
	if (std::find(m_modules.begin(), m_modules.end(), &module) == m_modules.end())
	{
		m_modules.push_back(&module);
	}
	module.Initialize(moduleName, m_context);
}

void CEngine::FinalizeModule(CModule& module)
{
	module.Finalize();
	m_modules.erase(std::remove(m_modules.begin(), m_modules.end(), &module), m_modules.end());
}

const EngineContext& CEngine::GetContext() const
{
	return m_context;
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
	m_threadService = MakeOwnerPtr<CThreadService>();
	m_reflectionRegistry = MakeOwnerPtr<CReflectionRegistry>();
	m_sceneManager = MakeOwnerPtr<CSceneManager>();
	if (!m_time || !m_input || !m_fileSystem || !m_threadService || !m_reflectionRegistry || !m_sceneManager)
	{
		return false;
	}

	m_time->Reset();
	if (false == m_fileSystem->Initialize("Assets", EFileSystemAccess::ReadWrite))
	{
		return false;
	}
	if (false == m_threadService->Initialize())
	{
		return false;
	}

	Core::Time = m_time.GetSafePtr();
	Core::Input = m_input.GetSafePtr();
	Core::FileSystem = m_fileSystem.GetSafePtr();
	Core::Thread = m_threadService.GetSafePtr();
	Core::Reflection = m_reflectionRegistry.GetSafePtr();
	Core::SceneManager = m_sceneManager.GetSafePtr();

	RegisterBuiltinComponents(*m_reflectionRegistry);
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

	PlatformDesc desc;
	desc.IsEditor = JBRO_EDITOR != 0;
	return m_platform && m_platform->Initialize(desc);
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
	desc.AssetRootPath = "Assets";
	if (!m_assetManager || false == m_assetManager->Initialize(desc))
	{
		return false;
	}

	m_assetManager->RegisterLoader(MakeOwnerPtr<CFileAssetLoader>(EAssetType::Scene));
	m_assetManager->RegisterLoader(MakeOwnerPtr<CFileAssetLoader>(EAssetType::Prefab));
	m_assetManager->RegisterLoader(MakeOwnerPtr<CFileAssetLoader>(EAssetType::Shader));
	m_assetManager->RegisterLoader(MakeOwnerPtr<CFileAssetLoader>(EAssetType::Script));
	m_assetManager->RegisterLoader(MakeOwnerPtr<CFileAssetLoader>(EAssetType::Custom));
	m_assetManager->RegisterLoader(MakeOwnerPtr<CTextureAssetLoader>());
	m_assetManager->RegisterLoader(MakeOwnerPtr<CSpriteAssetLoader>());
	m_assetManager->RegisterLoader(MakeOwnerPtr<CMaterialAssetLoader>());
	return true;
}

bool CEngine::InitializeRenderer()
{
	m_renderScene = MakeOwnerPtr<CRenderScene>();
	m_renderer = MakeOwnerPtr<CForward2DRenderer>();
	if (!m_renderScene || !m_renderer)
	{
		return false;
	}

	RendererDesc desc;
	desc.RHIDevice = GetRHIDevice();
	return m_renderer->Initialize(desc);
}

void CEngine::BeginFrame()
{
	if (m_time)
	{
		m_time->BeginFrame();
	}
	if (m_input)
	{
		m_input->BeginFrame();
	}
	if (m_threadService)
	{
		m_threadService->ExecuteMainThreadTasks();
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
	if (m_sceneManager)
	{
		m_sceneManager->Update();
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

	m_rhiDevice->BeginFrame();
	PrepareRenderModules();

	SafePtr<IRHICommandContext> commandContext = m_rhiDevice->GetImmediateCommandContext();
	if (commandContext)
	{
		RenderPassDesc renderPassDesc;
		renderPassDesc.ColorAttachment.LoadOp = ERHILoadOp::Clear;
		renderPassDesc.ColorAttachment.StoreOp = ERHIStoreOp::Store;
		renderPassDesc.ColorAttachment.ClearColor = Color{ 0.08f, 0.09f, 0.11f, 1.0f };

		commandContext->BeginRenderPass(renderPassDesc);
		if (m_renderer && m_renderScene)
		{
			m_renderer->Render(*m_renderScene);
			m_renderScene->Clear();
		}
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

void CEngine::SyncContext()
{
	m_context.Platform = GetPlatform();
	m_context.MainRenderSurface = GetMainRenderSurface();
	m_context.RHIDevice = GetRHIDevice();
	m_context.AssetManager = GetAssetManager();
	m_context.Renderer = GetRenderer();
	m_context.RenderScene = GetRenderScene();
	m_context.Time = m_time ? m_time.GetSafePtr() : nullptr;
	m_context.Input = m_input ? m_input.GetSafePtr() : nullptr;
	m_context.SceneManager = m_sceneManager ? m_sceneManager.GetSafePtr() : nullptr;
	m_context.FileSystem = m_fileSystem ? m_fileSystem.GetSafePtr() : nullptr;
	m_context.Thread = m_threadService ? m_threadService.GetSafePtr() : nullptr;
	m_context.Reflection = m_reflectionRegistry ? m_reflectionRegistry.GetSafePtr() : nullptr;
}
