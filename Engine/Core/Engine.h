#pragma once

#include "Core/EngineContext.h"
#include "Core/Platform/PlatformTypes.h"
#include "Core/RHI/RHITypes.h"

#include <vector>

class CModule;
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

	void InitializeModule(CModule& module, const char* moduleName);
	void FinalizeModule(CModule& module);

	const EngineContext& GetContext() const;
	SafePtr<IPlatform> GetPlatform() const;
	SafePtr<IRenderSurface> GetMainRenderSurface() const;
	SafePtr<IRHIDevice> GetRHIDevice() const;
	SafePtr<IAssetManager> GetAssetManager() const;
	SafePtr<IRenderer> GetRenderer() const;
	SafePtr<IRenderScene> GetRenderScene() const;

private:
	bool InitializePlatform();
	bool InitializeRHI();
	bool InitializeAssetManager();
	bool InitializeRenderer();
	bool InitializeCoreServices();
	void BeginFrame();
	void UpdateModules();
	void UpdateCoreServices();
	void PrepareRenderModules();
	void RenderFrame();
	void EndFrame();
	void FillRenderSurfaceDesc(RHIDesc& desc) const;
	void SyncContext();

private:
	OwnerPtr<IPlatform> m_platform;
	OwnerPtr<IRHIDevice> m_rhiDevice;
	OwnerPtr<IAssetManager> m_assetManager;
	OwnerPtr<IRenderer> m_renderer;
	OwnerPtr<IRenderScene> m_renderScene;
	OwnerPtr<CTime> m_time;
	OwnerPtr<CInput> m_input;
	OwnerPtr<CFileSystem> m_fileSystem;
	OwnerPtr<CThreadService> m_threadService;
	OwnerPtr<CReflectionRegistry> m_reflectionRegistry;
	OwnerPtr<CSceneManager> m_sceneManager;
	std::vector<CModule*> m_modules;
	EngineContext m_context;
	bool m_isInitialized = false;
};
